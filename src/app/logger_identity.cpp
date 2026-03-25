/**
 * @file logger_identity.cpp
 * @brief Factory identity storage and retrieval using SAMD21 internal flash.
 *
 * This module stores a small "factory identity" record in the MCU internal flash
 * using the Arduino `FlashStorage` library (cmaglie/FlashStorage).
 *
 * The stored record contains:
 * - a magic value (to detect presence),
 * - a version (to allow future format evolution),
 * - a CRC16-CCITT checksum (to validate integrity),
 * - a `LoggerIdentityFlash` payload (strings such as manufacturer, type, serial...).
 *
 * On initialization, the module tries to load and validate the record from flash.
 * If missing/invalid, it writes a default identity.
 *
 * @note The identity payload is cached in RAM after initialization for fast access.
 * @warning Flash has limited write endurance; do not call programming functions
 *          at high frequency.
 */

#include "logger_identity.h"

#include <string.h>
#include "log.h"
#include <FlashStorage.h>  // provided by cmaglie/FlashStorage

// ---- Flash record format ----------------------------------------------------

/**
 * @brief Magic value used to detect a valid identity record in flash.
 *
 * ASCII: 'MRKI'
 */
static constexpr uint32_t ID_MAGIC = 0x4D524B49UL;  // 'MRKI'

/**
 * @brief Identity record version stored in flash.
 *
 * Increment this if the `IdentityRecord` payload format changes.
 */
static constexpr uint16_t ID_VERSION = 0x0001;

/**
 * @brief On-flash identity record wrapper.
 *
 * The CRC is computed only over the `id` payload (not including header fields).
 */
struct IdentityRecord {
    uint32_t magic;         /**< Magic marker (must be ID_MAGIC). */
    uint16_t version;       /**< Record version (must be ID_VERSION). */
    uint16_t crc16;         /**< CRC16-CCITT computed over @ref id payload. */
    LoggerIdentityFlash id; /**< Factory identity payload. */
};

/**
 * @brief Ensure the record fits within one flash row/page used by FlashStorage.
 *
 * On SAMD21, FlashStorage typically stores objects in flash pages/rows; this check
 * enforces the record remains compact.
 */
static_assert(sizeof(IdentityRecord) <= 256,
              "IdentityRecord too large for a single flash row/page; keep it small.");

/**
 * @brief FlashStorage instance used to persist the identity record.
 */
FlashStorage(moonraker_identity_store, IdentityRecord);

// ---- RAM cache --------------------------------------------------------------

/**
 * @brief Cached identity in RAM.
 *
 * This cache is loaded from flash at init or filled with defaults if flash is invalid.
 */
static LoggerIdentityFlash g_identity{};

/**
 * @brief Indicates whether @ref g_identity is valid.
 */
static bool g_identity_valid = false;

// ---- Small helpers ----------------------------------------------------------

/**
 * @brief Compute CRC16-CCITT (poly 0x1021, init 0xFFFF) over a byte buffer.
 *
 * @param data Pointer to input buffer.
 * @param len  Number of bytes to process.
 * @return CRC16-CCITT checksum.
 */
static uint16_t crc16_ccitt(const uint8_t* data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (uint8_t b = 0; b < 8; b++) crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : (crc << 1);
    }
    return crc;
}

/**
 * @brief Copy a C-string safely into a fixed-size destination buffer.
 *
 * Ensures null-termination and handles null pointers.
 *
 * @param dst     Destination buffer.
 * @param dstSize Destination buffer size in bytes.
 * @param src     Source string (may be null).
 */
static void safeCopy(char* dst, size_t dstSize, const char* src) {
    if (!dst || dstSize == 0) return;
    if (!src) src = "";
    strncpy(dst, src, dstSize - 1);
    dst[dstSize - 1] = '\0';
}

/**
 * @brief Fill a @ref LoggerIdentityFlash structure with default values.
 *
 * The defaults are meant to be a safe baseline if flash does not contain a valid record.
 *
 * @param id Identity payload to fill.
 */
static void fillDefaults(LoggerIdentityFlash& id) {
    memset(&id, 0, sizeof(id));
    safeCopy(id.manufacturer, sizeof(id.manufacturer), "CNRS");
    safeCopy(id.logger_type, sizeof(id.logger_type), "Moonraker");
    safeCopy(id.date_fab, sizeof(id.date_fab), "2025-01-01");
    safeCopy(id.serial_number, sizeof(id.serial_number), "UNKNOWN");
}

/**
 * @brief Build a complete flash record from an identity payload.
 *
 * @param id Identity payload to embed.
 * @return Fully formed record including magic, version and CRC.
 */
static IdentityRecord makeRecord(const LoggerIdentityFlash& id) {
    IdentityRecord r{};
    r.magic   = ID_MAGIC;
    r.version = ID_VERSION;
    r.id      = id;
    r.crc16   = crc16_ccitt(reinterpret_cast<const uint8_t*>(&r.id), sizeof(r.id));
    return r;
}

