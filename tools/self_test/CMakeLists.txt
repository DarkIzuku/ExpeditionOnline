add_executable(ExpeditionOnlineSelfTest src/main.cpp)
target_link_libraries(ExpeditionOnlineSelfTest PRIVATE ExpeditionOnlineProtocol)
if(MSVC)
    target_compile_options(ExpeditionOnlineSelfTest PRIVATE /W4 /permissive-)
else()
    target_compile_options(ExpeditionOnlineSelfTest PRIVATE -Wall -Wextra -Wpedantic)
endif()
set_target_properties(ExpeditionOnlineSelfTest PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
