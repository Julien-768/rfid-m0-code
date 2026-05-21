/**
 * @file main.cpp
 * @defgroup MainApplication GreatTitCore Application
 * @ingroup SystemModules
 * @brief Main control logic and global runtime state machine of the Great Tit Logger.
 *
 * The **Main Application** coordinates all core modules of the autonomous
 * logger platform. It implements the global system state machine, manages
 * low-power transitions, and controls shared hardware resources depending on
 * the active runtime mode.
 *
 * ## Responsibilities
 * - Initialize and coordinate hardware subsystems:
 *   - SD card
 *   - RTC (DS3231)
 *   - Configuration and assembly metadata
 *   - Battery monitor
 *   - Sensors
 *   - RFID reader
 *
 * - Execute boot-time diagnostics and detect hardware failures.
 *
 * - Manage operational modes via a global state machine:
 *   - **CONNECTED** → Temporary UART/GUI configuration window after boot.
 *   - **INIT** → Runtime hardware initialization before deployment.
 *   - **DEPLOY** → Periodic low-power autonomous acquisition mode.
 *   - **STOCK** → Storage/idle state before deployment.
 *   - **END-OF-LIFE** → Safe shutdown on critical error or low battery.
 *
 * - Dynamically assign shared hardware interfaces depending on runtime mode.
 *
 * ## UART ownership model
 * The hardware UART @c Serial1 is dynamically shared between:
 *
 * - GUI communication during @ref STATE_CONNECTED
 * - RFID reader communication during @ref STATE_DEPLOY
 *
 * The RFID driver explicitly acquires and releases Serial1 ownership using
 * @ref rfid_driver::start() and @ref rfid_driver::stop().
 *
 * RFID communication is only enabled during active RFID usage windows after
 * IR-triggered wake events.
 *
 * ## Boot model
 * The boot sequence is executed once in @c setup() via
 * @ref runBootSequence().
 *
 * After boot:
 * - The logger first enters @ref STATE_CONNECTED for a temporary GUI/UART
 *   configuration window.
 * - If no GUI activity is detected before timeout expiration, the system
 *   automatically transitions to @ref STATE_INIT.
 * - @ref STATE_INIT initializes deployment runtime services and enters
 *   @ref STATE_DEPLOY.
 *
 * ## Power management
 * - Uses the ArduinoLowPower library for SAMD21 standby management.
 * - Wakes up periodically via DS3231 alarm interrupts.
 * - Minimizes SD writes using buffered logging when enabled.
 * - Powers the RFID reader only during active acquisition windows.
 *
 * @see sd_manager.h
 * @see rtc.h
 * @see battery.h
 * @see system_state.h
 * @see log.h
 * @see rfid_driver.h
 *
 * @{
 */

#include <ArduinoLowPower.h>
#include <Arduino.h>
#include <SD.h>
#include "hardware.h"
#include "rtc.h"
#include "config.h"
#include "assembly.h"
#include "sd_manager.h"
#include "Wire.h"

#include "system_state.h"
#include "log.h"
#include "error_handler.h"
#include "connected_mode.h"
#include "det_ext.h"
#include "deploy_mode.h"
#include "sensors.h"
#include "sensors_internal.h"
#include "logger_identity.h"
#include "mcu_uid.h"
#include "utils.h"
#include "battery.h"
#include "battery_service.h"
#include "ir_pwm.h"
#include "signal.h"
#include "rfid_driver.h"
#include "pwr_manager.h"
#include "gui_serial.h"

SystemState currentState = STATE_INIT;
static bool i2c_ok       = false;
bool rtc_available       = false;
String string_widget     = "------------------------------------------------------------";

void logWidgetTwice() {
    LOG_INFO(string_widget.c_str());
    LOG_INFO(string_widget.c_str());
}

/**
 * @brief IR PWM driver instance
 */
ir_pwm ir_driver(PIN_PWM_IR, PIN_PR_1, PIN_PR_2);

/**
 * @brief RFID driver instance
 */
rfid_driver_t rfid_driver;

/**
 * @brief Execute the full hardware initialization sequence at startup.
 *
 **/
