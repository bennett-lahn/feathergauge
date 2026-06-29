#pragma once

#include <stdint.h>
#include <string.h>
#include "mock_registry.h"

// Alarm mode constants matching RTClib so firmware compiles unchanged on native builds.

typedef uint8_t Ds3231SqwPinMode;
#define DS3231_OFF       0x01
#define DS3231_SquareWave1Hz 0x00

typedef uint8_t Ds3231Alarm1Mode;
#define DS3231_A1_PerSecond 0x0F
#define DS3231_A1_Second    0x0E
#define DS3231_A1_Minute    0x0C
#define DS3231_A1_Hour      0x08
#define DS3231_A1_Date      0x00
#define DS3231_A1_Day       0x10

typedef uint8_t Ds3231Alarm2Mode;
#define DS3231_A2_PerMinute 0x0E
#define DS3231_A2_Minute    0x0C
#define DS3231_A2_Hour      0x08
#define DS3231_A2_Date      0x00

// Duration type used for burst elapsed time and alarm offsets (mirrors RTClib TimeSpan).
// Used by enterBurstDeepSleep() to compute the next alarm after a burst window ends.
class TimeSpan {
public:
    int32_t _seconds;

    TimeSpan() : _seconds(0) {}
    explicit TimeSpan(int32_t s) : _seconds(s) {}
    TimeSpan(int16_t days, int8_t hours, int8_t minutes, int8_t seconds)
        : _seconds((int32_t)days * 86400L + (int32_t)hours * 3600L
                   + (int32_t)minutes * 60L + seconds) {}

    int32_t totalseconds() const { return _seconds; }
    int16_t days()    const { return (int16_t)(_seconds / 86400L); }
    int8_t  hours()   const { return (int8_t)((_seconds % 86400L) / 3600L); }
    int8_t  minutes() const { return (int8_t)((_seconds % 3600L) / 60L); }
    int8_t  seconds() const { return (int8_t)(_seconds % 60L); }

    TimeSpan operator+(const TimeSpan& o) const { return TimeSpan(_seconds + o._seconds); }
    TimeSpan operator-(const TimeSpan& o) const { return TimeSpan(_seconds - o._seconds); }
};

// Full calendar arithmetic (not stubbed) so temporal tests validate leap years and midnight rollover.
namespace _rtc_detail {

static const uint8_t kDaysInMonth[] = {31,28,31,30,31,30,31,31,30,31,30,31};

static inline bool isLeap(uint16_t y) {
    return (y % 4 == 0) && ((y % 100 != 0) || (y % 400 == 0));
}

static inline uint32_t dateToUnix(uint16_t yr, uint8_t mo, uint8_t dy,
                                   uint8_t hr, uint8_t mi, uint8_t sc) {
    uint16_t year = yr; uint8_t month = mo; uint8_t day = dy;
    uint8_t hour = hr; uint8_t minute = mi; uint8_t second = sc;
    uint32_t days = 0;
    for (uint16_t y = 1970; y < year; y++)
        days += isLeap(y) ? 366u : 365u;
    for (uint8_t m = 1; m < month; m++) {
        days += kDaysInMonth[m - 1];
        if (m == 2 && isLeap(year)) days++;
    }
    days += (uint32_t)(day - 1);
    return days * 86400UL + (uint32_t)hour * 3600UL + (uint32_t)minute * 60UL + second;
}

static inline void unixToDate(uint32_t t,
                               uint16_t& year, uint8_t& month, uint8_t& day,
                               uint8_t& hour,  uint8_t& minute, uint8_t& second) {
    second = (uint8_t)(t % 60);
    minute = (uint8_t)((t % 3600) / 60);
    hour   = (uint8_t)((t % 86400) / 3600);

    uint32_t d = t / 86400;
    uint16_t y = 1970;
    while (true) {
        uint32_t diy = isLeap(y) ? 366u : 365u;
        if (d < diy) break;
        d -= diy;
        y++;
    }
    year = y;

    uint8_t m = 1;
    while (m <= 12) {
        uint8_t dim = kDaysInMonth[m - 1];
        if (m == 2 && isLeap(y)) dim = 29;
        if (d < dim) break;
        d -= dim;
        m++;
    }
    month = m;
    day   = (uint8_t)(d + 1);
}

} // namespace _rtc_detail

class DateTime {
public:
    uint32_t _unix;

