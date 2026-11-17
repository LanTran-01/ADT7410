# Test Report — ADT7410 Driver, AXI IIC HAL & Reference Application

| Field | Value |
|:---|:---|
| Document ID | TR-ADT-IIC |
| Revision | Draft B |
| Date | 2026-11-17 |
| Software under test | `include/axi_iic_hal.h`, `src/axi_iic_hal.c`, `include/adt7410_driver.h`, `src/adt7410_driver.c`, `src/main.c` |
| Requirements | `LLR_ADT7410_AXI_IIC.md` (Draft B) |
| Design | `SDD_ADT7410_AXI_IIC.md` (Draft B) |
| Test procedures | `tests/test_adt7410_driver.c`, `tests/test_axi_iic_hal.c`, `tests/test_integration.c`, `tests/test_app.c` |
| How to reproduce | `bash tests/run_tests.sh` (or `run_tests.ps1`, `make`, CMake/CTest) → logs in `tests/out/` |
| **Overall result** | **PASS — 56/56 test cases, 1056/1056 checks, 0 failures** |

> **Scope warning.** These results come from **host-based** tests (x86-64
> Linux, GCC 13.3.0). They show that the logic is correct and give
> structural-coverage *indications*. They are **not** DO-178C credit on
> their own: target execution, a qualified coverage tool (MC/DC) and
> independent review are still required (see §7).

---

## 1. Summary dashboard

| Area | Item | Result |
|:---|:---|:---:|
| Static checks | Strict compile, release (`-std=c99 -pedantic-errors -Werror` + all §12 flags): HAL, driver, `main.c` | ✅ PASS |
| Static checks | Strict compile, `DEBUG_BUILD`: HAL, driver, `main.c` | ✅ PASS |
| Static checks | Formatting: ≤ 80 cols, no TAB, no `//`, no `goto`/`continue`/`malloc`/`free`, one final newline (`include/*.h`, `src/*.c`) | ✅ PASS (0 findings) |
| Static checks | Stack per function ≤ 256 B (`-Wstack-usage=256`) | ✅ PASS (max 64 B: `adt7410_init`, `adt7410_read_temp_degc`) |
| Static checks | Floating-point warnings (`-Wfloat-equal -Wdouble-promotion -Wfloat-conversion`) on driver and `main.c` | ✅ PASS |
| Static checks | clang-tidy 18 (analyzer, bugprone, cert, misc) | ⚠️ 7 style findings in HAL/driver, all accepted by design (§5); `main.c` 0 findings |
| Unit test | ADT7410 driver (mock HAL): 31 TC, 881 checks | ✅ PASS |
| Unit test | AXI IIC HAL (RAM register model): 13 TC, 120 checks | ✅ PASS |
| Integration test | Driver + real HAL: 2 TC, 15 checks | ✅ PASS |
| Application test | `src/main.c` + driver + mock HAL: 10 TC, 40 checks | ✅ PASS |
| Coverage | `adt7410_driver.c`: statements / branches | ✅ 100 % / 100 % |
| Coverage | `main.c`: statements / branches | ✅ 100 % / 100 % |
| Coverage | `axi_iic_hal.c`: statements / branches | ⚠️ 99.51 % / 98.96 % (1 gap, §4.3) |
| Coverage | MC/DC | ⛔ Not measured (tool not available, §7) |
| Build systems | `tests/run_tests.sh`, `make static-check test coverage`, CMake + CTest (coverage OFF and ON: 4/4 suites) | ✅ PASS |
| Build systems | `tests/run_tests.ps1` (Windows) | ⬜ Not executed (no PowerShell on the test host); updated in the same way as `run_tests.sh` |
| Requirements | 69 LLR clauses: 56 verified by test, 3 partially by test, 9 by inspection/analysis, 1 not yet verified (target only) | ✅ No clause failed |

---

## 2. Test environment

| Item | Value |
|:---|:---|
| Host | Linux x86-64 (cloud container) |
| Compiler | GCC 13.3.0 (Ubuntu 13.3.0-6ubuntu2~24.04.1) |
| Coverage tool | gcov 13.3.0 (statement + branch); not qualified |
| Static analysis | clang-tidy (LLVM 18.1.3); cppcheck/MISRA addon not available |
| Build tools | GNU Make, CMake + CTest |
| Build for coverage | `-std=c99 -O0 -g --coverage`, `DEBUG_BUILD` **not** defined (so `ASSERT` compiles out, as in flight builds) |
| Build for rule check | `-O1 -fno-inline` + full §12 flag set |

