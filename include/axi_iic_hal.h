/**
 * @file    axi_iic_hal.h
 * @brief   Xilinx AXI IIC Hardware Abstraction Layer
 *          (100 MHz AXI clock, 100 kHz SCL, 7-bit addressing).
 * @req     LLR-IIC-001, LLR-IIC-002, LLR-IIC-003, LLR-IIC-004
 * @design  SDD-IIC-1.0
 */

#ifndef AXI_IIC_HAL_H
#define AXI_IIC_HAL_H

#include <stdint.h>
#include <stddef.h>

/**
 * @brief   Status codes for AXI IIC HAL operations.
 * @req     LLR-IIC-001
 * @design  SDD-IIC-1.1
 */
typedef enum
{
    AXI_IIC_OK            = 0U,
    AXI_IIC_ERR_PARAM     = 1U,
    AXI_IIC_ERR_STATE     = 2U,
    AXI_IIC_ERR_BUS_BUSY  = 3U,
    AXI_IIC_ERR_NACK_ARB  = 4U,
    AXI_IIC_ERR_TIMEOUT   = 5U
} axi_iic_status_t;

/**
 * @brief   SEU-resilient initialization state pattern (Hamming distance >= 4).
 * @req     LLR-IIC-001
 * @design  SDD-IIC-1.2
 * @note    Bit 31 is kept clear: C99 §6.7.2.2p2 requires every enumeration
 *          constant to be representable as an int, so a value above INT_MAX
 *          is a constraint violation under -pedantic-errors.
 *          Hamming distance UNINIT <-> READY = 31 bits.
 */
typedef enum
{
    AXI_IIC_STATE_UNINIT = 0x25C3691EU,
    AXI_IIC_STATE_READY  = 0x5A3C96E1U
} axi_iic_init_state_t;

/**
 * @brief   AXI IIC instance context structure.
 * @req     LLR-IIC-002
 * @design  SDD-IIC-1.3
 */
typedef struct
{
    uintptr_t            base_addr;     /* Physical MMIO base address, != 0 */
    uint32_t             timeout_polls; /* SR polls per wait, 1..10000000   */
    axi_iic_init_state_t init_state;    /* SEU-resilient lifecycle state    */
} axi_iic_dev_t;

/* Clock and timing constants for 100 MHz AXI clock, 100 kHz Standard Mode */
#define AXI_IIC_AXI_CLK_HZ          (100000000UL) /* 100 MHz input clock     */
#define AXI_IIC_SCL_FREQ_HZ         (100000UL)    /* 100 kHz Standard mode   */
#define AXI_IIC_7BIT_ADDR_MAX       (0x7FU)       /* Max 7-bit I2C address   */
#define AXI_IIC_MAX_XFER_BYTES      (32U)         /* Max payload / transfer  */
#define AXI_IIC_MAX_POLL_LIMIT      (10000000U)   /* Max timeout_polls (WCET)*/
#define AXI_IIC_DEFAULT_POLLS       (100000U)     /* Default WCET poll limit */

/**
 * @brief       Initialize the Xilinx AXI IIC controller in 100 kHz mode.
 * @req         LLR-IIC-002
 * @design      SDD-IIC-2.1
 * @param[out]  p_dev          Pointer to AXI IIC device instance.
 * @param[in]   base_addr      Physical MMIO base address of AXI IIC IP.
 * @param[in]   timeout_polls  Max register poll iterations,
 *                             1..AXI_IIC_MAX_POLL_LIMIT.
 * @return      AXI_IIC_OK or AXI_IIC_ERR_PARAM.
 * @note        Not ISR-safe. Reentrant across distinct p_dev instances.
 *              Non-blocking: register writes only, no polling loop.
 *              WCET bound: constant (12 MMIO accesses).
 */
axi_iic_status_t axi_iic_init(axi_iic_dev_t * p_dev,
                              uintptr_t base_addr,
                              uint32_t timeout_polls);

/**
 * @brief       Transmit a byte stream to a 7-bit I2C slave in dynamic mode.
 * @req         LLR-IIC-003
 * @design      SDD-IIC-2.2
 * @param[in]   p_dev          Pointer to initialized AXI IIC device.
 * @param[in]   slave_addr_7b  7-bit slave address, 0x00..0x7F.
 * @param[in]   p_tx_bytes     Pointer to transmit payload buffer.
 * @param[in]   tx_len_bytes   Number of bytes to send,
 *                             1..AXI_IIC_MAX_XFER_BYTES.
 * @return      AXI_IIC_OK, AXI_IIC_ERR_PARAM, AXI_IIC_ERR_STATE,
 *              AXI_IIC_ERR_BUS_BUSY, AXI_IIC_ERR_NACK_ARB,
 *              AXI_IIC_ERR_TIMEOUT.
 * @note        Not ISR-safe. Blocking (polled). Reentrant across distinct
 *              p_dev instances; callers sharing one p_dev must serialise.
 *              WCET bound: O((tx_len_bytes + 3) * timeout_polls).
 */
axi_iic_status_t axi_iic_write_bytes(axi_iic_dev_t const * p_dev,
                                     uint8_t slave_addr_7b,
                                     uint8_t const * p_tx_bytes,
                                     size_t tx_len_bytes);

/**
 * @brief       Write a register address then read bytes via Repeated Start.
 * @req         LLR-IIC-004
 * @design      SDD-IIC-2.3
 * @param[in]   p_dev          Pointer to initialized AXI IIC device.
 * @param[in]   slave_addr_7b  7-bit slave address, 0x00..0x7F.
 * @param[in]   reg_addr       Target register pointer byte in slave device.
 * @param[out]  p_rx_bytes     Destination buffer for received bytes.
 * @param[in]   rx_len_bytes   Number of bytes to read,
 *                             1..AXI_IIC_MAX_XFER_BYTES.
 * @return      AXI_IIC_OK, AXI_IIC_ERR_PARAM, AXI_IIC_ERR_STATE,
 *              AXI_IIC_ERR_BUS_BUSY, AXI_IIC_ERR_NACK_ARB,
 *              AXI_IIC_ERR_TIMEOUT.
 * @note        Not ISR-safe. Blocking (polled). Reentrant across distinct
 *              p_dev instances; callers sharing one p_dev must serialise.
 *              WCET bound: O((rx_len_bytes + 6) * timeout_polls).
 */
axi_iic_status_t axi_iic_write_read_bytes(axi_iic_dev_t const * p_dev,
                                          uint8_t slave_addr_7b,
                                          uint8_t reg_addr,
                                          uint8_t * p_rx_bytes,
                                          size_t rx_len_bytes);

#endif /* AXI_IIC_HAL_H */
