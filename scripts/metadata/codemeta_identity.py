#!/usr/bin/env python3
"""
@file codemeta_identity.py
@brief Resolve Git identities to the people declared in ``codemeta.json``.

Git author names are often nicknames, aliases or abbreviations. The names
published in generated files must be the ``givenName`` / ``familyName`` pairs
declared in CodeMeta. The e-mail address is the matching key between a Git
identity and a CodeMeta person.

The module only reads CodeMeta; it never modifies it.

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

import json
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

PEOPLE_FIELDS = ("author", "contributor", "maintainer")


@dataclass
class Identity:
    """A person declared in CodeMeta, identified by e-mail address."""

    given: str
    family: str
    email: str
    affiliations: list[str] = field(default_factory=list)

    @property
    def full_name(self) -> str:
        """Return ``givenName familyName`` exactly as written in CodeMeta."""
        return " ".join(part for part in (self.given, self.family) if part)

    @property
    def affiliation(self) -> str:
        return "; ".join(self.affiliations)


def as_list(value: Any) -> list[Any]:
    if value is None:
        return []
    return value if isinstance(value, list) else [value]


def get_value(data: dict[str, Any], *keys: str) -> Any:
    """Return the first non-null value among ``keys`` (with or without prefix)."""
    for key in keys:
        if key in data and data[key] is not None:
            return data[key]
    return None


def clean_text(value: Any) -> str:
    return value.strip() if isinstance(value, str) else ""


def normalize_email(email: Any) -> str:
    return clean_text(email).casefold()


def load_codemeta(path: Path) -> dict[str, Any]:
    with path.open(encoding="utf-8-sig") as stream:
        data = json.load(stream)

    if not isinstance(data, dict):
        raise ValueError("codemeta.json must contain a JSON object.")

    return data


def is_role(item: Any) -> bool:
    if not isinstance(item, dict):
        return False

    types = as_list(get_value(item, "@type", "type"))
    return any(str(value).split(":")[-1] == "Role" for value in types)


def given_family(item: dict[str, Any]) -> tuple[str, str]:
    given = clean_text(get_value(item, "givenName", "schema:givenName"))
    family = clean_text(get_value(item, "familyName", "schema:familyName"))
    return given, family


def full_name_of(item: dict[str, Any]) -> str:
    """Return ``givenName familyName`` for a CodeMeta person or organization.

    Falls back to ``name`` only when no given or family name is available.
    """
    given, family = given_family(item)

    if given or family:
        return " ".join(part for part in (given, family) if part)

    return clean_text(get_value(item, "name", "schema:name"))


def affiliations_of(item: dict[str, Any]) -> list[str]:
    result: list[str] = []

    for affiliation in as_list(get_value(item, "affiliation", "schema:affiliation")):
        if isinstance(affiliation, dict):
            name = clean_text(get_value(affiliation, "name", "legalName"))
        else:
            name = clean_text(affiliation)

        if name and name not in result:
            result.append(name)

    return result


def emails_of(item: dict[str, Any]) -> list[str]:
    emails: list[str] = []

    for value in as_list(get_value(item, "email", "schema:email")):
        email = clean_text(value)
        if email and email not in emails:
            emails.append(email)

    return emails


def load_identities(metadata: dict[str, Any]) -> dict[str, Identity]:
    """Index every CodeMeta person by normalized e-mail address.

    People are read from ``author``, ``contributor`` and ``maintainer``.
    ``Role`` objects are ignored. A person who appears several times
    (for example author and maintainer) is merged into one identity.
    """
    identities: dict[str, Identity] = {}

    for field_name in PEOPLE_FIELDS:
        for item in as_list(get_value(metadata, field_name, f"schema:{field_name}")):
            if not isinstance(item, dict) or is_role(item):
                continue

            given, family = given_family(item)
            if not (given or family):
                given = clean_text(get_value(item, "name", "schema:name"))

            if not (given or family):
                continue

            affiliations = affiliations_of(item)

            for email in emails_of(item):
                key = normalize_email(email)
                identity = identities.get(key)

                if identity is None:
                    identities[key] = Identity(
                        given=given,
                        family=family,
                        email=email,
                        affiliations=list(affiliations),
                    )
                    continue

                for affiliation in affiliations:
                    if affiliation not in identity.affiliations:
                        identity.affiliations.append(affiliation)

    return identities


def copyright_holders(metadata: dict[str, Any]) -> list[str]:
    """Return the full names of the copyright holders declared in CodeMeta."""
    holders: list[str] = []

    for holder in as_list(get_value(metadata, "copyrightHolder", "schema:copyrightHolder")):
        name = full_name_of(holder) if isinstance(holder, dict) else clean_text(holder)

        if name and name not in holders:
            holders.append(name)

    return holders
