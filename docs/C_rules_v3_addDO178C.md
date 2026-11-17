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

## 8. Memory, serialization, parameter data items, and standard library

- No dynamic allocation (`malloc`, `calloc`, `realloc`, `free`); no VLAs; no
  `alloca()`. Use `static` module buffers or a deterministic fixed-block pool
  allocator. [M 18.8, 21.3, DO §6.3.4]
- **Stack budget & WCSU [DO §6.3.4f]:** Never allocate large arrays or structs
  on the stack. Any local buffer `> 64 bytes` must not live on the stack
  *(project convention)*:
  - in a **non-reentrant** function (called from one context only), make it
    `static` at module scope;
  - in a **reentrant or ISR-callable** function, take the buffer from the
    caller as pointer + length. A `static` buffer here is a bug: two callers
    would overwrite each other's data.
  State reentrancy in the function's `@note`.
- No `<stdio.h>`, `<stdlib.h>`, `<setjmp.h>`, `<signal.h>`, `<time.h>`,
  `<tgmath.h>`, or `<fenv.h>` in production code [M 21.3–21.12]; no
  `<errno.h>` *(project convention; see also [M 22.8–22.10])*.
- `<string.h>` only with explicit checked lengths (`memcpy`, `memset`,
  `memcmp`, `memmove`); never `strcpy`, `strcat`, `sprintf`, `gets`, `atoi`,
  `strtok`. Use project bounds-checked `*_n` wrappers for strings.
  [M 21.7, B 8.4]
- **No pointer-cast struct overlays (aliasing & alignment):** Never cast a
  byte buffer (`uint8_t *`) to a `struct *` or multi-byte scalar pointer to
  parse/encode packets. It violates strict aliasing and causes unaligned
  access faults: on Cortex-M0/M0+ for any multi-byte access, on
  Cortex-M3/M4/M7/R for `LDRD`/`LDM`, and on Cortex-A53 bare-metal with the
  MMU off for **any** unaligned access (all memory is treated as Device).
  [M 11.3]
  - For wire/protocol formats, serialize and deserialize explicitly
    byte-by-byte with shifts and masks; this also fixes endianness.
  - `memcpy()` into a local struct only when both sides use the same
    in-memory layout (same compiler, padding, and endianness), never for
    data crossing a link or another processor.
  - Avoid `__attribute__((packed))` unless mandated by vendor headers, with a
    deviation.
- **Parameter Data Items (PDI) integrity [DO-178C §2.5, §5.2.2, §6.3.5]:**
  Calibration tables, filter coefficients, RF/radar lut tables, and NVM
  configuration blocks that govern system behaviour are *Parameter Data Items*.
  Every PDI block must include a schema version, explicit per-field range
  validation, and an end-to-end **CRC-32** (or cryptographic hash) verified at
  load/init before any parameter is consumed.
- No `union` for type punning; hardware-layout unions only with a deviation
  comment. [M 19.2]
- String literals bind only to `char const *`. [M 7.4]
- Every write into an array is bounds-checked against the array's named
  capacity constant before indexing. [M 18.1]

---

## 9. Hardware, ISRs, partitioning, and SEU robustness

- `volatile` only on objects shared with ISRs, DMA, or memory-mapped
  hardware — nothing else. Never assume `volatile` provides atomicity or
  memory ordering. [B 8.7]
- Register access only through `volatile`-typed pointers or a single
  `REG_READ()`/`REG_WRITE()` inline/macro pair defined once in the HAL header.
  [M 11.4]
- Bit fields in hardware registers via explicit `_MASK`/`_SHIFT` constants;
  never C bit-field structs (bit ordering and padding are
  implementation-defined). *(Project convention; [M 6.1] only restricts
  bit-field types.)*
- Read-modify-write of a shared register, and any access to shared data wider
  than the native atomic word (or multi-field structs), happens inside a
  critical section. Nothing wider than a 32-bit aligned word is assumed
  atomic.
- **Memory barriers and cache coherency (Cortex-A/R/M7):** When sharing
  buffers with DMA, configuring MPU/MMU/SCB registers, or synchronizing across
  cores/ISRs, insert explicit architectural barriers (`DMB`, `DSB`, `ISB` via
  the CMSIS or vendor BSP intrinsics) and perform explicit D-cache
  clean/invalidate operations. Every DMA buffer must be **both** aligned to
  the cache line **and** sized to a whole multiple of it (64 B on Cortex-A53,
  32 B on Cortex-M7); otherwise an invalidate destroys neighbouring variables
  that share the line:
  ```c
  #define CACHE_LINE_BYTES  (64U)  /* Cortex-A53 TRM, L1 D-cache line */
  static uint8_t dma_rx_buf[512U]
      __attribute__((aligned(CACHE_LINE_BYTES)));  /* 512 = 8 lines */
  ```
