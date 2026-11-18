/**
 * @file    main.c
 * @brief   Reference application demonstrating ADT7410 setup, periodic
 *          10 ms polling mode, and hardware interrupt (INT pin) mode.
 * @req     LLR-APP-001, LLR-APP-002, LLR-APP-003, LLR-APP-004,
 *          LLR-APP-005
 * @design  SDD-APP-1.0
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "adt7410_driver.h"
#include "axi_iic_hal.h"

/* Hardware MMIO base address of the Xilinx AXI IIC IP core (platform map) */
#define APP_AXI_IIC_BASE_ADDR       ((uintptr_t)0x40800000U)

/*
 * Poll budget P per FIFO / bus wait (timing analysis: SDD-APP-4, OI-08).
 *  (1) No false timeout: P * t_poll_min must exceed the longest single
 *      wait, a 2-byte register read (~37 SCL clocks + START / repeated
 *      START / STOP set-up times) ~= 0.4 ms at 100 kHz.
 *  (2) Period: a normal cycle is bounded by 15 * P polls and a recovery
 *      cycle by 35 * P polls (analytic, every wait at its limit).
 * t_poll (one ISR + one SR AXI4-Lite read) is target dependent and must be
 * measured (OI-08). 5000 satisfies (1) only for t_poll >= 80 ns; increase
 * it if the measured t_poll is shorter.
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
 * @req     LLR-APP-002, LLR-APP-003
 * @design  SDD-APP-1.1
 * @note    One temperature representation only (degC), written by both the
 *          polling and the interrupt path, so the snapshot never mixes a
 *          fresh value with a stale one.
 */
