# Open Items Tracker — ADT7410 & AXI IIC Project

Consolidated from `docs/LLR_ADT7410_AXI_IIC.md` (§1.3, §5) and
`docs/SDD_ADT7410_AXI_IIC.md` (§5.1, §6.3, §8).

| ID | Description | Source Reference | Status |
| :--- | :--- | :--- | :--- |
| `OI-01` | Parent HLR links are missing (`HLR-TBD` in LLR). Link each LLR group (`LLR-IIC-001..004`, `LLR-ADT-001..005`) to an approved HLR document or mark derived before baseline. Draft placeholder created in `docs/HLR_ADT7410_AXI_IIC.md`. | `LLR §1.3, §5`; `SDD §8` | Open (Draft placeholder added) |
| `OI-02` | `ADT7410_ERR_NOT_READY` (`5U`) is defined in `adt7410_status_t` (`include/adt7410_driver.h`) and `LLR-ADT-001.1`, but no function currently returns it. Decide whether to check `Status.RDY` and return it or remove the enumerator. | `LLR §5`; `SDD §4.1, §8` | Open |
| `OI-03` | Verify on real Xilinx AXI IIC hardware whether `ISR.TX_ERR` is asserted at the end of a master receive (master NACK on last byte), which would cause `axi_iic_write_read_bytes()` to report a false `AXI_IIC_ERR_NACK_ARB`. | `LLR §5 (003.6/004.4)`; `SDD §8` | Open (Needs HIL/Target) |
| `OI-04` | Regenerate per-function stack usage (`.su`) and WCET measurements using the qualified target ARM cross-compiler and project optimization profile (`SDD §5.1` figures are host x86-64 only). | `SDD §5.1, §8` | Open (Needs ARM toolchain) |
| `OI-05` | In `DEBUG_BUILD`, `ASSERT(expr)` in `src/adt7410_driver.c` and `src/axi_iic_hal.c` currently has an empty `/* trap */` body. Connect it to the project fault handler. | `SDD §6.3, §8`; `src/*.c` | Open |
| `OI-06` | Verify the `[ADT7410]` and `[PG090]` table and page numbers cited in source comments against the exact datasheet revisions under configuration control. | `SDD §8` | Open |
| `OI-07` | Submit derived requirements (`LLR-IIC-001.3`, `LLR-ADT-001.2`, `LLR-ADT-001.10`, `LLR-ADT-003.6`) to the system safety assessment process per DO-178C §5.2.2 (noted as `OI-04` in LLR §5). | `LLR §1.1, §5` | Open |
