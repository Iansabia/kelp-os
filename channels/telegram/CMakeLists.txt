# clawd-linux :: clawd-channel-telegram
# SPDX-License-Identifier: MIT

add_executable(clawd-channel-telegram src/main.c)

target_link_libraries(clawd-channel-telegram
    PRIVATE
        clawd-core
        clawd-net
        clawd-config
        Threads::Threads
)

if(CJSON_FOUND)
    target_link_libraries(clawd-channel-telegram PRIVATE PkgConfig::CJSON)
endif()

target_compile_options(clawd-channel-telegram PRIVATE
    -Wall -Wextra -Wpedantic -Wno-unused-parameter
)

install(TARGETS clawd-channel-telegram RUNTIME DESTINATION bin)
