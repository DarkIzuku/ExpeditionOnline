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

foreach(required_marker IN ITEMS
        "UAssetRegistryHelpers::GetAsset"
        "REMOTE_ASSET_LOAD_FAILED"
        "REMOTE_HAIR_DRIFT"
        "REMOTE_VISUAL_DRIFT"
        "SetMovementMode"
        "pawn_mesh_property"
        "REMOTE_DYNAMIC_MESH_COMPONENT"
        "CHARACTER_SKIN_PROPERTY"
        "REMOTE_OUTFIT_DRIFT"
        "LOCAL_JUMP_SIGNAL"
        "LOCAL_JUMP_EVENT")
    string(FIND "${game_bridge_source}" "${required_marker}" marker_position)
    if(marker_position EQUAL -1)
        message(FATAL_ERROR "GameBridge safety instrumentation is missing: ${required_marker}")
    endif()
endforeach()
