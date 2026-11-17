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
| `LLR-ADT-003` | ±25 °C, +150 °C, −55 °C, ±1 LSB, 13-bit flags, corrupted `active_res`, float `read_temp_degc` (`003.8`) | Required |
| `LLR-ADT-004` | Setpoints (64 °C, −55 °C, 147 °C, range bounds, bad selector) | Required |
| `LLR-ADT-005` | Hysteresis (0..15 °C and out-of-range 16) | Required |

## 3. Static & Formatting Check Results *(Populate After Run)*

- Formatting check (`<= 80` cols, no TAB, no `//`, no `goto`/`continue`/`malloc`/`free`, single trailing newline): `[PASS — verified via tests/run_tests.ps1 -FormatOnly on include/*.h & src/*.c (0 findings)]`
- Strict C99 compile (`release` & `debug` with `-Werror`): `[PENDING — requires GCC in PATH]`
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

## 7. Summary: Passed vs. Pending Cases (Team Action Checklist)

> **Status Note:** Only the formatting and VS Code JSON configuration checks
> have been executed in the current Windows environment (no `gcc` / `gcov` /
> target hardware attached). All compile, host unit/integration, coverage, and
> hardware-in-the-loop (HIL) cases below are marked `PENDING` for the team to
> execute and expand.

| Item / Case ID | Scope & Test Description | Status | Owner / Next Action for Team |
| :--- | :--- | :--- | :--- |
| `FMT-01` | Source formatting & banned-keyword check (`include/*.h`, `src/*.c` incl. `main.c`: `<= 80` cols, no TAB, no `//`, no `goto`/`continue`/`malloc`/`free`, single final `\n`) | **PASS** | Verified via `tests/run_tests.ps1 -FormatOnly` (0 findings) |
| `IDE-01` | VS Code / Antigravity JSON config validation (`settings.json`, `c_cpp_properties.json`, `tasks.json`, `launch.json`, `extensions.json`) | **PASS** | 5/5 JSON files valid |
| `CC-01` | Strict C99 release & debug (`-DDEBUG_BUILD`) compile with `-Werror -Wstack-usage=256` (`axi_iic_hal.c`, `adt7410_driver.c`, `main.c`) | **PENDING** | Install GCC (MSYS2 / WSL / Linux) and run `tests/run_tests.ps1` or `tests/run_tests.sh` |
| `WCSU-HOST` | Per-function stack limit (`<= 256 B`) from `rules_release_*.su` | **PENDING** | Auto-checked by `run_tests.ps1` / `run_tests.sh` once GCC runs |
| `TC-IIC-001`..`003` | AXI IIC HAL init validation, timing/PIRQ/SOFTR programming, ISR toggle-clear (`tests/test_axi_iic_hal.c`) | **PENDING** | Run `test_axi_iic_hal` with GCC |
| `TC-IIC-010`..`016` | AXI IIC HAL `write_bytes` PARAM/SEU state, bus busy, TX_FULL timeout, NACK/ARB_LOST, completion (`tests/test_axi_iic_hal.c`) | **PENDING** | Run `test_axi_iic_hal` with GCC |
| `TC-IIC-020`..`022` | AXI IIC HAL `write_read_bytes` PARAM/STATE, RX bits `[7:0]`, RX timeout/NACK/busy (`tests/test_axi_iic_hal.c`) | **PENDING** | Run `test_axi_iic_hal` with GCC |
| `TC-ADT-001`..`012` | ADT7410 init, address bounds (`0x48..0x4B`), ID `0x19` check, HAL error mapping, NULL/SEU `ERR_STATE`, `read_id`, `soft_reset` (`tests/test_adt7410_driver.c`) | **PENDING** | Run `test_adt7410_driver` with GCC |
| `TC-ADT-020`..`024` | Config register `0x03` encode/decode round-trip (all 256 values), one-shot RMW (`tests/test_adt7410_driver.c`) | **PENDING** | Run `test_adt7410_driver` with GCC |
| `TC-ADT-030`..`034` | `read_temperature` (16-bit all 65,536 words, 13-bit + 8 flag combos, SEU `active_res`), `read_status` (`tests/test_adt7410_driver.c`) | **PENDING** | Run `test_adt7410_driver` with GCC |
| `TC-ADT-035` | New float API `adt7410_read_temp_degc()` (16-bit vectors, 13-bit flag masking, I2C failure invariance) | **PENDING** | Run `test_adt7410_driver` with GCC (newly added case) |
| `TC-ADT-040`..`044` | Setpoints `THIGH`/`TLOW`/`TCRIT` range `-55..+150 °C`, full sweep encoding, decode & failure paths (`tests/test_adt7410_driver.c`) | **PENDING** | Run `test_adt7410_driver` with GCC |
| `TC-ADT-050`..`051` | Hysteresis `THYST` `0..15 °C` accept, `16..255` reject, masking `[3:0]` (`tests/test_adt7410_driver.c`) | **PENDING** | Run `test_adt7410_driver` with GCC |
| `TC-INT-001`..`002` | Integration: real driver + real HAL on `regmodel.h` (`read_temperature`, `read_temp_degc`, fault propagation) | **PENDING** | Run `test_integration` with GCC |
| `COV-GCOV` | Statement & branch coverage (`gcov -b -c`) & static analyzer (`clang-tidy`) | **PENDING** | Run via `run_tests.ps1` / `run_tests.sh` and inspect `tests/out/*.gcov` |
| `NEW-UNIT-01` | **Team to add:** Fault-injection test for `adt7410_read_temp_degc()` when `active_res` is corrupted (`SEU`) | **PENDING (To Write)** | Add `TC-ADT-036` in `tests/test_adt7410_driver.c` |
| `NEW-APP-01` | **Team to add:** Host/target tests for `src/main.c` 10 ms polling NACK recovery & deferred `INT` pin ISR (`adt7410_int_pin_isr` + `service_int_alert`) | **PENDING (To Write)** | Test on host harness or target board |
| `HIL-01` (`OI-03`) | **Hardware HIL:** Verify `ISR.TX_ERR` behavior on final master-receive byte (NACK) on real Xilinx AXI IIC IP | **PENDING (Hardware)** | Probe with ILA / target run; confirm no false `ERR_NACK_ARB` |
| `HIL-02` | **Hardware HIL:** Real I2C bus waveforms (`100 kHz`), `START`/`RSTART`/`STOP`, slave disconnect (no ACK), SCL/SDA stuck-low timeout `< 0.5 ms` in 10 ms poll | **PENDING (Hardware)** | Test with oscilloscope/logic analyzer + calibrate `APP_IIC_TIMEOUT_POLLS` |
| `HIL-03` | **Hardware HIL:** Physical ADT7410 thermal chamber / heat-gun test for `INT` & `CT` pins (Interrupt vs Comparator mode, `THIGH`/`TLOW`/`TCRIT`, `THYST`, One-Shot `>= 240 ms`, Reset `>= 200 µs`) | **PENDING (Hardware)** | Verify on target FPGA + ADT7410 PCB |
| `CERT-01` (`OI-04`) | **Target Toolchain:** Regenerate `.su` stack & WCET figures with qualified ARM cross-compiler (`-O1 -fno-inline`) | **PENDING (Target)** | Replace host x86-64 values in `docs/SDD_ADT7410_AXI_IIC.md` §5.1 |
| `CERT-02` (`OI-05`) | **Fault Trap & MC/DC:** Hook `ASSERT(expr)` trap in `DEBUG_BUILD` to project fault handler and run formal MC/DC coverage tool | **PENDING (Target)** | Wire fault handler and generate DO-178C SVCP evidence |

