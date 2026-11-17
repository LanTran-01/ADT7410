# C Coding Rules (MISRA C:2012 + BARR-C:2018 + DO-178A/B/C, unified)

**Scope:** All C code in this repository (bare-metal / RTOS embedded firmware,
ARM Cortex-A/R/M, safety-critical & high-reliability systems up to DO-178C
DAL A).
**Audience:** Any human or AI assistant writing or modifying code here.
**Priority when rules conflict:** safety > DO-178C > MISRA > BARR-C > project
convention.

Apply every rule below **without being reminded**. Rule references in brackets
(`[M 15.7]` = MISRA C:2012 Rule 15.7, `[M Dir 4.1]` = Directive 4.1,
`[B 1.3]` = BARR-C:2018 §1.3, `[DO §6.4.4.2]` = DO-178B/C section/table) are
for look-up only. Items marked *(project convention)* are stricter than the
standards and have no rule number.

If a rule must be broken (hardware register access, vendor API), write a
structured deviation comment on the line above containing the rule, reason,
and safety justification/mitigation, and record it in the project deviation
log as described in *MISRA Compliance:2020*:
`/* MISRA deviation: <rule> — <reason> — <mitigation> */`

---

## 0. Non-negotiable formatting

1. **Allman braces.** Every `{` and `}` on its own line, aligned with the
   keyword. Never cuddled. Always present, even for one statement.
   [M 15.6, B 1.3]
   ```c
   if (0U == len)
   {
       result = ERR_PARAM;
   }
   else
   {
       result = OK;
   }
   ```
2. **No `//` comments, ever.** Only `/* … */`, including trailing comments and
   TODOs. Never nest comments (`/*` inside `/* … */`). *(Project convention;
   stricter than BARR-C §2.1, which permits `//`.)* [M 3.1]
3. **Line length ≤ 80 characters** including indentation. Wrap argument lists
   one per line aligned under the first; wrap conditions one sub-expression per
   line with the operator starting the continuation line. [B 1.1]
   ```c
   result = uart_send(p_cfg,
                      p_buffer,
                      length,
                      UART_TIMEOUT_MS);

   if ((NULL == p_buffer)
       || (0U == length)
       || (length > UART_MAX_LEN))
   {
   ```

Code violating any of these three is wrong regardless of what else it gets
right.

---

## 1. Types and literals

- Only fixed-width types from `<stdint.h>`: `uint8_t`, `int32_t`, … Never bare
  `int`/`long`/`short`/`char` for data. Use `char` (or project `char_t`) only
  for text/strings and never for arithmetic; `int` only for `main()` or where a
  vendor API forces it. [M Dir 4.6, 10.1, 10.2, B 5.2]
- `bool` from `<stdbool.h>` for flags; test directly: `if (is_ready)`, never
  `== true`. [B 5.3]
- `size_t` for sizes and indices. Lengths are never signed.
- Unsigned for bit manipulation, counters, hardware values. Never mix signed
  and unsigned in one expression; never apply bitwise operators to signed
  types. [M 10.1, 10.4]
- **Integer promotion awareness:** Operands narrower than `int` (`uint8_t`,
  `uint16_t`) promote to `signed int` before arithmetic or bitwise operations.
  Explicitly cast the result back to the target narrow type, and cast narrow
  operands to the wider type *before* shifting: [M 10.3, 10.6, 10.8]
  ```c
  uint8_t  inv  = (uint8_t)(~raw_byte);                           /* ok */
  uint16_t sum  = (uint16_t)(a_u16 + b_u16);                      /* ok */
  uint32_t word = ((uint32_t)hi_u16 << 16U) | (uint32_t)lo_u16;   /* ok */
  ```
- Literals: unsigned end in `U`, long in `UL`; floats always have a decimal
  point and an `f` suffix. No octal literals except `0`. [M 7.1, 7.2, 7.3]
- No `float`/`double` `==` or `!=` comparison; use an explicit epsilon bound.
  No floating point in ISRs. *(Project convention; MISRA C:2012 has no
  dedicated rule — this was Rule 13.3 in MISRA C:2004.)*
- No pointer↔integer casts except MMIO addresses, and then only through one
  `volatile`-typed accessor defined once in the HAL (see §9). [M 11.4]
- Never cast away `const` or `volatile`. [M 11.8]
- Typedefs for every `struct`/`enum`/`union`: `snake_case_t`. Enum members and
  constants are `UPPER_SNAKE`. [B 5.1]

---

## 2. Naming

