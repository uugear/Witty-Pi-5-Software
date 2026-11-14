#define _GNU_SOURCE
#include "wp5dst.h"

#include <string.h>

static bool leap(int year) {
    return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
}

static int days_in_month(int year, int month) {
    static const int days[] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month < 1 || month > 12) return 0;
    return days[month] + (month == 2 && leap(year));
}

static bool valid_spec(const Wp5DstTimeSpec *spec, bool fixed) {
    if (spec->month < 1 || spec->month > 12 || spec->hour > 23 || spec->minute > 59)
        return false;
    if (fixed)
        return spec->date >= 1 && spec->date <= days_in_month(2023, spec->month);
    return spec->week <= 5 && spec->weekday <= 6;
}

bool wp5_dst_rule_valid(const Wp5DstRule *rule) {
    return rule && rule->offset_minutes <= 127 &&
        (rule->offset_minutes == 0 ||
         (valid_spec(&rule->begin, rule->fixed_date) &&
          valid_spec(&rule->end, rule->fixed_date)));
}

static uint8_t bcd(int number) {
    return (uint8_t)(((number / 10) << 4) | (number % 10));
}

static bool valid_bcd(uint8_t value) {
    return (value & 0x0f) <= 9 && ((value >> 4) & 0x0f) <= 9;
}

static int from_bcd(uint8_t value) {
    return (value >> 4) * 10 + (value & 0x0f);
}

bool wp5_dst_decode(const uint8_t registers[9], Wp5DstRule *rule) {
    if (!registers || !rule) return false;
    Wp5DstRule temp = {0};
    temp.offset_minutes = registers[0] & 0x7f;
    temp.fixed_date = (registers[0] & 0x80) != 0;
    /* A disabled rule can retain arbitrary stale values from an earlier
     * configuration. They are irrelevant until DST is re-enabled. */
    if (temp.offset_minutes == 0) {
        *rule = temp;
        return true;
    }
    for (int i = 1; i < 9; ++i)
        if (!valid_bcd(registers[i])) return false;
    temp.begin.month = from_bcd(registers[1]);
    temp.end.month = from_bcd(registers[5]);
    int begin_day = from_bcd(registers[2]);
    int end_day = from_bcd(registers[6]);
    if (temp.fixed_date) {
        temp.begin.date = begin_day;
        temp.end.date = end_day;
    } else {
        temp.begin.week = begin_day / 10;
        temp.begin.weekday = begin_day % 10;
        temp.end.week = end_day / 10;
        temp.end.weekday = end_day % 10;
    }
    temp.begin.hour = from_bcd(registers[3]);
    temp.begin.minute = from_bcd(registers[4]);
    temp.end.hour = from_bcd(registers[7]);
    temp.end.minute = from_bcd(registers[8]);
    if (!wp5_dst_rule_valid(&temp)) return false;
    *rule = temp;
    return true;
}

bool wp5_dst_encode(const Wp5DstRule *rule, uint8_t registers[9]) {
    if (!wp5_dst_rule_valid(rule) || !registers) return false;
    registers[0] = (rule->fixed_date ? 0x80 : 0) | rule->offset_minutes;
    registers[1] = bcd(rule->begin.month);
    registers[2] = bcd(rule->fixed_date ? rule->begin.date :
                       rule->begin.week * 10 + rule->begin.weekday);
    registers[3] = bcd(rule->begin.hour);
    registers[4] = bcd(rule->begin.minute);
    registers[5] = bcd(rule->end.month);
    registers[6] = bcd(rule->fixed_date ? rule->end.date :
                       rule->end.week * 10 + rule->end.weekday);
    registers[7] = bcd(rule->end.hour);
    registers[8] = bcd(rule->end.minute);
    return true;
}

void wp5_dst_central_europe_rule(Wp5DstRule *rule) {
    if (!rule) return;
    *rule = (Wp5DstRule){
        .offset_minutes = 60,
        .begin = {.month = 3, .week = 0, .weekday = 0, .hour = 2},
        .end   = {.month = 10, .week = 0, .weekday = 0, .hour = 3},
    };
}

void wp5_dst_western_europe_rule(Wp5DstRule *rule) {
    wp5_dst_central_europe_rule(rule);
    if (!rule) return;
    rule->begin.hour = 1;
    rule->end.hour = 2;
}

