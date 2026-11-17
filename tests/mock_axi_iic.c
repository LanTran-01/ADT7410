/**
 * @file    mock_axi_iic.c
 * @brief   Mock AXI IIC HAL emulating an ADT7410 (host test code only).
 */

#include "mock_axi_iic.h"

#include <string.h>

mock_iic_t g_mock;

/* ADT7410 power-on defaults (data sheet register map). */
#define MOCK_ADT7410_ID_REG         (0xCBU) /* MFG 0x19, revision 3 */
#define MOCK_ADT7410_THIGH_MSB_DEF  (0x20U) /* 64 degC              */
#define MOCK_ADT7410_TLOW_MSB_DEF   (0x05U) /* 10 degC              */
#define MOCK_ADT7410_TCRIT_MSB_DEF  (0x49U) /* 147 degC             */
#define MOCK_ADT7410_TCRIT_LSB_DEF  (0x80U)
#define MOCK_ADT7410_THYST_DEF      (0x05U) /* 5 degC               */
#define MOCK_ADT7410_RESET_CMD      (0x2FU)


static void load_power_on_defaults(void)
{
    (void)memset(g_mock.regs, 0, sizeof(g_mock.regs));
    g_mock.regs[0x04] = MOCK_ADT7410_THIGH_MSB_DEF;
    g_mock.regs[0x06] = MOCK_ADT7410_TLOW_MSB_DEF;
    g_mock.regs[0x08] = MOCK_ADT7410_TCRIT_MSB_DEF;
    g_mock.regs[0x09] = MOCK_ADT7410_TCRIT_LSB_DEF;
    g_mock.regs[0x0A] = MOCK_ADT7410_THYST_DEF;
    g_mock.regs[0x0B] = MOCK_ADT7410_ID_REG;
}


void mock_reset(void)
{
    (void)memset(&g_mock, 0, sizeof(g_mock));
    load_power_on_defaults();
    g_mock.device_addr_7b = 0x48U;
    g_mock.fail_status = AXI_IIC_ERR_NACK_ARB;
}


mock_xfer_t const * mock_last_xfer(uint32_t back)
{
    uint32_t const idx = (g_mock.xfer_count - 1U - back) % MOCK_LOG_DEPTH;

    return &g_mock.log[idx];
}


static mock_xfer_t * log_new_xfer(void)
{
    mock_xfer_t * p_xfer = &g_mock.log[g_mock.xfer_count % MOCK_LOG_DEPTH];

    (void)memset(p_xfer, 0, sizeof(*p_xfer));
    g_mock.xfer_count++;

    return p_xfer;
}


static axi_iic_status_t injected_status(void)
{
    axi_iic_status_t status = AXI_IIC_OK;

    if (g_mock.fail_on_xfer == g_mock.xfer_count)
    {
        status = g_mock.fail_status;
    }

    return status;
}


axi_iic_status_t axi_iic_write_bytes(axi_iic_dev_t const * p_dev,
                                     uint8_t slave_addr_7b,
                                     uint8_t const * p_tx_bytes,
                                     size_t tx_len_bytes)
{
    mock_xfer_t * p_xfer = log_new_xfer();
    axi_iic_status_t status = injected_status();
    size_t idx = 0U;
    uint8_t reg_ptr = 0U;

    (void)p_dev;
    p_xfer->kind = MOCK_XFER_WRITE;
    p_xfer->slave_addr_7b = slave_addr_7b;
    p_xfer->tx_len_bytes = tx_len_bytes;
    for (idx = 0U; (idx < tx_len_bytes) && (idx < MOCK_MAX_TX_BYTES); idx++)
    {
        p_xfer->tx_bytes[idx] = p_tx_bytes[idx];
    }

    if (AXI_IIC_OK != status)
    {
        /* Injected fault: device state unchanged */
    }
    else if (slave_addr_7b != g_mock.device_addr_7b)
    {
        status = AXI_IIC_ERR_NACK_ARB; /* Nobody ACKs this address */
    }
    else if ((1U == tx_len_bytes)
             && (MOCK_ADT7410_RESET_CMD == p_tx_bytes[0]))
    {
        load_power_on_defaults();
    }
    else
    {
        /* Byte 0 = address pointer, the rest auto-increment */
        reg_ptr = p_tx_bytes[0];
        for (idx = 1U; idx < tx_len_bytes; idx++)
        {
            g_mock.regs[reg_ptr % MOCK_REG_COUNT] = p_tx_bytes[idx];
            reg_ptr++;
        }
    }

    return status;
}


axi_iic_status_t axi_iic_write_read_bytes(axi_iic_dev_t const * p_dev,
                                          uint8_t slave_addr_7b,
                                          uint8_t reg_addr,
                                          uint8_t * p_rx_bytes,
                                          size_t rx_len_bytes)
{
    mock_xfer_t * p_xfer = log_new_xfer();
    axi_iic_status_t status = injected_status();
    size_t idx = 0U;

    (void)p_dev;
    p_xfer->kind = MOCK_XFER_WRITE_READ;
    p_xfer->slave_addr_7b = slave_addr_7b;
    p_xfer->reg_addr = reg_addr;
    p_xfer->rx_len_bytes = rx_len_bytes;

    if (AXI_IIC_OK != status)
    {
        /* Injected fault: leave garbage in the buffer like real HW might */
        for (idx = 0U; idx < rx_len_bytes; idx++)
        {
            p_rx_bytes[idx] = 0xEEU;
        }
    }
    else if (slave_addr_7b != g_mock.device_addr_7b)
    {
        status = AXI_IIC_ERR_NACK_ARB;
    }
    else
    {
        for (idx = 0U; idx < rx_len_bytes; idx++)
        {
            p_rx_bytes[idx] =
                g_mock.regs[((size_t)reg_addr + idx) % MOCK_REG_COUNT];
        }
    }

    return status;
}