    DateTime() : _unix(0) {}
    explicit DateTime(uint32_t t) : _unix(t) {}
    DateTime(uint16_t year, uint8_t month, uint8_t day,
             uint8_t hour = 0, uint8_t minute = 0, uint8_t second = 0)
        : _unix(_rtc_detail::dateToUnix(year, month, day, hour, minute, second)) {}

    // Stub for F(__DATE__)/F(__TIME__) constructor used only in production setup() if RTC power is lost.
    DateTime(const char*, const char*) : _unix(0) {}

    uint32_t unixtime() const { return _unix; }

    uint16_t year()   const { uint16_t y; uint8_t mo,d,h,mi,s; _rtc_detail::unixToDate(_unix,y,mo,d,h,mi,s); return y; }
    uint8_t  month()  const { uint16_t y; uint8_t mo,d,h,mi,s; _rtc_detail::unixToDate(_unix,y,mo,d,h,mi,s); return mo; }
    uint8_t  day()    const { uint16_t y; uint8_t mo,d,h,mi,s; _rtc_detail::unixToDate(_unix,y,mo,d,h,mi,s); return d; }
    uint8_t  hour()   const { return (uint8_t)((_unix % 86400) / 3600); }
    uint8_t  minute() const { return (uint8_t)((_unix % 3600) / 60); }
    uint8_t  second() const { return (uint8_t)(_unix % 60); }

    DateTime operator+(const TimeSpan& ts) const { return DateTime(_unix + (uint32_t)ts._seconds); }
    DateTime operator-(const TimeSpan& ts) const { return DateTime(_unix - (uint32_t)ts._seconds); }
    TimeSpan operator-(const DateTime& o)  const { return TimeSpan((int32_t)(_unix - o._unix)); }

    bool operator==(const DateTime& o) const { return _unix == o._unix; }
    bool operator!=(const DateTime& o) const { return _unix != o._unix; }
    bool operator< (const DateTime& o) const { return _unix <  o._unix; }
    bool operator<=(const DateTime& o) const { return _unix <= o._unix; }
    bool operator> (const DateTime& o) const { return _unix >  o._unix; }
    bool operator>=(const DateTime& o) const { return _unix >= o._unix; }
};

// Stand-in for RTClib RTC_DS3231: time comes from mockReg.input.rtcNowQueue, not I2C.
// Alarm and adjust calls are logged to mockReg.audit for scheduling and setup-path verification.
class RTC_DS3231 {
public:
    // Returns mockReg.input.rtcBeginResult; false makes setup() call error(2).
    bool begin()    { return mockReg.input.rtcBeginResult; }
    // No-op: square-wave output is unused in this firmware build.
    void disable32K()                              {}
    // Logs which alarm was cleared; tests verify alarm teardown before sleep transitions.
    void clearAlarm(int n)                         { mockReg.audit.clearAlarmCalls.push_back(n); }
    // Logs disable calls; firmware disables alarms when leaving continuous 1 Hz mode.
    void disableAlarm(int n)                       { mockReg.audit.disableAlarmCalls.push_back(n); }
    void writeSqwPinMode(Ds3231SqwPinMode)         {}
    // When true, setup() calls adjust() with compile-time date (lost-power recovery path).
    bool lostPower()                               { return mockReg.input.rtcLostPower; }

    // Records compile-time adjust in audit; production uses this when the RTC battery was dead.
    // Tests assert adjustCalled and adjustedToUnix without needing a real I2C clock chip.
    void adjust(const DateTime& dt)                { mockReg.audit.adjustedToUnix = dt.unixtime(); mockReg.audit.adjustCalled = true; }

    // Pops mockReg.input.rtcNowQueue; tests push timestamps in the order firmware will call now().
    // currentDateTime and makeFileName() both depend on this returning scripted times.
    DateTime now()                                 { return DateTime(mockReg.popNow()); }

    // Logs alarm target and mode so tests can verify per-second, hourly, and date-based wake scheduling.
    // setAlarm1Calls is the primary audit trail for burst sleep and delay-start timing tests.
    void setAlarm1(const DateTime& dt, Ds3231Alarm1Mode mode) {
        mockReg.audit.setAlarm1Calls.push_back({dt.unixtime(), (uint8_t)mode});
    }
    // Alarm 2 is unused in current firmware; stub satisfies the RTClib API surface.
    void setAlarm2(const DateTime& dt, Ds3231Alarm2Mode mode) {
        (void)dt; (void)mode;
    }
};
