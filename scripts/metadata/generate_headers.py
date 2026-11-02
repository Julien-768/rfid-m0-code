#!/usr/bin/env python3
"""
@file generate_headers.py
@brief Generate Doxygen file headers from CodeMeta and Git contributions.

Supported files:

- C and C++ sources and headers (``.c``, ``.h``, ``.cpp``, ``.hpp``, ``.cc``):
  a ``/** ... */`` comment at the top of the file.
- Python files (``.py``): a Doxygen module docstring placed after the shebang.

People are always written as ``givenName familyName`` as declared in
``codemeta.json``. File-level contributors from ``contributions.json`` are
matched to CodeMeta people by e-mail address.

Existing descriptions (``@brief``, details, ``@note``, ``@ingroup``, ...) are
preserved. Hand-written ``@author``, ``@version`` and ``@date`` lines are
replaced by the generated ones.

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
import re
import subprocess
import sys
from pathlib import Path
from typing import Any

from codemeta_identity import (
    Identity,
    as_list,
    clean_text,
    copyright_holders,
    emails_of,
    full_name_of,
    get_value,
    is_role,
    load_codemeta,
    load_identities,
    normalize_email,
)

ROOT = Path(__file__).resolve().parents[2]
CODEMETA_FILE = ROOT / "codemeta.json"
CONTRIBUTIONS_FILE = ROOT / "contributions.json"

C_EXTENSIONS = {".c", ".h", ".cpp", ".hpp", ".cc"}
PYTHON_EXTENSIONS = {".py"}
SOURCE_EXTENSIONS = C_EXTENSIONS | PYTHON_EXTENSIONS

EXCLUDED_DIRS = {
    ".git",
    ".pio",
    ".venv",
    "venv",
    "build",
    "dist",
    "node_modules",
    "__pycache__",
}

# Header comment: "/** ... */" (but not "/**/") at the top of a C/C++ file.
C_COMMENT_RE = re.compile(r"\A\s*/\*\*(?!/)(.*?)\*/", re.DOTALL)
# Plain "/* ... */" comment at the top of a C/C++ file.
PLAIN_COMMENT_RE = re.compile(r"\A\s*/\*(?![*!])(.*?)\*/[ \t]*(?:\n|\Z)", re.DOTALL)

FILE_TAG_RE = re.compile(r"^\s*[@\\]file\b")
BRIEF_TAG_RE = re.compile(r"^\s*[@\\]brief\b")
# Single lines replaced by generated values.
DROPPED_TAG_RE = re.compile(r"^\s*[@\\](?:file|authors?|version|date)\b")
# Start of the generated sections: everything from here is regenerated.
GENERATED_SECTION_RE = re.compile(
    r"^\s*[@\\]section\s+(?:authors|project|contributors|license)\b"
)

CODING_RE = re.compile(r"^#.*coding[:=]\s*[-\w.]+")
PRAGMA_COMMENT_RE = re.compile(
    r"^#\s*(?:type:|noqa|pylint:|fmt:|isort:|pragma|-\*-|!)"
)
DOCSTRING_RE = re.compile(
    r"\A(?P<prefix>[rRuU]?)(?P<quote>\"\"\"|''')(?P<body>.*?)(?P=quote)[ \t]*(?:\n|\Z)",
    re.DOTALL,
)

LICENSE_TEXT = [
    "This program is free software: you can redistribute it and/or",
    "modify it under the terms of the GNU General Public License",
    "as published by the Free Software Foundation, either version 3",
    "of the License, or (at your option) any later version.",
    "",
    "This program is distributed in the hope that it will be useful,",
    "but WITHOUT ANY WARRANTY; without even the implied warranty of",
    "MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.",
    "See the GNU General Public License for more details.",
]


# --------------------------------------------------------------------------- #
# CodeMeta and contributions
# --------------------------------------------------------------------------- #


def person_name(person: dict[str, Any]) -> str:
    """Return ``givenName familyName`` exactly as written in CodeMeta."""
    return full_name_of(person)


def person_email(person: dict[str, Any]) -> str:
    emails = emails_of(person)
    return emails[0] if emails else ""


def person_affiliation(person: dict[str, Any]) -> str:
    affiliation = get_value(person, "affiliation", "schema:affiliation")
    values = []

    for item in as_list(affiliation):
        if isinstance(item, dict):
            item = get_value(item, "name", "legalName")

        text = clean_text(item)
        if text and text not in values:
            values.append(text)

    return "; ".join(values)


def load_contributions(path: Path) -> dict[str, Any]:
    if not path.exists():
        return {"files": {}}

    with path.open(encoding="utf-8") as stream:
        data = json.load(stream)

    if not isinstance(data, dict):
        raise ValueError("contributions.json must contain a JSON object.")

    return data


def get_authors(metadata: dict[str, Any]) -> list[dict[str, Any]]:
    authors = []

    for person in as_list(get_value(metadata, "author", "schema:author")):
        if isinstance(person, dict) and not is_role(person):
            authors.append(person)

    return authors


def get_file_contributors(
    filename: str,
    contributions: dict[str, Any],
) -> list[dict[str, Any]]:
    files = contributions.get("files", {})

    if not isinstance(files, dict):
        return []

    entries = files.get(filename, [])

    if not isinstance(entries, list):
        return []

    return [item for item in entries if isinstance(item, dict)]


def resolve_file_contributors(
    filename: str,
    authors: list[dict[str, Any]],
    contributions: dict[str, Any],
    identities: dict[str, Identity],
) -> list[tuple[str, str, str]]:
    """Return ``(name, affiliation, email)`` for the contributors of a file.

    Contributors are matched to CodeMeta people by e-mail address so that the
    CodeMeta given and family names are used instead of Git author names.
    Authors are excluded: they already have their own section.
    """
    author_emails = {
        normalize_email(email) for author in authors for email in emails_of(author)
    }
    author_names = {person_name(author).casefold() for author in authors}

    seen: set[str] = set()
    result: list[tuple[str, str, str]] = []

    for entry in get_file_contributors(filename, contributions):
        email_key = normalize_email(entry.get("email"))
        identity = identities.get(email_key)

        if identity is not None:
            name = identity.full_name
            affiliation = identity.affiliation
            email = identity.email
        else:
            name = clean_text(entry.get("name"))
            affiliation = ""
            email = clean_text(entry.get("email"))

        if not name:
            continue

        if (email_key and email_key in author_emails) or name.casefold() in author_names:
            continue

        identity_key = email_key or name.casefold()
        if identity_key in seen:
            continue

        seen.add(identity_key)
        result.append((name, affiliation, email))

    return sorted(result, key=lambda item: (item[0].casefold(), item[2].casefold()))


def get_date(metadata: dict[str, Any]) -> str:
    for key in ("dateModified", "datePublished", "dateCreated"):
        value = metadata.get(key)
        if isinstance(value, str) and value.strip():
            return value.strip()

    return "Not specified"


def get_version(metadata: dict[str, Any]) -> str:
    return clean_text(metadata.get("version")) or "Not specified"


def get_license_id(metadata: dict[str, Any]) -> str:
    license_value = get_value(metadata, "license", "schema:license")

    for item in as_list(license_value):
        if isinstance(item, dict):
            item = get_value(item, "@id", "id", "url", "name")

        if isinstance(item, str):
            match = re.search(
                r"(GPL-[0-9]+(?:\.[0-9]+)?(?:-or-later|-only)?)",
                item,
                re.IGNORECASE,
            )
            if match:
                return match.group(1)

    return "License not specified"


def get_copyright_year(metadata: dict[str, Any]) -> str:
    year = metadata.get("copyrightYear")

    if year is not None:
        return str(year)

    for key in ("dateModified", "datePublished", "dateCreated"):
        value = metadata.get(key)
        if isinstance(value, str) and re.match(r"^\d{4}", value):
            return value[:4]

    return "Year not specified"


def get_project_name(metadata: dict[str, Any]) -> str:
    return (
        clean_text(get_value(metadata, "name", "schema:name"))
        or "Project name not specified"
    )


def get_project_description(metadata: dict[str, Any]) -> str:
    return clean_text(get_value(metadata, "description", "schema:description"))


# --------------------------------------------------------------------------- #
# Header content (plain Doxygen text, without comment decoration)
# --------------------------------------------------------------------------- #


def person_block(name: str, affiliation: str, email: str) -> list[str]:
    """Format a person identically for authors and contributors."""
    lines = [f"- {name}"]

    if affiliation:
        lines.append(f"  Affiliation: {affiliation}")

    if email:
        lines.append(f"  Email: {email}")

    return lines


def generated_lines(
    filename: str,
    metadata: dict[str, Any],
    contributions: dict[str, Any],
    identities: dict[str, Identity],
) -> list[str]:
    """Return the generated part of a header, from ``@version`` to the license."""
    authors = get_authors(metadata)

    lines = [
        f"@version {get_version(metadata)}",
        f"@date {get_date(metadata)}",
        "",
        "@section authors Authors",
    ]

    written = 0
    for author in authors:
        name = person_name(author)
        if not name:
            continue

        lines.extend(
            person_block(name, person_affiliation(author), person_email(author))
        )
        written += 1

    if not written:
        lines.append("No authors specified.")

    lines.extend(["", "@section project Project", f"Name: {get_project_name(metadata)}"])

    description = get_project_description(metadata)
    if description:
        lines.append(f"Description: {description}")

    contributors = resolve_file_contributors(filename, authors, contributions, identities)

    if contributors:
        lines.extend(["", "@section contributors Contributors"])

        for name, affiliation, email in contributors:
            lines.extend(person_block(name, affiliation, email))

    holders = copyright_holders(metadata)
    year = get_copyright_year(metadata)
    copyright_line = f"Copyright (C) {year}"
    if holders:
        copyright_line += " " + " - ".join(holders)

    lines.extend(["", "@section license License", copyright_line, ""])
    lines.extend(LICENSE_TEXT)
    lines.extend(
        [
            "",
            f"License: {get_license_id(metadata)}",
            "https://www.gnu.org/licenses/gpl-3.0.html",
        ]
    )

    return lines


def document_lines(
    filename: str,
    preserved: list[str],
    metadata: dict[str, Any],
    contributions: dict[str, Any],
    identities: dict[str, Identity],
) -> list[str]:
    """Return the complete header text: ``@file``, preserved text, generated part."""
    lines = [f"@file {Path(filename).name}"]
    lines.extend(preserved)
    lines.append("")
    lines.extend(generated_lines(filename, metadata, contributions, identities))
    return [line.rstrip() for line in lines]


# --------------------------------------------------------------------------- #
# Reading existing headers
# --------------------------------------------------------------------------- #


def clean_preserved(lines: list[str]) -> list[str]:
    """Keep the hand-written part of a header.

    Drops ``@file``, ``@author``, ``@version`` and ``@date`` lines and the
    previously generated sections, then collapses blank lines.
    """
    kept: list[str] = []

    for line in lines:
        if GENERATED_SECTION_RE.match(line):
            break

        if DROPPED_TAG_RE.match(line):
            continue

        if not line.strip() and (not kept or not kept[-1]):
            continue

        kept.append(line.rstrip())

    while kept and not kept[-1]:
        kept.pop()

    return kept


def comment_inner_lines(inner: str) -> list[str]:
    """Remove the ``*`` decoration of a C block comment."""
    return [re.sub(r"^\s*\*? ?", "", raw).rstrip() for raw in inner.split("\n")]


def brief_from_plain_comment(inner: str, filename: str) -> str | None:
    """Interpret a plain ``/* ... */`` comment at the top of a C/C++ file.

    Returns ``""`` for a comment that only repeats the file name (it is
    removed), the description for ``name - description`` (it becomes the
    ``@brief``), and ``None`` for any other comment (it is left untouched).
    """
    text = " ".join(line for line in comment_inner_lines(inner) if line)
    stem = Path(filename).stem

    if re.fullmatch(r"[\w.\-]+", text) and Path(text).stem == stem:
        return ""

    match = re.fullmatch(r"([\w.\-]+)\s+-\s+(.+)", text)
    if match and Path(match.group(1)).stem == stem:
        return match.group(2).strip()

    return None


def split_c_source(text: str, filename: str) -> tuple[list[str], str]:
    """Return ``(preserved header lines, rest of the file)`` for C/C++."""
    match = C_COMMENT_RE.match(text)

    if match:
        inner = comment_inner_lines(match.group(1))
        if any(FILE_TAG_RE.match(line) for line in inner):
            return clean_preserved(inner), text[match.end() :]

    plain = PLAIN_COMMENT_RE.match(text)

    if plain:
        brief = brief_from_plain_comment(plain.group(1), filename)
        if brief is not None:
            return ([f"@brief {brief}"] if brief else []), text[plain.end() :]

    return [], text


def brief_and_details(lines: list[str]) -> list[str]:
    """Turn a plain description into ``@brief`` plus detailed text."""
    lines = list(lines)

    while lines and not lines[0].strip():
        lines.pop(0)

    if not lines:
        return []

    first_blank = next((i for i, line in enumerate(lines) if not line.strip()), len(lines))
    result = [f"@brief {lines[0]}", *lines[1:first_blank], *lines[first_blank:]]
    return clean_preserved(result)


def split_python_source(text: str) -> tuple[list[str], list[str], str]:
    """Return ``(prologue, preserved header lines, rest of the file)`` for Python.

    The prologue is the shebang and the encoding line, which must stay first.
    """
    lines = text.split("\n")
    prologue: list[str] = []
    index = 0

    if lines and lines[0].startswith("#!"):
        prologue.append(lines[0])
        index = 1

    if index < len(lines) and CODING_RE.match(lines[index]):
        prologue.append(lines[index])
        index += 1

    rest = "\n".join(lines[index:]).lstrip("\n")
    match = DOCSTRING_RE.match(rest)

    if match:
        doc_lines = [line.rstrip() for line in match.group("body").split("\n")]

        if any(FILE_TAG_RE.match(line) or BRIEF_TAG_RE.match(line) for line in doc_lines):
            preserved = clean_preserved(doc_lines)
        else:
            preserved = brief_and_details(doc_lines)

        return prologue, preserved, rest[match.end() :]

    # No docstring: a leading "#" comment block followed by a blank line is
    # the description of the script.
    rest_lines = rest.split("\n")
    block: list[str] = []
    position = 0

    while (
        position < len(rest_lines)
        and rest_lines[position].startswith("#")
        and not PRAGMA_COMMENT_RE.match(rest_lines[position])
    ):
        block.append(re.sub(r"^#\s?", "", rest_lines[position]).rstrip())
        position += 1

    followed_by_blank = position >= len(rest_lines) or not rest_lines[position].strip()

    if block and followed_by_blank:
        return prologue, brief_and_details(block), "\n".join(rest_lines[position:])

    return prologue, [], rest


# --------------------------------------------------------------------------- #
# Rendering
# --------------------------------------------------------------------------- #


def render_c(doc: list[str], body: str) -> str:
    safe = [line.replace("*/", "* /") for line in doc]
    lines = ["/**"]
    lines.extend(f" * {line}" if line else " *" for line in safe)
    lines.append(" */")

    text = "\n".join(lines) + "\n"
    body = body.lstrip("\n")

    return text + ("\n" + body if body else "")


def render_python(prologue: list[str], doc: list[str], rest: str) -> str:
    safe = [line.replace('"""', "'''") for line in doc]
    opening = 'r"""' if any("\\" in line for line in safe) else '"""'

    text = "\n".join([*prologue, opening, *safe, '"""']) + "\n"
    rest = rest.lstrip("\n")

    return text + ("\n" + rest if rest else "")


