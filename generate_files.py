#!/usr/bin/env python3
"""Generate AUTHORS.md and CITATION.cff from CodeMeta v3 metadata."""

import json
import re
import sys
from datetime import date
from pathlib import Path
from urllib.parse import urlparse

CODEMETA_FILE = Path("codemeta.json")
AUTHORS_FILE = Path("AUTHORS.md")
CITATION_FILE = Path("CITATION.cff")


def get_type(item):
    """Return the JSON-LD type, accepting legacy 'type' as well."""
    value = item.get("@type", item.get("type", ""))
    if isinstance(value, list):
        return value
    return [value] if value else []


def get_id(item):
    """Return the JSON-LD identifier."""
    return item.get("@id") or item.get("id") or ""


def get_name(person):
    """Build a person's display name."""
    given = person.get("givenName", "").strip()
    family = person.get("familyName", "").strip()
    return " ".join(part for part in (given, family) if part)


def get_orcid(person):
    """Return a validated ORCID URL, or None."""
    identifier = get_id(person)
    if not identifier:
        identifier = person.get("orcid", "")

    match = re.fullmatch(
        r"https?://orcid\.org/(\d{4}-\d{4}-\d{4}-[\dX]{4})/?",
        identifier,
        re.IGNORECASE,
    )
    return f"https://orcid.org/{match.group(1)}" if match else None


def get_affiliation(person):
    """Extract an affiliation name."""
    affiliation = person.get("affiliation", {})

    if isinstance(affiliation, dict):
        return affiliation.get("name", "")

    if isinstance(affiliation, list):
        return "; ".join(
            item["name"]
            for item in affiliation
            if isinstance(item, dict) and item.get("name")
        )

    return ""


def get_people(items):
    """Separate people from Role objects and index roles by person URI."""
    people = []
    roles_by_person = {}

    for item in items:
        if not isinstance(item, dict):
            continue

        types = get_type(item)

        if "Person" in types or (
            not types and (item.get("givenName") or item.get("familyName"))
        ):
            people.append(item)
            continue

        if "Role" not in types:
            continue

        person_id = (
            item.get("schema:author")
            or item.get("author")
            or item.get("contributor")
            or item.get("schema:contributor")
        )

        if not person_id:
            continue

        role_name = item.get("roleName") or item.get("schema:roleName")
        if role_name:
            roles_by_person.setdefault(person_id, []).append(role_name)

    return people, roles_by_person


def roles_for(person, roles_by_person):
    """Find roles associated with a person's identifier."""
    person_id = get_id(person)
    roles = roles_by_person.get(person_id, [])

    # Support metadata that stores roles directly on the person.
    direct_roles = person.get("roleName", [])
    if isinstance(direct_roles, str):
        direct_roles = [direct_roles]
    roles.extend(direct_roles)

    return list(dict.fromkeys(roles))


def yaml_string(value):
    """Serialize a string safely as a YAML double-quoted scalar."""
    return json.dumps(str(value), ensure_ascii=False)


def valid_iso_date(value):
    """Return an ISO date only if it is a real calendar date."""
    if not isinstance(value, str):
        return None

    try:
        parsed = date.fromisoformat(value)
    except ValueError:
        return None

    # Require YYYY-MM-DD, not a timestamp or another date representation.
    if parsed.isoformat() != value:
        return None

    return value


def get_license(metadata):
    """Extract an SPDX identifier from a license URL or string."""
    license_value = metadata.get("license")

    if not isinstance(license_value, str) or not license_value.strip():
        return None

    license_value = license_value.strip()
    parsed = urlparse(license_value)

    if parsed.scheme in ("http", "https"):
        identifier = parsed.path.rstrip("/").split("/")[-1]
    else:
        identifier = license_value

    # Avoid writing an arbitrary URL as if it were an SPDX identifier.
    if re.fullmatch(r"[A-Za-z0-9.+-]+", identifier):
        return identifier

    return None


def get_doi(metadata):
    """Return a DOI only when an actual DOI URL is supplied."""
    for key in ("doi", "identifier", "@id", "id"):
        value = metadata.get(key)

        if not isinstance(value, str):
            continue

        match = re.fullmatch(
            r"https?://(?:dx\.)?doi\.org/(10\.\d{4,9}/\S+)",
            value.strip(),
            re.IGNORECASE,
        )
        if match:
            return match.group(1)

    return None