| Thing | Style | Example |
|:---|:---|:---|
| File / module | `snake_case` | `uart_driver.c`, `uart_driver.h` |
| Public function | `module_verb_noun` | `uart_send_byte()` |
| Private function | `static`, `verb_noun` | `static void flush_fifo(void)` |
| Variable | `snake_case` noun | `rx_count` |
| Global (forbidden across modules) | `g_` prefix | `g_tick_count` |
| Pointer | `p_` prefix, `pp_` for `**` | `p_buffer` |
| Boolean | `is_` / `has_` prefix | `is_ready` |
| Constant / macro / enum member | `UPPER_SNAKE` | `UART_BAUD_DEFAULT`, `STATE_IDLE` |
| Type | `snake_case_t` | `uart_config_t` |
| ISR | `_isr` suffix | `uart0_rx_isr()` |
| Quantity with a unit | unit in name | `timeout_ms`, `voltage_mv`, `angle_rad` |

- Names ≥ 3 characters except loop indices `i`, `j`, `k`; ≤ 31 significant
  characters. [B 3.1, M 5.1]
- **No type prefixes** (`f_`, `u8_`, `i32_`, `sz_`, `str_`). The only prefixes
  are the role prefixes in the table: `p_`, `pp_`, `g_`, `is_`/`has_`. The
  compiler knows the type; the name must say the *meaning and unit*. [B 3.1]
  ```c
  float velocity_mps;   /* ok */
  float f_velocity;     /* NOT ok: type prefix, no unit */
  float value_mps;      /* NOT ok: "value" says nothing */
  ```
- **No vague words** as a whole name or stem: `value`, `val`, `data`, `tmp`,
  `buf` alone, `info`, and `temp` meaning *temporary*. For temperature, always
  qualify the stem with what is measured: `die_temp_mdegc`,
  `ambient_temp_mdegc`.
- Never shadow a name from an enclosing scope; never reuse a name across
  different namespaces (type vs. variable vs. macro vs. tag). [M 5.3, 5.6–5.9]
- No identifiers starting with `_`; no redefinition of standard library or
  keyword names. [M 21.1, 21.2]

---

## 3. Files, modules, and coupling

- One module = one `.h` + one `.c`. The header exposes only the public API;
  everything else is `static` in the `.c`. [M 8.7, B 4.2]
- **Data & Control Coupling [DO §6.4.4.2, Table A-7]:**
  - **Zero `extern` shared globals across modules.** Modules exchange data
    exclusively through explicit public API parameters and return values.
  - **Minimal Data Coupling:** Pass only the struct fields or sub-structs a
    function actually uses; never pass a giant "god struct" when 1–2 fields
    suffice.
  - **Minimal Control Coupling:** Do not pass mode/selector flags (`int cmd`,
    `bool is_write`) that switch a function between unrelated behaviours; split
    into dedicated functions instead.
- Header: include guard `MODULE_NAME_H` → includes the header itself needs →
  public types → public constants → public prototypes. No object definitions,
  no executable code (except `static inline` helpers if strictly required).
  [M Dir 4.10, 8.5, B 4.2]
- `.c` order: file header comment (with `@file`, `@brief`, `@req`, `@design`) →
  includes (own header first, then system, vendor, project) → private `#define`
  → private types → `static` prototypes → `static` data → public functions →
  private functions. [B 4.3, DO §5.5]
- `#include` only at the top; `<>` for system/vendor, `""` for project. Never
  use backslashes or `'`, `"`, `/*`, `//` in header names [M 20.1, 20.2]; never
  use relative paths (`../`) — use include search paths instead *(project
  convention)*.
- Every file: 4-space indent (spaces only), ends in a single newline, starts
  with a comment stating purpose and traceability metadata. [B 1.1, 4.1]

---

## 4. Functions and traceability

- Prototype visible before use; declarations and definitions must match
  identically in parameter **names**, types, and qualifiers; `(void)` for no
  parameters. [M 8.2, 8.3, 8.4]
- One-file-only functions and objects are `static`. [M 8.7]
- Pointer parameters whose target data is not modified are `const`-qualified
  (`uint8_t const * p_src`). Avoid top-level `const` on by-value parameters to
  prevent prototype/definition mismatch. [M 8.3, 8.13]
- **Boundary validation:** Every **public** function and callback must
  validate every pointer against `NULL` and every length/divisor against named
  bounds at entry. Internal `static` helpers whose callers already guarantee
  valid inputs use `ASSERT()` rather than redundant runtime `if` checks that
  create unreachable dead branches. [M 2.1, 2.2, Dir 4.1, B 7.1, DO §6.4.4.2]