static SystemState runBootSequence() {

    logWidgetTwice();

    static bool ir_enabled = true;
    delay(2000);  // Allow time for peripherals to stabilize (e.g., SD card)

    LOG_INFO("Boot sequence started");
    String buildDateTime = "Build: " + String(F(__DATE__)) + " " + String(F(__TIME__));
    LOG_INFO(buildDateTime.c_str());
#ifdef __PIO_BOARD_NAME__
    String board = "Board: " + String(__PIO_BOARD_NAME__);
#else
    String board = "Board: unknown";
#endif
    LOG_INFO(board.c_str());
    log_flush();

    /*
    Initialize the built-in LED for visual feedback during boot.
    */
    signal_engine_init(PIN_BUZZER_LED, PIN_BUZZER_LED);
    // led_start_blink_isr(3, blink_mode::fast);

    /*
    Initialize SD card
     */
    LOG_INFO(string_widget.c_str());
    LOG_INFO("Initializing SD card");
    if (!sd_initialization(PIN_SD_CS)) {
        return STATE_ENDOFLIFE;
    }

    // --- Load existing hw_assembly.cfg ---
    if (!assembly_load(hw_assembly)) {
        return STATE_ENDOFLIFE;
    }
    // initialize RTC for logging file creation and timestamping

    /*
    RTC initialization and sanity check
     */
    LOG_INFO(string_widget.c_str());
    if (hw_assembly.rtc_type == "ds3231" and i2c_ok == true) {
        LOG_INFO("RTC used\tDS3231");
        // // Register RTC ISR callback
        // rtc_set_alarm_callback(nullptr);
        // Initialize RTC
        if (!rtc_initialization(RTC_INTERRUPT_PIN)) {
            LOG_ERROR("RTC initialization failed");
            return STATE_ENDOFLIFE;
        }
        // Boot-time sanity check
        rtc_boot_recover();
        // // dev fix: should not be needed here as rtc_boot_recover() already sets a valid time if the RTC was lost, but just in case, ensure that the RTC is set to a valid time before proceeding with file creation and timestamping
        rtc_apply_external_time(DateTime(__DATE__, __TIME__));
        rtc_available = true;
    } else {
        LOG_WARN("RTC type not recognized or not specified. RTC features will be unavailable.");
        return STATE_ENDOFLIFE;  // TODO: consider allowing operation without RTC, but with limited functionality (e.g., limited timestamping, limited daily file management)
    }

    if (rtc_available) {
        /*
        Check and create the daily log file on SD card
        */
        // TODO: add rtc_available in rtc module
        DateTime now = rtc().now();
        if (!check_and_create_new_daily_file(now)) {
            LOG_ERROR("Failed to create daily log file at boot");
            return STATE_ENDOFLIFE;
        }
    } else {
        LOG_WARN("Skipping daily log file creation: no RTC available");
    }

    /*
    Load hardware assembly information (UIDs, etc.) and sync with SD card.
    */
    LOG_INFO(string_widget.c_str());
    // Read all hardware UIDs (in RAM only) ---
    bool uid_updated = false;
    if (hw_assembly.uid_mainboard == "$uid_mainboard$") {
        hw_assembly.uid_mainboard = mcu_uid_read();
        uid_updated               = true;
    }
    // if (hw_assembly.uid_light_sensor1 == "$uid_light_sensor1$")
    //     {
    //         hw_assembly.uid_light_sensor1 = readAS7341DeviceID();  // TODO read from sensor
    // uid_updated               = true;
    // }
    // if (hw_assembly.uid_light_sensor2 == "$uid_light_sensor2$")
    //     {
    //         hw_assembly.uid_light_sensor2 = readTSL2591DeviceID();  // TODO read from sensor
    // uid_updated               = true;
    // }

    if (uid_updated) {
        LOG_INFO("Hardware UIDs updated by software at boot");
        LOG_DEBUG("\tMainboard UID: %s", hw_assembly.uid_mainboard.c_str());
        LOG_DEBUG("Hardware assembly information:");
        LOG_DEBUG("\tLight sensor 1 UID: %s", hw_assembly.uid_light_sensor1.c_str());
        LOG_DEBUG("\tLight sensor 2 UID: %s", hw_assembly.uid_light_sensor2.c_str());
        LOG_DEBUG("\tSoftware UID: %s", hw_assembly.uid_software.c_str());
        LOG_DEBUG("\tExperiment UID: %s", hw_assembly.uid_experiment.c_str());
        LOG_DEBUG("\tBattery type: %s", hw_assembly.battery_type.c_str());
    }

    // --- Sync Serial Number ---
    LOG_DEBUG("Trying to synchronize SN from factory identity to assembly configuration");
    if (assembly_sync_sn(hw_assembly) or uid_updated) {

        if (!assembly_save(hw_assembly)) {
            LOG_WARN("Failed to synchronize SN to assembly configuration file");
        }
    }
    /*
    Load configuration from SD card
    */
    if (!load_configuration(config)) {
        LOG_WARN("Using default compiled configuration");
    }

    // --- Load factory identity from flash ---
    device_id_init();
    const auto& idFlash = device_id_get();
    LOG_INFO("Factory identity loaded from flash");
    LOG_INFO("\tManufacturer: %s", idFlash.manufacturer);
    LOG_INFO("\tLogger type: %s", idFlash.logger_type);
    LOG_INFO("\tDate of fabrication: %s", idFlash.date_fab);
    LOG_INFO("\tSerial number: %s", idFlash.serial_number);

    /*
     Battery initialization
     */
    LOG_INFO(string_widget.c_str());
    battery_service_config_t batt_serv_cfg{};

    // Hardware configuration
    batt_serv_cfg.hw.pin = PIN_VBAT;
    // ADC configuration - defaults as a reminder, can be overridden if needed
    batt_serv_cfg.hw.adc_cfg.ratio      = 2.0f;
    batt_serv_cfg.hw.adc_cfg.adc_ref_mv = 3300;
    batt_serv_cfg.hw.adc_cfg.adc_max    = 4095;

    // Get configuration policy from configuration file
    battery_thresholds_t batt_thr = battery_service_apply_type_string(hw_assembly.battery_type);
    batt_serv_cfg.policy.plausible_min_mv = batt_thr.low_warn_mv;   // e.g. 3300mV for LiPo 1S
    batt_serv_cfg.policy.plausible_max_mv = batt_thr.high_crit_mv;  // e.g. 4200mV for LiPo 1S

    // Remplir batt_serv_cfg.filter
    batt_serv_cfg.filter.ema_alpha          = 0.2;  // Smoothing factor for EMA (0..1).
    batt_serv_cfg.filter.delta_threshold_mv = 10;   // Change in mV for a battery level "changed".

    // Init service
    if (!battery_service_init(batt_serv_cfg)) {
        // Handle battery initialization failure
        LOG_ERROR("Battery service initialization failed");
        return STATE_ENDOFLIFE;
    }

    // Vérification initiale boot
    int32_t vbat_mv = 0;
    bool changed    = false;
    if (battery_service_read_vbat_filtered_mv(vbat_mv, changed)) {
        battery_set_available(true);
        if (!battery_service_decision("Boot", vbat_mv)) {
            // Log and blink error led
            error_signal(ERR_BATTERY_CRITICAL);
            return STATE_ENDOFLIFE;
        }
    } else {
        LOG_WARN("Initial battery reading failed");
        battery_set_available(false);
    }

    // --- Initialize IR PWM module ---
    LOG_INFO(string_widget.c_str());
    if (ir_enabled) {

        // Enable both sensors and provide the ISR callback
        ir_driver.begin(true, true, nullptr, nullptr);
        LOG_INFO("IR PWM driver initialized");
    } else {
        LOG_INFO("IR PWM driver disabled by configuration");
    }

    LOG_INFO(string_widget.c_str());
    // led_start_blink_isr(3, blink_mode::fast);
    LOG_INFO("Boot sequence completed");

    //LOG_INFO("Entering INIT mode");
    LOG_INFO("Entering CONNECTED mode");

    return STATE_CONNECTED;
}

