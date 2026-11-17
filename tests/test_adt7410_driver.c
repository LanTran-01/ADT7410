/**
 * @file    test_adt7410_driver.c
 * @brief   Host unit tests for adt7410_driver.c against mock_axi_iic.c.
 *
 * Each test case traces to LLR clauses in docs/LLR_ADT7410_AXI_IIC.md.
 * Build and run with tests/run_tests.sh.
 */

#include <math.h>
#include <string.h>

#include "adt7410_driver.h"
#include "mock_axi_iic.h"
#include "test_harness.h"

/* Sensor configuration used by most tests: encodes to register 0x9A. */
static adt7410_config_t const cfg_default =
{
    .resolution   = ADT7410_RES_16BIT,
    .op_mode      = ADT7410_MODE_CONTINUOUS,
    .event_mode   = ADT7410_EVENT_COMPARATOR,
    .int_polarity = ADT7410_POL_ACTIVE_HIGH,
    .ct_polarity  = ADT7410_POL_ACTIVE_LOW,
    .fault_queue  = ADT7410_FAULT_QUEUE_3
};

static axi_iic_dev_t bus;  /* Opaque to the mock; only its address is used */
static adt7410_dev_t dev;


/* Reset the mock and bring dev to READY with the given resolution. */
static void setup_ready(adt7410_resolution_t res)
{
    adt7410_config_t cfg = cfg_default;
    adt7410_status_t status;

    mock_reset();
    cfg.resolution = res;
    status = adt7410_init(&dev, &bus, 0x48U, &cfg);
    TH_CHECK(ADT7410_OK == status);
    g_mock.xfer_count = 0U; /* Count only the transfers of the test itself */
}


static void set_temp_reg(uint16_t reg_word)
{
    g_mock.regs[0x00] = (uint8_t)(reg_word >> 8U);
    g_mock.regs[0x01] = (uint8_t)(reg_word & 0xFFU);
}


/* Independent reference: round(x) half away from zero, in double. */
static int32_t ref_round(double x)
{
    double r;

    if (x >= 0.0)
    {
        r = floor(x + 0.5);
    }
    else
    {
        r = -floor(-x + 0.5);
    }

    return (int32_t)r;
}


/* ===================== LLR-ADT-001: init / ID / reset / state ========== */

static void tc_adt_001(void)
{
    adt7410_status_t status;

    th_begin("TC-ADT-001", "LLR-ADT-001.3", "init rejects NULL p_dev");
    mock_reset();
    status = adt7410_init(NULL, &bus, 0x48U, &cfg_default);
    TH_CHECK(ADT7410_ERR_PARAM == status);
    TH_CHECK(0U == g_mock.xfer_count);
    th_end();
}


static void tc_adt_002(void)
{
    static uint8_t const bad_addr[] = { 0x00U, 0x47U, 0x4CU, 0x7FU, 0xFFU };
    adt7410_status_t status;
    size_t i;

    th_begin("TC-ADT-002", "LLR-ADT-001.4,001.7",
             "init rejects NULL bus/cfg and bad address");
    mock_reset();
    dev.init_state = ADT7410_STATE_READY; /* Must be forced back to UNINIT */
    status = adt7410_init(&dev, NULL, 0x48U, &cfg_default);
    TH_CHECK(ADT7410_ERR_PARAM == status);
    TH_CHECK(ADT7410_STATE_UNINIT == dev.init_state);

    dev.init_state = ADT7410_STATE_READY;
    status = adt7410_init(&dev, &bus, 0x48U, NULL);
    TH_CHECK(ADT7410_ERR_PARAM == status);
    TH_CHECK(ADT7410_STATE_UNINIT == dev.init_state);

    for (i = 0U; i < (sizeof(bad_addr) / sizeof(bad_addr[0])); i++)
    {
        status = adt7410_init(&dev, &bus, bad_addr[i], &cfg_default);
        TH_CHECK(ADT7410_ERR_PARAM == status);
    }
    TH_CHECK(0U == g_mock.xfer_count);
    th_end();
}


static void tc_adt_003(void)
{
    adt7410_config_t cfg;
    adt7410_status_t status;
    uint32_t field;

    th_begin("TC-ADT-003", "LLR-ADT-001.4,002.1",
             "init rejects each out-of-range config field");
    mock_reset();
    for (field = 0U; field < 7U; field++)
    {
        cfg = cfg_default;
        switch (field)
        {
            case 0U: cfg.resolution = (adt7410_resolution_t)2; break;
            case 1U: cfg.op_mode = (adt7410_op_mode_t)4; break;
            case 2U: cfg.event_mode = (adt7410_event_mode_t)2; break;
            case 3U: cfg.int_polarity = (adt7410_pin_polarity_t)2; break;
            case 4U: cfg.ct_polarity = (adt7410_pin_polarity_t)2; break;
            case 5U: cfg.fault_queue = (adt7410_fault_queue_t)4; break;
            default: cfg.op_mode = (adt7410_op_mode_t)-1; break;
        }
        status = adt7410_init(&dev, &bus, 0x48U, &cfg);
        TH_CHECK(ADT7410_ERR_PARAM == status);
        TH_CHECK(ADT7410_STATE_UNINIT == dev.init_state);
    }
    TH_CHECK(0U == g_mock.xfer_count);
    th_end();
}


