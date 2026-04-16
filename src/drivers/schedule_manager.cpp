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

    DateTime today_start(now.year(), now.month(), now.day(), m_window.start_hour,
                         m_window.start_minute, 0);

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

    DateTime today_end(now.year(), now.month(), now.day(), m_window.end_hour, m_window.end_minute,
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
