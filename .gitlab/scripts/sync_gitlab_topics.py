#!/usr/bin/env python3

# Synchronize GitLab project topics with codemeta.json.

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

api_url = f"{os.environ['CI_API_V4_URL']}" f"/projects/{os.environ['CI_PROJECT_ID']}"

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
