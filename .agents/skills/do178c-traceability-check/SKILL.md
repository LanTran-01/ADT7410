---
name: do178c-traceability-check
description: >-
  Use this skill when adding, modifying, or reviewing functions, types, tests,
  requirements, or MISRA deviations in the ADT7410 / AXI IIC codebase to ensure
  bi-directional DO-178C traceability across LLR, SDD, source, and tests.
---

# DO-178C Bi-Directional Traceability Check

Use this checklist before completing any code, test, or specification edit.

## 1. Authoritative Documents

- Low-Level Requirements: [LLR_ADT7410_AXI_IIC.md](../../../docs/LLR_ADT7410_AXI_IIC.md)
- Software Design Description: [SDD_ADT7410_AXI_IIC.md](../../../docs/SDD_ADT7410_AXI_IIC.md)
- Coding Rules: [C_rules_v3_addDO178C.md](../../../docs/C_rules_v3_addDO178C.md)
- Open Items: [OPEN_ITEMS.md](../../../docs/OPEN_ITEMS.md)

## 2. Source Code <-> SDD <-> LLR Checklist

1. **Every public type, public function, `static` function, and file-scope object**
   in `include/axi_iic_hal.h`, `src/axi_iic_hal.c`, `include/adt7410_driver.h`,
   `src/adt7410_driver.c`, and `src/main.c` (`LLR-APP` / `SDD-APP`) must carry `@req LLR-...` and `@design SDD-...` tags.
2. Verify each tag exists in `docs/LLR_ADT7410_AXI_IIC.md` and
   `docs/SDD_ADT7410_AXI_IIC.md`, and appears in the Traceability Matrix
   (`docs/SDD_ADT7410_AXI_IIC.md` §8). No orphan code is allowed.
3. Verify no requirement in `docs/LLR_ADT7410_AXI_IIC.md` is left unimplemented.

## 3. Deviation & Defensive Code Checklist

1. Any MISRA rule deviation in `include/` or `src/` must have an inline comment
   immediately above the line:
   `/* MISRA deviation: <rule> — <reason> — <mitigation> */`
   and a matching entry in `docs/SDD_ADT7410_AXI_IIC.md` §7.1 (currently `DEV-01`).
2. Any unreachable defensive branch (`default:` or bit-flip `else`) must carry:
   `/* DEFENSIVE [DO-178C]: <fault guarded> — unreachable in normal test */`
   and appear in `docs/SDD_ADT7410_AXI_IIC.md` §7.2.

## 4. Test Case Traceability Checklist

1. Every test function in `tests/test_axi_iic_hal.c`, `tests/test_adt7410_driver.c`,
   `tests/test_integration.c`, and `tests/test_app.c` must call `th_begin("TC-...", "LLR-...", "...")`
   referencing exact LLR clauses.
2. If you alter test assumptions or hardware register model behavior, update
   [test_report.md](../../../docs/test_report.md) accordingly.
