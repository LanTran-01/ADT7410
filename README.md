# ADT7410 Temperature Sensor Driver & Xilinx AXI IIC HAL

Safety-critical C99 driver for the Analog Devices **ADT7410** 16-bit digital
I2C temperature sensor built on a polled **Xilinx AXI IIC** Hardware Abstraction
Layer (HAL), designed to meet **MISRA C:2012**, **BARR-C:2018**, and
**DO-178C (up to DAL A)** coding and traceability requirements.

## Architecture

```text
+------------------------------------------------------+
| Application / Health Monitor  (src/main.c)           |
+------------------------------------------------------+
            | adt7410_* API  (float degC, mdegC, Q8.7, flags)
            v
+------------------------------------------------------+
| ADT7410 Driver  (include/ & src/adt7410_driver.*)    |
+------------------------------------------------------+
            | axi_iic_write_bytes / axi_iic_write_read_bytes
            v
+------------------------------------------------------+
| AXI IIC HAL     (include/ & src/axi_iic_hal.*)       |
+------------------------------------------------------+
            | 32-bit volatile MMIO (reg_read32/reg_write32)
            v
+------------------------------------------------------+
| Xilinx AXI IIC IP (PL)  --SCL/SDA 100 kHz-->  ADT7410|
+------------------------------------------------------+
```

## Project Structure

- `include/` — Public C99 module headers (`axi_iic_hal.h`, `adt7410_driver.h`).
- `src/` — Flight C99 implementations (`axi_iic_hal.c`, `adt7410_driver.c`) and
  reference application entry point (`main.c` with Polling & Interrupt examples).
- `tests/` — Host unit/integration test suites (`test_axi_iic_hal.c`,
  `test_adt7410_driver.c`, `test_integration.c`, `test_app.c`), mocks/register model
  (`mock_axi_iic.c/.h`, `regmodel.h`, `test_harness.h`), and test runners
  (`run_tests.sh`, `run_tests.ps1`).
- `docs/` — Requirements, design, coding standard, usage guide, verification
  baseline, and open items (`C_rules_v3_addDO178C.md`, `LLR_ADT7410_AXI_IIC.md`,
  `SDD_ADT7410_AXI_IIC.md`, `HLR_ADT7410_AXI_IIC.md`, `DRIVER_USAGE_GUIDE.md`,
  `test_report.md`, `OPEN_ITEMS.md`).
- `.agents/` — Antigravity IDE rules (`.agents/rules/*.md`) and skills
  (`.agents/skills/*/SKILL.md`).
- `.vscode/` — IDE settings, IntelliSense (`c_cpp_properties.json`), build/test
  tasks, and debug launch configurations.
- `GEMINI.md` — Root Antigravity IDE instructions.
- `AI_AGENT_GUIDE.md` — Master guide & 6-axis cross-check protocol for AI agents reviewing or auditing this codebase.

## Prerequisites

- **Formatting checks only (Windows)**: PowerShell (built-in).
- **Full compile, static checks, unit tests, stack usage & gcov coverage**:
  - **Windows**: MSYS2 / MinGW-w64 (`gcc`, `gcov`), optional `cmake` and `clang-tidy`, OR WSL with `build-essential` + `python3`.
  - **Linux / macOS**: `gcc`, `gcov`, `python3`, `awk`, optional `cmake` and `clang-tidy`.

## Build & Verification

### 1. Native PowerShell Runner (Windows)

```powershell
# Format & banned-construct checks only (works without GCC):
powershell -ExecutionPolicy Bypass -File tests/run_tests.ps1 -FormatOnly

# Full static checks, unit/integration tests, stack usage & gcov coverage:
powershell -ExecutionPolicy Bypass -File tests/run_tests.ps1
```

### 2. Bash Runner (Linux / WSL / MSYS2)

```bash
bash tests/run_tests.sh
```

### 3. CMake / CTest

```bash
cmake -S . -B build -DENABLE_COVERAGE=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

### 4. Make

```bash
make static-check
make test
make coverage
```

## Open Items

Track unresolved certification/hardware integration items (`OI-01`..`OI-08`) in
[docs/OPEN_ITEMS.md](docs/OPEN_ITEMS.md).
