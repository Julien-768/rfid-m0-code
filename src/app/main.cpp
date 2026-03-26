/**
 * @file main.cpp
 * @defgroup MainApplication  Great titCore Application
 * @ingroup SystemModules
 * @brief Main control logic and global state machine of the Great tit Logger.
 *
 * The **Main Application** coordinates all core modules of the  Great tit
 * autonomous data logger. It implements the global system state machine and
 * ensures safe operation under low-power constraints.
 *
 * ## Responsibilities
 * - Initialize and coordinate hardware subsystems:
 *   - SD card
 *   - RTC (DS3231)
 *   - Configuration and assembly metadata
 *   - Battery monitor
 *   - Sensors
 * - Execute boot-time diagnostics and handle hardware failures.
 * - Manage operational modes via a global state machine:
 *   - **INIT** → Decide between CONNECTED or DEPLOY after boot sequence.
 *   - **CONNECTED** → User interaction via serial link and GUI.
 *   - **DEPLOY** → Periodic low-power data logging.
 *   - **STOCK** → Storage/idle state before deployment.
 *   - **END-OF-LIFE** → Safe shutdown on critical error or low battery.
 *
 * ## Boot model
 * The boot sequence is executed **once in `setup()`** via @ref runBootSequence().
 * It performs all critical checks and sets the initial runtime state.
 *
 * ## Power management
 * - Uses the `ArduinoLowPower` library for SAMD21 sleep/deep sleep.
 * - Wakes up periodically via DS3231 alarm interrupts.
 * - Minimizes SD writes using buffered logging (when enabled).
 *
 * @see sd_manager.h
 * @see rtc.h
 * @see battery.h
 * @see system_state.h
 * @see log.h
 *
 * @{
 */

#include <ArduinoLowPower.h>
#include <SD.h>
#include "hardware.h"
#include "rtc.h"
#include "config.h"
#include "assembly.h"
#include "sd_manager.h"

#include "system_state.h"
#include "log.h"
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

//TODO record in SD log messages if needed

SystemState currentState = STATE_INIT;

/**
 * @brief IR PWM driver instance
 */
ir_pwm ir_driver(PIN_IR_SEND, PIN_PR_1, PIN_PR_2);

enum class BlinkMode {
    Slow   = 500,
    Medium = 250,
    Fast   = 100
    // Seules 3 valeurs possibles
};

void Led_Blink(uint32_t Pin, BlinkMode speed, uint8_t count) {
    for (uint8_t i = 0; i < count; i++) {
        digitalWrite(Pin, HIGH);
        delay((uint16_t)speed);
        digitalWrite(Pin, LOW);
        delay((uint16_t)speed);
    }
}

/**
 * @brief Execute the full hardware initialization sequence at startup.
 *
 * This function is called once from @ref setup(). It performs all critical
 * hardware checks and prepares the logging environment.
 *
 * ### Responsibilities
 * - Initialize SD card storage via @ref sd_initialization().
 * - Initialize RTC (DS3231), validate time, and perform recovery via
 *   @ref rtc_bootRecover().
 * - Initialize RTC (DS3231), validate time, and perform recovery via
 *   @ref rtc_bootRecover().
 * - Create the daily log file if the RTC time is trusted, or delay file creation
 *   until time is confirmed when connected to a GUI.
 * - Perform a battery diagnostic and enforce @ref STATE_ENDOFLIFE if voltage is critical.
 * - Load assembly metadata from the SD card.
 * - Load the factory identity from MCU flash via @ref loggerIdentity_init().
 * - Initialize system identifiers (Feather UID, RTC UID, sensor UIDs).
 *
 * ### Behavior Summary
 * - If SD or RTC initialization fails → transitions to @ref STATE_ENDOFLIFE.
 * - If RTC time is invalid and a GUI is connected → waits for GUI-provided time
 *   before creating the daily log file.
 * - On success → sets @ref currentState to @ref STATE_INIT.
 *
 * @note This function interacts heavily with the logging system. If no daily file
 *       exists yet, logging may be buffered in RAM and flushed once the daily file
 *       is created.
 *
 * @warning If RTC time remains unverified and no GUI time is provided, the system
 *          may fall back to firmware build time (see @ref STATE_CONNECTED handling).
 *
 * @see sd_initialization()
 * @see initRTC()
 * @see rtc_bootRecover()
 * @see check_and_create_new_daily_file()
 * @see batteryBootDiagnostic()
 * @see loggerIdentity_init()
 */
