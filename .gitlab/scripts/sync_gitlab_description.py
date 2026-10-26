#!/usr/bin/env python3

# This script synchronizes the GitLab project description with the description found in codemeta.json.


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
