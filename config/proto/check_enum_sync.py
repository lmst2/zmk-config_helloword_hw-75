#!/usr/bin/env python3
"""Drift check: firmware C enums must stay numerically in lockstep with the
single-source protocol enums in usb_comm.proto.

AGENTS.md §4.2 documents these as a MANUAL sync table. The firmware casts between
the hand-written C enums and the nanopb-generated proto enums by raw integer
(e.g. handler_debug_log.c `(usb_comm_LogEventId)src->event_id`, handler_rgb.c
`(int)req->effect`), so any value drift silently corrupts the wire and compiles
clean on both ends. This script turns that invariant into a fast, toolchain-free
CI / pre-commit check.

Exit 0 if every mapped (name, value) pair matches; exit 1 with a diff otherwise.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PROTO = ROOT / "config" / "proto" / "usb_comm.proto"
DIAG_H = ROOT / "config" / "app" / "include" / "app" / "diag_log.h"
RGB_H = ROOT / "config" / "app" / "include" / "app" / "hw75_rgb_effects.h"

# (label, proto enum name, proto member prefix, C header path, C enum name, C member prefix)
PAIRS = [
    ("LogLevel", "LogLevel", "", DIAG_H, "hw75_diag_level", "HW75_DIAG_LEVEL_"),
    ("LogModule", "LogModule", "", DIAG_H, "hw75_diag_module", "HW75_DIAG_MODULE_"),
    ("LogEventId", "LogEventId", "LOG_EVENT_", DIAG_H, "hw75_diag_event_id", "HW75_DIAG_EVENT_"),
    ("RgbState.Effect", "Effect", "", RGB_H, "hw75_rgb_effect_id", "HW75_RGB_EFFECT_"),
]


def parse_enum(text, enum_name):
    """Return {member_name: int} for the first `enum <enum_name> { ... }` block.

    Handles both C (`NAME = N,`) and proto (`NAME = N;`) member syntax. Assumes
    flat enums (no nested braces), which holds for every enum checked here.
    """
    m = re.search(r"enum\s+" + re.escape(enum_name) + r"\s*\{(.*?)\}", text, re.S)
    if not m:
        sys.exit(f"error: enum '{enum_name}' not found")
    out = {}
    for name, val in re.findall(r"([A-Za-z_][A-Za-z0-9_]*)\s*=\s*(-?\d+)", m.group(1)):
        out[name] = int(val)
    return out


def strip_prefix(d, prefix, where):
    out = {}
    for name, val in d.items():
        if not name.startswith(prefix):
            sys.exit(f"error: {where}: member '{name}' lacks expected prefix '{prefix}'")
        out[name[len(prefix):]] = val
    return out


def main():
    proto_text = PROTO.read_text(encoding="utf-8")
    failures = []

    for label, proto_enum, proto_prefix, c_path, c_enum, c_prefix in PAIRS:
        proto_vals = strip_prefix(parse_enum(proto_text, proto_enum), proto_prefix, f"proto {proto_enum}")
        c_text = c_path.read_text(encoding="utf-8")
        c_vals = strip_prefix(parse_enum(c_text, c_enum), c_prefix, f"C {c_enum}")

        if proto_vals == c_vals:
            print(f"  OK  {label:<16} {len(proto_vals)} values match ({c_enum})")
            continue

        msgs = []
        for name in sorted(set(proto_vals) | set(c_vals)):
            p, c = proto_vals.get(name), c_vals.get(name)
            if p != c:
                msgs.append(f"      {name}: proto={p} {c_enum}={c}")
        failures.append(f"  FAIL {label}: {proto_enum} ({c_enum}) drifted:\n" + "\n".join(msgs))

    if failures:
        print("\nproto/firmware enum drift detected (see AGENTS.md §4.2):\n")
        print("\n".join(failures))
        return 1

    print("\nAll firmware enums are in sync with usb_comm.proto.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
