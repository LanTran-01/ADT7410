/**
 * @file    adt7410_driver.c
 * @brief   Analog Devices ADT7410 16-Bit Digital I2C Temperature Sensor Driver.
 * @req     LLR-ADT-001, LLR-ADT-002, LLR-ADT-003, LLR-ADT-004, LLR-ADT-005
 * @design  SDD-ADT-1.0
 */

#include "adt7410_driver.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "axi_iic_hal.h"

/* Project assertion macro: compiles out in flight/release builds
 * [DO §6.4.4] */
#if defined(DEBUG_BUILD)
#define ASSERT(expr) do { if (!(expr)) { /* trap */ } } while (0)
#else /* DEBUG_BUILD */
#define ASSERT(expr) ((void)0)
#endif /* DEBUG_BUILD */

/* Register map (Datasheet §REGISTER MAP, Address Pointer Register) */
#define ADT7410_REG_TEMP_MSB        (0x00U) /* Temperature value MSB/LSB   */
#define ADT7410_REG_STATUS          (0x02U) /* Status register             */
#define ADT7410_REG_CONFIG          (0x03U) /* Configuration register      */
#define ADT7410_REG_THIGH_MSB       (0x04U) /* THIGH setpoint MSB/LSB      */
#define ADT7410_REG_TLOW_MSB        (0x06U) /* TLOW setpoint MSB/LSB       */
#define ADT7410_REG_TCRIT_MSB       (0x08U) /* TCRIT setpoint MSB/LSB      */
#define ADT7410_REG_THYST           (0x0AU) /* THYST setpoint              */
#define ADT7410_REG_ID              (0x0BU) /* Manufacturer / revision ID  */
#define ADT7410_CMD_SOFT_RESET      (0x2FU) /* Software reset (§RESET)     */

/* Configuration Register 0x03 fields (Datasheet Table 11) */
#define ADT7410_CFG_RES_SHIFT       (7U)    /* Bit 7: resolution           */
#define ADT7410_CFG_RES_MASK        (0x80U)
#define ADT7410_CFG_OP_MODE_SHIFT   (5U)    /* Bits [6:5]: operating mode  */
#define ADT7410_CFG_OP_MODE_MASK    (0x60U)
#define ADT7410_CFG_EVENT_SHIFT     (4U)    /* Bit 4: INT / CT mode        */
#define ADT7410_CFG_EVENT_MASK      (0x10U)
#define ADT7410_CFG_INT_POL_SHIFT   (3U)    /* Bit 3: INT pin polarity     */
#define ADT7410_CFG_INT_POL_MASK    (0x08U)
#define ADT7410_CFG_CT_POL_SHIFT    (2U)    /* Bit 2: CT pin polarity      */
#define ADT7410_CFG_CT_POL_MASK     (0x04U)
#define ADT7410_CFG_FAULT_Q_MASK    (0x03U) /* Bits [1:0]: fault queue     */

/* Largest legal enum value per config field (= field all-ones) */
#define ADT7410_RES_FIELD_MAX       ((uint32_t)ADT7410_RES_16BIT)
#define ADT7410_OP_MODE_FIELD_MAX   ((uint32_t)ADT7410_MODE_SHUTDOWN)
#define ADT7410_EVENT_FIELD_MAX     ((uint32_t)ADT7410_EVENT_COMPARATOR)
#define ADT7410_POL_FIELD_MAX       ((uint32_t)ADT7410_POL_ACTIVE_HIGH)
#define ADT7410_FAULT_Q_FIELD_MAX   ((uint32_t)ADT7410_FAULT_QUEUE_4)

/* Lookup table sizes: one entry per 2-bit field code */
#define ADT7410_OP_MODE_COUNT       (4U)
#define ADT7410_FAULT_QUEUE_COUNT   (4U)

/* Status Register 0x02 bits (Datasheet Table 10) */
#define ADT7410_STATUS_NRDY_MASK    (0x80U) /* Bit 7: RDY, active low      */
#define ADT7410_STATUS_TCRIT_MASK   (0x40U) /* Bit 6: T >= TCRIT           */
#define ADT7410_STATUS_THIGH_MASK   (0x20U) /* Bit 5: T >= THIGH           */
#define ADT7410_STATUS_TLOW_MASK    (0x10U) /* Bit 4: T <= TLOW            */

/* Temperature Value Register, 13-bit mode (Datasheet §TEMPERATURE VALUE) */
#define ADT7410_TEMP_13BIT_DATA_MASK (0xFFF8U) /* Bits [15:3]: temperature */
#define ADT7410_TEMP_FLAG_TCRIT_MASK (0x0004U) /* Bit 2: TCRIT flag        */
#define ADT7410_TEMP_FLAG_THIGH_MASK (0x0002U) /* Bit 1: THIGH flag        */
#define ADT7410_TEMP_FLAG_TLOW_MASK  (0x0001U) /* Bit 0: TLOW flag         */

/* ID Register 0x0B (Datasheet Table 19) */
#define ADT7410_ID_MFG_SHIFT        (3U)    /* Bits [7:3]: manufacturer ID */
#define ADT7410_ID_MFG_MASK         (0x1FU) /* 5-bit field after shift     */
#define ADT7410_ID_REV_MASK         (0x07U) /* Bits [2:0]: silicon rev     */

/* THYST Register 0x0A: bits [3:0] = hysteresis, 1 degC/LSB */
#define ADT7410_THYST_DEGC_MASK     (0x0FU)

/* Big-endian 16-bit register serialisation */
#define ADT7410_BYTE_SHIFT          (8U)
#define ADT7410_BYTE_MASK           (0xFFU)
#define ADT7410_REG16_SIGN_MASK     (0x8000U) /* Two's complement sign bit */
#define ADT7410_REG16_MODULUS       (65536)   /* 2^16, int32_t arithmetic  */

/* Fixed-point scaling: Q8.7 degC (degC = q / 128) <-> mdegC / float degC */
#define ADT7410_Q7_LSB_PER_DEGC     (128)        /* 2^7 LSB per degC        */
#define ADT7410_Q7_ROUND_HALF       (64)         /* 128 / 2, round half     */
#define ADT7410_MDEGC_PER_DEGC      (1000)       /* milli-degC per degC     */
#define ADT7410_MDEGC_ROUND_HALF    (500)        /* 1000 / 2, round half    */
#define ADT7410_DEGC_PER_Q8_7_LSB   (0.0078125f) /* 2^-7 degC per Q8.7 LSB  */

