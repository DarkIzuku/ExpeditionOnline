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

string(FIND "${game_bridge_source}"
       "const auto skin = local_skin_objects_.find(object);"
       skin_object_filter_position)
string(FIND "${game_bridge_source}"
       "const auto tracked = local_jump_objects_.find(object);"
       jump_object_filter_position)
string(FIND "${game_bridge_source}"
       "tracked == local_jump_objects_.end())"
       combined_object_filter_position)
string(FIND "${game_bridge_source}"
       "const auto function_name = object_leaf_name(function_object);"
       process_event_name_position)
if(skin_object_filter_position EQUAL -1 OR
   jump_object_filter_position EQUAL -1 OR
   combined_object_filter_position EQUAL -1 OR
   process_event_name_position EQUAL -1 OR
   skin_object_filter_position GREATER process_event_name_position OR
   jump_object_filter_position GREATER process_event_name_position OR
   combined_object_filter_position GREATER process_event_name_position)
    message(FATAL_ERROR
            "ProcessEvent must reject untracked objects before allocating function names")
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
        "LOCAL_JUMP_EVENT"
        "LOCAL_JUMP_STARTED"
        "REMOTE_JUMP_EVENT"
        "REMOTE_TRANSFORM_DRIFT"
        "REMOTE_NETWORK_AUTHORITY"
        "REMOTE_APPEARANCE_DEFERRED"
        "LOCAL_SKIN_EVENT"
        "CSAP_SwapAssign")
    string(FIND "${game_bridge_source}" "${required_marker}" marker_position)
    if(marker_position EQUAL -1)
        message(FATAL_ERROR "GameBridge safety instrumentation is missing: ${required_marker}")
    endif()
endforeach()

foreach(forbidden_call IN ITEMS
        "LaunchCharacter"
        "AddImpulse"
        "AddForce")
    string(FIND "${game_bridge_source}" "${forbidden_call}" forbidden_position)
    if(NOT forbidden_position EQUAL -1)
        message(FATAL_ERROR "Remote jump path must not add local physics: ${forbidden_call}")
    endif()
endforeach()

file(READ "${CONFIG_HEADER}" config_header_source)
foreach(default_marker IN ITEMS
        "remote_network_authority{true}"
        "unsafe_direct_appearance{false}"
        "unsafe_direct_hair{false}")
    string(FIND "${config_header_source}" "${default_marker}" default_position)
    if(default_position EQUAL -1)
        message(FATAL_ERROR "Crash-safe client default is missing: ${default_marker}")
    endif()
endforeach()

file(READ "${CONFIG_EXAMPLE}" config_example_source)
foreach(config_marker IN ITEMS
        "remote_network_authority=true"
        "unsafe_direct_appearance=false"
        "unsafe_direct_hair=false")
    string(FIND "${config_example_source}" "${config_marker}" config_position)
    if(config_position EQUAL -1)
        message(FATAL_ERROR "Packaged config is missing safe default: ${config_marker}")
    endif()
endforeach()
