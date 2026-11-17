/**
 * @file    main.c
 * @brief   Reference application demonstrating ADT7410 setup, periodic
 *          10 ms polling mode, and hardware interrupt (INT pin) mode.
 * @req     LLR-IIC-002, LLR-ADT-001, LLR-ADT-002, LLR-ADT-003,
 *          LLR-ADT-004, LLR-ADT-005
 * @design  SDD-ADT-1.0, SDD-IIC-1.0
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "adt7410_driver.h"
#include "axi_iic_hal.h"

/* Hardware MMIO base address of the Xilinx AXI IIC IP core (platform map) */
#define APP_AXI_IIC_BASE_ADDR       ((uintptr_t)0x40800000U)

/*
 * Poll budget per FIFO/bus wait for a 10 ms periodic task:
 * 5000 polls (~250..500 us on typical AXI4-Lite bus) exceeds the ~90 us
 * single-byte I2C transfer time at 100 kHz while guaranteeing that a stuck
 * bus times out in < 0.5 ms without overrunning the 10 ms task period.
 */
#define APP_IIC_TIMEOUT_POLLS       (5000U)

/* Temperature alarm thresholds in milli-degrees Celsius (Datasheet Table 1) */
#define APP_THIGH_LIMIT_MDEGC       (85000)   /* +85.000 degC high alarm     */
#define APP_TLOW_LIMIT_MDEGC        (-10000)  /* -10.000 degC low alarm      */
#define APP_TCRIT_LIMIT_MDEGC       (105000)  /* +105.000 degC critical trip */
#define APP_HYST_LIMIT_DEGC         (5U)      /* 5 degC hysteresis (0..15)   */

/* Bounded demonstration loop iteration counts (WCET / DO-178C §6.3.4f) */
#define APP_POLL_EXAMPLE_CYCLES     (10U)     /* 10 x 10 ms polling ticks    */
#define APP_INTR_EXAMPLE_CYCLES     (10U)     /* 10 background check cycles  */
#define APP_SIMULATED_IRQ_CYCLE     (3U)      /* Cycle simulating INT assert */

/**
 * @brief   Application telemetry snapshot updated by the monitor examples.
 * @req     LLR-ADT-003
 * @design  SDD-ADT-1.11
 */
typedef struct
{
    int32_t  last_die_temp_mdegc; /* Latest valid temperature in mdegC     */
    int16_t  last_die_temp_q8_7;  /* Latest valid temperature in Q8.7 degC */
    uint32_t sample_count;        /* Number of valid samples acquired      */
    uint32_t comm_error_count;    /* Number of I2C / sensor errors seen    */
    bool     is_tcrit_tripped;    /* Latched or active TCRIT fault flag    */
    bool     is_thigh_tripped;    /* Latched or active THIGH fault flag    */
    bool     is_tlow_tripped;     /* Latched or active TLOW fault flag     */
} app_health_snapshot_t;

/* Public ISR and entry-point prototypes (MISRA C:2012 Rule 8.4) */
void adt7410_int_pin_isr(void);
int main(void);

/* Private helper prototypes */
static adt7410_status_t setup_bus_and_sensor(adt7410_event_mode_t event_mode);
static adt7410_status_t configure_alarm_thresholds(void);
static adt7410_status_t recover_bus_and_sensor(
    adt7410_event_mode_t event_mode);
static adt7410_status_t poll_sensor_once(void);
static adt7410_status_t run_polling_example(void);
static adt7410_status_t service_int_alert(void);
static adt7410_status_t run_interrupt_example(void);

/* Module-scope device contexts and telemetry (no stack buffers > 64 B) */
static axi_iic_dev_t g_iic_bus;
static adt7410_dev_t g_sensor;
static app_health_snapshot_t g_health_snapshot;

/*
 * Volatile flag shared exclusively between adt7410_int_pin_isr() and task
 * context (C Coding Rules §9: ISR sets flag only; task performs I2C).
 */
static volatile bool g_is_int_alert_pending = false;


/**
 * @brief       GPIO / External Interrupt ISR for the ADT7410 INT pin.
 * @req         LLR-ADT-002, LLR-ADT-003
 * @design      SDD-ADT-2.6
 * @note        ISR-safe. Deterministic O(1), no blocking loops, no I2C
 *              access inside ISR. Sets a volatile flag for deferred task
 *              processing per C Coding Rules §9.
 *              WCET bound: constant (1 volatile store).
 */
void adt7410_int_pin_isr(void)
{
    g_is_int_alert_pending = true;
}


