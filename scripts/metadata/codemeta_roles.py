#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2022 Julien Courtecuisse
# SPDX-License-Identifier: GPL-3.0-or-later

"""
@file codemeta_roles.py
@brief Extract dated CodeMeta Role objects without changing codemeta_people.py.

The existing codemeta_people module remains responsible for person identity and
merging. This module only adds the temporal information carried by Role objects.

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

from dataclasses import dataclass
from datetime import date
from typing import Any, Iterable

import codemeta_people as cp

ROLE_TARGET_KEYS = ("schema:author", "author", "schema:contributor", "contributor")
ROLE_NAME_KEYS = ("roleName", "schema:roleName")
IDENTITY_FIELDS = ("@id", "id", "identifier", "orcid", "sameAs", "email")
START_KEYS = ("schema:startDate", "startDate")
END_KEYS = ("schema:endDate", "endDate")


@dataclass(frozen=True)
class RolePeriod:
    name: str
    start_date: str | None = None
    end_date: str | None = None
    source: str = ""

    @property
    def label(self) -> str:
        if self.start_date and self.end_date:
            return f"{self.start_date} – {self.end_date}"
        if self.start_date:
            return f"Since {self.start_date}"
        if self.end_date:
            return f"Until {self.end_date}"
        return "Period not specified"


def _strings(value: Any) -> Iterable[str]:
    if isinstance(value, str):
        if value.strip():
            yield value.strip()
    elif isinstance(value, list):
        for item in value:
            yield from _strings(item)
    elif isinstance(value, dict):
        for key in ("@id", "id", "@value", "value", "url"):
            yield from _strings(value.get(key))


def _identity_keys(value: Any) -> set[str]:
    values: list[str] = []
    if isinstance(value, dict):
        for field in IDENTITY_FIELDS:
            values.extend(_strings(value.get(field)))
    else:
        values.extend(_strings(value))
    return {key for text in values if (key := cp.identity_key(text))}


def _date_value(
    item: dict[str, Any],
    keys: tuple[str, ...],
    where: str,
    issues: list[str],
) -> str | None:
    raw = next((item[key] for key in keys if key in item), None)
    if raw is None:
        return None
    if not isinstance(raw, str):
        issues.append(f"{where}: {keys[0]} must be an ISO date string; ignored.")
        return None
    try:
        parsed = date.fromisoformat(raw)
    except ValueError:
        issues.append(f"{where}: invalid date {raw!r}; ignored.")
        return None
    if parsed.isoformat() != raw:
        issues.append(f"{where}: date {raw!r} must use YYYY-MM-DD; ignored.")
        return None
    return raw


def load_role_periods(
    metadata: dict[str, Any],
    people: cp.People,
) -> tuple[dict[str, list[RolePeriod]], list[str]]:
    """Return role periods indexed by each matching person's identity keys.

    Keys in the returned mapping use the canonical form produced by
    ``codemeta_people.identity_key``. The person parser remains the authority
    for resolving people; this function only enriches those known identities.
    """
    issues: list[str] = []
    result: dict[str, list[RolePeriod]] = {}
    known_people = people.authors + people.contributors
    known_keys = (
        set().union(*(set(p.keys) for p in known_people)) if known_people else set()
    )

    for field in ("author", "contributor"):
        for index, entry in enumerate(cp.as_list(metadata.get(field))):
            if not isinstance(entry, dict):
                continue
            types = cp.type_names(entry)
            looks_like_role = (
                not types
                and any(key in entry for key in ROLE_TARGET_KEYS)
                and any(key in entry for key in ROLE_NAME_KEYS)
            )
            if "Role" not in types and not looks_like_role:
                continue

            where = f"{field}[{index}]"
            names: list[str] = []
            for key in ROLE_NAME_KEYS:
                names.extend(_strings(entry.get(key)))
            names = list(dict.fromkeys(names))
            if not names:
                continue

            target_keys: set[str] = set()
            for key in ROLE_TARGET_KEYS:
                target_keys.update(_identity_keys(entry.get(key)))
            matched = target_keys & known_keys
            if not matched:
                issues.append(f"{where}: dated Role does not match a declared Person.")
                continue

            start = _date_value(entry, START_KEYS, where, issues)
            end = _date_value(entry, END_KEYS, where, issues)
            if start and end and end < start:
                issues.append(f"{where}: end date precedes start date; period ignored.")
                continue

            for key in matched:
                bucket = result.setdefault(key, [])
                for name in names:
                    period = RolePeriod(
                        name=name,
                        start_date=start,
                        end_date=end,
                        source=where,
                    )
                    if period not in bucket:
                        bucket.append(period)

    return result, issues


def periods_for(
    person: cp.Person,
    periods: dict[str, list[RolePeriod]],
) -> list[RolePeriod]:
    """Return de-duplicated periods associated with ``person``."""
    found: list[RolePeriod] = []
    for key in person.keys:
        for period in periods.get(key, []):
            if period not in found:
                found.append(period)
    return found