static void tc_adt_004(void)
{
    adt7410_status_t status;

    th_begin("TC-ADT-004", "LLR-ADT-001.4",
             "init accepts address boundaries 0x48 and 0x4B");
    mock_reset();
    status = adt7410_init(&dev, &bus, 0x48U, &cfg_default);
    TH_CHECK(ADT7410_OK == status);
    TH_CHECK(0x48U == mock_last_xfer(0U)->slave_addr_7b);

    mock_reset();
    g_mock.device_addr_7b = 0x4BU;
    status = adt7410_init(&dev, &bus, 0x4BU, &cfg_default);
    TH_CHECK(ADT7410_OK == status);
    TH_CHECK(0x4BU == mock_last_xfer(0U)->slave_addr_7b);
    th_end();
}


static void tc_adt_005(void)
{
    adt7410_status_t status;

    th_begin("TC-ADT-005", "LLR-ADT-001.5,001.7",
             "init rejects wrong manufacturer ID, no config write");
    mock_reset();
    g_mock.regs[0x0B] = 0xC3U; /* MFG = 0x18 */
    status = adt7410_init(&dev, &bus, 0x48U, &cfg_default);
    TH_CHECK(ADT7410_ERR_DEV_ID == status);
    TH_CHECK(ADT7410_STATE_UNINIT == dev.init_state);
    TH_CHECK(1U == g_mock.xfer_count);
    TH_CHECK(0x00U == g_mock.regs[0x03]);
    th_end();
}


static void tc_adt_006(void)
{
    adt7410_status_t status;
    mock_xfer_t const * p_id;
    mock_xfer_t const * p_cfg;

    th_begin("TC-ADT-006", "LLR-ADT-001.5,001.6,002.2",
             "init success: ID read, config 0x9A written, READY");
    mock_reset();
    status = adt7410_init(&dev, &bus, 0x48U, &cfg_default);
    p_id = mock_last_xfer(1U);
    p_cfg = mock_last_xfer(0U);
    TH_CHECK(ADT7410_OK == status);
    TH_CHECK(2U == g_mock.xfer_count);
    TH_CHECK(MOCK_XFER_WRITE_READ == p_id->kind);
    TH_CHECK(0x0BU == p_id->reg_addr);
    TH_CHECK(1U == p_id->rx_len_bytes);
    TH_CHECK(MOCK_XFER_WRITE == p_cfg->kind);
    TH_CHECK(2U == p_cfg->tx_len_bytes);
    TH_CHECK(0x03U == p_cfg->tx_bytes[0]);
    TH_CHECK(0x9AU == p_cfg->tx_bytes[1]);
    TH_CHECK(ADT7410_RES_16BIT == dev.active_res);
    TH_CHECK(ADT7410_STATE_READY == dev.init_state);
    TH_CHECK(&bus == dev.p_iic_bus);
    TH_CHECK(0x48U == dev.slave_addr_7b);
    th_end();
}


static void tc_adt_007(void)
{
    adt7410_status_t status;

    th_begin("TC-ADT-007", "LLR-ADT-001.2,001.7",
             "init I2C failure on ID read or on config write");
    mock_reset();
    g_mock.fail_on_xfer = 1U;
    status = adt7410_init(&dev, &bus, 0x48U, &cfg_default);
    TH_CHECK(ADT7410_ERR_I2C_COMM == status);
    TH_CHECK(ADT7410_STATE_UNINIT == dev.init_state);

    mock_reset();
    g_mock.fail_on_xfer = 2U;
    status = adt7410_init(&dev, &bus, 0x48U, &cfg_default);
    TH_CHECK(ADT7410_ERR_I2C_COMM == status);
    TH_CHECK(ADT7410_STATE_UNINIT == dev.init_state);
    TH_CHECK(ADT7410_RES_13BIT == dev.active_res);

    mock_reset();
    status = adt7410_init(&dev, &bus, 0x49U, &cfg_default); /* No ACK */
    TH_CHECK(ADT7410_ERR_I2C_COMM == status);
    th_end();
}


static void tc_adt_008(void)
{
    static axi_iic_status_t const hal_errors[] =
    {
        AXI_IIC_ERR_PARAM, AXI_IIC_ERR_STATE, AXI_IIC_ERR_BUS_BUSY,
        AXI_IIC_ERR_NACK_ARB, AXI_IIC_ERR_TIMEOUT
    };
    adt7410_status_t status;
    uint8_t hyst_degc = 0U;
    size_t i;

    th_begin("TC-ADT-008", "LLR-ADT-001.2",
             "every HAL error code maps to ERR_I2C_COMM");
    for (i = 0U; i < (sizeof(hal_errors) / sizeof(hal_errors[0])); i++)
    {
        setup_ready(ADT7410_RES_16BIT);
        g_mock.fail_on_xfer = 1U;
        g_mock.fail_status = hal_errors[i];
        status = adt7410_get_hysteresis(&dev, &hyst_degc);
        TH_CHECK(ADT7410_ERR_I2C_COMM == status);
    }
    th_end();
}


