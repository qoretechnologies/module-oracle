
cmake_minimum_required(VERSION 3.5...3.31)

project(qore-oracle-module VERSION 3.4.1)

option(enable-scu "build as a single compilation unit" ON)

include(CheckCXXCompilerFlag)
include(CheckCXXSourceCompiles)
include(CheckCXXSymbolExists)
include(CheckIncludeFileCXX)

# Check for C++11.
CHECK_CXX_COMPILER_FLAG("-std=c++11" COMPILER_SUPPORTS_CXX11)
if(COMPILER_SUPPORTS_CXX11)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")
else()
    message(FATAL_ERROR "The compiler ${CMAKE_CXX_COMPILER} has no C++11 support. Please use a different C++ compiler.")
endif()

find_package(Qore 1.0 REQUIRED)
qore_find_pthreads()

set(CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)

if (DEFINED ENV{ORACLE_INCLUDES})
    set(ORACLE_INCLUDES $ENV{ORACLE_INCLUDES})
endif()

find_package(OracleClient REQUIRED)

set(CMAKE_REQUIRED_INCLUDES ${ORACLE_INCLUDES})
set(CMAKE_REQUIRED_LIBRARIES ${ORACLE_LIBRARY_CLNTSH})
if (NOT APPLE)
    # the checks link a test executable against the client library only to learn whether it defines the
    # function; the Instant Client is built against glibc, so on musl (Alpine) the executable link fails on the
    # library's own glibc-versioned references, which gcompat resolves at runtime, and every check would fail
    set(CMAKE_REQUIRED_LINK_OPTIONS "-Wl,--allow-shlib-undefined")
endif()
check_symbol_exists(OCIClientVersion ociap.h HAVE_OCICLIENTVERSION)
check_symbol_exists(OCILobWrite2 ociap.h HAVE_OCILOBWRITE2)
unset(CMAKE_REQUIRED_INCLUDES)
unset(CMAKE_REQUIRED_LIBRARIES)
unset(CMAKE_REQUIRED_LINK_OPTIONS)

include_directories(${CMAKE_SOURCE_DIR}/src)
include_directories(${CMAKE_SOURCE_DIR}/src/ocilib)
include_directories(${ORACLE_INCLUDES})
add_definitions(-DQORE_MODULE_PACKAGE_VERSION="${PROJECT_VERSION}")

check_include_file_cxx(dlfcn.h HAVE_DLFCN_H)

check_cxx_compiler_flag(-fvisibility=hidden HAVE_GCC_VISIBILITY)

configure_file(${CMAKE_SOURCE_DIR}/cmake/config.h.cmake
               ${CMAKE_BINARY_DIR}/config.h)

set(QPP_SRC src/QC_AQMessage.qpp
    src/QC_AQQueue.qpp
    src/ql_oracle.qpp
)

set(CPP_SRC
    src/oracle.cpp
    src/oracleobject.cpp
    src/OraColumnValue.cpp
    src/OraResultSet.cpp
    src/QoreOracleConnection.cpp
    src/QoreOracleBulkLoad.cpp
    src/QoreOracleStatement.cpp
    src/QorePreparedStatement.cpp

    src/ocilib/agent.cpp
    src/ocilib/callback.cpp
    src/ocilib/collection.cpp
    src/ocilib/column.cpp
    src/ocilib/connection.cpp
    src/ocilib/date.cpp
    src/ocilib/dequeue.cpp
    src/ocilib/element.cpp
    src/ocilib/enqueue.cpp
    src/ocilib/error.cpp
    src/ocilib/exception.cpp
    src/ocilib/file.cpp
    src/ocilib/hash.cpp
    src/ocilib/interval.cpp
    src/ocilib/library.cpp
    src/ocilib/list.cpp
    src/ocilib/lob.cpp
    src/ocilib/long.cpp
    src/ocilib/memory.cpp
    src/ocilib/msg.cpp
    src/ocilib/mutex.cpp
    src/ocilib/number.cpp
    src/ocilib/object.cpp
    src/ocilib/ref.cpp
    src/ocilib/string.cpp
    src/ocilib/threadkey.cpp
    src/ocilib/timestamp.cpp
    src/ocilib/typeinfo.cpp
)

