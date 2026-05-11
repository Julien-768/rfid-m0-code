#pragma once

#include <stdint.h>
#include <RTClib.h>

class ScheduleManager {
   public:
    struct TimeWindow {
        uint8_t start_hour;
        uint8_t start_minute;
        uint8_t end_hour;
        uint8_t end_minute;
    };

    explicit ScheduleManager(const TimeWindow& window);

    void setWindow(const TimeWindow& window);
    const TimeWindow& window() const;

    bool isValid() const;
    bool isActive(const DateTime& now) const;

    // Précondition: idéalement appelé quand on est hors fenêtre.
    DateTime nextStart(const DateTime& now) const;

    // Optionnel: utile si tu veux connaître la prochaine fin de plage.
    DateTime nextEnd(const DateTime& now) const;

   private:
    TimeWindow m_window;

    static bool isValidHHMM(uint8_t hour, uint8_t minute);
    static uint16_t toMinutes(uint8_t hour, uint8_t minute);
    static uint16_t nowMinutes(const DateTime& now);

    // Teste si current_min est dans [start_min, end_min)
    static bool isMinuteInWindow(uint16_t current_min, uint16_t start_min, uint16_t end_min);
};