static void tc_adt_009(void)
{
    adt7410_config_t cfg;
    adt7410_status_flags_t flags;
    adt7410_sample_t sample;
    float die_temp_degc = 0.0f;
    int32_t limit_mdegc = 0;
    uint8_t u8_a = 0U;
    uint8_t u8_b = 0U;

    th_begin("TC-ADT-009", "LLR-ADT-001.8",
             "every API rejects NULL pointers without I2C access");
    setup_ready(ADT7410_RES_16BIT);
    TH_CHECK(ADT7410_ERR_PARAM == adt7410_soft_reset(NULL));
    TH_CHECK(ADT7410_ERR_PARAM == adt7410_read_id(NULL, &u8_a, &u8_b));
    TH_CHECK(ADT7410_ERR_PARAM == adt7410_read_id(&dev, NULL, &u8_b));
    TH_CHECK(ADT7410_ERR_PARAM == adt7410_read_id(&dev, &u8_a, NULL));
    TH_CHECK(ADT7410_ERR_PARAM == adt7410_set_config(NULL, &cfg_default));
    TH_CHECK(ADT7410_ERR_PARAM == adt7410_set_config(&dev, NULL));
    TH_CHECK(ADT7410_ERR_PARAM == adt7410_get_config(NULL, &cfg));
    TH_CHECK(ADT7410_ERR_PARAM == adt7410_get_config(&dev, NULL));
    TH_CHECK(ADT7410_ERR_PARAM == adt7410_read_status(NULL, &flags));
    TH_CHECK(ADT7410_ERR_PARAM == adt7410_read_status(&dev, NULL));
    TH_CHECK(ADT7410_ERR_PARAM == adt7410_trigger_one_shot(NULL));
    TH_CHECK(ADT7410_ERR_PARAM == adt7410_read_temperature(NULL, &sample));
    TH_CHECK(ADT7410_ERR_PARAM == adt7410_read_temperature(&dev, NULL));
    TH_CHECK(ADT7410_ERR_PARAM
             == adt7410_read_temp_degc(NULL, &die_temp_degc));
    TH_CHECK(ADT7410_ERR_PARAM == adt7410_read_temp_degc(&dev, NULL));
    TH_CHECK(ADT7410_ERR_PARAM
             == adt7410_set_temp_setpoint(NULL, ADT7410_SETPOINT_THIGH, 0));
    TH_CHECK(ADT7410_ERR_PARAM
             == adt7410_get_temp_setpoint(NULL, ADT7410_SETPOINT_THIGH,
                                          &limit_mdegc));
    TH_CHECK(ADT7410_ERR_PARAM
             == adt7410_get_temp_setpoint(&dev, ADT7410_SETPOINT_THIGH,
                                          NULL));
    TH_CHECK(ADT7410_ERR_PARAM == adt7410_set_hysteresis(NULL, 5U));
    TH_CHECK(ADT7410_ERR_PARAM == adt7410_get_hysteresis(NULL, &u8_a));
    TH_CHECK(ADT7410_ERR_PARAM == adt7410_get_hysteresis(&dev, NULL));
    TH_CHECK(0U == g_mock.xfer_count);
    th_end();
}


/* Call every post-init API and expect ERR_STATE with no bus traffic. */
static void expect_all_state_errors(void)
{
    adt7410_config_t cfg;
    adt7410_status_flags_t flags;
    adt7410_sample_t sample;
    float die_temp_degc = 0.0f;
    int32_t limit_mdegc = 0;
    uint8_t u8_a = 0U;
    uint8_t u8_b = 0U;

    g_mock.xfer_count = 0U;
    TH_CHECK(ADT7410_ERR_STATE == adt7410_soft_reset(&dev));
    TH_CHECK(ADT7410_ERR_STATE == adt7410_read_id(&dev, &u8_a, &u8_b));
    TH_CHECK(ADT7410_ERR_STATE == adt7410_set_config(&dev, &cfg_default));
    TH_CHECK(ADT7410_ERR_STATE == adt7410_get_config(&dev, &cfg));
    TH_CHECK(ADT7410_ERR_STATE == adt7410_read_status(&dev, &flags));
    TH_CHECK(ADT7410_ERR_STATE == adt7410_trigger_one_shot(&dev));
    TH_CHECK(ADT7410_ERR_STATE == adt7410_read_temperature(&dev, &sample));
    TH_CHECK(ADT7410_ERR_STATE == adt7410_read_temp_degc(&dev, &die_temp_degc));
    TH_CHECK(ADT7410_ERR_STATE
             == adt7410_set_temp_setpoint(&dev, ADT7410_SETPOINT_THIGH, 0));
    TH_CHECK(ADT7410_ERR_STATE
             == adt7410_get_temp_setpoint(&dev, ADT7410_SETPOINT_THIGH,
                                          &limit_mdegc));
    TH_CHECK(ADT7410_ERR_STATE == adt7410_set_hysteresis(&dev, 5U));
    TH_CHECK(ADT7410_ERR_STATE == adt7410_get_hysteresis(&dev, &u8_a));
    TH_CHECK(0U == g_mock.xfer_count);
}


static void tc_adt_010(void)
{
    uint32_t bit;

    th_begin("TC-ADT-010", "LLR-ADT-001.8,001.10",
             "ERR_STATE when UNINIT or state hit by 1-bit SEU");
    setup_ready(ADT7410_RES_16BIT);
    dev.init_state = ADT7410_STATE_UNINIT;
    expect_all_state_errors();

    /* Fault injection: every single-bit flip of READY must be rejected */
    for (bit = 0U; bit < 32U; bit++)
    {
        dev.init_state =
            (adt7410_init_state_t)((uint32_t)ADT7410_STATE_READY
                                   ^ (1UL << bit));
        expect_all_state_errors();
    }
    th_end();
}


