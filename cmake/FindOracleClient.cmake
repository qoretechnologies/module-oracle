# Copyright (C) 2026 David Nichols
# SPDX-License-Identifier: MIT
# Locate the OCI headers and shared client used by this module. Supports an
# Instant Client SDK or a database home, as well as explicit include/lib paths.
# ORACLE_INCLUDES and ORACLE_LIBRARY_CLNTSH remain the public result variables.

set(_oracle_roots)
foreach(_oracle_hint ORACLE_INSTANT_CLIENT ORACLE_HOME)
    if(DEFINED ${_oracle_hint})
        list(APPEND _oracle_roots "${${_oracle_hint}}")
    endif()
    if(DEFINED ENV{${_oracle_hint}})
        file(TO_CMAKE_PATH "$ENV{${_oracle_hint}}" _oracle_env)
        list(APPEND _oracle_roots "${_oracle_env}")
    endif()
endforeach()

if(ORACLE_PATH_INCLUDES)
    find_path(ORACLE_INCLUDES NAMES oci.h
        PATHS "${ORACLE_PATH_INCLUDES}" NO_DEFAULT_PATH)
else()
    find_path(ORACLE_INCLUDES NAMES oci.h HINTS ${_oracle_roots}
        PATH_SUFFIXES sdk/include rdbms/public include oci/include OCI/include)
endif()
if(ORACLE_PATH_LIB)
    find_library(ORACLE_LIBRARY_CLNTSH NAMES clntsh oci
        PATHS "${ORACLE_PATH_LIB}" NO_DEFAULT_PATH)
else()
    find_library(ORACLE_LIBRARY_CLNTSH NAMES clntsh oci HINTS ${_oracle_roots}
        PATH_SUFFIXES lib lib64 sdk/lib/msvc oci/lib/msvc OCI/lib/MSVC)
endif()

# Reject invalid cached or environment-provided paths at configure time.
if(NOT EXISTS "${ORACLE_INCLUDES}/oci.h")
    set(ORACLE_INCLUDES "ORACLE_INCLUDES-NOTFOUND")
endif()
if(NOT EXISTS "${ORACLE_LIBRARY_CLNTSH}")
    set(ORACLE_LIBRARY_CLNTSH "ORACLE_LIBRARY_CLNTSH-NOTFOUND")
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(OracleClient REQUIRED_VARS
    ORACLE_INCLUDES ORACLE_LIBRARY_CLNTSH)
set(ORACLE_FOUND "${OracleClient_FOUND}")
set(ORACLE_LIBRARIES "${ORACLE_LIBRARY_CLNTSH}")
mark_as_advanced(ORACLE_INCLUDES ORACLE_LIBRARY_CLNTSH)
unset(_oracle_roots)
unset(_oracle_hint)
unset(_oracle_env)
