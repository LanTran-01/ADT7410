/**
 * @file    adt7410_driver.h
 * @brief   Analog Devices ADT7410 16-Bit Digital I2C Temperature Sensor Driver.
 * @req     LLR-ADT-001, LLR-ADT-002, LLR-ADT-003, LLR-ADT-004, LLR-ADT-005
 * @design  SDD-ADT-1.0
 */

#ifndef ADT7410_DRIVER_H
#define ADT7410_DRIVER_H

#include <stdint.h>
#include <stdbool.h>
#include "axi_iic_hal.h"

/**
 * @brief   Status codes for ADT7410 driver operations.
 * @req     LLR-ADT-001
 * @design  SDD-ADT-1.1
 */
typedef enum
{
    ADT7410_OK                = 0U,
    ADT7410_ERR_PARAM         = 1U,
    ADT7410_ERR_STATE         = 2U,
    ADT7410_ERR_I2C_COMM      = 3U,
    ADT7410_ERR_DEV_ID        = 4U,
    ADT7410_ERR_NOT_READY     = 5U
} adt7410_status_t;

/**
 * @brief   SEU-resilient driver state pattern (Hamming distance >= 4).
 * @req     LLR-ADT-001
 * @design  SDD-ADT-1.2
 * @note    Bit 31 is kept clear: C99 §6.7.2.2p2 requires every enumeration
 *          constant to be representable as an int, so a value above INT_MAX
 *          is a constraint violation under -pedantic-errors.
 *          Hamming distance UNINIT <-> READY = 31 bits.
 */
typedef enum
{
    ADT7410_STATE_UNINIT = 0x3C96A5E1U,
    ADT7410_STATE_READY  = 0x43695A1EU
} adt7410_init_state_t;

/**
 * @brief   ADC conversion resolution selection (Datasheet Table 11, Bit 7).
 * @req     LLR-ADT-002
 * @design  SDD-ADT-1.3
 */
typedef enum
{
    ADT7410_RES_13BIT = 0U, /* 0.0625 degC/LSB (Sign + 12 bits, default) */
    ADT7410_RES_16BIT = 1U  /* 0.0078125 degC/LSB (Sign + 15 bits)       */
} adt7410_resolution_t;

/**
 * @brief   Operational power and conversion mode (Datasheet Table 11, [6:5]).
 * @req     LLR-ADT-002
 * @design  SDD-ADT-1.4
 */
typedef enum
{
    ADT7410_MODE_CONTINUOUS = 0U, /* Continuous conversion (240 ms/sample) */
    ADT7410_MODE_ONE_SHOT   = 1U, /* Single conversion then shutdown       */
    ADT7410_MODE_1SPS       = 2U, /* 1 sample/sec (60 ms active, 940 idle) */
    ADT7410_MODE_SHUTDOWN   = 3U  /* Full power-down (2 uA typical)        */
} adt7410_op_mode_t;

/**
 * @brief   Overtemperature/undertemperature event mode (Table 11, Bit 4).
 * @req     LLR-ADT-002
 * @design  SDD-ADT-1.5
 */
typedef enum
{
    ADT7410_EVENT_INTERRUPT  = 0U, /* Latched until register read (default) */
    ADT7410_EVENT_COMPARATOR = 1U  /* Self-clearing with hysteresis         */
} adt7410_event_mode_t;

/**
 * @brief   Active output polarity for INT and CT pins (Table 11, Bits [3:2]).
 * @req     LLR-ADT-002
 * @design  SDD-ADT-1.6
 */
typedef enum
{
    ADT7410_POL_ACTIVE_LOW  = 0U, /* Pin pulls low on fault (default)  */
    ADT7410_POL_ACTIVE_HIGH = 1U  /* Pin pulls high on fault           */
} adt7410_pin_polarity_t;

/**
 * @brief   Consecutive fault queue depth before tripping INT/CT (Bits [1:0]).
 * @req     LLR-ADT-002
 * @design  SDD-ADT-1.7
 */
