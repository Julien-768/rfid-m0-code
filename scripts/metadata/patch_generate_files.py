#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2022 Julien Courtecuisse
# SPDX-License-Identifier: GPL-3.0-or-later

"""
@file patch_generate_files.py
@brief Run the RFID-M0 metadata generation workflow.

@section authors Authors
- Julien Courtecuisse
  Affiliation: IPHC-DEPE; UMR 7178; Université de Strasbourg-CNRS; F-67000 Strasbourg, France
  Email: julien.courtecuisse@iphc.cnrs.fr

@section project Project
Name: RFID-M0
Description: Embedded firmware for the open-source RFID-M0 project, designed to monitor small animal species around burrows and nest boxes. The software integrates RFID-based individual identification, infrared passage detection, real-time clock (RTC) timestamping, and SD-card data logging, with low-power management for autonomous field deployments. It supports ecological research, including long-term monitoring of Great Tits (Parus major) and research on the European hamster (Cricetus cricetus) conducted within the cross-border CRICETUS project.
@version 3.0.0
@date 2022-08-17

@section license License
SPDX-FileCopyrightText: 2022 Julien Courtecuisse
SPDX-License-Identifier: GPL-3.0-or-later

Copyright (C) 2022 Julien Courtecuisse

This program is free software: you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation, either version 3
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
See the GNU General Public License for more details.

License: GPL-3.0-or-later
https://www.gnu.org/licenses/gpl-3.0.html
"""

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