/**
 * @brief       Application entry point running both Polling and Interrupt
 *              reference examples sequentially.
 * @req         LLR-IIC-002, LLR-ADT-001, LLR-ADT-002, LLR-ADT-003,
 *              LLR-ADT-004, LLR-ADT-005
 * @design      SDD-ADT-1.0, SDD-IIC-1.0
 * @return      0 on success, 1 if either example returned an error.
 * @note        Not ISR-safe. Called once at startup.
 */
int main(void)
{
    int exit_code = 0;
    adt7410_status_t status = ADT7410_OK;

    /* Example 1: Periodic 10 ms Polling in Comparator mode */
    status = run_polling_example();

    if (ADT7410_OK == status)
    {
        /* Example 2: Deferred Interrupt Handling in Latched Interrupt mode */
        status = run_interrupt_example();
    }
    else
    {
        /* Polling example failed; skip interrupt example */
    }

    if (ADT7410_OK != status)
    {
        exit_code = 1;
    }
    else
    {
        exit_code = 0;
    }

    return exit_code;
}


/**
 * @brief       Initialize AXI IIC HAL, ADT7410 sensor, and alarm setpoints.
 * @req         LLR-IIC-002, LLR-ADT-001, LLR-ADT-002
 * @design      SDD-IIC-2.1, SDD-ADT-2.1
 * @param[in]   event_mode  ADT7410_EVENT_COMPARATOR (polling) or
 *                          ADT7410_EVENT_INTERRUPT (latched INT pin).
 * @return      ADT7410_OK, ADT7410_ERR_PARAM, ADT7410_ERR_I2C_COMM,
 *              or ADT7410_ERR_DEV_ID.
 * @note        Not ISR-safe. Blocking (polled I2C).
 *              WCET bound: O(36 * APP_IIC_TIMEOUT_POLLS).
 */
static adt7410_status_t setup_bus_and_sensor(adt7410_event_mode_t event_mode)
{
    adt7410_status_t result = ADT7410_OK;
    axi_iic_status_t iic_status = AXI_IIC_OK;
    adt7410_config_t const sensor_cfg =
    {
        .resolution   = ADT7410_RES_16BIT,
        .op_mode      = ADT7410_MODE_CONTINUOUS,
        .event_mode   = event_mode,
        .int_polarity = ADT7410_POL_ACTIVE_LOW,
        .ct_polarity  = ADT7410_POL_ACTIVE_LOW,
        .fault_queue  = ADT7410_FAULT_QUEUE_2
    };

    iic_status = axi_iic_init(&g_iic_bus,
                              APP_AXI_IIC_BASE_ADDR,
                              APP_IIC_TIMEOUT_POLLS);
    if (AXI_IIC_OK != iic_status)
    {
        result = ADT7410_ERR_I2C_COMM;
    }
    else
    {
        result = adt7410_init(&g_sensor,
                              &g_iic_bus,
                              ADT7410_I2C_ADDR_A1L_A0L,
                              &sensor_cfg);
    }

    if (ADT7410_OK == result)
    {
        result = configure_alarm_thresholds();
    }
    else
    {
        /* Initialization failed; skip threshold programming */
    }

    return result;
}


/**
 * @brief       Program THIGH, TLOW, TCRIT setpoints and THYST hysteresis.
 * @req         LLR-ADT-004, LLR-ADT-005
 * @design      SDD-ADT-2.9, SDD-ADT-2.11
 * @return      ADT7410_OK, ADT7410_ERR_PARAM, ADT7410_ERR_STATE,
 *              or ADT7410_ERR_I2C_COMM.
 * @note        Not ISR-safe. Blocking (4 polled I2C write transactions).
 *              WCET bound: O(23 * APP_IIC_TIMEOUT_POLLS).
 */
static adt7410_status_t configure_alarm_thresholds(void)
{
    adt7410_status_t result = ADT7410_OK;

    result = adt7410_set_temp_setpoint(&g_sensor,
                                       ADT7410_SETPOINT_THIGH,
                                       APP_THIGH_LIMIT_MDEGC);

    if (ADT7410_OK == result)
    {
        result = adt7410_set_temp_setpoint(&g_sensor,
                                           ADT7410_SETPOINT_TLOW,
                                           APP_TLOW_LIMIT_MDEGC);
    }
    else
    {
        /* Keep THIGH error */
    }

    if (ADT7410_OK == result)
    {
        result = adt7410_set_temp_setpoint(&g_sensor,
                                           ADT7410_SETPOINT_TCRIT,
                                           APP_TCRIT_LIMIT_MDEGC);
    }
    else
    {
        /* Keep prior setpoint error */
    }

    if (ADT7410_OK == result)
    {
        result = adt7410_set_hysteresis(&g_sensor, APP_HYST_LIMIT_DEGC);
    }
    else
    {
        /* Keep prior setpoint error */
    }

    return result;
}


