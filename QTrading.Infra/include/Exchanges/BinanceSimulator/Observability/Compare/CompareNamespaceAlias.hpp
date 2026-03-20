#pragma once

#include "Exchanges/BinanceSimulator/Diagnostics/Compare/BinanceCompareBridge.hpp"
#include "Exchanges/BinanceSimulator/Diagnostics/Compare/DiagnosticReportFormatter.hpp"
#include "Exchanges/BinanceSimulator/Diagnostics/Compare/DifferentialReplayRunner.hpp"
#include "Exchanges/BinanceSimulator/Diagnostics/Compare/LegacyLogRowCompare.hpp"
#include "Exchanges/BinanceSimulator/Diagnostics/Compare/PerformanceCiGate.hpp"
#include "Exchanges/BinanceSimulator/Diagnostics/Compare/PerformanceEvidenceFormatter.hpp"
#include "Exchanges/BinanceSimulator/Diagnostics/Compare/ReplayCompareCiGate.hpp"
#include "Exchanges/BinanceSimulator/Diagnostics/Compare/ReplayCompareTestHarness.hpp"
#include "Exchanges/BinanceSimulator/Diagnostics/Compare/StepCompareModel.hpp"
#include "Exchanges/BinanceSimulator/Diagnostics/Compare/V2ReplayScenarioPack.hpp"

namespace QTrading::Infra::Exchanges::BinanceSim::Observability {

// Phase 4 naming convergence: expose compare diagnostics under Observability.
namespace Compare = ::QTrading::Infra::Exchanges::BinanceSim::Diagnostics::Compare;

} // namespace QTrading::Infra::Exchanges::BinanceSim::Observability