static void tc_adt_011(void)
{
    adt7410_status_t status;
    uint8_t mfg_id = 0U;
    uint8_t silicon_rev = 0U;

    th_begin("TC-ADT-011", "LLR-ADT-001.9", "read_id fields and mismatch");
    setup_ready(ADT7410_RES_16BIT);
    status = adt7410_read_id(&dev, &mfg_id, &silicon_rev);
    TH_CHECK(ADT7410_OK == status);
    TH_CHECK(0x19U == mfg_id);
    TH_CHECK(0x03U == silicon_rev);

    g_mock.regs[0x0B] = 0x55U; /* MFG 0x0A, rev 5 */
    status = adt7410_read_id(&dev, &mfg_id, &silicon_rev);
    TH_CHECK(ADT7410_ERR_DEV_ID == status);
    TH_CHECK(0x0AU == mfg_id);
    TH_CHECK(0x05U == silicon_rev);

    mfg_id = 0xAAU;
    g_mock.fail_on_xfer = g_mock.xfer_count + 1U;
    status = adt7410_read_id(&dev, &mfg_id, &silicon_rev);
    TH_CHECK(ADT7410_ERR_I2C_COMM == status);
    TH_CHECK(0xAAU == mfg_id);
    th_end();
}


static void tc_adt_012(void)
{
    adt7410_status_t status;
    mock_xfer_t const * p_xfer;

    th_begin("TC-ADT-012", "LLR-ADT-001.11",
             "soft_reset sends 0x2F, resets active_res");
    setup_ready(ADT7410_RES_16BIT);
    status = adt7410_soft_reset(&dev);
    p_xfer = mock_last_xfer(0U);
    TH_CHECK(ADT7410_OK == status);
    TH_CHECK(MOCK_XFER_WRITE == p_xfer->kind);
    TH_CHECK(1U == p_xfer->tx_len_bytes);
    TH_CHECK(0x2FU == p_xfer->tx_bytes[0]);
    TH_CHECK(ADT7410_RES_13BIT == dev.active_res);
    TH_CHECK(0x00U == g_mock.regs[0x03]);

    setup_ready(ADT7410_RES_16BIT);
    g_mock.fail_on_xfer = 1U;
    status = adt7410_soft_reset(&dev);
    TH_CHECK(ADT7410_ERR_I2C_COMM == status);
    TH_CHECK(ADT7410_RES_16BIT == dev.active_res);
    th_end();
}


/* ===================== LLR-ADT-002: configuration ====================== */

static void tc_adt_020(void)
{
    adt7410_config_t cfg;
    adt7410_status_t status;

    th_begin("TC-ADT-020", "LLR-ADT-002.2,002.3",
             "set_config encodes all-zero / all-max / mixed");
    setup_ready(ADT7410_RES_16BIT);

    cfg.resolution = ADT7410_RES_13BIT;
    cfg.op_mode = ADT7410_MODE_CONTINUOUS;
    cfg.event_mode = ADT7410_EVENT_INTERRUPT;
    cfg.int_polarity = ADT7410_POL_ACTIVE_LOW;
    cfg.ct_polarity = ADT7410_POL_ACTIVE_LOW;
    cfg.fault_queue = ADT7410_FAULT_QUEUE_1;
    status = adt7410_set_config(&dev, &cfg);
    TH_CHECK(ADT7410_OK == status);
    TH_CHECK(0x00U == g_mock.regs[0x03]);
    TH_CHECK(ADT7410_RES_13BIT == dev.active_res);

    cfg.resolution = ADT7410_RES_16BIT;
    cfg.op_mode = ADT7410_MODE_SHUTDOWN;
    cfg.event_mode = ADT7410_EVENT_COMPARATOR;
    cfg.int_polarity = ADT7410_POL_ACTIVE_HIGH;
    cfg.ct_polarity = ADT7410_POL_ACTIVE_HIGH;
    cfg.fault_queue = ADT7410_FAULT_QUEUE_4;
    status = adt7410_set_config(&dev, &cfg);
    TH_CHECK(ADT7410_OK == status);
    TH_CHECK(0xFFU == g_mock.regs[0x03]);
    TH_CHECK(ADT7410_RES_16BIT == dev.active_res);

    cfg.resolution = ADT7410_RES_13BIT;
    cfg.op_mode = ADT7410_MODE_1SPS;          /* 0x40 */
    cfg.event_mode = ADT7410_EVENT_INTERRUPT;
    cfg.int_polarity = ADT7410_POL_ACTIVE_LOW;
    cfg.ct_polarity = ADT7410_POL_ACTIVE_HIGH; /* 0x04 */
    cfg.fault_queue = ADT7410_FAULT_QUEUE_2;  /* 0x01 */
    status = adt7410_set_config(&dev, &cfg);
    TH_CHECK(ADT7410_OK == status);
    TH_CHECK(0x45U == g_mock.regs[0x03]);
    th_end();
}


static void tc_adt_021(void)
{
    adt7410_config_t cfg = cfg_default;
    adt7410_status_t status;

    th_begin("TC-ADT-021", "LLR-ADT-002.1,002.3",
             "set_config: invalid field rejected; I2C failure");
    setup_ready(ADT7410_RES_16BIT);
    cfg.fault_queue = (adt7410_fault_queue_t)4;
    status = adt7410_set_config(&dev, &cfg);
    TH_CHECK(ADT7410_ERR_PARAM == status);
    TH_CHECK(0U == g_mock.xfer_count);

    cfg = cfg_default;
    cfg.resolution = ADT7410_RES_13BIT;
    g_mock.fail_on_xfer = 1U;
    status = adt7410_set_config(&dev, &cfg);
    TH_CHECK(ADT7410_ERR_I2C_COMM == status);
    TH_CHECK(ADT7410_RES_16BIT == dev.active_res);
    th_end();
}


