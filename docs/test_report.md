# Host Verification & Test Report — ADT7410 Driver & AXI IIC HAL

| Field | Value |
| :--- | :--- |
| Document ID | TR-ADT-IIC |
| Status | Baseline Template — Pending Execution on Target Host Toolchain |
| Software Items | `include/axi_iic_hal.h`, `src/axi_iic_hal.c`, `include/adt7410_driver.h`, `src/adt7410_driver.c` |
| LLR Baseline | `docs/LLR_ADT7410_AXI_IIC.md` (`LLR-ADT-IIC`, Draft A) |
| SDD Baseline | `docs/SDD_ADT7410_AXI_IIC.md` (`SDD-ADT-IIC`, Draft A) |

> **Note:** Populate §3–§5 with actual outputs from `tests/out/*.log` and
> `tests/out/*.gcov` after running `tests/run_tests.ps1` or `tests/run_tests.sh`
> on a host with `gcc` and `gcov` installed.

## 1. Test Environment & Suites

- **Driver Unit Suite (`tests/test_adt7410_driver.c`)**: Tests
  `src/adt7410_driver.c` against `tests/mock_axi_iic.c`.
- **HAL Unit Suite (`tests/test_axi_iic_hal.c`)**: Tests `src/axi_iic_hal.c`
  against the static RAM register model in `tests/regmodel.h`.
- **Software/Software Integration Suite (`tests/test_integration.c`)**: Links
  real `src/adt7410_driver.c` on top of real `src/axi_iic_hal.c` using
  `tests/regmodel.h`.

## 2. Verification Scope Mapped to LLR

Per `docs/LLR_ADT7410_AXI_IIC.md` §4:

| LLR Group | Host Test Scope | Target / HIL Status |
| :--- | :--- | :--- |
| `LLR-IIC-001`..`004` | Static register-model functional checks (`test_axi_iic_hal.c`) | Required (`OI-03`) |
| `LLR-ADT-001` | Init, ID check, reset, PARAM/STATE paths | Required |
| `LLR-ADT-002` | Config encode/decode round-trip, one-shot RMW | Required |
| `LLR-ADT-003` | ±25 °C, +150 °C, −55 °C, ±1 LSB, 13-bit flags, corrupted `active_res` | Required |
| `LLR-ADT-004` | Setpoints (64 °C, −55 °C, 147 °C, range bounds, bad selector) | Required |
| `LLR-ADT-005` | Hysteresis (0..15 °C and out-of-range 16) | Required |

## 3. Static & Formatting Check Results *(Populate After Run)*

- Formatting check (`<= 80` cols, no TAB, no `//`, no `goto`/`continue`/`malloc`/`free`, single trailing newline): `[PENDING]`
- Strict C99 compile (`release` & `debug` with `-Werror`): `[PENDING]`
- Stack usage check (`-Wstack-usage=256`, `rules_release_*.su`): `[PENDING — see SDD §5.1 for host x86-64 figures; OI-04 requires ARM target rebuild]`

## 4. Test Execution Results *(Populate After Run)*

- `test_adt7410_driver`: `[PENDING — check tests/out/test_adt7410_driver.log]`
- `test_axi_iic_hal`: `[PENDING — check tests/out/test_axi_iic_hal.log]`
- `test_integration`: `[PENDING — check tests/out/test_integration.log]`

## 5. Structural Coverage Summary *(Populate After Run)*

- `axi_iic_hal.c`: `[PENDING — check tests/out/gcov_axi_iic_hal.log]`
- `adt7410_driver.c`: `[PENDING — check tests/out/gcov_adt7410_driver.log]`
- Unreachable defensive branches documented in `docs/SDD_ADT7410_AXI_IIC.md` §6.2:
  - `reg16_to_sample()` `default:` (fault-injection tested in `test_adt7410_driver.c`)
  - `init_state != READY` defensive branches

## 6. Limits of Host Register Model & Mock (Referenced by `tests/regmodel.h`)

The host test environment provides functional verification only and **does not**
constitute formal DO-178C verification evidence (which requires a formal SVCP,
qualified tooling, MC/DC coverage, and target/HIL execution):

1. **Static Register RAM (`tests/regmodel.h`)**:
   - Registers are plain memory (`rm_regs[]`); `SR` and `ISR` do not evolve
     dynamically while `axi_iic_hal.c` polls. Each test sets a fixed
     "hardware situation" before calling the HAL.
   - `TX_FIFO` (`RM_TX_FIFO`) retains only the **last word** written; complete
     multi-byte I2C frame timing and ordering (`START`, `RSTART`, `ACK`/`NACK`,
     `STOP`) can only be verified on target hardware, HIL, or RTL co-simulation.
   - `ISR` toggle-on-write clear semantics (`W1C`) are not emulated by RAM
     (written bits are stored directly).
2. **Mock HAL (`tests/mock_axi_iic.c`)**:
   - Emulates a 48-byte (`0x00..0x2F`) ADT7410 register array and logs the last
     16 transactions (`MOCK_LOG_DEPTH`). It verifies driver framing and fault
     propagation but not actual AXI4-Lite bus timing or I2C electrical protocol.
   - Hardware check `OI-03` (`ISR.TX_ERR` behavior at the end of master receive)
     cannot be proved on the mock or static RAM model and must be tested on the
     real Xilinx AXI IIC IP core.
