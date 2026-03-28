# QTrading.Infra Testcase Migration Backlog

日期: 2026-03-28

## 文件定位

這份文件取代以 `T1/T2/T3/T4` 為主的粗粒度切法。
之後 subagent 應以「baseline test file -> test case -> test data -> 新架構落點」為最小遷移單位。

## 執行硬規則

1. 逐個 test case 遷移，不准只看舊檔名決定責任。
2. 若舊 test data 存在，必須一模一樣遷回；若舊案例是 inline scenario，必須原樣保留其語意與輸入。
3. 不准用 `baseline` 或 phase 編號當 code/test/comment 命名。
4. `class/struct` 下不能再定義 `class/struct`。
5. Infra 內部不准新增 `using Xxx = Contracts::Yyy` 這種型別回導。
6. 不准新增或漂移 `BinanceExchange` public facade。
7. 行為應遷到新架構的正確模組去測，不要被舊類別名稱綁架。
8. 高頻優先：不要為了補測試把 hot path 做厚。

## Baseline QTrading.Infra Test Files

### Exchanges\BinanceSimulator\Account\AccountPolicyInjectionTests.cpp

- Baseline source: [Exchanges\BinanceSimulator\Account\AccountPolicyInjectionTests.cpp](d:/QTrading.Cpp-baseline-7110533/QTrading.Infra/tests/Exchanges\BinanceSimulator\Account\AccountPolicyInjectionTests.cpp)
- Test data form: inline scenario only
- Current migration status: migrated to new owner module

已遷移到：
- [AccountPolicyExecutionServiceTests.cpp](d:/QTrading.Cpp/QTrading.Infra/tests/Exchanges/BinanceSimulator/Domain/AccountPolicyExecutionServiceTests.cpp)
- [AccountPolicyInjectionTests.cpp](d:/QTrading.Cpp/QTrading.Infra/tests/Exchanges/BinanceSimulator/Domain/AccountPolicyInjectionTests.cpp)

已完成：
- [x] `AccountPoliciesTest.InjectedExecutionPriceIsUsed`
- [x] `AccountPoliciesTest.InjectedFeeRatesAffectChargedFeeRate`
- [x] `AccountPoliciesTest.ContextExecutionPriceSeesMarkPrice`

最近完成批次：
- policy injection 舊案例已從舊 `Account` 入口遷到 `Domain/AccountPolicyExecutionService` 與其對應測試檔。

說明：
- 這 3 個舊案例不再綁在 `Account` 類別本身。
- 已改由新架構的 `Domain/AccountPolicyExecutionService` 承接測試責任。
- 舊的 [AccountPolicyInjectionTests.cpp](d:/QTrading.Cpp/QTrading.Infra/tests/Exchanges/BinanceSimulator/Account/AccountPolicyInjectionTests.cpp) 已刪除，不再要求 `Account` 充當 policy-driven order/matching 入口。

### Exchanges\BinanceSimulator\Account\AccountTests.cpp

- Baseline source: [Exchanges\BinanceSimulator\Account\AccountTests.cpp](d:/QTrading.Cpp-baseline-7110533/QTrading.Infra/tests/Exchanges\BinanceSimulator\Account\AccountTests.cpp)
- Test data form: inline scenario only
- Current migration status: owner remap completed; `Account` owner 已收斂後的 migrate-now 子集 restored

已遷移到：
- [AccountTests.cpp](d:/QTrading.Cpp/QTrading.Infra/tests/Exchanges/BinanceSimulator/Account/AccountTests.cpp)
- [AccountPolicyExecutionServiceTests.cpp](d:/QTrading.Cpp/QTrading.Infra/tests/Exchanges/BinanceSimulator/Domain/AccountPolicyExecutionServiceTests.cpp)
- [OrderEntryServiceTests.cpp](d:/QTrading.Cpp/QTrading.Infra/tests/Exchanges/BinanceSimulator/Domain/OrderEntryServiceTests.cpp)
- [BinanceExchangeReplayTests.cpp](d:/QTrading.Cpp/QTrading.Infra/tests/Exchanges/BinanceSimulator/BinanceExchangeReplayTests.cpp)

