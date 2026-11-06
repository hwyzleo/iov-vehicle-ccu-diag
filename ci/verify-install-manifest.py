#!/usr/bin/env python3
# ci/verify-install-manifest.py - CCU-DIAG-DSN-CR-009 §3.2/§9
#
# Verify ccu-diag-runtime produces exactly the declared rootfs file set under a
# DESTDIR install-root, with no out-of-bounds / stale / dev / secret content.
#
# Usage:
#   verify-install-manifest.py <install-root>
#
# Exit: 0 = pass, 1 = manifest mismatch.

import argparse
import os
import sys

EXPECTED = {
    "usr/bin/ccu_diag",
    "usr/lib/systemd/system/ccu-diag.service",
    "etc/ccu/conf.d/diag.yaml",
    "usr/share/ccu-diag/schema/diag.schema.yaml",
}

FORBIDDEN_PATTERNS = (
    "CcuDiagTests",
    "test_ccu_sec_client_proxy",
    "libccu_diag_lib",
    "compile_commands.json",
    "diag.default.yaml",   # installed renamed to diag.yaml
    "common.yaml",         # BUILD-owned, not shipped by the service
    "diag_config.yaml",    # legacy dev fixture
    "did_registry.yaml",   # legacy dev fixture
    "someip_config.yaml",  # legacy dev fixture
    "sec_config.yaml",     # legacy dev fixture
    "vin.dat",             # device instance state, preserve-only
    "10-vsomeip.conf",     # platform drop-in, not a service asset
    ".o",
    ".a",
    ".h",
    ".hpp",
)


def collect_files(root):
    found = set()
    for dirpath, _dirs, files in os.walk(root):
        for name in files:
            full = os.path.join(dirpath, name)
            found.add(os.path.relpath(full, root))
    return found


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("install_root", help="DESTDIR install-root to verify")
    args = ap.parse_args()

    root = args.install_root
    if not os.path.isdir(root):
        print(f"[FAIL] install-root not found: {root}", file=sys.stderr)
        return 1

    found = collect_files(root)
    missing = EXPECTED - found
    extra = found - EXPECTED

    forbidden = sorted(
        rel for rel in found
        if any(os.path.basename(rel) == pat or rel.endswith(pat)
               for pat in FORBIDDEN_PATTERNS)
    )

    ok = True
    if missing:
        ok = False
        print("[FAIL] missing expected files:", file=sys.stderr)
        for m in sorted(missing):
            print(f"       - {m}", file=sys.stderr)

    if extra:
        ok = False
        print("[FAIL] unexpected extra files (out-of-bounds / stale):",
              file=sys.stderr)
        for e in sorted(extra):
            print(f"       + {e}", file=sys.stderr)

    if forbidden:
        ok = False
        print("[FAIL] forbidden content in runtime component:", file=sys.stderr)
        for f in forbidden:
            print(f"       ! {f}", file=sys.stderr)

    if ok:
        print(f"[OK] ccu-diag-runtime install manifest exact: "
              f"{len(found)} file(s) under {root}")
        for rel in sorted(EXPECTED):
            print(f"     - {rel}")
        return 0
    return 1


if __name__ == "__main__":
    sys.exit(main())
