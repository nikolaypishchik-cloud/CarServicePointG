# P2 architecture: tests are registered from a dedicated CMake module.

if (Python3_Interpreter_FOUND)
    add_test(NAME PackagingSafetyTests
        COMMAND ${CS_PYTHON_NO_BYTECODE_CMD} ${CMAKE_SOURCE_DIR}/tests/PackagingSafetyTests.py)
endif()

if (CS_INTERNAL_QT_TESTS_AVAILABLE)
    add_executable(SyncRecovery EXCLUDE_FROM_ALL scripts/SyncRecovery.cpp)
    set_target_properties(SyncRecovery PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tools")
    target_include_directories(SyncRecovery PRIVATE src)
    target_link_libraries(SyncRecovery PRIVATE CarServiceLib Qt6::Sql Qt6::Widgets)
    function(cs_add_internal_test test_name)
        add_executable(${test_name} ${ARGN})
        # Keep internal test executables out of the build-release root.
        # Deployment smoke scripts search *.exe recursively and pick the first
        # path when copying dist\CarService.exe; if tests live next to the
        # app, Backup*/Regression*/Smoke* can be selected and exit immediately.
        set_target_properties(${test_name} PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
            RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/tests"
            RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/tests"
            RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/tests"
            RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_BINARY_DIR}/tests"
        )
        target_include_directories(${test_name} PRIVATE src tests)
        target_link_libraries(${test_name} PRIVATE CarServiceLib Qt6::Test Qt6::Sql Qt6::Widgets Qt6::Gui Qt6::Core)
        set(cs_uses_qtest FALSE)
        foreach(cs_test_source IN LISTS ARGN)
            file(READ "${CMAKE_SOURCE_DIR}/${cs_test_source}" cs_test_text)
            if(cs_test_text MATCHES "QTEST_[A-Z_]*MAIN|QTest::qExec")
                set(cs_uses_qtest TRUE)
            endif()
        endforeach()
        if(test_name STREQUAL "PurchasePaymentTests")
            add_test(NAME ${test_name} COMMAND ${CS_PYTHON_NO_BYTECODE_CMD}
                "${CMAKE_SOURCE_DIR}/scripts/run_purchase_acceptance.py" $<TARGET_FILE:${test_name}>)
        elseif(cs_uses_qtest)
            add_test(NAME ${test_name} COMMAND ${CS_PYTHON_NO_BYTECODE_CMD}
                "${CMAKE_SOURCE_DIR}/scripts/run_qt_test.py" $<TARGET_FILE:${test_name}>)
        else()
            add_test(NAME ${test_name} COMMAND $<TARGET_FILE:${test_name}>)
        endif()
        set_tests_properties(${test_name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 300)
    endfunction()


    # Real process termination at seven archive commit boundaries, plus native restored-profile UI.
    add_executable(ArchiveCrashRecoveryTests
        tests/ArchiveCrashRecoveryTests.cpp
        src/infrastructure/db/sqlite/OrderArchiveShardSqlSupport.cpp
        src/infrastructure/db/sqlite/DbArchiveRoutingStageSupport.cpp
        resources/resources.qrc)
    set_target_properties(ArchiveCrashRecoveryTests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")
    target_include_directories(ArchiveCrashRecoveryTests PRIVATE src tests)
    target_compile_definitions(ArchiveCrashRecoveryTests PRIVATE ORDER_ARCHIVE_TEST)
    target_link_libraries(ArchiveCrashRecoveryTests PRIVATE CarServiceLib Qt6::Test Qt6::Sql Qt6::Widgets Qt6::Gui Qt6::Core)
    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/change-gate-test-data/archive-crash")
    add_test(NAME ArchiveCrashRecoveryTests COMMAND ${CS_PYTHON_NO_BYTECODE_CMD}
        "${CMAKE_SOURCE_DIR}/scripts/run_qt_test.py" $<TARGET_FILE:ArchiveCrashRecoveryTests>
        prepared copyCommitted verified routeCommitted routed beforePurgeCommit afterPurgeCommit
        restoredProfileWithProtectedSettings)
    set_tests_properties(ArchiveCrashRecoveryTests PROPERTIES TIMEOUT 300
        ENVIRONMENT "QT_QPA_PLATFORM=windows;CS_TEST_DATA_ROOT=${CMAKE_BINARY_DIR}/change-gate-test-data/archive-crash;CS_TEST_PYTHON=${Python3_EXECUTABLE};PYTHONUTF8=1")

    cs_add_internal_test(BayLoadReportTests tests/BayLoadReportTests.cpp)
    cs_add_internal_test(BaySnapshotTests tests/BaySnapshotTests.cpp)
    cs_add_internal_test(ElitInvoiceStorageTests tests/ElitInvoiceStorageTests.cpp)
    cs_add_internal_test(OrderDetailIdentityTests tests/OrderDetailIdentityTests.cpp)
    cs_add_internal_test(MigrationArchiveWorkspaceTests tests/MigrationArchiveWorkspaceTests.cpp)
    cs_add_internal_test(HistoricalRecordStoreTests tests/HistoricalRecordStoreTests.cpp)
    cs_add_internal_test(ReportsOrdersTests tests/ReportsOrdersTests.cpp)
    cs_add_internal_test(SettingsLayoutTests tests/SettingsLayoutTests.cpp resources/resources.qrc)
    cs_add_internal_test(SplashScreenTests tests/SplashScreenTests.cpp resources/resources.qrc)
    cs_add_internal_test(DataAttachmentSafetyTests tests/DataAttachmentSafetyTests.cpp)
    cs_add_internal_test(ShutdownAccessSafetyTests tests/ShutdownAccessSafetyTests.cpp)
    cs_add_internal_test(HistoryPartsAutofillTests tests/HistoryPartsAutofillTests.cpp)
    cs_add_internal_test(AdditionalExpenseTests tests/AdditionalExpenseTests.cpp)
    cs_add_internal_test(MobileSyncTests tests/MobileSyncTests.cpp)
    cs_add_internal_test(DialogLocalizationTests tests/DialogLocalizationTests.cpp resources/resources.qrc)
    cs_add_internal_test(InvoicePdfMappingTests tests/InvoicePdfMappingTests.cpp)
    cs_add_internal_test(PlanningOrdersDisplayTests tests/PlanningOrdersDisplayTests.cpp)
    cs_add_internal_test(PurchasePaymentTests tests/PurchasePaymentTests.cpp resources/resources.qrc)
    cs_add_internal_test(OrderClosureDialogTests tests/OrderClosureDialogTests.cpp src/core/services/orders/OrderClosureJournal.cpp)
    cs_add_internal_test(OptionalDatePopupTests tests/OptionalDatePopupTests.cpp)
    cs_add_internal_test(ManualInputCorrectionTests tests/ManualInputCorrectionTests.cpp)
    cs_add_internal_test(ClearOnEntryTests tests/ClearOnEntryTests.cpp)
    cs_add_internal_test(LocalWorkshopBackendTests tests/LocalWorkshopBackendTests.cpp src/core/services/orders/LocalWorkshopBackend.cpp)
    target_compile_definitions(LocalWorkshopBackendTests PRIVATE LOCAL_BACKEND_TEST)
    target_compile_definitions(OrderClosureDialogTests PRIVATE ORDER_CLOSURE_TEST)
    add_test(NAME PurchaseAccessRegressionTests COMMAND ${CS_PYTHON_NO_BYTECODE_CMD}
        "${CMAKE_SOURCE_DIR}/scripts/run_qt_test.py" $<TARGET_FILE:PurchasePaymentTests>
        masterToolbarHidesPdfAndImport masterScopeAndDebtTotals sessionChangeClearsOrderModelsImmediately
        hiddenPartsRefreshDoesNotDisablePainting masterPartsKeepDeadlineHighlighting
        planningHidesOtherMastersAndPreservesOccupiedSlots legacyStatesAndPartialSettlement
        paymentRollsBackOnAuditFailureAndMasterDenied zeroAndDeferredPayments paymentHistoryInStatistics)
    set_tests_properties(PurchaseAccessRegressionTests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    cs_add_internal_test(OrderIdentityResolverTests tests/OrderIdentityResolverTests.cpp)
    cs_add_internal_test(OrderDetailsRepositoryTests tests/OrderDetailsRepositoryTests.cpp)
    cs_add_internal_test(LegacyCanonicalAncillaryImportSupportTests tests/LegacyCanonicalAncillaryImportSupportTests.cpp)

    cs_add_internal_test(OrderWorkspaceServiceTests tests/OrderWorkspaceServiceTests.cpp)
    cs_add_internal_test(OrderIdentityServiceTests tests/OrderIdentityServiceTests.cpp)
    cs_add_internal_test(OrderMutationFacadeTests tests/OrderMutationFacadeTests.cpp)
    cs_add_internal_test(PartMutationServiceTests tests/PartMutationServiceTests.cpp)
    cs_add_internal_test(CurrentOrdersQueryServiceTests tests/CurrentOrdersQueryServiceTests.cpp)
    cs_add_internal_test(OrderSearchPerformanceTests tests/OrderSearchPerformanceTests.cpp)
    cs_add_internal_test(DateFormatsTests tests/DateFormatsTests.cpp)
    cs_add_internal_test(OrderTableUiMapperTests tests/OrderTableUiMapperTests.cpp)
    add_executable(NativeDatabaseMaintenance scripts/NativeDatabaseMaintenance.cpp)
    target_include_directories(NativeDatabaseMaintenance PRIVATE src)
    target_link_libraries(NativeDatabaseMaintenance PRIVATE CarServiceLib)
    cs_add_internal_test(PartsQueryServiceTests tests/PartsQueryServiceTests.cpp)
    cs_add_internal_test(PartOverduePolicyTests tests/PartOverduePolicyTests.cpp)
    cs_add_internal_test(BackupManagerCriticalFlowTests tests/BackupManagerCriticalFlowTests.cpp)
    cs_add_internal_test(RecoveryConflictResolutionTests tests/RecoveryConflictResolutionTests.cpp)
    cs_add_internal_test(BackupRecoveryAcceptanceTests tests/BackupRecoveryAcceptanceTests.cpp)

    cs_add_internal_test(SmokeMainWindowTests tests/smoke/SmokeMainWindowTests.cpp)
    cs_add_internal_test(SmokeCurrentOrdersPageTests tests/smoke/SmokeCurrentOrdersPageTests.cpp)
    cs_add_internal_test(SmokeOrderDialogTests tests/smoke/SmokeOrderDialogTests.cpp)
    cs_add_internal_test(SmokeWorkPartsDialogTests tests/smoke/SmokeWorkPartsDialogTests.cpp)
    cs_add_internal_test(SmokeImportPathTests tests/smoke/SmokeImportPathTests.cpp)
    cs_add_internal_test(SmokeBackupRecoveryPathTests tests/smoke/SmokeBackupRecoveryPathTests.cpp)
    cs_add_internal_test(SmokeSettingsPageTests tests/smoke/SmokeSettingsPageTests.cpp)
    cs_add_internal_test(SmokePlanningPageTests tests/smoke/SmokePlanningPageTests.cpp)

    cs_add_internal_test(RegressionOrderPartsSaveTests tests/regression/RegressionOrderPartsSaveTests.cpp)
    cs_add_internal_test(RegressionImportInvoiceTests tests/regression/RegressionImportInvoiceTests.cpp)
    cs_add_internal_test(RegressionCloseReopenLifecycleTests tests/regression/RegressionCloseReopenLifecycleTests.cpp)

    cs_add_internal_test(RegressionPartDatesTests tests/regression/RegressionPartDatesTests.cpp)
    cs_add_internal_test(RegressionMarkupRecalculationTests tests/regression/RegressionMarkupRecalculationTests.cpp)
    cs_add_internal_test(RegressionRecoveryAfterCrashTests tests/regression/RegressionRecoveryAfterCrashTests.cpp)
    cs_add_internal_test(RegressionTabSwitchAfterAsyncRefreshTests tests/regression/RegressionTabSwitchAfterAsyncRefreshTests.cpp)
    cs_add_internal_test(RegressionWorkPartsP0ImportedPartsTests tests/regression/RegressionWorkPartsP0ImportedPartsTests.cpp)
    cs_add_internal_test(RegressionP0DbContextTransactionTests tests/regression/RegressionP0DbContextTransactionTests.cpp)
    cs_add_internal_test(RegressionP0SQLiteIntegrationTests tests/regression/RegressionP0SQLiteIntegrationTests.cpp)
    cs_add_internal_test(RegressionP0ImportProcessBoundaryTests tests/regression/RegressionP0ImportProcessBoundaryTests.cpp)
    cs_add_internal_test(RegressionP0ImportJobTests tests/regression/RegressionP0ImportJobTests.cpp)
    cs_add_internal_test(RegressionP0ImportNoUiWaitTests tests/regression/RegressionP0ImportNoUiWaitTests.cpp)
    cs_add_internal_test(RegressionP0ImportIncrementalProcessIoTests tests/regression/RegressionP0ImportIncrementalProcessIoTests.cpp)
    cs_add_internal_test(RegressionP0VendorParserSplitTests tests/regression/RegressionP0VendorParserSplitTests.cpp)
    cs_add_internal_test(RegressionP0ImportViewModelBoundaryTests tests/regression/RegressionP0ImportViewModelBoundaryTests.cpp)
    cs_add_internal_test(RegressionP0InvoiceImportScenarioTests tests/regression/RegressionP0InvoiceImportScenarioTests.cpp)
    cs_add_internal_test(RegressionP0ImportModuleExtractionTests tests/regression/RegressionP0ImportModuleExtractionTests.cpp)
    cs_add_internal_test(RegressionP1InlineEditorControllerSplitTests tests/regression/RegressionP1InlineEditorControllerSplitTests.cpp)
    cs_add_internal_test(RegressionP1InlineEditorPersistentIndexTests tests/regression/RegressionP1InlineEditorPersistentIndexTests.cpp)
    cs_add_internal_test(RegressionP1InlineEditorStateMachineTests tests/regression/RegressionP1InlineEditorStateMachineTests.cpp)
    cs_add_internal_test(RegressionP1InlineEditorRevisionGuardTests tests/regression/RegressionP1InlineEditorRevisionGuardTests.cpp)
    cs_add_internal_test(RegressionP1InlineEditorCanonicalCommitTests tests/regression/RegressionP1InlineEditorCanonicalCommitTests.cpp)
    cs_add_internal_test(RegressionP1InlineEditorNavigationCoverageTests tests/regression/RegressionP1InlineEditorNavigationCoverageTests.cpp)
    cs_add_internal_test(RegressionP1AsyncRefreshOwnershipTests tests/regression/RegressionP1AsyncRefreshOwnershipTests.cpp)
    cs_add_internal_test(RegressionP1AsyncOwnerShutdownTests tests/regression/RegressionP1AsyncOwnerShutdownTests.cpp)
    set_tests_properties(RegressionP1AsyncOwnerShutdownTests PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    cs_add_internal_test(RegressionP1AsyncCancellationLifecycleTests tests/regression/RegressionP1AsyncCancellationLifecycleTests.cpp)
    cs_add_internal_test(RegressionCurrentHistoryAsyncRefreshTests tests/regression/RegressionCurrentHistoryAsyncRefreshTests.cpp)
    cs_add_internal_test(RegressionRuntimeJobDispatcherShutdownTests tests/regression/RegressionRuntimeJobDispatcherShutdownTests.cpp)
    cs_add_internal_test(RegressionRuntimeJobTimeoutGuardsTests tests/regression/RegressionRuntimeJobTimeoutGuardsTests.cpp)
    cs_add_internal_test(RegressionPerformanceTzFinalGapGuardsTests tests/regression/RegressionPerformanceTzFinalGapGuardsTests.cpp)
    cs_add_internal_test(RegressionPerformanceTzFinal100ClosureTests tests/regression/RegressionPerformanceTzFinal100ClosureTests.cpp)
    cs_add_internal_test(RegressionPerformanceTzPartsArchiveModelClosureTests tests/regression/RegressionPerformanceTzPartsArchiveModelClosureTests.cpp)
    cs_add_internal_test(RegressionWorkPartsPerformanceContinuationTests tests/regression/RegressionWorkPartsPerformanceContinuationTests.cpp)
    cs_add_internal_test(RegressionPerformanceTzCacheRevisionTests tests/regression/RegressionPerformanceTzCacheRevisionTests.cpp)
    cs_add_internal_test(RegressionPerformanceTzPass3CacheAndHeightTests tests/regression/RegressionPerformanceTzPass3CacheAndHeightTests.cpp)
    cs_add_internal_test(RegressionPerformanceTzPass4WorkPartsAsyncReadTests tests/regression/RegressionPerformanceTzPass4WorkPartsAsyncReadTests.cpp)
    cs_add_internal_test(RegressionPerformanceTzPass5ReportsDialogModelTests tests/regression/RegressionPerformanceTzPass5ReportsDialogModelTests.cpp)
    cs_add_internal_test(RegressionPerformanceTzPass6OrderSnapshotCacheTests tests/regression/RegressionPerformanceTzPass6OrderSnapshotCacheTests.cpp)
    cs_add_internal_test(RegressionPerformanceTzPass7FreezeLogDbClosureTests tests/regression/RegressionPerformanceTzPass7FreezeLogDbClosureTests.cpp)
    cs_add_internal_test(RegressionPerformanceTzPass8ImportRowHeightReportsTests tests/regression/RegressionPerformanceTzPass8ImportRowHeightReportsTests.cpp)
    cs_add_internal_test(RegressionPerformanceTzPass9SnapshotCacheGatesTests tests/regression/RegressionPerformanceTzPass9SnapshotCacheGatesTests.cpp)
    cs_add_internal_test(RegressionPerformanceTzPass10FinalClosureTests tests/regression/RegressionPerformanceTzPass10FinalClosureTests.cpp)
    cs_add_internal_test(RegressionPerformanceTzPass11ImportPreviewSnapshotTests tests/regression/RegressionPerformanceTzPass11ImportPreviewSnapshotTests.cpp)
    cs_add_internal_test(RegressionPerformanceTzPass12WorkPartsRowIndexTests tests/regression/RegressionPerformanceTzPass12WorkPartsRowIndexTests.cpp)
    cs_add_internal_test(RegressionPerformanceTzPass13ProductionLogClosureTests tests/regression/RegressionPerformanceTzPass13ProductionLogClosureTests.cpp)
    cs_add_internal_test(RegressionUnikTradeInvoiceImportTests tests/regression/RegressionUnikTradeInvoiceImportTests.cpp)
    cs_add_internal_test(RegressionHybridInvoiceColumnDetectorTests tests/regression/RegressionHybridInvoiceColumnDetectorTests.cpp)
    cs_add_internal_test(RegressionHybridInvoiceImportManualMappingTests tests/regression/RegressionHybridInvoiceImportManualMappingTests.cpp)
    cs_add_internal_test(RegressionP1PartsPageLargeTableAdapterTests tests/regression/RegressionP1PartsPageLargeTableAdapterTests.cpp)
    cs_add_internal_test(RegressionP1PartsPageDelegateProxyTests tests/regression/RegressionP1PartsPageDelegateProxyTests.cpp)
    cs_add_internal_test(RegressionP1PartsPageIncrementalRefreshTests tests/regression/RegressionP1PartsPageIncrementalRefreshTests.cpp)
    cs_add_internal_test(RegressionP2ArchitectureIncModuleTests tests/regression/RegressionP2ArchitectureIncModuleTests.cpp)
    cs_add_internal_test(RegressionP2ArchitectureModuleBoundaryTests tests/regression/RegressionP2ArchitectureModuleBoundaryTests.cpp)
    cs_add_internal_test(RegressionP2ArchitectureLayerRulesTests tests/regression/RegressionP2ArchitectureLayerRulesTests.cpp)
    cs_add_internal_test(RegressionP2CMakeModuleSplitTests tests/regression/RegressionP2CMakeModuleSplitTests.cpp)
    cs_add_internal_test(RegressionP2ArchitectureGateCoverageTests tests/regression/RegressionP2ArchitectureGateCoverageTests.cpp)
    cs_add_internal_test(RegressionP2LegacyFallbackInventoryTests tests/regression/RegressionP2LegacyFallbackInventoryTests.cpp)
    cs_add_internal_test(RegressionP2LegacyTailCleanupGateTests tests/regression/RegressionP2LegacyTailCleanupGateTests.cpp)
    cs_add_internal_test(RegressionP2LegacyTailCiGateTests tests/regression/RegressionP2LegacyTailCiGateTests.cpp)
    cs_add_internal_test(RegressionP2LoggingCrashDiagnosticsTests tests/regression/RegressionP2LoggingCrashDiagnosticsTests.cpp)
    cs_add_internal_test(P2AcceptanceUnitTests tests/unit/P2AcceptanceUnitTests.cpp)
    cs_add_internal_test(P2AcceptanceIntegrationTests tests/integration/P2AcceptanceIntegrationTests.cpp)
    cs_add_internal_test(P2AcceptanceSmokeTests tests/smoke/P2AcceptanceSmokeTests.cpp)
    cs_add_internal_test(P2AcceptanceCriteriaTests tests/acceptance/P2AcceptanceCriteriaTests.cpp)
    cs_add_internal_test(UpdateReleaseContractTests tests/update/UpdateReleaseContractTests.cpp)
    cs_add_internal_test(UpdateOnceGuaranteeTests tests/update/UpdateOnceGuaranteeTests.cpp)
    cs_add_internal_test(InvoicePdfRecognitionTests tests/InvoicePdfRecognitionTests.cpp)
    cs_add_internal_test(PartImportTextSupportTests tests/PartImportTextSupportTests.cpp)
    cs_add_internal_test(VehiclePreviewProviderTests tests/VehiclePreviewProviderTests.cpp)
    cs_add_internal_test(OilServiceClassifierTests tests/OilServiceClassifierTests.cpp)
elseif(BUILD_TESTING)
    message(STATUS "BUILD_TESTING is ON, but Qt6::Test is unavailable; internal Qt-based tests will be skipped.")
endif()

if (BUILD_TESTING)
    add_executable(RegressionSingleInstanceCoordinatorTests tests/regression/RegressionSingleInstanceCoordinatorTests.cpp)
    set_target_properties(RegressionSingleInstanceCoordinatorTests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/tests"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/tests"
        RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/tests"
        RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_BINARY_DIR}/tests"
    )
    target_include_directories(RegressionSingleInstanceCoordinatorTests PRIVATE src tests)
    target_link_libraries(RegressionSingleInstanceCoordinatorTests PRIVATE CarServiceLib Qt6::Core Qt6::Network)
    add_test(NAME RegressionSingleInstanceCoordinatorTests COMMAND $<TARGET_FILE:RegressionSingleInstanceCoordinatorTests>)

    add_executable(RegressionWorkshopInventoryServiceTests tests/regression/RegressionWorkshopInventoryServiceTests.cpp)
    set_target_properties(RegressionWorkshopInventoryServiceTests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/tests"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/tests"
        RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/tests"
        RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_BINARY_DIR}/tests"
    )
    target_include_directories(RegressionWorkshopInventoryServiceTests PRIVATE src tests)
    target_link_libraries(RegressionWorkshopInventoryServiceTests PRIVATE CarServiceLib Qt6::Core Qt6::Sql)
    add_test(NAME RegressionWorkshopInventoryServiceTests COMMAND $<TARGET_FILE:RegressionWorkshopInventoryServiceTests>)

    add_executable(RegressionActivityAuditServiceTests tests/regression/RegressionActivityAuditServiceTests.cpp)
    set_target_properties(RegressionActivityAuditServiceTests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/tests"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/tests"
        RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/tests"
        RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_BINARY_DIR}/tests"
    )
    target_include_directories(RegressionActivityAuditServiceTests PRIVATE src tests)
    target_link_libraries(RegressionActivityAuditServiceTests PRIVATE CarServiceLib Qt6::Core Qt6::Sql)
    add_test(NAME RegressionActivityAuditServiceTests COMMAND $<TARGET_FILE:RegressionActivityAuditServiceTests>)
endif()
