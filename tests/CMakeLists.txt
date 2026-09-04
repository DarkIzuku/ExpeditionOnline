add_executable(ExpeditionOnlineProtocolTests protocol_tests.cpp)
target_link_libraries(ExpeditionOnlineProtocolTests PRIVATE ExpeditionOnlineProtocol)
if(MSVC)
    target_compile_options(ExpeditionOnlineProtocolTests PRIVATE /W4 /permissive-)
else()
    target_compile_options(ExpeditionOnlineProtocolTests PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_test(NAME protocol_round_trip COMMAND ExpeditionOnlineProtocolTests)

add_executable(ExpeditionOnlineConfigTests
    config_tests.cpp
    ${PROJECT_SOURCE_DIR}/client/src/config.cpp
)
target_include_directories(ExpeditionOnlineConfigTests PRIVATE
    ${PROJECT_SOURCE_DIR}/client/include
)
if(MSVC)
    target_compile_options(ExpeditionOnlineConfigTests PRIVATE /W4 /permissive-)
else()
    target_compile_options(ExpeditionOnlineConfigTests PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_test(NAME client_config_parse COMMAND ExpeditionOnlineConfigTests)
add_test(
    NAME appearance_no_global_scan
    COMMAND ${CMAKE_COMMAND}
            -DSOURCE_FILE=${PROJECT_SOURCE_DIR}/client/src/game_bridge.cpp
            -DCONFIG_HEADER=${PROJECT_SOURCE_DIR}/client/include/expedition_online/client/config.hpp
            -DCONFIG_EXAMPLE=${PROJECT_SOURCE_DIR}/config/client.example.ini
            -P ${CMAKE_CURRENT_SOURCE_DIR}/appearance_safety.cmake
)
