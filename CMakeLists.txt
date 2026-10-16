cmake_minimum_required(VERSION 3.20)

set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION")
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" W11WIDGET_VERSION LIMIT_COUNT 1)
project(w11widget VERSION "${W11WIDGET_VERSION}" LANGUAGES CXX)

set(W11WIDGET_DISTRIBUTION_NOTICES_DIR "" CACHE PATH
    "Reviewed third-party notices and corresponding library sources for distribution")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

enable_testing()

if(WIN32)
    add_executable(taskbar_diagnostic
        src/taskbar_diagnostic.cpp
    )

    target_compile_definitions(taskbar_diagnostic PRIVATE UNICODE _UNICODE)

    target_link_libraries(taskbar_diagnostic PRIVATE
        shell32
        shcore
        user32
        gdi32
    )

    add_test(NAME taskbar_diagnostic_self_test COMMAND taskbar_diagnostic --self-test)

endif()

add_library(panel_positioner STATIC src/core/PanelPositioner.cpp)
target_include_directories(panel_positioner PUBLIC src)
add_executable(test_positioner tests/test_positioner.cpp)
target_link_libraries(test_positioner PRIVATE panel_positioner)
add_test(NAME panel_positioner_test COMMAND test_positioner)
add_library(visibility_policy STATIC src/core/VisibilityPolicy.cpp)
target_include_directories(visibility_policy PUBLIC src)
add_executable(test_visibility_policy tests/test_visibility_policy.cpp)
target_link_libraries(test_visibility_policy PRIVATE visibility_policy)
add_test(NAME panel_visibility_policy_test COMMAND test_visibility_policy)

