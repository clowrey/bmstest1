#!/usr/bin/env bash
set -euo pipefail

BUILD_DIR="${1:-$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/build}"
shift || true

if ! command -v gcov >/dev/null 2>&1; then
    echo "error: gcov is required but was not found on PATH" >&2
    exit 1
fi

if [[ ! -d "$BUILD_DIR" ]]; then
    echo "error: build directory not found: $BUILD_DIR" >&2
    exit 1
fi

cd "$BUILD_DIR"
mkdir -p coverage

# Remove previous counters to avoid stale checksum warnings and mixed runs.
find . -name '*.gcda' -delete

# Decide which tests to run.
TEST_BINS=()
if [[ "$#" -gt 0 ]]; then
    for t in "$@"; do
        if [[ -x "$t" ]]; then
            TEST_BINS+=("$t")
        elif [[ -x "./$t" ]]; then
            TEST_BINS+=("./$t")
        else
            echo "warning: test binary not found or not executable: $t" >&2
        fi
    done
else
    while IFS= read -r bin; do
        TEST_BINS+=("$bin")
    done < <(find . -maxdepth 1 -type f -name 'test_*' -perm -111 | sort)
fi

if [[ "${#TEST_BINS[@]}" -eq 0 ]]; then
    echo "error: no executable test binaries found in $BUILD_DIR" >&2
    exit 1
fi

echo "Running ${#TEST_BINS[@]} test binary/binaries for coverage..."
for bin in "${TEST_BINS[@]}"; do
    run_bin="$bin"
    if [[ "$run_bin" != */* ]]; then
        run_bin="./$run_bin"
    fi
    echo "  -> $run_bin"
    "$run_bin"
done

mapfile -t GCDA_FILES < <(find . -name '*.gcda' | sort)
if [[ "${#GCDA_FILES[@]}" -eq 0 ]]; then
    echo "error: no .gcda files were produced; ensure tests were built with coverage flags" >&2
    exit 1
fi

GCOV_OUTPUT="coverage/gcov_output.txt"
: > "$GCOV_OUTPUT"

for f in "${GCDA_FILES[@]}"; do
    gcov -b -c "$f" >> "$GCOV_OUTPUT" 2>&1
done

SUMMARY_FILE="coverage/summary.txt"
awk '
BEGIN {
    files = 0;
    covered = 0.0;
    total = 0;
}
/^Lines executed:/ {
    # Example: "Lines executed:63.42% of 257"
    split($2, parts, ":");
    pct = parts[2];
    gsub("%", "", pct);
    lines = $4 + 0;

    files += 1;
    total += lines;
    covered += (pct / 100.0) * lines;
}
END {
    if (total == 0) {
        print "No line coverage data found.";
        exit 1;
    }

    printf("Files measured: %d\n", files);
    printf("Covered lines: %.0f\n", covered);
    printf("Total lines: %d\n", total);
    printf("Line coverage: %.2f%%\n", (covered * 100.0) / total);
}
' "$GCOV_OUTPUT" > "$SUMMARY_FILE"

echo
echo "Coverage summary written to: $BUILD_DIR/$SUMMARY_FILE"
cat "$SUMMARY_FILE"
echo
echo "Detailed gcov output written to: $BUILD_DIR/$GCOV_OUTPUT"
