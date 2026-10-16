cmake_minimum_required(VERSION 3.20...4.4)

if(NOT EXISTS "${PACKAGE_DIR}/w11widget.exe" OR NOT EXISTS "${DUMPBIN}")
    message(FATAL_ERROR "Runtime deployment requires the application and MSVC dumpbin.")
endif()

# Conda Qt uses external libraries that windeployqt does not collect. Resolve
# imports of the EXE, Qt libraries and all deployed plugins recursively.
# Prefer the runtime supplied with this Qt build; official Qt can use the
# compiler's redistributable folder instead. Never copy Windows system DLLs.
file(GLOB _crt_files "${QT_RUNTIME_DIR}/msvcp140*.dll"
     "${QT_RUNTIME_DIR}/vcruntime140*.dll" "${QT_RUNTIME_DIR}/concrt140.dll")
if(NOT _crt_files)
    file(GLOB _crt_files "${MSVC_RUNTIME_DIR}/msvcp140*.dll"
         "${MSVC_RUNTIME_DIR}/vcruntime140*.dll" "${MSVC_RUNTIME_DIR}/concrt140.dll")
endif()
if(NOT _crt_files)
    message(FATAL_ERROR "No app-local MSVC runtime found for the standalone package.")
endif()
file(COPY ${_crt_files} DESTINATION "${PACKAGE_DIR}")

file(GLOB _libraries "${PACKAGE_DIR}/*.dll")
file(GLOB_RECURSE _modules "${PACKAGE_DIR}/*.dll")
list(REMOVE_ITEM _modules ${_libraries})
set(CMAKE_GET_RUNTIME_DEPENDENCIES_PLATFORM "windows+pe")
set(CMAKE_GET_RUNTIME_DEPENDENCIES_TOOL "dumpbin")
set(CMAKE_GET_RUNTIME_DEPENDENCIES_COMMAND "${DUMPBIN}")
file(GET_RUNTIME_DEPENDENCIES
    EXECUTABLES "${PACKAGE_DIR}/w11widget.exe"
    LIBRARIES ${_libraries}
    MODULES ${_modules}
    DIRECTORIES "${PACKAGE_DIR}" "${QT_RUNTIME_DIR}" "${QT_BIN_DIR}"
    PRE_EXCLUDE_REGEXES "^api-ms-" "^ext-ms-"
    POST_EXCLUDE_REGEXES ".*[/\\][Ss][Yy][Ss][Tt][Ee][Mm]32[/\\].*"
                         "^[A-Za-z]:[/\\][Ww][Ii][Nn][Dd][Oo][Ww][Ss][/\\].*"
    RESOLVED_DEPENDENCIES_VAR _resolved
    UNRESOLVED_DEPENDENCIES_VAR _unresolved
    CONFLICTING_DEPENDENCIES_PREFIX _conflicts
)
if(_unresolved)
    message(FATAL_ERROR "Standalone runtime dependencies are unresolved: ${_unresolved}")
endif()
# The same import can resolve to both a staged copy and its Qt source. Keep
# windeployqt's staged libraries (QtCore may be patched by that tool). Conflicting
# sources for a genuinely missing library must contain identical bytes.
foreach(_name IN LISTS _conflicts_FILENAMES)
    if(EXISTS "${PACKAGE_DIR}/${_name}")
        continue()
    endif()
    set(_candidates "${_conflicts_${_name}}")
    list(GET _candidates 0 _source)
    file(SHA256 "${_source}" _expected_hash)
    foreach(_candidate IN LISTS _candidates)
        file(SHA256 "${_candidate}" _candidate_hash)
        if(NOT _candidate_hash STREQUAL _expected_hash)
            message(FATAL_ERROR "Conflicting runtime versions for ${_name}: ${_candidates}")
        endif()
    endforeach()
    list(APPEND _resolved "${_source}")
endforeach()
set(_copied 0)
foreach(_dependency IN LISTS _resolved)
    get_filename_component(_name "${_dependency}" NAME)
    if(NOT EXISTS "${PACKAGE_DIR}/${_name}")
        file(COPY "${_dependency}" DESTINATION "${PACKAGE_DIR}")
        math(EXPR _copied "${_copied} + 1")
        message(STATUS "Additional runtime: ${_name}")
    endif()
endforeach()
message(STATUS "Standalone runtime imports resolved; added ${_copied} libraries and app-local MSVC runtime.")
