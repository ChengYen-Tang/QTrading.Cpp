#include <gtest/gtest.h>

#include "Time/ReplayTimeRange.hpp"

namespace {

void set_env_var(const char* key, const char* value)
{
#ifdef _WIN32
    _putenv_s(key, value);
#else
    setenv(key, value, 1);
#endif
}

void unset_env_var(const char* key)
{
#ifdef _WIN32
    _putenv_s(key, "");
#else
    unsetenv(key);
#endif
}

class ScopedEnvVar {
public:
    ScopedEnvVar(const char* key, const char* value)
        : key_(key)
    {
        set_env_var(key_.c_str(), value);
    }

    ~ScopedEnvVar()
    {
        unset_env_var(key_.c_str());
    }

private:
    std::string key_;
};

} // namespace

TEST(ReplayTimeRangeTests, DateToUnixMsUtcConvertsFullDayRange)
{
    const auto start = QTrading::Utils::Time::DateYmdToUnixMsUtc("2023-02-03", false);
    const auto end = QTrading::Utils::Time::DateYmdToUnixMsUtc("2023-02-03", true);

    ASSERT_TRUE(start.has_value());
    ASSERT_TRUE(end.has_value());
    EXPECT_EQ(*start, 1675382400000ULL);
    EXPECT_EQ(*end, 1675468799999ULL);
}

TEST(ReplayTimeRangeTests, ParseFromDateEnvUsesDateWhenMsAbsent)
{
    ScopedEnvVar start_date("QTR_SIM_START_DATE", "2023-02-03");
    ScopedEnvVar end_date("QTR_SIM_END_DATE", "2023-02-03");

    auto parsed = QTrading::Utils::Time::ParseReplayTimeRangeFromEnv();
    EXPECT_TRUE(parsed.ok()) << parsed.error;
    ASSERT_TRUE(parsed.start_ms.has_value());
    ASSERT_TRUE(parsed.end_ms.has_value());
    EXPECT_EQ(*parsed.start_ms, 1675382400000ULL);
    EXPECT_EQ(*parsed.end_ms, 1675468799999ULL);
}

TEST(ReplayTimeRangeTests, ParseFromMsEnvOverridesDateEnv)
{
    ScopedEnvVar start_date("QTR_SIM_START_DATE", "2023-02-03");
    ScopedEnvVar end_date("QTR_SIM_END_DATE", "2023-02-03");
    ScopedEnvVar start_ms("QTR_SIM_START_TS_MS", "60000");
    ScopedEnvVar end_ms("QTR_SIM_END_TS_MS", "120000");

    auto parsed = QTrading::Utils::Time::ParseReplayTimeRangeFromEnv();
    EXPECT_TRUE(parsed.ok()) << parsed.error;
    ASSERT_TRUE(parsed.start_ms.has_value());
    ASSERT_TRUE(parsed.end_ms.has_value());
    EXPECT_EQ(*parsed.start_ms, 60000ULL);
    EXPECT_EQ(*parsed.end_ms, 120000ULL);
}

TEST(ReplayTimeRangeTests, InvalidDateRangeReturnsError)
{
    ScopedEnvVar start_date("QTR_SIM_START_DATE", "2023-02-04");
    ScopedEnvVar end_date("QTR_SIM_END_DATE", "2023-02-03");

    auto parsed = QTrading::Utils::Time::ParseReplayTimeRangeFromEnv();
    EXPECT_FALSE(parsed.ok());
    EXPECT_FALSE(parsed.error.empty());
}

