cmake_minimum_required(VERSION 3.20)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/../VERSION" "${CMAKE_CURRENT_SOURCE_DIR}/native-required-files.txt")
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/../VERSION" W11WIDGET_VERSION LIMIT_COUNT 1)
project(w11widget_native_installer VERSION "${W11WIDGET_VERSION}" LANGUAGES CXX RC)
if(NOT WIN32 OR NOT MSVC)
    message(FATAL_ERROR "The native installer requires Windows and the MSVC toolchain.")
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(W11WIDGET_PACKAGE_DIRECTORY "" CACHE PATH "Verified native runtime package directory")
if(NOT EXISTS "${W11WIDGET_PACKAGE_DIRECTORY}/w11widget.exe" OR
   NOT EXISTS "${W11WIDGET_PACKAGE_DIRECTORY}/package-manifest.json")
    message(FATAL_ERROR "Set W11WIDGET_PACKAGE_DIRECTORY to a completed native application package.")
endif()
set(_payload "${CMAKE_CURRENT_BINARY_DIR}/payload.cab")
file(GLOB_RECURSE _payload_files CONFIGURE_DEPENDS LIST_DIRECTORIES false "${W11WIDGET_PACKAGE_DIRECTORY}/*")
add_custom_command(OUTPUT "${_payload}"
    COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
        -File "${CMAKE_CURRENT_SOURCE_DIR}/../scripts/build-payload-cab.ps1"
        -PackageDirectory "${W11WIDGET_PACKAGE_DIRECTORY}"
        -CabinetFile "${_payload}"
        -DefinitionFile "${CMAKE_CURRENT_BINARY_DIR}/payload.ddf"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/../scripts/build-payload-cab.ps1"
        ${_payload_files}
    VERBATIM)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/Bootstrapper.rc"
    "1 PAYLOAD \"${_payload}\"\n1 24 \"${CMAKE_CURRENT_SOURCE_DIR}/Bootstrapper.manifest\"\n")
set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/Bootstrapper.rc" PROPERTIES OBJECT_DEPENDS "${_payload}")
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/native-required-files.txt" _native_required_files)
set(_native_header "#pragma once\ninline constexpr const wchar_t* NativePackageFiles[] = {\n")
foreach(_file IN LISTS _native_required_files)
    if(NOT _file MATCHES "^[A-Za-z0-9_.-]+$")
        message(FATAL_ERROR "Invalid native runtime filename in shared package contract: ${_file}")
    endif()
    string(APPEND _native_header "    L\"${_file}\",\n")
endforeach()
string(APPEND _native_header "};\n")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/NativePackageFiles.h" "${_native_header}")
add_executable(w11widget_setup WIN32 Bootstrapper.cpp "${CMAKE_CURRENT_BINARY_DIR}/Bootstrapper.rc" "${_payload}")
target_include_directories(w11widget_setup PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_compile_definitions(w11widget_setup PRIVATE UNICODE _UNICODE W11WIDGET_NATIVE_PACKAGE
    "W11WIDGET_VERSION=L\"${PROJECT_VERSION}\"")
target_compile_options(w11widget_setup PRIVATE /W4 /utf-8 /permissive-)
target_link_options(w11widget_setup PRIVATE /MANIFEST:NO)
target_link_libraries(w11widget_setup PRIVATE setupapi shell32 ole32)
set_target_properties(w11widget_setup PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded"
    OUTPUT_NAME "w11widget-${PROJECT_VERSION}-installer" RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/bin")
add_executable(installer_live_fixture WIN32 ../tests/installer_live_fixture.cpp)
target_compile_definitions(installer_live_fixture PRIVATE UNICODE _UNICODE NOMINMAX)
target_compile_options(installer_live_fixture PRIVATE /W4 /utf-8 /permissive-)
target_link_libraries(installer_live_fixture PRIVATE shell32)
set_target_properties(installer_live_fixture PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded"
    OUTPUT_NAME "w11widget" RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/fixture")
