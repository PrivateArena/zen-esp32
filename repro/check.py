#!/usr/bin/env python3
"""Assertion helper for repro/run.sh - one case per invocation.

Runs the skill's own CLI (validate_pinmap.py / generate_config.py) on a repro
input and asserts the documented expectation. Exit code 0 = case passes, so
run.sh can aggregate a real pass/fail instead of a human reading a dump.

Usage:
    check.py validate-invalid <label> <input.json> <error_code> <skill_dir>
    check.py validate-warn    <label> <input.json> <warning_code> <skill_dir>
    check.py validate-gpio-clean <label> <input.json> <gpio> <skill_dir>  (no finding at all on that GPIO)
    check.py code-has         <label> <input.json> <substring> <skill_dir>
    check.py code-lacks       <label> <input.json> <substring> <skill_dir>

All four CLI contracts are asserted through JSON: any non-JSON stdout is a
failure, because that is defect D3 as originally reported.
"""

import json
import subprocess
import sys

VALIDATE = "validate_pinmap.py"
GENERATE = "generate_config.py"


def run(script, args, input_path):
    with open(input_path) as handle:
        return subprocess.run(
            [sys.executable, script] + args,
            stdin=handle, capture_output=True, text=True,
        )


def parse_json(proc, label):
    text = proc.stdout.strip()
    try:
        return json.loads(text)
    except Exception:
        print(f"  FAIL {label}: non-JSON output (callers cannot parse): {text[:120]!r}")
        if proc.stderr.strip():
            print(f"       stderr: {proc.stderr.strip()[:160]}")
        raise SystemExit(1)


def main():
    kind, label, input_path, needle, skill = sys.argv[1:6]
    scripts = f"{skill}/scripts/"

    if kind.startswith("validate"):
        proc = run(scripts + VALIDATE, ["--format", "json"], input_path)
        data = parse_json(proc, label)
        errors = sorted({e["code"] for e in data.get("errors", [])})
        warnings = sorted({w["code"] for w in data.get("warnings", [])})
        detail = f"valid={data.get('valid')} errors={errors} warnings={warnings}"
        if kind == "validate-invalid":
            ok = not data.get("valid") and needle in errors
            expect = f"an error {needle}"
        elif kind == "validate-warn":
            ok = needle in warnings
            expect = f"a warning {needle}"
        elif kind == "validate-gpio-clean":
            hits = [f"{item['code']}:{item['gpio']}"
                    for item in data.get("errors", []) + data.get("warnings", [])
                    if str(item.get("gpio")) == needle]
            ok = not hits
            detail += f" findings on GPIO{needle}={hits}"
            expect = f"no finding at all on GPIO{needle}"
        else:
            print(f"unknown validate kind {kind}")
            raise SystemExit(2)
    else:
        proc = run(scripts + GENERATE, ["--format", "json", "--framework", "arduino"], input_path)
        data = parse_json(proc, label)
        code = data.get("init_code") or ""
        if kind == "code-has":
            ok = needle in code
            expect = f"generated code contains {needle!r}"
            detail = "\n       ".join(
                line.strip() for line in code.splitlines() if needle in line
            )[:300] or "<no match>"
        elif kind == "code-lacks":
            ok = needle not in code
            expect = f"generated code must not contain {needle!r}"
            detail = "clean"
        else:
            print(f"unknown kind {kind}")
            raise SystemExit(2)

    print(f"=== {label}\n  {detail}\n  {'PASS' if ok else 'FAIL'}: expected {expect}")
    raise SystemExit(0 if ok else 1)


if __name__ == "__main__":
    main()