typedef enum
{
    ADT7410_FAULT_QUEUE_1 = 0U, /* 1 consecutive fault (default) */
    ADT7410_FAULT_QUEUE_2 = 1U, /* 2 consecutive faults          */
    ADT7410_FAULT_QUEUE_3 = 2U, /* 3 consecutive faults          */
    ADT7410_FAULT_QUEUE_4 = 3U  /* 4 consecutive faults          */
} adt7410_fault_queue_t;

/**
 * @brief   Selector for programmable temperature limit registers.
 * @req     LLR-ADT-004
 * @design  SDD-ADT-1.8
 */
typedef enum
{
    ADT7410_SETPOINT_THIGH = 0U, /* Overtemperature threshold (0x04..0x05)  */
    ADT7410_SETPOINT_TLOW  = 1U, /* Undertemperature threshold (0x06..0x07) */
    ADT7410_SETPOINT_TCRIT = 2U  /* Critical temp threshold (0x08..0x09)    */
} adt7410_setpoint_id_t;

/**
 * @brief   Complete configuration structure for Register 0x03.
 * @req     LLR-ADT-002
 * @design  SDD-ADT-1.9
 */
typedef struct
{
    adt7410_resolution_t   resolution;
    adt7410_op_mode_t      op_mode;
    adt7410_event_mode_t   event_mode;
    adt7410_pin_polarity_t int_polarity;
    adt7410_pin_polarity_t ct_polarity;
    adt7410_fault_queue_t  fault_queue;
} adt7410_config_t;

/**
 * @brief   Parsed status flags from Status Register 0x02 (Table 10, p. 14).
 * @req     LLR-ADT-003
 * @design  SDD-ADT-1.10
 */
typedef struct
{
    bool is_not_ready;  /* Bit 7: true while converting, false when ready */
    bool has_tcrit_flt; /* Bit 6: true if temperature exceeded TCRIT      */
    bool has_thigh_flt; /* Bit 5: true if temperature exceeded THIGH      */
    bool has_tlow_flt;  /* Bit 4: true if temperature dropped below TLOW  */
} adt7410_status_flags_t;

/**
 * @brief   Temperature sample output with physical units and inline flags.
 * @req     LLR-ADT-003
 * @design  SDD-ADT-1.11
 * @note    die_temp_q8_7 format: Q8.7 (sign + 8 integer + 7 fraction bits),
 *          degC = raw / 2^7, LSB = 0.0078125 degC, representable range
 *          [-256.0, +255.9921875] degC. In 13-bit mode bits [2:0] are 0.
 */
typedef struct
{
    int32_t die_temp_mdegc;   /* Die temp, mdegC, nominal -55000..150000   */
    int16_t die_temp_q8_7;    /* Die temp, Q8.7 degC (see @note above)     */
    bool    has_inline_flags; /* True in 13-bit mode (bits [2:0] are flags) */
    bool    is_tcrit_active;  /* Inline bit 2 in 13-bit mode, else false   */
    bool    is_thigh_active;  /* Inline bit 1 in 13-bit mode, else false   */
    bool    is_tlow_active;   /* Inline bit 0 in 13-bit mode, else false   */
} adt7410_sample_t;

/**
 * @brief   ADT7410 device instance context.
 * @req     LLR-ADT-001
 * @design  SDD-ADT-1.12
 */
typedef struct
{
    axi_iic_dev_t const * p_iic_bus;     /* Bus HAL; must outlive p_dev    */
    uint8_t               slave_addr_7b; /* 7-bit address, 0x48..0x4B      */
    adt7410_resolution_t  active_res;    /* Mirror of Config bit 7         */
    adt7410_init_state_t  init_state;    /* SEU-resilient lifecycle state  */
} adt7410_dev_t;

/* ADT7410 7-bit Slave Addresses (Datasheet Table 20, p. 17) */
#define ADT7410_I2C_ADDR_A1L_A0L    (0x48U) /* A1 = GND, A0 = GND */
#define ADT7410_I2C_ADDR_A1L_A0H    (0x49U) /* A1 = GND, A0 = VDD */
#define ADT7410_I2C_ADDR_A1H_A0L    (0x4AU) /* A1 = VDD, A0 = GND */
#define ADT7410_I2C_ADDR_A1H_A0H    (0x4BU) /* A1 = VDD, A0 = VDD */