已完成：
- [x] `AccountTest.ConstructorAndGetters`
- [x] `AccountTest.AccountInitConfigConstructorInitializesStartingBalance`
- [x] `AccountTest.AccountInitConfigRejectsInvalidValues`
- [x] `AccountTest.DomainApisRouteByInstrumentType`
- [x] `AccountTest.DomainCancelOnlyAffectsOwnInstrumentBook`
- [x] `AccountTest.SpotAndPerpUseDifferentDefaultFeeTables`
- [x] `AccountTest.SetAndGetSymbolLeverage`
- [x] `AccountTest.SpotSymbolLeverageIsAlwaysOne`
- [x] `AccountTest.SpotBuyConsumesOnlySpotCash`
- [x] `AccountTest.PerpTradeConsumesOnlyPerpWallet`
- [x] `AccountTest.SpotSellIncreasesOnlySpotCash`
- [x] `AccountTest.SpotOpenOrderReservesOnlySpotBudget`
- [x] `AccountTest.SpotBuyPlacementRejectedWhenSpotBudgetExceededByOpenOrders`
- [x] `AccountTest.TransferBetweenLedgersRespectsAvailableBalance`
- [x] `AccountTest.ExplicitInstrumentTypeAppliesWithoutSuffixNaming`
- [x] `AccountTest.CompatibilityMode_UnspecifiedInstrumentDefaultsToPerpPolicy`
- [x] `AccountTest.StrictModeRejectsUnknownSymbolOrderPlacement`
- [x] `AccountTest.StrictModeDomainApiRejectsUnknownSymbolOrderPlacement`
- [x] `AccountTest.InstrumentFiltersRejectInvalidPriceTickAndRange`
- [x] `AccountTest.InstrumentFiltersRejectInvalidQuantityStepAndBounds`
- [x] `AccountTest.InstrumentFiltersRejectOrderByMinNotional`
- [x] `AccountTest.InstrumentFiltersPerpMarketNotionalUsesMarkPrice`
- [x] `AccountTest.SpotMarketBudgetUsesTradePriceNotMarkPrice`
- [x] `AccountTest.SpotQuoteOrderQtyMarketBuyConvertsAndLogsOriginalQuoteQty`
- [x] `AccountTest.SpotQuoteOrderQtyRequiresTradeReferencePrice`
- [x] `AccountTest.SpotBaseFeeModeBuyUsesBaseCommissionAndNotionalCashflow`
- [x] `AccountTest.SpotBaseFeeModeSellStillUsesQuoteCommission`
- [x] `AccountTest.SpotBaseFeeModeOpenOrderReserveUsesNotionalOnly`
- [x] `AccountTest.MixedBookSpotAndPerpRulesApplyByInstrumentType`
- [x] `AccountTest.SpotRejectsNakedShortAndOversell`
- [x] `AccountTest.SpotPositionHasNoMaintenanceMarginAndNoLiquidationPath`
- [x] `AccountTest.PlaceOrderSuccessCheckOpenOrders`
- [x] `AccountTest.UpdatePositionsPartialFillSameOrder`
- [x] `AccountTest.ClosePositionBySymbol`
- [x] `AccountTest.PerpClosePositionOrder_OneWayClosesFullPosition`
- [x] `AccountTest.PerpClosePositionOrder_HedgeModeClosesOnlyTargetSide`
- [x] `AccountTest.CancelOrderByID`
- [x] `AccountTest.ClientOrderIdMustBeUniqueAmongOpenOrders`
- [x] `AccountTest.StpExpireTakerRejectsCrossingIncomingOrder`
- [x] `AccountTest.StpExpireMakerCancelsConflictingRestingOrder`
- [x] `AccountTest.StpExpireBothCancelsRestingAndRejectsIncomingOrder`
- [x] `AccountTest.Liquidation`
- [x] AccountTest.LiquidationWarningZoneTriggersStagedReduction
- [x] `AccountTest.DistressedLiquidationCancelsPerpOpenOrdersBeforeReduction`
- [x] `AccountTest.PerpLiquidationDoesNotConsumeSpotPosition`
- [x] `AccountTest.HedgeModeSameSymbolOppositeDirection`
- [x] `AccountTest.SwitchingModeWithOpenPositionsFails`
- [x] `AccountTest.SwitchingModeWithoutPositionsSucceeds`
- [x] `AccountTest.SwitchingModeWithOpenOrdersFails`
- [x] `AccountTest.SingleModeAutoReduceOppositePositionOpen`
- [x] `AccountTest.HedgeModeReduceOnlyOrderRejectedByDefault`
- [x] `AccountTest.CompatibilityModeAllowsHedgeReduceOnlyWhenStrictDisabled`
- [x] `AccountTest.MergePositionsSameDirection`
- [x] `AccountTest.MergePositionsCanBeDisabledToPreserveFillLineage`
- [x] `AccountTest.MergePositionsDifferentDirectionNotMerged`
- [x] `AccountTest.CloseOnlyLongSideInHedgeMode`
- [x] `AccountTest.CloseBothSidesInHedgeMode`
- [x] `AccountTest.AdjustLeverageWithExistingPositions`
- [x] AccountTest.MaintenanceMarginUsesBracketDeductionAboveFirstTier
- [x] `AccountTest.SingleMode_MultipleSymbols`
- [x] `AccountTest.HedgeMode_MultipleSymbols_ReduceOnly`
- [x] `AccountTest.TickVolumeIsConsumedAcrossOrdersSameSymbol`
- [x] `AccountTest.ImmediatelyExecutableLimitIsTakerFee`
- [x] `AccountTest.LimitOrderFillsAtLimitPrice`
- [x] `AccountTest.TickPriceTimePriority_BuyHigherLimitFillsFirst`
- [x] `AccountTest.TickPriceTimePriority_SamePriceLowerIdFillsFirst`
- [x] `AccountTest.OhlcTrigger_BuyLimitTriggersOnLow`
- [x] `AccountTest.OhlcTrigger_SellLimitTriggersOnHigh`
- [x] AccountTest.IntraBarExpectedPath_SplitsOppositePassiveLimitVolume
- [x] AccountTest.IntraBarPathModeUsesOpenMarketabilityForTakerClassification
- [x] AccountTest.IntraBarMonteCarloPathWithFixedSeedIsDeterministic
- [x] AccountTest.LimitFillProbabilityModelUsesPenetrationAndSizeRatio
- [x] `AccountTest.HedgeMode_OrderRequiresExplicitPositionSide_IsRejectedWithoutException`
- [x] `AccountTest.ReduceOnlyWithoutReduciblePosition_IsRejectedWithoutException`
- [x] `AccountTest.HedgeModeReduceOnly_WrongSideOrNoMatchingPosition_IsRejectedWithoutException`
- [x] AccountTest.OneWayFlip_OvershootTransitionsCloseThenOpenThroughLifecycle
- [x] `AccountTest.ReduceOnly_OneWayRejectsIfWouldIncreaseExposure`
- [x] `AccountTest.ReduceOnly_HedgeMode_RequiresExplicitPositionSide`
- [x] `AccountTest.ReduceOnly_HedgeMode_DirectionMustCloseCorrectSide`
- [x] AccountTest.OpenOrderInitialMargin_MarketOrderUsesLastMarkWithBuffer
- [x] AccountTest.UnrealizedPnlUsesMarkOverrideNotTradeClose
- [x] AccountTest.OpenOrderInitialMargin_OneWayClosingDirectionDoesNotReserveMargin
- [x] AccountTest.OpenOrderInitialMargin_OneWayFlipReservesOnlyForOpeningOvershoot
- [x] AccountTest.MarketOrderFill_UsesExecutionSlippageBoundedByOHLC
- [x] AccountTest.LimitOrderFill_UsesExecutionSlippageButRespectsLimit
- [x] AccountTest.LimitOrderFill_ExecutionSlippageCanWorsenPriceWithinLimit
- [x] AccountTest.MarketImpactSlippageCurveWorsensLargeOrderMoreThanSmallOrder
- [x] AccountTest.MarketImpactSlippageRespectsLimitProtection
- [x] AccountTest.TakerProbabilityModelUsesDiscreteFeeRate
- [x] AccountTest.TickVolumeSplit_UsesTakerBuyBaseVolumePools
- [x] AccountTest.TickVolumeSplit_Heuristic_CloseNearHighBiasesSellOrders
- [x] AccountTest.TickVolumeSplit_Heuristic_CloseNearLowBiasesBuyOrders
- [x] `AccountTest.ApplyFunding_LongPays`
- [x] `AccountTest.ApplyFunding_ShortReceives`

