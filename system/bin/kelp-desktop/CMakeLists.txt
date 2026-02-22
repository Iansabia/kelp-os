# kelp-linux :: kelp-desktop — Graphical desktop shell
# SPDX-License-Identifier: MIT

# ---- sources -------------------------------------------------------------

set(KELP_DESKTOP_SOURCES
    main.c
    desktop.c
    render.c
    theme.c
    topbar.c
    dock.c
    chat.c
    terminal.c
    monitor.c
    files.c
    cursor.c
    ai_control.c
    animation.c
)

# ---- executable -----------------------------------------------------------

add_executable(kelp-desktop ${KELP_DESKTOP_SOURCES})

target_compile_options(kelp-desktop PRIVATE
    -Wall -Wextra -Wpedantic -Wno-unused-parameter
)

set_target_properties(kelp-desktop PROPERTIES
    C_STANDARD          17
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS        OFF
    OUTPUT_NAME         kelp-desktop
)

# ---- dependencies --------------------------------------------------------

target_link_libraries(kelp-desktop PRIVATE
    kelp-core
    kelp-config
    kelp-terminal
    kelp-process
    kelp-kernel
    PkgConfig::SDL2
    PkgConfig::CAIRO
    PkgConfig::PANGOCAIRO
    Threads::Threads
)

# cJSON
if(CJSON_FOUND)
    target_link_libraries(kelp-desktop PRIVATE PkgConfig::CJSON)
endif()

# Math library
target_link_libraries(kelp-desktop PRIVATE m)

# ---- install -------------------------------------------------------------

include(GNUInstallDirs)
install(TARGETS kelp-desktop RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
