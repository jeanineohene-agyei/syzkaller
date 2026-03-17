#!/bin/bash
# compare_fidl_syzlang.sh
# Compares Fuchsia Zircon FIDL syscall definitions against syzkaller syzlang descriptions
# Usage: ./compare_fidl_syzlang.sh /path/to/fuchsia /path/to/syzkaller

FUCHSIA_DIR="${1:-/proj/ecs-251-PG0/groups/fuzzing/fuchsia}"
SYZKALLER_DIR="${2:-/proj/ecs-251-PG0/groups/fuzzing/syzkaller}"

FIDL_DIR="$FUCHSIA_DIR/zircon/vdso"
SYZLANG_DIR="$SYZKALLER_DIR/sys/fuchsia"

echo "============================================"
echo "FIDL vs Syzlang Comparison"
echo "============================================"
echo "FIDL dir:    $FIDL_DIR"
echo "Syzlang dir: $SYZLANG_DIR"
echo ""

# --- Step 1: Extract syscall names from FIDL files ---
echo "--- Extracting syscall names from FIDL files ---"
FIDL_SYSCALLS=$(mktemp)
# Look for method names in FIDL protocol definitions
# FIDL syscalls follow the pattern: method_name(struct { ... }) -> (...)
grep -rhoP '^\s+\K\w+(?=\s*\()' "$FIDL_DIR"/*.fidl 2>/dev/null | \
    grep -v '^struct$\|^resource$\|^protocol$\|^library$\|^using$\|^type$\|^strict$\|^flexible$' | \
    sort -u > "$FIDL_SYSCALLS"

# Also try extracting with zx_ prefix pattern from FIDL
FIDL_SYSCALLS_ZX=$(mktemp)
grep -rhoP '\b\w+(?=\s*\(struct)' "$FIDL_DIR"/*.fidl 2>/dev/null | \
    sort -u >> "$FIDL_SYSCALLS"
sort -u -o "$FIDL_SYSCALLS" "$FIDL_SYSCALLS"

echo "Found $(wc -l < "$FIDL_SYSCALLS") potential syscall definitions in FIDL"
echo ""

# --- Step 2: Extract syscall names from syzlang files ---
echo "--- Extracting syscall names from syzlang files ---"
SYZLANG_SYSCALLS=$(mktemp)
# Syzlang syscalls start with zx_ at the beginning of a line
grep -rhoP '^zx_\w+' "$SYZLANG_DIR"/*.txt 2>/dev/null | \
    sed 's/^zx_//' | \
    sort -u > "$SYZLANG_SYSCALLS"

echo "Found $(wc -l < "$SYZLANG_SYSCALLS") syscall descriptions in syzlang"
echo ""

# --- Step 3: List FIDL files and corresponding syzlang coverage ---
echo "============================================"
echo "FIDL Files"
echo "============================================"
for fidl_file in "$FIDL_DIR"/*.fidl; do
    [ -f "$fidl_file" ] || continue
    basename=$(basename "$fidl_file" .fidl)
    echo "  $basename.fidl"
done
echo ""

echo "============================================"
echo "Syzlang Files"
echo "============================================"
for txt_file in "$SYZLANG_DIR"/*.txt; do
    [ -f "$txt_file" ] || continue
    echo "  $(basename "$txt_file")"
done
echo ""

# --- Step 4: Compare syscalls ---
echo "============================================"
echo "Syscalls in FIDL but NOT in syzlang (MISSING)"
echo "============================================"
MISSING=$(comm -23 "$FIDL_SYSCALLS" "$SYZLANG_SYSCALLS")
if [ -z "$MISSING" ]; then
    echo "  (none - all FIDL syscalls have syzlang descriptions)"
else
    echo "$MISSING" | while read -r syscall; do
        # Find which FIDL file defines this syscall
        fidl_source=$(grep -rl "$syscall" "$FIDL_DIR"/*.fidl 2>/dev/null | head -1 | xargs basename 2>/dev/null)
        echo "  zx_$syscall  (from: ${fidl_source:-unknown})"
    done
fi
echo ""

echo "============================================"
echo "Syscalls in syzlang but NOT in FIDL (STALE)"
echo "============================================"
STALE=$(comm -13 "$FIDL_SYSCALLS" "$SYZLANG_SYSCALLS")
if [ -z "$STALE" ]; then
    echo "  (none - all syzlang descriptions match FIDL definitions)"
else
    echo "$STALE" | while read -r syscall; do
        # Find which syzlang file defines this syscall
        txt_source=$(grep -rl "^zx_$syscall" "$SYZLANG_DIR"/*.txt 2>/dev/null | head -1 | xargs basename 2>/dev/null)
        echo "  zx_$syscall  (in: ${txt_source:-unknown})"
    done
fi
echo ""

echo "============================================"
echo "Syscalls present in BOTH (covered)"
echo "============================================"
BOTH=$(comm -12 "$FIDL_SYSCALLS" "$SYZLANG_SYSCALLS")
BOTH_COUNT=$(echo "$BOTH" | grep -c .)
FIDL_COUNT=$(wc -l < "$FIDL_SYSCALLS")
echo "  $BOTH_COUNT out of $FIDL_COUNT FIDL syscalls have syzlang descriptions"
echo ""

# --- Step 5: Summary ---
MISSING_COUNT=$(echo "$MISSING" | grep -c . 2>/dev/null || echo 0)
STALE_COUNT=$(echo "$STALE" | grep -c . 2>/dev/null || echo 0)

echo "============================================"
echo "Summary"
echo "============================================"
echo "  FIDL syscalls:           $FIDL_COUNT"
echo "  Syzlang descriptions:    $(wc -l < "$SYZLANG_SYSCALLS")"
echo "  Covered (in both):       $BOTH_COUNT"
echo "  Missing (need to add):   $MISSING_COUNT"
echo "  Stale (need to remove):  $STALE_COUNT"

# Cleanup
rm -f "$FIDL_SYSCALLS" "$SYZLANG_SYSCALLS"