/* I2C frame lengths in bytes: pointer byte + payload */
#define ADT7410_CMD_LEN             (1U)   /* Pointer/command byte only    */
#define ADT7410_REG8_WRITE_LEN      (2U)   /* Pointer + data               */
#define ADT7410_REG16_WRITE_LEN     (3U)   /* Pointer + MSB + LSB          */
#define ADT7410_REG8_READ_LEN       (1U)   /* Data                         */
#define ADT7410_REG16_READ_LEN      (2U)   /* MSB + LSB (auto-increment)   */

/* Static helper prototypes */
static adt7410_status_t map_iic_status(axi_iic_status_t iic_status);
static adt7410_status_t write_reg8(axi_iic_dev_t const * p_iic_bus,
                                   uint8_t slave_addr_7b,
                                   uint8_t reg_addr,
                                   uint8_t reg_byte);
static adt7410_status_t write_reg16(axi_iic_dev_t const * p_iic_bus,
                                    uint8_t slave_addr_7b,
                                    uint8_t reg_addr,
                                    uint16_t reg_word);
static adt7410_status_t read_reg8(axi_iic_dev_t const * p_iic_bus,
                                  uint8_t slave_addr_7b,
                                  uint8_t reg_addr,
                                  uint8_t * p_reg_byte);
static adt7410_status_t read_reg16(axi_iic_dev_t const * p_iic_bus,
                                   uint8_t slave_addr_7b,
                                   uint8_t reg_addr,
                                   uint16_t * p_reg_word);
static adt7410_status_t read_id_fields(axi_iic_dev_t const * p_iic_bus,
                                       uint8_t slave_addr_7b,
                                       uint8_t * p_mfg_id,
                                       uint8_t * p_silicon_rev);
static bool is_slave_addr_valid(uint8_t slave_addr_7b);
static bool is_temp_in_range(int32_t temp_mdegc);
static bool is_config_valid(adt7410_config_t const * p_cfg);
static uint8_t config_to_reg(adt7410_config_t const * p_cfg);
static void reg_to_config(uint8_t cfg_reg, adt7410_config_t * p_cfg);
static void reg_to_status_flags(uint8_t status_reg,
                                adt7410_status_flags_t * p_flags);
static adt7410_status_t setpoint_to_reg(adt7410_setpoint_id_t setpoint_id,
                                        uint8_t * p_reg_addr);
static int32_t reg16_to_q8_7(uint16_t reg_word);
static uint16_t q8_7_to_reg16(int32_t temp_q8_7);
static int32_t q8_7_to_mdegc(int32_t temp_q8_7);
static int32_t mdegc_to_q8_7(int32_t temp_mdegc);
static float q8_7_to_degc(int16_t temp_q8_7);
static adt7410_status_t reg16_to_sample(uint16_t reg_word,
                                        adt7410_resolution_t active_res,
                                        adt7410_sample_t * p_sample);

/**
 * @brief   Config bits [6:5] code -> operating mode (Datasheet Table 11).
 * @req     LLR-ADT-002
 * @design  SDD-ADT-3.1
 */
static adt7410_op_mode_t const op_mode_lut[ADT7410_OP_MODE_COUNT] =
{
    ADT7410_MODE_CONTINUOUS,
    ADT7410_MODE_ONE_SHOT,
    ADT7410_MODE_1SPS,
    ADT7410_MODE_SHUTDOWN
};

/**
 * @brief   Config bits [1:0] code -> fault queue depth (Datasheet Table 11).
 * @req     LLR-ADT-002
 * @design  SDD-ADT-3.2
 */
static adt7410_fault_queue_t const fault_queue_lut[ADT7410_FAULT_QUEUE_COUNT] =
{
    ADT7410_FAULT_QUEUE_1,
    ADT7410_FAULT_QUEUE_2,
    ADT7410_FAULT_QUEUE_3,
    ADT7410_FAULT_QUEUE_4
};


/* Public API implementations (Doxygen contract in adt7410_driver.h) */

adt7410_status_t adt7410_init(adt7410_dev_t * p_dev,
                              axi_iic_dev_t const * p_iic_bus,
                              uint8_t slave_addr_7b,
                              adt7410_config_t const * p_cfg)
{
    adt7410_status_t result = ADT7410_OK;
    uint8_t mfg_id = 0U;
    uint8_t silicon_rev = 0U;
    uint8_t cfg_reg = 0U;
    bool const is_addr_ok = is_slave_addr_valid(slave_addr_7b);

    if (NULL == p_dev)
    {
        result = ADT7410_ERR_PARAM;
    }
    else
    {
        /* Mark unusable first so that every failure below leaves UNINIT */
        p_dev->init_state = ADT7410_STATE_UNINIT;

        if ((NULL == p_iic_bus)
            || (NULL == p_cfg)
            || (!is_addr_ok))
        {
            result = ADT7410_ERR_PARAM;
        }
        else if (!is_config_valid(p_cfg))
        {
            result = ADT7410_ERR_PARAM;
        }
        else
        {
            p_dev->p_iic_bus = p_iic_bus;
            p_dev->slave_addr_7b = slave_addr_7b;
            p_dev->active_res = ADT7410_RES_13BIT; /* Power-on default */
            result = read_id_fields(p_iic_bus,
                                    slave_addr_7b,
                                    &mfg_id,
                                    &silicon_rev);
        }
    }

    if (ADT7410_OK == result)
    {
        cfg_reg = config_to_reg(p_cfg);
        result = write_reg8(p_iic_bus,
                            slave_addr_7b,
                            ADT7410_REG_CONFIG,
                            cfg_reg);
    }
    else
    {
        /* Parameter, communication, or silicon ID failure propagated */
    }

    if (ADT7410_OK == result)
    {
        p_dev->active_res = p_cfg->resolution;
        p_dev->init_state = ADT7410_STATE_READY;
    }
    else
    {
        /* init_state stays UNINIT (or p_dev is NULL) */
    }

    return result;
}


adt7410_status_t adt7410_soft_reset(adt7410_dev_t * p_dev)
{
    adt7410_status_t result = ADT7410_OK;
    axi_iic_status_t iic_status = AXI_IIC_OK;
    uint8_t const cmd_bytes[ADT7410_CMD_LEN] = { ADT7410_CMD_SOFT_RESET };

    if (NULL == p_dev)
    {
        result = ADT7410_ERR_PARAM;
    }
    else if (ADT7410_STATE_READY != p_dev->init_state)
    {
        result = ADT7410_ERR_STATE;
    }
    else
    {
        iic_status = axi_iic_write_bytes(p_dev->p_iic_bus,
                                         p_dev->slave_addr_7b,
                                         cmd_bytes,
                                         ADT7410_CMD_LEN);
        result = map_iic_status(iic_status);

        if (ADT7410_OK == result)
        {
            /* Reset restores Config 0x03 = 0x00, i.e. 13-bit resolution */
            p_dev->active_res = ADT7410_RES_13BIT;
        }
        else
        {
            /* Command not acknowledged: device state unchanged */
        }
    }

    return result;
}


