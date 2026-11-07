cmake_minimum_required(VERSION 3.21) # PROJECT_IS_TOP_LEVEL, presets v3

project(ppv
  VERSION 0.1.0
  DESCRIPTION "ppv - portable pipe viewer"
  LANGUAGES C)

include(GNUInstallDirs)
include(CheckCSourceCompiles)

option(PPV_BUILD_TESTS "Build the tests" ${PROJECT_IS_TOP_LEVEL})
option(PPV_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(PPV_STATIC "Link the C runtime statically (MinGW: no DLL dependencies)" ON)
# For coverage builds only: gcov writes its data at exit(), which a death by
# signal skips. With this ON, ppv flushes the data before re-raising the
# signal, so the Ctrl-C paths are measured. Needs gcc/clang with --coverage.
option(PPV_COVERAGE "Flush gcov data before dying by signal" OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Static MSVC runtime. Must be set before the first target is created: the
# variable only initialises the property of targets defined after it, and a
# mix of /MT and /MD fails to link (unresolved __imp_* symbols).
if(MSVC AND PPV_STATIC)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# ---------------------------------------------------------------------------
# Common settings for all targets of this project

add_library(ppv_settings INTERFACE)
if(PPV_COVERAGE)
  target_compile_definitions(ppv_settings INTERFACE PPV_COVERAGE=1)
endif()

if(MSVC)
  target_compile_options(ppv_settings INTERFACE /W4 /utf-8
    $<$<BOOL:${PPV_WARNINGS_AS_ERRORS}>:/WX>)
  target_compile_definitions(ppv_settings INTERFACE _CRT_SECURE_NO_WARNINGS)
else()
  target_compile_options(ppv_settings INTERFACE
    -Wall -Wextra -Wpedantic -Wshadow -Wformat=2 -Wcast-qual -Wwrite-strings
    -Wstrict-prototypes -Wmissing-prototypes -Wundef
    $<$<BOOL:${PPV_WARNINGS_AS_ERRORS}>:-Werror>)
endif()

if(WIN32)
  target_compile_definitions(ppv_settings INTERFACE
    WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0601)
endif()

# 64-bit atomics: some 32-bit targets need libatomic.
if(NOT MSVC)
  set(_ppv_atomic_test [[
    #include <stdatomic.h>
    #include <stdint.h>
    int main(void) { _Atomic uint64_t v = 0; atomic_fetch_add(&v, 1); return (int)atomic_load(&v) - 1; }
  ]])
  check_c_source_compiles("${_ppv_atomic_test}" PPV_HAVE_ATOMIC64)
  if(NOT PPV_HAVE_ATOMIC64)
    set(CMAKE_REQUIRED_LIBRARIES atomic)
    check_c_source_compiles("${_ppv_atomic_test}" PPV_HAVE_ATOMIC64_LIBATOMIC)
    unset(CMAKE_REQUIRED_LIBRARIES)
    if(NOT PPV_HAVE_ATOMIC64_LIBATOMIC)
      message(FATAL_ERROR "64-bit atomics are not available")
    endif()
    target_link_libraries(ppv_settings INTERFACE atomic)
  endif()
endif()

# ---------------------------------------------------------------------------
# Portable core (no OS dependencies, unit tested)

add_library(ppv_core STATIC
  src/display.c
  src/format.c
  src/meter.c
  src/options.c
  src/report.c)
target_compile_definitions(ppv_core PRIVATE PPV_VERSION="${PROJECT_VERSION}")
target_include_directories(ppv_core PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(ppv_core PUBLIC ppv_settings)

# ---------------------------------------------------------------------------
# Platform layer (the only code that talks to the OS)

if(WIN32)
  add_library(ppv_platform STATIC src/platform/win32/platform_win32.c)
else()
  add_library(ppv_platform STATIC src/platform/posix/platform_posix.c)
  find_package(Threads REQUIRED)
  target_link_libraries(ppv_platform PUBLIC Threads::Threads)
endif()
target_link_libraries(ppv_platform PUBLIC ppv_core)

# ---------------------------------------------------------------------------
# Executable

add_executable(ppv
  src/main.c
  src/transfer.c)

target_link_libraries(ppv PRIVATE ppv_platform ppv_core)

if(MINGW AND PPV_STATIC)
  target_link_options(ppv PRIVATE -static)
endif()

install(TARGETS ppv RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES docs/ppv.1 DESTINATION ${CMAKE_INSTALL_MANDIR}/man1)

# ---------------------------------------------------------------------------

if(PPV_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
