/**
 * @file    test_app.c
 * @brief   Host tests for the reference application src/main.c.
 *
 * src/main.c is compiled with -Dmain=app_main (test build only) and linked
 * with the real adt7410_driver.c and the mock HAL. The mock counts I2C
 * transactions, so each scenario checks both the return code and the exact
 * number of bus transactions the application performed.
 *
 * Transaction budget used below:
 *   setup (ID read, config, 3 setpoints, hysteresis) = 6 transactions
 *   one poll / alert service (status + temperature)  = 2 transactions
 *   polling example  = 6 + 10 x 2 = 26
 *   interrupt example = 6 + 1 alert x 2 = 8          -> healthy total 34
 */

#include "adt7410_driver.h"
#include "mock_axi_iic.h"
#include "test_harness.h"

int app_main(void);

/* Status the stubbed axi_iic_init() returns (fault injection). */
static axi_iic_status_t iic_init_status = AXI_IIC_OK;

/* The mock HAL has no axi_iic_init(); provide one for the application. */
axi_iic_status_t axi_iic_init(axi_iic_dev_t * p_dev,
                              uintptr_t base_addr,
                              uint32_t timeout_polls)
{
    p_dev->base_addr = base_addr;
    p_dev->timeout_polls = timeout_polls;
    p_dev->init_state = AXI_IIC_STATE_READY;

    return iic_init_status;
}


static void run_app(uint32_t fail_on_xfer,
                    int expected_rc,
                    uint32_t expected_xfers)
{
    int rc;

    mock_reset();
    g_mock.fail_on_xfer = fail_on_xfer;
    rc = app_main();
    TH_CHECK(expected_rc == rc);
    TH_CHECK(expected_xfers == g_mock.xfer_count);
}


static void tc_app_001(void)
{
    th_begin("TC-APP-001", "LLR-APP-001,002,003,005",
             "healthy run: setup, 10 polls, 1 INT alert, rc 0");
    run_app(0U, 0, 34U);
    /* INT service order: temperature (0x00) first, then Status (0x02) */
    TH_CHECK(0x00U == mock_last_xfer(1U)->reg_addr);
    TH_CHECK(0x02U == mock_last_xfer(0U)->reg_addr);
    th_end();
}


static void tc_app_002(void)
{
    th_begin("TC-APP-002", "LLR-APP-001",
             "setup programs 16-bit/comparator config and thresholds");
    run_app(0U, 0, 34U);
    /* Interrupt example ran last: config 0x80 | FQ2 (0x01), event = INT */
    TH_CHECK(0x81U == g_mock.regs[0x03]);
    /* 85 degC = 0x2A80, -10 degC = 0xFB00, 105 degC = 0x3480 (Q8.7) */
    TH_CHECK((0x2AU == g_mock.regs[0x04]) && (0x80U == g_mock.regs[0x05]));
    TH_CHECK((0xFBU == g_mock.regs[0x06]) && (0x00U == g_mock.regs[0x07]));
    TH_CHECK((0x34U == g_mock.regs[0x08]) && (0x80U == g_mock.regs[0x09]));
    TH_CHECK(5U == g_mock.regs[0x0A]);
    th_end();
}


static void tc_app_003(void)
{
    th_begin("TC-APP-003", "LLR-APP-002,004",
             "poll fails in cycle 0 -> recovery alone in cycle 1, rc 0");
    /* 6 setup + 1 failed + 6 recovery + 8 x 2 polls + 8 interrupt = 37 */
    run_app(7U, 0, 37U);
    th_end();
}


static void tc_app_004(void)
{
    th_begin("TC-APP-004", "LLR-APP-002",
             "unrecovered fault (last cycle / failed recovery) -> rc 1");
    run_app(25U, 1, 25U);         /* Poll fails in the last cycle */

    mock_reset();                 /* Poll fails, then its recovery fails */
    g_mock.fail_on_xfer = 7U;
    g_mock.fail_xfer_count = 2U;
    TH_CHECK(1 == app_main());
    TH_CHECK(8U == g_mock.xfer_count);
    th_end();
}


static void tc_app_005(void)
{
    th_begin("TC-APP-005", "LLR-APP-003,004",
             "INT service fails -> recovery -> alert re-serviced, rc 0");
    /* 26 polling + 6 setup + 1 failed + 6 recovery + 2 re-service = 41 */
    run_app(33U, 0, 41U);
    TH_CHECK(0x02U == mock_last_xfer(0U)->reg_addr); /* Status re-read */
    th_end();
}


static void tc_app_006(void)
{
    th_begin("TC-APP-006", "LLR-APP-001,005",
             "sensor init fails -> no polling, rc 1");
    run_app(1U, 1, 1U);
    th_end();
}


static void tc_app_007(void)
{
    int rc;

    th_begin("TC-APP-007", "LLR-APP-001",
             "AXI IIC HAL init fails -> ERR, no sensor access, rc 1");
    mock_reset();
    iic_init_status = AXI_IIC_ERR_PARAM;
    rc = app_main();
    iic_init_status = AXI_IIC_OK;
    TH_CHECK(1 == rc);
    TH_CHECK(0U == g_mock.xfer_count);
    th_end();
}


static void tc_app_008(void)
{
    uint32_t xfer;

    th_begin("TC-APP-008", "LLR-APP-001",
             "THIGH / TLOW / TCRIT / THYST write fails -> setup stops");
    for (xfer = 3U; xfer <= 6U; xfer++)
    {
        run_app(xfer, 1, xfer);  /* Stops right after the failed write */
        /* Polling set-up: 16-bit 0x80 | comparator 0x10 | FQ2 0x01 */
        TH_CHECK(0x91U == g_mock.regs[0x03]);
    }
    th_end();
}


static void tc_app_009(void)
{
    th_begin("TC-APP-009", "LLR-APP-003,004",
             "INT service fails and the recovery fails too -> rc 1");
    mock_reset();
    g_mock.fail_on_xfer = 33U;     /* Alert temperature read */
    g_mock.fail_xfer_count = 2U;   /* ...and the first recovery transfer */
    TH_CHECK(1 == app_main());
    TH_CHECK(34U == g_mock.xfer_count);
    th_end();
}


static void tc_app_010(void)
{
    th_begin("TC-APP-010", "LLR-APP-002,003,004",
             "temperature reads always fail: alternating recovery slots");
    /* Polling: 6 + 5 x (status ok, temp fail) + 5 x 6 recovery = 46,
     * pending cleared by the last-cycle recovery -> example returns OK.
     * Interrupt: 6 setup, alert fails on cycles 3/5/7/9 (1 transfer each),
     * recoveries on cycles 4/6/8 -> 46 + 6 + 4 + 18 = 74, still pending
     * after the last cycle -> rc 1. */
    mock_reset();
    g_mock.is_fail_reg_enabled = true;
    g_mock.fail_reg_addr = 0x00U;
    TH_CHECK(1 == app_main());
    TH_CHECK(74U == g_mock.xfer_count);
    th_end();
}


int main(void)
{
    tc_app_001();
    tc_app_002();
    tc_app_003();
    tc_app_004();
    tc_app_005();
    tc_app_006();
    tc_app_007();
    tc_app_008();
    tc_app_009();
    tc_app_010();

    return th_summary("application");
}