static void tc_adt_022(void)
{
    adt7410_config_t cfg;
    adt7410_status_t status;
    uint32_t reg;
    uint32_t mismatch = 0U;

    th_begin("TC-ADT-022", "LLR-ADT-002.2,002.4",
             "get_config/set_config round-trip, all 256 values");
    setup_ready(ADT7410_RES_16BIT);
    for (reg = 0U; reg < 256U; reg++)
    {
        g_mock.regs[0x03] = (uint8_t)reg;
        status = adt7410_get_config(&dev, &cfg);
        if (ADT7410_OK == status)
        {
            g_mock.regs[0x03] = 0x00U;
            status = adt7410_set_config(&dev, &cfg);
        }
        if ((ADT7410_OK != status) || (reg != g_mock.regs[0x03]))
        {
            mismatch++;
        }
    }
    TH_CHECK(0U == mismatch);
    th_end();
}


static void tc_adt_023(void)
{
    adt7410_config_t cfg;
    adt7410_status_t status;

    th_begin("TC-ADT-023", "LLR-ADT-002.4",
             "get_config leaves output untouched on failure");
    setup_ready(ADT7410_RES_16BIT);
    (void)memset(&cfg, 0xA5, sizeof(cfg));
    g_mock.fail_on_xfer = 1U;
    status = adt7410_get_config(&dev, &cfg);
    TH_CHECK(ADT7410_ERR_I2C_COMM == status);
    TH_CHECK(0xA5A5A5A5U == (uint32_t)cfg.op_mode);
    th_end();
}


static void tc_adt_024(void)
{
    adt7410_status_t status;
    uint32_t reg;
    uint32_t mismatch = 0U;

    th_begin("TC-ADT-024", "LLR-ADT-002.5",
             "one-shot RMW keeps other bits, all 256 values");
    setup_ready(ADT7410_RES_16BIT);
    for (reg = 0U; reg < 256U; reg++)
    {
        g_mock.regs[0x03] = (uint8_t)reg;
        status = adt7410_trigger_one_shot(&dev);
        if ((ADT7410_OK != status)
            || (((reg & 0x9FU) | 0x20U) != g_mock.regs[0x03]))
        {
            mismatch++;
        }
    }
    TH_CHECK(0U == mismatch);

    g_mock.regs[0x03] = 0x9BU;
    g_mock.xfer_count = 0U;
    g_mock.fail_on_xfer = 1U;
    status = adt7410_trigger_one_shot(&dev);
    TH_CHECK(ADT7410_ERR_I2C_COMM == status);
    TH_CHECK(1U == g_mock.xfer_count); /* No write after a failed read */
    TH_CHECK(0x9BU == g_mock.regs[0x03]);
    th_end();
}


/* ===================== LLR-ADT-003: temperature & status =============== */

static void tc_adt_030(void)
{
    static struct
    {
        uint16_t reg_word;
        int32_t  expected_mdegc;
    } const vec[] =
    {
        { 0x4B00U, 150000 }, { 0x4980U, 147000 }, { 0x2000U, 64000 },
        { 0x0C80U, 25000 },  { 0x0001U, 8 },      { 0x0000U, 0 },
        { 0xFFFFU, -8 },     { 0xF380U, -25000 }, { 0xE480U, -55000 },
        { 0x7FFFU, 255992 }, { 0x8000U, -256000 }
    };
    adt7410_sample_t sample;
    adt7410_status_t status;
    mock_xfer_t const * p_xfer;
    size_t i;

    th_begin("TC-ADT-030", "LLR-ADT-003.1,003.3,003.4",
             "read_temperature 16-bit reference vectors");
    setup_ready(ADT7410_RES_16BIT);
    for (i = 0U; i < (sizeof(vec) / sizeof(vec[0])); i++)
    {
        set_temp_reg(vec[i].reg_word);
        status = adt7410_read_temperature(&dev, &sample);
        TH_CHECK(ADT7410_OK == status);
        TH_CHECK(vec[i].expected_mdegc == sample.die_temp_mdegc);
        TH_CHECK((int16_t)vec[i].reg_word == sample.die_temp_q8_7);
        TH_CHECK(!sample.has_inline_flags);
        TH_CHECK(!sample.is_tcrit_active);
    }
    p_xfer = mock_last_xfer(0U);
    TH_CHECK(MOCK_XFER_WRITE_READ == p_xfer->kind);
    TH_CHECK(0x00U == p_xfer->reg_addr);
    TH_CHECK(2U == p_xfer->rx_len_bytes);
    th_end();
}


static void tc_adt_031(void)
{
    adt7410_sample_t sample;
    adt7410_status_t status;
    uint16_t flags;

    th_begin("TC-ADT-031", "LLR-ADT-003.2,003.4",
             "13-bit: data bits [15:3] + all 8 flag combinations");
    setup_ready(ADT7410_RES_13BIT);
    for (flags = 0U; flags < 8U; flags++)
    {
        set_temp_reg((uint16_t)(0x0C80U | flags)); /* 25 degC */
        status = adt7410_read_temperature(&dev, &sample);
        TH_CHECK(ADT7410_OK == status);
        TH_CHECK(25000 == sample.die_temp_mdegc);
        TH_CHECK(3200 == sample.die_temp_q8_7);
        TH_CHECK(sample.has_inline_flags);
        TH_CHECK(sample.is_tcrit_active == (0U != (flags & 4U)));
        TH_CHECK(sample.is_thigh_active == (0U != (flags & 2U)));
        TH_CHECK(sample.is_tlow_active == (0U != (flags & 1U)));
    }
    set_temp_reg(0xE487U); /* -55 degC + all flags */
    status = adt7410_read_temperature(&dev, &sample);
    TH_CHECK(-55000 == sample.die_temp_mdegc);
    th_end();
}