- Arrays are passed as pointer + explicit length; never `sizeof` a pointer
  parameter. [M 17.5]
- Never discard a non-void return value: use it or explicitly cast `(void)`
  with a justifying comment. [M 17.7]
- No variadic functions (`stdarg.h`), no direct or indirect recursion.
  [M 17.1, 17.2, DO §6.3.4]
- **Function pointers & Call-Graph determinism [DO §6.3.4f]:** Avoid dynamic
  function pointers. Where callbacks or state dispatch tables are unavoidable,
  store them in `static const` tables initialised at compile time and document
  all possible target functions (`/* @targets: fn_a, fn_b */`) so static WCET
  and stack analyzers can resolve the complete call graph.
- Single `return` at the end of the function; collect the outcome in a
  `result` variable. [M 15.5]
- ≤ 100 lines, cyclomatic complexity ≤ 10, ≤ 5 parameters; split otherwise.
  [B 6.1]
- **Bi-directional Traceability [DO §5.5, Table A-5]:** Every function (public
  and `static`), file-scope object, and public type must trace to a Low-Level
  Requirement (`@req`) and Software Design Description (`@design`). No orphan
  (untraced) code is permitted.
- Doxygen block in the header for every public function, and above the
  definition for every `static` function (`@brief`, `@req`, `@design`, `@param`
  with direction `[in]`/`[out]` and units, `@return` listing status values,
  `@note` for ISR-safety, reentrancy, blocking, and WCET bound). [B 6.2]

**Canonical shape — every generated function looks like this:**

```c
/**
 * @brief       Copy up to out_len bytes of module payload into p_out.
 * @req         LLR-MOD-042
 * @design      SDD-MOD-3.2.1
 * @param[in]   timeout_ms  Wait budget in milliseconds; 0 = non-blocking.
 * @param[out]  p_out       Destination buffer, must not be NULL.
 * @param[in]   out_len     Capacity of p_out in bytes, 1..MODULE_MAX_LEN.
 * @return      MODULE_OK, MODULE_ERR_PARAM, MODULE_ERR_TIMEOUT.
 * @note        Not ISR-safe. Reentrant.
 */
module_status_t module_read_payload(uint32_t timeout_ms,
                                    uint8_t * p_out,
                                    size_t out_len)
{
    module_status_t result = MODULE_OK;

    if ((NULL == p_out)
        || (0U == out_len)
        || (out_len > MODULE_MAX_LEN))
    {
        result = MODULE_ERR_PARAM;
    }
    else
    {
        /* body: one exit, braces everywhere, no magic numbers */
    }

    return result;
}
```

---

## 5. Expressions, statements, and MC/DC testability

- One side effect per statement; one assignment per line. No side effects in
  `sizeof`, right operands of `&&`/`||`, function arguments, or conditions.
  Never access more than one `volatile` object in a single expression or
  statement (each access is a side effect with unspecified order).
  [M 13.1–13.6]
  ```c
  if (buf[i++] == 0U)          /* NOT ok: side effect in condition */
  x = reg_status + reg_ctrl;   /* NOT ok: two volatiles, unspecified order */
  ```
- **MC/DC condition simplicity [DO Table A-7]:**
  - At most **3 (hard limit: 4) atomic boolean conditions** combined with `&&`
    or `||` in any `if`, `while`, or `for` decision. Split larger decisions
    into named `bool` step variables or nested `if` blocks.
  - **No coupled conditions:** Never reference the same variable more than once
    in a compound boolean decision (e.g. `(x > 0) && (y == 1 || x < 10)`), as
    coupled conditions create infeasible MC/DC test pairs.
- **Object-to-Source Traceability (no hidden branches) [DO §6.4.4.2b]:**
  Avoid C constructs that cause the compiler to emit hidden conditional
  branches in assembly not visible as control flow in C:
  - Avoid the ternary operator `? :` (especially with compound expressions);
    use explicit `if / else` when the decision affects safety-critical outputs.
  - Avoid assigning compound boolean expressions (`&&` / `||`) directly to a
    variable (`bool is_ok = (a > 0U) && (b < 5U);`) if the coverage tool
    instruments only control-flow statements.
- Parenthesise everything beyond `* / %` over `+ -`. [M 12.1]
- Constant on the **left** of `==`/`!=`: `if (0U == len)`. [B 8.2]
- Spaces around binary operators; none after unary, `->`, `.`, `[]`; one space
  after `if`/`for`/`while`/`switch`/`return`; none between a function name and
  `(`. [B 1.3]
