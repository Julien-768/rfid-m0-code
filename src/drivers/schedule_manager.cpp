/**
 * @file schedule_manager.cpp
 *
 * @version 3.0.0
 * @date 2022-08-17
 *
 * @section authors Authors
 * - Julien Courtecuisse
 *   Affiliation: IPHC-DEPE; UMR 7178; Université de Strasbourg-CNRS; F-67000 Strasbourg, France
 *   Email: julien.courtecuisse@iphc.cnrs.fr
 *
 * @section project Project
 * Name: RFID-M0
 * Description: Embedded firmware for the open-source RFID-M0 project, designed to monitor small animal species around burrows and nest boxes. The software integrates RFID-based individual identification, infrared passage detection, real-time clock (RTC) timestamping, and SD-card data logging, with low-power management for autonomous field deployments. It supports ecological research, including long-term monitoring of Great Tits (Parus major) and research on the European hamster (Cricetus cricetus) conducted within the cross-border CRICETUS project.
 *
 * @section contributors Contributors
 * - Robin Laesser
 *   Affiliation: IPHC-DEPE; UMR 7178; Université de Strasbourg-CNRS; F-67000 Strasbourg, France
 *   Email: robin.laesser@iphc.cnrs.fr
 *
 * @section license License
 * Copyright (C) 2022 Julien Courtecuisse
 *
 * This program is free software: you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation, either version 3
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * License: GPL-3.0-or-later
 * https://www.gnu.org/licenses/gpl-3.0.html
 */

#include "schedule_manager.h"

ScheduleManager::ScheduleManager(const TimeWindow& window) : m_window(window) {}

void ScheduleManager::setWindow(const TimeWindow& window) {
    m_window = window;
}

const ScheduleManager::TimeWindow& ScheduleManager::window() const {
    return m_window;
}

bool ScheduleManager::isValidHHMM(uint8_t hour, uint8_t minute) {
    return (hour < 24u) && (minute < 60u);
}

uint16_t ScheduleManager::toMinutes(uint8_t hour, uint8_t minute) {
    return static_cast<uint16_t>(hour) * 60u + static_cast<uint16_t>(minute);
}

uint16_t ScheduleManager::nowMinutes(const DateTime& now) {
    return static_cast<uint16_t>(now.hour()) * 60u + static_cast<uint16_t>(now.minute());
}

bool ScheduleManager::isMinuteInWindow(uint16_t current_min, uint16_t start_min, uint16_t end_min) {
    // start == end => actif 24h/24
    if (start_min == end_min) {
        return true;
    }

    // plage normale, ex: 08:00 -> 18:30
    if (start_min < end_min) {
        return (current_min >= start_min) && (current_min < end_min);
    }

    // plage traversant minuit, ex: 20:00 -> 06:00
    return (current_min >= start_min) || (current_min < end_min);
}

bool ScheduleManager::isValid() const {
    return isValidHHMM(m_window.start_hour, m_window.start_minute) &&
           isValidHHMM(m_window.end_hour, m_window.end_minute);
}

bool ScheduleManager::isActive(const DateTime& now) const {
    if (!isValid()) {
        return true;  // fail-safe
    }

    const uint16_t current_min = nowMinutes(now);
    const uint16_t start_min   = toMinutes(m_window.start_hour, m_window.start_minute);
    const uint16_t end_min     = toMinutes(m_window.end_hour, m_window.end_minute);

    return isMinuteInWindow(current_min, start_min, end_min);
}

DateTime ScheduleManager::nextStart(const DateTime& now) const {
    if (!isValid()) {
        return now;
    }

    const uint16_t current_min = nowMinutes(now);
    const uint16_t start_min   = toMinutes(m_window.start_hour, m_window.start_minute);
    const uint16_t end_min     = toMinutes(m_window.end_hour, m_window.end_minute);

    // 24h/24
    if (start_min == end_min) {
        return now;
    }

    DateTime today_start(now.year(),
                         now.month(),
                         now.day(),
                         m_window.start_hour,
                         m_window.start_minute,
                         0);

    // plage normale
    if (start_min < end_min) {
        if (current_min < start_min) {
            return today_start;
        }
        return today_start + TimeSpan(1, 0, 0, 0);  // demain
    }

    // plage traversant minuit
    // hors fenêtre => forcément avant start le même jour
    if (!isMinuteInWindow(current_min, start_min, end_min)) {
        return today_start;
    }

    // si on l'appelle alors qu'on est actif la nuit, prochain début = demain soir
    return today_start + TimeSpan(1, 0, 0, 0);
}

DateTime ScheduleManager::nextEnd(const DateTime& now) const {
    if (!isValid()) {
        return now;
    }

    const uint16_t current_min = nowMinutes(now);
    const uint16_t start_min   = toMinutes(m_window.start_hour, m_window.start_minute);
    const uint16_t end_min     = toMinutes(m_window.end_hour, m_window.end_minute);

    // 24h/24
    if (start_min == end_min) {
        return now;
    }

    DateTime today_end(now.year(),
                       now.month(),
                       now.day(),
                       m_window.end_hour,
                       m_window.end_minute,
                       0);

    // plage normale
    if (start_min < end_min) {
        if (current_min < end_min) {
            return today_end;
        }
        return today_end + TimeSpan(1, 0, 0, 0);
    }

    // plage traversant minuit
    // ex: 20:00 -> 06:00
    if (current_min >= start_min) {
        // on est dans la partie du soir, fin demain matin
        return today_end + TimeSpan(1, 0, 0, 0);
    }

    // sinon fin ce matin
    return today_end;
}