static void tc_adt_032(void)
{
    adt7410_sample_t sample;
    adt7410_status_t status;
    uint32_t word;
    uint32_t mismatch = 0U;
    int32_t q_ref;

    th_begin("TC-ADT-032", "LLR-ADT-003.4",
             "16-bit decode vs double reference, all 65536 words");
    setup_ready(ADT7410_RES_16BIT);
    for (word = 0U; word < 65536U; word++)
    {
        set_temp_reg((uint16_t)word);
        status = adt7410_read_temperature(&dev, &sample);
        q_ref = (word >= 32768U) ? ((int32_t)word - 65536) : (int32_t)word;
        if ((ADT7410_OK != status)
            || (q_ref != sample.die_temp_q8_7)
            || (ref_round((double)q_ref * 1000.0 / 128.0)
                != sample.die_temp_mdegc))
        {
            mismatch++;
        }
    }
    TH_CHECK(0U == mismatch);
    th_end();
}


static void tc_adt_033(void)
{
    adt7410_sample_t sample;
    adt7410_status_t status;

    th_begin("TC-ADT-033", "LLR-ADT-003.5,003.6",
             "sample untouched on I2C failure / corrupted active_res");
    setup_ready(ADT7410_RES_16BIT);
    (void)memset(&sample, 0x5A, sizeof(sample));
    g_mock.fail_on_xfer = 1U;
    status = adt7410_read_temperature(&dev, &sample);
    TH_CHECK(ADT7410_ERR_I2C_COMM == status);
    TH_CHECK(0x5A5A5A5A == sample.die_temp_mdegc);

    /* Fault injection: SEU in active_res reaches the DEFENSIVE default */
    setup_ready(ADT7410_RES_16BIT);
    set_temp_reg(0x0C80U);
    dev.active_res = (adt7410_resolution_t)2;
    status = adt7410_read_temperature(&dev, &sample);
    TH_CHECK(ADT7410_ERR_STATE == status);
    TH_CHECK(0x5A5A5A5A == sample.die_temp_mdegc);
    th_end();
}


static void tc_adt_034(void)
{
    adt7410_status_flags_t flags;
    adt7410_status_t status;
    uint32_t pattern;

    th_begin("TC-ADT-034", "LLR-ADT-003.7",
             "read_status decodes all 16 bit-7..4 combinations");
    setup_ready(ADT7410_RES_16BIT);
    for (pattern = 0U; pattern < 16U; pattern++)
    {
        g_mock.regs[0x02] = (uint8_t)((pattern << 4U) | 0x0FU);
        status = adt7410_read_status(&dev, &flags);
        TH_CHECK(ADT7410_OK == status);
        TH_CHECK(flags.is_not_ready == (0U != (pattern & 8U)));
        TH_CHECK(flags.has_tcrit_flt == (0U != (pattern & 4U)));
        TH_CHECK(flags.has_thigh_flt == (0U != (pattern & 2U)));
        TH_CHECK(flags.has_tlow_flt == (0U != (pattern & 1U)));
    }
    TH_CHECK(0x02U == mock_last_xfer(0U)->reg_addr);

    g_mock.fail_on_xfer = g_mock.xfer_count + 1U;
    status = adt7410_read_status(&dev, &flags);
    TH_CHECK(ADT7410_ERR_I2C_COMM == status);
    th_end();
}


static void tc_adt_035(void)
{
    static struct
    {
        uint16_t reg_word;
        float    expected_degc;
    } const vec16[] =
    {
        { 0x4B00U,  150.0f       },
        { 0x0C80U,   25.0f       },
        { 0x0001U,    0.0078125f },
        { 0x0000U,    0.0f       },
        { 0xFFFFU,   -0.0078125f },
        { 0xF380U,  -25.0f       },
        { 0xE480U,  -55.0f       }
    };
    float die_temp_degc = 0.0f;
    adt7410_status_t status;
    size_t i;

    th_begin("TC-ADT-035", "LLR-ADT-003.8",
             "read_temp_degc float output in 16-bit, 13-bit, and fault paths");
    setup_ready(ADT7410_RES_16BIT);
    for (i = 0U; i < (sizeof(vec16) / sizeof(vec16[0])); i++)
    {
        set_temp_reg(vec16[i].reg_word);
        status = adt7410_read_temp_degc(&dev, &die_temp_degc);
        TH_CHECK(ADT7410_OK == status);
        TH_CHECK(fabs((double)die_temp_degc - (double)vec16[i].expected_degc)
                 <= 1.0e-7);
    }

    /* 13-bit mode: inline flag bits [2:0] must be masked out automatically */
    setup_ready(ADT7410_RES_13BIT);
    set_temp_reg(0x0C87U); /* +25.0 degC with all 3 inline flags set */
    status = adt7410_read_temp_degc(&dev, &die_temp_degc);
    TH_CHECK(ADT7410_OK == status);
    TH_CHECK(fabs((double)die_temp_degc - 25.0) <= 1.0e-7);

    set_temp_reg(0x000FU); /* +0.0625 degC (1 LSB in 13-bit) + 3 flags */
    status = adt7410_read_temp_degc(&dev, &die_temp_degc);
    TH_CHECK(ADT7410_OK == status);
    TH_CHECK(fabs((double)die_temp_degc - 0.0625) <= 1.0e-7);

    /* Output untouched on I2C failure */
    die_temp_degc = 42.5f;
    g_mock.fail_on_xfer = g_mock.xfer_count + 1U;
    status = adt7410_read_temp_degc(&dev, &die_temp_degc);
    TH_CHECK(ADT7410_ERR_I2C_COMM == status);
    TH_CHECK(fabs((double)die_temp_degc - 42.5) <= 1.0e-7);
    th_end();
}