adt7410_status_t adt7410_read_id(adt7410_dev_t const * p_dev,
                                 uint8_t * p_mfg_id,
                                 uint8_t * p_silicon_rev)
{
    adt7410_status_t result = ADT7410_OK;

    if ((NULL == p_dev)
        || (NULL == p_mfg_id)
        || (NULL == p_silicon_rev))
    {
        result = ADT7410_ERR_PARAM;
    }
    else if (ADT7410_STATE_READY != p_dev->init_state)
    {
        result = ADT7410_ERR_STATE;
    }
    else
    {
        result = read_id_fields(p_dev->p_iic_bus,
                                p_dev->slave_addr_7b,
                                p_mfg_id,
                                p_silicon_rev);
    }

    return result;
}


adt7410_status_t adt7410_set_config(adt7410_dev_t * p_dev,
                                    adt7410_config_t const * p_cfg)
{
    adt7410_status_t result = ADT7410_OK;
    uint8_t cfg_reg = 0U;

    if ((NULL == p_dev)
        || (NULL == p_cfg))
    {
        result = ADT7410_ERR_PARAM;
    }
    else if (!is_config_valid(p_cfg))
    {
        result = ADT7410_ERR_PARAM;
    }
    else if (ADT7410_STATE_READY != p_dev->init_state)
    {
        result = ADT7410_ERR_STATE;
    }
    else
    {
        cfg_reg = config_to_reg(p_cfg);
        result = write_reg8(p_dev->p_iic_bus,
                            p_dev->slave_addr_7b,
                            ADT7410_REG_CONFIG,
                            cfg_reg);

        if (ADT7410_OK == result)
        {
            p_dev->active_res = p_cfg->resolution;
        }
        else
        {
            /* Write failed: keep previous resolution mirror */
        }
    }

    return result;
}


adt7410_status_t adt7410_get_config(adt7410_dev_t const * p_dev,
                                    adt7410_config_t * p_cfg)
{
    adt7410_status_t result = ADT7410_OK;
    uint8_t cfg_reg = 0U;

    if ((NULL == p_dev)
        || (NULL == p_cfg))
    {
        result = ADT7410_ERR_PARAM;
    }
    else if (ADT7410_STATE_READY != p_dev->init_state)
    {
        result = ADT7410_ERR_STATE;
    }
    else
    {
        result = read_reg8(p_dev->p_iic_bus,
                           p_dev->slave_addr_7b,
                           ADT7410_REG_CONFIG,
                           &cfg_reg);

        if (ADT7410_OK == result)
        {
            reg_to_config(cfg_reg, p_cfg);
        }
        else
        {
            /* Communication failure propagated; p_cfg untouched */
        }
    }

    return result;
}


adt7410_status_t adt7410_read_status(adt7410_dev_t const * p_dev,
                                     adt7410_status_flags_t * p_flags)
{
    adt7410_status_t result = ADT7410_OK;
    uint8_t status_reg = 0U;

    if ((NULL == p_dev)
        || (NULL == p_flags))
    {
        result = ADT7410_ERR_PARAM;
    }
    else if (ADT7410_STATE_READY != p_dev->init_state)
    {
        result = ADT7410_ERR_STATE;
    }
    else
    {
        result = read_reg8(p_dev->p_iic_bus,
                           p_dev->slave_addr_7b,
                           ADT7410_REG_STATUS,
                           &status_reg);

        if (ADT7410_OK == result)
        {
            reg_to_status_flags(status_reg, p_flags);
        }
        else
        {
            /* Communication failure propagated; p_flags untouched */
        }
    }

    return result;
}


adt7410_status_t adt7410_trigger_one_shot(adt7410_dev_t const * p_dev)
{
    adt7410_status_t result = ADT7410_OK;
    uint8_t cfg_reg = 0U;
    uint32_t cfg_bits = 0U;

    if (NULL == p_dev)
    {
        result = ADT7410_ERR_PARAM;
    }
    else if (ADT7410_STATE_READY != p_dev->init_state)
    {
        result = ADT7410_ERR_STATE;
    }
    else
    {
        result = read_reg8(p_dev->p_iic_bus,
                           p_dev->slave_addr_7b,
                           ADT7410_REG_CONFIG,
                           &cfg_reg);

        if (ADT7410_OK == result)
        {
            /* Keep resolution, event, polarity and fault-queue bits;
             * replace only the operating-mode field [6:5]. */
            cfg_bits = (uint32_t)cfg_reg & ~ADT7410_CFG_OP_MODE_MASK;
            cfg_bits |= ((uint32_t)ADT7410_MODE_ONE_SHOT
                         << ADT7410_CFG_OP_MODE_SHIFT);
            cfg_reg = (uint8_t)(cfg_bits & ADT7410_BYTE_MASK);
            result = write_reg8(p_dev->p_iic_bus,
                                p_dev->slave_addr_7b,
                                ADT7410_REG_CONFIG,
                                cfg_reg);
        }
        else
        {
            /* Communication failure propagated; no write issued */
        }
    }

    return result;
}


adt7410_status_t adt7410_read_temperature(adt7410_dev_t const * p_dev,
                                          adt7410_sample_t * p_sample)
{
    adt7410_status_t result = ADT7410_OK;
    uint16_t reg_word = 0U;

    if ((NULL == p_dev)
        || (NULL == p_sample))
    {
        result = ADT7410_ERR_PARAM;
    }
    else if (ADT7410_STATE_READY != p_dev->init_state)
    {
        result = ADT7410_ERR_STATE;
    }
    else
    {
        result = read_reg16(p_dev->p_iic_bus,
                            p_dev->slave_addr_7b,
                            ADT7410_REG_TEMP_MSB,
                            &reg_word);

        if (ADT7410_OK == result)
        {
            result = reg16_to_sample(reg_word, p_dev->active_res, p_sample);
        }
        else
        {
            /* Communication failure propagated; p_sample untouched */
        }
    }

    return result;
}