void wp5_dst_eastern_europe_rule(Wp5DstRule *rule) {
    wp5_dst_central_europe_rule(rule);
    if (!rule) return;
    rule->begin.hour = 3;
    rule->end.hour = 4;
}

void wp5_dst_us_rule(Wp5DstRule *rule) {
    if (!rule) return;
    *rule = (Wp5DstRule){
        .offset_minutes = 60,
        .begin = {.month = 3, .week = 2, .weekday = 0, .hour = 2},
        .end   = {.month = 11, .week = 1, .weekday = 0, .hour = 2},
    };
}

/* Calendar days since 2000-01-01, like the firmware's DST time code. */
static int64_t wall_seconds(int year, int month, int day, int hour, int minute) {
    int64_t days = 0;
    for (int y = 2000; y < year; ++y) days += leap(y) ? 366 : 365;
    for (int m = 1; m < month; ++m) days += days_in_month(year, m);
    return (days + day - 1) * 86400 + hour * 3600 + minute * 60;
}

static int weekday(int year, int month, int day) {
    return (int)(((wall_seconds(year, month, day, 0, 0) / 86400 + 6) % 7));
}

static bool transition_wall_seconds(const Wp5DstRule *rule,
                                    const Wp5DstTimeSpec *spec, int year,
                                    int64_t *result) {
    int days = days_in_month(year, spec->month);
    int date;
    if (rule->fixed_date) {
        date = spec->date;
    } else if (spec->week == 0) {
        date = days - (weekday(year, spec->month, days) - spec->weekday + 7) % 7;
    } else {
        date = 1 + (spec->weekday - weekday(year, spec->month, 1) + 7) % 7 +
               (spec->week - 1) * 7;
    }
    if (date < 1 || date > days) return false;
    *result = wall_seconds(year, spec->month, date, spec->hour, spec->minute);
    return true;
}

/* Classify a wall-clock sample away from the repeated/nonexistent interval.
 * The firmware defines begin and end in the local time immediately before the
 * transition; after entering DST the first valid wall time is begin + offset. */
static bool expected_dst_on_wall(const Wp5DstRule *rule, const struct tm *local,
                                 bool *expected) {
    int year = local->tm_year + 1900;
    if (year < 2000 || year > 2099) return false;
    int64_t begin, end;
    if (!transition_wall_seconds(rule, &rule->begin, year, &begin) ||
        !transition_wall_seconds(rule, &rule->end, year, &end)) return false;
    int64_t wall = wall_seconds(year, local->tm_mon + 1, local->tm_mday,
                                local->tm_hour, local->tm_min);
    int64_t start_dst = begin + (int64_t)rule->offset_minutes * 60;
    *expected = begin < end ? (wall >= start_dst && wall < end) :
                              (wall >= start_dst || wall < end);
    return true;
}

/* Check the exact transition minute, not merely the correct season.
 * A rule that differs by an hour could pass monthly/daytime spot checks but
 * still make the firmware and Linux disagree during the actual clock change. */
static bool transition_matches_timezone(const Wp5DstRule *rule,
                                        const Wp5DstTimeSpec *spec,
                                        int year, bool entering) {
    int64_t wall;
    if (!transition_wall_seconds(rule, spec, year, &wall)) return false;
    const time_t epoch_2000 = 946684800;
    time_t calendar_as_utc = (time_t)wall + epoch_2000;
    struct tm before_change;
    if (!gmtime_r(&calendar_as_utc, &before_change)) return false;
    /* The rule's transition is specified in wall time BEFORE the change. */
    before_change.tm_isdst = entering ? 0 : 1;
    time_t instant = mktime(&before_change);
    if (instant == (time_t)-1) return false;
    time_t just_before = instant - 1;
    struct tm prev, next;
    if (!localtime_r(&just_before, &prev) ||
        !localtime_r(&instant, &next)) return false;
    if ((prev.tm_isdst > 0) != !entering ||
        (next.tm_isdst > 0) != entering) return false;
#ifdef __USE_MISC
    int64_t shift = (int64_t)next.tm_gmtoff - prev.tm_gmtoff;
    if (shift != (entering ? 1 : -1) * (int64_t)rule->offset_minutes * 60)
        return false;
#endif
    return true;
}