- No `,` operator; no ternary nested in a ternary. [M 12.3]
- Shift amount `>= 0` and `< bit_width`, checked when it comes from runtime
  data; never shift a signed type. [M 10.1, 12.2]
- **Division and overflow protection:** Verify any runtime divisor is `!= 0`
  before `/` or `%`. Guard arithmetic against signed overflow (Undefined
  Behavior) and unintended unsigned wrap-around before computing. [M Dir 4.1]
- Pointer arithmetic only via array indexing `p_arr[idx]` within a single
  array; compare pointers only from the same object; ≤ 2 levels of pointer
  indirection. [M 18.1, 18.3, 18.4, 18.5]
- No magic numbers. Any literal other than `0`, `1`, `0U`, `1U` is a named
  constant with a comment giving its source (datasheet §, spec table). [B 8.1]
- One declaration per line, initialised at the point of definition; aggregates
  use explicit braces per level, structs use designated initialisers.
  [M 9.1, 9.2, 9.3]
- Pointer `*` attaches to the type with spaces around it: `uint8_t * p_data;`
  and `uint8_t const * p_src;`. [B 5.1]

---

## 6. Control flow, defensive code, and WCET bounds

- Every `if … else if …` chain ends with a final `else` (containing at least a
  defensive action or comment explaining why no action is required). [M 15.7]
- Every `switch`: `default` present (handling unexpected values defensively),
  every non-empty `case` ends in `break` (never `return`, to preserve the
  single point of exit in §4), no fall-through, labels indented one level and
  bodies one more. [M 15.5, 16.1–16.5]
- Every `switch` has at least two non-default cases; otherwise use `if/else`.
  *(Project convention; [M 16.6] only requires two clauses including
  `default`.)*
- **Dead Code vs. Defensive / Deactivated Code [DO §5.2.4, §6.4.4.2, §6.4.4.3]:**
  - **Dead code** (unreachable due to design/coding error or redundant checks)
    is strictly forbidden and must be removed.
  - **Defensive branches** required by MISRA (`default:` on a fully covered
    `enum`, or final `else` guarding against RAM bit-flips / hardware faults)
    that cannot be triggered by normal software inputs must be tagged with a
    standardised justification comment for the DO-178C Structural Coverage
    analysis:
    `/* DEFENSIVE [DO-178C]: <fault guarded> — unreachable in normal test */`
- Loop counter is an integer (prefer `size_t` or `uint32_t`), never modified
  in the body, never floating-point, condition has no side effects.
  [M 14.1, 14.2]
- **Statically bounded loops (WCET) [DO §6.3.4f]:** Every `for`, `while`, or
  `do … while` loop must have a provable upper iteration bound derivable from a
  named compile-time constant (`MAX_ITERATIONS` or `TIMEOUT_TICKS`).
- Every loop that waits on hardware or external state has an explicit bounded
  timeout or iteration guard, reads the register into a local **before** the
  first test, and keeps one `volatile` access per statement (§5): [M 9.1]
  ```c
  uint32_t reg_val = hal_read_reg();
  uint32_t timeout_ticks = READY_TIMEOUT_TICKS;

  while ((0U == (reg_val & READY_MASK))
         && (0U < timeout_ticks))
  {
      timeout_ticks--;
      reg_val = hal_read_reg();
  }
  ```
- No `goto`, no `continue`; at most one `break` terminating any loop;
  restructure control flow instead. [M 15.1, 15.4, B 8.6]
- Prefer `static const` lookup tables (with bounds checking) over large
  `switch` blocks for pure mappings.

---

## 7. Preprocessor and deactivated code

- Prefer `static inline` functions and `enum`/`const` over function-like
  macros. If a macro is unavoidable: every parameter and the whole body in
  parentheses; each parameter evaluated at most once; statement-like macros
  wrapped in `do { … } while (0)`; no `#` or `##`. [M Dir 4.9, 20.7, 20.10]
- No `#undef`; no `#define` of keywords or reserved names.
  [M 20.4, 20.5, 21.1]
- `#if` / `#elif` conditions use only `defined(MACRO)` and integer constant
  expressions; every `#else` and `#endif` carries a `/* MODULE_NAME_H */`
  comment naming its matching guard. [M 20.8, 20.9]
- **No in-function `#ifdef` (Deactivated Code control) [DO §5.2.4, §6.4.4.3]:**
  Never scatter `#if` / `#ifdef` inside function bodies to toggle features or
  hardware variants. Isolate target/variant differences at the module/file
  boundary (via HAL implementations selected by the build system) so every
  compiled translation unit is 100% active and verifiable.

---