最近完成批次：
- 帳務 owner 收斂批次：
  - `ConstructorAndGetters`
  - `AccountInitConfigConstructorInitializesStartingBalance`
  - `AccountInitConfigRejectsInvalidValues`
  - `DomainApisRouteByInstrumentType`
  - `DomainCancelOnlyAffectsOwnInstrumentBook`
  - `PlaceOrderSuccessCheckOpenOrders`
  - `UpdatePositionsPartialFillSameOrder`
  - `TickVolumeIsConsumedAcrossOrdersSameSymbol`
  - `PerpTradeConsumesOnlyPerpWallet`
  - `ApplyFunding_LongPays`
  - `ApplyFunding_ShortReceives`
- order-entry owner 遷移批次：
  - `InstrumentFiltersRejectInvalidPriceTickAndRange`
  - `InstrumentFiltersRejectInvalidQuantityStepAndBounds`
  - `InstrumentFiltersRejectOrderByMinNotional`
- exchange/replay owner 遷移批次：
  - `ExplicitInstrumentTypeAppliesWithoutSuffixNaming`
  - `CompatibilityMode_UnspecifiedInstrumentDefaultsToPerpPolicy`
  - `StrictModeRejectsUnknownSymbolOrderPlacement`
  - `StrictModeDomainApiRejectsUnknownSymbolOrderPlacement`
  - `SetAndGetSymbolLeverage`
  - `SpotSymbolLeverageIsAlwaysOne`
  - `SpotBuyConsumesOnlySpotCash`
  - `SpotSellIncreasesOnlySpotCash`
  - `SpotOpenOrderReservesOnlySpotBudget`
  - `SpotBuyPlacementRejectedWhenSpotBudgetExceededByOpenOrders`
