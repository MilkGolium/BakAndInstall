cmake_minimum_required(VERSION 3.10)
project(Project1 C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# ---- 生产二进制 ----
add_executable(Project1
    main.c
    init.c
    dbAPI.c
    deploy.c
    tui.c
    sqlite/sqlite3.c
)

# ---- 回归测试二进制 ----
add_executable(run_tests
    test_runner.c
    test.c
    init.c
    dbAPI.c
    deploy.c
    sqlite/sqlite3.c
)

# ---- 平台源码 ----
if(APPLE)
    target_sources(Project1  PRIVATE platform/macOS/openURL.c platform/macOS/sysUtil.c)
    target_sources(run_tests PRIVATE platform/macOS/sysUtil.c)
elseif(WIN32)
    target_sources(Project1  PRIVATE platform/Windows/openURL.c platform/Windows/sysUtil.c)
    target_sources(run_tests PRIVATE platform/Windows/sysUtil.c)
else()
    target_sources(Project1  PRIVATE platform/Linux/openURL.c platform/Linux/sysUtil.c)
    target_sources(run_tests PRIVATE platform/Linux/sysUtil.c)
endif()

if(WIN32)
    target_link_libraries(Project1  PRIVATE urlmon)
    target_link_libraries(run_tests PRIVATE urlmon)
endif()

target_include_directories(Project1  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(run_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
