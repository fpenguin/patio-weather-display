#pragma once

#include "weather/models.hpp"

namespace patio {

constexpr std::size_t kMaxHolidaysPerYear = 16;

struct HolidayCalendar {
    std::array<Holiday, kMaxHolidaysPerYear> entries{};
    std::size_t count{0};
};

HolidayCalendar bc_holidays(int year, bool include_observances = true);
// Canada Labour Code general holidays. This is intentionally distinct from
// provincial employment-entitlement calendars such as British Columbia's.
HolidayCalendar canada_federal_holidays(int year, bool include_observances = true);
Optional<Holiday> featured_holiday_for_date(const HolidayCalendar& calendar,
                                            int year, int month, int day);
bool is_leap_year(int year);
int weekday_for_date(int year, int month, int day);

}  // namespace patio

namespace patio {
struct CalendarChoice {
    const char *country, *region, *label;
};
// Codes are stored in the existing NVS fields. Empty legacy US regions use
// the saved city's most specific available calendar.
CalendarChoice holiday_choice(const char* country, const char* region, const Location& location);
CalendarChoice location_calendar(const Location& location);
Optional<Holiday> regional_holiday(CalendarChoice calendar, int year, int month, int day);
const char* us_calendar_options();
unsigned us_calendar_index(const char* region);
const char* us_calendar_code(unsigned index);
const char* canada_calendar_options();
unsigned canada_calendar_index(const char* region);
const char* canada_calendar_code(unsigned index);
}