- **SEU / Bit-flip resilience on critical state [DO §2.4, §6.3.4]:** For
  safety-critical state machines, arming latches, or partition permissions
  (DAL A/B), assign explicit 32-bit enum patterns with **Hamming distance ≥ 4**
  (e.g. `STATE_DISARMED = 0xA5C3691EU`, `STATE_ARMED = 0x5A3C96E1U`) instead of
  `0, 1, 2` or plain `bool`, so single/double bit-flips in RAM trap into the
  defensive `default:` handler.
- ISRs: short, deterministic, no blocking loops, no floating point, no RTOS or
  library calls that allocate or lock; set a `volatile` flag or push to a
  lock-free/ISR-safe queue and return. Every ISR must have a visible prototype
  or vector-table declaration. [M 8.4, B 8.8]

---

## 10. Error handling and assertions

- Every module defines `module_status_t`; `MODULE_OK = 0` first, errors
  positive and explicitly numbered or sequentially ordered. *(Project
  convention — neither MISRA nor BARR-C mandates a status return.)*
- A function **that can fail** (NULL input, out-of-range length, timeout,
  hardware error, CRC mismatch) returns `module_status_t`; output data goes
  out through pointer parameters. [M Dir 4.7]
- A function **that cannot fail** (pure math on pre-validated inputs, simple
  getter) returns `void` or its plain result. Do not invent a status just to
  "look standard" — no explanation needed.
- A function that **can fail but deliberately returns `void`**
  (fire-and-forget send, ISR, fault logger) must say so in its Doxygen
  `@note`: what error is swallowed, and why that is acceptable. Without that
  note the function is wrong.
- Every returned status is checked at every call site; none discarded
  silently. [M Dir 4.7, 17.7]
- **Assertions & Structural Coverage [B 7.3, DO §6.4.4.2]:** No libc `assert()`
  in production. Use the project `ASSERT(expr)` macro to guard internal
  invariants and programmer contracts (e.g. inside `static` helpers), never
  external runtime input or hardware state. No side effects inside `ASSERT()`.
  In flight/release builds subject to DO-178C MC/DC or Decision coverage,
  `ASSERT(expr)` must compile out to `((void)0)` (with invariants proven via
  unit tests / DO-333 formal analysis) or be formally accounted for as
  deactivated defensive code so its failure branch does not create unreached
  coverage gaps.

---

## 11. Comments

- Explain **why** (intent, rationale, datasheet reference, algorithm
  constraint), never restate what the code already says. [B 2.2]
- No commented-out code (`/* x = 1; */` or `#if 0`) — delete it; git keeps
  history. [M Dir 4.4]
- `/* TODO(name): … */` for incomplete work (forbidden in baseline certification
  builds), `/* WARNING: … */` for hazards.
- Blank line between logical blocks; two blank lines between function
  definitions.

### 11.1 Units and scaling (mandatory for every physical quantity)

A "physical quantity" is anything with a unit: time, voltage, current,
temperature, distance, angle, velocity, acceleration, frequency, pressure,
attenuation, power (`dBm`, `mdB`), or raw sensor counts.

- **Names carry the unit** for variables, parameters, struct fields,
  constants, and return-holding variables: `timeout_ms`, `vbus_mv`,
  `die_temp_mdegc`, `rate_hz`, `accel_x_mg`, `angle_rad`. Raw sensor/ADC
  values use `_raw` or `_lsb`. [B 5.1]
  ```c
  #define UART_TIMEOUT_MS     (500U)   /* NOT: UART_TIMEOUT */
  int32_t die_temp_mdegc;              /* NOT: int32_t temp; */
  ```
- **Doxygen states the unit and valid range** of every `@param` and `@return`
  that is a quantity, even when the name already has it:
  ```c
  /**
   * @param[in]  period_us  Sample period, microseconds, 100..1000000.
   * @return     Die temperature in milli-degrees Celsius, -40000..125000.
   */
  ```
- **Fixed-point and scaled integers declare their format** at the typedef or
  field: the scale factor and the exact representable range. Use `Qm.n`
  notation where `m` counts integer bits **excluding** the sign bit (so
  `int16_t` with 15 fraction bits is `Q0.15`, usually written `Q15`):
  ```c
  /* Q15: value = raw / 2^15, range [-1.0, +1.0 - 2^-15].
   * +1.0 is NOT representable; 32767 is the largest value.
   * Angle meaning: raw / 2^15 * pi radians. */
  typedef int16_t angle_q15_t;

  uint16_t voltage_x100;   /* volts * 100, 1250 == 12.50 V, 0..655.35 V */
  ```
  Arithmetic on a scaled value is preceded by a comment giving the scale
  factor, the intermediate widening type (to prevent overflow), and the
  source formula.
- **Conversion functions name both ends**: `ms_to_ticks()`, `adc_raw_to_mv()`,
  `deg_to_q15()`. Never a bare `convert()` or `scale()`.
- **Struct fields never rely on the struct name for their unit**:
  ```c
  typedef struct
  {
      int32_t x_mg;    /* milli-g, -16000..+16000 */
      int32_t y_mg;
      int32_t z_mg;
  } accel_sample_t;
  ```