### 2.1 Test harness architecture

```
 test_adt7410_driver.c ──> adt7410_driver.c ──> mock_axi_iic.c
   (31 TC)                  (code under test)    emulates ADT7410 register
                                                 file, logs every I2C frame,
                                                 injects HAL error codes

 test_axi_iic_hal.c ─────> axi_iic_hal.c ─────> regmodel.h
   (13 TC)                  (code under test)    RAM array at base_addr,
                                                 fixed SR / ISR "situations"

 test_integration.c ─────> adt7410_driver.c ─> axi_iic_hal.c ─> regmodel.h
   (2 TC)                   (both real modules linked together)

 test_app.c ─────────────> main.c (main renamed app_main, test build only)
   (10 TC)                  -> adt7410_driver.c -> mock_axi_iic.c
                            stub axi_iic_init(); counts every I2C transaction
```

Mock fault injection: fail one transfer (`fail_on_xfer`), several consecutive
transfers (`fail_xfer_count`), or every read of one register
(`is_fail_reg_enabled` + `fail_reg_addr`).

---

## 3. Test case results

Result legend: ✅ PASS · ❌ FAIL. Check counts come from `tests/out/*.log`.

### 3.1 ADT7410 driver unit tests (`test_adt7410_driver`)

| TC | LLR clauses | Description | Checks | Result |
|:---|:---|:---|---:|:---:|
| TC-ADT-001 | 001.3 | `init` rejects NULL `p_dev`, no I2C | 2 | ✅ |
| TC-ADT-002 | 001.4, 001.7 | `init` rejects NULL bus/cfg and addresses 0x00, 0x47, 0x4C, 0x7F, 0xFF | 10 | ✅ |
| TC-ADT-003 | 001.4, 002.1 | `init` rejects each out-of-range config field (incl. negative enum) | 15 | ✅ |
| TC-ADT-004 | 001.4 | Address boundaries 0x48 and 0x4B accepted | 4 | ✅ |
| TC-ADT-005 | 001.5, 001.7 | Wrong manufacturer ID → `ERR_DEV_ID`, no config write | 4 | ✅ |
| TC-ADT-006 | 001.5, 001.6, 002.2 | `init` success: ID read 0x0B, config 0x9A written, READY | 13 | ✅ |
| TC-ADT-007 | 001.2, 001.7 | I2C failure on ID read / config write / no ACK | 6 | ✅ |
| TC-ADT-008 | 001.2 | All 5 HAL error codes → `ERR_I2C_COMM` | 10 | ✅ |
| TC-ADT-009 | 001.8 | Every API (incl. `read_temp_degc`) rejects every NULL pointer, 0 I2C frames | 23 | ✅ |
| TC-ADT-010 | 001.8, 001.10 | `ERR_STATE` when UNINIT **and** for all 32 single-bit flips of READY (SEU fault injection) × 12 APIs | 430 | ✅ |
| TC-ADT-011 | 001.9 | `read_id` fields, mismatch (outputs still written), I2C failure | 9 | ✅ |
| TC-ADT-012 | 001.11 | `soft_reset` sends single byte 0x2F; `active_res` handling | 10 | ✅ |
| TC-ADT-020 | 002.2, 002.3 | Encoding all-zero (0x00), all-max (0xFF), mixed (0x45) | 9 | ✅ |
| TC-ADT-021 | 002.1, 002.3 | Invalid field rejected; I2C failure keeps `active_res` | 5 | ✅ |
| TC-ADT-022 | 002.2, 002.4 | Decode→encode round-trip, **all 256** register values | 2 | ✅ |
| TC-ADT-023 | 002.4 | Output untouched on failure | 3 | ✅ |
| TC-ADT-024 | 002.5 | One-shot RMW `(v & 0x9F) \| 0x20`, **all 256** values; no write after failed read | 5 | ✅ |
| TC-ADT-030 | 003.1, 003.3, 003.4 | 16-bit vectors: +150, +147, +64, +25, ±1 LSB, 0, −25, −55, +max, −min | 59 | ✅ |
| TC-ADT-031 | 003.2, 003.4 | 13-bit: data bits [15:3] + all 8 flag combinations | 58 | ✅ |
| TC-ADT-032 | 003.4 | 16-bit decode vs independent `double` reference, **all 65 536 words** | 2 | ✅ |
| TC-ADT-033 | 003.5, 003.6 | Sample untouched on I2C failure; corrupted `active_res` → `ERR_STATE` (DEFENSIVE branch) | 6 | ✅ |
| TC-ADT-034 | 003.7 | Status decode, all 16 combinations of bits 7..4 | 83 | ✅ |
| TC-ADT-035 | 003.8 | `read_temp_degc`: 16-bit vectors, 13-bit flag masking, output untouched on I2C failure | 22 | ✅ |
| TC-ADT-036 | 003.6, 003.8 | `read_temp_degc` with corrupted `active_res` → `ERR_STATE`, output untouched | 3 | ✅ |
| TC-ADT-040 | 004.1 | Range limits −55 000 / 150 000 accepted; −55 001, 150 001, INT32_MIN/MAX rejected | 8 | ✅ |
| TC-ADT-041 | 004.2 | THIGH→0x04, TLOW→0x06, TCRIT→0x08; invalid selector rejected | 16 | ✅ |
| TC-ADT-042 | 004.3 | Encoding vectors incl. rounding (3 mdegC→0, 4→1, −4→0xFFFF) | 45 | ✅ |
| TC-ADT-043 | 004.3 | Encode vs `double` reference, **all 205 001 values** −55 000..150 000 | 2 | ✅ |
| TC-ADT-044 | 004.4 | Decode power-on defaults (147 °C, 64 °C), round-trip, failure | 8 | ✅ |
| TC-ADT-050 | 005.1 | Hysteresis 0..15 written, 16..255 rejected (all 256 values) | 2 | ✅ |
| TC-ADT-051 | 005.2 | Mask bits [3:0] (0xF7→7), failure paths | 7 | ✅ |
| **Total** | | **31 test cases** | **881** | **✅ 31/31** |

