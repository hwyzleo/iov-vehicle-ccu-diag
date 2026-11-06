#!/bin/bash
# ci/run-ci-gates.sh - CCU-DIAG-DSN-CR-009 §9 commit-level CI gates
#
# Runs DIAG's CR-009 HOST/commit gates in order:
#   1. CMake build-contract lint
#   2. systemd baseline unit contract
#   3. unified diag.yaml schema + cross-field/secret checks (positive+negative)
#   4. configure + build + unit/contract tests (HOST, mock SOME/IP)
#   5. installed Framework SDK consumer (relocatable find_package + link)
#   6. production configure (BUILD_TESTING=OFF) proves GTest/tests are absent
#   7. DESTDIR install + exact ccu-diag-runtime manifest + repeat-install digest
#   8. daemon lifecycle smoke (temp config root -> SIGTERM bounded stop)
#
# TARGET cross-compile / ELF / ABI / NEEDED / RPATH / target-machine business
# smoke / VIN preserve-rollback are run by CCU-BUILD and are not replaced here.
#
# Usage: ci/run-ci-gates.sh [--sdk-prefix <dir>] [--build-dir <dir>]
# Exit: 0 = all pass, 1 = any gate failed.

set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SDK_PREFIX="${CCU_FRAMEWORK_PREFIX:-$HOME/.local}"
BUILD_DIR="${ROOT}/build-ci"
PROD_BUILD_DIR="${ROOT}/build-ci-prod"

while [[ $# -gt 0 ]]; do
    case $1 in
        --sdk-prefix) SDK_PREFIX="$2"; shift 2 ;;
        --build-dir)  BUILD_DIR="$2";  shift 2 ;;
        --help) echo "Usage: $0 [--sdk-prefix <dir>] [--build-dir <dir>]"; exit 0 ;;
        *) echo "Unknown option: $1" >&2; exit 1 ;;
    esac
done

RED='\033[0;31m'; GREEN='\033[0;32m'; BLUE='\033[0;34m'; NC='\033[0m'
gate() { echo -e "\n${BLUE}=== GATE $1: $2 ===${NC}"; }
pass() { echo -e "${GREEN}[PASS]${NC} $1"; }
fail() { echo -e "${RED}[FAIL]${NC} $1"; }

FAILURES=0
run() {
    local name="$1"; shift
    if "$@"; then pass "$name"; else fail "$name"; FAILURES=$((FAILURES+1)); fi
}

schema_ok() {
    local f="$1"
    if python3 "${ROOT}/ci/verify-config-schema.py" "$f" >/dev/null 2>&1; then
        return 0
    fi
    return 1
}

# 1. CMake lint
gate 1 "CMake build-contract lint"
run "cmake-lint" bash "${ROOT}/ci/verify-cmake-lint.sh"

# 2. systemd unit
gate 2 "systemd baseline unit contract"
run "systemd-unit" bash "${ROOT}/ci/verify-systemd-unit.sh"

# 3. unified config schema
gate 3 "unified diag.yaml schema + checks"
run "schema-default" python3 "${ROOT}/ci/verify-config-schema.py" \
    "${ROOT}/config/diag.default.yaml"
run "schema-dev" python3 "${ROOT}/ci/verify-config-schema.py" \
    "${ROOT}/config/dev/diag.yaml"
NEG_FAILURES=0
for f in "${ROOT}"/tests/config/invalid_*.yaml; do
    if schema_ok "$f"; then
        fail "negative schema case unexpectedly passed: $(basename "$f")"
        NEG_FAILURES=$((NEG_FAILURES+1))
    else
        pass "negative schema rejected: $(basename "$f")"
    fi
done
if [ "$NEG_FAILURES" -ne 0 ]; then
    FAILURES=$((FAILURES+1))
fi

# 4. HOST configure + build + tests (NON_PRODUCTION mock backend)
gate 4 "configure + build + unit/contract tests"
rm -rf "$BUILD_DIR"
if cmake -S "$ROOT" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Debug \
        -DCMAKE_INSTALL_PREFIX=/usr \
        -DBUILD_TESTING=ON \
        -DUSE_MOCK_SOMEIP=ON \
        -DCMAKE_PREFIX_PATH="$SDK_PREFIX" \
        -DCCUFramework_DIR="$SDK_PREFIX/lib/cmake/CCUFramework" >/dev/null 2>&1 \
   && cmake --build "$BUILD_DIR" -j >/dev/null 2>&1 \
   && ctest --test-dir "$BUILD_DIR" --output-on-failure >/dev/null 2>&1; then
    pass "build+tests"
