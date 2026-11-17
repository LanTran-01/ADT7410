#!/usr/bin/env bash
# run_tests.sh - static checks, unit + integration tests, gcov coverage.
# Usage: tests/run_tests.sh      (from any directory)
# Outputs: tests/out/*.log, tests/out/*.gcov
set -u

TEST_DIR="$(cd "$(dirname "$0")" && pwd)"
SRC_DIR="$(cd "$TEST_DIR/../src" && pwd)"
INC_DIR="$(cd "$TEST_DIR/../include" && pwd)"
OUT="$TEST_DIR/out"
SOURCES="axi_iic_hal.c adt7410_driver.c"
STATIC_SOURCES="axi_iic_hal.c adt7410_driver.c main.c"
RULE_FLAGS="-std=c99 -pedantic-errors -Wall -Wextra -Werror -Wconversion \
 -Wsign-conversion -Wshadow -Wstrict-prototypes -Wmissing-prototypes \
 -Wundef -Wcast-qual -Wcast-align=strict -Wvla -Wstack-usage=256 \
 -fstack-usage -fno-common"
TEST_FLAGS="-std=c99 -Wall -Wextra -O0 -g -I$INC_DIR -I$TEST_DIR"
overall=0

rm -rf "$OUT" && mkdir -p "$OUT"
cd "$OUT" || exit 1

echo "=== 1. Static checks (C rules §0, §12) ==="
for build in "release:" "debug:-DDEBUG_BUILD"; do
    name="${build%%:*}"; def="${build#*:}"
    for src in $STATIC_SOURCES; do
        if gcc $RULE_FLAGS -O1 -fno-inline $def -I"$INC_DIR" \
               -c "$SRC_DIR/$src" -o "rules_${name}_${src%.c}.o" \
               2> "rules_${name}_${src%.c}.log"; then
            echo "[PASS] compile $name $src (strict flags, -Werror)"
        else
            echo "[FAIL] compile $name $src"; cat "rules_${name}_${src%.c}.log"
            overall=1
        fi
    done
done
python3 - "$SRC_DIR" "$INC_DIR" <<'EOF' || overall=1
import sys, glob, os, re
bad = 0
files = []
for d in sys.argv[1:]:
    files.extend(glob.glob(os.path.join(d, '*.[ch]')))
for f in sorted(files):
    text = open(f, encoding='utf-8').read()
    for n, line in enumerate(text.split('\n'), 1):
        code = re.sub(r'/\*.*?\*/', '', line)
        if len(line) > 80: print(f'  {f}:{n}: line > 80 chars'); bad += 1
        if '\t' in line:   print(f'  {f}:{n}: TAB'); bad += 1
        if '//' in code:   print(f'  {f}:{n}: // comment'); bad += 1
        if re.search(r'\b(goto|continue|malloc|free)\b', code):
            print(f'  {f}:{n}: forbidden keyword'); bad += 1
    if not text.endswith('\n') or text.endswith('\n\n'):
        print(f'  {f}: must end with exactly one newline'); bad += 1
tag = 'PASS' if bad == 0 else 'FAIL'
print(f'[{tag}] formatting: <=80 cols, no TAB, no //, no goto/continue/'
      f'malloc, single final newline ({bad} findings)')
sys.exit(1 if bad else 0)
EOF
awk -F'\t' '{split($1,a,":"); if ($2 > 256) bad=1;
     if ($2 > max) {max=$2; fn=a[4]}} END {
     printf("[%s] stack: max %d B per function (%s), limit 256 B\n",
            bad ? "FAIL" : "PASS", max, fn); exit bad}' rules_release_*.su \
     || overall=1

echo
echo "=== 2. Build tests with coverage ==="
for src in $SOURCES; do
    gcc $TEST_FLAGS --coverage -c "$SRC_DIR/$src" -o "${src%.c}.o" || exit 1
done
gcc $TEST_FLAGS -c "$TEST_DIR/mock_axi_iic.c" -o mock_axi_iic.o || exit 1
gcc $TEST_FLAGS --coverage "$TEST_DIR/test_adt7410_driver.c" \
    adt7410_driver.o mock_axi_iic.o -lm -o test_adt7410_driver || exit 1
gcc $TEST_FLAGS --coverage "$TEST_DIR/test_axi_iic_hal.c" \
    axi_iic_hal.o -o test_axi_iic_hal || exit 1
gcc $TEST_FLAGS --coverage "$TEST_DIR/test_integration.c" \
    adt7410_driver.o axi_iic_hal.o -o test_integration || exit 1
echo "[PASS] build"

echo
echo "=== 3. Run test suites ==="
for t in test_adt7410_driver test_axi_iic_hal test_integration; do
    ./"$t" > "$t.log" 2>&1
    rc=$?
    grep -E '^\[(PASS|FAIL)\]|FAIL ' "$t.log"
    grep '^SUMMARY' "$t.log"
    [ $rc -eq 0 ] || overall=1
done

echo
echo "=== 4. Structural coverage (gcov: statement + branch) ==="
for src in $SOURCES; do
    gcov -f -o . "$SRC_DIR/$src" 2>/dev/null \
        | awk '/^Function/ {fn=$2; next}
               /^File/ {fn=""; next}
               /^Lines executed/ && fn != "" {printf "    %-24s %s\n", fn, $0}' \
        | tr -d "'" | sort > "gcov_${src%.c}_functions.log"
    # Run last so the annotated *.c.gcov keeps branch counts
    gcov -b -c -o . "$SRC_DIR/$src" > "gcov_${src%.c}.log" 2>&1
    grep -A4 "File '.*$src'" "gcov_${src%.c}.log" | sed 's/^/  /'
done

echo
echo "=== 5. Supplementary static analysis (clang-tidy, if installed) ==="
if command -v clang-tidy > /dev/null 2>&1; then
    checks='-*,clang-analyzer-*,bugprone-*,cert-*,misc-*,readability-braces-around-statements,readability-else-after-return,-misc-include-cleaner,-bugprone-easily-swappable-parameters'
    for src in $SOURCES; do
        clang-tidy -checks="$checks" "$SRC_DIR/$src" -- -std=c99 \
            -I"$INC_DIR" > "tidy_${src%.c}.log" 2>&1
        n=$(grep -c 'warning:' "tidy_${src%.c}.log")
        if [ "$n" -eq 0 ]; then
            echo "[PASS] clang-tidy $src (0 warnings)"
        else
            echo "[INFO] clang-tidy $src: $n warnings (tests/out/tidy_*.log)"
        fi
    done
else
    echo "[SKIP] clang-tidy not installed"
fi
echo "  Per-function details: tests/out/gcov_*_functions.log"
echo "  Annotated source:     tests/out/*.c.gcov"

echo
if [ $overall -eq 0 ]; then echo "OVERALL: PASS"; else echo "OVERALL: FAIL"; fi
exit $overall