### 3.2 AXI IIC HAL unit tests (`test_axi_iic_hal`)

| TC | LLR clauses | Description | Checks | Result |
|:---|:---|:---|---:|:---:|
| TC-IIC-001 | 002.1 | `init` PARAM cases (NULL, base 0, polls 0, polls max+1), no register access; boundaries 1 and max accepted | 8 | ✅ |
| TC-IIC-002 | 002.2–002.7 | SOFTR = 0xA, 8 timing values, PIRQ = 0x0F, CR = 0x01, fields stored, READY | 15 | ✅ |
| TC-IIC-003 | 002.6 | Only ARB_LOST/TX_ERR bits are written back to ISR | 4 | ✅ |
| TC-IIC-010 | 003.1 | PARAM checks; boundaries addr 0x7F, len 1 and 32 | 9 | ✅ |
| TC-IIC-011 | 001.3, 003.2 | `ERR_STATE` when UNINIT and for all 32 single-bit flips of READY | 35 | ✅ |
| TC-IIC-012 | 003.3 | `ERR_BUS_BUSY` when SR.BB stays set; no START queued | 3 | ✅ |
| TC-IIC-013 | 003.5, 003.8 | `ERR_TIMEOUT` when TX_FULL stays set (bounded loop terminates) | 3 | ✅ |
| TC-IIC-014 | 003.6 | NACK and ARB_LOST detected while waiting on TX_FULL; controller re-enabled | 5 | ✅ |
| TC-IIC-015 | 003.6, 003.7 | Completion timeout; NACK inside completion loop; NACK latched at idle | 6 | ✅ |
| TC-IIC-016 | 003.4 *(partial)* | Last FIFO word = last byte \| STOP (n = 3 and n = 1) | 6 | ✅ |
| TC-IIC-020 | 004.1 | PARAM / STATE checks for `write_read_bytes` | 8 | ✅ |
| TC-IIC-021 | 004.2 *(partial)*, 004.3 | 32 bytes received, RX bits [7:0] masked, last word = STOP \| n | 7 | ✅ |
| TC-IIC-022 | 004.3–004.5 | RX timeout, NACK in RX wait, ARB_LOST, bus busy, completion timeout | 11 | ✅ |
| **Total** | | **13 test cases** | **120** | **✅ 13/13** |