def generate_authors_file(metadata, output_file=AUTHORS_FILE):
    """Generate AUTHORS.md from CodeMeta authors and contributors."""
    with output_file.open("w", encoding="utf-8", newline="\n") as file:
        file.write("# Authors and Contributors\n\n")

        for field, heading in (
            ("author", "Authors"),
            ("contributor", "Contributors"),
        ):
            entries = metadata.get(field, [])
            if isinstance(entries, dict):
                entries = [entries]
            if not isinstance(entries, list):
                continue

            people, roles_by_person = get_people(entries)

            if not people:
                continue

            file.write(f"## {heading}\n\n")

            for person in people:
                name = get_name(person) or "Name not provided"
                orcid = get_orcid(person)
                affiliation = get_affiliation(person)
                roles = roles_for(person, roles_by_person)

                file.write(f"- **{name}**")

                if orcid:
                    file.write(f" ([ORCID]({orcid}))")

                file.write("\n")

                if affiliation:
                    file.write(f"  - Affiliation: {affiliation}\n")

                if roles:
                    file.write(f"  - Role: {', '.join(roles)}\n")

                file.write("\n")


def generate_citation_file(metadata, output_file=CITATION_FILE):
    """Generate a Citation File Format (CFF) 1.2.0 file."""
    authors_data = metadata.get("author", [])
    if isinstance(authors_data, dict):
        authors_data = [authors_data]
    if not isinstance(authors_data, list):
        authors_data = []

    authors, _ = get_people(authors_data)

    if not authors:
        raise ValueError(
            "No Person entries found in CodeMeta 'author'; "
            "cannot generate a valid citation."
        )

    name = metadata.get("name")
    if not isinstance(name, str) or not name.strip():
        raise ValueError("Missing required CodeMeta field: name")

    lines = [
        "cff-version: 1.2.0",
        "message: "
        + yaml_string("If you use this software, please cite it as follows."),
        "title: " + yaml_string(name),
        "authors:",
    ]

    for person in authors:
        given = person.get("givenName", "").strip()
        family = person.get("familyName", "").strip()
        orcid = get_orcid(person)
        affiliation = get_affiliation(person)

        author_lines = []

        if given:
            author_lines.append("  - given-names: " + yaml_string(given))
        else:
            author_lines.append("  -")

        if family:
            author_lines.append("    family-names: " + yaml_string(family))

        if orcid:
            author_lines.append("    orcid: " + yaml_string(orcid))
        if affiliation:
            author_lines.append("    affiliation: " + yaml_string(affiliation))

        lines.extend(author_lines)

    version = metadata.get("version")
    if isinstance(version, str) and version.strip():
        lines.append("version: " + yaml_string(version))

    released = valid_iso_date(metadata.get("datePublished"))
    if released:
        lines.append("date-released: " + yaml_string(released))
    elif metadata.get("datePublished"):
        print(
            "WARNING: datePublished is not a valid YYYY-MM-DD date; "
            "date-released omitted.",
            file=sys.stderr,
        )

    license_id = get_license(metadata)
    if license_id:
        lines.append("license: " + yaml_string(license_id))

    doi = get_doi(metadata)
    if doi:
        lines.append("doi: " + yaml_string(doi))

    repository = metadata.get("codeRepository")
    if isinstance(repository, str) and repository.strip():
        lines.append("repository-code: " + yaml_string(repository))

    with output_file.open("w", encoding="utf-8", newline="\n") as file:
        file.write("\n".join(lines) + "\n")


def main():
    try:
        with CODEMETA_FILE.open("r", encoding="utf-8") as file:
            metadata = json.load(file)

        if not isinstance(metadata, dict):
            raise ValueError("The CodeMeta document must be a JSON object.")

        generate_authors_file(metadata)
        generate_citation_file(metadata)

    except (OSError, json.JSONDecodeError, ValueError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        sys.exit(1)

    print(f"Generated {AUTHORS_FILE} and {CITATION_FILE}.")


if __name__ == "__main__":
    main()