/**
 * @brief       Recover AXI IIC controller and ADT7410 after a bus/NACK fault.
 * @req         LLR-IIC-002, LLR-ADT-001
 * @design      SDD-IIC-2.1, SDD-ADT-2.1
 * @param[in]   event_mode  Active event mode to restore on the sensor.
 * @return      ADT7410_OK on successful recovery, error code otherwise.
 * @note        Not ISR-safe. Blocking. Invoked when a periodic poll or alert
 *              read returns ADT7410_ERR_I2C_COMM or ADT7410_ERR_STATE.
 *              WCET bound: O(36 * APP_IIC_TIMEOUT_POLLS).
 */
static adt7410_status_t recover_bus_and_sensor(adt7410_event_mode_t event_mode)
{
    adt7410_status_t result = ADT7410_OK;

    g_health_snapshot.comm_error_count++;
    result = setup_bus_and_sensor(event_mode);

    return result;
}


/**
 * @brief       Execute one non-hanging 10 ms polling step.
 * @req         LLR-ADT-003
 * @design      SDD-ADT-2.6, SDD-ADT-2.8
 * @return      ADT7410_OK or error status from status/temperature read.
 * @note        Checks Status Register (0x02) first; reads Temperature (0x00)
 *              only when RDY bit indicates a completed conversion, or reads
 *              the latest register value directly. Not ISR-safe. Blocking.
 *              WCET bound: O(15 * APP_IIC_TIMEOUT_POLLS).
 */
static adt7410_status_t poll_sensor_once(void)
{
    adt7410_status_t result = ADT7410_OK;
    adt7410_status_flags_t status_flags =
    {
        .is_not_ready  = true,
        .has_tcrit_flt = false,
        .has_thigh_flt = false,
        .has_tlow_flt  = false
    };
    adt7410_sample_t sample =
    {
        .die_temp_mdegc   = 0,
        .die_temp_q8_7    = 0,
        .has_inline_flags = false,
        .is_tcrit_active  = false,
        .is_thigh_active  = false,
        .is_tlow_active   = false
    };

    result = adt7410_read_status(&g_sensor, &status_flags);

    if (ADT7410_OK == result)
    {
        g_health_snapshot.is_tcrit_tripped = status_flags.has_tcrit_flt;
        g_health_snapshot.is_thigh_tripped = status_flags.has_thigh_flt;
        g_health_snapshot.is_tlow_tripped  = status_flags.has_tlow_flt;

        result = adt7410_read_temperature(&g_sensor, &sample);
        if (ADT7410_OK == result)
        {
            g_health_snapshot.last_die_temp_mdegc = sample.die_temp_mdegc;
            g_health_snapshot.last_die_temp_q8_7  = sample.die_temp_q8_7;
            g_health_snapshot.sample_count++;
        }
        else
        {
            /* Temperature read failed; retain previous sample */
        }
    }
    else
    {
        /* Status read failed; retain previous snapshot */
    }

    return result;
}


/**
 * @brief       Example 1: Periodic 10 ms Polling workflow (Comparator mode).
 * @req         LLR-IIC-002, LLR-ADT-001, LLR-ADT-002, LLR-ADT-003,
 *              LLR-ADT-004, LLR-ADT-005
 * @design      SDD-ADT-2.1, SDD-ADT-2.6, SDD-ADT-2.8
 * @return      ADT7410_OK if setup and polling cycles succeed, error otherwise.
 * @note        In production firmware, the body of the for-loop runs once
 *              per 10 ms RTOS task wakeup or timer tick. If the slave does
 *              not ACK, poll_sensor_once() returns ADT7410_ERR_I2C_COMM
 *              within APP_IIC_TIMEOUT_POLLS and triggers recovery.
 *              WCET bound: O(APP_POLL_EXAMPLE_CYCLES * 51 * TIMEOUT_POLLS).
 */
static adt7410_status_t run_polling_example(void)
{
    adt7410_status_t result = ADT7410_OK;
    uint32_t poll_cycle_idx = 0U;

    result = setup_bus_and_sensor(ADT7410_EVENT_COMPARATOR);

    for (poll_cycle_idx = 0U;
         (poll_cycle_idx < APP_POLL_EXAMPLE_CYCLES)
         && (ADT7410_OK == result);
         poll_cycle_idx++)
    {
        result = poll_sensor_once();
        if (ADT7410_OK != result)
        {
            /* Slave NACK or bus timeout: attempt bounded recovery */
            result = recover_bus_and_sensor(ADT7410_EVENT_COMPARATOR);
        }
        else
        {
            /* Sample acquired; 10 ms RTOS delay would occur here */
        }
    }

    return result;
}


