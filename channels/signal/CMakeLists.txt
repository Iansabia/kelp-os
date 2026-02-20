# clawd-linux :: clawd-channel-signal
# SPDX-License-Identifier: MIT

add_executable(clawd-channel-signal src/main.c)

target_link_libraries(clawd-channel-signal
    PRIVATE
        clawd-core
        clawd-config
        Threads::Threads
)

if(CJSON_FOUND)
    target_link_libraries(clawd-channel-signal PRIVATE PkgConfig::CJSON)
endif()

target_compile_options(clawd-channel-signal PRIVATE
    -Wall -Wextra -Wpedantic -Wno-unused-parameter
)

install(TARGETS clawd-channel-signal RUNTIME DESTINATION bin)
