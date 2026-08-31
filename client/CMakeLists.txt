set(TARGET ExpeditionOnline)

add_library(${TARGET} SHARED
    src/dllmain.cpp
    src/config.cpp
    src/logger.cpp
    src/network_client.cpp
    src/game_bridge.cpp
)

target_include_directories(${TARGET} PRIVATE include)
target_link_libraries(${TARGET} PRIVATE UE4SS ExpeditionOnlineProtocol)
target_compile_definitions(${TARGET} PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)

if(MSVC)
    target_compile_options(${TARGET} PRIVATE /W4 /permissive-)
    set_property(TARGET ${TARGET} PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
endif()

set(EXPEDITION_PACKAGE_ROOT "${CMAKE_BINARY_DIR}/package/UE4SS/Mods/ExpeditionOnline")
set_target_properties(${TARGET} PROPERTIES
    OUTPUT_NAME main
    RUNTIME_OUTPUT_DIRECTORY "${EXPEDITION_PACKAGE_ROOT}/dlls"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${EXPEDITION_PACKAGE_ROOT}/dlls"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${EXPEDITION_PACKAGE_ROOT}/dlls"
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${EXPEDITION_PACKAGE_ROOT}/dlls"
)

add_custom_command(TARGET ${TARGET} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${EXPEDITION_PACKAGE_ROOT}/config"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/../config/client.example.ini"
            "${EXPEDITION_PACKAGE_ROOT}/config/config.ini"
    COMMAND ${CMAKE_COMMAND} -E touch "${EXPEDITION_PACKAGE_ROOT}/enabled.txt"
    COMMENT "Packaging ExpeditionOnline as a UE4SS C++ mod"
)
