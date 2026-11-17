
Low-Level Requirements (LLR) — ADT7410 Driver & AXI IIC HAL
Field	Value
Document ID	LLR-ADT-IIC
Revision	Draft A
Software items	axi_iic_hal.c/.h, adt7410_driver.c/.h
Design document	SDD-ADT-IIC (SDD_ADT7410_AXI_IIC.md)
Coding standard	C Coding Rules v3 (MISRA C:2012 + BARR-C:2018 + DO-178C)
Target DAL	Up to DAL A (per project plan)
Status	Draft — derived from source code, pending review against HLR
1. Purpose and scope
This document states the Low-Level Requirements implemented by:

AXI IIC HAL (LLR-IIC-xxx): a polled driver for the Xilinx AXI IIC soft IP (PG090) running at 100 MHz AXI clock, 100 kHz SCL, 7-bit addressing, dynamic-mode transfers.
ADT7410 driver (LLR-ADT-xxx): a driver for the Analog Devices ADT7410 16-bit I2C temperature sensor, built on top of the AXI IIC HAL.
Each LLR is a group ID: the ID is the one used in the source @req tags. Each group holds numbered atomic clauses (LLR-ADT-003.4). Test cases should trace to the clause level. Source code traces to the group level.

1.1 Conventions
shall: mandatory, verifiable requirement.
Derived = Yes: the requirement does not trace to a High-Level Requirement (HLR). It came from design choices. Per DO-178C §5.2.2, derived requirements must be given to the system safety assessment process.
Verification method: T = Test, A = Analysis, R = Review/Inspection.
P = timeout_polls value of the AXI IIC instance (1..10 000 000).
1.2 References
Ref	Document
[PG090]	Xilinx AXI IIC Bus Interface LogiCORE IP Product Guide
[ADT7410]	Analog Devices ADT7410 Data Sheet
[RULES]	C Coding Rules v3 (C_rules_v3_addDO178C.md)
[SDD]	SDD_ADT7410_AXI_IIC.md
1.3 Parent HLR traceability
Open item OI-01: this project baseline has no HLR document, so the parent HLR column reads HLR-TBD. Link each LLR group to its HLR, or mark it derived, before baseline.

