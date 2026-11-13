#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2022 Julien Courtecuisse
# SPDX-License-Identifier: GPL-3.0-or-later

"""
@file sync_gitlab_topics.py
@brief Synchronize GitLab project topics with codemeta.json.

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

import json
import os
import re
import unicodedata
from urllib.request import Request, urlopen

CODEMETA_FILE = "codemeta.json"

# CodeMeta properties used to generate GitLab topics.
TOPIC_FIELDS = (
    "keywords",
    "applicationCategory",
    "programmingLanguage",
)


def extract_values(value):
    """Extract text values from common CodeMeta property formats."""
    if isinstance(value, str):
        # CodeMeta keywords are commonly comma-separated.
        return [item.strip() for item in re.split(r"[,;]", value) if item.strip()]

    if isinstance(value, list):
        values = []
        for item in value:
            values.extend(extract_values(item))
        return values

    if isinstance(value, dict):
        # Handle language or category objects when present.
        for key in ("name", "alternateName"):
            if isinstance(value.get(key), str):
                return extract_values(value[key])

    return []


def normalize_topic(value):
    """Convert a value into a GitLab-friendly topic slug."""
    value = unicodedata.normalize("NFKD", value)
    value = value.encode("ascii", "ignore").decode("ascii")
    value = value.lower().strip()
    value = re.sub(r"[^a-z0-9]+", "-", value)
    return value.strip("-")


with open(CODEMETA_FILE, encoding="utf-8") as file:
    metadata = json.load(file)

topics = []
seen = set()

for field in TOPIC_FIELDS:
    for value in extract_values(metadata.get(field)):
        topic = normalize_topic(value)

        if topic and topic not in seen:
            topics.append(topic)
            seen.add(topic)

if not topics:
    raise SystemExit("ERROR: no topics found in codemeta.json.")

api_url = f"{os.environ['CI_API_V4_URL']}/projects/{os.environ['CI_PROJECT_ID']}"

payload = json.dumps({"topics": topics}).encode("utf-8")

request = Request(
    api_url,
    data=payload,
    headers={
        "PRIVATE-TOKEN": os.environ["GITLAB_API_TOKEN"],
        "Content-Type": "application/json",
    },
    method="PUT",
)

with urlopen(request, timeout=30) as response:
    result = json.load(response)

print("GitLab project topics synchronized.")
print("Topics:", ", ".join(result.get("topics", [])))
