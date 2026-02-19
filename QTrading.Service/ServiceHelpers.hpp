#pragma once

#include "Dto/Trading/InstrumentSpec.hpp"

#include <optional>
#include <string>

namespace QTrading::Service::Helpers {

std::string JsonEscape(const std::string& input);
std::string Utf8Path(const char8_t* path);

void SetEnvVar(const char* key, const std::string& value);
void InstallSignalHandlers();
bool StopRequested();

std::string InstrumentTypeToString(std::optional<QTrading::Dto::Trading::InstrumentType> type);

} // namespace QTrading::Service::Helpers
