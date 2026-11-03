cmake_minimum_required(VERSION 3.16)

# The version lives here and in include/divejson.h's DIVEJSON_VERSION; a test holds them equal.
project(divejson VERSION 0.1.0 LANGUAGES C)

option(DIVEJSON_LIBDIVECOMPUTER "Build the libdivecomputer reader (the library links no libdivecomputer)" OFF)
option(DIVEJSON_CLI "Build divejson-raw, compiling the vendored libdivecomputer into it" ON)
option(DIVEJSON_TESTS "Build the test suite" ON)
option(DIVEJSON_FUZZ "Build the libFuzzer target (clang)" OFF)
option(DIVEJSON_STATIC "Link divejson-raw statically" OFF)
set(DIVEJSON_LIBDIVECOMPUTER_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/vendor/libdivecomputer/include"
    CACHE PATH "libdivecomputer's headers, which the reader compiles against")

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
  set(DIVEJSON_WARNINGS -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wno-sign-conversion)
endif()

add_library(divejson STATIC src/dive.c src/identity.c src/json.c)
target_include_directories(divejson PUBLIC include)
target_compile_options(divejson PRIVATE ${DIVEJSON_WARNINGS})
if(NOT MSVC)
  target_link_libraries(divejson PUBLIC m)
endif()

if(DIVEJSON_LIBDIVECOMPUTER)
  # The reader compiles against libdivecomputer's headers and leaves every dc_* symbol for
  # whatever links the library to supply.
  target_sources(divejson PRIVATE src/libdivecomputer.c)
  target_include_directories(divejson PRIVATE "${DIVEJSON_LIBDIVECOMPUTER_INCLUDE_DIR}")
endif()

set(LIBDC "${CMAKE_CURRENT_SOURCE_DIR}/vendor/libdivecomputer")

if(DIVEJSON_LIBDIVECOMPUTER AND (DIVEJSON_CLI OR DIVEJSON_TESTS OR DIVEJSON_FUZZ))
  if(NOT EXISTS "${LIBDC}/src/parser.c")
    message(FATAL_ERROR "vendor/libdivecomputer is empty: run git submodule update --init")
  endif()

  # The vendored libdivecomputer, configured as libdc-swift's Clibdivecomputer target
  # configures it: every source but the Windows serial one, HAVE_PTHREAD_H and
  # ENABLE_LOGGING, and no config.h, so every transport compiles to its stub. glibc's
  # TIOCGSERIAL needs <linux/serial.h>, which serial_posix.c includes only when told to.
  file(GLOB LIBDC_SOURCES "${LIBDC}/src/*.c" "${LIBDC}/src/heatshrink/*.c")
  list(REMOVE_ITEM LIBDC_SOURCES "${LIBDC}/src/serial_win32.c")
  add_library(libdivecomputer_vendored STATIC ${LIBDC_SOURCES})
  target_include_directories(libdivecomputer_vendored
    PUBLIC "${LIBDC}/include"
    PRIVATE "${LIBDC}/include/libdivecomputer" "${LIBDC}/src")
  target_compile_definitions(libdivecomputer_vendored PRIVATE HAVE_PTHREAD_H ENABLE_LOGGING)
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_compile_definitions(libdivecomputer_vendored PRIVATE HAVE_LINUX_SERIAL_H)
  endif()
  set_target_properties(libdivecomputer_vendored PROPERTIES C_EXTENSIONS ON C_STANDARD 99)
  find_package(Threads REQUIRED)
  target_link_libraries(libdivecomputer_vendored PUBLIC Threads::Threads)
  if(NOT MSVC)
    target_link_libraries(libdivecomputer_vendored PUBLIC m)
  endif()

  # The commit, short, for --version. LIBDIVECOMPUTER_COMMIT is the file a reader of the
  # repository finds it in; CI holds it equal to the submodule's.
  file(READ "${CMAKE_CURRENT_SOURCE_DIR}/LIBDIVECOMPUTER_COMMIT" LIBDC_COMMIT)
  string(STRIP "${LIBDC_COMMIT}" LIBDC_COMMIT)
  string(SUBSTRING "${LIBDC_COMMIT}" 0 7 LIBDC_COMMIT_SHORT)
endif()

if(DIVEJSON_LIBDIVECOMPUTER AND DIVEJSON_CLI)
  add_executable(divejson-raw cli/divejson-raw.c)
  target_compile_options(divejson-raw PRIVATE ${DIVEJSON_WARNINGS})
  target_compile_definitions(divejson-raw PRIVATE DIVEJSON_LIBDIVECOMPUTER_COMMIT="${LIBDC_COMMIT_SHORT}")
  target_link_libraries(divejson-raw PRIVATE divejson libdivecomputer_vendored)
  if(DIVEJSON_STATIC)
    target_link_options(divejson-raw PRIVATE -static)
  endif()
  install(TARGETS divejson-raw RUNTIME DESTINATION bin)
endif()

if(DIVEJSON_TESTS)
  enable_testing()
  add_executable(test_core tests/test_core.c)
  target_link_libraries(test_core PRIVATE divejson)
  target_include_directories(test_core PRIVATE src)
  target_compile_definitions(test_core PRIVATE DIVEJSON_PROJECT_VERSION="${PROJECT_VERSION}")
  add_test(NAME core COMMAND test_core)

  if(DIVEJSON_LIBDIVECOMPUTER)
    add_executable(test_libdivecomputer tests/test_libdivecomputer.c)
    target_link_libraries(test_libdivecomputer PRIVATE divejson libdivecomputer_vendored)
    target_compile_definitions(test_libdivecomputer PRIVATE
      DIVEJSON_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/libdivecomputer")
    add_test(NAME libdivecomputer COMMAND test_libdivecomputer)

    if(DIVEJSON_CLI)
      find_package(Python3 COMPONENTS Interpreter REQUIRED)
      add_test(NAME cli
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_cli.py"
                "$<TARGET_FILE:divejson-raw>")
    endif()
  endif()
endif()

if(DIVEJSON_FUZZ)
  if(NOT DIVEJSON_LIBDIVECOMPUTER)
    message(FATAL_ERROR "the fuzz target reads through the libdivecomputer reader")
  endif()
  add_executable(fuzz_libdivecomputer fuzz/fuzz_libdivecomputer.c)
  target_compile_options(fuzz_libdivecomputer PRIVATE -fsanitize=fuzzer)
  target_link_options(fuzz_libdivecomputer PRIVATE -fsanitize=fuzzer)
  target_link_libraries(fuzz_libdivecomputer PRIVATE divejson libdivecomputer_vendored)
endif()
