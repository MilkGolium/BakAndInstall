cmake_minimum_required(VERSION 3.10)
project(Project1 C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_executable(Project1
    main.c
    init.c
    dbAPI.c
    deploy.c
    test.c
    sqlite/sqlite3.c
)

if(APPLE)
    target_sources(Project1 PRIVATE
        platform/macOS/openURL.c
        platform/macOS/sysUtil.c)
elseif(WIN32)
    target_sources(Project1 PRIVATE
        platform/Windows/openURL.c
        platform/Windows/sysUtil.c)
else()
    target_sources(Project1 PRIVATE
        platform/Linux/openURL.c
        platform/Linux/sysUtil.c)
endif()

if(WIN32)
    target_link_libraries(Project1 PRIVATE urlmon)
endif()

target_include_directories(Project1 PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)