adt7410_status_t adt7410_read_temp_degc(adt7410_dev_t const * p_dev,
                                        float * p_die_temp_degc)
{
    adt7410_status_t result = ADT7410_OK;
    adt7410_sample_t sample =
    {
        .die_temp_mdegc   = 0,
        .die_temp_q8_7    = 0,
        .has_inline_flags = false,
        .is_tcrit_active  = false,
        .is_thigh_active  = false,
        .is_tlow_active   = false
    };

    if ((NULL == p_dev)
        || (NULL == p_die_temp_degc))
    {
        result = ADT7410_ERR_PARAM;
    }
    else
    {
        result = adt7410_read_temperature(p_dev, &sample);

        if (ADT7410_OK == result)
        {
            *p_die_temp_degc = q8_7_to_degc(sample.die_temp_q8_7);
        }
        else
        {
            /* Error propagated; *p_die_temp_degc untouched */
        }
    }

    return result;
}


adt7410_status_t adt7410_set_temp_setpoint(adt7410_dev_t const * p_dev,
                                           adt7410_setpoint_id_t setpoint_id,
                                           int32_t limit_temp_mdegc)
{
    adt7410_status_t result = ADT7410_OK;
    uint8_t reg_addr = 0U;
    int32_t limit_q8_7 = 0;
    uint16_t reg_word = 0U;
    bool const is_range_ok = is_temp_in_range(limit_temp_mdegc);

    if (NULL == p_dev)
    {
        result = ADT7410_ERR_PARAM;
    }
    else if (!is_range_ok)
    {
        result = ADT7410_ERR_PARAM;
    }
    else if (ADT7410_STATE_READY != p_dev->init_state)
    {
        result = ADT7410_ERR_STATE;
    }
    else
    {
        result = setpoint_to_reg(setpoint_id, &reg_addr);

        if (ADT7410_OK == result)
        {
            limit_q8_7 = mdegc_to_q8_7(limit_temp_mdegc);
            reg_word = q8_7_to_reg16(limit_q8_7);
            result = write_reg16(p_dev->p_iic_bus,
                                 p_dev->slave_addr_7b,
                                 reg_addr,
                                 reg_word);
        }
        else
        {
            /* Invalid setpoint selector propagated */
        }
    }

    return result;
}


adt7410_status_t adt7410_get_temp_setpoint(adt7410_dev_t const * p_dev,
                                           adt7410_setpoint_id_t setpoint_id,
                                           int32_t * p_limit_temp_mdegc)
{
    adt7410_status_t result = ADT7410_OK;
    uint8_t reg_addr = 0U;
    uint16_t reg_word = 0U;
    int32_t limit_q8_7 = 0;

    if ((NULL == p_dev)
        || (NULL == p_limit_temp_mdegc))
    {
        result = ADT7410_ERR_PARAM;
    }
    else if (ADT7410_STATE_READY != p_dev->init_state)
    {
        result = ADT7410_ERR_STATE;
    }
    else
    {
        result = setpoint_to_reg(setpoint_id, &reg_addr);

        if (ADT7410_OK == result)
        {
            result = read_reg16(p_dev->p_iic_bus,
                                p_dev->slave_addr_7b,
                                reg_addr,
                                &reg_word);
        }
        else
        {
            /* Invalid setpoint selector propagated */
        }

        if (ADT7410_OK == result)
        {
            limit_q8_7 = reg16_to_q8_7(reg_word);
            *p_limit_temp_mdegc = q8_7_to_mdegc(limit_q8_7);
        }
        else
        {
            /* Error propagated; output untouched */
        }
    }

    return result;
}


adt7410_status_t adt7410_set_hysteresis(adt7410_dev_t const * p_dev,
                                        uint8_t hyst_degc)
{
    adt7410_status_t result = ADT7410_OK;

    if (NULL == p_dev)
    {
        result = ADT7410_ERR_PARAM;
    }
    else if (hyst_degc > ADT7410_HYST_MAX_DEGC)
    {
        result = ADT7410_ERR_PARAM;
    }
    else if (ADT7410_STATE_READY != p_dev->init_state)
    {
        result = ADT7410_ERR_STATE;
    }
    else
    {
        /* Bits [7:4] are unused and written as 0 */
        result = write_reg8(p_dev->p_iic_bus,
                            p_dev->slave_addr_7b,
                            ADT7410_REG_THYST,
                            hyst_degc);
    }

    return result;
}


adt7410_status_t adt7410_get_hysteresis(adt7410_dev_t const * p_dev,
                                        uint8_t * p_hyst_degc)
{
    adt7410_status_t result = ADT7410_OK;
    uint8_t thyst_reg = 0U;

    if ((NULL == p_dev)
        || (NULL == p_hyst_degc))
    {
        result = ADT7410_ERR_PARAM;
    }
    else if (ADT7410_STATE_READY != p_dev->init_state)
    {
        result = ADT7410_ERR_STATE;
    }
    else
    {
        result = read_reg8(p_dev->p_iic_bus,
                           p_dev->slave_addr_7b,
                           ADT7410_REG_THYST,
                           &thyst_reg);

        if (ADT7410_OK == result)
        {
            *p_hyst_degc = (uint8_t)((uint32_t)thyst_reg
                                     & ADT7410_THYST_DEGC_MASK);
        }
        else
        {
            /* Communication failure propagated; output untouched */
        }
    }

    return result;
}


/* Private helper implementations */

/**
 * @brief       Map an AXI IIC HAL status onto the driver status domain.
 * @req         LLR-ADT-001
 * @design      SDD-ADT-3.3
 * @param[in]   iic_status  Status returned by the AXI IIC HAL.
 * @return      ADT7410_OK or ADT7410_ERR_I2C_COMM.
 * @note        Every HAL fault (PARAM, STATE, BUS_BUSY, NACK_ARB, TIMEOUT)
 *              is reported as a communication failure. ISR-safe. Reentrant.
 */
static adt7410_status_t map_iic_status(axi_iic_status_t iic_status)
{
    adt7410_status_t result = ADT7410_ERR_I2C_COMM;

    if (AXI_IIC_OK == iic_status)
    {
        result = ADT7410_OK;
    }
    else
    {
        result = ADT7410_ERR_I2C_COMM;
    }

    return result;
}


/**
 * @brief       Write one 8-bit ADT7410 register.
 * @req         LLR-ADT-002, LLR-ADT-005
 * @design      SDD-ADT-3.4
 * @param[in]   p_iic_bus      Initialized AXI IIC HAL instance.
 * @param[in]   slave_addr_7b  7-bit slave address, 0x48..0x4B.
 * @param[in]   reg_addr       Register pointer byte.
 * @param[in]   reg_byte       Register contents to write.
 * @return      ADT7410_OK or ADT7410_ERR_I2C_COMM.
 * @note        Blocking (1 polled I2C transfer). Not ISR-safe.
 */