/**
 * @brief Arduino setup routine — performs one-time boot sequence and starts runtime.
 */

void setup() {
    // Initialize logging system first to capture all subsequent logs
    logInit();
    delay(1000);

    // Initialize I²C for RTC and sensors
    Wire.begin();
    i2c_ok = scanI2CBus();

    // Initialize switch and power relay
    pwr_manager::begin();
    blink_blocking_safe(PIN_BUZZER_LED, 50, 50, 2);

    // Run the boot sequence
    currentState = runBootSequence();
    logWidgetTwice();
}

/**
 * @brief Main runtime loop implementing the global state machine.
 *
 * The loop executes the current system state stored in @ref currentState.
 *
 * ## State Overview
 * | State               | Description |
 * |:--------------------|:------------|
 * | **STATE_CONNECTED** | Temporary GUI/UART configuration window after boot.
 * | **STATE_INIT**      | Runtime hardware initialization before deployment.
 * | **STATE_DEPLOY**    | Periodic low-power data logging of sensors and battery. |
 * | **STATE_STOCK**     | Storage/idle state for pre-deployment conservation. |
 * | **STATE_ENDOFLIFE** | Safe shutdown when a critical error or low battery occurs. |
 * | **STATE_ERROR**     | Default fallback / placeholder state. |
 *
 * ## Power Management
 * Uses `ArduinoLowPower` to minimize energy usage between acquisitions.
 *
 * @see runConnectedMode()
 */

