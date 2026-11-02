#!/usr/bin/env python3
"""
@file sync_gitlab_description.py
@brief This script synchronizes the GitLab project description with the description found in codemeta.json.

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

import json
import os
from urllib.parse import urlencode
from urllib.request import Request, urlopen

with open("codemeta.json", encoding="utf-8") as file:
    metadata = json.load(file)

description = metadata.get("description", "").strip()

if not description:
    raise SystemExit("ERROR: codemeta.json has no description.")

url = f"{os.environ['CI_API_V4_URL']}" f"/projects/{os.environ['CI_PROJECT_ID']}"

request = Request(
    url,
    data=urlencode({"description": description}).encode("utf-8"),
    headers={
        "PRIVATE-TOKEN": os.environ["GITLAB_API_TOKEN"],
        "Content-Type": "application/x-www-form-urlencoded",
    },
    method="PUT",
)

with urlopen(request, timeout=30) as response:
    result = json.load(response)

print("GitLab project description synchronized.")
print(f"Description: {result.get('description', '')}")