2. AXI IIC HAL requirements
LLR-IIC-001 — Register access, status codes and lifecycle state
Field	Value
Parent HLR	HLR-TBD
Derived	Partly (001.3 is derived)
Design	SDD-IIC-1.1, SDD-IIC-1.2, SDD-IIC-3.1, SDD-IIC-3.2
Clause	Requirement	Method
001.1	The HAL shall access AXI IIC registers only through one 32-bit volatile read accessor and one 32-bit volatile write accessor. Each accessor performs exactly one bus access.	R, A
001.2	Every public HAL function that can fail shall return axi_iic_status_t with the values AXI_IIC_OK = 0, ERR_PARAM = 1, ERR_STATE = 2, ERR_BUS_BUSY = 3, ERR_NACK_ARB = 4, ERR_TIMEOUT = 5.	R
001.3	The HAL shall encode the instance lifecycle as AXI_IIC_STATE_UNINIT = 0x25C3691E and AXI_IIC_STATE_READY = 0x5A3C96E1 (Hamming distance 31). It shall treat any value other than READY as "not initialised". (Derived: SEU robustness.)	R, T
LLR-IIC-002 — Controller initialisation
Field	Value
Parent HLR	HLR-TBD
Derived	No
Design	SDD-IIC-2.1, SDD-IIC-3.3, SDD-IIC-3.9
Clause	Requirement	Method
002.1	axi_iic_init() shall return AXI_IIC_ERR_PARAM without any register access when p_dev is NULL, base_addr is 0, timeout_polls is 0, or timeout_polls is greater than 10 000 000.	T
002.2	On valid arguments, axi_iic_init() shall store base_addr and timeout_polls in the instance and set init_state to UNINIT before any register access.	T
002.3	axi_iic_init() shall issue a soft reset by writing 0x0000000A to SOFTR (offset 0x040).	T
002.4	axi_iic_init() shall program the timing registers with the following tick counts (10 ns per tick): TSUSTA = 570, TSUSTO = 500, THDSTA = 430, TSUDAT = 55, TBUF = 500, THIGH = 493, TLOW = 493, THDDAT = 30.	T, A
002.5	axi_iic_init() shall write 0x0F to RX_FIFO_PIRQ (offset 0x120).	T
002.6	axi_iic_init() shall reset the TX FIFO and leave the controller enabled (CR.EN = 1). It shall then clear any latched ISR ARB_LOST or TX_ERR bit by toggle-on-write.	T
002.7	axi_iic_init() shall set init_state to READY only after clauses 002.3–002.6 complete, and then return AXI_IIC_OK.	T
LLR-IIC-003 — Master write transfer
Field	Value
Parent HLR	HLR-TBD
Derived	No
Design	SDD-IIC-2.2, SDD-IIC-3.3 … 3.6, 3.8, 3.10 … 3.12
Clause	Requirement	Method
003.1	axi_iic_write_bytes() shall return AXI_IIC_ERR_PARAM without bus access when p_dev or p_tx_bytes is NULL, slave_addr_7b > 0x7F, or tx_len_bytes is outside 1..32.	T
003.2	axi_iic_write_bytes() shall return AXI_IIC_ERR_STATE without bus access when the instance is not READY and every argument is valid.	T
003.3	Before the first frame, the HAL shall reset the TX FIFO, clear latched ISR errors, and wait until SR.BB = 0. If BB is still set after P polls, it shall return AXI_IIC_ERR_BUS_BUSY.	T
003.4	The HAL shall send the TX_FIFO words in this order: START | (addr << 1) | 0, then each payload byte in order. The last payload byte shall carry the STOP flag (0x200).	T
003.5	Before each TX_FIFO write, the HAL shall wait while SR.TX_FULL = 1. It shall return AXI_IIC_ERR_TIMEOUT after P polls.	T
003.6	While waiting in any poll loop, if ISR.ARB_LOST or ISR.TX_ERR is set, the HAL shall reset the TX FIFO, clear those ISR bits, and return AXI_IIC_ERR_NACK_ARB.	T
003.7	After the last word, the HAL shall wait until SR.TX_EMPTY = 1 and SR.BB = 0. It shall return AXI_IIC_ERR_TIMEOUT after P polls. Otherwise it shall make a final ISR error check per 003.6.	T
003.8	Every poll loop shall be bounded by P iterations. The total poll iterations of one call shall not exceed (tx_len_bytes + 3) × P.	A
LLR-IIC-004 — Register read with repeated START
Field	Value
Parent HLR	HLR-TBD
Derived	No
Design	SDD-IIC-2.3, SDD-IIC-3.7, SDD-IIC-3.10, SDD-IIC-3.11
Clause	Requirement	Method
004.1	axi_iic_write_read_bytes() shall apply the argument and state checks of 003.1 and 003.2, with p_rx_bytes and rx_len_bytes in place of the TX arguments.	T
004.2	The HAL shall send the TX_FIFO words in this order: START | (addr << 1) | 0, reg_addr, START | (addr << 1) | 1, STOP | rx_len_bytes.	T
004.3	The HAL shall read rx_len_bytes bytes from RX_FIFO bits [7:0] into p_rx_bytes[0..n-1] in order. Before each read it shall wait while SR.RX_EMPTY = 1, and return AXI_IIC_ERR_TIMEOUT after P polls.	T
004.4	Bus-idle wait, error detection and completion shall follow 003.3, 003.6 and 003.7.	T
004.5	The total poll iterations of one call shall not exceed (rx_len_bytes + 6) × P.	A
3. ADT7410 driver requirements
LLR-ADT-001 — Initialisation, identification, reset, status and state
Field	Value
Parent HLR	HLR-TBD
Derived	Partly (001.2, 001.10 are derived)
Design	SDD-ADT-1.1, 1.2, 1.12, 2.1 … 2.3, 3.3, 3.6, 3.8, 3.9
Clause	Requirement	Method
001.1	Every driver function that can fail shall return adt7410_status_t with the values OK = 0, ERR_PARAM = 1, ERR_STATE = 2, ERR_I2C_COMM = 3, ERR_DEV_ID = 4, ERR_NOT_READY = 5.	R
001.2	The driver shall map every AXI IIC HAL status other than AXI_IIC_OK to ADT7410_ERR_I2C_COMM. (Derived.)	T
001.3	adt7410_init() shall return ADT7410_ERR_PARAM without writing memory when p_dev is NULL.	T
001.4	When p_dev is not NULL, adt7410_init() shall set p_dev->init_state = UNINIT before any other check. It shall then return ADT7410_ERR_PARAM without I2C access if p_iic_bus or p_cfg is NULL, slave_addr_7b is outside 0x48..0x4B, or p_cfg fails 002.1.	T
001.5	adt7410_init() shall read the ID register (0x0B). It shall return ADT7410_ERR_DEV_ID if bits [7:3] are not 0x19.	T
001.6	After a successful ID check, adt7410_init() shall write the encoded configuration (002.2) to register 0x03. Only on success shall it set active_res = p_cfg->resolution and init_state = READY.	T
001.7	On any failure, adt7410_init() shall leave init_state = UNINIT.	T
001.8	Every public driver function other than adt7410_init() shall return ADT7410_ERR_PARAM when any pointer argument is NULL. It shall return ADT7410_ERR_STATE when p_dev->init_state is not READY. In both cases it shall perform no I2C access.	T
001.9	adt7410_read_id() shall read register 0x0B and output bits [7:3] as the manufacturer ID and bits [2:0] as the silicon revision. It shall write the outputs whenever the I2C read succeeds, and return ADT7410_ERR_DEV_ID when the manufacturer ID is not 0x19.	T
001.10	The driver shall encode its lifecycle as ADT7410_STATE_UNINIT = 0x3C96A5E1 and ADT7410_STATE_READY = 0x43695A1E (Hamming distance 31). (Derived: SEU robustness.)	R, T
001.11	adt7410_soft_reset() shall send the single byte 0x2F. On success it shall set active_res = ADT7410_RES_13BIT. On failure it shall leave active_res unchanged.	T
LLR-ADT-002 — Configuration register (0x03)
Field	Value
Parent HLR	HLR-TBD
Derived	No
Design	SDD-ADT-1.3 … 1.7, 1.9, 2.4, 2.5, 2.7, 3.1, 3.2, 3.4, 3.6, 3.11 … 3.13
Clause	Requirement	Method
002.1	The driver shall reject a configuration (return ADT7410_ERR_PARAM) when any field is above its maximum: resolution > 1, op_mode > 3, event_mode > 1, int_polarity > 1, ct_polarity > 1, fault_queue > 3.	T
002.2	The driver shall encode register 0x03 as: bit 7 = resolution, bits [6:5] = op_mode, bit 4 = event_mode, bit 3 = int_polarity, bit 2 = ct_polarity, bits [1:0] = fault_queue.	T
002.3	adt7410_set_config() shall write the encoded byte to register 0x03. It shall update p_dev->active_res only when the write succeeds.	T
002.4	adt7410_get_config() shall read register 0x03 and decode it with the inverse of 002.2. It shall write *p_cfg only when the read succeeds.	T
002.5	adt7410_trigger_one_shot() shall read register 0x03, replace bits [6:5] with 01b, keep all other bits, and write the result back. If the read fails, it shall not write.	T
LLR-ADT-003 — Temperature and status acquisition
Field	Value
Parent HLR	HLR-TBD
Derived	Partly (003.6 is derived)
Design	SDD-ADT-1.10, 1.11, 2.6, 2.8, 3.6, 3.7, 3.14, 3.16, 3.18, 3.20
Clause	Requirement	Method
003.1	adt7410_read_temperature() shall read two bytes starting at register 0x00 and assemble them MSB first.	T
003.2	When active_res is 13-bit, the driver shall take the temperature from bits [15:3] (bits [2:0] = 0). It shall set has_inline_flags = true, is_tcrit_active = bit 2, is_thigh_active = bit 1, is_tlow_active = bit 0.	T
003.3	When active_res is 16-bit, the driver shall take the temperature from all 16 bits and set all four flag fields to false.	T
003.4	The driver shall convert the 16-bit two's complement word to Q8.7 (degC = q / 128). It shall output die_temp_q8_7 = q and die_temp_mdegc = q × 1000 / 128, rounded half away from zero.	T, A
003.5	adt7410_read_temperature() shall write *p_sample only when it returns ADT7410_OK.	T
003.6	When active_res holds a value other than 13-bit or 16-bit, adt7410_read_temperature() shall return ADT7410_ERR_STATE and leave *p_sample unchanged. (Derived: defensive SEU handling.)	T (fault injection)
003.7	adt7410_read_status() shall read register 0x02 and decode: is_not_ready = bit 7, has_tcrit_flt = bit 6, has_thigh_flt = bit 5, has_tlow_flt = bit 4.	T
LLR-ADT-004 — Temperature limit setpoints (THIGH, TLOW, TCRIT)
Field	Value
Parent HLR	HLR-TBD
Derived	No
Design	SDD-ADT-1.8, 2.9, 2.10, 3.5, 3.7, 3.10, 3.15 … 3.19
Clause	Requirement	Method
004.1	adt7410_set_temp_setpoint() shall return ADT7410_ERR_PARAM when limit_temp_mdegc is outside −55 000..+150 000 (inclusive).	T
004.2	The driver shall map the selector THIGH → 0x04, TLOW → 0x06, TCRIT → 0x08. For any other selector value it shall return ADT7410_ERR_PARAM.	T
004.3	adt7410_set_temp_setpoint() shall encode the limit as q = mdegC × 128 / 1000, rounded half away from zero. It shall write the 16-bit two's complement result as [reg, MSB, LSB].	T, A
004.4	adt7410_get_temp_setpoint() shall read two bytes from the selected register and convert them to mdegC per 003.4. It shall write the output only on success.	T
LLR-ADT-005 — Hysteresis register (THYST, 0x0A)
Field	Value
Parent HLR	HLR-TBD
Derived	No
Design	SDD-ADT-2.11, 2.12, 3.4, 3.6
Clause	Requirement	Method
005.1	adt7410_set_hysteresis() shall return ADT7410_ERR_PARAM when hyst_degc > 15. Otherwise it shall write hyst_degc to register 0x0A (bits [7:4] = 0).	T
005.2	adt7410_get_hysteresis() shall read register 0x0A and output bits [3:0] in degC (0..15). It shall write the output only on success.	T
4. Verification status (current baseline)
LLR group	Host unit test (mock HAL)	Target / HIL test	Notes
LLR-IIC-001 … 004	Not done	Required	HAL does MMIO and needs the target or an AXI IIC model
LLR-ADT-001	Covered (init, ID, reset, PARAM/STATE paths)	Required	—
LLR-ADT-002	Covered (encode/decode round-trip, one-shot RMW)	Required	—
LLR-ADT-003	Covered (±25 °C, 150 °C, −55 °C, ±1 LSB, 13-bit flags, corrupted active_res)	Required	—
LLR-ADT-004	Covered (64 °C, −55 °C, 147 °C, range limits ±1, bad selector)	Required	—
LLR-ADT-005	Covered (0..15 and 16)	Required	—
The host mock test only shows functional behaviour. It is not formal verification evidence. It needs a test procedure (SVCP), qualified tooling and structural coverage (MC/DC) to count as evidence.

5. Open items
ID	Description
OI-01	Link every LLR group to its parent HLR (now HLR-TBD).
OI-02	ADT7410_ERR_NOT_READY (001.1) is defined but no function returns it. Either add a requirement that uses it (e.g. check Status.RDY before reading the temperature) or remove it.
OI-03	LLR-IIC-003.6 / 004.4: on the target, check whether ISR.TX_ERR is set at the end of a master receive (master NACK on the last byte). If so, the HAL would report a false ERR_NACK_ARB.
OI-04	Submit the derived requirements (IIC-001.3, ADT-001.2, ADT-001.10, ADT-003.6) to the system safety assessment.