### 3.3 Integration tests (`test_integration`)

| TC | LLR clauses | Description | Checks | Result |
|:---|:---|:---|---:|:---:|
| TC-INT-001 | ADT-001.5, 001.6, 003.1, 003.8, IIC-004 | `adt7410_init` + `read_temperature` + `read_temp_degc` through the real HAL; byte order 0xCBCB → −104 414 mdegC / −104.4140625 °C | 10 | ✅ |
| TC-INT-002 | ADT-001.2, IIC-003.3, 003.6 | NACK, stuck bus, uninitialised bus → `ERR_I2C_COMM` at driver level | 5 | ✅ |
| **Total** | | **2 test cases** | **15** | **✅ 2/2** |

### 3.4 Application tests (`test_app`)

Transaction budget: set-up = 6 transfers, one poll / alert service = 2,
healthy run = 26 (polling) + 8 (interrupt) = 34.

| TC | LLR clauses | Description | Checks | Result |
|:---|:---|:---|---:|:---:|
| TC-APP-001 | APP-001, 002, 003, 005 | Healthy run: 34 transfers, rc 0; INT service order temperature → Status | 4 | ✅ |
| TC-APP-002 | APP-001 | Interrupt-mode config 0x81; THIGH 0x2A80, TLOW 0xFB00, TCRIT 0x3480, THYST 5 | 7 | ✅ |
| TC-APP-003 | APP-002, 004 | Poll fails in cycle 0 → recovery alone in cycle 1 → polling resumes; 37 transfers, rc 0 | 2 | ✅ |
| TC-APP-004 | APP-002 | Poll fails in the last cycle (rc 1, 25 transfers); poll fails then its recovery fails (rc 1, 8 transfers) | 4 | ✅ |
| TC-APP-005 | APP-003, 004 | INT service fails → flag re-armed → recovery → alert re-serviced; 41 transfers, rc 0 | 3 | ✅ |
| TC-APP-006 | APP-001, 005 | Sensor init fails → no polling, rc 1 | 2 | ✅ |
| TC-APP-007 | APP-001 | AXI IIC HAL init fails → no sensor access, rc 1 | 2 | ✅ |
| TC-APP-008 | APP-001 | THIGH / TLOW / TCRIT / THYST write fails → set-up stops; comparator-mode config 0x91 checked | 12 | ✅ |
| TC-APP-009 | APP-003, 004 | INT service fails and the recovery fails too → rc 1, 34 transfers | 2 | ✅ |
| TC-APP-010 | APP-002, 003, 004 | Temperature reads always fail: alternating poll / recovery slots (46), INT re-service every other cycle; 74 transfers, rc 1 | 2 | ✅ |
| **Total** | | **10 test cases** | **40** | **✅ 10/10** |

---

## 4. Structural coverage

### 4.1 Per file (gcov, statement + branch)

| File | Statements | Branches executed | Branches taken ≥ 1 | Calls |
|:---|---:|---:|---:|---:|
| `adt7410_driver.c` | **100.00 %** (383/383) | 100.00 % (161) | **100.00 %** (161/161) | 100.00 % (43) |
| `main.c` | **100.00 %** (115/115) | 100.00 % (48) | **100.00 %** (48/48) | 100.00 % (22) |
| `axi_iic_hal.c` | **99.51 %** (202/203) | 100.00 % (96) | **98.96 %** (95/96) | 100.00 % (50) |

### 4.2 Per function

All functions of `adt7410_driver.c` (13 public + 19 `static`) and `main.c`
(2 public + 8 `static`) reach 100 % statements. In `axi_iic_hal.c` every
function reaches 100 % except `is_tx_idle()` (87.5 %, GAP-01).

### 4.3 Coverage gap analysis

| ID | Location | Uncovered element | Cause | Disposition |
|:---|:---|:---|:---|:---|
| GAP-01 | `src/axi_iic_hal.c`, `is_tx_idle()` | Branch "TX FIFO empty **but** SR.BB = 1" (STOP not yet finished on the bus) | Needs SR.BB = 0 when the transfer starts and BB = 1 at completion, i.e. a register that changes over time. The static RAM model cannot do that. | **Reachable code, not dead code.** Close on target / HIL with HIL-01 (normal transfer: the first completion poll sees TX_EMPTY while BB is still high). |

