#!/bin/bash
# ci/verify-systemd-unit.sh - CCU-DIAG-DSN-CR-009 §6.1
#
# Verify packaging/systemd/ccu-diag.service against the baseline unit contract:
# ExecStart, SEC ordering without Requires, restart policy, bounded stop timeout,
# sandbox hardening, writable state path and no platform/root defaults.
#
# Exit: 0 = pass, 1 = violation.

set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
UNIT="${ROOT}/packaging/systemd/ccu-diag.service"
STATUS=0

violation() { echo "[FAIL] $1" >&2; STATUS=1; }
ok()        { echo "[OK]   $1"; }

[ -f "$UNIT" ] || { echo "[FAIL] unit not found: $UNIT" >&2; exit 1; }

require_value() {
    grep -qE "^$1=$2" "$UNIT" && ok "$1=$2" || violation "$1 != $2"
}

require_value "ExecStart" "/usr/bin/ccu_diag"
require_value "Type" "simple"
require_value "Restart" "on-failure"
require_value "NoNewPrivileges" "yes"
require_value "PrivateTmp" "yes"
require_value "ProtectSystem" "strict"
require_value "ProtectHome" "yes"
require_value "ReadWritePaths" "/var/lib/ccu/diag"
require_value "WantedBy" "multi-user.target"

grep -qE '^RestartSec=' "$UNIT" && ok "RestartSec set" || violation "missing RestartSec"

# TimeoutStopSec must be >= the measured bounded shutdown budget. The measured
# HOST budget is well below 30s; require at least a 10s floor here and the
# smoke test proves the real exit stays inside the configured value.
STOP=$(grep -E '^TimeoutStopSec=' "$UNIT" | head -1 | cut -d= -f2 | tr -dc '0-9')
if [ -n "${STOP:-}" ] && [ "$STOP" -ge 10 ]; then
    ok "TimeoutStopSec=${STOP}s (>= 10s floor)"
else
    violation "TimeoutStopSec missing or < 10s"
fi

# Ordering: after network and ccu-sec; wants ccu-sec. Never Requires=.
grep -qE '^After=.*network\.target' "$UNIT" \
    && ok "After network.target" || violation "missing After=network.target"
grep -qE '^After=.*ccu-sec\.service' "$UNIT" \
    && ok "After ccu-sec.service" || violation "missing After=ccu-sec.service"
grep -qE '^Wants=.*ccu-sec\.service' "$UNIT" \
    && ok "Wants ccu-sec.service" || violation "missing Wants=ccu-sec.service"
if grep -qE '^Requires=' "$UNIT"; then
    violation "baseline unit must not use Requires= (SEC unavailability stays degraded, not hard-fail)"
fi

# Baseline must NOT default to root; User/Group come from the platform drop-in.
if grep -qE '^User=root|^Group=root' "$UNIT"; then
    violation "baseline unit defaults to User/Group=root (must come from platform drop-in)"
fi

# /etc/ccu and source trees must never be writable; vsomeip env is a platform
# asset and must not be hard-coded in the baseline unit.
if grep -qE 'ReadWritePaths=.*(/etc/ccu|/home/|/Users/|/build)' "$UNIT"; then
    violation "unit grants write access to /etc/ccu, home or source/build path"
fi
if grep -qE '^Environment=VSOMEIP_|vsomeip\.json' "$UNIT"; then
    violation "vsomeip platform environment hard-coded in baseline unit"
fi
if grep -qE '/home/|/Users/|/build|password|secret|token|private_key' "$UNIT"; then
    violation "unit contains personal path / secret"
fi

if command -v systemd-analyze >/dev/null 2>&1; then
    if systemd-analyze verify "$UNIT" 2>/dev/null; then
        ok "systemd-analyze verify"
    else
        violation "systemd-analyze verify failed"
    fi
else
    ok "systemd-analyze not available (static checks only)"
fi

[ "$STATUS" -eq 0 ] && echo "systemd-unit: PASS" || echo "systemd-unit: FAIL"
exit $STATUS
