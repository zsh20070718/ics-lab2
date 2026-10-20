#!/usr/bin/env python3
"""Deterministic direct-challenge regression harness for the personal BombLab.

This script is evidence tooling kept outside the course-shaped package. It runs
the frozen solver executable and bomb serially, records every generated case and
every subprocess result, and refuses to start if any frozen input hash drifts.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import platform
import sys
from pathlib import Path


SCHEMA = "bomblab-direct-challenge-suite-v1"
EXPECTED = {
    "defuser_source_sha256": "b7684f22ad44745bac27faad1036e3bc290f167f29f3b147909ca50045d65e29",
    "solver_executable_sha256": "d50fc10015707e060dbf8ad2656b53621e73a66acf3f92e2f760b775c0a6142d",
    "bomb_sha256": "ecbbf2c8f382cd638e762e4494f41bff160ad80c87ea0c7da720914fdccf47b7",
}


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def case(case_id: str, phase: int, challenge: int, coverage: dict[str, object]) -> dict[str, object]:
    return {
        "id": case_id,
        "phase": phase,
        "challenge_decimal": str(challenge),
        "challenge_hex": f"0x{challenge:016x}",
        "coverage": coverage,
    }


def generate_cases() -> list[dict[str, object]]:
    cases: list[dict[str, object]] = []

    phase1_boundaries = [
        ("zero", 0),
        ("all-ones", (1 << 64) - 1),
        ("lo-max", (1 << 32) - 1),
        ("hi-max", ((1 << 32) - 1) << 32),
        ("both-sign-bits", (1 << 63) | (1 << 31)),
    ]
    for name, challenge in phase1_boundaries:
        cases.append(case(f"p1-boundary-{name}", 1, challenge, {"boundary": name}))

    phase2_boundaries = [
        ("zero", 0),
        ("all-ones", (1 << 64) - 1),
        ("initial-window-max", 0x1F << 35),
        ("recurrence-window-max", 0xFFFF << 11),
        ("high-bit-only", 1 << 63),
    ]
    for name, challenge in phase2_boundaries:
        cases.append(case(f"p2-boundary-{name}", 2, challenge, {"boundary": name}))

    for selector in range(8):
        cases.append(case(
            f"p3-selector-{selector}", 3, selector,
            {"selector": selector},
        ))

    for target in range(128):
        cases.append(case(
            f"p4-target-{target:03d}", 4, target,
            {"target": target},
        ))

    for head in range(8):
        for steps in range(4):
            challenge = head | (steps << 3)
            cases.append(case(
                f"p5-head-{head}-steps-{steps}", 5, challenge,
                {"head_selector": head, "steps": steps},
            ))

    for first in range(8):
        for second in range(8):
            challenge = first | (second << 3)
            cases.append(case(
                f"p6-first-{first}-second-{second}", 6, challenge,
                {"first_table_index": first, "second_table_index": second},
            ))

    if len(cases) != 242:
        raise AssertionError(f"generator produced {len(cases)} cases, expected 242")
    if len({str(item["id"]) for item in cases}) != len(cases):
        raise AssertionError("case IDs must be unique")
    return cases


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", required=True, type=Path)
    parser.add_argument("--solver", required=True, type=Path)
    parser.add_argument("--bomb", required=True, type=Path)
    parser.add_argument("--manifest", required=True, type=Path)
    parser.add_argument("--results", required=True, type=Path)
    parser.add_argument("--summary", required=True, type=Path)
    args = parser.parse_args()

    invocation_cwd = Path.cwd().resolve()
    root = args.root.resolve()
    solver = args.solver.resolve()
    bomb = args.bomb.resolve()
    source = (root / "defuser.c").resolve()
    manifest_path = args.manifest.resolve()
    results_path = args.results.resolve()
    summary_path = args.summary.resolve()

    for path in (root, solver, bomb, source, manifest_path.parent, results_path.parent, summary_path.parent):
        if not path.exists():
            raise SystemExit(f"missing required path: {path}")

    actual_hashes = {
        "defuser_source_sha256": sha256(source),
        "solver_executable_sha256": sha256(solver),
        "bomb_sha256": sha256(bomb),
    }
    if actual_hashes != EXPECTED:
        raise SystemExit(
            "frozen input hash mismatch:\n"
            + json.dumps({"expected": EXPECTED, "actual": actual_hashes}, indent=2)
        )

    sys.path.insert(0, str(root))
    from runner import answer_line, run_limited  # noqa: PLC0415

    cases = generate_cases()
    manifest = {
        "schema": SCHEMA,
        "generator": "evidence/run_branch_suite.py",
        "generator_sha256": sha256(Path(__file__).resolve()),
        "invocation_cwd": str(invocation_cwd),
        "argv": sys.argv,
        "hashes": actual_hashes,
        "coverage_counts": {
            "phase1_boundaries": 5,
            "phase2_boundaries": 5,
            "phase3_selectors": 8,
            "phase4_targets": 128,
            "phase5_head_step_pairs": 32,
            "phase6_table_index_pairs": 64,
            "total": 242,
        },
        "cases": cases,
    }
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")

    results: list[dict[str, object]] = []
    passed = 0
    for item in cases:
        phase = int(item["phase"])
        challenge = str(item["challenge_decimal"])
        flags = ["--phase", str(phase), "--challenge", challenge]
        solver_result = run_limited([str(solver), *flags], cwd=root, timeout=3)
        answer, answer_error = answer_line(solver_result)
        bomb_result = None
        if answer is not None:
            bomb_result = run_limited([str(bomb), *flags], cwd=root, data=answer, timeout=3)
        expected_line = f"Phase {phase} defused."
        bomb_stdout = "" if bomb_result is None else bomb_result.stdout.decode("utf-8", "replace")
        ok = (
            answer_error is None
            and bomb_result is not None
            and not bomb_result.timed_out
            and not bomb_result.truncated
            and bomb_result.returncode == 0
            and expected_line in bomb_stdout.splitlines()
        )
        if ok:
            passed += 1
        results.append({
            **item,
            "answer_error": answer_error,
            "solver": {
                "returncode": solver_result.returncode,
                "timed_out": solver_result.timed_out,
                "truncated": solver_result.truncated,
                "stdout": solver_result.stdout.decode("utf-8", "replace"),
                "stderr": solver_result.stderr.decode("utf-8", "replace"),
            },
            "bomb": None if bomb_result is None else {
                "returncode": bomb_result.returncode,
                "timed_out": bomb_result.timed_out,
                "truncated": bomb_result.truncated,
                "stdout": bomb_stdout,
                "stderr": bomb_result.stderr.decode("utf-8", "replace"),
            },
            "passed": ok,
        })

    os_release: dict[str, str] = {}
    os_release_path = Path("/etc/os-release")
    if os_release_path.is_file():
        for line in os_release_path.read_text(encoding="utf-8").splitlines():
            if "=" in line:
                key, value = line.split("=", 1)
                os_release[key] = value.strip().strip('"')
    environment = {
        "python": sys.version,
        "python_executable": sys.executable,
        "platform": platform.platform(),
        "uname": list(platform.uname()),
        "libc": list(platform.libc_ver()),
        "os_release": os_release,
        "os_name": os.name,
        "cwd": str(invocation_cwd),
        "lc_all": os.environ.get("LC_ALL"),
        "python_dont_write_bytecode": os.environ.get("PYTHONDONTWRITEBYTECODE"),
    }
    results_document = {
        "schema": SCHEMA,
        "manifest_sha256": sha256(manifest_path),
        "hashes": actual_hashes,
        "environment": environment,
        "passed": passed,
        "total": len(cases),
        "results": results,
    }
    results_path.write_text(json.dumps(results_document, indent=2) + "\n", encoding="utf-8")
    summary = (
        f"schema: {SCHEMA}\n"
        f"generator_sha256: {manifest['generator_sha256']}\n"
        f"manifest_sha256: {results_document['manifest_sha256']}\n"
        f"defuser_source_sha256: {actual_hashes['defuser_source_sha256']}\n"
        f"solver_executable_sha256: {actual_hashes['solver_executable_sha256']}\n"
        f"bomb_sha256: {actual_hashes['bomb_sha256']}\n"
        f"python: {platform.python_version()}\n"
        f"platform: {environment['platform']}\n"
        f"direct-challenge branch suite: {passed}/{len(cases)} "
        f"{'PASS' if passed == len(cases) else 'FAIL'}\n"
    )
    summary_path.write_text(summary, encoding="utf-8")
    sys.stdout.write(summary)
    return 0 if passed == len(cases) else 1


if __name__ == "__main__":
    raise SystemExit(main())