typedef struct
{
    float    last_die_temp_degc;  /* Latest valid die temp, degC,
                                     -55.0..150.0 nominal               */
    uint32_t sample_count;        /* Number of valid samples acquired    */
    uint32_t comm_error_count;    /* Number of I2C / sensor errors seen  */
    bool     is_tcrit_tripped;    /* Latched or active TCRIT fault flag  */
    bool     is_thigh_tripped;    /* Latched or active THIGH fault flag  */
    bool     is_tlow_tripped;     /* Latched or active TLOW fault flag   */
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
static adt7410_status_t service_pending_alert(void);
static adt7410_status_t run_interrupt_example(void);

/**
 * @brief   AXI IIC HAL instance shared by every sensor on the bus.
 * @req     LLR-APP-001
 * @design  SDD-APP-1.2
 */
static axi_iic_dev_t g_iic_bus;

/**
 * @brief   ADT7410 device context (address 0x48).
 * @req     LLR-APP-001
 * @design  SDD-APP-1.2
 */
static adt7410_dev_t g_sensor;

/**
 * @brief   Latest telemetry published by the monitor examples.
 * @req     LLR-APP-002, LLR-APP-003
 * @design  SDD-APP-1.2
 */
static app_health_snapshot_t g_health_snapshot;

/**
 * @brief   Flag set by adt7410_int_pin_isr(), consumed in task context.
 * @req     LLR-APP-003
 * @design  SDD-APP-1.2
 * @note    volatile: shared with the ISR (C Coding Rules §9).
 */
static volatile bool g_is_int_alert_pending = false;


/**
 * @brief       GPIO / External Interrupt ISR for the ADT7410 INT pin.
 * @req         LLR-APP-003
 * @design      SDD-APP-2.1
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
 * @req         LLR-APP-005
 * @design      SDD-APP-2.2
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
 * @req         LLR-APP-001
 * @design      SDD-APP-3.1
 * @param[in]   event_mode  ADT7410_EVENT_COMPARATOR (polling) or
 *                          ADT7410_EVENT_INTERRUPT (latched INT pin).
 * @return      ADT7410_OK, ADT7410_ERR_PARAM, ADT7410_ERR_STATE,
 *              ADT7410_ERR_I2C_COMM, or ADT7410_ERR_DEV_ID.
 * @note        Not ISR-safe. Blocking (polled I2C).
 *              WCET bound: 35 * APP_IIC_TIMEOUT_POLLS polls
 *              (adt7410_init 12 P + 3 setpoints 18 P + hysteresis 5 P).
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
 * @req         LLR-APP-001
 * @design      SDD-APP-3.2
 * @return      ADT7410_OK, ADT7410_ERR_PARAM, ADT7410_ERR_STATE,
 *              or ADT7410_ERR_I2C_COMM.
 * @note        Not ISR-safe. Blocking (4 polled I2C write transactions).
 *              WCET bound: 23 * APP_IIC_TIMEOUT_POLLS polls.
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
 * @req         LLR-APP-002, LLR-APP-003
 * @design      SDD-APP-3.3
 * @param[in]   event_mode  Active event mode to restore on the sensor.
 * @return      ADT7410_OK on successful recovery, error code otherwise.
 * @note        Not ISR-safe. Blocking. Runs in its own cycle, never in the
 *              same cycle as the failed poll / alert service.
 *              WCET bound: 35 * APP_IIC_TIMEOUT_POLLS polls.
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
 * @req         LLR-APP-002
 * @design      SDD-APP-3.4
 * @return      ADT7410_OK or error status from status/temperature read.
 * @note        Uses adt7410_read_temp_degc() so the application needs no
 *              resolution-dependent scaling. The snapshot is updated only
 *              when both reads succeed. Not ISR-safe. Blocking.
 *              WCET bound: 15 * APP_IIC_TIMEOUT_POLLS polls.
 */
static adt7410_status_t poll_sensor_once(void)
{
    adt7410_status_t result = ADT7410_OK;
    float die_temp_degc = 0.0f;
    adt7410_status_flags_t status_flags =
    {
        .is_not_ready  = true,
        .has_tcrit_flt = false,
        .has_thigh_flt = false,
        .has_tlow_flt  = false
    };

    result = adt7410_read_status(&g_sensor, &status_flags);

    if (ADT7410_OK == result)
    {
        result = adt7410_read_temp_degc(&g_sensor, &die_temp_degc);
    }
    else
    {
        /* Status read failed; retain previous snapshot */
    }

    if (ADT7410_OK == result)
    {
        g_health_snapshot.last_die_temp_degc = die_temp_degc;
        g_health_snapshot.is_tcrit_tripped = status_flags.has_tcrit_flt;
        g_health_snapshot.is_thigh_tripped = status_flags.has_thigh_flt;
        g_health_snapshot.is_tlow_tripped  = status_flags.has_tlow_flt;
        g_health_snapshot.sample_count++;
    }
    else
    {
        /* Read failed; retain previous snapshot */
    }

    return result;
}


/**
 * @brief       Example 1: Periodic 10 ms Polling workflow (Comparator mode).
 * @req         LLR-APP-002, LLR-APP-004
 * @design      SDD-APP-3.5, SDD-APP-4
 * @return      ADT7410_OK if setup and polling cycles succeed, error otherwise.
 * @note        Each loop iteration stands for one 10 ms RTOS task wakeup.
 *              A failed poll schedules recovery for the NEXT cycle, so one
 *              cycle never does poll + recovery. The example ends with the
 *              poll error if a recovery is still pending after the last cycle.
 *              WCET per cycle: max(15, 35) * APP_IIC_TIMEOUT_POLLS polls.
 */
static adt7410_status_t run_polling_example(void)
{
    adt7410_status_t result = ADT7410_OK;
    adt7410_status_t poll_status = ADT7410_OK;
    uint32_t poll_cycle_idx = 0U;
    bool is_recovery_pending = false;

    result = setup_bus_and_sensor(ADT7410_EVENT_COMPARATOR);

    /* Bounded by APP_POLL_EXAMPLE_CYCLES */
    while ((poll_cycle_idx < APP_POLL_EXAMPLE_CYCLES)
           && (ADT7410_OK == result))
    {
        if (is_recovery_pending)
        {
            /* Recovery slot: re-initialise bus and sensor */
            result = recover_bus_and_sensor(ADT7410_EVENT_COMPARATOR);
            is_recovery_pending = false;
        }
        else
        {
            poll_status = poll_sensor_once();
            if (ADT7410_OK != poll_status)
            {
                /* Slave NACK or bus timeout: recover in the next cycle */
                is_recovery_pending = true;
            }
            else
            {
                /* Sample acquired; 10 ms RTOS delay would occur here */
            }
        }
        poll_cycle_idx++;
    }

    if (is_recovery_pending)
    {
        result = poll_status;
    }
    else
    {
        /* No unrecovered fault */
    }

    return result;
}


/**
 * @brief       Bottom-half handler servicing a latched ADT7410 INT pin alert.
 * @req         LLR-APP-003
 * @design      SDD-APP-3.6
 * @return      ADT7410_OK or error status from I2C read.
 * @note        Reading Status Register 0x02 clears the latched
 *              TLOW/THIGH/TCRIT flags and releases the INT pin
 *              (Datasheet p. 14). The snapshot is updated only when both
 *              reads succeed. Not ISR-safe. Blocking.
 *              WCET bound: 15 * APP_IIC_TIMEOUT_POLLS polls.
 */
static adt7410_status_t service_int_alert(void)
{
    adt7410_status_t result = ADT7410_OK;
    float die_temp_degc = 0.0f;
    adt7410_status_flags_t status_flags =
    {
        .is_not_ready  = false,
        .has_tcrit_flt = false,
        .has_thigh_flt = false,
        .has_tlow_flt  = false
    };

    /* 1. Read the temperature that caused the threshold excursion */
    result = adt7410_read_temp_degc(&g_sensor, &die_temp_degc);

    if (ADT7410_OK == result)
    {
        /* 2. Identify the tripped threshold AND unlatch the INT output */
        result = adt7410_read_status(&g_sensor, &status_flags);
    }
    else
    {
        /* Temperature read failed */
    }

    if (ADT7410_OK == result)
    {
        g_health_snapshot.last_die_temp_degc = die_temp_degc;
        g_health_snapshot.is_tcrit_tripped = status_flags.has_tcrit_flt;
        g_health_snapshot.is_thigh_tripped = status_flags.has_thigh_flt;
        g_health_snapshot.is_tlow_tripped  = status_flags.has_tlow_flt;
        g_health_snapshot.sample_count++;
    }
    else
    {
        /* Keep previous snapshot on communication error */
    }

    return result;
}


/**
 * @brief       Consume the ISR flag and service the alert if one is pending.
 * @req         LLR-APP-003
 * @design      SDD-APP-3.8
 * @return      ADT7410_OK (no alert, or alert serviced) or the error from
 *              service_int_alert().
 * @note        Single volatile read into a local (C Coding Rules §5). The
 *              flag is cleared before the I2C service so an INT edge that
 *              arrives during the service is not lost. If the service
 *              fails, the INT pin stays latched (no new edge will come),
 *              so the flag is set again to re-service after recovery.
 *              Not ISR-safe. WCET bound: 15 * APP_IIC_TIMEOUT_POLLS polls.
 */
static adt7410_status_t service_pending_alert(void)
{
    adt7410_status_t result = ADT7410_OK;
    bool const is_alert_pending = g_is_int_alert_pending;

    if (is_alert_pending)
    {
        g_is_int_alert_pending = false;
        result = service_int_alert();

        if (ADT7410_OK != result)
        {
            g_is_int_alert_pending = true;
        }
        else
        {
            /* Alert serviced; INT pin released by the Status read */
        }
    }
    else
    {
        /* No interrupt pending; CPU is free for other tasks */
    }

    return result;
}


/**
 * @brief       Example 2: Interrupt-driven workflow (Latched Interrupt mode).
 * @req         LLR-APP-003, LLR-APP-004
 * @design      SDD-APP-3.7, SDD-APP-4
 * @return      ADT7410_OK on success, error status otherwise.
 * @note        When the INT pin asserts, adt7410_int_pin_isr() sets the
 *              flag; this task loop services it. A failed service schedules
 *              recovery for the NEXT cycle (same rule as the polling
 *              example). WCET per cycle: max(15, 35) * TIMEOUT_POLLS polls.
 */
static adt7410_status_t run_interrupt_example(void)
{
    adt7410_status_t result = ADT7410_OK;
    adt7410_status_t alert_status = ADT7410_OK;
    uint32_t cycle_idx = 0U;
    bool is_recovery_pending = false;

    g_is_int_alert_pending = false;
    result = setup_bus_and_sensor(ADT7410_EVENT_INTERRUPT);

    /* Bounded by APP_INTR_EXAMPLE_CYCLES */
    while ((cycle_idx < APP_INTR_EXAMPLE_CYCLES)
           && (ADT7410_OK == result))
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

        if (is_recovery_pending)
        {
            result = recover_bus_and_sensor(ADT7410_EVENT_INTERRUPT);
            is_recovery_pending = false;
        }
        else
        {
            alert_status = service_pending_alert();
            if (ADT7410_OK != alert_status)
            {
                /* INT stays latched; recover, then re-service next time */
                is_recovery_pending = true;
            }
            else
            {
                /* Alert serviced (or none pending) */
            }
        }
        cycle_idx++;
    }

    if (is_recovery_pending)
    {
        result = alert_status;
    }
    else
    {
        /* No unrecovered fault */
    }

    return result;
}