- `SpotQuoteOrderQtyMarketBuyConvertsAndLogsOriginalQuoteQty`
- `SpotQuoteOrderQtyRequiresTradeReferencePrice`
- `SpotRejectsNakedShortAndOversell`
  - `SpotPositionHasNoMaintenanceMarginAndNoLiquidationPath`
  - `TransferBetweenLedgersRespectsAvailableBalance`
  - `SpotAndPerpUseDifferentDefaultFeeTables`
  - `SpotBaseFeeModeBuyUsesBaseCommissionAndNotionalCashflow`
  - `SpotBaseFeeModeSellStillUsesQuoteCommission`
  - `SpotBaseFeeModeOpenOrderReserveUsesNotionalOnly`
  - `ClosePositionBySymbol`
  - `CancelOrderByID`
  - `ClientOrderIdMustBeUniqueAmongOpenOrders`
  - `StpExpireTakerRejectsCrossingIncomingOrder`
  - `StpExpireMakerCancelsConflictingRestingOrder`
  - `StpExpireBothCancelsRestingAndRejectsIncomingOrder`

仍待後續批次：
  - 其餘案例仍待後續批次，例如：
    - reserve-aware available balance
    - strict/compatibility instrument policy
    - hedge mode lifecycle
    - full margin tier / mode switching / merge rules

說明：
- 這一批不再把行為硬塞回 `Account`。
- 已恢復案例會依責任分別落在 `Account`、`Domain/AccountPolicyExecutionService`、`Domain/OrderEntryService`、`BinanceExchangeReplay`。
- `Account` 目前只保留 cash-ledger / transfer / balance / state-version owner；不再承接 queue / matching / position update / policy execution。
- leverage owner 已移到 `BinanceExchange/runtime_state`，不再留在 `Account`。
- spot-budget reservation 行為目前由 `Adapters/AccountFacadeAdapter` 依 `runtime_state.orders` 計算；review 已確認這不是 blocker，但後續仍值得再收斂。
- instrument filter / min-notional 類行為已移到 `Domain/OrderEntryServiceTests.cpp`，不再掛在 `Account`。
- perp market min-notional 使用 mark price 的行為已移到 `Domain/OrderEntryServiceTests.cpp`。
- quote-order-qty / strict unknown symbol / leverage / spot cash budget / no-maintenance-margin 這批 exchange-owned 行為已移到 `BinanceExchangeReplayTests.cpp`。
- mixed spot/perp book routing 與 trade-vs-mark spot budget 行為已移到 `BinanceExchangeReplayTests.cpp`。

### Exchanges\BinanceSimulator\BinanceExchangeLogTests.cpp

- Baseline source: [Exchanges\BinanceSimulator\BinanceExchangeLogTests.cpp](d:/QTrading.Cpp-baseline-7110533/QTrading.Infra/tests/Exchanges\BinanceSimulator\BinanceExchangeLogTests.cpp)
- Test data form: mixed inline + file/serialized fixtures (inspect file before migration)
- Current migration status: pending triage

- [ ] BinanceExchangeLogTestFixture.BinanceExchangeLogUsesSinkLogger

### Exchanges\BinanceSimulator\BinanceExchangeTests.cpp

- Baseline source: [Exchanges\BinanceSimulator\BinanceExchangeTests.cpp](d:/QTrading.Cpp-baseline-7110533/QTrading.Infra/tests/Exchanges\BinanceSimulator\BinanceExchangeTests.cpp)
- Test data form: mixed inline + file/serialized fixtures (inspect file before migration)
- Current migration status: pending triage

