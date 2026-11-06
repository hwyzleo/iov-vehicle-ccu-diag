#!/usr/bin/env python3
# ci/verify-config-schema.py - CCU-DIAG-DSN-CR-009 §5.2/§9
#
# Side-effect-free checker for the complete unified diag.yaml. Runs the
# versioned JSON Schema (Draft-07) and adds cross-field / secret checks that
# JSON Schema cannot express:
#   * no top-level common:
#   * no secret/provisioning fields or real VIN values
#   * provider and SEC client share exactly one process application name
#   * distinct Service IDs; no endpoint collision when both networks are set
#   * method IDs unique in 0x0001-0x7FFF, event IDs unique in 0x8000-0xFFFF
#
# No network, no device access, no secret reads, no writes.
# Usage: verify-config-schema.py [--schema PATH] <diag.yaml> [...]
# Exit: 0 = pass, 1 = validation failure.

import argparse
import ipaddress
import re
import signal
import sys
from pathlib import Path

import yaml
import jsonschema


VIN_RE = re.compile(r"^[A-HJ-NPR-Z0-9]{17}$")
FORBIDDEN_KEYS = {
    "vin",
    "device_sn",
    "device_id",
    "device-id",
    "password",
    "secret",
    "token",
    "private_key",
    "private-key",
    "cert",
    "certificate",
    "session_key",
    "seed",
    "credentials",
    "provisioning",
}


def walk(node, path, errors):
    if isinstance(node, dict):
        for key, value in node.items():
            key_s = str(key)
            if key_s.lower() in FORBIDDEN_KEYS:
                errors.append(f"{path}.{key_s}: forbidden secret/provisioning key")
            walk(value, f"{path}.{key_s}", errors)
    elif isinstance(node, list):
        for idx, value in enumerate(node):
            walk(value, f"{path}[{idx}]", errors)
    elif isinstance(node, str):
        if VIN_RE.match(node):
            errors.append(f"{path}: value looks like a real VIN")


def cross_field_checks(data, errors):
    diag = data.get("diag")
    if not isinstance(diag, dict):
        return
    someip = diag.get("someip", {})
    sec = diag.get("sec_client", {}).get("someip", {})

    app = someip.get("application_name")
    sec_app = sec.get("application_name")
    if app != sec_app:
        errors.append(
            f"application_name mismatch: diag.someip={app!r} vs "
            f"diag.sec_client.someip={sec_app!r} (one process must use one application)")

    provider_service = someip.get("service", {}).get("service_id")
    sec_service = sec.get("service", {}).get("service_id")
    if provider_service == sec_service:
        errors.append(
            f"provider and SEC client use the same Service ID {provider_service}")

    provider_net = someip.get("network")
    sec_net = sec.get("network")
    for label, net in (("diag.someip.network", provider_net),
                       ("diag.sec_client.someip.network", sec_net)):
        if isinstance(net, dict):
            ip = net.get("ip_address")
            if not isinstance(ip, str) or not _is_ip(ip):
                errors.append(f"{label}.ip_address: invalid IP address {ip!r}")
    if isinstance(provider_net, dict) and isinstance(sec_net, dict):
        if (provider_net.get("ip_address") == sec_net.get("ip_address")
                and provider_net.get("port") == sec_net.get("port")):
            errors.append(
                "SOME/IP endpoint collision: provider and SEC client use the same ip:port")

    methods = someip.get("methods", {})
    seen_methods = set()
    if isinstance(methods, dict):
        for name, entry in methods.items():
            mid = entry.get("method_id") if isinstance(entry, dict) else None
            if mid in seen_methods:
                errors.append(f"duplicate method_id {mid} at diag.someip.methods.{name}")
            elif isinstance(mid, int):
                seen_methods.add(mid)

    events = someip.get("events", {})
    seen_events = set()
    if isinstance(events, dict):
        for name, entry in events.items():
            eid = entry.get("event_id") if isinstance(entry, dict) else None
            if eid in seen_events:
                errors.append(f"duplicate event_id {eid} at diag.someip.events.{name}")
            elif isinstance(eid, int):
                seen_events.add(eid)


def _is_ip(value):
    try:
        ipaddress.ip_address(value)
        return True
    except ValueError:
        return False


class CheckerTimeout(Exception):
    pass


def _on_alarm(_signum, _frame):
    raise CheckerTimeout()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--schema", default=None)
    ap.add_argument("--timeout", type=int, default=30,
                    help="hard wall-clock timeout in seconds (default 30)")
    ap.add_argument("files", nargs="+")
    args = ap.parse_args()

    if args.timeout > 0:
        signal.signal(signal.SIGALRM, _on_alarm)
        signal.alarm(args.timeout)

    repo = Path(__file__).resolve().parent.parent
    schema_path = Path(args.schema) if args.schema else repo / "config" / "schema" / "diag.schema.yaml"

    try:
        schema = yaml.safe_load(schema_path.read_text())
    except (OSError, yaml.YAMLError) as exc:
        print(f"[FAIL] cannot load schema {schema_path}: {exc}", file=sys.stderr)
        return 1

    validator = jsonschema.Draft7Validator(schema)
    status = 0

    for file_name in args.files:
        path = Path(file_name)
        errors = []
        try:
            data = yaml.safe_load(path.read_text())
        except (OSError, yaml.YAMLError) as exc:
            print(f"[FAIL] {path}: cannot read/parse YAML: {exc}", file=sys.stderr)
            status = 1
            continue

        if isinstance(data, dict) and "common" in data:
            errors.append("top-level 'common:' is forbidden in diag.yaml")

        for err in sorted(validator.iter_errors(data), key=lambda e: list(e.absolute_path)):
            loc = ".".join(str(p) for p in err.absolute_path) or "(root)"
            errors.append(f"schema {loc}: {err.message}")

        walk(data, "root", errors)
        if isinstance(data, dict):
            cross_field_checks(data, errors)

        if errors:
            status = 1
            print(f"[FAIL] {path}", file=sys.stderr)
            for err in errors:
                print(f"       - {err}", file=sys.stderr)
        else:
            print(f"[OK]   {path}")

    return status


if __name__ == "__main__":
    try:
        sys.exit(main())
    except CheckerTimeout:
        print("[FAIL] config checker timed out", file=sys.stderr)
        sys.exit(1)
