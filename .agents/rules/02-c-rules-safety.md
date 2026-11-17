# C Coding Rules (Part 2: Memory, Hardware, Verification, §8–§14)

Continuation of `docs/C_rules_v3_addDO178C.md` (Part 1 is in `01-c-rules-core.md`).
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
