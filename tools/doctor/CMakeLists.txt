add_executable(ExpeditionOnlineDoctor src/main.cpp)
target_link_libraries(ExpeditionOnlineDoctor PRIVATE ExpeditionOnlineProtocol)
if(MSVC)
    target_compile_options(ExpeditionOnlineDoctor PRIVATE /W4 /permissive-)
else()
    target_compile_options(ExpeditionOnlineDoctor PRIVATE -Wall -Wextra -Wpedantic)
endif()
set_target_properties(ExpeditionOnlineDoctor PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