/**
 * @brief       Bottom-half handler servicing a latched ADT7410 INT pin alert.
 * @req         LLR-ADT-002, LLR-ADT-003
 * @design      SDD-ADT-2.6, SDD-ADT-2.8
 * @return      ADT7410_OK or error status from I2C read.
 * @note        Executed in task context after adt7410_int_pin_isr() sets
 *              g_is_int_alert_pending. Reading Status Register 0x02 clears
 *              the latched TLOW/THIGH/TCRIT flags and releases the INT pin
 *              (Datasheet p. 14). Not ISR-safe. Blocking.
 *              WCET bound: O(15 * APP_IIC_TIMEOUT_POLLS).
 */
static adt7410_status_t service_int_alert(void)
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
    adt7410_status_flags_t status_flags =
    {
        .is_not_ready  = false,
        .has_tcrit_flt = false,
        .has_thigh_flt = false,
        .has_tlow_flt  = false
    };

    /* 1. Read the temperature that caused the threshold excursion */
    result = adt7410_read_temperature(&g_sensor, &sample);

    if (ADT7410_OK == result)
    {
        g_health_snapshot.last_die_temp_mdegc = sample.die_temp_mdegc;
        g_health_snapshot.last_die_temp_q8_7  = sample.die_temp_q8_7;
        g_health_snapshot.sample_count++;

        /*
         * 2. Read Status Register (0x02) to identify which threshold tripped
         *    AND unlatch the ADT7410 INT output pin in Interrupt mode.
         */
        result = adt7410_read_status(&g_sensor, &status_flags);
    }
    else
    {
        /* Temperature read failed */
    }

    if (ADT7410_OK == result)
    {
        g_health_snapshot.is_tcrit_tripped = status_flags.has_tcrit_flt;
        g_health_snapshot.is_thigh_tripped = status_flags.has_thigh_flt;
        g_health_snapshot.is_tlow_tripped  = status_flags.has_tlow_flt;
    }
    else
    {
        /* Keep fault flags unchanged on communication error */
    }

    return result;
}


/**
 * @brief       Example 2: Interrupt-driven workflow (Latched Interrupt mode).
 * @req         LLR-IIC-002, LLR-ADT-001, LLR-ADT-002, LLR-ADT-003,
 *              LLR-ADT-004, LLR-ADT-005
 * @design      SDD-ADT-2.1, SDD-ADT-2.6, SDD-ADT-2.8
 * @return      ADT7410_OK on success, error status otherwise.
 * @note        Configures ADT7410 in ADT7410_EVENT_INTERRUPT mode. When the
 *              hardware INT pin asserts, the GPIO controller invokes
 *              adt7410_int_pin_isr(), which sets g_is_int_alert_pending.
 *              This task loop detects the flag, clears it, and performs the
 *              I2C read in task context to unlatch the interrupt.
 *              WCET bound: O(APP_INTR_EXAMPLE_CYCLES * 51 * TIMEOUT_POLLS).
 */
static adt7410_status_t run_interrupt_example(void)
{
    adt7410_status_t result = ADT7410_OK;
    uint32_t cycle_idx = 0U;
    bool is_alert_pending = false;

    g_is_int_alert_pending = false;
    result = setup_bus_and_sensor(ADT7410_EVENT_INTERRUPT);

    for (cycle_idx = 0U;
         (cycle_idx < APP_INTR_EXAMPLE_CYCLES)
         && (ADT7410_OK == result);
         cycle_idx++)
    {
        if (APP_SIMULATED_IRQ_CYCLE == cycle_idx)
        {
            /* Simulate hardware GPIO interrupt firing on INT pin assertion */
            adt7410_int_pin_isr();
        }
        else
        {
            /* Normal cycle: ISR only fires when hardware asserts INT */
        }

        /* Single volatile read into local variable (C Coding Rules §5) */
        is_alert_pending = g_is_int_alert_pending;

        if (is_alert_pending)
        {
            g_is_int_alert_pending = false;
            result = service_int_alert();

            if (ADT7410_OK != result)
            {
                result = recover_bus_and_sensor(ADT7410_EVENT_INTERRUPT);
            }
            else
            {
                /* Alert serviced and INT pin unlatched */
            }
        }
        else
        {
            /* No interrupt pending; CPU is free for other tasks */
        }
    }

    return result;
}