else
    fail "build+tests"; FAILURES=$((FAILURES+1))
fi

# 5. installed Framework SDK consumer
gate 5 "installed Framework SDK consumer"
run "installed-framework-consumer" cmake \
    -DSDK_PREFIX="$SDK_PREFIX" -P "${ROOT}/ci/verify-installed-framework.cmake"

# 6. production configure must not require GTest / build test targets
gate 6 "production configure (BUILD_TESTING=OFF)"
rm -rf "$PROD_BUILD_DIR"
PROD_OK=1
if ! cmake -S "$ROOT" -B "$PROD_BUILD_DIR" -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=/usr \
        -DBUILD_TESTING=OFF \
        -DUSE_MOCK_SOMEIP=ON \
        -DCMAKE_PREFIX_PATH="$SDK_PREFIX" \
        -DCCUFramework_DIR="$SDK_PREFIX/lib/cmake/CCUFramework" >/dev/null 2>&1; then
    PROD_OK=0
fi
if [ "$PROD_OK" -eq 1 ] && cmake --build "$PROD_BUILD_DIR" --target ccu_diag -j >/dev/null 2>&1; then
    pass "production configure+build without tests"
else
    fail "production configure+build"; PROD_OK=0; FAILURES=$((FAILURES+1))
fi
if [ "$PROD_OK" -eq 1 ]; then
    if grep -qiE 'GTEST|GTest' "$PROD_BUILD_DIR/CMakeCache.txt"; then
        fail "production configure referenced GTest"; FAILURES=$((FAILURES+1))
    else
        pass "production configure did not look up GTest"
    fi
    if cmake --build "$PROD_BUILD_DIR" --target CcuDiagTests >/dev/null 2>&1; then
        fail "test target CcuDiagTests exists with BUILD_TESTING=OFF"; FAILURES=$((FAILURES+1))
    else
        pass "test targets absent with BUILD_TESTING=OFF"
    fi
fi

# 7. DESTDIR install manifest + repeat-install digest
gate 7 "DESTDIR install manifest + repeat-install consistency"
ROOT1="$(mktemp -d -t diag-install1.XXXXXX)"
ROOT2="$(mktemp -d -t diag-install2.XXXXXX)"
if env DESTDIR="$ROOT1" cmake --install "$BUILD_DIR" --prefix /usr \
        --component ccu-diag-runtime >/dev/null 2>&1; then
    run "install-manifest" python3 "${ROOT}/ci/verify-install-manifest.py" "$ROOT1"
else
    fail "DESTDIR install"; FAILURES=$((FAILURES+1))
fi
if env DESTDIR="$ROOT2" cmake --install "$BUILD_DIR" --prefix /usr \
        --component ccu-diag-runtime >/dev/null 2>&1; then
    run "install-manifest-repeat" python3 "${ROOT}/ci/verify-install-manifest.py" "$ROOT2"
    if python3 - "$ROOT1" "$ROOT2" <<'PY'
import hashlib, os, stat, sys

def manifest(root):
    out = {}
    for dirpath, _dirs, files in os.walk(root):
        for name in files:
            full = os.path.join(dirpath, name)
            rel = os.path.relpath(full, root)
            with open(full, "rb") as fh:
                digest = hashlib.sha256(fh.read()).hexdigest()
            mode = stat.S_IMODE(os.stat(full).st_mode)
            out[rel] = (digest, mode)
    return out

if manifest(sys.argv[1]) != manifest(sys.argv[2]):
    sys.exit(1)
PY
    then
        pass "clean/incremental install set, mode and digest consistent"
    else
        fail "repeat install differs (set/mode/digest)"; FAILURES=$((FAILURES+1))
    fi
else
    fail "DESTDIR install (repeat)"; FAILURES=$((FAILURES+1))
fi
rm -rf "$ROOT1" "$ROOT2"

# 8. daemon lifecycle smoke
gate 8 "daemon lifecycle smoke"
run "smoke-diag" bash "${ROOT}/tests/smoke/smoke-diag.sh" --build-dir "$BUILD_DIR"

echo -e "\n========================================"
if [ "$FAILURES" -eq 0 ]; then
    echo -e "${GREEN}ALL CCU-DIAG-DSN-CR-009 HOST CI GATES PASSED${NC}"
    exit 0
else
    echo -e "${RED}${FAILURES} GATE(S) FAILED${NC}"
    exit 1
fi