/* ===================== LLR-ADT-004: setpoints ========================== */

static void tc_adt_040(void)
{
    th_begin("TC-ADT-040", "LLR-ADT-004.1",
             "set_temp_setpoint range boundaries");
    setup_ready(ADT7410_RES_16BIT);
    TH_CHECK(ADT7410_OK
             == adt7410_set_temp_setpoint(&dev, ADT7410_SETPOINT_TLOW,
                                          -55000));
    TH_CHECK(ADT7410_OK
             == adt7410_set_temp_setpoint(&dev, ADT7410_SETPOINT_TCRIT,
                                          150000));
    g_mock.xfer_count = 0U;
    TH_CHECK(ADT7410_ERR_PARAM
             == adt7410_set_temp_setpoint(&dev, ADT7410_SETPOINT_TLOW,
                                          -55001));
    TH_CHECK(ADT7410_ERR_PARAM
             == adt7410_set_temp_setpoint(&dev, ADT7410_SETPOINT_TCRIT,
                                          150001));
    TH_CHECK(ADT7410_ERR_PARAM
             == adt7410_set_temp_setpoint(&dev, ADT7410_SETPOINT_THIGH,
                                          INT32_MAX));
    TH_CHECK(ADT7410_ERR_PARAM
             == adt7410_set_temp_setpoint(&dev, ADT7410_SETPOINT_THIGH,
                                          INT32_MIN));
    TH_CHECK(0U == g_mock.xfer_count);
    th_end();
}


static void tc_adt_041(void)
{
    static struct
    {
        adt7410_setpoint_id_t id;
        uint8_t reg_addr;
    } const map[] =
    {
        { ADT7410_SETPOINT_THIGH, 0x04U },
        { ADT7410_SETPOINT_TLOW,  0x06U },
        { ADT7410_SETPOINT_TCRIT, 0x08U }
    };
    int32_t limit_mdegc = 0;
    size_t i;

    th_begin("TC-ADT-041", "LLR-ADT-004.2",
             "setpoint selector -> register, invalid selector");
    setup_ready(ADT7410_RES_16BIT);
    for (i = 0U; i < (sizeof(map) / sizeof(map[0])); i++)
    {
        TH_CHECK(ADT7410_OK
                 == adt7410_set_temp_setpoint(&dev, map[i].id, 30000));
        TH_CHECK(map[i].reg_addr == mock_last_xfer(0U)->tx_bytes[0]);
        TH_CHECK(ADT7410_OK
                 == adt7410_get_temp_setpoint(&dev, map[i].id,
                                              &limit_mdegc));
        TH_CHECK(map[i].reg_addr == mock_last_xfer(0U)->reg_addr);
    }
    g_mock.xfer_count = 0U;
    TH_CHECK(ADT7410_ERR_PARAM
             == adt7410_set_temp_setpoint(&dev, (adt7410_setpoint_id_t)3,
                                          0));
    TH_CHECK(ADT7410_ERR_PARAM
             == adt7410_get_temp_setpoint(&dev, (adt7410_setpoint_id_t)3,
                                          &limit_mdegc));
    TH_CHECK(0U == g_mock.xfer_count);
    th_end();
}


static void tc_adt_042(void)
{
    static struct
    {
        int32_t  limit_mdegc;
        uint16_t reg_word;
    } const vec[] =
    {
        { 150000, 0x4B00U }, { 147000, 0x4980U }, { 64000, 0x2000U },
        { 25000, 0x0C80U },  { 4, 0x0001U },      { 3, 0x0000U },
        { 0, 0x0000U },      { -3, 0x0000U },     { -4, 0xFFFFU },
        { -25000, 0xF380U }, { -55000, 0xE480U }
    };
    mock_xfer_t const * p_xfer;
    size_t i;

    th_begin("TC-ADT-042", "LLR-ADT-004.3",
             "setpoint encoding vectors incl. rounding");
    setup_ready(ADT7410_RES_16BIT);
    for (i = 0U; i < (sizeof(vec) / sizeof(vec[0])); i++)
    {
        TH_CHECK(ADT7410_OK
                 == adt7410_set_temp_setpoint(&dev, ADT7410_SETPOINT_THIGH,
                                              vec[i].limit_mdegc));
        p_xfer = mock_last_xfer(0U);
        TH_CHECK(3U == p_xfer->tx_len_bytes);
        TH_CHECK((uint8_t)(vec[i].reg_word >> 8U) == p_xfer->tx_bytes[1]);
        TH_CHECK((uint8_t)(vec[i].reg_word & 0xFFU) == p_xfer->tx_bytes[2]);
    }
    th_end();
}


