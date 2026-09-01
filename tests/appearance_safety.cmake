if(NOT DEFINED SOURCE_FILE)
    message(FATAL_ERROR "SOURCE_FILE is required")
endif()

file(READ "${SOURCE_FILE}" game_bridge_source)
string(FIND "${game_bridge_source}" "FindAllOf" global_scan_position)
if(NOT global_scan_position EQUAL -1)
    message(FATAL_ERROR "GameBridge must not perform global UObject FindAllOf scans")
endif()

string(FIND "${game_bridge_source}" "K2_GetComponentsByClass" bounded_scan_position)
if(bounded_scan_position EQUAL -1)
    message(FATAL_ERROR "GameBridge must enumerate components through the owning actor")
endif()