SystemState runBootSequence() {
    String buildDateTime = "Build" + String(F(__DATE__)) + " " + String(F(__TIME__));
    String board         = "Board:" + String(__PIO_BOARD_NAME__);
    String string_widget = "------------------------------------------------------------";

    LOG_INFO(string_widget.c_str());
    LOG_INFO("Boot sequence started");
    LOG_INFO(buildDateTime.c_str());
    LOG_INFO(board.c_str());
    LOG_INFO(string_widget.c_str());

    /* Initialize SD card */
    if (!sd_initialization(PIN_SD_CS)) {
        return STATE_ENDOFLIFE;
    }

    // --- Load existing assembly.cfg ---
    assembly_load(hw_assembly);
    LOG_INFO("Assembly information loaded from assembly.cfg");

    if (hw_assembly.rtc_type == "ds3231") {
        LOG_INFO("RTC type: DS3231");
        /* Initialize RTC */
        if (!rtc_initialization()) {
            return STATE_ENDOFLIFE;
        }
        /* Check any fault on RTC*/
        rtc_boot_recover();
    } else {
        LOG_WARN("RTC type not recognized or not specified. RTC features will be unavailable.");
    }

    DateTime now = rtc.now();
    check_and_create_new_daily_file(now);

    /*
     Battery initialization
     */
    battery_service_config_t cfg{};

    // 1) Remplir cfg.hw
    cfg.hw.pin                = ...;
    cfg.hw.adc_cfg.ratio      = ...;
    cfg.hw.adc_cfg.adc_ref_mv = ...;
    cfg.hw.adc_cfg.adc_max    = ...;

    // 2) Remplir cfg.policy
    cfg.policy.plausible_min_mv = ...;
    cfg.policy.plausible_max_mv = ...;

    // 3) Remplir cfg.filter
    cfg.filter.ema_alpha          = ...;
    cfg.filter.delta_threshold_mv = ...;

    // 4) Init service
    if (!battery_service_init(cfg)) {
        // gestion erreur init hardware batterie
    }

    // 5) Appliquer le type batterie réel
    battery_service_apply_type_string(app_config.battery_type);

    // 6) Vérification initiale boot
    int32_t vbat_mv = 0;
    bool changed    = false;
    if (battery_service_read_vbat_filtered_mv(vbat_mv, changed)) {
        if (!battery_service_decision("Boot", vbat_mv)) {
            // Handle decision failure
            return STATE_ENDOFLIFE;
        }
    }

    loggerIdentity_init();
    const auto& idFlash = device_id_get();

    LOG_INFO("Factory identity: %s / %s / %s / %s", idFlash.manufacturer, idFlash.logger_type,
             idFlash.date_fab, idFlash.serial_number);

    // Read all hardware UIDs (in RAM only) ---
    if (hw_assembly.uid_mainboard == "$uid_mainboard$") {
        hw_assembly.uid_mainboard = mcu_uid_read();
        assembly_save(hw_assembly);
    }
    // if (hw_assembly.uid_light_sensor1 == "$uid_light_sensor1$")
    //     {
    //         hw_assembly.uid_light_sensor1 = readAS7341DeviceID();  // TODO read from sensor
    //         assembly_save(hw_assembly);
    // }
    // if (hw_assembly.uid_light_sensor2 == "$uid_light_sensor2$")
    //     {
    //         hw_assembly.uid_light_sensor2 = readTSL2591DeviceID();  // TODO read from sensor
    //         assembly_save(hw_assembly);
    // }

    // --- Sync SD assembly with factory identity (SN, etc.) ---
    assembly_sync_sn();

    // --- Initialize IR PWM module ---

    // Enable both sensors and provide the ISR callback
    ir_driver.begin(true, true, nullptr, nullptr);
    LOG_INFO("IR PWM driver initialized");

    Led_Blink(LED_BUILTIN, BlinkMode::Fast, 3);
    LOG_INFO("Boot sequence completed");
    return STATE_INIT;
}

