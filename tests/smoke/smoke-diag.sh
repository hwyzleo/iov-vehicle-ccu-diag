#!/bin/bash
# tests/smoke/smoke-diag.sh - CCU-DIAG-DSN-CR-009 §8.2/§9 smoke entry
#
# Canonical smoke test referenced by the CCU-BUILD service manifest
# (smoke_test: tests/smoke/smoke-diag.sh).
#
# HOST scope (this script):
#   * daemon starts from a temp config root (no source-tree WorkingDirectory)
#   * unified diag.yaml + BUILD-style common.yaml load successfully
#   * mock SOME/IP provider initializes and reaches the main loop
#   * SIGTERM triggers bounded clean shutdown within the unit TimeoutStopSec
#
# TARGET scope (run by CCU-BUILD): daemon-reload, start/stop/restart,
# SOME/IP offer against real backend, DIAG->SEC, VIN preserve/rollback and
# business smoke. That acceptance is NOT replaced by this HOST script.
#
# Usage: tests/smoke/smoke-diag.sh [--build-dir <dir>] [--show-logs]
# Exit: 0 = pass, 1 = fail

set -e

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="${DIAG_BUILD_DIR:-${PROJECT_ROOT}/build}"

DIAG_LOG=""
SMOKE_TMP=""
DIAG_PID=""

# packaging/systemd/ccu-diag.service TimeoutStopSec=30s; allow a small margin.
STOP_BUDGET_S=35
READY_WAIT_S=4

RED='\033[0;31m'; GREEN='\033[0;32m'; BLUE='\033[0;34m'; NC='\033[0m'
info()    { echo -e "${BLUE}[INFO]${NC} $1"; }
success() { echo -e "${GREEN}[OK]${NC} $1"; }
fail()    { echo -e "${RED}[FAIL]${NC} $1"; }

cleanup() {
    if [ -n "$DIAG_PID" ] && kill -0 "$DIAG_PID" 2>/dev/null; then
        kill "$DIAG_PID" 2>/dev/null || true
        wait "$DIAG_PID" 2>/dev/null || true
    fi
    [ -n "$SMOKE_TMP" ] && rm -rf "$SMOKE_TMP"
}
trap cleanup EXIT INT TERM

SHOW_LOGS=false
while [[ $# -gt 0 ]]; do
    case $1 in
        --show-logs) SHOW_LOGS=true; shift ;;
        --build-dir) BUILD_DIR="$2"; shift 2 ;;
        --help) echo "Usage: $0 [--build-dir <dir>] [--show-logs]"; exit 0 ;;
        *) fail "Unknown option: $1"; exit 1 ;;
    esac
done

DIAG_BIN="${BUILD_DIR}/ccu_diag"
if [[ "$BUILD_DIR" != /* ]]; then
    BUILD_DIR="${PROJECT_ROOT}/${BUILD_DIR}"
    DIAG_BIN="${BUILD_DIR}/ccu_diag"
fi
if [ ! -x "$DIAG_BIN" ]; then
    fail "ccu_diag executable not found at ${DIAG_BIN}. Build first."
    exit 1
fi

# --- temp config root (BUILD-style layout: common.yaml + conf.d/diag.yaml) ---
SMOKE_TMP="$(mktemp -d -t diag-smoke.XXXXXX)"
mkdir -p "${SMOKE_TMP}/conf.d"
cp "${PROJECT_ROOT}/config/dev/common.yaml" "${SMOKE_TMP}/common.yaml"
cp "${PROJECT_ROOT}/config/dev/diag.yaml" "${SMOKE_TMP}/conf.d/diag.yaml"
DIAG_LOG="${SMOKE_TMP}/daemon.log"

# Pick an ephemeral port so parallel/local runs do not collide on 51110.
PORT="$(python3 - <<'PY'
import socket
s = socket.socket()
s.bind(("127.0.0.1", 0))
print(s.getsockname()[1])
s.close()
PY
)"

# --- start from the temp config root, not the source tree ---
info "Starting ccu_diag with temp config root (no source-tree cwd)..."
( cd "$SMOKE_TMP" && exec "$DIAG_BIN" \
    --diag-config "${SMOKE_TMP}/conf.d/diag.yaml" \
    --common-config "${SMOKE_TMP}/common.yaml" \
    --someip-ip 127.0.0.1 --someip-port "$PORT" \
    > "$DIAG_LOG" 2>&1 ) &
DIAG_PID=$!

# --- readiness: any config/SOME-IP/logger init failure exits immediately ---
info "Waiting ${READY_WAIT_S}s for daemon init..."
sleep "$READY_WAIT_S"
if ! kill -0 "$DIAG_PID" 2>/dev/null; then
    fail "ccu_diag exited during init (config/logger/SOME-IP failure)"
    [ -s "$DIAG_LOG" ] && { echo "=== diag log ==="; cat "$DIAG_LOG"; }
    exit 1
fi
success "ccu_diag running (unified config + logger + SOME/IP init passed)"

# Prove the mock provider is actually listening (business offer evidence).
if python3 - "$PORT" <<'PROBE'
import socket, sys, time
port = int(sys.argv[1])
deadline = time.time() + 5
while time.time() < deadline:
    s = socket.socket()
    s.settimeout(1)
    try:
        s.connect(("127.0.0.1", port))
        s.close()
        sys.exit(0)
    except OSError:
        s.close()
        time.sleep(0.2)
sys.exit(1)
PROBE
then
    success "SOME/IP provider listening on 127.0.0.1:${PORT}"
else
    fail "SOME/IP provider not reachable on 127.0.0.1:${PORT}"
    exit 1
fi

# --- SIGTERM -> bounded clean shutdown (unit TimeoutStopSec budget) ---
info "Sending SIGTERM, expecting clean shutdown within ${STOP_BUDGET_S}s..."
t0=$(date +%s)
kill -TERM "$DIAG_PID" 2>/dev/null
deadline=$((t0 + STOP_BUDGET_S))
while kill -0 "$DIAG_PID" 2>/dev/null; do
    now=$(date +%s)
    if [ "$now" -ge "$deadline" ]; then
        fail "ccu_diag did not stop within ${STOP_BUDGET_S}s (TimeoutStopSec budget)"
        kill -KILL "$DIAG_PID" 2>/dev/null || true
        wait "$DIAG_PID" 2>/dev/null || true
        [ -s "$DIAG_LOG" ] && { echo "=== diag log ==="; cat "$DIAG_LOG"; }
        exit 1
    fi
    sleep 1
done
t1=$(date +%s)
success "ccu_diag stopped cleanly in $((t1 - t0))s (within TimeoutStopSec budget)"

# The async logger flushes on shutdown; verify business offer + clean stop.
if [ -s "$DIAG_LOG" ] && grep -q "SOME/IP service offered" "$DIAG_LOG"; then
    success "SOME/IP provider offer recorded (method handlers registered)"
else
    fail "SOME/IP provider offer evidence missing in log"
    [ -s "$DIAG_LOG" ] && { echo "=== diag log ==="; cat "$DIAG_LOG"; }
    exit 1
fi
if [ -s "$DIAG_LOG" ] && grep -q "CCU-DIAG Service stopped" "$DIAG_LOG"; then
    success "clean shutdown recorded"
else
    fail "clean shutdown evidence missing in log"
    [ -s "$DIAG_LOG" ] && { echo "=== diag log ==="; cat "$DIAG_LOG"; }
    exit 1
fi

if [ "$SHOW_LOGS" = true ] && [ -s "$DIAG_LOG" ]; then
    echo "=== diag log ==="; cat "$DIAG_LOG"
fi

success "smoke-diag PASSED"
exit 0
