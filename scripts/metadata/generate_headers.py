#!/usr/bin/env python3
"""Generate Doxygen file headers from CodeMeta and Git contributions."""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[2]
CODEMETA_FILE = ROOT / "codemeta.json"
CONTRIBUTIONS_FILE = ROOT / "contributions.json"

SOURCE_EXTENSIONS = {".c", ".h"}

HEADER_RE = re.compile(
    r"\A(\s*)/\*\*.*?\*/",
    re.DOTALL,
)

BRIEF_RE = re.compile(
    r"^\s*\*\s*@brief\s+(.*)$",
    re.MULTILINE,
)


def as_list(value: Any) -> list[Any]:
    if value is None:
        return []
    return value if isinstance(value, list) else [value]


def get_value(data: dict[str, Any], *keys: str) -> Any:
    for key in keys:
        if key in data and data[key] is not None:
            return data[key]
    return None


def text_value(value: Any) -> str:
    if isinstance(value, str):
        return value.strip()

    if isinstance(value, dict):
        return str(
            get_value(value, "name", "givenName", "@value", "value") or ""
        ).strip()

    return ""


def person_name(person: dict[str, Any]) -> str:
    given = text_value(get_value(person, "givenName", "schema:givenName"))
    family = text_value(get_value(person, "familyName", "schema:familyName"))

    if given or family:
        return " ".join(part for part in (given, family.upper()) if part)

    return text_value(get_value(person, "name", "schema:name"))


def person_email(person: dict[str, Any]) -> str:
    return text_value(get_value(person, "email", "schema:email"))


def person_affiliation(person: dict[str, Any]) -> str:
    affiliation = get_value(person, "affiliation", "schema:affiliation")

    if isinstance(affiliation, list):
        values = [text_value(item) for item in affiliation]
        return "; ".join(value for value in values if value)

    return text_value(affiliation)


def person_keys(person: dict[str, Any]) -> set[str]:
    """Build identifiers for comparing author/contributor identities."""
    values = []

    for key in (
        "@id",
        "id",
        "email",
        "orcid",
        "identifier",
        "sameAs",
    ):
        value = person.get(key)
        if isinstance(value, list):
            values.extend(value)
        elif value is not None:
            values.append(value)

    name = person_name(person)
    if name:
        values.append(name)

    result = set()

    for value in values:
        if isinstance(value, dict):
            value = get_value(value, "@id", "id", "value")

        if isinstance(value, str) and value.strip():
            normalized = value.strip().casefold()
            result.add(normalized)

    return result


def is_role(person: Any) -> bool:
    if not isinstance(person, dict):
        return False

    types = as_list(get_value(person, "@type", "type"))
    return any(str(item).split(":")[-1] == "Role" for item in types)


def load_codemeta(path: Path) -> dict[str, Any]:
    with path.open(encoding="utf-8") as stream:
        data = json.load(stream)

    if not isinstance(data, dict):
        raise ValueError("codemeta.json must contain a JSON object.")

    return data


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


def get_date(metadata: dict[str, Any]) -> str:
    for key in ("dateModified", "datePublished", "dateCreated"):
        value = metadata.get(key)
        if isinstance(value, str) and value.strip():
            return value.strip()

    return "Not specified"


def get_version(metadata: dict[str, Any]) -> str:
    return text_value(metadata.get("version")) or "Not specified"


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


def get_copyright_holders(metadata: dict[str, Any]) -> list[str]:
    holders = []

    for holder in as_list(
        get_value(metadata, "copyrightHolder", "schema:copyrightHolder")
    ):
        if isinstance(holder, dict):
            name = text_value(get_value(holder, "name", "schema:name"))
        else:
            name = text_value(holder)

        if name and name not in holders:
            holders.append(name)

    return holders


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
        text_value(get_value(metadata, "name", "schema:name"))
        or "Project name not specified"
    )


def get_project_description(metadata: dict[str, Any]) -> str:
    return text_value(get_value(metadata, "description", "schema:description"))


