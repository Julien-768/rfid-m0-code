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

#include "stdint.h"
#include "hardware.h"
#include "rtc.h"
#include "config.h"
#include "assembly.h"
#include "sd_manager.h"

#include "system_state.h"
#include "log.h"
#include <ArduinoLowPower.h>
#include <SD.h>
#include "connected_mode.h"
#include "det_ext.h"
#include "deploy_mode.h"
#include "sensors.h"
#include "sensors_internal.h"
#include "logger_identity.h"
#include "mcu_uid.h"
#include "sd_manager.h"
#include "utils.h"
#include "battery.h"

SystemState currentState = STATE_INIT;

enum class BlinkMode
{
    Slow   = 500,
    Medium = 250,
    Fast   = 100
    // Seules 3 valeurs possibles
};

void Led_Blink(uint32_t Pin, BlinkMode speed, uint8_t count) {
    for (uint8_t i = 0; i < count; i++)
        {
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
 * - Initialize SD card storage via @ref initSD().
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
 * @see initSD()
 * @see initRTC()
 * @see rtc_bootRecover()
 * @see check_and_create_new_daily_file()
 * @see batteryBootDiagnostic()
 * @see loggerIdentity_init()
 */
void runBootSequence() {
    LOG_INFO("------------------------------------------------------------");
    LOG_INFO("Great tit Logger — Boot sequence started");
    LOG_INFO("Firmware: vx.x.x");
    LOG_INFO("Board: Feather M0 Adalogger");
    LOG_INFO("------------------------------------------------------------");

    /* Initialize SD card */
    if (!initSD(PIN_SD_CS))
        {
            currentState = STATE_ENDOFLIFE;
            return;
    }

    /* Initialize RTC */
    if (!init_rtc())
        {
            currentState = STATE_ENDOFLIFE;
            return;
    }

    /* Check if external detector is connected */
    const bool connectedNow = DET_EXT_Connected();

    /* Check any fault on RTC*/
    rtc_boot_recover(connectedNow);

    /* If everything is ok */
    if (!connectedNow || !rtc_state.time_unverified)
        {
            DateTime now = rtc.now();
            // TODO sd_manager
            check_and_create_new_daily_file(now);
            LOG_INFO("Daily data file created at boot");
    } else
        {
            LOG_INFO("RTC time unverified and logger connected — waiting for GUI time before creating daily data file");
        }

    // TODO ?
    /* Battery diagnostic */
    // uint32_t vbat_mv = read_battery_voltage(PIN_VBAT);
    // if (!battery_boot_diagnostic(vbat_mv))
    //     {
    //         currentState = STATE_ENDOFLIFE;
    //         return;
    // }

    // --- Load existing assembly.cfg ---
    loadAssembly(assembly);
    LOG_INFO("Assembly information loaded from assembly.cfg");

    loggerIdentity_init();
    const auto& idFlash = loggerIdentity_get();

    LOG_INFO("Factory identity: %s / %s / %s / %s", idFlash.manufacturer, idFlash.logger_type, idFlash.date_fab, idFlash.serial_number);

    // --- Read all hardware UIDs (in RAM only) ---
    readFeatherUID();
    // readAS7341DeviceID();
    // readTSL2591DeviceID();

    // --- Complete missing “meta” fields ---
    if (assembly.uid_software.length() == 0) assembly.uid_software = "DefaultSoftware_v1.0.0";
    if (assembly.uid_experiment.length() == 0) assembly.uid_experiment = "DefaultExperiment";

    // --- Sync SD assembly with factory identity (SN, etc.) ---
    syncAssemblyWithFactoryIdentity();

    // --- Persist assembly metadata once everything is coherent ---
    // @todo Consider persisting assembly.cfg at boot once the write policy is finalized.
    // if (saveAssembly(assembly))
    // {
    //     LOG_INFO("assembly.cfg updated at boot");
    // }
    // else
    // {
    //     LOG_WARN("Failed to save assembly.cfg at boot");
    // }

    // --- Log runtime configuration summary (independent from assembly) ---
    LOG_DEBUG("Config summary at boot:");
    LOG_DEBUG(config.use_buffer ? "  use_buffer: true" : "  use_buffer: false");
    LOG_DEBUG(config.enable_light1 ? "  light1: true" : "  light1: false");
    LOG_DEBUG(config.enable_light2 ? "  light2: true" : "  light2: false");
    LOG_DEBUG(config.enable_vbat ? "  vbat: true" : "  vbat: false");

    Led_Blink(LED_BUILTIN, BlinkMode::Fast, 3);
    LOG_INFO("Boot sequence completed — entering INIT state");
    currentState = STATE_INIT;
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

    switch (currentState)
        {
            case STATE_INIT:
                if (DET_EXT_Connected())
                    {
                        LOG_INFO("External detector detected: entering CONNECTED mode");

                        // If time is unverified, CONNECTED mode may wait for GUI-provided time.
                        currentState = STATE_CONNECTED;
                } else
                    {
                        if (loadConfiguration(config))
                            {
                                LOG_INFO("Configuration loaded from SD");
                        } else
                            {
                                LOG_WARN("Using default compiled configuration");
                            }

                        rtc_schedule_next_wake(rtc.now(), config.acquisition_interval_s);

                        // Initialize sensors once before entering DEPLOY
                        Sensors_InitForDeploy(g_sensors, G_SENSOR_COUNT);

                        LOG_INFO("Entering DEPLOY mode");
                        currentState = STATE_DEPLOY;
                    }
                break;

                case STATE_CONNECTED: {
                    if (rtc_state.time_unverified && millis() > rtc_state.wait_deadline_ms)
                        {
                            const DateTime build(F(__DATE__), F(__TIME__));
                            rtc.adjust(build);
                            rtc_state.time_unverified = false;
                            LOG_WARN("GUI time timeout — using build time");
                            // TODO sd_manager
                            // check_and_create_new_daily_file(rtc.now());
                    }

                    runConnectedMode(currentState);
                    break;
                }

            case STATE_DEPLOY:
                runDeployState(currentState);
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

                while (true)
                    {
                        LowPower.deepSleep();
                    }
                break;

            default:
                currentState = STATE_WAIT;
                break;
        }
}

/** @} */  // end of MainApplication group
