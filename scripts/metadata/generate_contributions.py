#!/usr/bin/env python3
"""
@file generate_contributions.py
@brief Generate file-level contribution metadata from Git history.

@version 3.0.0
@date 2022-08-17

@section authors Authors
- Julien Courtecuisse
  Affiliation: IPHC-DEPE; UMR 7178; Université de Strasbourg-CNRS; F-67000 Strasbourg, France
  Email: julien.courtecuisse@iphc.cnrs.fr

@section project Project
Name: RFID-M0
Description: Embedded firmware for the open-source RFID-M0 project, designed to monitor small animal species around burrows and nest boxes. The software integrates RFID-based individual identification, infrared passage detection, real-time clock (RTC) timestamping, and SD-card data logging, with low-power management for autonomous field deployments. It supports ecological research, including long-term monitoring of Great Tits (Parus major) and research on the European hamster (Cricetus cricetus) conducted within the cross-border CRICETUS project.

@section license License
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
import json
import subprocess
import sys
from collections import defaultdict
from pathlib import Path
from typing import Any

from codemeta_identity import Identity, load_codemeta, load_identities, normalize_email

ROOT = Path(__file__).resolve().parents[2]
CODEMETA_FILE = ROOT / "codemeta.json"
DEFAULT_JSON = ROOT / "contributions.json"
DEFAULT_MD = ROOT / "CONTRIBUTIONS.md"

EXCLUDED_PATHS = {
    "CONTRIBUTIONS.md",
    "contributions.json",
}


def git(*args: str, cwd: Path = ROOT) -> str:
    """Run Git and return stdout."""
    result = subprocess.run(
        # core.quotepath=false keeps non-ASCII file names readable.
        ["git", "-c", "core.quotepath=false", *args],
        cwd=cwd,
        text=True,
        encoding="utf-8",
        errors="replace",
        capture_output=True,
    )
    if result.returncode != 0:
        raise RuntimeError(result.stderr.strip() or "Git command failed")
    return result.stdout


def git_available() -> bool:
    try:
        git("rev-parse", "--show-toplevel")
        return True
    except RuntimeError:
        return False


def get_commits(
    since: str | None = None,
    until: str | None = None,
) -> list[dict[str, Any]]:
    """Return commits with author identity and changed files."""
    args = [
        "log",
        "--all",
        "--reverse",
        "--format=%H%x1f%an%x1f%ae%x1f%aI",
        "--name-only",
    ]

    if since:
        args.append(f"--since={since}")
    if until:
        args.append(f"--until={until}")

    output = git(*args)
    commits: list[dict[str, Any]] = []
    current: dict[str, Any] | None = None

    for line in output.splitlines():
        if not line:
            continue

        if "\x1f" in line:
            parts = line.split("\x1f")
            if len(parts) != 4:
                current = None
                continue

            current = {
                "commit": parts[0],
                "name": parts[1].strip(),
                "email": parts[2].strip(),
                "date": parts[3].strip(),
                "files": [],
            }
            commits.append(current)
        elif current is not None:
            current["files"].append(line.strip())

    return commits


def contributor_key(name: str, email: str) -> str:
    normalized_email = normalize_email(email)
    if normalized_email:
        return normalized_email
    return name.strip().casefold()


def collect_contributions(
    commits: list[dict[str, Any]],
    identities: dict[str, Identity] | None = None,
) -> dict[str, dict[str, Any]]:
    """Aggregate commits and file paths by contributor.

    The e-mail address is the key used to match a Git identity with a person
    declared in ``codemeta.json``. When a match exists, the CodeMeta
    ``givenName`` and ``familyName`` replace the Git author name, which may be
    a nickname or an alias. Git names are kept in ``gitNames``.
    """
    identities = identities or {}
    contributors: dict[str, dict[str, Any]] = {}

    for commit in commits:
        name = commit["name"]
        email = commit["email"]
        key = contributor_key(name, email)

        if not key:
            continue

        identity = identities.get(key)

        contributor = contributors.setdefault(
            key,
            {
                "name": identity.full_name if identity else name,
                "email": identity.email if identity else email,
                "givenName": identity.given if identity else "",
                "familyName": identity.family if identity else "",
                "affiliation": identity.affiliation if identity else "",
                "matched": identity is not None,
                "gitNames": [],
                "commits": [],
                "files": set(),
            },
        )

        if name and name not in contributor["gitNames"]:
            contributor["gitNames"].append(name)

        contributor["commits"].append(
            {
                "hash": commit["commit"],
                "date": commit["date"],
            }
        )

        for filename in commit["files"]:
            if filename and filename not in EXCLUDED_PATHS:
                contributor["files"].add(filename)

    return contributors


def build_file_index(
    contributors: dict[str, dict[str, Any]],
) -> dict[str, list[dict[str, str]]]:
    """Build a file-to-contributors index."""
    files: dict[str, list[dict[str, str]]] = defaultdict(list)

    for contributor in contributors.values():
        person = {
            "name": contributor["name"],
            "email": contributor["email"],
        }

        for filename in contributor["files"]:
            files[filename].append(person)

    for filename in files:
        files[filename].sort(
            key=lambda person: (
                person["name"].casefold(),
                person["email"].casefold(),
            )
        )

    return dict(sorted(files.items()))


def serialize_contributors(
    contributors: dict[str, dict[str, Any]],
) -> list[dict[str, Any]]:
    """Convert internal sets to JSON-compatible structures."""
    result = []

    for contributor in contributors.values():
        entry: dict[str, Any] = {
            "name": contributor["name"],
            "email": contributor["email"],
        }

        for key in ("givenName", "familyName", "affiliation"):
            if contributor[key]:
                entry[key] = contributor[key]

        entry["gitNames"] = sorted(contributor["gitNames"], key=str.casefold)
        entry["commits"] = sorted(
            contributor["commits"],
            key=lambda item: item["date"],
        )
        entry["files"] = sorted(contributor["files"])
        result.append(entry)

    return sorted(
        result,
        key=lambda item: (
            item["name"].casefold(),
            item["email"].casefold(),
        ),
    )


def write_json(
    output: Path,
    contributors: list[dict[str, Any]],
    file_index: dict[str, list[dict[str, str]]],
) -> None:
    data = {
        "schemaVersion": "1.0",
        "generatedBy": "generate_contributions.py",
        "contributors": contributors,
        "files": file_index,
    }

    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(
        json.dumps(data, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8",
    )


def write_markdown(
    output: Path,
    contributors: list[dict[str, Any]],
    file_index: dict[str, list[dict[str, str]]],
) -> None:
    lines = [
        "# Contributions",
        "",
        "This file is generated from the Git history.",
        "Contributor names come from `codemeta.json`, matched by e-mail",
        "address; Git author names are only listed as aliases.",
        "",
        "## Contributors",
        "",
    ]

    if not contributors:
        lines.append("No contributors found.")
    else:
        for contributor in contributors:
            name = contributor["name"]
            email = contributor["email"]
            commits = len(contributor["commits"])
            lines.append(f"### {name}")
            lines.append("")
            if email:
                lines.append(f"- Email: `{email}`")
            if contributor.get("affiliation"):
                lines.append(f"- Affiliation: {contributor['affiliation']}")
            git_names = [
                git_name
                for git_name in contributor.get("gitNames", [])
                if git_name != name
            ]
            if git_names:
                joined = ", ".join(f"`{git_name}`" for git_name in git_names)
                lines.append(f"- Git author names: {joined}")
            lines.append(f"- Commits: {commits}")
            lines.append(f"- Files touched: {len(contributor['files'])}")
            lines.append("")

    lines.extend(
        [
            "## File attribution",
            "",
            "The following list associates each file with the contributors",
            "who modified it according to the available Git history.",
            "",
        ]
    )

    if not file_index:
        lines.append("No file contributions found.")
    else:
        for filename, people in file_index.items():
            names = ", ".join(person["name"] for person in people)
            lines.append(f"- `{filename}`: {names}")

    lines.append("")
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text("\n".join(lines), encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Generate CONTRIBUTIONS.md and contributions.json."
    )
    parser.add_argument("--since", help="Only include commits after this date.")
    parser.add_argument("--until", help="Only include commits before this date.")
    parser.add_argument("--codemeta", type=Path, default=CODEMETA_FILE)
    parser.add_argument("--json", type=Path, default=DEFAULT_JSON)
    parser.add_argument("--markdown", type=Path, default=DEFAULT_MD)
    args = parser.parse_args()

    if not git_available():
        print("Error: this script must run inside a Git repository.", file=sys.stderr)
        return 1

    try:
        commits = get_commits(args.since, args.until)
        identities = load_identities(load_codemeta(args.codemeta))
        contributors = collect_contributions(commits, identities)

        for contributor in contributors.values():
            if not contributor["matched"]:
                print(
                    f"WARNING: no codemeta.json person has the e-mail "
                    f"{contributor['email'] or '(none)'}; keeping the Git name "
                    f"'{contributor['name']}'.",
                    file=sys.stderr,
                )

        serialized = serialize_contributors(contributors)
        file_index = build_file_index(contributors)

        write_json(args.json, serialized, file_index)
        write_markdown(args.markdown, serialized, file_index)

    except (RuntimeError, OSError, ValueError) as exc:
        print(f"Error: {exc}", file=sys.stderr)
        return 1

    print(f"Generated: {args.markdown}")
    print(f"Generated: {args.json}")
    print(f"Contributors: {len(serialized)}")
    print(f"Files: {len(file_index)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
