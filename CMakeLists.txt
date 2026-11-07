cmake_minimum_required(VERSION 3.15)
project(tracex VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(WIN32)
    add_compile_definitions(_WIN32_WINNT=0x0A00 WIN32_LEAN_AND_MEAN)
endif()

if(MSVC)
    add_compile_options(/W4 /EHsc /utf-8)
else()
    add_compile_options(-Wall -Wextra -Wno-unused-parameter)
endif()

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/backend/include
    ${CMAKE_CURRENT_SOURCE_DIR}/backend
)

# Shared core sources
set(TRACEX_CORE_SOURCES
    backend/src/Classifier.cpp
    backend/src/LogParser.cpp
    backend/src/AnalysisStrategy.cpp
    backend/src/Analyzer.cpp
    backend/src/Database.cpp
    backend/src/Report.cpp
    backend/third_party/sqlite3.c
)

# Common libraries needed (especially ws2_32 on Windows for httplib)
set(PLATFORM_LIBS "")
if(WIN32)
    set(PLATFORM_LIBS ws2_32 wsock32)
    if(MINGW)
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static-libgcc -static-libstdc++ -static")
    endif()
endif()

# 1. CLI Executable
add_executable(tracex
    backend/src/main.cpp
    ${TRACEX_CORE_SOURCES}
)
target_link_libraries(tracex PRIVATE ${PLATFORM_LIBS})

# 2. Server Executable
add_executable(tracex_server
    backend/src/server.cpp
    ${TRACEX_CORE_SOURCES}
)
target_link_libraries(tracex_server PRIVATE ${PLATFORM_LIBS})

# 3. Unit Tests Executable
add_executable(tracex_tests
    backend/src/tests.cpp
    ${TRACEX_CORE_SOURCES}
)
target_link_libraries(tracex_tests PRIVATE ${PLATFORM_LIBS})

enable_testing()
add_test(NAME UnitTests COMMAND tracex_tests)