static adt7410_status_t write_reg8(axi_iic_dev_t const * p_iic_bus,
                                   uint8_t slave_addr_7b,
                                   uint8_t reg_addr,
                                   uint8_t reg_byte)
{
    adt7410_status_t result = ADT7410_OK;
    axi_iic_status_t iic_status = AXI_IIC_OK;
    uint8_t const tx_bytes[ADT7410_REG8_WRITE_LEN] = { reg_addr, reg_byte };

    ASSERT(NULL != p_iic_bus);

    iic_status = axi_iic_write_bytes(p_iic_bus,
                                     slave_addr_7b,
                                     tx_bytes,
                                     ADT7410_REG8_WRITE_LEN);
    result = map_iic_status(iic_status);

    return result;
}


/**
 * @brief       Write one 16-bit ADT7410 register, MSB first.
 * @req         LLR-ADT-004
 * @design      SDD-ADT-3.5
 * @param[in]   p_iic_bus      Initialized AXI IIC HAL instance.
 * @param[in]   slave_addr_7b  7-bit slave address, 0x48..0x4B.
 * @param[in]   reg_addr       Register pointer byte (MSB address).
 * @param[in]   reg_word       16-bit register contents to write.
 * @return      ADT7410_OK or ADT7410_ERR_I2C_COMM.
 * @note        Explicit byte serialisation fixes endianness (§8).
 *              Blocking (1 polled I2C transfer). Not ISR-safe.
 */
static adt7410_status_t write_reg16(axi_iic_dev_t const * p_iic_bus,
                                    uint8_t slave_addr_7b,
                                    uint8_t reg_addr,
                                    uint16_t reg_word)
{
    adt7410_status_t result = ADT7410_OK;
    axi_iic_status_t iic_status = AXI_IIC_OK;
    uint8_t const reg_msb = (uint8_t)(((uint32_t)reg_word
                                       >> ADT7410_BYTE_SHIFT)
                                      & ADT7410_BYTE_MASK);
    uint8_t const reg_lsb = (uint8_t)((uint32_t)reg_word
                                      & ADT7410_BYTE_MASK);
    uint8_t const tx_bytes[ADT7410_REG16_WRITE_LEN] =
    {
        reg_addr,
        reg_msb,
        reg_lsb
    };

    ASSERT(NULL != p_iic_bus);

    iic_status = axi_iic_write_bytes(p_iic_bus,
                                     slave_addr_7b,
                                     tx_bytes,
                                     ADT7410_REG16_WRITE_LEN);
    result = map_iic_status(iic_status);

    return result;
}


/**
 * @brief       Read one 8-bit ADT7410 register.
 * @req         LLR-ADT-001, LLR-ADT-002, LLR-ADT-003, LLR-ADT-005
 * @design      SDD-ADT-3.6
 * @param[in]   p_iic_bus      Initialized AXI IIC HAL instance.
 * @param[in]   slave_addr_7b  7-bit slave address, 0x48..0x4B.
 * @param[in]   reg_addr       Register pointer byte.
 * @param[out]  p_reg_byte     Receives register contents on success.
 * @return      ADT7410_OK or ADT7410_ERR_I2C_COMM.
 * @note        Blocking (1 polled I2C transfer). Not ISR-safe.
 */
static adt7410_status_t read_reg8(axi_iic_dev_t const * p_iic_bus,
                                  uint8_t slave_addr_7b,
                                  uint8_t reg_addr,
                                  uint8_t * p_reg_byte)
{
    adt7410_status_t result = ADT7410_OK;
    axi_iic_status_t iic_status = AXI_IIC_OK;
    uint8_t rx_bytes[ADT7410_REG8_READ_LEN] = { 0U };

    ASSERT(NULL != p_iic_bus);
    ASSERT(NULL != p_reg_byte);

    iic_status = axi_iic_write_read_bytes(p_iic_bus,
                                          slave_addr_7b,
                                          reg_addr,
                                          rx_bytes,
                                          ADT7410_REG8_READ_LEN);
    result = map_iic_status(iic_status);

    if (ADT7410_OK == result)
    {
        *p_reg_byte = rx_bytes[0U];
    }
    else
    {
        /* Output untouched on failure */
    }

    return result;
}


/**
 * @brief       Read one 16-bit ADT7410 register, MSB first.
 * @req         LLR-ADT-003, LLR-ADT-004
 * @design      SDD-ADT-3.7
 * @param[in]   p_iic_bus      Initialized AXI IIC HAL instance.
 * @param[in]   slave_addr_7b  7-bit slave address, 0x48..0x4B.
 * @param[in]   reg_addr       Register pointer byte (MSB address).
 * @param[out]  p_reg_word     Receives big-endian register word on success.
 * @return      ADT7410_OK or ADT7410_ERR_I2C_COMM.
 * @note        Explicit byte deserialisation fixes endianness (§8).
 *              Blocking (1 polled I2C transfer). Not ISR-safe.
 */
static adt7410_status_t read_reg16(axi_iic_dev_t const * p_iic_bus,
                                   uint8_t slave_addr_7b,
                                   uint8_t reg_addr,
                                   uint16_t * p_reg_word)
{
    adt7410_status_t result = ADT7410_OK;
    axi_iic_status_t iic_status = AXI_IIC_OK;
    uint8_t rx_bytes[ADT7410_REG16_READ_LEN] = { 0U, 0U };

    ASSERT(NULL != p_iic_bus);
    ASSERT(NULL != p_reg_word);

    iic_status = axi_iic_write_read_bytes(p_iic_bus,
                                          slave_addr_7b,
                                          reg_addr,
                                          rx_bytes,
                                          ADT7410_REG16_READ_LEN);
    result = map_iic_status(iic_status);

    if (ADT7410_OK == result)
    {
        /* rx_bytes[0] = MSB, rx_bytes[1] = LSB */
        *p_reg_word = (uint16_t)(((uint32_t)rx_bytes[0U]
                                  << ADT7410_BYTE_SHIFT)
                                 | (uint32_t)rx_bytes[1U]);
    }
    else
    {
        /* Output untouched on failure */
    }

    return result;
}