- [ ] BinanceExchangeFixture.SymbolsSynchronisedWithHoles
- [ ] BinanceExchangeFixture.ReplayWindowFiltersKlineRangeByTimestampEnv
- [ ] BinanceExchangeFixture.ReplayWindowFiltersKlineRangeByDateEnv
- [ ] BinanceExchangeFixture.PushOnlyOnChange
- [ ] BinanceExchangeFixture.OrderLatencyBarsDelaysPlacementUntilFutureStep
- [ ] BinanceExchangeFixture.OrderLatencyBarsPublishesPendingThenAcceptedAsyncAck
- [ ] BinanceExchangeFixture.OrderLatencyBarsPublishesPendingThenRejectedAsyncAck
- [ ] BinanceExchangeFixture.SnapshotConsistent
- [ ] BinanceExchangeFixture.ConstructWithAccountInitConfig
- [ ] BinanceExchangeFixture.DomainFacadeRoutesByInstrumentType
- [ ] BinanceExchangeFixture.AccountFacadeTransfersRespectAvailability
- [ ] BinanceExchangeFixture.FundingAppliedAndDeduped
- [ ] BinanceExchangeFixture.FundingTimestampUsesInterpolatedMarkPriceBetweenBars
- [ ] BinanceExchangeFixture.PerpUnrealizedPnlUsesMarkDatasetWhenAvailable
- [ ] BinanceExchangeFixture.StatusSnapshotPriceIncludesTradeMarkAndIndex
- [ ] BinanceExchangeFixture.MarkIndexDivergenceAddsUncertaintyBand
- [ ] BinanceExchangeFixture.MarkIndexStressBlocksNewPerpOpeningOrders
- [ ] BinanceExchangeFixture.DisabledSimulatorRiskOverlayKeepsDiagnosticsButDoesNotBlockOrders
- [ ] BinanceExchangeFixture.MarkIndexStressAllowsReduceOnlyPerpClose
- [ ] BinanceExchangeFixture.MarkIndexWarningAutoDeleveragesPerpLeverage
- [ ] BinanceExchangeFixture.FundingInterpolationPrefersConfiguredMarkDataset
- [ ] BinanceExchangeFixture.FundingApplyTimingControlsSameTimestampFunding
- [ ] BinanceExchangeFixture.MarketSnapshotFundingUsesPreviousPeriodUntilUpdateIsApplied
- [ ] BinanceExchangeFixture.FundingWithoutMarkSourceIsSkipped
- [ ] BinanceExchangeFixture.NoFundingPathKeepsBalance
- [ ] BinanceExchangeFixture.ExplicitInstrumentTypeSpotWorksWithoutSuffixSymbol
- [ ] BinanceExchangeFixture.LegacyLeverageWrapperMatchesPerpFacade
- [ ] BinanceExchangeFixture.StatusSnapshotExposesDualLedgerTotals
- [ ] BinanceExchangeFixture.StatusSnapshotOutputsUncertaintyBands
- [ ] BinanceExchangeFixture.StrictModeAllowsDatasetSymbolWithoutExplicitInstrumentType
- [ ] BinanceExchangeFixture.StrictModeRejectsOrdersForUnknownDatasetSymbol

### Exchanges\BinanceSimulator\Calibration\CalibrationMetricsTests.cpp

- Baseline source: [Exchanges\BinanceSimulator\Calibration\CalibrationMetricsTests.cpp](d:/QTrading.Cpp-baseline-7110533/QTrading.Infra/tests/Exchanges\BinanceSimulator\Calibration\CalibrationMetricsTests.cpp)
- Test data form: inline scenario only
- Current migration status: pending triage

- [ ] CalibrationMetricsTest.EvaluateObjectiveComputesWeightedScore
- [ ] CalibrationMetricsTest.AcceptanceKpiGateUsesThresholds
- [ ] CalibrationMetricsTest.BuildWalkForwardWindowsCreatesRollingSplits
- [ ] CalibrationMetricsTest.EvaluateWalkForwardPipelineAggregatesWindowScores
- [ ] CalibrationMetricsTest.AcceptanceGateDecisionUsesMinPassRate

### Exchanges\BinanceSimulator\DataProvider\FundingRateDataTests.cpp

- Baseline source: [Exchanges\BinanceSimulator\DataProvider\FundingRateDataTests.cpp](d:/QTrading.Cpp-baseline-7110533/QTrading.Infra/tests/Exchanges\BinanceSimulator\DataProvider\FundingRateDataTests.cpp)
- Test data form: mixed inline + file/serialized fixtures (inspect file before migration)
- Current migration status: pending triage

- [ ] FundingRateDataTests.LoadCsvAndCheckCount
- [ ] FundingRateDataTests.GetLatestFunding
- [ ] FundingRateDataTests.GetFundingOutOfRange
- [ ] FundingRateDataTests.IteratorTraversal
- [ ] FundingRateDataTests.GetLatestThrowsWhenNoParsedRows

