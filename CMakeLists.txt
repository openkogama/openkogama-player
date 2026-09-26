cmake_minimum_required(VERSION 3.20)

project(openkogama-player LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(SOURCES
    src/host.cpp
    src/log.cpp
    src/options.cpp
    src/script.cpp
    src/url.cpp
)

if(WIN32)
    if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
        message(FATAL_ERROR "Build the Windows player as x86, Unity Web Player for Windows is 32-bit")
    endif()

    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    add_executable(openkogama-player ${SOURCES} src/win/main.cpp src/win/platform_win.cpp src/win/unity.cpp)
    target_compile_definitions(openkogama-player PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(openkogama-player PRIVATE wininet shell32 ole32 advapi32 user32 gdi32)
    set_target_properties(openkogama-player PROPERTIES WIN32_EXECUTABLE TRUE)
    if(MSVC)
        target_compile_options(openkogama-player PRIVATE /utf-8 /W4)
        target_link_options(openkogama-player PRIVATE /ENTRY:mainCRTStartup)
    endif()
    add_custom_command(TARGET openkogama-player POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/versions $<TARGET_FILE_DIR:openkogama-player>/versions)
elseif(APPLE)
    message(FATAL_ERROR "The macOS host is not written yet")
else()
    message(FATAL_ERROR "Unity Web Player only exists for Windows and macOS, run the Windows build through Wine")
endif()