/* Physical Temperature Operating Bounds (Datasheet Table 1, p. 3) */
#define ADT7410_TEMP_MIN_MDEGC      (-55000)  /* -55.000 degC in mdegC */
#define ADT7410_TEMP_MAX_MDEGC      (150000)  /* +150.000 degC in mdegC */
#define ADT7410_HYST_MAX_DEGC       (15U)     /* Max hysteresis: 15 degC */

/* Manufacturer ID (Datasheet Table 19, p. 16: bits [7:3] = 11001b) */
#define ADT7410_EXPECTED_MFG_ID     (0x19U)


/**
 * @brief       Initialize the ADT7410 sensor, verify Silicon ID, and configure.
 * @req         LLR-ADT-001
 * @design      SDD-ADT-2.1
 * @param[out]  p_dev          Pointer to ADT7410 device context to initialize.
 * @param[in]   p_iic_bus      Pointer to initialized AXI IIC HAL instance;
 *                             stored in p_dev, must outlive it.
 * @param[in]   slave_addr_7b  7-bit slave address, 0x48..0x4B.
 * @param[in]   p_cfg          Pointer to initial sensor configuration.
 * @return      ADT7410_OK, ADT7410_ERR_PARAM, ADT7410_ERR_I2C_COMM,
 *              or ADT7410_ERR_DEV_ID.
 * @note        Not ISR-safe. Blocking (2 polled I2C transfers).
 *              Reentrant across distinct p_dev on distinct buses; callers
 *              sharing one AXI IIC bus must serialise access.
 *              On any failure p_dev->init_state is left UNINIT.
 */
adt7410_status_t adt7410_init(adt7410_dev_t * p_dev,
                              axi_iic_dev_t const * p_iic_bus,
                              uint8_t slave_addr_7b,
                              adt7410_config_t const * p_cfg);

/**
 * @brief       Issue a software reset (Command 0x2F) to restore defaults.
 * @req         LLR-ADT-001
 * @design      SDD-ADT-2.2
 * @param[in,out] p_dev  Pointer to initialized ADT7410 device context.
 * @return      ADT7410_OK, ADT7410_ERR_PARAM, ADT7410_ERR_STATE,
 *              or ADT7410_ERR_I2C_COMM.
 * @note        Caller must wait >= 200 us after reset before next I2C access
 *              per Datasheet §RESET (p. 19). Updates active_res to 13-bit.
 *              Not ISR-safe. Blocking (1 polled I2C transfer).
 */
adt7410_status_t adt7410_soft_reset(adt7410_dev_t * p_dev);

/**
 * @brief       Read and verify the Manufacturer and Revision ID (Reg 0x0B).
 * @req         LLR-ADT-001
 * @design      SDD-ADT-2.3
 * @param[in]   p_dev         Pointer to initialized ADT7410 device context.
 * @param[out]  p_mfg_id      Pointer to receive 5-bit manufacturer ID (0x19).
 * @param[out]  p_silicon_rev Pointer to receive 3-bit silicon revision ID.
 * @return      ADT7410_OK, ADT7410_ERR_PARAM, ADT7410_ERR_STATE,
 *              ADT7410_ERR_I2C_COMM, or ADT7410_ERR_DEV_ID.
 * @note        Not ISR-safe. Blocking (1 polled I2C transfer).
 *              Outputs are written whenever the I2C read succeeds, also on
 *              ADT7410_ERR_DEV_ID (for diagnostics).
 */
adt7410_status_t adt7410_read_id(adt7410_dev_t const * p_dev,
                                 uint8_t * p_mfg_id,
                                 uint8_t * p_silicon_rev);

/**
 * @brief       Write a new configuration to Configuration Register (0x03).
 * @req         LLR-ADT-002
 * @design      SDD-ADT-2.4
 * @param[in,out] p_dev  Pointer to initialized ADT7410 device context.
 * @param[in]   p_cfg  Pointer to desired configuration parameters.
 * @return      ADT7410_OK, ADT7410_ERR_PARAM, ADT7410_ERR_STATE,
 *              or ADT7410_ERR_I2C_COMM.
 * @note        Updates p_dev->active_res to match p_cfg->resolution.
 *              Not ISR-safe. Blocking (1 polled I2C transfer).
 */