set(QMOD
    qlib/OracleExtensions.qm
)

qore_wrap_qpp_value(QPP_SOURCES DOXLIST QPP_DOX_SRC METALIST QPP_META_SRC ${QPP_SRC})
add_custom_target(QPP_GENERATED_FILES DEPENDS ${QPP_SOURCES})

set(module_name "oracle")

set(QORE_DOX_TMPL_SRC
    docs/mainpage.doxygen.tmpl
    docs/guide-aq.dox.tmpl
    docs/guide-bindings_and_types.dox.tmpl
    docs/guide-named_types.dox.tmpl
    docs/guide-ora_known_issues.dox.tmpl
    docs/guide-ora_native_bulk_load.dox.tmpl
    docs/guide-ora_options.dox.tmpl
    docs/guide-ora_releasenotes.dox.tmpl
    docs/guide-ora_thread_session_info.dox.tmpl
    docs/guide-select_with_dup_col.dox.tmpl
)

if(enable-scu)
    add_library(${module_name} MODULE src/single-compilation-unit.cpp)
    add_dependencies(${module_name} QPP_GENERATED_FILES)
else(enable-scu)
    add_library(${module_name} MODULE ${CPP_SRC} ${QPP_SOURCES})
endif(enable-scu)

if (WIN32 AND MINGW AND MSYS)
    target_compile_definitions(${module_name} PUBLIC BUILDING_DLL)
endif (WIN32 AND MINGW AND MSYS)

if (DEFINED ENV{DOXYGEN_EXECUTABLE})
    set(DOXYGEN_EXECUTABLE $ENV{DOXYGEN_EXECUTABLE})
endif()

# Document the public Qore declarations, not generated C++ implementation files.
set(MODULE_DOX_INPUT ${CMAKE_BINARY_DIR}/mainpage.dox ${QPP_DOX_SRC})
list(APPEND MODULE_DOX_INPUT
    ${CMAKE_CURRENT_BINARY_DIR}/guide-aq.dox
    ${CMAKE_CURRENT_BINARY_DIR}/guide-bindings_and_types.dox
    ${CMAKE_CURRENT_BINARY_DIR}/guide-named_types.dox
    ${CMAKE_CURRENT_BINARY_DIR}/guide-ora_known_issues.dox
    ${CMAKE_CURRENT_BINARY_DIR}/guide-ora_native_bulk_load.dox
    ${CMAKE_CURRENT_BINARY_DIR}/guide-ora_options.dox
    ${CMAKE_CURRENT_BINARY_DIR}/guide-ora_releasenotes.dox
    ${CMAKE_CURRENT_BINARY_DIR}/guide-ora_thread_session_info.dox
    ${CMAKE_CURRENT_BINARY_DIR}/guide-select_with_dup_col.dox
)
qore_external_binary_module(${module_name} ${PROJECT_VERSION} ${ORACLE_LIBRARY_CLNTSH} Threads::Threads)

qore_external_user_module("qlib/OracleExtensions.qm" "")

qore_dist(${PROJECT_VERSION})

# install qpp metadata files
if (COMMAND qore_install_qpp_metadata)
    qore_install_qpp_metadata(${module_name} ${QPP_META_SRC})
else()
    install(FILES ${QPP_META_SRC}
            DESTINATION ${CMAKE_INSTALL_DATADIR}/qore/metadata)
endif()

qore_config_info()

if (DOXYGEN_FOUND)
    qore_wrap_dox(QORE_DOX_SRC ${QORE_DOX_TMPL_SRC})
    add_custom_target(QORE_MOD_DOX_FILES DEPENDS ${QORE_DOX_SRC})
    add_dependencies(docs-module QORE_MOD_DOX_FILES)
endif()

# Resolve links between the native API and all companion user modules.
if (DOXYGEN_FOUND)
    qore_binary_module_two_phase_docs(${module_name} "OracleExtensions")
endif()