/**
 * @brief       Read Register 0x0B and split / verify its ID fields.
 * @req         LLR-ADT-001
 * @design      SDD-ADT-3.8
 * @param[in]   p_iic_bus      Initialized AXI IIC HAL instance.
 * @param[in]   slave_addr_7b  7-bit slave address, 0x48..0x4B.
 * @param[out]  p_mfg_id       Receives 5-bit manufacturer ID.
 * @param[out]  p_silicon_rev  Receives 3-bit silicon revision.
 * @return      ADT7410_OK, ADT7410_ERR_I2C_COMM, or ADT7410_ERR_DEV_ID.
 * @note        Shared by adt7410_init() (before READY) and
 *              adt7410_read_id(). Blocking. Not ISR-safe.
 */
static adt7410_status_t read_id_fields(axi_iic_dev_t const * p_iic_bus,
                                       uint8_t slave_addr_7b,
                                       uint8_t * p_mfg_id,
                                       uint8_t * p_silicon_rev)
{
    adt7410_status_t result = ADT7410_OK;
    uint8_t id_reg = 0U;
    uint32_t id_bits = 0U;

    ASSERT(NULL != p_iic_bus);
    ASSERT(NULL != p_mfg_id);
    ASSERT(NULL != p_silicon_rev);

    result = read_reg8(p_iic_bus, slave_addr_7b, ADT7410_REG_ID, &id_reg);

    if (ADT7410_OK == result)
    {
        id_bits = (uint32_t)id_reg;
        *p_mfg_id = (uint8_t)((id_bits >> ADT7410_ID_MFG_SHIFT)
                              & ADT7410_ID_MFG_MASK);
        *p_silicon_rev = (uint8_t)(id_bits & ADT7410_ID_REV_MASK);

        if (ADT7410_EXPECTED_MFG_ID != *p_mfg_id)
        {
            result = ADT7410_ERR_DEV_ID;
        }
        else
        {
            /* Genuine ADT7410 silicon */
        }
    }
    else
    {
        /* Communication failure propagated */
    }

    return result;
}


/**
 * @brief       Check that a 7-bit address is one of 0x48..0x4B.
 * @req         LLR-ADT-001
 * @design      SDD-ADT-3.9
 * @param[in]   slave_addr_7b  Candidate 7-bit slave address.
 * @return      true if the address is an ADT7410 strap option.
 * @note        Nested decisions keep each condition uncoupled for MC/DC.
 *              ISR-safe. Reentrant.
 */
static bool is_slave_addr_valid(uint8_t slave_addr_7b)
{
    bool is_valid = false;

    if (ADT7410_I2C_ADDR_A1L_A0L <= slave_addr_7b)
    {
        if (ADT7410_I2C_ADDR_A1H_A0H >= slave_addr_7b)
        {
            is_valid = true;
        }
        else
        {
            /* Above 0x4B */
        }
    }
    else
    {
        /* Below 0x48 */
    }

    return is_valid;
}


/**
 * @brief       Check a temperature against the ADT7410 operating range.
 * @req         LLR-ADT-004
 * @design      SDD-ADT-3.10
 * @param[in]   temp_mdegc  Candidate temperature, mdegC.
 * @return      true if within -55000..150000 mdegC.
 * @note        Nested decisions keep each condition uncoupled for MC/DC.
 *              ISR-safe. Reentrant.
 */
static bool is_temp_in_range(int32_t temp_mdegc)
{
    bool is_valid = false;

    if (ADT7410_TEMP_MIN_MDEGC <= temp_mdegc)
    {
        if (ADT7410_TEMP_MAX_MDEGC >= temp_mdegc)
        {
            is_valid = true;
        }
        else
        {
            /* Above +150 degC */
        }
    }
    else
    {
        /* Below -55 degC */
    }

    return is_valid;
}


/**
 * @brief       Check every configuration field fits its register field.
 * @req         LLR-ADT-002
 * @design      SDD-ADT-3.11
 * @param[in]   p_cfg  Configuration to validate, non-NULL.
 * @return      true if every enum field holds a legal encoding.
 * @note        A C enum object can hold any int, so values from the caller
 *              are range-checked before they are shifted into Reg 0x03.
 *              ISR-safe. Reentrant.
 */
static bool is_config_valid(adt7410_config_t const * p_cfg)
{
    bool is_valid = false;

    ASSERT(NULL != p_cfg);

    if (((uint32_t)p_cfg->resolution > ADT7410_RES_FIELD_MAX)
        || ((uint32_t)p_cfg->op_mode > ADT7410_OP_MODE_FIELD_MAX)
        || ((uint32_t)p_cfg->event_mode > ADT7410_EVENT_FIELD_MAX))
    {
        is_valid = false;
    }
    else if (((uint32_t)p_cfg->int_polarity > ADT7410_POL_FIELD_MAX)
             || ((uint32_t)p_cfg->ct_polarity > ADT7410_POL_FIELD_MAX)
             || ((uint32_t)p_cfg->fault_queue > ADT7410_FAULT_Q_FIELD_MAX))
    {
        is_valid = false;
    }
    else
    {
        is_valid = true;
    }

    return is_valid;
}


/**
 * @brief       Encode a validated configuration into Register 0x03.
 * @req         LLR-ADT-002
 * @design      SDD-ADT-3.12
 * @param[in]   p_cfg  Configuration already accepted by is_config_valid().
 * @return      Configuration register byte.
 * @note        Enum numeric values equal the Table 11 field encodings.
 *              ISR-safe. Reentrant.
 */
static uint8_t config_to_reg(adt7410_config_t const * p_cfg)
{
    uint32_t cfg_bits = 0U;

    ASSERT(NULL != p_cfg);

    cfg_bits = ((uint32_t)p_cfg->resolution << ADT7410_CFG_RES_SHIFT);
    cfg_bits |= ((uint32_t)p_cfg->op_mode << ADT7410_CFG_OP_MODE_SHIFT);
    cfg_bits |= ((uint32_t)p_cfg->event_mode << ADT7410_CFG_EVENT_SHIFT);
    cfg_bits |= ((uint32_t)p_cfg->int_polarity << ADT7410_CFG_INT_POL_SHIFT);
    cfg_bits |= ((uint32_t)p_cfg->ct_polarity << ADT7410_CFG_CT_POL_SHIFT);
    cfg_bits |= (uint32_t)p_cfg->fault_queue;

    return (uint8_t)(cfg_bits & ADT7410_BYTE_MASK);
}


/**
 * @brief       Decode Register 0x03 into a configuration structure.
 * @req         LLR-ADT-002
 * @design      SDD-ADT-3.13
 * @param[in]   cfg_reg  Raw configuration register byte.
 * @param[out]  p_cfg    Receives decoded configuration, non-NULL.
 * @note        Integer -> enum mapping via if/else and const lookup tables
 *              (no integer-to-enum casts, MISRA 10.5). ISR-safe. Reentrant.
 */
