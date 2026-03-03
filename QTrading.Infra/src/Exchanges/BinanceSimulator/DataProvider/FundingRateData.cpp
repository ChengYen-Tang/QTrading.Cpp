#include "Exchanges/BinanceSimulator/DataProvider/FundingRateData.hpp"
#include <algorithm>
#include <charconv>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string_view>

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

#ifdef _WIN32
static std::wstring utf8_to_wide(const std::string& utf8)
{
    if (utf8.empty()) {
        return std::wstring();
    }

    auto convert = [](UINT codepage, const std::string& input) -> std::wstring {
        const int len = MultiByteToWideChar(codepage, 0, input.data(),
            static_cast<int>(input.size()), nullptr, 0);
        if (len <= 0) {
            return std::wstring();
        }
        std::wstring out(static_cast<size_t>(len), L'\0');
        MultiByteToWideChar(codepage, 0, input.data(),
            static_cast<int>(input.size()), out.data(), len);
        return out;
    };

    std::wstring out = convert(CP_UTF8, utf8);
    if (!out.empty()) {
        return out;
    }

    return convert(CP_ACP, utf8);
}
#endif

static inline std::string_view next_field(std::string_view& s)
{
    const size_t pos = s.find(',');
    if (pos == std::string_view::npos) {
        auto out = s;
        s = std::string_view{};
        return out;
    }
    auto out = s.substr(0, pos);
    s.remove_prefix(pos + 1);
    return out;
}

template <typename T>
static bool parse_int(std::string_view sv, T& out)
{
    sv = std::string_view{ sv.data(), sv.size() };
    T v{};
    auto [p, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), v);
    if (ec != std::errc{} || p != sv.data() + sv.size()) return false;
    out = v;
    return true;
}

static bool parse_double(std::string_view sv, double& out)
{
    double v{};
    auto [p, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), v);
    if (ec != std::errc{} || p != sv.data() + sv.size()) return false;
    out = v;
    return true;
}

static bool parse_funding_line(std::string_view line, FundingRateDto& out)
{
    // Expected columns:
    // 0 FundingTime,1 Rate,2 MarkPrice(optional)
    std::string_view s = line;
    if (s.empty()) return false;

    auto f0 = next_field(s);
    auto f1 = next_field(s);
    auto f2 = next_field(s);

    if (f0.empty() || f1.empty()) {
        return false;
    }

    uint64_t fundingTime = 0;
    double rate = 0.0;

    if (!parse_int<uint64_t>(f0, fundingTime)) return false;
    if (!parse_double(f1, rate)) return false;

    std::optional<double> mark;
    if (!f2.empty()) {
        double v = 0.0;
        if (!parse_double(f2, v)) return false;
        mark = v;
    }

    out = FundingRateDto(fundingTime, rate, mark);
    return true;
}

} // namespace

/// @brief Construct FundingRateData for a given symbol by loading CSV.
/// @param symbol Trading symbol (e.g., "BTCUSDT").
/// @param csv_file Path to funding CSV.
FundingRateData::FundingRateData(const std::string& symbol, const std::string& csv_file) : symbol(symbol) {
    load_csv(csv_file);
}

/// @brief Load FundingRateDto entries from a CSV file into `rates`.
///        Lines with insufficient tokens or parse errors are skipped.
/// @param csv_file Path to CSV file.
void FundingRateData::load_csv(const std::string& csv_file) {
#ifdef _WIN32
    const std::filesystem::path path(utf8_to_wide(csv_file));
#else
    const std::filesystem::path path(csv_file);
#endif
    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open file: " + csv_file);
    }

    rates.clear();
    rates.reserve(1 << 12);

    std::string line;

    // Skip header
    if (!std::getline(file, line)) {
        throw std::runtime_error("CSV file is empty or cannot read header: " + csv_file);
    }

    bool monotonic = true;
    uint64_t last_ts = 0;
    bool has_last = false;

    while (std::getline(file, line)) {
        FundingRateDto fr{};
        if (!parse_funding_line(std::string_view(line), fr)) {
            continue;
        }

        if (has_last) {
            if (fr.FundingTime < last_ts) {
                monotonic = false;
            }
        }
        last_ts = fr.FundingTime;
        has_last = true;

        rates.emplace_back(std::move(fr));
    }

    if (!monotonic) {
        std::sort(rates.begin(), rates.end(),
            [](const FundingRateDto& a, const FundingRateDto& b) {
                return a.FundingTime < b.FundingTime;
            });
    }
}

const FundingRateDto& FundingRateData::get_latest() const {
    return rates.back();
}

const FundingRateDto& FundingRateData::get_funding(size_t index) const {
    if (index < rates.size()) {
        return rates[index];
    }
    throw std::out_of_range("Funding index out of range");
}

size_t FundingRateData::get_count() const {
    return rates.size();
}

size_t FundingRateData::upper_bound_ts(uint64_t ts) const {
    const auto it = std::upper_bound(rates.begin(), rates.end(), ts,
        [](uint64_t value, const FundingRateDto& entry) {
            return value < entry.FundingTime;
        });
    return static_cast<size_t>(std::distance(rates.begin(), it));
}

FundingRateData::iterator FundingRateData::begin() {
    return rates.begin();
}

FundingRateData::iterator FundingRateData::end() {
    return rates.end();
}

FundingRateData::const_iterator FundingRateData::begin() const {
    return rates.begin();
}

FundingRateData::const_iterator FundingRateData::end() const {
    return rates.end();
}

FundingRateData::const_iterator FundingRateData::cbegin() const {
    return rates.cbegin();
}

FundingRateData::const_iterator FundingRateData::cend() const {
    return rates.cend();
}