The `default:` branch of `reg16_to_sample()` (tagged `DEFENSIVE [DO-178C]`)
**is** covered by fault injection (TC-ADT-033, TC-ADT-036). The `ASSERT()`
macros compile out in this build, so they add no uncovered branches.

---

## 5. Static analysis findings

| # | Tool / rule | Location | Finding | Disposition |
|:---|:---|:---|:---|:---|
| 1–5 | clang-tidy `bugprone-branch-clone` | `adt7410_driver.c`: `adt7410_init`, `adt7410_set_config`, `adt7410_set_temp_setpoint`, `adt7410_set_hysteresis`, `is_config_valid` | Several `else if` branches have the same body (`result = ERR_PARAM` / `is_valid = false`) | **Accepted by design.** The validation is deliberately split into separate decisions to keep ≤ 3 uncoupled conditions per decision (C rules §5, MC/DC). |
| 6 | same | `axi_iic_hal.c`: `validate_xfer_args` | same | Accepted, same rationale |
| 7 | same | `axi_iic_hal.c`: `is_tx_idle` | Two branches set `is_idle = false` | Accepted, avoids a coupled decision on `sr_flags` |

clang-analyzer (path-sensitive: NULL dereference, uninitialised reads, dead
stores) and the cert-* checks reported **no findings**. `main.c`: 0 findings.

---

## 6. Requirements verification matrix

Legend: ✅ Verified by test · 🟡 Partially verified (host model limit, needs
target/HIL) · 🔍 Verified by inspection/analysis · ⬜ Not verified yet ·
❌ Failed

### 6.1 AXI IIC HAL

| Clause | Method | Evidence | Status |
|:---|:---|:---|:---:|
| IIC-001.1 | R, A | Code inspection: the only `volatile` casts are in `reg_read32`/`reg_write32` (DEV-01) | 🔍 |
| IIC-001.2 | R | Code inspection of `axi_iic_status_t` | 🔍 |
| IIC-001.3 | R, T | TC-IIC-011 | ✅ |
| IIC-002.1 | T | TC-IIC-001 | ✅ |
| IIC-002.2 | T | TC-IIC-002 | ✅ |
| IIC-002.3 | T | TC-IIC-002 | ✅ |
| IIC-002.4 | T, A | TC-IIC-002 | ✅ |
| IIC-002.5 | T | TC-IIC-002 | ✅ |
| IIC-002.6 | T | TC-IIC-002, TC-IIC-003 — final CR and ISR values checked; the TX_FIFO_RESET pulse itself is not visible in the RAM model | 🟡 |
| IIC-002.7 | T | TC-IIC-002 | ✅ |
| IIC-003.1 | T | TC-IIC-010 | ✅ |
| IIC-003.2 | T | TC-IIC-011 | ✅ |
| IIC-003.3 | T | TC-IIC-012, TC-INT-002 | ✅ |
| IIC-003.4 | T | TC-IIC-016 — only the **last** FIFO word is observable | 🟡 |
| IIC-003.5 | T | TC-IIC-013 | ✅ |
| IIC-003.6 | T | TC-IIC-014, TC-IIC-015, TC-INT-002 | ✅ |
| IIC-003.7 | T | TC-IIC-015 | ✅ |
| IIC-003.8 | A | TC-IIC-013 (loop terminates) + bound derivation in SDD-IIC-2.2 | 🔍 |
| IIC-004.1 | T | TC-IIC-020 | ✅ |
| IIC-004.2 | T | TC-IIC-021, TC-INT-001 — only the last FIFO word is observable | 🟡 |
| IIC-004.3 | T | TC-IIC-021, TC-IIC-022 | ✅ |
| IIC-004.4 | T | TC-IIC-022 | ✅ |
| IIC-004.5 | A | TC-IIC-022 + bound derivation in SDD-IIC-2.3 | 🔍 |

### 6.2 ADT7410 driver