def make_header(
    filename: str,
    metadata: dict[str, Any],
    contributions: dict[str, Any],
    brief: str = "",
) -> str:
    authors = get_authors(metadata)
    license_id = get_license_id(metadata)
    copyright_holders = get_copyright_holders(metadata)
    copyright_year = get_copyright_year(metadata)

    lines = [
        "/**",
        f" * @file {Path(filename).name}",
    ]

    if brief:
        lines.append(f" * @brief {brief}")

    lines.extend(
        [
            " *",
            f" * @version {get_version(metadata)}",
            f" * @date {get_date(metadata)}",
            " *",
            " * @section authors Authors",
        ]
    )

    if authors:
        for author in authors:
            name = person_name(author)
            if not name:
                continue

            lines.append(f" * - {name}")

            affiliation = person_affiliation(author)
            email = person_email(author)

            if affiliation:
                lines.append(f" *   Affiliation: {affiliation}")

            if email:
                lines.append(f" *   Email: {email}")
    else:
        lines.append(" * No authors specified.")

    lines.extend(
        [
            " *",
            " * @section project Project",
            f" * Name: {get_project_name(metadata)}",
        ]
    )

    description = get_project_description(metadata)
    if description:
        lines.append(f" * Description: {description}")

    # File-specific contributors only.
    author_keys = set()
    for author in authors:
        author_keys.update(person_keys(author))

    file_contributors = get_file_contributors(filename, contributions)

    unique_contributors = []
    seen = set()

    for contributor in file_contributors:
        keys = person_keys(contributor)

        if keys & author_keys:
            continue

        identity = tuple(sorted(keys))
        if not identity or identity in seen:
            continue

        seen.add(identity)
        unique_contributors.append(contributor)

    if unique_contributors:
        lines.extend(
            [
                " *",
                " * @section contributors Contributors",
            ]
        )

        for contributor in unique_contributors:
            name = text_value(contributor.get("name")) or person_name(contributor)
            email = person_email(contributor)

            if name:
                if email:
                    lines.append(f" * - {name} ({email})")
                else:
                    lines.append(f" * - {name}")

    lines.extend(
        [
            " *",
            " * @section license License",
        ]
    )

    if copyright_holders:
        holders = " - ".join(copyright_holders)
        lines.append(f" * Copyright (C) {copyright_year} {holders}")
    else:
        lines.append(f" * Copyright (C) {copyright_year}")

    lines.extend(
        [
            " *",
            " * This program is free software: you can redistribute it and/or",
            " * modify it under the terms of the GNU General Public License",
            " * as published by the Free Software Foundation, either version 3",
            " * of the License, or (at your option) any later version.",
            " *",
            " * This program is distributed in the hope that it will be useful,",
            " * but WITHOUT ANY WARRANTY; without even the implied warranty of",
            " * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.",
            " * See the GNU General Public License for more details.",
            " *",
            " * License: " + license_id,
            " * https://www.gnu.org/licenses/gpl-3.0.html",
            " */",
            "",
        ]
    )

    return "\n".join(lines)


def update_file(
    path: Path,
    metadata: dict[str, Any],
    contributions: dict[str, Any],
) -> bool:
    relative_path = path.relative_to(ROOT).as_posix()
    original = path.read_text(encoding="utf-8")

    brief = ""
    match = HEADER_RE.match(original)

    if match:
        old_header = match.group(0)
        brief_match = BRIEF_RE.search(old_header)
        if brief_match:
            brief = brief_match.group(1).strip()

        body = original[match.end() :].lstrip("\r\n")
    else:
        body = original

    header = make_header(
        relative_path,
        metadata,
        contributions,
        brief,
    )

    updated = header + body

    if updated == original:
        return False

    path.write_text(updated, encoding="utf-8")
    return True


def find_source_files() -> list[Path]:
    excluded = {
        ".git",
        ".pio",
        ".venv",
        "venv",
        "build",
        "dist",
    }

    result = []

    for path in ROOT.rglob("*"):
        if not path.is_file() or path.suffix.lower() not in SOURCE_EXTENSIONS:
            continue

        if any(part in excluded for part in path.relative_to(ROOT).parts):
            continue

        result.append(path)

    return sorted(result)


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
    parser.add_argument(
        "--codemeta",
        type=Path,
        default=CODEMETA_FILE,
    )
    parser.add_argument(
        "--contributions",
        type=Path,
        default=CONTRIBUTIONS_FILE,
    )
    args = parser.parse_args()

    try:
        metadata = load_codemeta(args.codemeta)
        contributions = load_contributions(args.contributions)
    except (OSError, json.JSONDecodeError, ValueError) as exc:
        print(f"Error loading metadata: {exc}", file=sys.stderr)
        return 1

    files = find_source_files()
    changed = []

    for path in files:
        relative_path = path.relative_to(ROOT).as_posix()

        if args.check:
            original = path.read_text(encoding="utf-8")
            brief = ""
            match = HEADER_RE.match(original)

            if match:
                brief_match = BRIEF_RE.search(match.group(0))
                if brief_match:
                    brief = brief_match.group(1).strip()
                body = original[match.end() :].lstrip("\r\n")
            else:
                body = original

            expected = (
                make_header(
                    relative_path,
                    metadata,
                    contributions,
                    brief,
                )
                + body
            )

            if expected != original:
                changed.append(relative_path)
        else:
            if update_file(path, metadata, contributions):
                changed.append(relative_path)

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