option(W11WIDGET_BUILD_OVERLAY "Build Qt widgets and, on Windows, the native taskbar panel" ON)
if(W11WIDGET_BUILD_OVERLAY)
    find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 Widgets)
    add_library(widget_host STATIC
        src/core/IWidget.h
        src/core/WidgetConfiguration.h
        src/core/WidgetRegistry.h
        src/core/WidgetRegistry.cpp
        src/core/WidgetHostModel.h
        src/core/WidgetHostModel.cpp
        src/widgets/SampleWidget.h
        src/widgets/SampleWidget.cpp
        src/widgets/WorldClockWidget.h
        src/widgets/WorldClockWidget.cpp
        src/widgets/CountdownWidget.h
        src/widgets/CountdownWidget.cpp
        src/widgets/FocusTimerWidget.h
        src/widgets/FocusTimerWidget.cpp
        src/ui/Strings.h
        src/ui/Strings.cpp
        src/ui/ScreenGeometry.h
        src/ui/ScreenGeometry.cpp
        src/ui/strings.qrc
    )
    set_target_properties(widget_host PROPERTIES AUTOMOC ON AUTORCC ON)
    target_include_directories(widget_host PUBLIC src)
    target_link_libraries(widget_host PUBLIC Qt6::Core Qt6::Gui Qt6::Widgets)
    add_executable(test_widget_host tests/test_widget_host.cpp)
    target_link_libraries(test_widget_host PRIVATE widget_host)
    add_test(NAME panel_widget_host_test COMMAND test_widget_host)
    add_executable(test_world_clock tests/test_world_clock.cpp)
    target_link_libraries(test_world_clock PRIVATE widget_host)
    add_test(NAME panel_world_clock_test COMMAND test_world_clock)
    add_executable(test_countdown tests/test_countdown.cpp)
    target_link_libraries(test_countdown PRIVATE widget_host)
    add_test(NAME panel_countdown_test COMMAND test_countdown)
    add_executable(test_focus_timer tests/test_focus_timer.cpp)
    target_link_libraries(test_focus_timer PRIVATE widget_host)
    add_test(NAME panel_focus_timer_test COMMAND test_focus_timer)
    add_library(widget_controls STATIC
        src/ui/WidgetSettingsDialog.h
        src/ui/WidgetSettingsDialog.cpp
        src/ui/FluentStyle.h
        src/ui/FluentStyle.cpp
        src/ui/PlacementPreview.h
        src/ui/PlacementPreview.cpp
    )
    set_target_properties(widget_controls PROPERTIES AUTOMOC ON)
    target_include_directories(widget_controls PUBLIC src)
    target_link_libraries(widget_controls PUBLIC widget_host Qt6::Widgets)
    add_executable(test_widget_controls tests/test_widget_controls.cpp)
    target_link_libraries(test_widget_controls PRIVATE widget_controls)
    add_test(NAME panel_widget_controls_test COMMAND test_widget_controls)
    add_library(panel_settings STATIC src/core/Settings.cpp)
    target_include_directories(panel_settings PUBLIC src)
    target_link_libraries(panel_settings PUBLIC Qt6::Core)
    add_executable(test_settings tests/test_settings.cpp)
    target_link_libraries(test_settings PRIVATE panel_settings)
    add_test(NAME panel_settings_test COMMAND test_settings)
    add_executable(test_qml_components tests/test_qml_components.cpp src/ui/panel.qrc)
    set_target_properties(test_qml_components PROPERTIES AUTORCC ON)
    target_link_libraries(test_qml_components PRIVATE widget_controls Qt6::Qml Qt6::Quick Qt6::QuickControls2)
    add_test(NAME panel_qml_components_test COMMAND test_qml_components)
    set(_portable_qt_tests panel_settings_test panel_widget_host_test panel_world_clock_test
        panel_countdown_test panel_focus_timer_test panel_widget_controls_test panel_qml_components_test)
    set_tests_properties(${_portable_qt_tests} PROPERTIES TIMEOUT 20
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")

    if(WIN32)
        add_library(application_instance STATIC src/platform/windows/ApplicationInstance.cpp)
        target_include_directories(application_instance PUBLIC src)
        target_compile_definitions(application_instance PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
        target_link_libraries(application_instance PUBLIC Qt6::Core user32)
        add_executable(test_application_instance tests/test_application_instance.cpp)
        target_link_libraries(test_application_instance PRIVATE application_instance)
        add_test(NAME panel_application_instance_test COMMAND test_application_instance)
        qt_add_executable(w11widget WIN32
            src/main.cpp
            src/app/ApplicationController.h
            src/app/ApplicationController.cpp
            src/platform/windows/TaskbarLocator.cpp
            src/platform/windows/WindowBehavior.cpp
            src/ui/PanelWindow.h
            src/ui/PanelWindow.cpp
            src/ui/panel.qrc
        )
        set_target_properties(w11widget PROPERTIES AUTOMOC ON AUTORCC ON RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/${PROJECT_VERSION}")
        file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/panel-executable.txt" CONTENT "$<TARGET_FILE:w11widget>\n")
        target_include_directories(w11widget PRIVATE src)
        target_compile_definitions(w11widget PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN "W11WIDGET_APP_VERSION=\"${PROJECT_VERSION}\"")
        target_link_libraries(w11widget PRIVATE application_instance panel_positioner panel_settings visibility_policy widget_host widget_controls Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::Widgets user32 shell32 dwmapi)
        find_program(WINDEPLOYQT_EXECUTABLE NAMES windeployqt REQUIRED)
        get_filename_component(_windeployqt_bin_dir "${WINDEPLOYQT_EXECUTABLE}" DIRECTORY)
        get_filename_component(_qt_cmake_dir "${Qt6_DIR}" DIRECTORY)
        get_filename_component(_qt_library_dir "${_qt_cmake_dir}" DIRECTORY)
        get_filename_component(_qt_install_prefix "${_qt_library_dir}" DIRECTORY)
        set(_qt_runtime_bin_dir "${_qt_install_prefix}/bin")
        get_filename_component(_qt_qml_root "${_windeployqt_bin_dir}" DIRECTORY)
        set(_qt_qml_dir "${_qt_qml_root}/qml")
        set(_compiler_runtime_dir "")
        if(MSVC)
            set(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS_SKIP TRUE)
            include(InstallRequiredSystemLibraries)
            if(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS)
                list(GET CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS 0 _msvc_runtime_file)
                get_filename_component(_compiler_runtime_dir "${_msvc_runtime_file}" DIRECTORY)
            endif()
            get_filename_component(_msvc_tools_bin "${CMAKE_LINKER}" DIRECTORY)
            set(_compiler_family "MSVC")
            set(_dependency_tool "dumpbin")
            set(_dependency_command "${_msvc_tools_bin}/dumpbin.exe")
        elseif(MINGW)
            get_filename_component(_compiler_runtime_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
            set(_compiler_family "MINGW")
            set(_dependency_tool "objdump")
            set(_dependency_command "${CMAKE_OBJDUMP}")
        else()
            message(FATAL_ERROR "Windows packaging requires MSVC or MinGW.")
        endif()
        set(W11WIDGET_DEPLOY_DIR "${CMAKE_BINARY_DIR}/deploy/w11widget-${PROJECT_VERSION}")
        add_custom_target(deploy_w11widget
            COMMAND "${CMAKE_COMMAND}" -E rm -rf "${W11WIDGET_DEPLOY_DIR}"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${W11WIDGET_DEPLOY_DIR}"
            COMMAND "${CMAKE_COMMAND}" -E copy "$<TARGET_FILE:w11widget>" "${W11WIDGET_DEPLOY_DIR}"
            COMMAND "${CMAKE_COMMAND}" -E env
                    "PATH=${_qt_runtime_bin_dir};${_windeployqt_bin_dir};${_qt_qml_root};$ENV{PATH}"
                    "${WINDEPLOYQT_EXECUTABLE}" --release --no-compiler-runtime --no-translations
                    --skip-plugin-types qmltooling
                    --qmldir "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/qml" --qmlimport "${_qt_qml_dir}"
                    --qml-deploy-dir "${W11WIDGET_DEPLOY_DIR}/qml"
                    --dir "${W11WIDGET_DEPLOY_DIR}" "$<TARGET_FILE:w11widget>"
            COMMAND "${CMAKE_COMMAND}" "-DPACKAGE_DIR=${W11WIDGET_DEPLOY_DIR}"
                    "-DQT_RUNTIME_DIR=${_qt_runtime_bin_dir}" "-DQT_BIN_DIR=${_windeployqt_bin_dir}"
                    "-DCOMPILER_FAMILY=${_compiler_family}" "-DCOMPILER_RUNTIME_DIR=${_compiler_runtime_dir}"
                    "-DDEPENDENCY_TOOL=${_dependency_tool}" "-DDEPENDENCY_COMMAND=${_dependency_command}"
                    -P "${CMAKE_CURRENT_SOURCE_DIR}/scripts/complete-runtime.cmake"
            COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_CURRENT_SOURCE_DIR}/config/qt.conf" "${W11WIDGET_DEPLOY_DIR}/qt.conf"
            DEPENDS w11widget
            VERBATIM
        )
        if(W11WIDGET_DISTRIBUTION_NOTICES_DIR)
            if(NOT EXISTS "${W11WIDGET_DISTRIBUTION_NOTICES_DIR}/THIRD-PARTY-NOTICES.txt")
                message(FATAL_ERROR "Distribution notices directory is missing THIRD-PARTY-NOTICES.txt")
            endif()
            add_custom_command(TARGET deploy_w11widget POST_BUILD
                COMMAND "${CMAKE_COMMAND}" -E copy_directory
                    "${W11WIDGET_DISTRIBUTION_NOTICES_DIR}" "${W11WIDGET_DEPLOY_DIR}/licenses"
                VERBATIM
            )
        endif()
        set(W11WIDGET_INSTALLER_DIR "${CMAKE_BINARY_DIR}/installer")
        set(W11WIDGET_PAYLOAD_CAB "${W11WIDGET_INSTALLER_DIR}/payload.cab")
        set(W11WIDGET_PAYLOAD_CAB_NATIVE "${W11WIDGET_PAYLOAD_CAB}")
        if(MSVC)
            file(TO_NATIVE_PATH "${W11WIDGET_PAYLOAD_CAB_NATIVE}" W11WIDGET_PAYLOAD_CAB_NATIVE)
        endif()
        configure_file(installer/Bootstrapper.rc.in "${W11WIDGET_INSTALLER_DIR}/Bootstrapper.rc" @ONLY)
        add_custom_target(w11widget_payload_cab
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${W11WIDGET_INSTALLER_DIR}"
            COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
                    -File "${CMAKE_CURRENT_SOURCE_DIR}/scripts/build-payload-cab.ps1"
                    -PackageDirectory "${W11WIDGET_DEPLOY_DIR}"
                    -CabinetFile "${W11WIDGET_PAYLOAD_CAB}"
                    -DefinitionFile "${W11WIDGET_INSTALLER_DIR}/payload.ddf"
            DEPENDS deploy_w11widget "${CMAKE_CURRENT_SOURCE_DIR}/scripts/build-payload-cab.ps1"
            BYPRODUCTS "${W11WIDGET_PAYLOAD_CAB}"
            VERBATIM
        )
        add_executable(w11widget_setup WIN32 EXCLUDE_FROM_ALL installer/Bootstrapper.cpp "${W11WIDGET_INSTALLER_DIR}/Bootstrapper.rc")
        set_source_files_properties("${W11WIDGET_INSTALLER_DIR}/Bootstrapper.rc"
            PROPERTIES OBJECT_DEPENDS "${W11WIDGET_PAYLOAD_CAB}")
        set_target_properties(w11widget_setup PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${W11WIDGET_INSTALLER_DIR}"
            OUTPUT_NAME "w11widget-${PROJECT_VERSION}-installer"
            MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
        )
        target_compile_definitions(w11widget_setup PRIVATE UNICODE _UNICODE "W11WIDGET_VERSION=L\"${PROJECT_VERSION}\"")
        if(MSVC)
            target_link_options(w11widget_setup PRIVATE /MANIFEST:NO)
        elseif(MINGW)
            target_link_options(w11widget_setup PRIVATE -municode -static -static-libgcc -static-libstdc++)
        endif()
        target_link_libraries(w11widget_setup PRIVATE setupapi shell32 ole32)
        add_dependencies(w11widget_setup w11widget_payload_cab)
        add_custom_target(installer_w11widget DEPENDS w11widget_setup)
        add_executable(installer_live_fixture WIN32 EXCLUDE_FROM_ALL tests/installer_live_fixture.cpp)
        set_target_properties(installer_live_fixture PROPERTIES
            OUTPUT_NAME "w11widget"
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/installer-fixture"
            MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
        )
        target_compile_definitions(installer_live_fixture PRIVATE UNICODE _UNICODE NOMINMAX)
        if(MINGW)
            target_link_options(installer_live_fixture PRIVATE -municode -static -static-libgcc -static-libstdc++)
        endif()
        target_link_libraries(installer_live_fixture PRIVATE shell32)
        add_custom_target(verify_installer_w11widget
            COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
                    -File "${CMAKE_CURRENT_SOURCE_DIR}/scripts/test-installer.ps1"
                    -Installer "$<TARGET_FILE:w11widget_setup>"
                    -PackageDirectory "${W11WIDGET_DEPLOY_DIR}"
                    -WorkRoot "${CMAKE_BINARY_DIR}/installer-verification"
                    -Version "${PROJECT_VERSION}"
                    -LiveFixture "$<TARGET_FILE:installer_live_fixture>"
            DEPENDS w11widget_setup installer_live_fixture
            VERBATIM
        )
        add_test(NAME panel_qml_smoke_test COMMAND w11widget --smoke-test)
        add_executable(test_window_behavior
            tests/test_window_behavior.cpp
            src/ui/PanelWindow.h
            src/ui/PanelWindow.cpp
            src/platform/windows/WindowBehavior.cpp
            src/ui/panel.qrc
        )
        set_target_properties(test_window_behavior PROPERTIES AUTOMOC ON AUTORCC ON)
        target_include_directories(test_window_behavior PRIVATE src)
        target_compile_definitions(test_window_behavior PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
        target_link_libraries(test_window_behavior PRIVATE panel_positioner widget_host Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::Widgets user32 dwmapi)
        add_test(NAME panel_window_behavior_test COMMAND test_window_behavior)
        add_executable(test_controller
            tests/test_controller.cpp
            src/app/ApplicationController.h
            src/app/ApplicationController.cpp
            src/ui/PanelWindow.h
            src/ui/PanelWindow.cpp
            src/platform/windows/WindowBehavior.cpp
            src/platform/windows/TaskbarLocator.cpp
            src/ui/panel.qrc
        )
        set_target_properties(test_controller PROPERTIES AUTOMOC ON AUTORCC ON)
        target_include_directories(test_controller PRIVATE src)
        target_compile_definitions(test_controller PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
        target_link_libraries(test_controller PRIVATE panel_positioner panel_settings visibility_policy widget_host widget_controls Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::Widgets user32 shell32 dwmapi)
        add_test(NAME panel_controller_test COMMAND test_controller)
        add_executable(test_monitor_helpers tests/test_monitor_helpers.cpp src/platform/windows/TaskbarLocator.cpp)
        target_include_directories(test_monitor_helpers PRIVATE src)
        target_compile_definitions(test_monitor_helpers PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
        target_link_libraries(test_monitor_helpers PRIVATE user32 shell32)
        add_test(NAME panel_monitor_helpers_test COMMAND test_monitor_helpers)
        get_target_property(_qt_core_dll Qt6::Core IMPORTED_LOCATION_RELEASE)
        get_filename_component(_qt_bin_dir "${_qt_core_dll}" DIRECTORY)
        get_filename_component(_qt_runtime_prefix "${_qt_bin_dir}" DIRECTORY)
        get_filename_component(_qt_library_prefix "${_qt_runtime_prefix}/../.." ABSOLUTE)
        file(TO_NATIVE_PATH "${_qt_bin_dir}" _qt_native_bin)
        file(TO_NATIVE_PATH "${_qt_library_prefix}/bin" _qt_dependency_bin)
        set(_qt_test_path "${_qt_native_bin};${_qt_dependency_bin};$ENV{PATH}")
        string(REPLACE ";" "\\;" _qt_test_path "${_qt_test_path}")
        set_tests_properties(panel_application_instance_test panel_qml_smoke_test panel_window_behavior_test panel_controller_test ${_portable_qt_tests} PROPERTIES TIMEOUT 20 ENVIRONMENT
            "PATH=${_qt_test_path};QT_PLUGIN_PATH=${_qt_runtime_prefix}/plugins;QML_IMPORT_PATH=${_qt_runtime_prefix}/qml;QT_QPA_PLATFORM=windows;QT_QUICK_BACKEND=software")
    else()
        message(STATUS "Windows integration tests and installer: SKIP (requires Windows; portable Qt tests remain enabled)")
    endif()
endif()
# Assertions must remain effective in Release tests, including future assert-based fixtures.
get_property(_w11widget_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
foreach(_target IN LISTS _w11widget_targets)
    get_target_property(_target_type ${_target} TYPE)
    if(_target_type STREQUAL "EXECUTABLE" OR _target_type STREQUAL "STATIC_LIBRARY")
        if(MSVC)
            target_compile_options(${_target} PRIVATE /W4 /permissive- /utf-8 /Zc:__cplusplus)
        else()
            target_compile_options(${_target} PRIVATE -Wall -Wextra -Wpedantic)
        endif()
        if(_target MATCHES "^test_" OR _target STREQUAL "taskbar_diagnostic")
            if(MSVC)
                target_compile_options(${_target} PRIVATE /UNDEBUG)
            else()
                target_compile_options(${_target} PRIVATE -UNDEBUG)
            endif()
        endif()
    endif()
endforeach()