void loop() {
    switch (currentState) {
        case STATE_INIT:
            LOG_DEBUG("INIT mode active");

            if (load_configuration(config)) {
                LOG_INFO("Configuration re-loaded from SD");
            } else {
                LOG_WARN("Using default compiled configuration");
            }

            Sensors_InitForDeploy(g_sensors, G_SENSOR_COUNT);

            LOG_DEBUG("Initializing DEPLOY mode");

            // GUI no longer owns Serial1 from this point.
            gui_serial_stop(&Serial1);
            // Serial1 is now acquired by the RFID driver for DEPLOY mode.
            if (config.enable_rfid) {
                rfid_driver::start(&rfid_driver, &Serial1, TAG_TYPE_EM4102, 1000);
            }

            deploy_enter(ir_driver);

            LOG_INFO("Entering DEPLOY mode");
            currentState = STATE_DEPLOY;
            LOG_INFO(string_widget.c_str());
            LOG_INFO(string_widget.c_str());
            break;

        case STATE_CONNECTED:
            // RFID must not own Serial1 in CONNECTED mode.
            if (config.enable_rfid) {
                rfid_driver::stop(&rfid_driver);
            }

            // Serial1 is now used by the GUI communication layer.
            gui_serial_start(&Serial1);
            runConnectedMode(currentState);
            break;

        case STATE_DEPLOY:
            run_deploy_state(currentState, rfid_driver, ir_driver);
            break;

        case STATE_STOCK:
            LOG_DEBUG("Stock mode active. Sleeping...");
            digitalWrite(LED_BUILTIN, LOW);
            LowPower.sleep();
            break;

        case STATE_ENDOFLIFE:
            // @todo Factorize shutdown steps into a dedicated shutdown function.
            LOG_ERROR("Entering END OF LIFE mode");
            deploy_exit(ir_driver);
            SD.end();
            // led_start_blink_isr(1, blink_mode::slow);
            // led_start_blink_isr(10, blink_mode::fast);
            // Blink fast 10 times before switching off
            blink_blocking_safe(PIN_BUZZER_LED, 100, 100, 10);
            pwr_manager::request_shutdown();
            delay(2000);

            while (true) {
                LowPower.deepSleep();
            }
            break;

        case STATE_ERROR:
            LOG_ERROR("Error, unexpected STATE_ERROR mode reached.");
            currentState = STATE_INIT;
            break;

        default:
            LOG_ERROR("Error, unexpected default mode reached.");
            currentState = STATE_ERROR;
            break;
    }
}

/** @} */  // end of MainApplication group
