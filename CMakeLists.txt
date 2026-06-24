cmake_minimum_required(VERSION 3.15)
project(bloomc)

# Set C++17 as the minimum standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Disable exceptions
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-exceptions")

# Treat warnings as errors
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Werror")

add_executable(bloomc
    src/allocation.cpp
    src/error-reporting.cpp
    src/parsing.cpp
    src/parse_declarations.cpp
    src/parse_expressions.cpp
    src/parse_statements.cpp
    src/string.cpp
    src/tokenization.cpp
    src/transpilation.cpp
    src/main.cpp
)

target_include_directories(bloomc
    PRIVATE
        include
)

# MinGW-w64 cross-compiler for Windows targets — downloaded locally, no system install needed
set(MINGW_VERSION "13.2.0-1")
set(MINGW_TOOLCHAIN_DIR "${CMAKE_BINARY_DIR}/toolchains/mingw")
set(MINGW_COMPILER_PATH "${MINGW_TOOLCHAIN_DIR}/bin/x86_64-w64-mingw32-gcc")

if(NOT EXISTS "${MINGW_COMPILER_PATH}")
    set(MINGW_ARCHIVE_URL
        "https://github.com/xpack-dev-tools/mingw-w64-gcc-xpack/releases/download/v${MINGW_VERSION}/xpack-mingw-w64-gcc-${MINGW_VERSION}-linux-x64.tar.gz"
    )
    set(MINGW_ARCHIVE_PATH "${CMAKE_BINARY_DIR}/toolchains/mingw-toolchain.tar.gz")

    message(STATUS "Downloading MinGW-w64 GCC ${MINGW_VERSION} cross-compiler...")
    file(DOWNLOAD "${MINGW_ARCHIVE_URL}" "${MINGW_ARCHIVE_PATH}"
        SHOW_PROGRESS
        STATUS MINGW_DOWNLOAD_STATUS
    )
    list(GET MINGW_DOWNLOAD_STATUS 0 MINGW_DOWNLOAD_CODE)
    if(NOT MINGW_DOWNLOAD_CODE EQUAL 0)
        list(GET MINGW_DOWNLOAD_STATUS 1 MINGW_DOWNLOAD_ERROR)
        message(FATAL_ERROR "Failed to download MinGW toolchain: ${MINGW_DOWNLOAD_ERROR}")
    endif()

    file(MAKE_DIRECTORY "${MINGW_TOOLCHAIN_DIR}")
    execute_process(
        COMMAND tar --strip-components=1 -xf "${MINGW_ARCHIVE_PATH}" -C "${MINGW_TOOLCHAIN_DIR}"
        RESULT_VARIABLE MINGW_TAR_RESULT
    )
    if(NOT MINGW_TAR_RESULT EQUAL 0)
        message(FATAL_ERROR "Failed to extract MinGW toolchain")
    endif()
    file(REMOVE "${MINGW_ARCHIVE_PATH}")
    message(STATUS "MinGW-w64 cross-compiler installed to ${MINGW_TOOLCHAIN_DIR}")
endif()

target_compile_definitions(bloomc PRIVATE
    "MINGW_COMPILER_PATH=\"${MINGW_COMPILER_PATH}\""
)