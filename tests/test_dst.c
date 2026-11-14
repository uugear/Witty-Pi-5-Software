#define _GNU_SOURCE
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../wp5dst.h"

static void zone(const char *name) {
    assert(setenv("TZ", name, 1) == 0);
    tzset();
}

static void test_eu(void) {
    Wp5DstRule rule;
    wp5_dst_central_europe_rule(&rule);
    uint8_t encoded[9];
    assert(wp5_dst_encode(&rule, encoded));
    const uint8_t expected[9] = {0x3c, 0x03, 0x00, 0x02, 0x00, 0x10, 0x00, 0x03, 0x00};
    assert(memcmp(encoded, expected, sizeof(expected)) == 0);
    Wp5DstRule decoded;
    assert(wp5_dst_decode(encoded, &decoded));
    assert(decoded.begin.week == 0 && decoded.end.month == 10);
    zone("Europe/Amsterdam");
    assert(wp5_dst_matches_system_timezone(&rule, 1780000000));
    zone("Europe/London");
    assert(!wp5_dst_matches_system_timezone(&rule, 1780000000));
    Wp5DstRule west, east;
    wp5_dst_western_europe_rule(&west);
    wp5_dst_eastern_europe_rule(&east);
    assert(wp5_dst_matches_system_timezone(&west, 1780000000));
    zone("Europe/Lisbon");
    assert(wp5_dst_matches_system_timezone(&west, 1780000000));
    zone("Europe/Helsinki");
    assert(wp5_dst_matches_system_timezone(&east, 1780000000));
    zone("UTC");
    assert(!wp5_dst_matches_system_timezone(&rule, 1780000000));
    zone("America/New_York");
    assert(!wp5_dst_matches_system_timezone(&rule, 1780000000));
}

static void test_us(void) {
    Wp5DstRule rule;
    wp5_dst_us_rule(&rule);
    uint8_t encoded[9];
    assert(wp5_dst_encode(&rule, encoded));
    const uint8_t expected[9] = {0x3c, 0x03, 0x20, 0x02, 0x00, 0x11, 0x10, 0x02, 0x00};
    assert(memcmp(encoded, expected, sizeof(expected)) == 0);
    zone("America/New_York");
    assert(wp5_dst_matches_system_timezone(&rule, 1780000000));
    zone("America/Chicago");
    assert(wp5_dst_matches_system_timezone(&rule, 1780000000));
    zone("Europe/Amsterdam");
    assert(!wp5_dst_matches_system_timezone(&rule, 1780000000));
}

static void test_custom(void) {
    Wp5DstRule rule = {.offset_minutes = 60, .fixed_date = true,
        .begin = {.month = 4, .date = 6, .hour = 2, .minute = 15},
        .end = {.month = 10, .date = 6, .hour = 3, .minute = 15}};
    uint8_t encoded[9];
    assert(wp5_dst_encode(&rule, encoded));
    assert(encoded[0] == 0xbc && encoded[2] == 0x06 && encoded[6] == 0x06);
    Wp5DstRule decoded;
    assert(wp5_dst_decode(encoded, &decoded));
    assert(decoded.fixed_date && decoded.begin.date == 6 && decoded.end.hour == 3);
    encoded[4] = 0x6a;
    assert(!wp5_dst_decode(encoded, &decoded));
    rule.begin.month = 13;
    assert(!wp5_dst_encode(&rule, encoded));
}