static void tc_adt_043(void)
{
    int32_t limit_mdegc;
    int32_t q_ref;
    uint16_t written;
    uint32_t mismatch = 0U;

    th_begin("TC-ADT-043", "LLR-ADT-004.3",
             "encode vs double reference, all -55000..150000");
    setup_ready(ADT7410_RES_16BIT);
    for (limit_mdegc = -55000; limit_mdegc <= 150000; limit_mdegc++)
    {
        q_ref = ref_round((double)limit_mdegc * 128.0 / 1000.0);
        if (ADT7410_OK
            != adt7410_set_temp_setpoint(&dev, ADT7410_SETPOINT_THIGH,
                                         limit_mdegc))
        {
            mismatch++;
        }
        else
        {
            written = (uint16_t)(((uint16_t)g_mock.regs[0x04] << 8U)
                                 | g_mock.regs[0x05]);
            if ((uint16_t)q_ref != written)
            {
                mismatch++;
            }
        }
    }
    TH_CHECK(0U == mismatch);
    th_end();
}


static void tc_adt_044(void)
{
    int32_t limit_mdegc = 0;
    adt7410_status_t status;

    th_begin("TC-ADT-044", "LLR-ADT-004.4",
             "get_temp_setpoint decode, defaults, failure");
    setup_ready(ADT7410_RES_16BIT);
    status = adt7410_get_temp_setpoint(&dev, ADT7410_SETPOINT_TCRIT,
                                       &limit_mdegc);
    TH_CHECK(ADT7410_OK == status);
    TH_CHECK(147000 == limit_mdegc);  /* Power-on default 0x4980 */
    status = adt7410_get_temp_setpoint(&dev, ADT7410_SETPOINT_THIGH,
                                       &limit_mdegc);
    TH_CHECK(64000 == limit_mdegc);   /* Power-on default 0x2000 */

    TH_CHECK(ADT7410_OK
             == adt7410_set_temp_setpoint(&dev, ADT7410_SETPOINT_TLOW,
                                          -55000));
    status = adt7410_get_temp_setpoint(&dev, ADT7410_SETPOINT_TLOW,
                                       &limit_mdegc);
    TH_CHECK(-55000 == limit_mdegc);

    limit_mdegc = 12345;
    g_mock.fail_on_xfer = g_mock.xfer_count + 1U;
    status = adt7410_get_temp_setpoint(&dev, ADT7410_SETPOINT_TLOW,
                                       &limit_mdegc);
    TH_CHECK(ADT7410_ERR_I2C_COMM == status);
    TH_CHECK(12345 == limit_mdegc);
    th_end();
}


/* ===================== LLR-ADT-005: hysteresis ========================= */

static void tc_adt_050(void)
{
    uint32_t hyst;
    uint32_t mismatch = 0U;

    th_begin("TC-ADT-050", "LLR-ADT-005.1",
             "set_hysteresis 0..15 written, 16..255 rejected");
    setup_ready(ADT7410_RES_16BIT);
    for (hyst = 0U; hyst < 256U; hyst++)
    {
        g_mock.regs[0x0A] = 0xEEU;
        if (hyst <= 15U)
        {
            if ((ADT7410_OK != adt7410_set_hysteresis(&dev, (uint8_t)hyst))
                || (hyst != g_mock.regs[0x0A]))
            {
                mismatch++;
            }
        }
        else
        {
            if ((ADT7410_ERR_PARAM
                 != adt7410_set_hysteresis(&dev, (uint8_t)hyst))
                || (0xEEU != g_mock.regs[0x0A]))
            {
                mismatch++;
            }
        }
    }
    TH_CHECK(0U == mismatch);
    th_end();
}


static void tc_adt_051(void)
{
    uint8_t hyst_degc = 0U;
    adt7410_status_t status;

    th_begin("TC-ADT-051", "LLR-ADT-005.2",
             "get_hysteresis masks bits [3:0], failure path");
    setup_ready(ADT7410_RES_16BIT);
    g_mock.regs[0x0A] = 0xF7U;
    status = adt7410_get_hysteresis(&dev, &hyst_degc);
    TH_CHECK(ADT7410_OK == status);
    TH_CHECK(7U == hyst_degc);
    TH_CHECK(0x0AU == mock_last_xfer(0U)->reg_addr);

    hyst_degc = 99U;
    g_mock.fail_on_xfer = g_mock.xfer_count + 1U;
    status = adt7410_get_hysteresis(&dev, &hyst_degc);
    TH_CHECK(ADT7410_ERR_I2C_COMM == status);
    TH_CHECK(99U == hyst_degc);

    g_mock.fail_on_xfer = g_mock.xfer_count + 1U;
    status = adt7410_set_hysteresis(&dev, 3U);
    TH_CHECK(ADT7410_ERR_I2C_COMM == status);
    th_end();
}


int main(void)
{
    tc_adt_001();
    tc_adt_002();
    tc_adt_003();
    tc_adt_004();
    tc_adt_005();
    tc_adt_006();
    tc_adt_007();
    tc_adt_008();
    tc_adt_009();
    tc_adt_010();
    tc_adt_011();
    tc_adt_012();
    tc_adt_020();
    tc_adt_021();
    tc_adt_022();
    tc_adt_023();
    tc_adt_024();
    tc_adt_030();
    tc_adt_031();
    tc_adt_032();
    tc_adt_033();
    tc_adt_034();
    tc_adt_035();
    tc_adt_040();
    tc_adt_041();
    tc_adt_042();
    tc_adt_043();
    tc_adt_044();
    tc_adt_050();
    tc_adt_051();

    return th_summary("adt7410_driver");
}