adt7410_status_t adt7410_set_config(adt7410_dev_t * p_dev,
                                    adt7410_config_t const * p_cfg);

/**
 * @brief       Read and decode the current Configuration Register (0x03).
 * @req         LLR-ADT-002
 * @design      SDD-ADT-2.5
 * @param[in]   p_dev  Pointer to initialized ADT7410 device context.
 * @param[out]  p_cfg  Pointer to structure receiving current configuration.
 * @return      ADT7410_OK, ADT7410_ERR_PARAM, ADT7410_ERR_STATE,
 *              or ADT7410_ERR_I2C_COMM.
 * @note        Not ISR-safe. Blocking (1 polled I2C transfer).
 */
adt7410_status_t adt7410_get_config(adt7410_dev_t const * p_dev,
                                    adt7410_config_t * p_cfg);

/**
 * @brief       Read and decode the Status Register (0x02).
 * @req         LLR-ADT-003
 * @design      SDD-ADT-2.6
 * @param[in]   p_dev    Pointer to initialized ADT7410 device context.
 * @param[out]  p_flags  Pointer to structure receiving decoded status flags.
 * @return      ADT7410_OK, ADT7410_ERR_PARAM, ADT7410_ERR_STATE,
 *              or ADT7410_ERR_I2C_COMM.
 * @note        Reading Status Register clears TLOW/THIGH/TCRIT flags in
 *              Interrupt mode (Datasheet p. 14).
 *              Not ISR-safe. Blocking (1 polled I2C transfer).
 */
adt7410_status_t adt7410_read_status(adt7410_dev_t const * p_dev,
                                     adt7410_status_flags_t * p_flags);

/**
 * @brief       Trigger a single One-Shot temperature conversion.
 * @req         LLR-ADT-002
 * @design      SDD-ADT-2.7
 * @param[in]   p_dev  Pointer to initialized ADT7410 device context.
 * @return      ADT7410_OK, ADT7410_ERR_PARAM, ADT7410_ERR_STATE,
 *              or ADT7410_ERR_I2C_COMM.
 * @note        Caller must wait >= 240 ms before reading temperature
 *              (Datasheet §ONE-SHOT MODE, p. 10).
 *              Read-modify-write of Register 0x03: only bits [6:5] change.
 *              Not ISR-safe. Blocking (2 polled I2C transfers).
 */
adt7410_status_t adt7410_trigger_one_shot(adt7410_dev_t const * p_dev);

/**
 * @brief       Read the 16-bit Temperature Value Register (0x00..0x01).
 * @req         LLR-ADT-003
 * @design      SDD-ADT-2.8
 * @param[in]   p_dev     Pointer to initialized ADT7410 device context.
 * @param[out]  p_sample  Pointer to structure receiving temperature in mdegC,
 *                        Q8.7 fixed-point, and inline alarm flags.
 * @return      ADT7410_OK, ADT7410_ERR_PARAM, ADT7410_ERR_STATE,
 *              or ADT7410_ERR_I2C_COMM.
 * @note        Handles both 13-bit (0.0625 degC) and 16-bit (0.0078125 degC)
 *              resolutions automatically based on active configuration.
 *              *p_sample is written only on ADT7410_OK.
 *              Not ISR-safe. Blocking (1 polled I2C transfer).
 */
adt7410_status_t adt7410_read_temperature(adt7410_dev_t const * p_dev,
                                          adt7410_sample_t * p_sample);

/**
 * @brief       Read the die temperature directly in degrees Celsius (float).
 * @req         LLR-ADT-003
 * @design      SDD-ADT-2.13
 * @param[in]   p_dev            Pointer to initialized ADT7410 device context.
 * @param[out]  p_die_temp_degc  Pointer receiving temperature in degrees C,
 *                               [-256.0f, +255.9921875f].
 * @return      ADT7410_OK, ADT7410_ERR_PARAM, ADT7410_ERR_STATE,
 *              or ADT7410_ERR_I2C_COMM.
 * @note        Automatically handles both 13-bit (0.0625 degC/LSB, inline
 *              flag bits [2:0] masked out) and 16-bit (0.0078125 degC/LSB)
 *              resolutions based on p_dev->active_res, so callers do not
 *              need to know the active resolution or scaling factor.
 *              *p_die_temp_degc is written only on ADT7410_OK.
 *              Not ISR-safe. Blocking (1 polled I2C transfer).
 */