def expected_content(
    relative_path: str,
    original: str,
    metadata: dict[str, Any],
    contributions: dict[str, Any],
    identities: dict[str, Identity],
) -> str:
    """Return the content the file should have, header included."""
    bom = original.startswith("﻿")
    text = original[1:] if bom else original

    crlf = "\r\n" in text
    text = text.replace("\r\n", "\n")

    if Path(relative_path).suffix.lower() in PYTHON_EXTENSIONS:
        prologue, preserved, rest = split_python_source(text)
        doc = document_lines(relative_path, preserved, metadata, contributions, identities)
        result = render_python(prologue, doc, rest)
    else:
        preserved, body = split_c_source(text, relative_path)
        doc = document_lines(relative_path, preserved, metadata, contributions, identities)
        result = render_c(doc, body)

    if crlf:
        result = result.replace("\n", "\r\n")

    return ("﻿" if bom else "") + result


# --------------------------------------------------------------------------- #
# File discovery and main
# --------------------------------------------------------------------------- #


def find_source_files() -> list[Path]:
    """List source files, preferring Git's view (tracked + untracked, not ignored)."""
    candidates: list[Path] = []

    try:
        output = subprocess.run(
            ["git", "ls-files", "-z", "--cached", "--others", "--exclude-standard"],
            cwd=ROOT,
            capture_output=True,
            check=True,
        ).stdout.decode("utf-8", errors="replace")
        candidates = [ROOT / name for name in output.split("\0") if name]
    except (OSError, subprocess.CalledProcessError):
        candidates = [path for path in ROOT.rglob("*")]

    result = []

    for path in candidates:
        if not path.is_file() or path.suffix.lower() not in SOURCE_EXTENSIONS:
            continue

        if any(part in EXCLUDED_DIRS for part in path.relative_to(ROOT).parts):
            continue

        result.append(path)

    return sorted(set(result))


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Generate Doxygen headers from CodeMeta."
    )
    parser.add_argument(
        "--check",
        action="store_true",
        help="Check whether headers need updating without modifying files.",
    )
    parser.add_argument(
        "--fix",
        action="store_true",
        help="Generate or update headers (default behaviour).",
    )
    parser.add_argument("--codemeta", type=Path, default=CODEMETA_FILE)
    parser.add_argument("--contributions", type=Path, default=CONTRIBUTIONS_FILE)
    args = parser.parse_args()

    try:
        metadata = load_codemeta(args.codemeta)
        contributions = load_contributions(args.contributions)
    except (OSError, json.JSONDecodeError, ValueError) as exc:
        print(f"Error loading metadata: {exc}", file=sys.stderr)
        return 1

    identities = load_identities(metadata)
    files = find_source_files()
    changed = []

    for path in files:
        relative_path = path.relative_to(ROOT).as_posix()

        try:
            original = path.read_bytes().decode("utf-8")
        except UnicodeDecodeError:
            print(f"WARNING: {relative_path} is not valid UTF-8; skipped.", file=sys.stderr)
            continue

        expected = expected_content(
            relative_path, original, metadata, contributions, identities
        )

        if expected == original:
            continue

        changed.append(relative_path)

        if not args.check:
            path.write_bytes(expected.encode("utf-8"))

    if args.check:
        if changed:
            print("Headers requiring updates:")
            for filename in changed:
                print(f"  {filename}")
            return 1

        print(f"All {len(files)} file headers are up to date.")
        return 0

    print(f"Scanned files: {len(files)}")
    print(f"Updated headers: {len(changed)}")

    for filename in changed:
        print(f"  {filename}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
