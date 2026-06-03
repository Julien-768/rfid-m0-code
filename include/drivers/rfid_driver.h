/**
 * @file rfid_driver.h
 * @brief Non-blocking RFID driver with autonomous line reader, parsing,
 * decoding, FIFO queue, and explicit polling control.
 *
 * Design goals:
 *  - No global RX state: each driver instance owns its own line reader.
 *  - Multi-port: create multiple rfid_driver_t instances (Serial1, Serial2, ...).
 *  - Non-blocking: call rfid_driver::tick() frequently from loop().
 *  - Robust parsing: CR-terminated line, overflow discard mode,
 *    sanitize + validate before decode.
 *  - Explicit polling control: support both periodic polling and
 *    immediate polling on demand.
 *
 * @author julien courtecuisse
 * @date 2026-02-24
 */

#ifndef RFID_DRIVER_H
#define RFID_DRIVER_H

#ifndef ARDUINO_ARCH_SAMD
#warning "This driver was validated on Feather M0 (SAMD21)"
#endif

#include <Arduino.h>
#include <stddef.h>

/** Supported RFID tag types. Extend as needed. */
typedef enum : uint8_t {
    TAG_TYPE_HDX = 0,
    TAG_TYPE_FDX,
    TAG_TYPE_EM4102,
    TAG_TYPE_OTHER
} tag_type_t;

/** A parsed/decoded tag plus a timestamp in milliseconds. */
typedef struct {
    char tag[32];
    uint32_t time_ms;
} tag_info_t;

/**
 * @brief Autonomous CR-terminated line reader state structure.
 *
 * Handles:
 *  - Ignoring '\n'
 *  - Terminating lines on '\r'
 *  - Overflow detection with discard-until-CR behavior
 */
typedef struct {
    char buf[64];
    size_t len;
    bool discarding;
    bool line_ready;
} line_reader_t;

/**
 * @brief Internal RFID driver instance state.
 *
 * Contains:
 *  - Serial port reference
 *  - Tag type configuration
 *  - Poll timing management
 *  - Line reader
 *  - FIFO queue for decoded tags
 */
typedef struct {
    Stream* port;
    tag_type_t type;

    uint32_t last_poll;
    uint32_t poll_interval_ms;

    line_reader_t lr;

    enum : uint8_t { QSIZE = 4 };
    tag_info_t q[QSIZE];
    uint8_t head;
    uint8_t tail;
    uint8_t count;
} rfid_driver_t;

/**
 * @brief Static RFID driver API.
 *
 * This class provides a namespace-like API for operating on an
 * rfid_driver_t instance:
 *
 * @code
 * rfid_driver_t drv;
 * rfid_driver::init(&drv, &RFID_SERIAL, TAG_TYPE_FDX, 200);
 * rfid_driver::poll_now(&drv);
 * rfid_driver::tick(&drv);
 * @endcode
 */
class rfid_driver {
   public:
    /**
     * @brief Initialize a driver instance.
     *
     * @param drv               Driver instance storage (must be non-null).
     * @param port              Stream to use (Serial1, Serial2, SoftwareSerial, etc.).
     * @param type              Tag type (affects polling command and decoding).
     * @param poll_interval_ms  Poll interval in milliseconds.
     */
    static void init(rfid_driver_t* drv, Stream* port, tag_type_t type, uint32_t poll_interval_ms);

    /**
     * @brief Prepare the RFID UART before powering the reader.
     *
     * Restarts the hardware serial port and flushes stale RX bytes so the
     * reader boot banner can be captured immediately after power-up.
     *
     * @param serial Hardware serial port connected to the RFID reader.
     */
    static void prepare_serial(HardwareSerial* serial);

    /**
     * @brief Non-blocking driver tick. Call as often as possible from loop().
     *
     * Responsibilities:
     *  - Send polling command periodically.
     *  - Read up to a small fixed number of lines per tick.
     *  - Sanitize, validate, and decode input lines.
     *  - Push decoded tags into the internal FIFO queue.
     *
     * @param drv Driver instance (must be initialized).
     */
    static void tick(rfid_driver_t* drv);

    /**
     * @brief Send a poll command immediately.
     *
     * This bypasses the periodic poll timer and writes the command directly
     * to the configured serial stream. It is useful right after powering
     * up the RFID reader when an immediate read request is needed.
     *
     * @param drv Driver instance (must be initialized).
     */
    static void poll_now(rfid_driver_t* drv);

    /**
     * @brief Clear all pending bytes from the RFID receive stream.
     *
     * @param drv Driver instance.
     */
    static void flush_rx(rfid_driver_t* drv);

    /**
     * @brief Start the RFID driver on a hardware serial port.
     *
     * @param drv               Driver instance storage.
     * @param serial            Hardware serial port connected to the RFID reader.
     * @param type              RFID tag type.
     * @param poll_interval_ms  Poll interval in milliseconds.
     */
    static void start(rfid_driver_t* drv,
                      HardwareSerial* serial,
                      tag_type_t type,
                      uint32_t poll_interval_ms);

    /**
    * @brief Stop the RFID driver processing state.
    *
    * Detaches the stream from the driver instance and resets internal parser/FIFO
    * state. This does not stop the underlying UART peripheral.
    *
    * Use @ref release_serial() when UART ownership must be released.
    *
    * @param drv Driver instance.
    */
    static void stop(rfid_driver_t* drv);

    /**
     * @brief Wait for the RFID reader boot marker.
     *
     * Waits for the TECTUS startup marker "BTboot" after reader power-up.
     * If the firmware banner is received, it is logged for diagnostics.
     *
     * @param serial Serial stream connected to the RFID reader.
     * @param timeout_ms Maximum time to wait for the boot marker, in milliseconds.
     * @return true if the boot marker was received, false otherwise.
     */
    static bool wait_for_boot_message(Stream* serial, uint32_t timeout_ms);

    /**
     * @brief Pop one ready tag from the FIFO.
     *
     * @param drv Driver instance.
     * @param out Output tag structure (must be non-null).
     * @return true if a tag was returned, false if FIFO is empty.
     */
    static bool get_tag(rfid_driver_t* drv, tag_info_t* out);

    /**
     * @brief Determine whether a tag should be recorded based on debounce delay.
     *
     * Logic:
     *  - If current.tag equals previous.tag, accept only if dt >= delay_ms.
     *  - If the tag differs, accept immediately.
     *
     * @param previous Previous accepted tag.
     * @param current  Current tag candidate.
     * @param delay_ms Minimum delay between identical tags.
     * @return true if the tag should be recorded, false otherwise.
     */
    static bool should_record_tag(const tag_info_t* previous,
                                  const tag_info_t* current,
                                  uint32_t delay_ms);

    /**
    * @brief Release the RFID UART serial interface.
    *
    * Stops the UART and detaches the serial port from the RFID driver.
    *
    * @param drv Pointer to RFID driver instance.
    */
    static void release_serial(rfid_driver_t* drv);
};

#endif  // RFID_DRIVER_H
