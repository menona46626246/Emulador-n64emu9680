#!/usr/bin/env python3
"""Run legal N64 ROMs headlessly and write a machine-readable report."""

from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import string
import subprocess
import sys
import time
from pathlib import Path
from typing import Any


REPO_ROOT = Path(__file__).resolve().parents[1]
ROM_SUFFIXES = {".z64", ".n64", ".v64"}


class ManifestError(ValueError):
    pass


def load_manifest(path: Path) -> dict[str, Any]:
    try:
        document = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise ManifestError(f"cannot read manifest {path}: {exc}") from exc
    if not isinstance(document, dict) or document.get("schema_version") != 1:
        raise ManifestError("manifest must be an object with schema_version=1")
    if not isinstance(document.get("cases"), list) or not document["cases"]:
        raise ManifestError("manifest cases must be a non-empty array")
    return document


def string_list(value: Any, field: str) -> list[str]:
    if value is None:
        return []
    if not isinstance(value, list) or any(not isinstance(item, str) for item in value):
        raise ManifestError(f"{field} must be an array of strings")
    return value


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def output_tail(value: str | bytes | None) -> str:
    if isinstance(value, bytes):
        value = value.decode("utf-8", errors="replace")
    return (value or "")[-4000:]


def resolve_from_repo(value: str) -> Path:
    path = Path(value)
    return path.resolve() if path.is_absolute() else (REPO_ROOT / path).resolve()


def run_case(emulator: Path, defaults: dict[str, Any], case: Any) -> dict[str, Any]:
    if not isinstance(case, dict):
        raise ManifestError("each case must be an object")
    name = case.get("name")
    rom_value = case.get("rom")
    if not isinstance(name, str) or not name.strip():
        raise ManifestError("each case needs a non-empty name")
    if not isinstance(rom_value, str) or not rom_value:
        raise ManifestError(f"case {name!r} needs a rom path")

    rom = resolve_from_repo(rom_value)
    max_runtime_ms = case.get("max_runtime_ms", defaults.get("max_runtime_ms", 500))
    expected_exit_code = case.get(
        "expected_exit_code", defaults.get("expected_exit_code", 0)
    )
    if not isinstance(max_runtime_ms, int) or max_runtime_ms <= 0:
        raise ManifestError(f"case {name!r}: max_runtime_ms must be positive")
    if not isinstance(expected_exit_code, int):
        raise ManifestError(f"case {name!r}: expected_exit_code must be an integer")
    required_output = string_list(
        case.get("required_output", defaults.get("required_output")),
        f"case {name!r}.required_output",
    )
    extra_args = string_list(defaults.get("args"), "defaults.args")
    extra_args += string_list(case.get("args"), f"case {name!r}.args")

    result: dict[str, Any] = {
        "name": name,
        "rom": rom_value,
        "status": "failed",
        "max_runtime_ms": max_runtime_ms,
    }
    if rom.suffix.lower() not in ROM_SUFFIXES:
        result["error"] = f"unsupported ROM extension: {rom.suffix}"
        return result
    if not rom.is_file():
        result["error"] = f"ROM not found: {rom}"
        return result

    actual_hash = sha256_file(rom)
    result["sha256"] = actual_hash
    expected_hash = case.get("sha256")
    if expected_hash is not None:
        if (
            not isinstance(expected_hash, str)
            or len(expected_hash) != 64
            or any(character not in string.hexdigits for character in expected_hash)
        ):
            raise ManifestError(f"case {name!r}.sha256 must contain 64 hex characters")
        if actual_hash.lower() != expected_hash.lower():
            result["error"] = "ROM sha256 does not match manifest"
            return result

    command = [
        str(emulator),
        "--headless",
        "--mute",
        "--no-vsync-emu",
        "--auto-close",
        str(max_runtime_ms),
        "--rom",
        str(rom),
        *extra_args,
    ]
    started = time.monotonic()
    try:
        completed = subprocess.run(
            command,
            cwd=REPO_ROOT,
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            timeout=max(15.0, max_runtime_ms / 1000.0 + 10.0),
            check=False,
        )
        combined_output = completed.stdout + completed.stderr
        missing_output = [text for text in required_output if text not in combined_output]
        result.update(
            {
                "duration_ms": round((time.monotonic() - started) * 1000),
                "exit_code": completed.returncode,
                "expected_exit_code": expected_exit_code,
                "missing_required_output": missing_output,
                "stdout_tail": completed.stdout[-4000:],
                "stderr_tail": completed.stderr[-4000:],
            }
        )
        if completed.returncode == expected_exit_code and not missing_output:
            result["status"] = "passed"
    except subprocess.TimeoutExpired as exc:
        result.update(
            {
                "duration_ms": round((time.monotonic() - started) * 1000),
                "error": "emulator process timed out",
                "stdout_tail": output_tail(exc.stdout),
                "stderr_tail": output_tail(exc.stderr),
            }
        )
    except OSError as exc:
        result["error"] = f"cannot start emulator: {exc}"
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--emulator", required=True, type=Path)
    parser.add_argument("--manifest", required=True, type=Path)
    parser.add_argument("--report", required=True, type=Path)
    args = parser.parse_args()

    emulator = resolve_from_repo(str(args.emulator))
    manifest_path = resolve_from_repo(str(args.manifest))
    report_path = resolve_from_repo(str(args.report))
    if not emulator.is_file():
        print(f"error: emulator not found: {emulator}", file=sys.stderr)
        return 2

    try:
        manifest = load_manifest(manifest_path)
        defaults = manifest.get("defaults", {})
        if not isinstance(defaults, dict):
            raise ManifestError("defaults must be an object")
        results = [run_case(emulator, defaults, case) for case in manifest["cases"]]
    except ManifestError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2

    passed = sum(item["status"] == "passed" for item in results)
    report = {
        "schema_version": 1,
        "generated_at": dt.datetime.now(dt.timezone.utc).isoformat(),
        "emulator": str(emulator),
        "summary": {"total": len(results), "passed": passed, "failed": len(results) - passed},
        "cases": results,
    }
    report_path.parent.mkdir(parents=True, exist_ok=True)
    report_path.write_text(json.dumps(report, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(f"conformance: {passed}/{len(results)} passed; report={report_path}")
    for item in results:
        if item["status"] != "passed":
            print(f"FAILED {item['name']}: {item.get('error', 'unexpected result')}")
    return 0 if passed == len(results) else 1


if __name__ == "__main__":
    raise SystemExit(main())
