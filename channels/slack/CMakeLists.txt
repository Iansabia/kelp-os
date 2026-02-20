# clawd-linux :: clawd-channel-slack
# SPDX-License-Identifier: MIT

add_executable(clawd-channel-slack src/main.c)

target_link_libraries(clawd-channel-slack
    PRIVATE
        clawd-core
        clawd-net
        clawd-config
        OpenSSL::SSL
        OpenSSL::Crypto
        Threads::Threads
)

if(CJSON_FOUND)
    target_link_libraries(clawd-channel-slack PRIVATE PkgConfig::CJSON)
endif()

target_compile_options(clawd-channel-slack PRIVATE
    -Wall -Wextra -Wpedantic -Wno-unused-parameter
)

install(TARGETS clawd-channel-slack RUNTIME DESTINATION bin)
