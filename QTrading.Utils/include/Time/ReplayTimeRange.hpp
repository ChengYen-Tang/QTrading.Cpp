#pragma once

#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <optional>
#include <string>
#include <string_view>

namespace QTrading::Utils::Time {

struct ReplayTimeRange {
    std::optional<uint64_t> start_ms;
    std::optional<uint64_t> end_ms;
    std::string error;

    [[nodiscard]] bool ok() const { return error.empty(); }
};

inline bool IsLeapYear(int year)
{
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

inline int DaysInMonth(int year, int month)
{
    static constexpr int kDays[12] = { 31,28,31,30,31,30,31,31,30,31,30,31 };
    if (month < 1 || month > 12) {
        return 0;
    }
    if (month == 2 && IsLeapYear(year)) {
        return 29;
    }
    return kDays[month - 1];
}

inline bool ParseInt(std::string_view input, int& out)
{
    int value = 0;
    const auto* begin = input.data();
    const auto* end = input.data() + input.size();
    const auto [ptr, ec] = std::from_chars(begin, end, value);
    if (ec != std::errc() || ptr != end) {
        return false;
    }
    out = value;
    return true;
}

inline bool ParseUint64(std::string_view input, uint64_t& out)
{
    uint64_t value = 0;
    const auto* begin = input.data();
    const auto* end = input.data() + input.size();
    const auto [ptr, ec] = std::from_chars(begin, end, value);
    if (ec != std::errc() || ptr != end) {
        return false;
    }
    out = value;
    return true;
}

inline bool ParseDateYmd(std::string_view s, int& year, int& month, int& day)
{
    if (s.size() != 10 || s[4] != '-' || s[7] != '-') {
        return false;
    }

    if (!ParseInt(s.substr(0, 4), year) ||
        !ParseInt(s.substr(5, 2), month) ||
        !ParseInt(s.substr(8, 2), day)) {
        return false;
    }

    if (year < 1970 || month < 1 || month > 12) {
        return false;
    }

    const int dim = DaysInMonth(year, month);
    if (day < 1 || day > dim) {
        return false;
    }

    return true;
}

inline std::optional<uint64_t> DateYmdToUnixMsUtc(std::string_view s, bool end_of_day)
{
    int year = 0;
    int month = 0;
    int day = 0;
    if (!ParseDateYmd(s, year, month, day)) {
        return std::nullopt;
    }

    std::tm tm{};
    tm.tm_year = year - 1900;
    tm.tm_mon = month - 1;
    tm.tm_mday = day;
    tm.tm_hour = end_of_day ? 23 : 0;
    tm.tm_min = end_of_day ? 59 : 0;
    tm.tm_sec = end_of_day ? 59 : 0;

#ifdef _WIN32
    const std::time_t ts = _mkgmtime(&tm);
#else
    const std::time_t ts = timegm(&tm);
#endif
    if (ts < 0) {
        return std::nullopt;
    }

    uint64_t ms = static_cast<uint64_t>(ts) * 1000ULL;
    if (end_of_day) {
        ms += 999ULL;
    }
    return ms;
}

inline ReplayTimeRange ParseReplayTimeRangeFromEnv(
    const char* start_ms_key = "QTR_SIM_START_TS_MS",
    const char* end_ms_key = "QTR_SIM_END_TS_MS",
    const char* start_date_key = "QTR_SIM_START_DATE",
    const char* end_date_key = "QTR_SIM_END_DATE")
{
    ReplayTimeRange out{};

    const char* raw_start_ms = std::getenv(start_ms_key);
    const char* raw_end_ms = std::getenv(end_ms_key);
    const char* raw_start_date = std::getenv(start_date_key);
    const char* raw_end_date = std::getenv(end_date_key);

    if (raw_start_ms != nullptr && raw_start_ms[0] != '\0') {
        uint64_t parsed = 0;
        if (!ParseUint64(raw_start_ms, parsed)) {
            out.error = std::string("Invalid ") + start_ms_key + ". Expect uint64 unix ms.";
            return out;
        }
        out.start_ms = parsed;
    }
    else if (raw_start_date != nullptr && raw_start_date[0] != '\0') {
        out.start_ms = DateYmdToUnixMsUtc(raw_start_date, false);
        if (!out.start_ms.has_value()) {
            out.error = std::string("Invalid ") + start_date_key + ". Expect YYYY-MM-DD.";
            return out;
        }
    }

    if (raw_end_ms != nullptr && raw_end_ms[0] != '\0') {
        uint64_t parsed = 0;
        if (!ParseUint64(raw_end_ms, parsed)) {
            out.error = std::string("Invalid ") + end_ms_key + ". Expect uint64 unix ms.";
            return out;
        }
        out.end_ms = parsed;
    }
    else if (raw_end_date != nullptr && raw_end_date[0] != '\0') {
        out.end_ms = DateYmdToUnixMsUtc(raw_end_date, true);
        if (!out.end_ms.has_value()) {
            out.error = std::string("Invalid ") + end_date_key + ". Expect YYYY-MM-DD.";
            return out;
        }
    }

    if (out.start_ms.has_value() && out.end_ms.has_value() && *out.end_ms < *out.start_ms) {
        out.error = "Replay end time must be >= start time.";
        return out;
    }

    return out;
}

} // namespace QTrading::Utils::Time

