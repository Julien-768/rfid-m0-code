#!/usr/bin/env sh
set -eu

OUT_FILE="include/core/fw_version.h"

cat > "$OUT_FILE" <<EOF
#pragma once

#define FW_VERSION_STRING "${CI_COMMIT_TAG}"
#define FW_GIT_HASH "${CI_COMMIT_SHORT_SHA}"
#define FW_BUILD_DATE "${CI_JOB_STARTED_AT}"
EOF
