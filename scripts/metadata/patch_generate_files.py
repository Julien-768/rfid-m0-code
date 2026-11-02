#!/usr/bin/env python3
"""Run the RFID-M0 metadata generation workflow."""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SCRIPT_DIR = Path(__file__).resolve().parent

CONTRIBUTIONS_SCRIPT = SCRIPT_DIR / "generate_contributions.py"
HEADERS_SCRIPT = SCRIPT_DIR / "generate_headers.py"


def run_script(script: Path, *args: str) -> int:
    """Run a Python script and return its exit code."""
    if not script.is_file():
        print(f"Error: script not found: {script}", file=sys.stderr)
        return 1

    command = [sys.executable, str(script), *args]

    print(f"\n$ {' '.join(command)}")
    result = subprocess.run(command, cwd=ROOT)

    return result.returncode


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Run the RFID-M0 metadata generation workflow."
    )

    parser.add_argument(
        "--check",
        action="store_true",
        help="Check headers without modifying source files.",
    )

    parser.add_argument(
        "--skip-contributions",
        action="store_true",
        help="Do not regenerate contribution metadata.",
    )

    args = parser.parse_args()

    if not args.skip_contributions:
        print("=== Step 1: Generate contribution metadata ===")

        result = run_script(CONTRIBUTIONS_SCRIPT)
        if result != 0:
            return result

    print("\n=== Step 2: Process Doxygen headers ===")

    header_args = ["--check"] if args.check else ["--fix"]

    result = run_script(HEADERS_SCRIPT, *header_args)
    if result != 0:
        return result

    print("\nMetadata workflow completed successfully.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
