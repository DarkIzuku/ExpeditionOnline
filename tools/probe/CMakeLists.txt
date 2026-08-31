add_executable(ExpeditionOnlineProbe src/main.cpp)
target_link_libraries(ExpeditionOnlineProbe PRIVATE ExpeditionOnlineProtocol)
if(MSVC)
    target_compile_options(ExpeditionOnlineProbe PRIVATE /W4 /permissive-)
else()
    target_compile_options(ExpeditionOnlineProbe PRIVATE -Wall -Wextra -Wpedantic)
endif()
set_target_properties(ExpeditionOnlineProbe PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
