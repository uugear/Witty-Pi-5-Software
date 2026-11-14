#ifndef WP5_DST_RULE_H
#define WP5_DST_RULE_H

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

/* DST transitions are expressed in local wall time BEFORE each change. */
typedef struct {
    uint8_t month;
    uint8_t week;       /* 0=last, 1..5=nth; used in recurring mode */
    uint8_t weekday;    /* Sunday=0, ... Saturday=6 */
    uint8_t date;       /* Used in fixed-date mode */
    uint8_t hour;
    uint8_t minute;
} Wp5DstTimeSpec;

typedef struct {
    uint8_t offset_minutes; /* 0=disabled; 1..127 otherwise */
    bool fixed_date;
    Wp5DstTimeSpec begin;
    Wp5DstTimeSpec end;
} Wp5DstRule;

bool wp5_dst_rule_valid(const Wp5DstRule *rule);
bool wp5_dst_decode(const uint8_t registers[9], Wp5DstRule *rule);
bool wp5_dst_encode(const Wp5DstRule *rule, uint8_t registers[9]);
void wp5_dst_central_europe_rule(Wp5DstRule *rule);
void wp5_dst_western_europe_rule(Wp5DstRule *rule);
void wp5_dst_eastern_europe_rule(Wp5DstRule *rule);
void wp5_dst_us_rule(Wp5DstRule *rule);

/* Compare the rule against the host's local timezone transitions. A mismatch
 * means that RTC local time and system local time cannot safely be synced. */
bool wp5_dst_matches_system_timezone(const Wp5DstRule *rule, time_t now);
/* Validate a bracketed RTC wall-clock snapshot without modifying either clock.
 * On success is_dst receives the Linux DST state for R53 initialization. */
bool wp5_dst_rtc_matches_system(const struct tm *rtc_wall,
                                time_t before_read, time_t after_read,
                                int tolerance_seconds, bool *is_dst);

#endif
