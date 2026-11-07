cmake_minimum_required(VERSION 3.21)

project(CarService VERSION 2.1.2 LANGUAGES CXX)

set(CS_RELEASE_VERSION "" CACHE STRING "Unique release version (major.minor.patch.build)")
if(CS_RELEASE_VERSION)
    if(NOT CS_RELEASE_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+\\.[0-9]+$")
        message(FATAL_ERROR "CS_RELEASE_VERSION must contain four numeric components")
    endif()
    string(REPLACE "." ";" _release_parts "${CS_RELEASE_VERSION}")
    foreach(_part IN LISTS _release_parts)
        if(_part GREATER 65535)
            message(FATAL_ERROR "Windows version components must not exceed 65535")
        endif()
    endforeach()
    set(PROJECT_VERSION "${CS_RELEASE_VERSION}")
    list(GET _release_parts 0 PROJECT_VERSION_MAJOR)
    list(GET _release_parts 1 PROJECT_VERSION_MINOR)
    list(GET _release_parts 2 PROJECT_VERSION_PATCH)
    list(GET _release_parts 3 PROJECT_VERSION_TWEAK)
endif()
set(CS_UPDATE_SIGNER_THUMBPRINT "" CACHE STRING "Trusted release certificate SHA1 thumbprint")
if(CS_UPDATE_SIGNER_THUMBPRINT AND NOT CS_UPDATE_SIGNER_THUMBPRINT MATCHES "^[0-9A-Fa-f]+$")
    message(FATAL_ERROR "Invalid signing certificate thumbprint")
endif()

set(CS_GITHUB_OWNER "nikolaypishchik-cloud")
set(CS_GITHUB_REPO "Release")
set(CS_VERSION_PATCH "${PROJECT_VERSION_PATCH}")
if (CS_VERSION_PATCH STREQUAL "")
    set(CS_VERSION_PATCH "0")
endif()
set(CS_VERSION_COMMA "${PROJECT_VERSION_MAJOR},${PROJECT_VERSION_MINOR},${CS_VERSION_PATCH},0")
if(CS_RELEASE_VERSION)
    string(REPLACE "." "," CS_VERSION_COMMA "${CS_RELEASE_VERSION}")
endif()
configure_file(${CMAKE_SOURCE_DIR}/src/app/AppVersion.h.in ${CMAKE_BINARY_DIR}/generated/AppVersion.h @ONLY)

set(CS_APP_ICON_RC "")
if (WIN32)
    configure_file(
        ${CMAKE_SOURCE_DIR}/resources/windows/CarServiceAppIcon.rc.in
        ${CMAKE_BINARY_DIR}/generated/CarServiceAppIcon.rc
        @ONLY)
    set(CS_APP_ICON_RC "${CMAKE_BINARY_DIR}/generated/CarServiceAppIcon.rc")
endif()

# Keep object-file paths inside the Windows MAX_PATH budget without
# forcing CMake/Ninja into an unrealistically small 128-character limit.
# The previous value generated repeated object-path warnings for long
# CarService support filenames during MSVC/Ninja configure.
set(CMAKE_OBJECT_PATH_MAX 240)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Guard against x86/x64 mismatch early with a clear error.
if (WIN32)
    if (DEFINED CMAKE_PREFIX_PATH AND CMAKE_PREFIX_PATH MATCHES "msvc2022_64|msvc2019_64|msvc64")
        if (CMAKE_SIZEOF_VOID_P EQUAL 4)
            message(FATAL_ERROR
                "Detected 32-bit generator/toolchain with a 64-bit Qt path in CMAKE_PREFIX_PATH. "
                "Open the x64 VS Developer PowerShell or configure with '-A x64' / a 64-bit Ninja toolchain.")
        endif()
    endif()
endif()

find_package(Qt6 REQUIRED COMPONENTS Core CorePrivate Gui Widgets Sql PrintSupport Network)
find_package(Qt6 QUIET COMPONENTS Multimedia Bluetooth Test Pdf)
find_package(SQLite3 QUIET)

# Extra post-find guard when Qt actually resolves.
if (WIN32 AND TARGET Qt6::Core)
    get_target_property(_qt_core_imported_location Qt6::Core IMPORTED_LOCATION_RELEASE)
    if (NOT _qt_core_imported_location)
        get_target_property(_qt_core_imported_location Qt6::Core IMPORTED_LOCATION)
    endif()
    if (_qt_core_imported_location MATCHES "msvc2022_64|msvc2019_64|msvc64")
        if (CMAKE_SIZEOF_VOID_P EQUAL 4)
            message(FATAL_ERROR
                "Qt6::Core resolves to a 64-bit Qt build, but the current compiler/generator is 32-bit. "
                "Use x64 build tools for this project.")
        endif()
    endif()
endif()

qt_standard_project_setup()
set(CMAKE_AUTORCC ON)
option(BUILD_TESTING "Build internal tests in this source tree" OFF)
include(CTest)

if (BUILD_TESTING AND NOT TARGET Qt6::Test)
    find_package(Qt6 QUIET COMPONENTS Test)
endif()

set(CS_INTERNAL_QT_TESTS_AVAILABLE FALSE)
if (BUILD_TESTING AND TARGET Qt6::Test)
    set(CS_INTERNAL_QT_TESTS_AVAILABLE TRUE)
endif()

# P2 architecture: CMake source ownership is split by layer.
# Source lists live in cmake/modules/ so the root build file stays orchestration-only.
set(CS_MAIN_SOURCE src/main.cpp)
include(cmake/modules/CarServiceSourcesCore.cmake)
include(cmake/modules/CarServiceSourcesInfrastructure.cmake)
include(cmake/modules/CarServiceSourcesIntegrations.cmake)
include(cmake/modules/CarServiceSourcesUi.cmake)

set(SRC_FILES
    ${CS_MAIN_SOURCE}
    ${CS_CORE_SOURCES}
    ${CS_INFRASTRUCTURE_SOURCES}
    ${CS_INTEGRATIONS_SOURCES}
    ${CS_UI_SOURCES}
)

set(HDR_FILES
    ${CS_CORE_HEADERS}
    ${CS_INFRASTRUCTURE_HEADERS}
    ${CS_INTEGRATIONS_HEADERS}
    ${CS_UI_HEADERS}
)


find_package(Python3 COMPONENTS Interpreter QUIET)
set(CS_ARCHITECTURE_GATE_SCRIPT ${CMAKE_SOURCE_DIR}/scripts/check_architecture.py)
set(CS_RELEASE_ARCHIVE_SCRIPT ${CMAKE_SOURCE_DIR}/scripts/create_release_archive.py)
set(CS_PERF_GUARD_SCRIPT ${CMAKE_SOURCE_DIR}/scripts/check_perf_guards.py)
set(CS_SUPPORT_INTEGRITY_SCRIPT ${CMAKE_SOURCE_DIR}/scripts/check_required_sources.py)
set(CS_RUNTIME_CONFLICT_GATE_SCRIPT ${CMAKE_SOURCE_DIR}/scripts/check_runtime_conflicts.py)
set(CS_DOMAIN_INTEGRITY_SCRIPT ${CMAKE_SOURCE_DIR}/scripts/check_domain_integrity.py)
set(CS_TEST_INVENTORY_SCRIPT ${CMAKE_SOURCE_DIR}/scripts/check_test_inventory.py)
set(CS_LEGACY_TAIL_INVENTORY_SCRIPT ${CMAKE_SOURCE_DIR}/scripts/check_legacy_tail_inventory.py)
set(CS_LEGACY_TAIL_CLEANUP_SCRIPT ${CMAKE_SOURCE_DIR}/scripts/check_legacy_tail_cleanup.py)
set(CS_LEGACY_MARKER_CI_SCRIPT ${CMAKE_SOURCE_DIR}/scripts/check_legacy_marker_ci_gate.py)
set(CS_LOGGING_DIAGNOSTICS_SCRIPT ${CMAKE_SOURCE_DIR}/scripts/check_logging_diagnostics.py)
set(CS_P2_ACCEPTANCE_TESTS_SCRIPT ${CMAKE_SOURCE_DIR}/scripts/check_p2_acceptance_tests.py)
set(CS_PROJECT_LAYOUT_SCRIPT ${CMAKE_SOURCE_DIR}/scripts/check_project_layout.py)
set(CS_NAMING_POLICY_SCRIPT ${CMAKE_SOURCE_DIR}/scripts/check_naming_policy.py)
set(CS_BUILD_SANITY_SCRIPT ${CMAKE_SOURCE_DIR}/scripts/check_build_sanity.py)
set(CS_REGRESSION_CHECKLIST_SCRIPT ${CMAKE_SOURCE_DIR}/scripts/check_regression_checklist.py)
set(CS_PART_PRICING_TZ_SCRIPT ${CMAKE_SOURCE_DIR}/scripts/check_part_pricing_tz.py)
set(CS_UPDATE_RELEASE_CONTRACT_SCRIPT ${CMAKE_SOURCE_DIR}/scripts/check_update_release_contract.py)
set(CS_P0_SAFETY_GATE_SCRIPT ${CMAKE_SOURCE_DIR}/scripts/check_p0_safety_gate.py)
set(CS_MONOLITH_SPLIT_GATE_SCRIPT ${CMAKE_SOURCE_DIR}/scripts/check_monolith_split_gate.py)
set(CS_SQL_SAFETY_GATE_SCRIPT ${CMAKE_SOURCE_DIR}/scripts/check_sql_safety_gate.py)
set(CS_RUNTIME_BLOCKING_GATE_SCRIPT ${CMAKE_SOURCE_DIR}/scripts/check_runtime_blocking_gate.py)
set(CS_NO_INC_FRAGMENTS_GATE_SCRIPT ${CMAKE_SOURCE_DIR}/scripts/check_no_inc_fragments.py)
set(CS_IPP_MACRO_BOUNDARIES_GATE_SCRIPT ${CMAKE_SOURCE_DIR}/scripts/check_ipp_macro_boundaries.py)
set(CS_SQL_EXEC_ACCOUNTABILITY_GATE_SCRIPT ${CMAKE_SOURCE_DIR}/scripts/check_sql_exec_accountability.py)
set(CS_PERFORMANCE_TZ_100_SCRIPT ${CMAKE_SOURCE_DIR}/scripts/check_performance_tz_100_completion.py)
set(CS_HAS_BUILD_HELPER_SCRIPTS FALSE)
set(CS_PYTHON_NO_BYTECODE_CMD ${CMAKE_COMMAND} -E env PYTHONDONTWRITEBYTECODE=1 ${Python3_EXECUTABLE})

# Mandatory structural contract; neither remediation mode nor the optional old
# quality-gate switch exempts new builds from dependency boundaries.
if (NOT Python3_Interpreter_FOUND)
    message(FATAL_ERROR "Python3 is required for mandatory structure checks")
endif()
add_custom_target(structure_contract_gate
    COMMAND ${CS_PYTHON_NO_BYTECODE_CMD} ${CMAKE_SOURCE_DIR}/scripts/check_structure_contract.py
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    VERBATIM)
add_custom_target(change_acceptance_gate
    COMMAND ${CS_PYTHON_NO_BYTECODE_CMD} ${CMAKE_SOURCE_DIR}/scripts/run_change_gates.py
        --build-dir ${CMAKE_BINARY_DIR} --cmake ${CMAKE_COMMAND} --ctest ${CMAKE_CTEST_COMMAND}
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    USES_TERMINAL VERBATIM)

if (Python3_Interpreter_FOUND AND EXISTS ${CS_ARCHITECTURE_GATE_SCRIPT})
    set(CS_HAS_BUILD_HELPER_SCRIPTS TRUE)
endif()





if (Python3_Interpreter_FOUND AND EXISTS ${CS_RUNTIME_BLOCKING_GATE_SCRIPT})
    add_custom_target(runtime_blocking_gate
        COMMAND ${CS_PYTHON_NO_BYTECODE_CMD} ${CS_RUNTIME_BLOCKING_GATE_SCRIPT}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Running strict runtime blocking/reentrancy gate"
        VERBATIM)
else()
    message(WARNING "Runtime blocking gate is unavailable; waitForFinished/msleep/processEvents checks will be skipped.")
endif()

if (Python3_Interpreter_FOUND AND EXISTS ${CS_NO_INC_FRAGMENTS_GATE_SCRIPT})
    add_custom_target(no_inc_fragments_gate
        COMMAND ${CS_PYTHON_NO_BYTECODE_CMD} ${CS_NO_INC_FRAGMENTS_GATE_SCRIPT}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Running P2 .inc retirement gate"
        VERBATIM)
else()
    message(WARNING "No-.inc fragment gate is unavailable; transitional fragment checks will be skipped.")
endif()

if (Python3_Interpreter_FOUND AND EXISTS ${CS_IPP_MACRO_BOUNDARIES_GATE_SCRIPT})
    add_custom_target(ipp_macro_boundaries_gate
        COMMAND ${CS_PYTHON_NO_BYTECODE_CMD} ${CS_IPP_MACRO_BOUNDARIES_GATE_SCRIPT}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Running .ipp macro-boundary gate"
        VERBATIM)
else()
    message(WARNING ".ipp macro-boundary gate is unavailable; generated fragment boundary checks will be skipped.")
endif()

if (Python3_Interpreter_FOUND AND EXISTS ${CS_SQL_EXEC_ACCOUNTABILITY_GATE_SCRIPT})
    add_custom_target(sql_exec_accountability_gate
        COMMAND ${CS_PYTHON_NO_BYTECODE_CMD} ${CS_SQL_EXEC_ACCOUNTABILITY_GATE_SCRIPT}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Running SQL exec accountability gate"
        VERBATIM)
else()
    message(WARNING "SQL exec accountability gate is unavailable; direct exec accounting will be skipped.")
endif()

if (Python3_Interpreter_FOUND AND EXISTS ${CS_PERFORMANCE_TZ_100_SCRIPT})
    add_custom_target(performance_tz_100_completion_gate
        COMMAND ${CS_PYTHON_NO_BYTECODE_CMD} ${CS_PERFORMANCE_TZ_100_SCRIPT}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Running performance TZ 100 completion gate"
        VERBATIM)
else()
    message(WARNING "Performance TZ 100 completion gate is unavailable; final runtime-family checks will be skipped.")
endif()

if (Python3_Interpreter_FOUND AND EXISTS ${CS_SQL_SAFETY_GATE_SCRIPT})
    add_custom_target(sql_safety_gate
        COMMAND ${CS_PYTHON_NO_BYTECODE_CMD} ${CS_SQL_SAFETY_GATE_SCRIPT}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Running P0 SQL transaction/destructive-operation safety gate"
        VERBATIM)
else()
    message(WARNING "SQL safety gate is unavailable; destructive SQL/transaction checks will be skipped.")
endif()

if (Python3_Interpreter_FOUND AND EXISTS ${CS_MONOLITH_SPLIT_GATE_SCRIPT})
    add_custom_target(monolith_split_gate
        COMMAND ${CS_PYTHON_NO_BYTECODE_CMD} ${CS_MONOLITH_SPLIT_GATE_SCRIPT}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Running P1/P2 monolith split and file-size gate"
        VERBATIM)
else()
    message(WARNING "Monolith split gate is unavailable; large .cpp/.h files will not be preflight-checked.")
endif()

if (Python3_Interpreter_FOUND AND EXISTS ${CS_P0_SAFETY_GATE_SCRIPT})
    add_custom_target(p0_safety_gate
        COMMAND ${CS_PYTHON_NO_BYTECODE_CMD} ${CS_P0_SAFETY_GATE_SCRIPT}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Running P0 safety gate for UI blocking, destructive deletes and schema guards"
        VERBATIM)
else()
    message(WARNING "P0 safety gate is unavailable; processEvents/raw delete/schema snapshot checks will be skipped.")
endif()

if (Python3_Interpreter_FOUND AND EXISTS ${CS_SUPPORT_INTEGRITY_SCRIPT})
    add_custom_target(support_integrity_gate
        COMMAND ${CS_PYTHON_NO_BYTECODE_CMD} ${CS_SUPPORT_INTEGRITY_SCRIPT}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Running support/include integrity gate"
        VERBATIM)
else()
    message(WARNING "Support integrity gate is unavailable; critical support/include files will not be preflight-checked.")
endif()

if (Python3_Interpreter_FOUND AND EXISTS ${CS_RUNTIME_CONFLICT_GATE_SCRIPT})
    add_custom_target(runtime_conflict_gate
        COMMAND ${CS_PYTHON_NO_BYTECODE_CMD} ${CS_RUNTIME_CONFLICT_GATE_SCRIPT}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Running P3 runtime/support conflict gate"
        VERBATIM)
else()
    message(WARNING "Runtime conflict gate is unavailable; placeholder/support tail validation will be skipped.")
endif()


if (Python3_Interpreter_FOUND AND EXISTS ${CS_DOMAIN_INTEGRITY_SCRIPT})
    add_custom_target(domain_integrity_gate
        COMMAND ${CS_PYTHON_NO_BYTECODE_CMD} ${CS_DOMAIN_INTEGRITY_SCRIPT}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Running P5 domain integrity gate"
        VERBATIM)
else()
    message(WARNING "Domain integrity gate is unavailable; P5 structural scenario validation will be skipped.")
endif()

if (Python3_Interpreter_FOUND AND EXISTS ${CS_TEST_INVENTORY_SCRIPT})
    add_custom_target(test_inventory_gate
        COMMAND ${CS_PYTHON_NO_BYTECODE_CMD} ${CS_TEST_INVENTORY_SCRIPT}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Checking internal test inventory"
        VERBATIM)
else()
    message(WARNING "Test inventory gate is unavailable; internal test coverage inventory will not be preflight-checked.")
endif()

if (Python3_Interpreter_FOUND AND EXISTS ${CS_LEGACY_TAIL_INVENTORY_SCRIPT})
    add_custom_target(legacy_tail_inventory_gate
        COMMAND ${CS_PYTHON_NO_BYTECODE_CMD} ${CS_LEGACY_TAIL_INVENTORY_SCRIPT}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Checking P2 legacy/fallback inventory classification"
        VERBATIM)
else()
    message(WARNING "Legacy tail inventory gate is unavailable; legacy/fallback inventory validation will be skipped.")
endif()

if (Python3_Interpreter_FOUND AND EXISTS ${CS_LEGACY_TAIL_CLEANUP_SCRIPT})
    add_custom_target(legacy_tail_cleanup_gate
        COMMAND ${CS_PYTHON_NO_BYTECODE_CMD} ${CS_LEGACY_TAIL_CLEANUP_SCRIPT}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Checking P2 legacy/fallback cleanup and sync fallback policy"
        VERBATIM)
else()
    message(WARNING "Legacy tail cleanup gate is unavailable; delete-candidate and sync-fallback validation will be skipped.")
endif()

if (Python3_Interpreter_FOUND AND EXISTS ${CS_LEGACY_MARKER_CI_SCRIPT})
    add_custom_target(legacy_marker_ci_gate
        COMMAND ${CS_PYTHON_NO_BYTECODE_CMD} ${CS_LEGACY_MARKER_CI_SCRIPT}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Checking P2 legacy/fallback marker CI gate and transitional allowlist budget"
        VERBATIM)
else()
    message(WARNING "Legacy marker CI gate is unavailable; new legacy/fallback/MissingMethods markers will not be preflight-checked.")
endif()

if (Python3_Interpreter_FOUND AND EXISTS ${CS_LOGGING_DIAGNOSTICS_SCRIPT})
    add_custom_target(logging_diagnostics_gate
        COMMAND ${CS_PYTHON_NO_BYTECODE_CMD} ${CS_LOGGING_DIAGNOSTICS_SCRIPT}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Checking P2 logging and crash diagnostics guard"
        VERBATIM)
else()
    message(WARNING "Logging diagnostics gate is unavailable; P2 crash diagnostics validation will be skipped.")
endif()

if (Python3_Interpreter_FOUND AND EXISTS ${CS_P2_ACCEPTANCE_TESTS_SCRIPT})
    add_custom_target(p2_acceptance_tests_gate
        COMMAND ${CS_PYTHON_NO_BYTECODE_CMD} ${CS_P2_ACCEPTANCE_TESTS_SCRIPT}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Checking P2 unit/integration/smoke/readiness acceptance coverage"
        VERBATIM)
else()
    message(WARNING "P2 acceptance tests gate is unavailable; P2 unit/integration/smoke/readiness coverage validation will be skipped.")
endif()

if (Python3_Interpreter_FOUND AND EXISTS ${CS_PROJECT_LAYOUT_SCRIPT})
    add_custom_target(project_layout_gate
        COMMAND ${CS_PYTHON_NO_BYTECODE_CMD} ${CS_PROJECT_LAYOUT_SCRIPT}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Checking final project layout"
        VERBATIM)
else()
    message(WARNING "Project layout gate is unavailable; final layout preflight-check will be skipped.")
endif()

if (Python3_Interpreter_FOUND AND EXISTS ${CS_NAMING_POLICY_SCRIPT})
    add_custom_target(naming_policy_gate
        COMMAND ${CS_PYTHON_NO_BYTECODE_CMD} ${CS_NAMING_POLICY_SCRIPT}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Checking naming policy"
        VERBATIM)
else()
    message(WARNING "Naming policy gate is unavailable; naming preflight-check will be skipped.")
endif()

if (Python3_Interpreter_FOUND AND EXISTS ${CS_BUILD_SANITY_SCRIPT})
    add_custom_target(build_sanity_gate
        COMMAND ${CS_PYTHON_NO_BYTECODE_CMD} ${CS_BUILD_SANITY_SCRIPT}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Running build-time sanity checks"
        VERBATIM)
else()
    message(WARNING "Build sanity gate is unavailable; qrc/main/temp artifact validation will be skipped.")
endif()

if (Python3_Interpreter_FOUND AND EXISTS ${CS_REGRESSION_CHECKLIST_SCRIPT})
    add_custom_target(regression_checklist_gate
        COMMAND ${CS_PYTHON_NO_BYTECODE_CMD} ${CS_REGRESSION_CHECKLIST_SCRIPT}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Checking regression checklist coverage"
        VERBATIM)
else()
    message(WARNING "Regression checklist gate is unavailable; release checklist validation will be skipped.")
endif()

if (Python3_Interpreter_FOUND AND EXISTS ${CS_PART_PRICING_TZ_SCRIPT})
    add_custom_target(part_pricing_tz_gate
        COMMAND ${CS_PYTHON_NO_BYTECODE_CMD} ${CS_PART_PRICING_TZ_SCRIPT}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Checking part pricing TZ contract"
        VERBATIM)
else()
    message(WARNING "Part pricing TZ gate is unavailable; markup pricing acceptance guard will be skipped.")
endif()

if (Python3_Interpreter_FOUND AND EXISTS ${CS_UPDATE_RELEASE_CONTRACT_SCRIPT})
    add_custom_target(update_release_contract_gate
        COMMAND ${CS_PYTHON_NO_BYTECODE_CMD} ${CS_UPDATE_RELEASE_CONTRACT_SCRIPT}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Checking GitHub updater/release contract"
        VERBATIM)
else()
    message(WARNING "Update release contract gate is unavailable; updater GitHub/installer contract will not be preflight-checked.")
endif()

if (CS_HAS_BUILD_HELPER_SCRIPTS)
    add_custom_target(architecture_gate
        COMMAND ${CS_PYTHON_NO_BYTECODE_CMD} ${CS_ARCHITECTURE_GATE_SCRIPT}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Running architecture dependency gate"
        VERBATIM)

    if (EXISTS ${CS_PERF_GUARD_SCRIPT})
        add_custom_target(perf_guard
            COMMAND ${CS_PYTHON_NO_BYTECODE_CMD} ${CS_PERF_GUARD_SCRIPT}
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            COMMENT "Running P4 async/performance guard"
            VERBATIM)
    endif()

    if (EXISTS ${CS_RELEASE_ARCHIVE_SCRIPT})
        add_custom_target(release_archive
            COMMAND ${CS_PYTHON_NO_BYTECODE_CMD} ${CS_RELEASE_ARCHIVE_SCRIPT} CarService_release_checked.zip
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            COMMENT "Creating release archive guarded by architecture/support gates"
            VERBATIM)
        add_dependencies(release_archive architecture_gate)
    endif()
    if (TARGET release_archive)
        if (TARGET project_layout_gate)
            add_dependencies(release_archive project_layout_gate)
        endif()
        if (TARGET naming_policy_gate)
            add_dependencies(release_archive naming_policy_gate)
        endif()
        if (TARGET perf_guard)
            add_dependencies(release_archive perf_guard)
        endif()
        if (TARGET support_integrity_gate)
            add_dependencies(release_archive support_integrity_gate)
        endif()
        if (TARGET runtime_conflict_gate)
            add_dependencies(release_archive runtime_conflict_gate)
        endif()
        if (TARGET domain_integrity_gate)
            add_dependencies(release_archive domain_integrity_gate)
        endif()
        if (TARGET test_inventory_gate)
            add_dependencies(release_archive test_inventory_gate)
        endif()
        if (TARGET legacy_tail_inventory_gate)
            add_dependencies(release_archive legacy_tail_inventory_gate)
        endif()
        if (TARGET legacy_tail_cleanup_gate)
            add_dependencies(release_archive legacy_tail_cleanup_gate)
        endif()
        if (TARGET legacy_marker_ci_gate)
            add_dependencies(release_archive legacy_marker_ci_gate)
        endif()
        if (TARGET logging_diagnostics_gate)
            add_dependencies(release_archive logging_diagnostics_gate)
        endif()
        if (TARGET build_sanity_gate)
            add_dependencies(release_archive build_sanity_gate)
        endif()
        if (TARGET regression_checklist_gate)
            add_dependencies(release_archive regression_checklist_gate)
        endif()
        if (TARGET part_pricing_tz_gate)
            add_dependencies(release_archive part_pricing_tz_gate)
        endif()
        if (TARGET update_release_contract_gate)
            add_dependencies(release_archive update_release_contract_gate)
        endif()
        if (TARGET p0_safety_gate)
            add_dependencies(release_archive p0_safety_gate)
        endif()
        if (TARGET monolith_split_gate)
            add_dependencies(release_archive monolith_split_gate)
        endif()
        if (TARGET sql_safety_gate)
            add_dependencies(release_archive sql_safety_gate)
        endif()
        if (TARGET runtime_blocking_gate)
            add_dependencies(release_archive runtime_blocking_gate)
        endif()
        if (TARGET no_inc_fragments_gate)
            add_dependencies(release_archive no_inc_fragments_gate)
        endif()
        if (TARGET ipp_macro_boundaries_gate)
            add_dependencies(release_archive ipp_macro_boundaries_gate)
        endif()
        if (TARGET sql_exec_accountability_gate)
            add_dependencies(release_archive sql_exec_accountability_gate)
        endif()
    endif()
else()
    message(STATUS "Architecture helper script is absent; architecture_gate target will be skipped.")
endif()

set(CS_COMMON_SOURCES ${SRC_FILES})
list(REMOVE_ITEM CS_COMMON_SOURCES src/main.cpp)

# Keep source paths anchored to the project root. On some Windows/Ninja + Qt
# configure runs Qt's wrapper may otherwise normalize relative source entries
# through the binary directory and look for files under build-release/src/...
# instead of the real source tree.
set(CS_COMMON_SOURCES_ABS "")
foreach(_cs_source IN LISTS CS_COMMON_SOURCES)
    if (IS_ABSOLUTE "${_cs_source}")
        list(APPEND CS_COMMON_SOURCES_ABS "${_cs_source}")
    else()
        list(APPEND CS_COMMON_SOURCES_ABS "${CMAKE_CURRENT_SOURCE_DIR}/${_cs_source}")
    endif()
endforeach()

set(CS_HEADERS_ABS "")
foreach(_cs_header IN LISTS HDR_FILES)
    if (IS_ABSOLUTE "${_cs_header}")
        list(APPEND CS_HEADERS_ABS "${_cs_header}")
    else()
        list(APPEND CS_HEADERS_ABS "${CMAKE_CURRENT_SOURCE_DIR}/${_cs_header}")
    endif()
endforeach()

file(GLOB CS_SOUND_ASSET_FILES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/assets/*.wav"
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/assets/*.mp3"
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/assets/*.ogg"
)

qt_add_library(CarServiceLib STATIC
    ${CS_COMMON_SOURCES_ABS}
    ${CS_HEADERS_ABS}
)

qt_add_executable(CarService
    WIN32
    "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/resources.qrc"
    ${CS_APP_ICON_RC}
)
set_target_properties(CarService PROPERTIES OUTPUT_NAME "CarServicePointG")
add_dependencies(CarServiceLib structure_contract_gate)
add_dependencies(CarService structure_contract_gate)

# Keep CI/release quality gates mandatory by default.  A workstation build can
# opt out explicitly when unrelated legacy gates prevent validation of a
# targeted UI fix; this never changes the default build contract.
option(CS_ENFORCE_QUALITY_GATES "Run quality gates before building the desktop executable" ON)
option(CS_REMEDIATION_TEST_BUILD "Use reviewed no-new-debt gates for test library; desktop release keeps strict gates" OFF)
if (CS_REMEDIATION_TEST_BUILD)
    foreach(gate_script IN ITEMS runtime_blocking_gate sql_exec_accountability performance_tz_100_completion monolith_split_gate legacy_tail_inventory legacy_marker_ci_gate project_layout perf_guards)
        add_custom_target(development_${gate_script}
            COMMAND ${CS_PYTHON_NO_BYTECODE_CMD} ${CMAKE_SOURCE_DIR}/scripts/run_development_gate.py check_${gate_script}.py
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            COMMENT "Checking reviewed development debt; not release acceptance")
    endforeach()
endif()
if (CS_ENFORCE_QUALITY_GATES)
if (TARGET architecture_gate)
    add_dependencies(CarServiceLib architecture_gate)
    add_dependencies(CarService architecture_gate)
endif()
if (TARGET p0_safety_gate)
    add_dependencies(CarServiceLib p0_safety_gate)
    add_dependencies(CarService p0_safety_gate)
endif()
if (TARGET monolith_split_gate)
    if (CS_REMEDIATION_TEST_BUILD)
        add_dependencies(CarServiceLib development_monolith_split_gate)
    else()
        add_dependencies(CarServiceLib monolith_split_gate)
    endif()
    add_dependencies(CarService monolith_split_gate)
endif()
if (TARGET sql_safety_gate)
    add_dependencies(CarServiceLib sql_safety_gate)
    add_dependencies(CarService sql_safety_gate)
endif()
if (TARGET runtime_blocking_gate)
    if (CS_REMEDIATION_TEST_BUILD)
        add_dependencies(CarServiceLib development_runtime_blocking_gate)
    else()
    add_dependencies(CarServiceLib runtime_blocking_gate)
    endif()
    add_dependencies(CarService runtime_blocking_gate)
endif()
if (TARGET no_inc_fragments_gate)
    add_dependencies(CarServiceLib no_inc_fragments_gate)
    add_dependencies(CarService no_inc_fragments_gate)
endif()
if (TARGET ipp_macro_boundaries_gate)
    add_dependencies(CarServiceLib ipp_macro_boundaries_gate)
    add_dependencies(CarService ipp_macro_boundaries_gate)
endif()
if (TARGET sql_exec_accountability_gate)
    if (CS_REMEDIATION_TEST_BUILD)
        add_dependencies(CarServiceLib development_sql_exec_accountability)
    else()
    add_dependencies(CarServiceLib sql_exec_accountability_gate)
    endif()
    add_dependencies(CarService sql_exec_accountability_gate)
endif()
if (TARGET project_layout_gate)
    if (CS_REMEDIATION_TEST_BUILD)
        add_dependencies(CarServiceLib development_project_layout)
    else()
    add_dependencies(CarServiceLib project_layout_gate)
    endif()
    add_dependencies(CarService project_layout_gate)
endif()
if (TARGET naming_policy_gate)
    add_dependencies(CarServiceLib naming_policy_gate)
    add_dependencies(CarService naming_policy_gate)
endif()
if (TARGET perf_guard)
    if (CS_REMEDIATION_TEST_BUILD)
        add_dependencies(CarServiceLib development_perf_guards)
    else()
    add_dependencies(CarServiceLib perf_guard)
    endif()
    add_dependencies(CarService perf_guard)
endif()
if (TARGET support_integrity_gate)
    add_dependencies(CarServiceLib support_integrity_gate)
    add_dependencies(CarService support_integrity_gate)
endif()
if (TARGET runtime_conflict_gate)
    add_dependencies(CarServiceLib runtime_conflict_gate)
    add_dependencies(CarService runtime_conflict_gate)
endif()
if (TARGET domain_integrity_gate)
    add_dependencies(CarServiceLib domain_integrity_gate)
    add_dependencies(CarService domain_integrity_gate)
endif()
if (TARGET test_inventory_gate)
    add_dependencies(CarServiceLib test_inventory_gate)
    add_dependencies(CarService test_inventory_gate)
endif()
if (TARGET legacy_tail_inventory_gate)
    if (CS_REMEDIATION_TEST_BUILD)
        add_dependencies(CarServiceLib development_legacy_tail_inventory)
    else()
    add_dependencies(CarServiceLib legacy_tail_inventory_gate)
    endif()
    add_dependencies(CarService legacy_tail_inventory_gate)
endif()
if (TARGET legacy_tail_cleanup_gate)
    add_dependencies(CarServiceLib legacy_tail_cleanup_gate)
    add_dependencies(CarService legacy_tail_cleanup_gate)
endif()
if (TARGET legacy_marker_ci_gate)
    if (CS_REMEDIATION_TEST_BUILD)
        add_dependencies(CarServiceLib development_legacy_marker_ci_gate)
    else()
    add_dependencies(CarServiceLib legacy_marker_ci_gate)
    endif()
    add_dependencies(CarService legacy_marker_ci_gate)
endif()
if (TARGET logging_diagnostics_gate)
    add_dependencies(CarServiceLib logging_diagnostics_gate)
    add_dependencies(CarService logging_diagnostics_gate)
endif()
if (TARGET p2_acceptance_tests_gate)
    add_dependencies(CarServiceLib p2_acceptance_tests_gate)
    add_dependencies(CarService p2_acceptance_tests_gate)
endif()
if (TARGET performance_tz_100_completion_gate)
    if (CS_REMEDIATION_TEST_BUILD)
        add_dependencies(CarServiceLib development_performance_tz_100_completion)
    else()
    add_dependencies(CarServiceLib performance_tz_100_completion_gate)
    endif()
    add_dependencies(CarService performance_tz_100_completion_gate)
endif()
if (TARGET part_pricing_tz_gate)
    add_dependencies(CarServiceLib part_pricing_tz_gate)
    add_dependencies(CarService part_pricing_tz_gate)
endif()

if (TARGET update_release_contract_gate)
    add_dependencies(CarServiceLib update_release_contract_gate)
    add_dependencies(CarService update_release_contract_gate)
endif()
endif()

# One include root keeps includes stable as we migrate.
target_include_directories(CarServiceLib PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src" "${CMAKE_BINARY_DIR}/generated")
target_include_directories(CarService PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src" "${CMAKE_BINARY_DIR}/generated")

# Optional Multimedia / Bluetooth
if (TARGET Qt6::Multimedia)
    target_link_libraries(CarServiceLib PUBLIC Qt6::Multimedia)
    target_compile_definitions(CarServiceLib PUBLIC CS_HAS_MULTIMEDIA=1)
else()
    target_compile_definitions(CarServiceLib PUBLIC CS_HAS_MULTIMEDIA=0)
endif()

if (TARGET Qt6::Bluetooth)
    target_link_libraries(CarServiceLib PUBLIC Qt6::Bluetooth)
    target_compile_definitions(CarServiceLib PUBLIC CS_HAS_BLUETOOTH=1)
else()
    target_compile_definitions(CarServiceLib PUBLIC CS_HAS_BLUETOOTH=0)
endif()

if (TARGET Qt6::Pdf)
    target_link_libraries(CarServiceLib PUBLIC Qt6::Pdf)
    target_compile_definitions(CarServiceLib PUBLIC CS_HAS_QT_PDF=1)
else()
    target_compile_definitions(CarServiceLib PUBLIC CS_HAS_QT_PDF=0)
endif()

target_link_libraries(CarServiceLib PUBLIC
    Qt6::CorePrivate
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Sql
    Qt6::PrintSupport
    Qt6::Network
)

target_link_libraries(CarService PRIVATE CarServiceLib)

if (TARGET SQLite::SQLite3)
    target_link_libraries(CarServiceLib PUBLIC SQLite::SQLite3)
    target_compile_definitions(CarServiceLib PUBLIC CS_HAS_SQLITE3_BACKUP_API=1)
elseif (SQLite3_FOUND)
    target_include_directories(CarServiceLib PUBLIC ${SQLite3_INCLUDE_DIRS})
    target_link_libraries(CarServiceLib PUBLIC ${SQLite3_LIBRARIES})
    target_compile_definitions(CarServiceLib PUBLIC CS_HAS_SQLITE3_BACKUP_API=1)
else()
    target_compile_definitions(CarServiceLib PUBLIC CS_HAS_SQLITE3_BACKUP_API=0)
endif()

if (WIN32)
    target_link_libraries(CarServiceLib PUBLIC winmm advapi32 bcrypt crypt32 ole32)

    if (CS_SOUND_ASSET_FILES)
        add_custom_command(TARGET CarService POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:CarService>/resources/assets"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${CS_SOUND_ASSET_FILES}
                "$<TARGET_FILE_DIR:CarService>/resources/assets"
            COMMENT "Copying app sound assets"
            VERBATIM)
    endif()

    if (EXISTS "${CMAKE_SOURCE_DIR}/resources/assets/vehicle_previews")
        add_custom_command(TARGET CarService POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:CarService>/resources/assets/vehicle_previews"
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_SOURCE_DIR}/resources/assets/vehicle_previews"
                "$<TARGET_FILE_DIR:CarService>/resources/assets/vehicle_previews"
            COMMENT "Copying local vehicle preview assets"
            VERBATIM)
    endif()

    foreach(CS_QT_IMAGE_PLUGIN IN ITEMS QJpegPlugin QWebpPlugin)
        if (TARGET Qt6::${CS_QT_IMAGE_PLUGIN})
            add_custom_command(TARGET CarService POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:CarService>/imageformats"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "$<TARGET_FILE:Qt6::${CS_QT_IMAGE_PLUGIN}>"
                    "$<TARGET_FILE_DIR:CarService>/imageformats"
                COMMENT "Copying Qt image plugin ${CS_QT_IMAGE_PLUGIN} for vehicle previews"
                VERBATIM)
        endif()
    endforeach()
    if (DEFINED Qt6Core_DIR)
        get_filename_component(CS_QT_CMAKE_DIR "${Qt6Core_DIR}" DIRECTORY)
        get_filename_component(CS_QT_LIB_DIR "${CS_QT_CMAKE_DIR}" DIRECTORY)
        get_filename_component(CS_QT_PREFIX_DIR "${CS_QT_LIB_DIR}" DIRECTORY)
        foreach(CS_QT_IMAGE_PLUGIN_FILE IN ITEMS qjpeg.dll qwebp.dll)
            if (EXISTS "${CS_QT_PREFIX_DIR}/plugins/imageformats/${CS_QT_IMAGE_PLUGIN_FILE}")
                add_custom_command(TARGET CarService POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:CarService>/imageformats"
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${CS_QT_PREFIX_DIR}/plugins/imageformats/${CS_QT_IMAGE_PLUGIN_FILE}"
                        "$<TARGET_FILE_DIR:CarService>/imageformats"
                    COMMENT "Copying Qt image plugin ${CS_QT_IMAGE_PLUGIN_FILE} for vehicle previews"
                    VERBATIM)
            endif()
        endforeach()
    endif()

    # Keep the PDF invoice recognition helpers beside the built executable.
    # The installer has explicit [Files] entries for the same folders, but this
    # post-build copy also protects direct build-release/dist smoke tests.
    if (EXISTS "${CMAKE_SOURCE_DIR}/resources/python_vendor")
        add_custom_command(TARGET CarService POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:CarService>/resources"
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_SOURCE_DIR}/resources/python_vendor"
                "$<TARGET_FILE_DIR:CarService>/resources/python_vendor"
            COMMENT "Copying bundled Python PDF vendor modules"
            VERBATIM)
    endif()
    if (EXISTS "${CMAKE_SOURCE_DIR}/resources/windows/ocr-layer")
        add_custom_command(TARGET CarService POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:CarService>/resources/windows"
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_SOURCE_DIR}/resources/windows/ocr-layer"
                "$<TARGET_FILE_DIR:CarService>/resources/windows/ocr-layer"
            COMMENT "Copying app-local OCR layer metadata/tools"
            VERBATIM)
    endif()
endif()

include(cmake/modules/CarServiceTests.cmake)
