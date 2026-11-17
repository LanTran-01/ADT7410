/**
 * @file    mock_axi_iic.h
 * @brief   Mock of the AXI IIC HAL that emulates an ADT7410 register file.
 *
 * Replaces axi_iic_hal.c when unit-testing adt7410_driver.c on the host.
 * It records every transaction so tests can check the exact bytes the
 * driver put on the bus. It can also inject HAL error codes.
 */

#ifndef MOCK_AXI_IIC_H
#define MOCK_AXI_IIC_H

#include <stddef.h>
#include <stdint.h>
#include "axi_iic_hal.h"

#define MOCK_REG_COUNT      (0x30U) /* Covers ADT7410 registers 0x00..0x2F */
#define MOCK_LOG_DEPTH      (16U)   /* Last transactions kept for checks   */
#define MOCK_MAX_TX_BYTES   (8U)

typedef enum
{
    MOCK_XFER_WRITE = 0,
    MOCK_XFER_WRITE_READ = 1
} mock_xfer_kind_t;

typedef struct
{
    mock_xfer_kind_t kind;
    uint8_t          slave_addr_7b;
    uint8_t          tx_bytes[MOCK_MAX_TX_BYTES]; /* write: payload        */
    size_t           tx_len_bytes;
    uint8_t          reg_addr;                    /* write_read: pointer   */
    size_t           rx_len_bytes;
} mock_xfer_t;

typedef struct
{
    uint8_t          regs[MOCK_REG_COUNT]; /* Emulated ADT7410 registers   */
    uint8_t          device_addr_7b;       /* Address the "sensor" ACKs    */
    uint32_t         xfer_count;           /* Transactions since reset     */
    mock_xfer_t      log[MOCK_LOG_DEPTH];  /* Ring of recent transactions  */
    uint32_t         fail_on_xfer;         /* 1-based index, 0 = never     */
    axi_iic_status_t fail_status;          /* Status returned on failure   */
} mock_iic_t;

extern mock_iic_t g_mock;

/* Reset mock: ADT7410 power-on register defaults, address 0x48. */
void mock_reset(void);

/* Return the n-th most recent transaction (0 = last). */
mock_xfer_t const * mock_last_xfer(uint32_t back);

#endif /* MOCK_AXI_IIC_H */
