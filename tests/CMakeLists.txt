add_executable(ExpeditionOnlineProtocolTests protocol_tests.cpp)
target_link_libraries(ExpeditionOnlineProtocolTests PRIVATE ExpeditionOnlineProtocol)
if(MSVC)
    target_compile_options(ExpeditionOnlineProtocolTests PRIVATE /W4 /permissive-)
else()
    target_compile_options(ExpeditionOnlineProtocolTests PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_test(NAME protocol_round_trip COMMAND ExpeditionOnlineProtocolTests)
add_test(
    NAME appearance_no_global_scan
    COMMAND ${CMAKE_COMMAND}
            -DSOURCE_FILE=${PROJECT_SOURCE_DIR}/client/src/game_bridge.cpp
            -P ${CMAKE_CURRENT_SOURCE_DIR}/appearance_safety.cmake
)
