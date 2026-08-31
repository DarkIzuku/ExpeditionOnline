add_executable(ExpeditionOnlineServer src/main.cpp)
target_link_libraries(ExpeditionOnlineServer PRIVATE ExpeditionOnlineProtocol)
target_compile_definitions(ExpeditionOnlineServer PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)

if(MSVC)
    target_compile_options(ExpeditionOnlineServer PRIVATE /W4 /permissive-)
else()
    target_compile_options(ExpeditionOnlineServer PRIVATE -Wall -Wextra -Wpedantic)
endif()

set_target_properties(ExpeditionOnlineServer PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