adt7410_status_t adt7410_read_temp_degc(adt7410_dev_t const * p_dev,
                                        float * p_die_temp_degc);

/**
 * @brief       Write a temperature limit setpoint (THIGH, TLOW, or TCRIT).
 * @req         LLR-ADT-004
 * @design      SDD-ADT-2.9
 * @param[in]   p_dev             Pointer to initialized ADT7410 device.
 * @param[in]   setpoint_id       Target setpoint register selector.
 * @param[in]   limit_temp_mdegc  Threshold in milli-degC, -55000..150000.
 * @return      ADT7410_OK, ADT7410_ERR_PARAM, ADT7410_ERR_STATE,
 *              or ADT7410_ERR_I2C_COMM.
 * @note        Encodes threshold into 16-bit two's complement Q8.7,
 *              rounded to the nearest 0.0078125 degC.
 *              Not ISR-safe. Blocking (1 polled I2C transfer).
 */
adt7410_status_t adt7410_set_temp_setpoint(adt7410_dev_t const * p_dev,
                                           adt7410_setpoint_id_t setpoint_id,
                                           int32_t limit_temp_mdegc);

/**
 * @brief       Read a temperature limit setpoint (THIGH, TLOW, or TCRIT).
 * @req         LLR-ADT-004
 * @design      SDD-ADT-2.10
 * @param[in]   p_dev               Pointer to initialized ADT7410 device.
 * @param[in]   setpoint_id         Target setpoint register selector.
 * @param[out]  p_limit_temp_mdegc  Pointer receiving threshold in milli-degC,
 *                                  -256000..255992.
 * @return      ADT7410_OK, ADT7410_ERR_PARAM, ADT7410_ERR_STATE,
 *              or ADT7410_ERR_I2C_COMM.
 * @note        Not ISR-safe. Blocking (1 polled I2C transfer).
 */
adt7410_status_t adt7410_get_temp_setpoint(adt7410_dev_t const * p_dev,
                                           adt7410_setpoint_id_t setpoint_id,
                                           int32_t * p_limit_temp_mdegc);

/**
 * @brief       Set the Hysteresis temperature register (THYST, Reg 0x0A).
 * @req         LLR-ADT-005
 * @design      SDD-ADT-2.11
 * @param[in]   p_dev      Pointer to initialized ADT7410 device.
 * @param[in]   hyst_degc  Hysteresis in whole degrees Celsius, 0..15.
 * @return      ADT7410_OK, ADT7410_ERR_PARAM, ADT7410_ERR_STATE,
 *              or ADT7410_ERR_I2C_COMM.
 * @note        Applies to THIGH, TLOW, and TCRIT limits (Datasheet p. 16).
 *              Not ISR-safe. Blocking (1 polled I2C transfer).
 */
adt7410_status_t adt7410_set_hysteresis(adt7410_dev_t const * p_dev,
                                        uint8_t hyst_degc);

/**
 * @brief       Read the Hysteresis temperature register (THYST, Reg 0x0A).
 * @req         LLR-ADT-005
 * @design      SDD-ADT-2.12
 * @param[in]   p_dev        Pointer to initialized ADT7410 device.
 * @param[out]  p_hyst_degc  Pointer receiving hysteresis in degrees C, 0..15.
 * @return      ADT7410_OK, ADT7410_ERR_PARAM, ADT7410_ERR_STATE,
 *              or ADT7410_ERR_I2C_COMM.
 * @note        Not ISR-safe. Blocking (1 polled I2C transfer).
 */
adt7410_status_t adt7410_get_hysteresis(adt7410_dev_t const * p_dev,
                                        uint8_t * p_hyst_degc);

#endif /* ADT7410_DRIVER_H */