static void reg_to_config(uint8_t cfg_reg, adt7410_config_t * p_cfg)
{
    uint32_t const cfg_bits = (uint32_t)cfg_reg;
    uint32_t const op_mode_idx = (cfg_bits & ADT7410_CFG_OP_MODE_MASK)
                                 >> ADT7410_CFG_OP_MODE_SHIFT;
    uint32_t const fault_q_idx = cfg_bits & ADT7410_CFG_FAULT_Q_MASK;

    ASSERT(NULL != p_cfg);
    ASSERT(ADT7410_OP_MODE_COUNT > op_mode_idx);
    ASSERT(ADT7410_FAULT_QUEUE_COUNT > fault_q_idx);

    if (0U != (cfg_bits & ADT7410_CFG_RES_MASK))
    {
        p_cfg->resolution = ADT7410_RES_16BIT;
    }
    else
    {
        p_cfg->resolution = ADT7410_RES_13BIT;
    }

    /* 2-bit masked index: always < 4 == table length */
    p_cfg->op_mode = op_mode_lut[op_mode_idx];

    if (0U != (cfg_bits & ADT7410_CFG_EVENT_MASK))
    {
        p_cfg->event_mode = ADT7410_EVENT_COMPARATOR;
    }
    else
    {
        p_cfg->event_mode = ADT7410_EVENT_INTERRUPT;
    }

    if (0U != (cfg_bits & ADT7410_CFG_INT_POL_MASK))
    {
        p_cfg->int_polarity = ADT7410_POL_ACTIVE_HIGH;
    }
    else
    {
        p_cfg->int_polarity = ADT7410_POL_ACTIVE_LOW;
    }

    if (0U != (cfg_bits & ADT7410_CFG_CT_POL_MASK))
    {
        p_cfg->ct_polarity = ADT7410_POL_ACTIVE_HIGH;
    }
    else
    {
        p_cfg->ct_polarity = ADT7410_POL_ACTIVE_LOW;
    }

    /* 2-bit masked index: always < 4 == table length */
    p_cfg->fault_queue = fault_queue_lut[fault_q_idx];
}


/**
 * @brief       Decode Status Register 0x02 into boolean flags.
 * @req         LLR-ADT-003
 * @design      SDD-ADT-3.14
 * @param[in]   status_reg  Raw status register byte.
 * @param[out]  p_flags     Receives decoded flags, non-NULL.
 * @note        ISR-safe. Reentrant.
 */
static void reg_to_status_flags(uint8_t status_reg,
                                adt7410_status_flags_t * p_flags)
{
    uint32_t const status_bits = (uint32_t)status_reg;

    ASSERT(NULL != p_flags);

    /* RDY (bit 7) is active low: 1 = conversion result not yet ready */
    p_flags->is_not_ready = (0U != (status_bits & ADT7410_STATUS_NRDY_MASK));
    p_flags->has_tcrit_flt = (0U != (status_bits & ADT7410_STATUS_TCRIT_MASK));
    p_flags->has_thigh_flt = (0U != (status_bits & ADT7410_STATUS_THIGH_MASK));
    p_flags->has_tlow_flt = (0U != (status_bits & ADT7410_STATUS_TLOW_MASK));
}


/**
 * @brief       Map a setpoint selector to its MSB register address.
 * @req         LLR-ADT-004
 * @design      SDD-ADT-3.15
 * @param[in]   setpoint_id  Setpoint selector from the caller.
 * @param[out]  p_reg_addr   Receives register address on success.
 * @return      ADT7410_OK or ADT7410_ERR_PARAM (selector out of range).
 * @note        default: is reachable from caller input (not DEFENSIVE).
 *              ISR-safe. Reentrant.
 */
static adt7410_status_t setpoint_to_reg(adt7410_setpoint_id_t setpoint_id,
                                        uint8_t * p_reg_addr)
{
    adt7410_status_t result = ADT7410_OK;

    ASSERT(NULL != p_reg_addr);

    switch (setpoint_id)
    {
        case ADT7410_SETPOINT_THIGH:
            *p_reg_addr = ADT7410_REG_THIGH_MSB;
            break;

        case ADT7410_SETPOINT_TLOW:
            *p_reg_addr = ADT7410_REG_TLOW_MSB;
            break;

        case ADT7410_SETPOINT_TCRIT:
            *p_reg_addr = ADT7410_REG_TCRIT_MSB;
            break;

        default:
            /* Caller passed an integer outside adt7410_setpoint_id_t */
            result = ADT7410_ERR_PARAM;
            break;
    }

    return result;
}


/**
 * @brief       Convert a 16-bit two's complement register word to Q8.7.
 * @req         LLR-ADT-003, LLR-ADT-004
 * @design      SDD-ADT-3.16
 * @param[in]   reg_word  Raw register word, 0x0000..0xFFFF.
 * @return      Temperature in Q8.7 degC, -32768..32767.
 * @note        uint16_t -> int16_t for values > INT16_MAX is
 *              implementation-defined (C99 §6.3.1.3p3), so the sign is
 *              applied explicitly in int32_t. ISR-safe. Reentrant.
 */
static int32_t reg16_to_q8_7(uint16_t reg_word)
{
    int32_t temp_q8_7 = (int32_t)reg_word;

    if (0U != ((uint32_t)reg_word & ADT7410_REG16_SIGN_MASK))
    {
        temp_q8_7 = temp_q8_7 - ADT7410_REG16_MODULUS;
    }
    else
    {
        /* Non-negative: raw value is already correct */
    }

    return temp_q8_7;
}


/**
 * @brief       Convert a Q8.7 temperature to a 16-bit register word.
 * @req         LLR-ADT-004
 * @design      SDD-ADT-3.17
 * @param[in]   temp_q8_7  Temperature in Q8.7 degC, -32768..32767.
 * @return      16-bit two's complement register word.
 * @note        Adding 2^16 to a negative value yields its two's complement
 *              bit pattern in 0..65535, so the final cast is exact.
 *              ISR-safe. Reentrant.
 */
static uint16_t q8_7_to_reg16(int32_t temp_q8_7)
{
    int32_t wrapped_q8_7 = temp_q8_7;

    ASSERT(INT16_MIN <= temp_q8_7);
    ASSERT(INT16_MAX >= temp_q8_7);

    if (0 > temp_q8_7)
    {
        wrapped_q8_7 = temp_q8_7 + ADT7410_REG16_MODULUS;
    }
    else
    {
        /* Non-negative: bit pattern equals the value */
    }

    return (uint16_t)wrapped_q8_7;
}


