#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2022 Julien Courtecuisse
# SPDX-License-Identifier: GPL-3.0-or-later

"""
@file generate_reuse_headers.py
@brief Generate SPDX headers for text files from CodeMeta metadata.

This script manages REUSE/SPDX headers for text files that support
comments but are not handled by generate_headers.py.

Supported formats:
- Hash comments (#): configuration files, YAML, TOML, INI, shell.
- HTML comments: Markdown documentation.

Files without a suitable comment syntax, such as JSON and binary files,
must be annotated in the root REUSE.toml file.

The copyright holders, copyright year and license identifier are
obtained from codemeta.json.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path
from typing import Any

from codemeta_identity import as_list, copyright_holders, get_value

# --------------------------------------------------------------------------- #
# Paths
# --------------------------------------------------------------------------- #

ROOT = Path(__file__).resolve().parents[2]
CODEMETA_FILE = ROOT / "codemeta.json"


# --------------------------------------------------------------------------- #
# Files managed by this script
# --------------------------------------------------------------------------- #

HASH_FILES = [
    ".clang-format",
    ".clang-tidy",
    ".flake8",
    ".gitignore",
    ".gitlab-ci.yml",
    ".lychee.toml",
    ".pre-commit-config.yaml",
    "doxygen/Doxyfile",
    "platformio.ini",
    "pyproject.toml",
    "CITATION.cff",
    "scripts/generate_fw_version.sh",
    "scripts/metadata/generate_reuse_headers.py",
    "sd/config.cfg",
    "sd/hw_assem.cfg",
]

HTML_FILES = [
    ".gitlab/issue_templates/Bug.md",
    ".gitlab/issue_templates/Default.md",
    ".gitlab/issue_templates/Feature.md",
    ".gitlab/issue_templates/Task.md",
    ".gitlab/merge_request_templates/Default.md",
    "AUTHORS.md",
    "CONTRIBUTING.md",
    "CONTRIBUTIONS.md",
    "CONTRIBUTORS.md",
    "README.md",
    "metadata checklist.md",
]


# --------------------------------------------------------------------------- #
# Regular expressions
# --------------------------------------------------------------------------- #

CODING_RE = re.compile(r"^#.*coding[:=]\s*[-\w.]+")
# REUSE-IgnoreStart
HASH_SPDX_RE = re.compile(r"^#\s*SPDX-(?:FileCopyrightText|License-Identifier):\s*.+$")
# REUSE-IgnoreEnd
FRONT_MATTER_RE = re.compile(
    r"\A---[ \t]*\n.*?\n(?:---|\.\.\.)[ \t]*(?:\n|\Z)",
    re.DOTALL,
)


# --------------------------------------------------------------------------- #
# CodeMeta metadata
# --------------------------------------------------------------------------- #


def load_metadata(path: Path) -> dict[str, Any]:
    """Load CodeMeta metadata from JSON."""
    with path.open(encoding="utf-8") as stream:
        metadata = json.load(stream)

    if not isinstance(metadata, dict):
        raise ValueError("codemeta.json must contain a JSON object.")

    return metadata


def get_copyright_year(metadata: dict[str, Any]) -> str:
    """Return the copyright year from CodeMeta."""
    year = get_value(
        metadata,
        "copyrightYear",
        "schema:copyrightYear",
    )

    if year is not None:
        match = re.match(r"^\d{4}", str(year))

        if match:
            return match.group(0)

    for key in (
        "dateModified",
        "datePublished",
        "dateCreated",
    ):
        value = get_value(metadata, key, f"schema:{key}")

        if isinstance(value, str):
            match = re.match(r"^\d{4}", value)

            if match:
                return match.group(0)

    raise ValueError("No valid copyright year found in CodeMeta.")


def get_license_id(metadata: dict[str, Any]) -> str:
    """Return the SPDX license identifier from CodeMeta."""
    license_value = get_value(
        metadata,
        "license",
        "schema:license",
    )

    for item in as_list(license_value):
        if isinstance(item, dict):
            item = get_value(
                item,
                "@id",
                "id",
                "url",
                "name",
            )

        if not isinstance(item, str):
            continue

        match = re.search(
            r"\b(GPL-[0-9]+(?:\.[0-9]+)?(?:-or-later|-only)?)\b",
            item,
            re.IGNORECASE,
        )

        if match:
            return match.group(1)

    raise ValueError("No supported SPDX license identifier found in CodeMeta.")


def get_spdx_metadata(
    metadata: dict[str, Any],
) -> tuple[str, list[str], str]:
    """Return copyright year, copyright holders and license identifier."""
    year = get_copyright_year(metadata)
    holders = copyright_holders(metadata)
    license_id = get_license_id(metadata)

    holders = [
        holder.strip()
        for holder in holders
        if isinstance(holder, str) and holder.strip()
    ]

    if not holders:
        raise ValueError("No copyright holders found in CodeMeta.")

    return year, holders, license_id


def get_hash_spdx_lines(
    year: str,
    holders: list[str],
    license_id: str,
) -> list[str]:
    """Generate SPDX lines using hash comments."""
    # REUSE-IgnoreStart
    lines = [f"# SPDX-FileCopyrightText: {year} {holder}" for holder in holders]
    lines.append(f"# SPDX-License-Identifier: {license_id}")
    # REUSE-IgnoreEnd
    return lines


def get_html_spdx_block(
    year: str,
    holders: list[str],
    license_id: str,
) -> str:
    """Generate SPDX metadata using an HTML comment."""
    # REUSE-IgnoreStart
    lines = [f"SPDX-FileCopyrightText: {year} {holder}" for holder in holders]
    lines.append(f"SPDX-License-Identifier: {license_id}")
    # REUSE-IgnoreEnd
    return "<!--\n" + "\n".join(lines) + "\n-->"


# --------------------------------------------------------------------------- #
# Hash-comment files
# --------------------------------------------------------------------------- #


def split_hash_prologue(
    lines: list[str],
) -> tuple[list[str], list[str]]:
    """Preserve shebang and encoding declaration."""
    prologue: list[str] = []
    index = 0

    if lines and lines[0].startswith("#!"):
        prologue.append(lines[0])
        index = 1

    if index < len(lines) and CODING_RE.match(lines[index]):
        prologue.append(lines[index])
        index += 1

    return prologue, lines[index:]


def remove_existing_hash_spdx(
    lines: list[str],
) -> list[str]:
    """Remove an existing SPDX block immediately after the prologue."""
    index = 0

    while index < len(lines) and not lines[index].strip():
        index += 1

    block_start = index

    while index < len(lines) and HASH_SPDX_RE.match(lines[index]):
        index += 1

    if index == block_start:
        return lines

    # Remove blank lines following the previous SPDX block.
    while index < len(lines) and not lines[index].strip():
        index += 1

    return lines[index:]


def render_hash_file(
    text: str,
    spdx_lines: list[str],
) -> str:
    """Insert SPDX metadata into a hash-comment file."""
    lines = text.split("\n")

    prologue, body = split_hash_prologue(lines)

    body = remove_existing_hash_spdx(body)

    # Remove leading empty lines from the body when the header
    # is regenerated.
    while body and not body[0].strip():
        body.pop(0)

    result = [
        *prologue,
        *spdx_lines,
        "",
        *body,
    ]

    return "\n".join(result).rstrip("\n") + "\n"


# --------------------------------------------------------------------------- #
# Markdown files
# --------------------------------------------------------------------------- #


def split_front_matter(
    text: str,
) -> tuple[str, str]:
    """Preserve YAML front matter when present."""
    match = FRONT_MATTER_RE.match(text)

    if match:
        return match.group(0), text[match.end() :]

    return "", text


def remove_existing_html_spdx(text: str) -> str:
    """Remove an existing SPDX HTML comment at the beginning."""
    match = re.match(
        r"\A[ \t]*<!--.*?-->[ \t]*(?:\n[ \t]*\n?)?",
        text,
        re.DOTALL,
    )

    if not match:
        return text

    block = match.group(0)
    # REUSE-IgnoreStart
    if (
        "SPDX-FileCopyrightText:" not in block
        or "SPDX-License-Identifier:" not in block
    ):
        return text
    # REUSE-IgnoreEnd
    return text[match.end() :]


def render_html_file(
    text: str,
    spdx_block: str,
) -> str:
    """Insert SPDX metadata into a Markdown file."""
    front_matter, body = split_front_matter(text)

    body = remove_existing_html_spdx(body)

    if front_matter:
        prefix = front_matter + "\n"
    else:
        prefix = ""

    # Avoid unnecessary blank lines before the document content.
    body = body.lstrip("\n")

    return (prefix + spdx_block + "\n\n" + body).rstrip("\n") + "\n"


# --------------------------------------------------------------------------- #
# File processing
# --------------------------------------------------------------------------- #


def read_text_file(path: Path) -> tuple[str, str, bool]:
    """Read a UTF-8 text file and preserve BOM and newline style."""
    raw = path.read_bytes()
    text = raw.decode("utf-8")

    bom = text.startswith("\ufeff")

    if bom:
        text = text[1:]

    newline = "\r\n" if "\r\n" in text else "\n"

    text = text.replace("\r\n", "\n")

    return text, newline, bom


def restore_text_format(
    text: str,
    newline: str,
    bom: bool,
) -> bytes:
    """Restore original newline convention and UTF-8 BOM."""
    if newline == "\r\n":
        text = text.replace("\n", "\r\n")

    if bom:
        text = "\ufeff" + text

    return text.encode("utf-8")


def process_file(
    relative_path: str,
    metadata: dict[str, Any],
    check_only: bool,
) -> bool:
    """Update one file and return True if it requires changes."""
    path = ROOT / relative_path

    if not path.is_file():
        return False

    try:
        original, newline, bom = read_text_file(path)

        year, holders, license_id = get_spdx_metadata(metadata)

        if relative_path in HASH_FILES:
            expected = render_hash_file(
                original,
                get_hash_spdx_lines(
                    year,
                    holders,
                    license_id,
                ),
            )

        else:
            expected = render_html_file(
                original,
                get_html_spdx_block(
                    year,
                    holders,
                    license_id,
                ),
            )

        expected_bytes = restore_text_format(
            expected,
            newline,
            bom,
        )

    except (OSError, UnicodeDecodeError, ValueError) as exc:
        print(
            f"ERROR: {relative_path}: {exc}",
            file=sys.stderr,
        )
        raise

    if expected_bytes == path.read_bytes():
        return False

    if not check_only:
        path.write_bytes(expected_bytes)

    return True


# --------------------------------------------------------------------------- #
# Main
# --------------------------------------------------------------------------- #


def main() -> int:
    parser = argparse.ArgumentParser(
        description=(
            "Generate REUSE/SPDX headers for project text files "
            "using CodeMeta metadata."
        )
    )

    parser.add_argument(
        "--check",
        action="store_true",
        help="Check files without modifying them.",
    )

    args = parser.parse_args()

    try:
        metadata = load_metadata(CODEMETA_FILE)

        # Validate metadata before processing any file.
        get_spdx_metadata(metadata)

    except (OSError, json.JSONDecodeError, ValueError) as exc:
        print(
            f"ERROR: Cannot load or validate CodeMeta: {exc}",
            file=sys.stderr,
        )
        return 1

    changed: list[str] = []
    failed = False

    for relative_path in HASH_FILES + HTML_FILES:
        try:
            if process_file(
                relative_path,
                metadata,
                args.check,
            ):
                changed.append(relative_path)

        except (OSError, UnicodeDecodeError, ValueError):
            failed = True

    if args.check:
        if changed:
            print("Files requiring SPDX updates:")

            for filename in changed:
                print(f"  {filename}")

            return 1 if not failed else 2

        if failed:
            return 2

        print("All managed REUSE text files are up to date.")
        return 0

    print(f"Scanned files: {len(HASH_FILES) + len(HTML_FILES)}")
    print(f"Updated files: {len(changed)}")

    for filename in changed:
        print(f"  {filename}")

    return 2 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
