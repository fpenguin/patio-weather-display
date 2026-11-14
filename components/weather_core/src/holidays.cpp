#include "weather/holidays.hpp"

#include <algorithm>

namespace patio {
namespace {

int nth_weekday(int year, int month, int weekday, int ordinal) {
    const int first = weekday_for_date(year, month, 1);
    return 1 + ((weekday - first + 7) % 7) + 7 * (ordinal - 1);
}

int monday_before_may_25(int year) {
    for (int day = 24; day >= 18; --day) {
        if (weekday_for_date(year, 5, day) == 1) return day;
    }
    return 24;
}

void easter_sunday(int year, int& month, int& day) {
    const int a = year % 19;
    const int b = year / 100;
    const int c = year % 100;
    const int d = b / 4;
    const int e = b % 4;
    const int f = (b + 8) / 25;
    const int g = (b - f + 1) / 3;
    const int h = (19 * a + b - d - g + 15) % 30;
    const int i = c / 4;
    const int k = c % 4;
    const int l = (32 + 2 * e + 2 * i - h - k) % 7;
    const int m = (a + 11 * h + 22 * l) / 451;
    month = (h + l - 7 * m + 114) / 31;
    day = ((h + l - 7 * m + 114) % 31) + 1;
}

void add(HolidayCalendar& calendar, int year, int month, int day,
         const char* name, const char* jurisdiction,
         HolidayClass classification, bool day_off) {
    if (calendar.count >= calendar.entries.size()) return;
    Holiday& h = calendar.entries[calendar.count++];
    h.year = year; h.month = month; h.day = day;
    h.name.assign(name);
    h.jurisdiction.assign(jurisdiction);
    h.classification = classification;
    h.configured_day_off = day_off;
}

}  // namespace

bool is_leap_year(int year) {
    return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
}

int weekday_for_date(int year, int month, int day) {
    static constexpr int offsets[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    if (month < 3) --year;
    return (year + year / 4 - year / 100 + year / 400 + offsets[month - 1] + day) % 7;
}

HolidayCalendar bc_holidays(int year, bool include_observances) {
    HolidayCalendar calendar{};
    int easter_month = 0, easter_day = 0;
    easter_sunday(year, easter_month, easter_day);
    // Easter Sunday is in March/April; subtracting two days cannot cross February.
    int good_friday_month = easter_month;
    int good_friday_day = easter_day - 2;
    if (good_friday_day <= 0) {
        good_friday_month = 3;
        good_friday_day += 31;
    }

    constexpr const char* kBc = "BC, Canada";
    add(calendar, year, 1, 1, "New Year's Day", kBc, HolidayClass::Statutory, true);
    add(calendar, year, 2, nth_weekday(year, 2, 1, 3), "Family Day", kBc, HolidayClass::Statutory, true);
    add(calendar, year, good_friday_month, good_friday_day, "Good Friday", kBc, HolidayClass::Statutory, true);
    add(calendar, year, 5, monday_before_may_25(year), "Victoria Day", kBc, HolidayClass::Statutory, true);
    add(calendar, year, 7, 1, "Canada Day", kBc, HolidayClass::Statutory, true);
    add(calendar, year, 8, nth_weekday(year, 8, 1, 1), "B.C. Day", kBc, HolidayClass::Statutory, true);
    add(calendar, year, 9, nth_weekday(year, 9, 1, 1), "Labour Day", kBc, HolidayClass::Statutory, true);
    add(calendar, year, 9, 30, "National Day for Truth and Reconciliation", kBc, HolidayClass::Statutory, true);
    add(calendar, year, 10, nth_weekday(year, 10, 1, 2), "Thanksgiving Day", kBc, HolidayClass::Statutory, true);
    add(calendar, year, 11, 11, "Remembrance Day", kBc, HolidayClass::Statutory, true);
    add(calendar, year, 12, 25, "Christmas Day", kBc, HolidayClass::Statutory, true);
    if (include_observances) {
        add(calendar, year, 10, 31, "Halloween", kBc, HolidayClass::Observance, false);
        add(calendar, year, 12, 26, "Boxing Day", kBc, HolidayClass::Observance, false);
    }
    return calendar;
}

HolidayCalendar canada_federal_holidays(int year, bool include_observances) {
    HolidayCalendar calendar{};
    int easter_month = 0, easter_day = 0;
    easter_sunday(year, easter_month, easter_day);
    int good_friday_month = easter_month;
    int good_friday_day = easter_day - 2;
    if (good_friday_day <= 0) {
        good_friday_month = 3;
        good_friday_day += 31;
    }

    constexpr const char* kFederal = "Canada (federal)";
    add(calendar, year, 1, 1, "New Year's Day", kFederal, HolidayClass::Statutory, true);
    add(calendar, year, good_friday_month, good_friday_day, "Good Friday", kFederal,
        HolidayClass::Statutory, true);
    add(calendar, year, 5, monday_before_may_25(year), "Victoria Day", kFederal,
        HolidayClass::Statutory, true);
    add(calendar, year, 7, 1, "Canada Day", kFederal, HolidayClass::Statutory, true);
    add(calendar, year, 9, nth_weekday(year, 9, 1, 1), "Labour Day", kFederal,
        HolidayClass::Statutory, true);
    add(calendar, year, 9, 30, "National Day for Truth and Reconciliation", kFederal,
        HolidayClass::Statutory, true);
    add(calendar, year, 10, nth_weekday(year, 10, 1, 2), "Thanksgiving Day", kFederal,
        HolidayClass::Statutory, true);
    add(calendar, year, 11, 11, "Remembrance Day", kFederal,
        HolidayClass::Statutory, true);
    add(calendar, year, 12, 25, "Christmas Day", kFederal,
        HolidayClass::Statutory, true);
    add(calendar, year, 12, 26, "Boxing Day", kFederal,
        HolidayClass::Statutory, true);
    if (include_observances) {
        add(calendar, year, 10, 31, "Halloween", kFederal,
            HolidayClass::Observance, false);
    }
    return calendar;
}

Optional<Holiday> featured_holiday_for_date(const HolidayCalendar& calendar,
                                            int year, int month, int day) {
    const Holiday* best = nullptr;
    for (std::size_t i = 0; i < calendar.count; ++i) {
        const Holiday& candidate = calendar.entries[i];
        if (candidate.year != year || candidate.month != month || candidate.day != day) continue;
        if (!best || (candidate.configured_day_off && !best->configured_day_off) ||
            (candidate.classification == HolidayClass::Statutory &&
             best->classification != HolidayClass::Statutory)) {
            best = &candidate;
        }
    }
    return best ? Optional<Holiday>::some(*best) : Optional<Holiday>::none();
}

}  // namespace patio

namespace patio {
namespace {
struct DateEntry { std::uint16_t date; std::uint8_t name; bool public_holiday; };
struct Calendar { const char *code, *label, *parent; std::uint16_t begin, end; };
#include "holiday_data.inc"

bool same(const char* a, const char* b) { return std::strcmp(a,b) == 0; }
const Calendar* find_calendar(const char* code) {
    for (const auto& c : calendars) if (same(c.code,code)) return &c;
    return nullptr;
}
const Calendar& us_local(const Location& location) {
    const char* state = location.region.c_str();
    if (!std::strncmp(state, "US-", 3)) state += 3;
    const Calendar* found = nullptr;
    for (std::size_t i = us_begin; i < us_end; ++i) {
        const Calendar& c = calendars[i];
        if (!c.parent[0] && (same(c.code,state) || same(c.label,state))) {
            found = &c;
            break;
        }
    }
    if (!found) return calendars[1];
    for (const Calendar& c : calendars) {
        if (!same(c.parent,found->code)) continue;
        const auto n = std::strcspn(c.label,",");
        if ((std::strlen(location.city.c_str()) == n && !std::strncmp(location.city.c_str(),c.label,n)) ||
            (same(c.code,"NYC") && location.city == "New York")) return c;
    }
    return *found;
}

const char* canada_subdivision_code(const char* region) {
    struct Alias { const char* name; const char* code; };
    static constexpr Alias aliases[] = {
        {"Alberta", "AB"}, {"British Columbia", "BC"}, {"Colombie-Britannique", "BC"},
        {"Manitoba", "MB"}, {"New Brunswick", "NB"}, {"Nouveau-Brunswick", "NB"},
        {"Newfoundland and Labrador", "NL"}, {"Terre-Neuve-et-Labrador", "NL"},
        {"Nova Scotia", "NS"}, {"Nouvelle-Écosse", "NS"},
        {"Northwest Territories", "NT"}, {"Territoires du Nord-Ouest", "NT"},
        {"Nunavut", "NU"}, {"Ontario", "ON"}, {"Prince Edward Island", "PE"},
        {"Île-du-Prince-Édouard", "PE"}, {"Quebec", "QC"}, {"Québec", "QC"},
        {"Saskatchewan", "SK"}, {"Yukon", "YT"},
    };
    if (!region) return nullptr;
    if (!std::strncmp(region, "CA-", 3)) region += 3;
    else if (!std::strncmp(region, "C-", 2)) region += 2;
    for (const auto& alias : aliases) {
        if (same(region, alias.code) || same(region, alias.name)) return alias.code;
    }
    return nullptr;
}

const Calendar* canada_local(const Location& location) {
    const char* code = canada_subdivision_code(location.region.c_str());
    if (!code) return nullptr;
    char stored[5] = {'C', '-', code[0], code[1], '\0'};
    return find_calendar(stored);
}
Optional<Holiday> lookup(const Calendar& c, int year, int month, int day) {
    const unsigned key = (year-first_year)*512 + month*32 + day;
    const auto it = std::lower_bound(dates+c.begin, dates+c.end, key,
        [](const DateEntry& e, unsigned k) { return e.date < k; });
    if (it == dates+c.end || it->date != key) return {};
    Holiday h{};
    h.year=year; h.month=month; h.day=day;
    h.name.assign(event_names[it->name]); h.jurisdiction.assign(c.label);
    h.configured_day_off=it->public_holiday;
    h.classification=it->public_holiday ? HolidayClass::Statutory : HolidayClass::Observance;
    return Optional<Holiday>::some(h);
}
}

const char* us_calendar_options() { return us_options; }
unsigned us_calendar_index(const char* region) {
    const auto* p = find_calendar(region);
    return p && p >= calendars + us_begin && p < calendars + us_end
        ? static_cast<unsigned>(p - calendars - us_begin) : 0;
}
const char* us_calendar_code(unsigned i) {
    return i < us_end - us_begin ? calendars[us_begin + i].code : "FED";
}

const char* canada_calendar_options() { return canada_options; }
unsigned canada_calendar_index(const char* region) {
    if (same(region, "FED")) return 0;
    const char* code = canada_subdivision_code(region);
    if (!code) return 0;
    char stored[5] = {'C', '-', code[0], code[1], '\0'};
    const auto* p = find_calendar(stored);
    return p && p >= calendars + canada_begin
        ? static_cast<unsigned>(p - calendars - canada_begin + 1) : 0;
}
const char* canada_calendar_code(unsigned i) {
    if (i == 0 || i > std::size(calendars) - canada_begin) return "FED";
    const char* generated = calendars[canada_begin + i - 1].code;
    return same(generated, "C-BC") ? "BC" : generated;
}

CalendarChoice holiday_choice(const char* country, const char* region, const Location& loc) {
    if (same(country,"AUTO")) return location_calendar(loc);
    if (same(country,"KR")) return {"KR","","South Korea"};
    if (same(country,"CA")) {
        if (same(region,"FED")) return {"CA","FED","Federal, Canada"};
        const char* code = canada_subdivision_code(region);
        const Calendar* c = nullptr;
        if (code) {
            char stored[5] = {'C', '-', code[0], code[1], '\0'};
            c = find_calendar(stored);
        } else if (!region[0] && loc.country_code == "CA") {
            c = canada_local(loc);
        }
        if (!c) return {"CA","FED","Federal, Canada"};
        return {"CA", same(c->code,"C-BC") ? "BC" : c->code, c->label};
    }
    if (same(country,"US")) {
        const Calendar* c = region[0] ? find_calendar(region) : nullptr;
        if (!c || c == calendars) c = !region[0] && loc.country_code == "US" ? &us_local(loc) : &calendars[1];
        return {"US",c->code,c->label};
    }
    return {"","","No supported holiday calendar"};
}
CalendarChoice location_calendar(const Location& loc) {
    if (loc.country_code == "US") return holiday_choice("US","",loc);
    if (loc.country_code == "CA") return holiday_choice("CA","",loc);
    if (loc.country_code == "KR") return {"KR","","South Korea"};
    return {"","","No supported holiday calendar"};
}
Optional<Holiday> regional_holiday(CalendarChoice choice, int year, int month, int day) {
    if (year < first_year || year > last_year || month < 1 || month > 12 || day < 1 || day > 31) return {};
    const auto* c = same(choice.country,"KR") ? calendars :
                    same(choice.country,"US") ? find_calendar(choice.region) :
                    same(choice.country,"CA") ? find_calendar(choice.region) : nullptr;
    if (!c) return {};
    auto result = lookup(*c,year,month,day);
    if (!result.valid && c->parent[0]) {
        c = find_calendar(c->parent);
        result = lookup(*c,year,month,day);
    }
    // Federal offices can observe a date that state offices do not. Keep its
    // federal jurisdiction visible rather than claiming a statewide closure.
    if (!result.valid && same(choice.country,"US") && c != &calendars[1])
        result = lookup(calendars[1],year,month,day);
    return result;
}
}  // namespace patio