| Clause | Evidence | Status |
|:---|:---|:---:|
| ADT-001.1 | Code inspection of `adt7410_status_t` | 🔍 |
| ADT-001.2 | TC-ADT-007, 008, TC-INT-002 | ✅ |
| ADT-001.3 | TC-ADT-001 | ✅ |
| ADT-001.4 | TC-ADT-002, 003, 004 | ✅ |
| ADT-001.5 | TC-ADT-005, 006, TC-INT-001 | ✅ |
| ADT-001.6 | TC-ADT-006, TC-INT-001 | ✅ |
| ADT-001.7 | TC-ADT-002, 003, 005, 007 | ✅ |
| ADT-001.8 | TC-ADT-009, 010 | ✅ |
| ADT-001.9 | TC-ADT-011 | ✅ |
| ADT-001.10 | TC-ADT-010 (32-bit SEU sweep) | ✅ |
| ADT-001.11 | TC-ADT-012 | ✅ |
| ADT-002.1 | TC-ADT-003, 021 | ✅ |
| ADT-002.2 | TC-ADT-006, 020, 022 | ✅ |
| ADT-002.3 | TC-ADT-020, 021 | ✅ |
| ADT-002.4 | TC-ADT-022, 023 | ✅ |
| ADT-002.5 | TC-ADT-024 | ✅ |
| ADT-003.1 | TC-ADT-030, TC-INT-001 | ✅ |
| ADT-003.2 | TC-ADT-031 | ✅ |
| ADT-003.3 | TC-ADT-030 | ✅ |
| ADT-003.4 | TC-ADT-030, 031, 032 (exhaustive) | ✅ |
| ADT-003.5 | TC-ADT-033 | ✅ |
| ADT-003.6 | TC-ADT-033, 036 (fault injection) | ✅ |
| ADT-003.7 | TC-ADT-034 | ✅ |
| ADT-003.8 | TC-ADT-035, 036, TC-INT-001 | ✅ |
| ADT-004.1 | TC-ADT-040 | ✅ |
| ADT-004.2 | TC-ADT-041 | ✅ |
| ADT-004.3 | TC-ADT-042, 043 (exhaustive) | ✅ |
| ADT-004.4 | TC-ADT-044 | ✅ |
| ADT-005.1 | TC-ADT-050 | ✅ |
| ADT-005.2 | TC-ADT-051 | ✅ |

### 6.3 Reference application

| Clause | Evidence | Status |
|:---|:---|:---:|
| APP-001.1 | TC-APP-007 | ✅ |
| APP-001.2 | TC-APP-002 (interrupt config 0x81), TC-APP-008 (comparator config 0x91) | ✅ |
| APP-001.3 | TC-APP-002 (values), TC-APP-008 (stops at the first failing write) | ✅ |
| APP-002.1 | TC-APP-010 (transfer count proves Status-then-temperature order) | ✅ |
| APP-002.2 | Code inspection: snapshot written in one block after both reads succeed (the `static` snapshot is not observable from the host test) | 🔍 |
| APP-002.3 | TC-APP-003, TC-APP-010 | ✅ |
| APP-002.4 | TC-APP-004 | ✅ |
| APP-003.1 | Code inspection of `adt7410_int_pin_isr()` + TC-APP-001 | ✅ |
| APP-003.2 | TC-APP-001 (temperature 0x00 then Status 0x02) | ✅ |
| APP-003.3 | Code inspection (same pattern as APP-002.2) | 🔍 |
| APP-003.4 | TC-APP-005, TC-APP-010 | ✅ |
| APP-003.5 | TC-APP-009, TC-APP-010 | ✅ |
| APP-004.1 | Analysis SDD-APP-4 (7·P + 8·P) | 🔍 |
| APP-004.2 | Analysis SDD-APP-4 (35·P) + TC-APP-003 (recovery cycle contains only the 6 set-up transfers) | 🔍 |
| APP-004.3 | Requires `t_poll` measured on target (`OI-08`) | ⬜ |
| APP-005.1 | TC-APP-001, 004, 006 | ✅ |

### 6.4 Totals

| Module | Clauses | ✅ Test | 🟡 Partial | 🔍 Inspection / analysis | ⬜ Not yet | ❌ Failed |
|:---|---:|---:|---:|---:|---:|---:|
| AXI IIC HAL | 23 | 16 | 3 | 4 | 0 | 0 |
| ADT7410 driver | 30 | 29 | 0 | 1 | 0 | 0 |
| Reference application | 16 | 11 | 0 | 4 | 1 | 0 |
| **Total** | **69** | **56** | **3** | **9** | **1** | **0** |