- **Magic scaling literals are named** with the unit in the name and the
  source in a comment: `#define LSB_PER_G (16384U) /* MPU6050 FS_SEL=0 */`.
- Where the hardware unit differs from the API unit (raw LSB vs. mV), the
  driver converts exactly once, at the module boundary, and the raw value
  never escapes the `.c` file.

---

## 12. Build flags and Object-to-Source traceability

Compile with C99 (`-std=c99 -pedantic-errors`) and at minimum:
`-Wall -Wextra -Werror -Wconversion -Wsign-conversion -Wshadow
 -Wstrict-prototypes -Wmissing-prototypes -Wundef -Wcast-qual
 -Wcast-align=strict -Wvla -Wstack-usage=256 -fstack-usage -fno-common`
and fix every warning in application code; never suppress warnings with
`#pragma` without a formal deviation record. [B 1.6]

- `-Wcast-align=strict` (GCC ≥ 8) is required on ARM: plain `-Wcast-align`
  stays silent on targets that tolerate some unaligned access, so it misses
  exactly the bugs §8 forbids.
- `-fstack-usage` emits per-function `.su` files required to verify Worst-Case
  Stack Usage (WCSU) against the call graph. [DO §6.3.4f]
- **Deterministic optimization (DAL A/B) [DO §6.4.4.2b]:** Do not enable
  aggressive optimizations that reorder/merge control flow or inline across
  functions (`-O3`, `-flto`, `-funroll-loops`, `-finline-functions`), as they
  break 1-to-1 Object-to-Source code traceability and invalidate source-level
  MC/DC coverage. Use the project's qualified optimization profile (typically
  `-O0` or `-O1 -fno-inline`).

---

## 13. Forbidden outright

`goto`, `continue`, multiple `return`s, `return` inside `switch`, recursion,
dynamic/unannotated function pointers, `extern` shared global variables across
modules, `malloc`/`calloc`/`realloc`/`free`, VLAs, `alloca`, local stack
buffers `> 64B`, `static` buffers in reentrant/ISR-callable functions,
unbounded loops, `> 4` boolean conditions in one decision, coupled variables in
compound boolean decisions, in-function `#ifdef` blocks, `register`, `auto`,
K&R definitions, implicit `int`, `//` comments, nested `/*` comments,
commented-out code (`#if 0`), untraced functions/objects (missing `@req` /
`@design`), unverified Parameter Data Items (missing CRC/range check), cuddled
braces, trigraphs, octal literals other than `0`, `sizeof` on expressions with
side effects, multiple `volatile` accesses in one statement, C bit fields on
hardware registers, type-punning unions, direct `(struct_t *)p_byte_buf`
pointer casts, DMA buffers not aligned and sized to the cache line, `assert()`
in release, unchecked return values, unchecked runtime divisors, lines over 80
characters.

---

## 14. Self-check before returning code

Confirm every item silently; fix before replying:

0. §0 — Allman braces, zero `//`, every line ≤ 80 chars.
1. Fixed-width types only; explicit casts on narrow (`uint8_t`/`uint16_t`)
   arithmetic/shifts; no signed/unsigned mixing; `U` suffixes on unsigned
   literals.
2. Braces on every block; final `else` on every `if … else if` chain;
   `default` and `break` (never `return`) on every `switch` case; defensive
   unreachable branches tagged with `/* DEFENSIVE [DO-178C]: … */`.
3. Single `return` at function end; no `goto`/`continue`/recursion/`malloc`/
   VLA; no stack buffer `> 64B`, and no `static` buffer in a reentrant
   function; no in-function `#ifdef`.
4. All public API pointers `NULL`-checked and `const` where unmodified (no
   redundant dead-code checks in private helpers); all lengths and runtime
   divisors bounded; no direct byte-buffer-to-struct pointer casts; zero
   `extern` globals across modules.
5. Decisions have ≤ 3–4 uncoupled boolean conditions (MC/DC-ready); no hidden
   control-flow branches (`? :` or boolean-assigned `&&`/`||`).
6. All non-void return values used or `(void)`-cast.
7. Names per §2 (no vague words, no type prefixes); constants on the left of
   `==`; no magic numbers; critical state enums use high Hamming distance
   patterns where required.
8. Doxygen block on every function (`@req`, `@design`, `@param[in/out]`, units,
   ranges, reentrancy); comments explain why, not what.
8a. Every physical quantity carries its unit in the name, in
    `@param`/`@return`, and in struct fields; scaled/fixed-point formats state
    scale factor and exact range (§11.1); Parameter Data Items carry CRC and
    range checks (§8).
9. `volatile` only on hardware/ISR-shared data (max one `volatile` access per
   statement); every polled register read into a local before the first test;
   critical sections and barriers where needed; DMA buffers cache-line aligned
   and sized; statically bounded timeouts on all loops/waits.
10. Every deviation carries
    `/* MISRA deviation: <rule> — <reason> — <mitigation> */`.