/**
 * @brief       Convert a Q8.7 temperature to milli-degrees Celsius.
 * @req         LLR-ADT-003, LLR-ADT-004
 * @design      SDD-ADT-3.18
 * @param[in]   temp_q8_7  Temperature in Q8.7 degC, -32768..32767.
 * @return      Temperature in mdegC, -256000..255992, rounded to nearest.
 * @note        ISR-safe. Reentrant.
 */
static int32_t q8_7_to_mdegc(int32_t temp_q8_7)
{
    /* mdegC = q * 1000 / 128, rounded half away from zero.
     * Widening type int32_t: |q| <= 32768, so |q * 1000| <= 32 768 000,
     * far below INT32_MAX. Divisor 128 is a non-zero constant. */
    int32_t temp_mdegc_x128 = temp_q8_7 * ADT7410_MDEGC_PER_DEGC;
    int32_t temp_mdegc = 0;

    if (0 > temp_mdegc_x128)
    {
        temp_mdegc_x128 = temp_mdegc_x128 - ADT7410_Q7_ROUND_HALF;
    }
    else
    {
        temp_mdegc_x128 = temp_mdegc_x128 + ADT7410_Q7_ROUND_HALF;
    }

    temp_mdegc = temp_mdegc_x128 / ADT7410_Q7_LSB_PER_DEGC;

    return temp_mdegc;
}


/**
 * @brief       Convert milli-degrees Celsius to a Q8.7 temperature.
 * @req         LLR-ADT-004
 * @design      SDD-ADT-3.19
 * @param[in]   temp_mdegc  Temperature in mdegC, -55000..150000
 *                          (pre-validated by is_temp_in_range()).
 * @return      Temperature in Q8.7 degC, -7040..19200.
 * @note        ISR-safe. Reentrant.
 */
static int32_t mdegc_to_q8_7(int32_t temp_mdegc)
{
    /* q = mdegC * 128 / 1000, rounded half away from zero.
     * Widening type int32_t: |mdegC| <= 150 000, so |mdegC * 128| <=
     * 19 200 000, far below INT32_MAX. Divisor 1000 is a non-zero
     * constant. */
    int32_t temp_q8_7_x1000 = temp_mdegc * ADT7410_Q7_LSB_PER_DEGC;
    int32_t temp_q8_7 = 0;

    if (0 > temp_q8_7_x1000)
    {
        temp_q8_7_x1000 = temp_q8_7_x1000 - ADT7410_MDEGC_ROUND_HALF;
    }
    else
    {
        temp_q8_7_x1000 = temp_q8_7_x1000 + ADT7410_MDEGC_ROUND_HALF;
    }

    temp_q8_7 = temp_q8_7_x1000 / ADT7410_MDEGC_PER_DEGC;

    return temp_q8_7;
}


/**
 * @brief       Convert a Q8.7 temperature to degrees Celsius (float).
 * @req         LLR-ADT-003
 * @design      SDD-ADT-3.21
 * @param[in]   temp_q8_7  Temperature in Q8.7 degC, -32768..32767.
 * @return      Temperature in degC, [-256.0f, +255.9921875f].
 * @note        0.0078125f = 2^-7 is an exact power of two in IEEE-754
 *              binary32 and any 16-bit integer fits in the 24-bit mantissa,
 *              so the conversion is exact. Reentrant.
 */
static float q8_7_to_degc(int16_t temp_q8_7)
{
    /* Scale factor 2^-7 degC/LSB; int16_t -> float is exact (<= 16 bits). */
    float const die_temp_degc =
        (float)temp_q8_7 * ADT7410_DEGC_PER_Q8_7_LSB;

    return die_temp_degc;
}


/**
 * @brief       Decode the Temperature Value Register into a sample.
 * @req         LLR-ADT-003
 * @design      SDD-ADT-3.20
 * @param[in]   reg_word    Raw Temperature Value Register (0x00..0x01).
 * @param[in]   active_res  Resolution currently programmed in Reg 0x03.
 * @param[out]  p_sample    Receives the decoded sample on ADT7410_OK.
 * @return      ADT7410_OK or ADT7410_ERR_STATE (corrupted active_res).
 * @note        In 13-bit mode bits [15:3] hold sign + 12 bits at
 *              0.0625 degC/LSB; with bits [2:0] cleared the word already
 *              has the Q8.7 scale (0.0625 = 8 / 128). ISR-safe. Reentrant.
 */
static adt7410_status_t reg16_to_sample(uint16_t reg_word,
                                        adt7410_resolution_t active_res,
                                        adt7410_sample_t * p_sample)
{
    adt7410_status_t result = ADT7410_OK;
    uint32_t const reg_bits = (uint32_t)reg_word;
    uint16_t temp_word = 0U;
    int32_t temp_q8_7 = 0;
    adt7410_sample_t sample =
    {
        .die_temp_mdegc = 0,
        .die_temp_q8_7 = 0,
        .has_inline_flags = false,
        .is_tcrit_active = false,
        .is_thigh_active = false,
        .is_tlow_active = false
    };

    ASSERT(NULL != p_sample);

    switch (active_res)
    {
        case ADT7410_RES_13BIT:
            temp_word = (uint16_t)(reg_bits & ADT7410_TEMP_13BIT_DATA_MASK);
            sample.has_inline_flags = true;
            sample.is_tcrit_active =
                (0U != (reg_bits & ADT7410_TEMP_FLAG_TCRIT_MASK));
            sample.is_thigh_active =
                (0U != (reg_bits & ADT7410_TEMP_FLAG_THIGH_MASK));
            sample.is_tlow_active =
                (0U != (reg_bits & ADT7410_TEMP_FLAG_TLOW_MASK));
            break;

        case ADT7410_RES_16BIT:
            /* All 16 bits are temperature; no inline flags */
            temp_word = reg_word;
            break;

        default:
            /* DEFENSIVE [DO-178C]: active_res corrupted by SEU / RAM
             * fault — unreachable in normal test */
            result = ADT7410_ERR_STATE;
            break;
    }

    if (ADT7410_OK == result)
    {
        temp_q8_7 = reg16_to_q8_7(temp_word);
        /* Range -32768..32767 by construction: cast is exact */
        sample.die_temp_q8_7 = (int16_t)temp_q8_7;
        sample.die_temp_mdegc = q8_7_to_mdegc(temp_q8_7);
        *p_sample = sample;
    }
    else
    {
        /* Output untouched on failure */
    }

    return result;
}
