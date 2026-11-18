---
name: adt7410-build-and-verify
description: >-
  Use this skill when the user asks to compile, run static analysis, check
  formatting/stack limits, execute unit/integration tests, or collect gcov
  coverage for the ADT7410 driver and AXI IIC HAL project.
---

# ADT7410 Build and Verification Procedure

Follow these steps to verify changes in `include/`, `src/`, and `tests/` against
the project's C99, MISRA C:2012, BARR-C:2018, and DO-178C checks.

## 1. Choose the Runner for the Host OS

- **Windows (PowerShell)**: run [run_tests.ps1](../../../tests/run_tests.ps1)
  ```powershell
  powershell -ExecutionPolicy Bypass -File tests/run_tests.ps1
  ```
- **Linux / macOS / WSL / MSYS2 Bash**: run [run_tests.sh](../../../tests/run_tests.sh)
  ```bash
  bash tests/run_tests.sh
  ```
- **CMake / CTest** (if GCC and CMake are installed):
  ```bash
  cmake -S . -B build -DENABLE_COVERAGE=ON
  cmake --build build
  ctest --test-dir build --output-on-failure
  ```

## 2. Verification Gates Checked by the Runner

1. **Strict Release & Debug Compilation (`src/axi_iic_hal.c`, `src/adt7410_driver.c`, `src/main.c`)**:
   - Compiles all three files with `-std=c99 -pedantic-errors -Wall -Wextra -Werror`
     plus `-Wconversion -Wsign-conversion -Wshadow -Wstrict-prototypes`
     `-Wmissing-prototypes -Wundef -Wcast-qual -Wcast-align=strict -Wvla`
     `-Wstack-usage=256 -fstack-usage -fno-common -O1 -fno-inline`, once without
     `DEBUG_BUILD` and once with `-DDEBUG_BUILD`.
2. **Source Formatting & Forbidden Constructs (`include/*.h`, `src/*.c`)**:
   - Every line `<= 80` characters.
   - Zero tab characters (`\t`), zero `//` comments.
   - No `goto`, `continue`, `malloc`, or `free`.
   - File ends with exactly one trailing `\n`.
3. **Per-Function Stack Budget (`rules_release_*.su`)**:
   - Every function's stack frame must be `<= 256` bytes.
4. **Unit & Integration Tests**:
   - `test_adt7410_driver` (`src/adt7410_driver.c` + `tests/mock_axi_iic.c`, linked with `-lm`).
   - `test_axi_iic_hal` (`src/axi_iic_hal.c` + `tests/regmodel.h`).
   - `test_integration` (`src/adt7410_driver.c` + `src/axi_iic_hal.c` + `tests/regmodel.h`).
   - `test_app` (`src/main.c` compiled with `-Dmain=app_main` in the test build only + `src/adt7410_driver.c` + `tests/mock_axi_iic.c`).
   - Verify all test cases print `[PASS]` and `SUMMARY ... 0 failed`.
5. **Structural Coverage (`gcov`) & Optional `clang-tidy`**:
   - Review `tests/out/gcov_*_functions.log` and `tests/out/*.c.gcov` for any uncovered branches.
   - Any uncovered defensive branch must carry `/* DEFENSIVE [DO-178C]: ... */` and be explained in `docs/SDD_ADT7410_AXI_IIC.md` §7.2.

## 3. Troubleshooting Missing Toolchain on Windows

If `gcc` is not in `PATH`, `tests/run_tests.ps1` still executes the pure-PowerShell
formatting checks on `include/*.h` and `src/*.c`, then reports the missing
compiler. Install MSYS2 (`mingw-w64-ucrt-x86_64-gcc`, `make`, `cmake`) or run
inside WSL Ubuntu with `build-essential` and `cmake` installed.
