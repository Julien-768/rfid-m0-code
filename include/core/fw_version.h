/**
 * @file fw_version.h
 * @brief Firmware version definitions.
 *
 * This file provides the firmware version string used by the logger and GUI.
 *
 * In CI/tag builds, this file may be auto-generated from the Git tag.
 * Local development builds use the default development version below.
 */

#pragma once

#ifndef FW_VERSION_STRING
#define FW_VERSION_STRING "0.0.0-dev"
#endif

#ifndef FW_GIT_HASH
#define FW_GIT_HASH "dev"
#endif

#ifndef FW_BUILD_DATE
#define FW_BUILD_DATE __DATE__ " " __TIME__
#endif