### Exchanges\BinanceSimulator\DataProvider\MarketDataTests.cpp

- Baseline source: [Exchanges\BinanceSimulator\DataProvider\MarketDataTests.cpp](d:/QTrading.Cpp-baseline-7110533/QTrading.Infra/tests/Exchanges\BinanceSimulator\DataProvider\MarketDataTests.cpp)
- Test data form: mixed inline + file/serialized fixtures (inspect file before migration)
- Current migration status: pending triage

- [ ] MarketDataTests.LoadCsvAndCheckCount
- [ ] MarketDataTests.GetLatestKline
- [ ] MarketDataTests.GetKlineOutOfRange
- [ ] MarketDataTests.GetSymbolAndFirstKline
- [ ] MarketDataTests.IteratorTraversal
- [ ] MarketDataTests.ManualIteratorUsage
- [ ] MarketDataTests.ConstIteratorTraversal
- [ ] MarketDataTests.LoadCompactSixColumnCsv
- [ ] MarketDataTests.LowerUpperBoundByTimestamp
- [ ] MarketDataTests.GetLatestKlineThrowsWhenNoParsedRows

### InfraLogFeatherRoundTripTests.cpp

- Baseline source: [InfraLogFeatherRoundTripTests.cpp](d:/QTrading.Cpp-baseline-7110533/QTrading.Infra/tests/InfraLogFeatherRoundTripTests.cpp)
- Test data form: mixed inline + file/serialized fixtures (inspect file before migration)
- Current migration status: pending triage

- [ ] InfraLogFeatherRoundTripFixture.FeatherRoundTripFixtureWritesAndReadsArrowTables
- [ ] InfraLogFeatherRoundTripFixture.AccountArrowSchemaMatchesLegacyFieldsExactly
- [ ] InfraLogFeatherRoundTripFixture.OrderEventArrowSchemaMatchesLegacyFieldsExactly
- [ ] InfraLogFeatherRoundTripFixture.PositionEventArrowSchemaMatchesLegacyFieldsExactly
- [ ] InfraLogFeatherRoundTripFixture.FundingEventArrowSchemaMatchesLegacyFieldsExactly
- [ ] InfraLogFeatherRoundTripFixture.MarketEventArrowSchemaMatchesLegacyFieldsExactly
- [ ] InfraLogFeatherRoundTripFixture.ArrowRowCountsMatchInMemorySinkRowsAfterMultiStepReplay
- [ ] InfraLogFeatherRoundTripFixture.ArrowFieldValuesMatchInMemoryPayloadDecodeResults

### InfraLogTests.cpp

- Baseline source: [InfraLogTests.cpp](d:/QTrading.Cpp-baseline-7110533/QTrading.Infra/tests/InfraLogTests.cpp)
- Test data form: mixed inline + file/serialized fixtures (inspect file before migration)
- Current migration status: pending triage