/**
 * @brief Validate a flash record (magic, version and CRC).
 *
 * @param r Record to validate.
 * @return true if record is valid and matches current format.
 */
static bool recordValid(const IdentityRecord& r) {
    if (r.magic != ID_MAGIC) return false;
    if (r.version != ID_VERSION) return false;
    const uint16_t crc = crc16_ccitt(reinterpret_cast<const uint8_t*>(&r.id), sizeof(r.id));
    return (crc == r.crc16);
}

/**
 * @brief Ensure all identity string fields are properly null-terminated.
 *
 * This prevents issues if flash contents are corrupted or if the payload was not
 * written with strict string termination.
 *
 * @param id Identity payload to sanitize (in-place).
 */
static void sanitize(LoggerIdentityFlash& id) {
    id.manufacturer[sizeof(id.manufacturer) - 1]   = '\0';
    id.logger_type[sizeof(id.logger_type) - 1]     = '\0';
    id.date_fab[sizeof(id.date_fab) - 1]           = '\0';
    id.serial_number[sizeof(id.serial_number) - 1] = '\0';
}

// ---- Public API -------------------------------------------------------------

/**
 * @brief Initialize the factory identity subsystem.
 *
 * Reads the identity record from flash and validates it. If it is missing or invalid,
 * the function writes a default identity record and verifies it via a readback check.
 *
 * @return true if a valid identity is available after initialization, false otherwise.
 *
 * @note On success, the identity is cached in RAM and can be accessed via
 *       @ref device_id_get.
 */
bool loggerIdentity_init() {
    const IdentityRecord r = moonraker_identity_store.read();

    if (recordValid(r)) {
        g_identity = r.id;
        sanitize(g_identity);
        g_identity_valid = true;
        LOG_INFO("Factory identity loaded from FLASH: SN=%s", g_identity.serial_number);
        return true;
    }

    LOG_WARN("Factory identity not found/invalid in FLASH — writing defaults.");
    fillDefaults(g_identity);

    const IdentityRecord wr = makeRecord(g_identity);
    moonraker_identity_store.write(wr);

    // readback check
    const IdentityRecord rb = moonraker_identity_store.read();
    if (!recordValid(rb)) {
        LOG_ERROR("FLASH identity write failed (readback invalid).");
        g_identity_valid = false;
        return false;
    }

    g_identity_valid = true;
    LOG_INFO("Default factory identity written to FLASH (SN=%s)", g_identity.serial_number);
    return true;
}

/**
 * @brief Get the current factory identity.
 *
 * @return Reference to the cached identity payload.
 *
 * @warning If called before a successful @ref loggerIdentity_init, this function
 *          will populate RAM defaults and mark the cache valid (without writing flash).
 */
const LoggerIdentityFlash& device_id_get() {
    if (!g_identity_valid) {
        LOG_WARN("device_id_get() called before successful init — using RAM defaults");
        fillDefaults(g_identity);
        g_identity_valid = true;
    }
    return g_identity;
}

/**
 * @brief Program a new factory identity into flash.
 *
 * The payload is sanitized (string termination) before being wrapped into a record,
 * written to flash, and verified via readback validation.
 *
 * @param id New identity payload to store.
 * @return true if programming and readback verification succeeded, false otherwise.
 *
 * @warning Flash has limited endurance; avoid frequent calls.
 */
bool loggerIdentity_program(const LoggerIdentityFlash& id) {
    LoggerIdentityFlash tmp = id;
    sanitize(tmp);

    const IdentityRecord wr = makeRecord(tmp);
    moonraker_identity_store.write(wr);

    const IdentityRecord rb = moonraker_identity_store.read();
    if (!recordValid(rb)) {
        LOG_ERROR("Factory identity programming failed (readback invalid).");
        return false;
    }

    g_identity = rb.id;
    sanitize(g_identity);
    g_identity_valid = true;

    LOG_INFO("Factory identity programmed to FLASH: SN=%s", g_identity.serial_number);
    return true;
}

/**
 * @brief Reset the factory identity to default values and program them into flash.
 *
 * @return true if defaults were programmed successfully, false otherwise.
 */
bool loggerIdentity_resetDefaults() {
    LoggerIdentityFlash def{};
    fillDefaults(def);
    return loggerIdentity_program(def);
}

static void copyField_(char* dst, size_t dstSize, const char* src) {
    if (!dst || dstSize == 0) return;
    if (!src) src = "";
    strncpy(dst, src, dstSize - 1);
    dst[dstSize - 1] = '\0';
}

void loggerIdentity_applyFromFields(const char* manufacturer, const char* logger_type,
                                    const char* date_fab, const char* serial_number) {
    // Start from existing identity, then override fields
    LoggerIdentityFlash id = device_id_get();  // copy

    copyField_(id.manufacturer, sizeof(id.manufacturer), manufacturer);
    copyField_(id.logger_type, sizeof(id.logger_type), logger_type);
    copyField_(id.date_fab, sizeof(id.date_fab), date_fab);
    copyField_(id.serial_number, sizeof(id.serial_number), serial_number);

    loggerIdentity_program(id);
}