bool wp5_dst_matches_system_timezone(const Wp5DstRule *rule, time_t now) {
    if (!wp5_dst_rule_valid(rule)) return false;
    if (rule->offset_minutes == 0) return true;
    struct tm current;
    if (!localtime_r(&now, &current)) return false;
    int year = current.tm_year + 1900;
    if (year < 2000 || year >= 2099) return false;

    /* Both years are checked because an apparently matching rule in winter
     * can otherwise go unnoticed until the next summer transition. */
    int64_t standard_offset = INT64_MIN, dst_offset = INT64_MIN;
    for (int y = year; y <= year + 1; ++y) {
        if (!transition_matches_timezone(rule, &rule->begin, y, true) ||
            !transition_matches_timezone(rule, &rule->end, y, false)) return false;
        for (int m = 1; m <= 12; ++m) {
            for (int d = 1; d <= 28; d += 3) {
                struct tm sample = {.tm_year = y - 1900, .tm_mon = m - 1,
                                    .tm_mday = d, .tm_hour = 12, .tm_isdst = -1};
                time_t value = mktime(&sample);
                struct tm local;
                if (value == (time_t)-1 || !localtime_r(&value, &local) ||
                    local.tm_isdst < 0) return false;
                bool expected = false;
                if (!expected_dst_on_wall(rule, &local, &expected) ||
                    expected != (local.tm_isdst > 0)) return false;
#ifdef __USE_MISC
                if (local.tm_isdst > 0) dst_offset = local.tm_gmtoff;
                else standard_offset = local.tm_gmtoff;
#endif
            }
        }
    }
#ifdef __USE_MISC
    if (standard_offset == INT64_MIN || dst_offset == INT64_MIN ||
        dst_offset - standard_offset != (int64_t)rule->offset_minutes * 60)
        return false;
#endif
    return true;
}

/* Compare an RTC wall-clock snapshot with a bracket of Linux local times.
 * Interpret all three wall times on a neutral calendar axis via timegm(),
 * so the autumn repeated hour is not accidentally disambiguated by mktime().
 * The caller is responsible for guarding I2C reads and DST_APPLIED changes. */
bool wp5_dst_rtc_matches_system(const struct tm *rtc_wall,
                                time_t before_read, time_t after_read,
                                int tolerance_seconds, bool *is_dst) {
    if (!rtc_wall || !is_dst || before_read == (time_t)-1 ||
        after_read == (time_t)-1 || after_read < before_read ||
        after_read - before_read > 10 || tolerance_seconds < 0)
        return false;
    if (rtc_wall->tm_year + 1900 < 2000 || rtc_wall->tm_year + 1900 > 2099 ||
        rtc_wall->tm_mon < 0 || rtc_wall->tm_mon > 11 ||
        rtc_wall->tm_mday < 1 || rtc_wall->tm_mday > 31 ||
        rtc_wall->tm_hour < 0 || rtc_wall->tm_hour > 23 ||
        rtc_wall->tm_min < 0 || rtc_wall->tm_min > 59 ||
        rtc_wall->tm_sec < 0 || rtc_wall->tm_sec > 59)
        return false;

    struct tm rtc = *rtc_wall;
    rtc.tm_isdst = 0;
    time_t rtc_seconds = timegm(&rtc);
    if (rtc_seconds == (time_t)-1) return false;
    struct tm roundtrip;
    if (!gmtime_r(&rtc_seconds, &roundtrip) ||
        roundtrip.tm_year != rtc_wall->tm_year ||
        roundtrip.tm_mon != rtc_wall->tm_mon ||
        roundtrip.tm_mday != rtc_wall->tm_mday ||
        roundtrip.tm_hour != rtc_wall->tm_hour ||
        roundtrip.tm_min != rtc_wall->tm_min ||
        roundtrip.tm_sec != rtc_wall->tm_sec)
        return false;

    struct tm start, end;
    if (!localtime_r(&before_read, &start) ||
        !localtime_r(&after_read, &end) ||
        start.tm_isdst < 0 || end.tm_isdst < 0 ||
        (start.tm_isdst > 0) != (end.tm_isdst > 0))
        return false;
    *is_dst = end.tm_isdst > 0;
    time_t start_wall = timegm(&start);
    time_t end_wall = timegm(&end);
    if (start_wall == (time_t)-1 || end_wall == (time_t)-1 ||
        end_wall < start_wall)
        return false;
    return difftime(rtc_seconds, start_wall) >= -tolerance_seconds &&
           difftime(rtc_seconds, end_wall) <= tolerance_seconds;
}