/**
 * @brief Arduino setup routine — performs one-time boot sequence and starts runtime.
 *
 * This function:
 * - Initializes serial communication (`Serial1`) for debug output.
 * - Configures the built-in LED for visual feedback.
 * - Initializes I²C (`Wire.begin()`) and external detector input (@ref DET_EXT_Init()).
 * - Runs the one-time boot sequence via @ref runBootSequence().
 *
 * After @ref runBootSequence(), the global state machine starts in @ref loop()
 * using the value set in @ref currentState (typically @ref STATE_INIT).
 *
 * @see runBootSequence()
 * @see loop()
 */

void setup() {
    Serial1.begin(115200);
    delay(100);

    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, LOW);

    // Initialize I²C for RTC and sensors
    Wire.begin();

    DET_EXT_Init();
    runBootSequence();
}

/**
 * @brief Main runtime loop implementing the global state machine.
 *
 * The loop executes the current system state stored in @ref currentState.
 *
 * ## State Overview
 * | State               | Description |
 * |:--------------------|:------------|
 * | **STATE_INIT**      | Decide between CONNECTED (GUI) or DEPLOY (autonomous logging). |
 * | **STATE_CONNECTED** | Serial/GUI interactive configuration mode (@ref runConnectedMode). |
 * | **STATE_DEPLOY**    | Periodic low-power data logging of sensors and battery. |
 * | **STATE_STOCK**     | Storage/idle state for pre-deployment conservation. |
 * | **STATE_ENDOFLIFE** | Safe shutdown when a critical error or low battery occurs. |
 * | **STATE_WAIT**      | Default fallback / placeholder state. |
 *
 * ## Power Management
 * Uses `ArduinoLowPower` to minimize energy usage between acquisitions.
 *
 * @see runConnectedMode()
 * @see runDeployState()
 */

void loop() {

    switch (currentState) {
        case STATE_INIT:
            if (DET_EXT_Connected()) {
                LOG_INFO("Entering CONNECTED mode");
                currentState = STATE_CONNECTED;
            } else {
                LOG_INFO("Initializing sensors for DEPLOY mode");
                if (loadConfiguration(config)) {
                    LOG_INFO("Configuration loaded from SD");
                } else {
                    LOG_WARN("Using default compiled configuration");
                }
                if (hw_assembly.rtc_type == "ds3231") {
                    LOG_INFO("Scheduling first wake-up via RTC alarm");
                    rtc_init_interrupt_pin(interrupt_pin);  // Initialize the interrupt pin
                }

                // Initialize sensors once before entering DEPLOY
                Sensors_InitForDeploy(g_sensors, G_SENSOR_COUNT);

                LOG_DEBUG("Entering DEPLOY mode");
                currentState = STATE_DEPLOY;
            }
            break;

        case STATE_CONNECTED: {
            runConnectedMode(currentState);
            break;
        }

        case STATE_DEPLOY:

            run_deploy_state(currentState);
            break;

        case STATE_STOCK:
            LOG_DEBUG("Stock mode active. Sleeping...");
            digitalWrite(LED_BUILTIN, LOW);
            LowPower.sleep();
            break;

        case STATE_ENDOFLIFE:
            // @todo Factorize shutdown steps into a dedicated shutdown function.
            LOG_DEBUG("Entering END OF LIFE mode: shutting down sensors and SD card.");

            SD.end();
            LOG_INFO("All peripherals powered off");
            Led_Blink(LED_BUILTIN, BlinkMode::Slow, 1);
            Led_Blink(LED_BUILTIN, BlinkMode::Fast, 3);
            LOG_DEBUG("System halted. LED off. Entering infinite sleep.");

            while (true) {
                LowPower.deepSleep();
            }
            break;

        default:
            currentState = STATE_WAIT;
            break;
    }
}

/** @} */  // end of MainApplication group