---

## 7. Limitations, pending activities and team checklist

### 7.1 Limits of the host register model and mock

1. **Static register RAM (`tests/regmodel.h`)**: SR and ISR do not change
   while `axi_iic_hal.c` polls; each test sets a fixed "hardware situation".
   TX_FIFO keeps only the **last word** written, so the full frame sequence
   (`START`, `RSTART`, `ACK`/`NACK`, `STOP`) can only be checked on target,
   HIL or RTL co-simulation. ISR toggle-on-write is not emulated.
2. **Mock HAL (`tests/mock_axi_iic.c`)**: emulates the ADT7410 register file
   (0x00..0x2F) and logs the last 16 transactions. It checks driver and
   application framing and fault propagation, not AXI4-Lite timing or the
   I2C electrical protocol.
3. **Application build**: `src/main.c` is compiled with `-Dmain=app_main`
   only for `test_app`; the flight build is unchanged. The INT edge is
   simulated by calling the ISR from the task loop.

### 7.2 Checklist

| Item | Scope | Status | Next action |
|:---|:---|:---|:---|
| FMT-01 | Formatting & banned constructs (`include/*.h`, `src/*.c`) | **PASS** | — |
| CC-01 | Strict C99 release & debug compile (HAL, driver, `main.c`) | **PASS** | — |
| WCSU-HOST | Per-function stack ≤ 256 B (host) | **PASS** | Target rebuild (`OI-04`) |
| TC-IIC / TC-ADT / TC-INT / TC-APP | 56 host test cases | **PASS** | — |
| COV-GCOV | Statement & branch coverage | **PASS** with GAP-01 | Close GAP-01 on HIL |
| PS1-01 | `tests/run_tests.ps1` on Windows | **PENDING** | Run on a Windows host with MSYS2 GCC |
| HIL-01 | Full I2C frame sequence on real AXI IIC IP; GAP-01 (TX_EMPTY while BB high) | **PENDING (Hardware)** | Probe with ILA / logic analyser |
| HIL-02 (`OI-03`) | `ISR.TX_ERR` at the end of a master receive (false `ERR_NACK_ARB`?) — 1000 × 2-byte reads | **PENDING (Hardware)** | Target run |
| HIL-03 | Slave disconnect / SDA stuck low: NACK, BUS_BUSY, TIMEOUT within `P` polls | **PENDING (Hardware)** | Target run |
| HIL-04 | Temperature chamber −55 / +25 / +150 °C in 13-bit and 16-bit modes; INT/CT pins, THYST, one-shot ≥ 240 ms, reset ≥ 200 µs | **PENDING (Hardware)** | Target FPGA + ADT7410 board |
| TIME-01 (`OI-08`) | Measure `t_poll` and worst-case 10 ms cycle time; choose `APP_IIC_TIMEOUT_POLLS` | **PENDING (Target)** | See SDD-APP-4 |
| CERT-01 (`OI-04`) | Stack & WCET figures with the qualified ARM cross-compiler | **PENDING (Target)** | Replace host values in SDD §6.1 |
| CERT-02 (`OI-05`) | `ASSERT` trap → project fault handler; MC/DC with a qualified tool | **PENDING** | Tool selection & qualification |
| CERT-03 | MISRA C:2012 checker run (record deviations vs DEV-01) | **PENDING** | Qualified MISRA checker |
| CERT-04 | Independent review of test cases against LLR (DO-178C Table A-7) | **PENDING** | Reviewer assignment |

---

## 8. Conclusion

Every host test case passed (56/56, 1056 checks) and no LLR clause failed.
`adt7410_driver.c` and `main.c` reach 100 % statement and branch coverage,
including the DEFENSIVE branch (reached by fault injection).
`axi_iic_hal.c` has one reachable but uncovered branch (GAP-01), assigned
to HIL-01.

Before this report can count as DO-178C verification evidence, the pending
items in §7.2 must be completed: target / HIL execution, timing measurement
(`OI-08`), MC/DC with a qualified tool, a MISRA checker run and independent
review.