- [ ] InfraLogTestFixture.SinkLoggerCapturesRows
- [ ] InfraLogTestFixture.AccountSnapshotRowIsCapturedAfterLoggerStart
- [ ] InfraLogTestFixture.PositionSnapshotRowIsCapturedAfterLoggerStart
- [ ] InfraLogTestFixture.OrderSnapshotRowIsCapturedAfterLoggerStart
- [ ] InfraLogTestFixture.MultiRowArrivalOrderRemainsStableWithinSingleStep
- [ ] InfraLogTestFixture.StopFlushesBufferedRowsThatWereNotFlushedYet
- [ ] InfraLogTestFixture.DifferentModulesKeepTheirOwnModuleIdsWithoutCrossPollution
- [ ] InfraLogTestFixture.LogBatchAtPreservesExplicitTimestampInsteadOfGlobalTimestamp
- [ ] InfraLogTestFixture.CriticalOnlyRowsKeepSameOrderWhenDebugChannelIsEnabled
- [ ] InfraLogTestFixture.InMemorySinkInjectionRowsRemainReadableAfterLoggerStop
- [ ] InfraLogTestFixture.ModuleIdResolverResolvesModuleNameFromModuleId
- [ ] InfraLogTestFixture.RegisterDefaultModulesRegistersAllExistingModuleNames
- [ ] InfraLogTestFixture.RegisterDefaultModulesKeepsStableModuleIdOrder
- [ ] InfraLogTestFixture.RegisterDefaultModulesKeepsExpectedSchemaAndSerializerForModuleNames
- [ ] InfraLogTestFixture.RunMetadataAppearsFirstAfterLoggerInitialization
- [ ] InfraLogTestFixture.StepRowsUseMarketTimestampAsTsExchangeWithinSingleStep
- [ ] InfraLogTestFixture.EventSeqIsMonotonicWithinSingleStepAcrossEventModules
- [ ] InfraLogTestFixture.StepSeqIncrementsByExactlyOnePerStep
- [ ] InfraLogTestFixture.ReplayWindowFirstStepKeepsCorrectTsExchange
- [ ] InfraLogTestFixture.FundingOnlyStepProducesCorrectTsExchange
- [ ] InfraLogTestFixture.AsyncOrderAckAndOrderEventKeepSubmittedDueResolvedStepRelationship
- [ ] InfraLogTestFixture.FirstFillProducesAccountSnapshotAlignedWithFillStatusSnapshot
- [ ] InfraLogTestFixture.DualLedgerAccountSnapshotContainsStableSpotAndPerpFields
- [ ] InfraLogTestFixture.PositionSnapshotCarriesInstrumentTypeDirectionQuantityAndEntryPrice
- [ ] InfraLogTestFixture.OrderSnapshotCarriesInstrumentTypeReduceOnlyClosePositionAndPositionSide
- [ ] InfraLogTestFixture.UnchangedPositionAndOrderDoNotEmitExtraSnapshotRows
- [ ] InfraLogTestFixture.PositionChangeWithoutOrderChangeEmitsOnlyPositionSnapshot
- [ ] InfraLogTestFixture.OrderChangeWithoutPositionChangeEmitsOnlyOrderSnapshot
- [ ] InfraLogTestFixture.RowPayloadCastReturnsNullptrForInvalidOrEmptyRow
- [ ] InfraLogTestFixture.DrainAndSortRowsByArrivalDistinguishesArrivalFromBusinessSort
- [ ] InfraLogTestFixture.FilterRowsByModuleKeepsOnlyRequestedModuleInArrivalOrder
- [ ] InfraLogTestFixture.AssertSingleStepEnvelopeValidatesMarketEventRowsFromSingleStep
- [ ] InfraLogTestFixture.LegacyLogContractSnapshotBuildsDeterministicEqualityComparableRows
- [ ] DeterministicReplayFixture.InfraLogSameReplayTwiceProducesIdenticalLegacyLogContractSnapshots
- [ ] DeterministicReplayFixture.SingleSymbolGoldenReplayProducesExpectedLegacyLogContractSnapshots
- [ ] DeterministicReplayFixture.DualSymbolHolesGoldenReplayProducesExpectedLegacyLogContractSnapshots
- [ ] DeterministicReplayFixture.FundingInterpolationGoldenReplayProducesExpectedLegacyLogContractSnapshots
- [ ] DeterministicReplayFixture.SpotPerpMixedBookGoldenReplayProducesExpectedLegacyLogContractSnapshots
- [ ] DeterministicReplayFixture.HedgeModeGoldenReplayProducesExpectedLegacyLogContractSnapshots
- [ ] DeterministicReplayFixture.LiquidationGoldenReplayProducesExpectedLegacyLogContractSnapshots
- [ ] DeterministicReplayFixture.AsyncLatencyAndRejectionGoldenReplayProducesExpectedLegacyLogContractSnapshots
- [ ] DeterministicReplayFixture.BasisStressOverlayBlockGoldenReplayProducesExpectedLegacyLogContractSnapshots
- [ ] InfraLogTestFixture.OrderEventAcceptedIsEmittedWhenNewOrderRemainsOpen
- [ ] InfraLogTestFixture.OrderEventFilledCarriesExecQtyExecPriceAndRemainingQty
- [ ] InfraLogTestFixture.OrderEventCanceledIsEmittedWhenOpenOrderIsCanceled
- [ ] InfraLogTestFixture.OrderEventLifecycleKeepsAcceptedFilledCanceledForPartialThenCancel
- [ ] InfraLogTestFixture.OrderEventFeeFieldsAreCorrectForSpotAndPerpFills
- [ ] InfraLogTestFixture.OrderEventCapturesSpotBaseFeeCommissionAndCashflowFields
- [ ] InfraLogTestFixture.OrderEventReflectsModeledExecutionResultsWhenExecutionModelsEnabled
- [ ] InfraLogTestFixture.OrderEventPreservesQuoteOrderQtyForQuoteBasedMarketBuy
- [ ] InfraLogTestFixture.RejectedAsyncOrderDoesNotEmitAcceptedOrFilledOrderEvents
- [ ] InfraLogTestFixture.PositionEventOpenedIsEmittedOnFirstOpen
- [ ] InfraLogTestFixture.PositionEventIncreasedIsEmittedWhenPositionAddsSize
- [ ] InfraLogTestFixture.PositionEventReducedIsEmittedWhenPositionShrinksWithoutClosing
- [ ] InfraLogTestFixture.PositionEventClosedIsEmittedOnFullClose
- [ ] InfraLogTestFixture.PositionEventDistinguishesLongAndShortSidesInHedgeMode
- [ ] InfraLogTestFixture.PositionEventOneWayReverseKeepsOpenedClosedOpenedLifecycle
- [ ] InfraLogTestFixture.AccountEventSpotOpenCarriesCorrectSpotCashAndInventoryDelta
- [ ] InfraLogTestFixture.AccountEventPerpOpenCarriesCorrectWalletMarginAndAvailableBalances
- [ ] InfraLogTestFixture.AccountEventFundingStepCarriesCorrectWalletDelta
- [ ] InfraLogTestFixture.AccountEventTransfersReflectSpotAndPerpBalancesAfterTransfer
- [ ] InfraLogTestFixture.AccountEventLiquidationStepReconcilesWalletBalanceChange
- [ ] InfraLogTestFixture.AccountEventAndOrderEventStayConsistentForSameFillFeeAndCashflow
- [ ] InfraLogTestFixture.MarketEventCarriesTradeMarkAndIndexPricesPerSymbol
- [ ] InfraLogTestFixture.MarketEventUsesAbsentFlagsForMissingSymbolDataInsteadOfDefaultValues
- [ ] InfraLogTestFixture.MarketEventInterpolatedMarkAndIndexUseInterpolatedSource
- [ ] InfraLogTestFixture.MarketEventRawMarkAndIndexUseRawSourceWhenPresent
- [ ] InfraLogTestFixture.MarketSnapshotUncertaintyBandAndBasisDiagnosticsMatchStatusSnapshot
- [ ] InfraLogTestFixture.FundingEventIsEmittedWhenFundingCsvHasApplicableRows
- [ ] InfraLogTestFixture.FundingEventExplicitMarkPriceCarriesCorrectRateMarkAndFundingValue
- [ ] InfraLogTestFixture.FundingEventInterpolatedMarkPriceRemainsCorrectAndSourceIsInterpolated
- [ ] InfraLogTestFixture.FundingEventTimingBeforeAndAfterMatchingProduceExpectedDifferences
- [ ] InfraLogTestFixture.FundingSkippedNoMarkStatisticsStayConsistentWithFundingEvents
- [ ] InfraLogTestFixture.AsyncAckLatencyPublishesPendingBeforeAcceptedOrRejected
- [ ] InfraLogTestFixture.PendingAsyncAckDoesNotEmitAcceptedOrderEventEarly
- [ ] InfraLogTestFixture.RejectedAsyncAckCarriesRejectAndBinanceErrorDetails
- [ ] InfraLogTestFixture.AcceptedAsyncAckAppearsBeforeSnapshotAndEventVisibility
- [ ] InfraLogTestFixture.MarketEventAndFundingEventKeepExistingArrivalOrderWithinSingleStep
- [ ] InfraLogTestFixture.OrderPositionAndAccountEventKeepExistingArrivalOrderWithinSingleStep
- [ ] InfraLogTestFixture.SnapshotLogAndEventLogKeepExistingArrivalOrderWithinSingleStep
- [ ] InfraLogTestFixture.LiquidationStepKeepsExistingEventArrivalOrder
- [ ] InfraLogTestFixture.FundingAndFillInSameStepKeepExistingEventArrivalOrder
- [ ] InfraLogTestFixture.PositionSnapshotReplayMatchesRuntimePositionsAtEachStep
- [ ] InfraLogTestFixture.OrderSnapshotReplayMatchesRuntimeOpenOrdersAtEachStep
- [ ] InfraLogTestFixture.AccountSnapshotLogMatchesFillStatusSnapshotPerChangedStep
- [ ] InfraLogTestFixture.AggregatedFillFeesMatchAccountWalletChange
- [ ] InfraLogTestFixture.AggregatedFundingEventsMatchAccountEventAndSnapshotWalletChange

## 遷移方式

每個 test case 完成遷移時，必須補這 4 個欄位：

- new owner module
- current test file path
- data source path or inline scenario confirmation
- status: direct restore / semantic rewrite / blocked

## 執行順序

建議順序：

1. DataProvider tests
2. Account tests
3. BinanceExchange tests
4. BinanceExchangeLog + InfraLogFeatherRoundTrip
5. InfraLog tests
6. Calibration decision
