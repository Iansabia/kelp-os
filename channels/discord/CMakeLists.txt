# clawd-linux :: clawd-channel-discord
# SPDX-License-Identifier: MIT

add_executable(clawd-channel-discord src/main.c)

target_link_libraries(clawd-channel-discord
    PRIVATE
        clawd-core
        clawd-net
        clawd-config
        OpenSSL::SSL
        OpenSSL::Crypto
        Threads::Threads
)

if(CJSON_FOUND)
    target_link_libraries(clawd-channel-discord PRIVATE PkgConfig::CJSON)
endif()

target_compile_options(clawd-channel-discord PRIVATE
    -Wall -Wextra -Wpedantic -Wno-unused-parameter
)

install(TARGETS clawd-channel-discord RUNTIME DESTINATION bin)
