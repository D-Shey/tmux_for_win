cmake_minimum_required(VERSION 3.20)
project(tmux-win C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Windows 10 1809+ for ConPTY
add_definitions(-D_WIN32_WINNT=0x0A00 -DWINVER=0x0A00)
add_definitions(-DUNICODE -D_UNICODE)
add_definitions(-D_CRT_SECURE_NO_WARNINGS)
add_definitions(-DTMUX_VERSION="3.3a")

set(PLATFORM_SOURCES
    src/platform/win32_conpty.c
    src/platform/win32_pipe.c
    src/platform/win32_console.c
    src/platform/win32_proc.c
)

set(CORE_SOURCES
    src/xmalloc.c
    src/log.c
    src/utf8.c
    src/grid.c
    src/input.c
    src/screen.c
    src/colour.c
)

set(SESSION_SOURCES
    src/session.c
    src/window.c
    src/pane.c
    src/layout.c
)

set(ENGINE_SOURCES
    src/server.c
    src/client.c
    src/cmd.c
    src/key.c
    src/options.c
    src/cfg.c
    src/copy_mode.c
)

set(ALL_SOURCES
    src/main.c
    ${PLATFORM_SOURCES}
    ${CORE_SOURCES}
    ${SESSION_SOURCES}
    ${ENGINE_SOURCES}
)

add_executable(tmux ${ALL_SOURCES})

target_include_directories(tmux PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(tmux PRIVATE
    ws2_32
    kernel32
    user32
    advapi32
)

# Install
install(TARGETS tmux RUNTIME DESTINATION bin)