static void test_disabled_and_southern(void) {
    Wp5DstRule disabled = {0};
    uint8_t regs[9] = {0, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
    assert(wp5_dst_decode(regs, &disabled));
    assert(disabled.offset_minutes == 0);
    Wp5DstRule southern = {.offset_minutes = 60,
        .begin = {.month = 10, .week = 1, .weekday = 0, .hour = 2},
        .end = {.month = 4, .week = 1, .weekday = 0, .hour = 3}};
    zone("Australia/Sydney");
    assert(wp5_dst_matches_system_timezone(&southern, 1780000000));
    zone("Europe/Amsterdam");
    assert(!wp5_dst_matches_system_timezone(&southern, 1780000000));
}

static void test_fall_back(void) {
    zone("Europe/Amsterdam");
    struct tm t = {.tm_year = 2027 - 1900, .tm_mon = 9, .tm_mday = 31,
                   .tm_hour = 2, .tm_min = 30, .tm_isdst = 1};
    time_t first = mktime(&t);
    t = (struct tm){.tm_year = 2027 - 1900, .tm_mon = 9, .tm_mday = 31,
                   .tm_hour = 2, .tm_min = 30, .tm_isdst = 0};
    time_t second = mktime(&t);
    assert(first != (time_t)-1 && second - first == 3600);
    struct tm local;
    localtime_r(&first, &local);
    assert(local.tm_hour == 2 && local.tm_isdst > 0);
    localtime_r(&second, &local);
    assert(local.tm_hour == 2 && local.tm_isdst == 0);
}

static struct tm rtc_wall_from_epoch(time_t epoch) {
    struct tm wall;
    assert(localtime_r(&epoch, &wall));
    return wall;
}

static void test_rtc_clock_consistency(void) {
    zone("Europe/Amsterdam");
    struct tm current = {.tm_year = 2026 - 1900, .tm_mon = 10,
                         .tm_mday = 14, .tm_hour = 14,
                         .tm_min = 15, .tm_isdst = 0};
    time_t now = mktime(&current);
    assert(now != (time_t)-1);
    struct tm rtc = rtc_wall_from_epoch(now);
    bool dst = true;
    assert(wp5_dst_rtc_matches_system(&rtc, now - 1, now + 1, 3, &dst));
    assert(!dst);
    rtc.tm_hour -= 1;
    assert(!wp5_dst_rtc_matches_system(&rtc, now - 1, now + 1, 3, &dst));
    rtc.tm_hour += 1;
    rtc.tm_sec += 5;
    assert(!wp5_dst_rtc_matches_system(&rtc, now, now, 3, &dst));
    rtc.tm_sec -= 5;
    assert(!wp5_dst_rtc_matches_system(&rtc, now + 2, now, 3, &dst));
    assert(!wp5_dst_rtc_matches_system(&rtc, now - 20, now, 3, &dst));
    rtc.tm_mon = 1; rtc.tm_mday = 30;
    assert(!wp5_dst_rtc_matches_system(&rtc, now, now, 3, &dst));

    /* During the repeated autumn hour the two identical RTC clock readings
     * have different absolute meanings; the Linux DST flag distinguishes them. */
    struct tm fall = {.tm_year = 2027 - 1900, .tm_mon = 9, .tm_mday = 31,
                      .tm_hour = 2, .tm_min = 30, .tm_isdst = 1};
    time_t first = mktime(&fall);
    fall.tm_isdst = 0;
    time_t second = mktime(&fall);
    assert(second - first == 3600);
    rtc = rtc_wall_from_epoch(first);
    assert(wp5_dst_rtc_matches_system(&rtc, first, first + 1, 3, &dst) && dst);
    assert(wp5_dst_rtc_matches_system(&rtc, second, second + 1, 3, &dst) && !dst);
    /* Reject a snapshot that straddles an actual DST transition. */
    time_t transition = first + 1800;
    assert(!wp5_dst_rtc_matches_system(&rtc, transition - 1,
                                       transition + 1, 3, &dst));

    zone("Australia/Sydney");
    struct tm summer = {.tm_year = 2027 - 1900, .tm_mon = 0, .tm_mday = 15,
                        .tm_hour = 14, .tm_isdst = 1};
    time_t summer_now = mktime(&summer);
    rtc = rtc_wall_from_epoch(summer_now);
    assert(wp5_dst_rtc_matches_system(&rtc, summer_now, summer_now, 3, &dst));
    assert(dst);
}

int main(void) {
    test_eu();
    test_us();
    test_custom();
    test_disabled_and_southern();
    test_fall_back();
    test_rtc_clock_consistency();
    puts("All DST rule and fall-back tests passed");
    return 0;
}
