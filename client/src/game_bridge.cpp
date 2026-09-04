#include <expedition_online/client/game_bridge.hpp>
#include <expedition_online/client_logic.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include <Windows.h>

#include <Unreal/AActor.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/World.hpp>

#if __has_include(<Unreal/UAssetRegistry.hpp>) &&                         \
    __has_include(<Unreal/UAssetRegistryHelpers.hpp>) &&                  \
    __has_include(<Unreal/UnrealVersion.hpp>)
#define EXPEDITION_HAS_UE4SS_ASSET_LOADER 1
#include <Unreal/UAssetRegistry.hpp>
#include <Unreal/UAssetRegistryHelpers.hpp>
#include <Unreal/UnrealVersion.hpp>
#else
#define EXPEDITION_HAS_UE4SS_ASSET_LOADER 0
#endif

#if __has_include(<Unreal/CoreUObject/UObject/Class.hpp>) &&               \
    __has_include(<Unreal/CoreUObject/UObject/UnrealType.hpp>)
#define EXPEDITION_HAS_UE4SS_REFLECTION 1
#include <Unreal/CoreUObject/UObject/Class.hpp>
#include <Unreal/CoreUObject/UObject/UnrealType.hpp>
#include <Unreal/FString.hpp>
#else
#define EXPEDITION_HAS_UE4SS_REFLECTION 0
#endif

namespace expedition_online::client {
namespace {
using RC::Unreal::AActor;
using RC::Unreal::FHitResult;
using RC::Unreal::FRotator;
using RC::Unreal::FVector;
using RC::Unreal::TArray;
using RC::Unreal::UClass;
using RC::Unreal::UFunction;
using RC::Unreal::UObject;
namespace UObjectGlobals = RC::Unreal::UObjectGlobals;
namespace logic = expedition_online::client_logic;

thread_local bool in_bridge_tick{};

auto object_is_valid(UObject *object) -> bool {
  return object != nullptr && !object->IsUnreachable();
}

auto widen(std::string_view value) -> std::wstring {
  if (value.empty())
    return {};
  const auto count =
      MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                          static_cast<int>(value.size()), nullptr, 0);
  if (count <= 0)
    return std::wstring(value.begin(), value.end());
  std::wstring result(static_cast<std::size_t>(count), L'\0');
  MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                      static_cast<int>(value.size()), result.data(), count);
  return result;
}

auto narrow(std::wstring_view value) -> std::string {
  if (value.empty())
    return {};
  const auto count = WideCharToMultiByte(CP_UTF8, 0, value.data(),
                                         static_cast<int>(value.size()),
                                         nullptr, 0, nullptr, nullptr);
  if (count <= 0)
    return {};
  std::string result(static_cast<std::size_t>(count), '\0');
  WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                      result.data(), count, nullptr, nullptr);
  return result;
}

auto object_name(UObject *object) -> std::string {
  return object_is_valid(object) ? narrow(object->GetFullName())
                                 : std::string{};
}

auto object_leaf_name(UObject *object) -> std::string {
  return object_is_valid(object) ? narrow(object->GetName()) : std::string{};
}

auto wall_clock_ms() -> std::uint64_t {
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::system_clock::now().time_since_epoch())
          .count());
}

auto object_property(UObject *object, const std::string &property_name)
    -> UObject * {
  if (!object_is_valid(object))
    return nullptr;
  const auto wide_name = widen(property_name);
  auto **value =
      object->GetValuePtrByPropertyNameInChain<UObject *>(wide_name.c_str());
  return value != nullptr && object_is_valid(*value) ? *value : nullptr;
}

auto vector_property(UObject *object, const std::string &property_name)
    -> FVector * {
  if (!object_is_valid(object))
    return nullptr;
  const auto wide_name = widen(property_name);
  return object->GetValuePtrByPropertyNameInChain<FVector>(wide_name.c_str());
}

auto byte_property(UObject *object, const std::string &property_name)
    -> std::uint8_t * {
  if (!object_is_valid(object))
    return nullptr;
  const auto wide_name = widen(property_name);
  return object->GetValuePtrByPropertyNameInChain<std::uint8_t>(
      wide_name.c_str());
}

auto bool_property(UObject *object, const std::string &property_name)
    -> bool * {
  if (!object_is_valid(object))
    return nullptr;
  const auto wide_name = widen(property_name);
  return object->GetValuePtrByPropertyNameInChain<bool>(wide_name.c_str());
}

auto rotator_property(UObject *object, const std::string &property_name)
    -> FRotator * {
  if (!object_is_valid(object))
    return nullptr;
  const auto wide_name = widen(property_name);
  return object->GetValuePtrByPropertyNameInChain<FRotator>(wide_name.c_str());
}

auto folded(std::string value) -> std::string;

#if EXPEDITION_HAS_UE4SS_REFLECTION
auto reflected_property(UObject *object, std::string_view wanted)
    -> RC::Unreal::FProperty * {
  if (!object_is_valid(object))
    return nullptr;
  std::size_t visited{};
  for (auto *property : RC::Unreal::TFieldRange<RC::Unreal::FProperty>(
           object->GetClassPrivate(),
           RC::Unreal::EFieldIterationFlags::IncludeSuper |
               RC::Unreal::EFieldIterationFlags::IncludeDeprecated)) {
    if (narrow(property->GetName()) == wanted)
      return property;
    if (++visited >= 512)
      break;
  }
  return nullptr;
}

auto reflected_fname_property(UObject *object, std::string_view name)
    -> RC::Unreal::FName * {
  auto *property = reflected_property(object, name);
  if (!property || !property->IsA<RC::Unreal::FNameProperty>())
    return nullptr;
  return property->ContainerPtrToValuePtr<RC::Unreal::FName>(object);
}

auto parameter_bounds_are_valid(RC::Unreal::UFunction *function,
                                RC::Unreal::FProperty *property) -> bool {
  if (!function || !property)
    return false;
  const auto offset = static_cast<std::size_t>(property->GetOffset_Internal());
  const auto size = static_cast<std::size_t>(property->GetSize());
  return offset <= function->GetParmsSize() &&
         size <= function->GetParmsSize() - offset;
}

auto call_fname_input_validated(UObject *object,
                                const std::string &function_name,
                                const std::string &value) -> bool {
  if (!object_is_valid(object) || value.empty())
    return false;
  auto *function =
      object->GetFunctionByNameInChain(widen(function_name).c_str());
  if (!function || function->GetParmsSize() == 0 ||
      function->GetParmsSize() > 4096)
    return false;
  RC::Unreal::FProperty *input{};
  for (auto *property : RC::Unreal::TFieldRange<RC::Unreal::FProperty>(
           function, RC::Unreal::EFieldIterationFlags::IncludeDeprecated)) {
    if (!property->HasAnyPropertyFlags(RC::Unreal::CPF_Parm) ||
        property->HasAnyPropertyFlags(RC::Unreal::CPF_ReturnParm))
      continue;
    if (input || !property->IsA<RC::Unreal::FNameProperty>() ||
        !parameter_bounds_are_valid(function, property))
      return false;
    input = property;
  }
  if (!input)
    return false;
  std::vector<std::uint8_t> params(function->GetParmsSize());
  auto *target =
      input->ContainerPtrToValuePtr<RC::Unreal::FName>(params.data());
  if (!target)
    return false;
  *target = RC::Unreal::FName(widen(value).c_str(), RC::Unreal::FNAME_Add);
  object->ProcessEvent(function, params.data());
  return true;
}

auto call_fname_object_return_validated(UObject *object,
                                        const std::string &function_name,
                                        const std::string &value) -> UObject * {
  if (!object_is_valid(object) || value.empty())
    return nullptr;
  auto *function =
      object->GetFunctionByNameInChain(widen(function_name).c_str());
  if (!function || function->GetParmsSize() == 0 ||
      function->GetParmsSize() > 4096)
    return nullptr;
  RC::Unreal::FProperty *input{};
  RC::Unreal::FProperty *output{};
  for (auto *property : RC::Unreal::TFieldRange<RC::Unreal::FProperty>(
           function, RC::Unreal::EFieldIterationFlags::IncludeDeprecated)) {
    if (!property->HasAnyPropertyFlags(RC::Unreal::CPF_Parm))
      continue;
    if (!parameter_bounds_are_valid(function, property))
      return nullptr;
    if (property->HasAnyPropertyFlags(RC::Unreal::CPF_ReturnParm)) {
      if (output || !property->IsA<RC::Unreal::FObjectProperty>())
        return nullptr;
      output = property;
    } else {
      if (input || !property->IsA<RC::Unreal::FNameProperty>())
        return nullptr;
      input = property;
    }
  }
  if (!input || !output)
    return nullptr;
  std::vector<std::uint8_t> params(function->GetParmsSize());
  auto *target =
      input->ContainerPtrToValuePtr<RC::Unreal::FName>(params.data());
  if (!target)
    return nullptr;
  *target = RC::Unreal::FName(widen(value).c_str(), RC::Unreal::FNAME_Add);
  object->ProcessEvent(function, params.data());
  auto **result = output->ContainerPtrToValuePtr<UObject *>(params.data());
  return result && object_is_valid(*result) ? *result : nullptr;
}

auto call_no_args_validated(UObject *object, const std::string &function_name)
    -> bool {
  if (!object_is_valid(object))
    return false;
  auto *function =
      object->GetFunctionByNameInChain(widen(function_name).c_str());
  if (!function || function->GetParmsSize() != 0)
    return false;
  object->ProcessEvent(function, nullptr);
  return true;
}

auto call_byte_input_validated(UObject *object,
                               const std::string &function_name,
                               std::uint8_t value) -> bool {
  if (!object_is_valid(object))
    return false;
  auto *function =
      object->GetFunctionByNameInChain(widen(function_name).c_str());
  if (!function || function->GetParmsSize() == 0 ||
      function->GetParmsSize() > 64)
    return false;
  RC::Unreal::FProperty *input{};
  for (auto *property : RC::Unreal::TFieldRange<RC::Unreal::FProperty>(
           function, RC::Unreal::EFieldIterationFlags::IncludeDeprecated)) {
    if (!property->HasAnyPropertyFlags(RC::Unreal::CPF_Parm) ||
        property->HasAnyPropertyFlags(RC::Unreal::CPF_ReturnParm))
      continue;
    const auto enum_like = property->IsA<RC::Unreal::FByteProperty>() ||
                           property->IsA<RC::Unreal::FEnumProperty>();
    if (input || !enum_like || property->GetSize() != 1 ||
        !parameter_bounds_are_valid(function, property))
      return false;
    input = property;
  }
  if (!input)
    return false;
  std::vector<std::uint8_t> params(function->GetParmsSize());
  auto *target = input->ContainerPtrToValuePtr<std::uint8_t>(params.data());
  if (!target)
    return false;
  *target = value;
  object->ProcessEvent(function, params.data());
  return true;
}

auto find_character_data(UObject *game_instance,
                         const std::string &character_id) -> UObject * {
  if (auto *data = call_fname_object_return_validated(
          game_instance, "GetCharacterByID", character_id))
    return data;
  if (auto *data = call_fname_object_return_validated(
          game_instance, "InitCharacterData", character_id))
    return data;
  std::size_t visited{};
  for (auto *property : RC::Unreal::TFieldRange<RC::Unreal::FProperty>(
           game_instance->GetClassPrivate(),
           RC::Unreal::EFieldIterationFlags::IncludeSuper |
               RC::Unreal::EFieldIterationFlags::IncludeDeprecated)) {
    if (++visited > 128)
      break;
    if (!property->IsA<RC::Unreal::FObjectProperty>())
      continue;
    auto **candidate =
        property->ContainerPtrToValuePtr<UObject *>(game_instance);
    if (!candidate || !object_is_valid(*candidate) ||
        object_name((*candidate)->GetClassPrivate())
                .find("AC_jRPG_CharactersManager") == std::string::npos)
      continue;
    if (auto *data = call_fname_object_return_validated(
            *candidate, "GetCharacterByID", character_id))
      return data;
    return call_fname_object_return_validated(*candidate, "InitCharacterData",
                                              character_id);
  }
  return nullptr;
}

auto read_item_customization_ids(UObject *character_data, std::string &skin,
                                 std::string &face) -> bool {
  auto *property =
      reflected_property(character_data, "CharacterCustomizationItemData");
  if (!property || !property->IsA<RC::Unreal::FStructProperty>())
    return false;
  auto *struct_property = static_cast<RC::Unreal::FStructProperty *>(property);
  auto *data = property->ContainerPtrToValuePtr<void>(character_data);
  auto *type = struct_property->GetStruct();
  if (!data || !type || type->GetStructureSize() <= 0 ||
      type->GetStructureSize() > 4096)
    return false;
  std::size_t visited{};
  for (auto *field : RC::Unreal::TFieldRange<RC::Unreal::FProperty>(
           type, RC::Unreal::EFieldIterationFlags::IncludeSuper |
                     RC::Unreal::EFieldIterationFlags::IncludeDeprecated)) {
    if (++visited > 64)
      break;
    if (!field->IsA<RC::Unreal::FNameProperty>())
      continue;
    const auto name = folded(narrow(field->GetName()));
    auto *value = field->ContainerPtrToValuePtr<RC::Unreal::FName>(data);
    if (!value)
      continue;
    if (skin.empty() && (name.find("skin") != std::string::npos ||
                         name.find("outfit") != std::string::npos))
      skin = narrow(value->ToString());
    if (face.empty() && (name.find("face") != std::string::npos ||
                         name.find("hair") != std::string::npos))
      face = narrow(value->ToString());
  }
  return !skin.empty() || !face.empty();
}

auto apply_vanilla_customization(UObject *actor,
                                 const protocol::AppearanceState &appearance,
                                 std::string &reason) -> bool {
  auto *game_instance = UObjectGlobals::FindFirstOf(L"BP_jRPG_GI_Custom_C");
  if (!object_is_valid(game_instance)) {
    reason = "game_instance_missing";
    return false;
  }
  auto *character_data =
      find_character_data(game_instance, appearance.character_id);
  if (!object_is_valid(character_data)) {
    reason = "character_data_missing";
    return false;
  }
  if (!call_fname_input_validated(character_data, "LoadCharacterBaseDataFromID",
                                  appearance.character_id)) {
    reason = "LoadCharacterBaseDataFromID_signature";
    return false;
  }
  auto *item_property =
      reflected_property(character_data, "CharacterCustomizationItemData");
  if (!item_property || !item_property->IsA<RC::Unreal::FStructProperty>()) {
    reason = "CharacterCustomizationItemData_missing";
    return false;
  }
  auto *item_type =
      static_cast<RC::Unreal::FStructProperty *>(item_property)->GetStruct();
  auto *item_data = item_property->ContainerPtrToValuePtr<void>(character_data);
  if (!item_type || !item_data || item_type->GetStructureSize() <= 0 ||
      item_type->GetStructureSize() > 4096) {
    reason = "CharacterCustomizationItemData_invalid";
    return false;
  }
  bool skin_written = appearance.customization_skin.empty();
  bool face_written = appearance.customization_face.empty();
  std::size_t visited{};
  for (auto *field : RC::Unreal::TFieldRange<RC::Unreal::FProperty>(
           item_type,
           RC::Unreal::EFieldIterationFlags::IncludeSuper |
               RC::Unreal::EFieldIterationFlags::IncludeDeprecated)) {
    if (++visited > 64)
      break;
    if (!field->IsA<RC::Unreal::FNameProperty>())
      continue;
    const auto name = folded(narrow(field->GetName()));
    auto *target = field->ContainerPtrToValuePtr<RC::Unreal::FName>(item_data);
    if (!target)
      continue;
    if (!skin_written && (name.find("skin") != std::string::npos ||
                          name.find("outfit") != std::string::npos)) {
      *target = RC::Unreal::FName(widen(appearance.customization_skin).c_str(),
                                  RC::Unreal::FNAME_Add);
      skin_written = true;
    } else if (!face_written && (name.find("face") != std::string::npos ||
                                 name.find("hair") != std::string::npos)) {
      *target = RC::Unreal::FName(widen(appearance.customization_face).c_str(),
                                  RC::Unreal::FNAME_Add);
      face_written = true;
    }
  }
  if (!skin_written || !face_written) {
    reason = "customization_FName_fields_not_confirmed";
    return false;
  }
  if (!call_no_args_validated(character_data,
                              "LoadCharacterCustomizationFromItemData")) {
    reason = "LoadCharacterCustomizationFromItemData_signature";
    return false;
  }
  auto *source_property =
      reflected_property(character_data, "CharacterCustomization");
  if (!source_property ||
      !source_property->IsA<RC::Unreal::FStructProperty>()) {
    reason = "CharacterCustomization_missing";
    return false;
  }
  auto *source_type =
      static_cast<RC::Unreal::FStructProperty *>(source_property)->GetStruct();
  auto *source = source_property->ContainerPtrToValuePtr<void>(character_data);
  auto *function =
      actor->GetFunctionByNameInChain(L"SetCharacterCustomization");
  if (!source_type || !source || !function || function->GetParmsSize() == 0 ||
      function->GetParmsSize() > 4096) {
    reason = "SetCharacterCustomization_missing";
    return false;
  }
  RC::Unreal::FProperty *customization_input{};
  RC::Unreal::FProperty *character_input{};
  for (auto *param : RC::Unreal::TFieldRange<RC::Unreal::FProperty>(
           function, RC::Unreal::EFieldIterationFlags::IncludeDeprecated)) {
    if (!param->HasAnyPropertyFlags(RC::Unreal::CPF_Parm) ||
        param->HasAnyPropertyFlags(RC::Unreal::CPF_ReturnParm))
      continue;
    if (!parameter_bounds_are_valid(function, param)) {
      reason = "SetCharacterCustomization_bounds";
      return false;
    }
    if (param->IsA<RC::Unreal::FStructProperty>()) {
      auto *target_type =
          static_cast<RC::Unreal::FStructProperty *>(param)->GetStruct();
      if (customization_input || target_type != source_type ||
          param->GetSize() != source_property->GetSize()) {
        reason = "SetCharacterCustomization_struct_type";
        return false;
      }
      customization_input = param;
    } else if (param->IsA<RC::Unreal::FNameProperty>()) {
      if (character_input) {
        reason = "SetCharacterCustomization_FName_count";
        return false;
      }
      character_input = param;
    } else {
      reason = "SetCharacterCustomization_unexpected_param";
      return false;
    }
  }
  if (!customization_input || !character_input) {
    reason = "SetCharacterCustomization_params";
    return false;
  }
  std::vector<std::uint8_t> params(function->GetParmsSize());
  auto *target_customization =
      customization_input->ContainerPtrToValuePtr<void>(params.data());
  auto *target_character =
      character_input->ContainerPtrToValuePtr<RC::Unreal::FName>(params.data());
  if (!target_customization || !target_character) {
    reason = "SetCharacterCustomization_param_ptr";
    return false;
  }
  std::memcpy(target_customization, source,
              static_cast<std::size_t>(source_property->GetSize()));
  *target_character = RC::Unreal::FName(widen(appearance.character_id).c_str(),
                                        RC::Unreal::FNAME_Add);
  actor->ProcessEvent(function, params.data());
  reason = "ok";
  return true;
}

auto capture_vanilla_appearance(std::uint64_t player_id, std::string &reason)
    -> protocol::AppearanceState {
  protocol::AppearanceState result;
  result.player_id = player_id;
  auto *game_instance = UObjectGlobals::FindFirstOf(L"BP_jRPG_GI_Custom_C");
  if (!object_is_valid(game_instance)) {
    reason = "game_instance_missing";
    return result;
  }
  auto *current =
      reflected_fname_property(game_instance, "CurrentCharacterWorld");
  if (!current) {
    reason = "CurrentCharacterWorld_not_FName";
    return result;
  }
  result.character_id = narrow(current->ToString());
  if (result.character_id.empty() || result.character_id == "None") {
    reason = "CurrentCharacterWorld_empty";
    result.character_id.clear();
    return result;
  }
  auto *character_data =
      find_character_data(game_instance, result.character_id);
  if (!object_is_valid(character_data)) {
    reason = "character_data_missing";
    return result;
  }
  if (!read_item_customization_ids(character_data, result.customization_skin,
                                   result.customization_face)) {
    reason = "customization_FName_fields_not_confirmed";
    return result;
  }
  reason = "ok";
  return result;
}
#else
auto call_byte_input_validated(UObject *, const std::string &, std::uint8_t)
    -> bool {
  return false;
}
auto apply_vanilla_customization(UObject *, const protocol::AppearanceState &,
                                 std::string &reason) -> bool {
  reason = "reflection_unavailable";
  return false;
}
auto capture_vanilla_appearance(std::uint64_t player_id, std::string &reason)
    -> protocol::AppearanceState {
  reason = "reflection_unavailable";
  return protocol::AppearanceState{player_id, {}, {}, {}};
}
#endif

auto set_object_property(UObject *object, const std::string &property_name,
                         UObject *value) -> bool {
  if (!object_is_valid(object) || !object_is_valid(value))
    return false;
  const auto wide_name = widen(property_name);
  auto **target =
      object->GetValuePtrByPropertyNameInChain<UObject *>(wide_name.c_str());
  if (!target)
    return false;
  *target = value;
  return true;
}

auto call_no_args(UObject *object, const std::string &function_name) -> bool {
  if (!object_is_valid(object))
    return false;
  const auto wide_name = widen(function_name);
  auto *function = object->GetFunctionByNameInChain(wide_name.c_str());
  if (!function)
    return false;
  object->ProcessEvent(function, nullptr);
  return true;
}

auto call_object_return(UObject *object, const std::string &function_name)
    -> UObject * {
  if (!object_is_valid(object))
    return nullptr;
  const auto wide_name = widen(function_name);
  auto *function = object->GetFunctionByNameInChain(wide_name.c_str());
  if (!function)
    return nullptr;
  struct Params {
    UObject *ReturnValue{};
  } params;
  object->ProcessEvent(function, &params);
  return object_is_valid(params.ReturnValue) ? params.ReturnValue : nullptr;
}

auto call_vector_return(UObject *object, const std::string &function_name,
                        FVector &result) -> bool {
  if (!object_is_valid(object))
    return false;
  const auto wide_name = widen(function_name);
  auto *function = object->GetFunctionByNameInChain(wide_name.c_str());
  if (!function)
    return false;
  struct Params {
    FVector ReturnValue{};
  } params;
  object->ProcessEvent(function, &params);
  result = params.ReturnValue;
  return true;
}

auto call_bool_return(UObject *object, const std::string &function_name,
                      bool &result) -> bool {
  if (!object_is_valid(object))
    return false;
  const auto wide_name = widen(function_name);
  auto *function = object->GetFunctionByNameInChain(wide_name.c_str());
  if (!function)
    return false;
  struct Params {
    bool ReturnValue{};
  } params;
  object->ProcessEvent(function, &params);
  result = params.ReturnValue;
  return true;
}

auto call_bool_return_validated(UObject *object,
                                const std::string &function_name, bool &result)
    -> bool {
#if EXPEDITION_HAS_UE4SS_REFLECTION
  if (!object_is_valid(object))
    return false;
  auto *function =
      object->GetFunctionByNameInChain(widen(function_name).c_str());
  if (!function || function->GetParmsSize() == 0 ||
      function->GetParmsSize() > 16)
    return false;
  std::size_t returns{};
  for (auto *property : RC::Unreal::TFieldRange<RC::Unreal::FProperty>(
           function, RC::Unreal::EFieldIterationFlags::IncludeDeprecated)) {
    if (!property->HasAnyPropertyFlags(RC::Unreal::CPF_Parm))
      continue;
    if (!property->HasAnyPropertyFlags(RC::Unreal::CPF_ReturnParm) ||
        !property->IsA<RC::Unreal::FBoolProperty>() ||
        property->GetOffset_Internal() != 0 ||
        !parameter_bounds_are_valid(function, property))
      return false;
    ++returns;
  }
  return returns == 1 && call_bool_return(object, function_name, result);
#else
  (void)object;
  (void)function_name;
  (void)result;
  return false;
#endif
}

auto call_bool_input(UObject *object, const std::string &function_name,
                     bool value) -> bool {
  if (!object_is_valid(object))
    return false;
  const auto wide_name = widen(function_name);
  auto *function = object->GetFunctionByNameInChain(wide_name.c_str());
  if (!function)
    return false;
  struct Params {
    bool bEnabled{};
  } params{value};
  object->ProcessEvent(function, &params);
  return true;
}

auto call_set_movement_mode(UObject *movement_component,
                            std::uint8_t movement_mode,
                            std::uint8_t custom_movement_mode) -> bool {
  if (!object_is_valid(movement_component))
    return false;
  auto *function =
      movement_component->GetFunctionByNameInChain(L"SetMovementMode");
  if (!function)
    return false;
  struct Params {
    std::uint8_t NewMovementMode{};
    std::uint8_t NewCustomMode{};
  } params{movement_mode, custom_movement_mode};
  movement_component->ProcessEvent(function, &params);
  return true;
}

auto normalized_object_path(std::string full_name) -> std::string {
  const auto space = full_name.find(' ');
  if (space != std::string::npos)
    full_name.erase(0, space + 1);
  return full_name;
}

auto class_leaf(std::string full_name) -> std::string {
  full_name = normalized_object_path(std::move(full_name));
  const auto dot = full_name.find_last_of('.');
  if (dot != std::string::npos)
    return full_name.substr(dot + 1);
  const auto slash = full_name.find_last_of('/');
  return slash == std::string::npos ? full_name : full_name.substr(slash + 1);
}

auto same_appearance(const protocol::AppearanceState &left,
                     const protocol::AppearanceState &right) -> bool {
  return left.character_id == right.character_id &&
         left.customization_skin == right.customization_skin &&
         left.customization_face == right.customization_face;
}

auto same_locomotion(const protocol::PlayerLocomotionState &left,
                     const protocol::PlayerLocomotionState &right) -> bool {
  return left.movement_mode == right.movement_mode &&
         left.locomotion_state == right.locomotion_state &&
         left.gait == right.gait && left.stance == right.stance &&
         left.sprinting == right.sprinting &&
         left.crouching == right.crouching && left.aiming == right.aiming &&
         std::fabs(left.aim_pitch - right.aim_pitch) < 0.5F;
}

auto first_byte_property(UObject *object,
                         std::initializer_list<const char *> names)
    -> std::uint8_t * {
  for (const auto *name : names) {
    if (auto *value = byte_property(object, name))
      return value;
  }
  return nullptr;
}

auto first_bool_property(UObject *object,
                         std::initializer_list<const char *> names) -> bool * {
  for (const auto *name : names) {
    if (auto *value = bool_property(object, name))
      return value;
  }
  return nullptr;
}

auto is_skeletal_mesh_component(UObject *object) -> bool {
  if (!object_is_valid(object))
    return false;
  auto *object_class = object->GetClassPrivate();
  if (!object_is_valid(object_class))
    return false;

  auto *skeletal_mesh_component_class =
      UObjectGlobals::StaticFindObject<UClass *>(
          nullptr, nullptr, L"/Script/Engine.SkeletalMeshComponent");
  if (object_is_valid(skeletal_mesh_component_class)) {
    return object->IsA(skeletal_mesh_component_class);
  }

  // The reflected Engine class should always be available. Retain a name
  // check so appearance capture still works on unusual stripped builds.
  return object_name(object_class).find("SkeletalMeshComponent") !=
         std::string::npos;
}

auto is_instance_of(UObject *object, const wchar_t *class_path,
                    std::string_view class_name_fallback) -> bool {
  if (!object_is_valid(object))
    return false;
  auto *expected_class =
      UObjectGlobals::StaticFindObject<UClass *>(nullptr, nullptr, class_path);
  if (object_is_valid(expected_class))
    return object->IsA(expected_class);
  auto *object_class = object->GetClassPrivate();
  return object_is_valid(object_class) &&
         object_name(object_class).find(class_name_fallback) !=
             std::string::npos;
}

auto is_skeletal_mesh_asset(UObject *object) -> bool {
  if (!object_is_valid(object))
    return false;
  auto *skeletal_mesh_class = UObjectGlobals::StaticFindObject<UClass *>(
      nullptr, nullptr, L"/Script/Engine.SkeletalMesh");
  if (object_is_valid(skeletal_mesh_class))
    return object->IsA(skeletal_mesh_class);
  const auto type = object_name(object->GetClassPrivate());
  return type.ends_with("/Script/Engine.SkeletalMesh") ||
         type.ends_with(".SkeletalMesh");
}

auto resolve_skeletal_mesh(
    const std::string &full_name,
    std::unordered_map<std::string, UObject *> &asset_cache, Logger &logger)
    -> UObject * {
  const auto normalized = normalized_object_path(full_name);
  if (normalized.empty())
    return nullptr;

  if (const auto cached = asset_cache.find(normalized);
      cached != asset_cache.end()) {
    if (object_is_valid(cached->second) &&
        is_skeletal_mesh_asset(cached->second)) {
      return cached->second;
    }
    asset_cache.erase(cached);
  }

  const auto path = widen(normalized);
  if (auto *found = UObjectGlobals::StaticFindObject<UObject *>(
          nullptr, nullptr, path.c_str());
      object_is_valid(found) && is_skeletal_mesh_asset(found)) {
    asset_cache.emplace(normalized, found);
    logger.info("REMOTE_ASSET_RESOLVED source=already_loaded asset=" +
                object_name(found));
    return found;
  }

  UObject *loaded{};
#if EXPEDITION_HAS_UE4SS_ASSET_LOADER
  auto *registry = static_cast<RC::Unreal::UAssetRegistry *>(
      RC::Unreal::UAssetRegistryHelpers::GetAssetRegistry().ObjectPointer);
  if (registry) {
    const auto object_path =
        RC::Unreal::FName(path.c_str(), RC::Unreal::FNAME_Add);
    auto asset_data = registry->GetAssetByObjectPath(object_path);
    if ((RC::Unreal::Version::IsAtMost(5, 0) &&
         asset_data.ObjectPath().GetComparisonIndex()) ||
        asset_data.PackageName().GetComparisonIndex()) {
      loaded = RC::Unreal::UAssetRegistryHelpers::GetAsset(asset_data);
    }
  }
#endif
  if (object_is_valid(loaded) && is_skeletal_mesh_asset(loaded)) {
    asset_cache.emplace(normalized, loaded);
    logger.info("REMOTE_ASSET_RESOLVED source=loaded asset=" +
                object_name(loaded));
    return loaded;
  }
  logger.warning("REMOTE_ASSET_LOAD_FAILED asset=" + full_name);
  return nullptr;
}

auto find_remote_movement_component(AActor *owner) -> UObject * {
  if (!object_is_valid(owner))
    return nullptr;
  auto *component = object_property(owner, "CharacterMovement");
  if (is_instance_of(component, L"/Script/Engine.CharacterMovementComponent",
                     "CharacterMovementComponent"))
    return component;

  component = object_property(owner, "MovementComponent");
  if (is_instance_of(component, L"/Script/Engine.MovementComponent",
                     "MovementComponent"))
    return component;

  auto *movement_component_class = UObjectGlobals::StaticFindObject<UClass *>(
      nullptr, nullptr, L"/Script/Engine.CharacterMovementComponent");
  if (!object_is_valid(movement_component_class))
    return nullptr;
  const auto &components =
      owner->K2_GetComponentsByClass(movement_component_class);
  for (auto *candidate : components) {
    if (is_instance_of(candidate, L"/Script/Engine.CharacterMovementComponent",
                       "CharacterMovementComponent")) {
      return candidate;
    }
  }
  return nullptr;
}

auto horizontal_speed(float x, float y, float z) -> float {
  return std::sqrt(x * x + y * y + z * z);
}

auto find_owned_skeletal_component(AActor *owner,
                                   const std::string &component_name)
    -> UObject * {
  if (!object_is_valid(owner))
    return nullptr;
  auto *skeletal_mesh_component_class =
      UObjectGlobals::StaticFindObject<UClass *>(
          nullptr, nullptr, L"/Script/Engine.SkeletalMeshComponent");
  if (!object_is_valid(skeletal_mesh_component_class))
    return nullptr;
  const auto &components =
      owner->K2_GetComponentsByClass(skeletal_mesh_component_class);
  for (auto *component : components) {
    if (is_skeletal_mesh_component(component) &&
        object_leaf_name(component) == component_name) {
      return component;
    }
  }
  return nullptr;
}

auto is_actor(UObject *object) -> bool {
  return is_instance_of(object, L"/Script/Engine.Actor", "Actor");
}

auto is_character_skin_actor(AActor *actor) -> bool {
  if (!object_is_valid(actor))
    return false;
  return object_name(actor).find("BP_CharacterSkin_") != std::string::npos ||
         object_name(actor->GetClassPrivate()).find("BP_CharacterSkin_") !=
             std::string::npos;
}

struct ReachableActor {
  AActor *actor{};
  std::string route;
};

auto add_reachable_actor(std::vector<ReachableActor> &actors, UObject *object,
                         std::string route) -> void {
  if (!object_is_valid(object) || !is_actor(object))
    return;
  auto *actor = static_cast<AActor *>(object);
  const auto duplicate = std::find_if(
      actors.begin(), actors.end(),
      [actor](const ReachableActor &value) { return value.actor == actor; });
  if (duplicate == actors.end())
    actors.push_back(ReachableActor{actor, std::move(route)});
}

auto append_actor_array_function(AActor *owner,
                                 const std::string &function_name,
                                 std::vector<ReachableActor> &actors,
                                 const std::string &route) -> void {
  if (!object_is_valid(owner))
    return;
  const auto wide_name = widen(function_name);
  auto *function = owner->GetFunctionByNameInChain(wide_name.c_str());
  if (!function)
    return;

  if (function_name == "GetAttachedActors") {
    struct Params {
      TArray<AActor *> OutActors{};
      bool bResetArray{true};
      bool bRecursivelyIncludeAttachedActors{true};
    } params;
    owner->ProcessEvent(function, &params);
    for (auto *actor : params.OutActors)
      add_reachable_actor(actors, actor, route);
  } else {
    struct Params {
      TArray<AActor *> ChildActors{};
      bool bIncludeDescendants{true};
    } params;
    owner->ProcessEvent(function, &params);
    for (auto *actor : params.ChildActors)
      add_reachable_actor(actors, actor, route);
  }
}

constexpr std::string_view kVisualActorProperties[]{
    "CharacterSkinActor",
    "CurrentCharacterSkinActor",
    "ActiveCharacterSkinActor",
    "CharacterSkin",
    "CurrentCharacterSkin",
    "ActiveCharacterSkin",
    "SkinActor",
    "VisualActor",
    "CharacterVisualActor",
};

auto collect_reachable_visual_actors(AActor *root)
    -> std::vector<ReachableActor> {
  std::vector<ReachableActor> actors;
  if (!object_is_valid(root))
    return actors;
  add_reachable_actor(actors, root, "pawn");

  for (const auto property_name : kVisualActorProperties) {
    if (auto *value = object_property(root, std::string(property_name));
        object_is_valid(value)) {
      add_reachable_actor(actors, value,
                          "pawn_property:" + std::string(property_name));
    }
  }

  auto *actor_component_class = UObjectGlobals::StaticFindObject<UClass *>(
      nullptr, nullptr, L"/Script/Engine.ActorComponent");
  if (object_is_valid(actor_component_class)) {
    const auto &components =
        root->K2_GetComponentsByClass(actor_component_class);
    for (auto *component : components) {
      if (!object_is_valid(component))
        continue;
      if (auto *child_actor = object_property(component, "ChildActor");
          object_is_valid(child_actor)) {
        add_reachable_actor(actors, child_actor,
                            "child_actor_component:" +
                                object_leaf_name(component));
      }
      for (const auto property_name : kVisualActorProperties) {
        if (auto *value =
                object_property(component, std::string(property_name));
            object_is_valid(value)) {
          add_reachable_actor(
              actors, value,
              "component_property:" + object_leaf_name(component) + "." +
                  std::string(property_name));
        }
      }
    }
  }

  // Both calls are bounded to actors related to this Pawn. No global UObject
  // enumeration is used, including while levels or customization actors stream.
  append_actor_array_function(root, "GetAttachedActors", actors,
                              "attached_actor");
  append_actor_array_function(root, "GetAllChildActors", actors, "child_actor");
  return actors;
}

struct ComponentSelection {
  UObject *component{};
  AActor *owner{};
  std::string route;
};

auto find_reachable_skeletal_component(
    const std::vector<ReachableActor> &actors,
    const std::string &component_name) -> ComponentSelection {
  const auto select_from = [&](bool character_skin_only) -> ComponentSelection {
    for (const auto &reachable : actors) {
      if (!object_is_valid(reachable.actor))
        continue;
      if (character_skin_only != is_character_skin_actor(reachable.actor))
        continue;
      if (auto *component =
              find_owned_skeletal_component(reachable.actor, component_name)) {
        return ComponentSelection{component, reachable.actor, reachable.route};
      }
    }
    return {};
  };

  if (!actors.empty()) {
    if (auto *direct = find_owned_skeletal_component(actors.front().actor,
                                                     component_name)) {
      return ComponentSelection{direct, actors.front().actor,
                                actors.front().route};
    }
  }
  if (auto selected = select_from(true); object_is_valid(selected.component))
    return selected;
  return select_from(false);
}

struct BodySelection {
  UObject *component{};
  UObject *mesh{};
  std::size_t candidates{};
  std::string source{"none"};
  std::vector<ReachableActor> reachable_actors;
};

auto component_mesh(UObject *component) -> UObject * {
  return is_skeletal_mesh_component(component)
             ? object_property(component, "SkeletalMesh")
             : nullptr;
}

auto component_has_customization_skin(UObject *component,
                                      std::string_view expected_character = {})
    -> bool {
  return is_skeletal_mesh_component(component) &&
         logic::is_customization_skin_mesh(
             object_name(component_mesh(component)), expected_character);
}

auto component_has_recognized_body(UObject *component,
                                   std::string_view expected_character = {})
    -> bool {
  const auto mesh_name = object_name(component_mesh(component));
  if (mesh_name.empty() || mesh_name.find("SKM_Quinn") != std::string::npos ||
      mesh_name.find("FaceMesh") != std::string::npos ||
      mesh_name.find("Placeholder") != std::string::npos ||
      mesh_name.find("/Game/Characters/Heros/") == std::string::npos)
    return false;
  const auto character = logic::infer_character_from_body_mesh(mesh_name);
  return character != "Unknown" &&
         (expected_character.empty() || character == expected_character);
}

auto is_character_skin_component(UObject *component) -> bool {
  if (!object_is_valid(component))
    return false;
  const auto signature =
      object_name(component) + " " + object_name(component->GetClassPrivate());
  return signature.find("BP_CharacterSkinComponent") != std::string::npos;
}

auto folded(std::string value) -> std::string {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char character) {
                   return static_cast<char>(std::tolower(character));
                 });
  return value;
}

auto is_skin_runtime_object(UObject *object) -> bool {
  if (!object_is_valid(object))
    return false;
  const auto signature =
      object_name(object) + " " + object_name(object->GetClassPrivate());
  return signature.find("BP_CharacterSkinComponent") != std::string::npos ||
         signature.find("CSAP_SwapAssign") != std::string::npos;
}

auto is_locomotion_anim_instance(UObject *object) -> bool {
  if (!object_is_valid(object))
    return false;
  const auto signature = folded(object_name(object) + " " +
                                object_name(object->GetClassPrivate()));
  if (signature.find("hair") != std::string::npos ||
      signature.find("face") != std::string::npos ||
      signature.find("kawaiiphysics") != std::string::npos ||
      signature.find("evaluategraphexposedinputs") != std::string::npos) {
    return false;
  }
  return signature.find("als") != std::string::npos &&
         (signature.find("anim") != std::string::npos ||
          signature.find("locomotion") != std::string::npos);
}

auto find_locomotion_anim_instance(AActor *actor, UObject *preferred_component)
    -> UObject * {
  if (!object_is_valid(actor))
    return nullptr;
  if (is_skeletal_mesh_component(preferred_component)) {
    auto *candidate =
        call_object_return(preferred_component, "GetAnimInstance");
    if (is_locomotion_anim_instance(candidate))
      return candidate;
  }
  auto *skeletal_mesh_component_class =
      UObjectGlobals::StaticFindObject<UClass *>(
          nullptr, nullptr, L"/Script/Engine.SkeletalMeshComponent");
  if (!object_is_valid(skeletal_mesh_component_class))
    return nullptr;
  for (auto *component :
       actor->K2_GetComponentsByClass(skeletal_mesh_component_class)) {
    if (!is_skeletal_mesh_component(component))
      continue;
    auto *candidate = call_object_return(component, "GetAnimInstance");
    if (is_locomotion_anim_instance(candidate))
      return candidate;
  }
  return nullptr;
}

auto has_visual_property_keyword(std::string name) -> bool {
  std::transform(name.begin(), name.end(), name.begin(),
                 [](unsigned char value) {
                   return static_cast<char>(std::tolower(value));
                 });
  constexpr std::string_view keywords[]{"skin",   "characterskin", "appearance",
                                        "outfit", "customization", "visual",
                                        "body",   "hair",          "mesh"};
  return std::any_of(std::begin(keywords), std::end(keywords),
                     [&](std::string_view keyword) {
                       return name.find(keyword) != std::string::npos;
                     });
}

auto log_filtered_visual_properties(Logger &logger, std::string_view prefix,
                                    std::uint64_t player_id, UObject *owner)
    -> void {
#if EXPEDITION_HAS_UE4SS_REFLECTION
  if (!object_is_valid(owner))
    return;
  std::size_t logged{};
  for (auto *property : RC::Unreal::TFieldRange<RC::Unreal::FProperty>(
           owner->GetClassPrivate(),
           RC::Unreal::EFieldIterationFlags::IncludeSuper |
               RC::Unreal::EFieldIterationFlags::IncludeDeprecated)) {
    const auto property_name = narrow(property->GetName());
    if (!has_visual_property_keyword(property_name) ||
        !property->IsA<RC::Unreal::FObjectProperty>()) {
      continue;
    }
    auto *object_property_value =
        *property->ContainerPtrToValuePtr<UObject *>(owner);
    logger.info(std::string(prefix) + " player=" + std::to_string(player_id) +
                " owner=" + object_name(owner) + " property=" + property_name +
                " value=" +
                (object_is_valid(object_property_value)
                     ? object_name(object_property_value)
                     : "nil") +
                " class=" +
                (object_is_valid(object_property_value)
                     ? object_name(object_property_value->GetClassPrivate())
                     : "nil"));
    if (++logged >= 64)
      break;
  }
#else
  (void)logger;
  (void)prefix;
  (void)player_id;
  (void)owner;
#endif
}

auto log_character_skin_properties(Logger &logger, std::string_view scope,
                                   std::uint64_t player_id, UObject *owner)
    -> void {
#if EXPEDITION_HAS_UE4SS_REFLECTION
  if (!object_is_valid(owner))
    return;
  std::size_t logged{};
  for (auto *property : RC::Unreal::TFieldRange<RC::Unreal::FProperty>(
           owner->GetClassPrivate(),
           RC::Unreal::EFieldIterationFlags::IncludeSuper |
               RC::Unreal::EFieldIterationFlags::IncludeDeprecated)) {
    const auto property_name = narrow(property->GetName());
    if (!has_visual_property_keyword(property_name) ||
        !property->IsA<RC::Unreal::FObjectProperty>()) {
      continue;
    }
    auto *value = *property->ContainerPtrToValuePtr<UObject *>(owner);
    logger.info("CHARACTER_SKIN_PROPERTY scope=" + std::string(scope) +
                " player=" + std::to_string(player_id) + " owner=" +
                object_name(owner) + " property=" + property_name + " value=" +
                (object_is_valid(value) ? object_name(value) : "nil") +
                " class=" +
                (object_is_valid(value) ? object_name(value->GetClassPrivate())
                                        : "nil"));
    if (++logged >= 64)
      break;
  }
#else
  (void)logger;
  (void)scope;
  (void)player_id;
  (void)owner;
#endif
}

auto select_character_skin_component_body(
    AActor *actor, std::string_view expected_character, Logger *logger,
    std::string_view scope, std::uint64_t player_id) -> UObject * {
  if (!object_is_valid(actor))
    return nullptr;
  auto *actor_component_class = UObjectGlobals::StaticFindObject<UClass *>(
      nullptr, nullptr, L"/Script/Engine.ActorComponent");
  if (!object_is_valid(actor_component_class))
    return nullptr;
  for (auto *skin_component :
       actor->K2_GetComponentsByClass(actor_component_class)) {
    if (!is_character_skin_component(skin_component))
      continue;
    if (logger) {
      if (scope == "remote") {
        logger->info(
            "REMOTE_SKIN_COMPONENT player=" + std::to_string(player_id) +
            " component=" + object_name(skin_component) + " attached_body=" +
            object_name(
                object_property(skin_component, "AttachedBodyOnCharacter")) +
            " policy=" +
            object_name(object_property(skin_component, "SkinAssignPolicy")) +
            " policy_class=" +
            object_name(
                object_property(skin_component, "SkinAssignPolicyClass")) +
            " spawned_skin=" +
            object_name(
                object_property(skin_component, "SpawnedCharacterSkin")));
      }
      log_character_skin_properties(*logger, scope, player_id, skin_component);
    }

    if (auto *mesh_property = object_property(skin_component, "Mesh");
        component_has_customization_skin(mesh_property, expected_character)) {
      return mesh_property;
    }
#if EXPEDITION_HAS_UE4SS_REFLECTION
    for (auto *property : RC::Unreal::TFieldRange<RC::Unreal::FProperty>(
             skin_component->GetClassPrivate(),
             RC::Unreal::EFieldIterationFlags::IncludeSuper |
                 RC::Unreal::EFieldIterationFlags::IncludeDeprecated)) {
      const auto property_name = narrow(property->GetName());
      if (!has_visual_property_keyword(property_name) ||
          !property->IsA<RC::Unreal::FObjectProperty>()) {
        continue;
      }
      auto *value =
          *property->ContainerPtrToValuePtr<UObject *>(skin_component);
      if (component_has_customization_skin(value, expected_character))
        return value;
    }
#endif
  }
  return nullptr;
}

auto select_actor_customization_component(AActor *actor,
                                          std::string_view expected_character)
    -> UObject * {
  if (!object_is_valid(actor))
    return nullptr;
  auto *skeletal_mesh_component_class =
      UObjectGlobals::StaticFindObject<UClass *>(
          nullptr, nullptr, L"/Script/Engine.SkeletalMeshComponent");
  if (!object_is_valid(skeletal_mesh_component_class))
    return nullptr;
  for (auto *component :
       actor->K2_GetComponentsByClass(skeletal_mesh_component_class)) {
    if (object_leaf_name(component) == "CharacterMesh0")
      continue;
    if (component_has_customization_skin(component, expected_character))
      return component;
  }
  return nullptr;
}

auto collect_visual_mesh_snapshot(const std::vector<ReachableActor> &actors)
    -> std::unordered_map<std::string, std::string> {
  std::unordered_map<std::string, std::string> snapshot;
  auto *skeletal_mesh_component_class =
      UObjectGlobals::StaticFindObject<UClass *>(
          nullptr, nullptr, L"/Script/Engine.SkeletalMeshComponent");
  if (!object_is_valid(skeletal_mesh_component_class))
    return snapshot;
  for (const auto &reachable : actors) {
    if (!object_is_valid(reachable.actor))
      continue;
    for (auto *component : reachable.actor->K2_GetComponentsByClass(
             skeletal_mesh_component_class)) {
      if (!is_skeletal_mesh_component(component))
        continue;
      const auto key =
          object_name(reachable.actor) + "|" + object_name(component);
      snapshot.emplace(key,
                       object_name(object_property(component, "SkeletalMesh")));
    }
  }
  return snapshot;
}

auto select_local_body_component(AActor *pawn, Logger *logger = nullptr)
    -> BodySelection {
  BodySelection selection;
  if (!object_is_valid(pawn))
    return selection;
  auto *skeletal_mesh_component_class =
      UObjectGlobals::StaticFindObject<UClass *>(
          nullptr, nullptr, L"/Script/Engine.SkeletalMeshComponent");
  if (!object_is_valid(skeletal_mesh_component_class))
    return selection;

  selection.reachable_actors = collect_reachable_visual_actors(pawn);
  for (auto *component :
       pawn->K2_GetComponentsByClass(skeletal_mesh_component_class)) {
    (void)component;
    ++selection.candidates;
  }

  if (auto *body = find_owned_skeletal_component(pawn, "Body");
      component_has_recognized_body(body)) {
    selection.component = body;
    selection.mesh = component_mesh(body);
    selection.source = "pawn_body";
    return selection;
  }

  if (auto *mesh_property = object_property(pawn, "Mesh");
      component_has_customization_skin(mesh_property)) {
    selection.component = mesh_property;
    selection.mesh = component_mesh(mesh_property);
    selection.source = "pawn_mesh_property";
    return selection;
  }

  if (auto *component = select_actor_customization_component(pawn, {})) {
    selection.component = component;
    selection.mesh = component_mesh(component);
    selection.source = "pawn_skin_component_scan";
    return selection;
  }

  if (auto *component =
          select_character_skin_component_body(pawn, {}, logger, "local", 0)) {
    selection.component = component;
    selection.mesh = component_mesh(component);
    selection.source = "pawn_skin_component_scan";
    return selection;
  }

  for (const auto &reachable : selection.reachable_actors) {
    if (!object_is_valid(reachable.actor) || reachable.actor == pawn)
      continue;
    if (auto *body = find_owned_skeletal_component(reachable.actor, "Body");
        component_has_recognized_body(body)) {
      selection.component = body;
      selection.mesh = component_mesh(body);
      selection.source = "pawn_skin_component_scan";
      return selection;
    }
    if (auto *component =
            select_actor_customization_component(reachable.actor, {})) {
      selection.component = component;
      selection.mesh = component_mesh(component);
      selection.source = "pawn_skin_component_scan";
      return selection;
    }
  }
  return selection;
}

auto remote_route_name(const std::string &reachable_route) -> std::string {
  if (reachable_route.find("attached_actor") != std::string::npos)
    return "attached_actor";
  if (reachable_route.find("child_actor") != std::string::npos)
    return "child_actor";
  return "skin_component";
}

auto select_remote_body_component(AActor *actor,
                                  std::string_view expected_character,
                                  Logger *logger, std::uint64_t player_id)
    -> BodySelection {
  BodySelection selection;
  if (!object_is_valid(actor))
    return selection;
  selection.reachable_actors = collect_reachable_visual_actors(actor);

  if (auto *body = find_owned_skeletal_component(actor, "Body");
      component_has_recognized_body(body, expected_character)) {
    selection.component = body;
    selection.mesh = component_mesh(body);
    selection.source = "body";
    return selection;
  }
  if (auto *mesh_property = object_property(actor, "Mesh");
      is_skeletal_mesh_component(mesh_property)) {
    if (logger) {
      logger->info(
          "REMOTE_DYNAMIC_MESH_COMPONENT player=" + std::to_string(player_id) +
          " component=" + object_name(mesh_property) +
          " mesh=" + object_name(component_mesh(mesh_property)));
    }
    if (component_has_customization_skin(mesh_property, expected_character)) {
      selection.component = mesh_property;
      selection.mesh = component_mesh(mesh_property);
      selection.source = "mesh_property";
      return selection;
    }
  }
  if (auto *component =
          select_actor_customization_component(actor, expected_character)) {
    selection.component = component;
    selection.mesh = component_mesh(component);
    selection.source = "skin_component";
    return selection;
  }
  if (auto *component = select_character_skin_component_body(
          actor, expected_character, logger, "remote", player_id)) {
    selection.component = component;
    selection.mesh = component_mesh(component);
    selection.source = "skin_component";
    return selection;
  }

  for (const auto &reachable : selection.reachable_actors) {
    if (!object_is_valid(reachable.actor) || reachable.actor == actor)
      continue;
    if (auto *body = find_owned_skeletal_component(reachable.actor, "Body");
        component_has_recognized_body(body, expected_character)) {
      selection.component = body;
      selection.mesh = component_mesh(body);
      selection.source = remote_route_name(reachable.route);
      return selection;
    }
    if (auto *mesh_property = object_property(reachable.actor, "Mesh");
        component_has_customization_skin(mesh_property, expected_character)) {
      selection.component = mesh_property;
      selection.mesh = component_mesh(mesh_property);
      selection.source = remote_route_name(reachable.route);
      return selection;
    }
    if (auto *component = select_actor_customization_component(
            reachable.actor, expected_character)) {
      selection.component = component;
      selection.mesh = component_mesh(component);
      selection.source = remote_route_name(reachable.route);
      return selection;
    }
    if (auto *component = select_character_skin_component_body(
            reachable.actor, expected_character, logger, "remote", player_id)) {
      selection.component = component;
      selection.mesh = component_mesh(component);
      selection.source = remote_route_name(reachable.route);
      return selection;
    }
  }
  return selection;
}

auto log_remote_skeletal_inventory(Logger &logger, std::uint64_t player_id,
                                   const std::vector<ReachableActor> &actors)
    -> void {
  auto *skeletal_mesh_component_class =
      UObjectGlobals::StaticFindObject<UClass *>(
          nullptr, nullptr, L"/Script/Engine.SkeletalMeshComponent");
  if (!object_is_valid(skeletal_mesh_component_class))
    return;
  std::size_t logged{};
  for (const auto &reachable : actors) {
    if (!object_is_valid(reachable.actor))
      continue;
    logger.info("REMOTE_VISUAL_ACTOR player=" + std::to_string(player_id) +
                " route=" + reachable.route +
                " actor=" + object_name(reachable.actor) +
                " class=" + object_name(reachable.actor->GetClassPrivate()));
    log_filtered_visual_properties(logger, "REMOTE_VISUAL_PROPERTY", player_id,
                                   reachable.actor);
    const auto &components =
        reachable.actor->K2_GetComponentsByClass(skeletal_mesh_component_class);
    for (auto *component : components) {
      if (!is_skeletal_mesh_component(component))
        continue;
      logger.info(
          "REMOTE_SKELETAL_COMPONENT player=" + std::to_string(player_id) +
          " leaf=" + object_leaf_name(component) +
          " full_name=" + object_name(component) +
          " mesh=" + object_name(object_property(component, "SkeletalMesh")) +
          " owner=" + object_name(reachable.actor) +
          " route=" + reachable.route);
      log_filtered_visual_properties(logger, "REMOTE_VISUAL_PROPERTY",
                                     player_id, component);
      if (++logged >= 64) {
        logger.warning("REMOTE_SKELETAL_COMPONENT_TRUNCATED player=" +
                       std::to_string(player_id) + " limit=64");
        return;
      }
    }

    auto *actor_component_class = UObjectGlobals::StaticFindObject<UClass *>(
        nullptr, nullptr, L"/Script/Engine.ActorComponent");
    if (object_is_valid(actor_component_class)) {
      for (auto *component :
           reachable.actor->K2_GetComponentsByClass(actor_component_class)) {
        if (!object_is_valid(component) ||
            object_leaf_name(component).find("ChildActor") ==
                std::string::npos) {
          continue;
        }
        logger.info(
            "REMOTE_CHILD_ACTOR_COMPONENT player=" + std::to_string(player_id) +
            " component=" + object_name(component) + " child_actor=" +
            object_name(object_property(component, "ChildActor")) +
            " child_actor_class=" +
            object_name(object_property(component, "ChildActorClass")) +
            " template=" +
            object_name(object_property(component, "ChildActorTemplate")));
        log_filtered_visual_properties(logger, "REMOTE_VISUAL_PROPERTY",
                                       player_id, component);
      }
    }
  }
  if (logged == 0) {
    logger.info("REMOTE_SKELETAL_COMPONENT player=" +
                std::to_string(player_id) + " leaf=nil full_name=nil mesh=nil");
  }
}

auto log_local_visual_diagnostic(Logger &logger, AActor *pawn,
                                 const BodySelection &selection) -> void {
  logger.info("LOCAL_VISUAL_DIAGNOSTIC pawn=" + object_name(pawn) +
              " reason=Body_not_reachable");
  for (const auto property_name : kVisualActorProperties) {
    const auto wide_name = widen(property_name);
    auto **slot =
        pawn->GetValuePtrByPropertyNameInChain<UObject *>(wide_name.c_str());
    if (slot != nullptr) {
      logger.info(
          "LOCAL_SKIN_PROPERTY property=" + std::string(property_name) +
          " value=" + (object_is_valid(*slot) ? object_name(*slot) : "nil"));
    }
  }

  auto *actor_component_class = UObjectGlobals::StaticFindObject<UClass *>(
      nullptr, nullptr, L"/Script/Engine.ActorComponent");
  if (object_is_valid(actor_component_class)) {
    const auto &components =
        pawn->K2_GetComponentsByClass(actor_component_class);
    std::size_t logged{};
    for (auto *component : components) {
      if (!object_is_valid(component))
        continue;
      logger.info(
          "LOCAL_COMPONENT_DIAGNOSTIC leaf=" + object_leaf_name(component) +
          " full_name=" + object_name(component) + " class=" +
          object_name(component->GetClassPrivate()) + " child_actor=" +
          object_name(object_property(component, "ChildActor")) +
          " child_actor_class=" +
          object_name(object_property(component, "ChildActorClass")) +
          " template=" +
          object_name(object_property(component, "ChildActorTemplate")));
      log_filtered_visual_properties(logger, "LOCAL_VISUAL_PROPERTY", 0,
                                     component);
      if (++logged >= 64) {
        logger.warning("LOCAL_COMPONENT_DIAGNOSTIC_TRUNCATED limit=64");
        break;
      }
    }
  }

  auto *skeletal_mesh_component_class =
      UObjectGlobals::StaticFindObject<UClass *>(
          nullptr, nullptr, L"/Script/Engine.SkeletalMeshComponent");
  if (object_is_valid(skeletal_mesh_component_class)) {
    for (auto *component :
         pawn->K2_GetComponentsByClass(skeletal_mesh_component_class)) {
      if (!is_skeletal_mesh_component(component))
        continue;
      logger.info(
          "LOCAL_SKELETAL_DIAGNOSTIC leaf=" + object_leaf_name(component) +
          " mesh=" + object_name(object_property(component, "SkeletalMesh")));
    }
  }
  log_filtered_visual_properties(logger, "LOCAL_VISUAL_PROPERTY", 0, pawn);

  for (const auto &reachable : selection.reachable_actors) {
    logger.info("LOCAL_VISUAL_ROUTE route=" + reachable.route +
                " actor=" + object_name(reachable.actor) + " class=" +
                object_name(object_is_valid(reachable.actor)
                                ? reachable.actor->GetClassPrivate()
                                : nullptr));
  }
}

auto has_jump_signal_keyword(std::string name) -> bool {
  std::transform(name.begin(), name.end(), name.begin(),
                 [](unsigned char value) {
                   return static_cast<char>(std::tolower(value));
                 });
  constexpr std::string_view keywords[]{"jump",
                                        "fall",
                                        "air",
                                        "ground",
                                        "movementstate",
                                        "movement_state",
                                        "movementaction",
                                        "movement_action",
                                        "locomotion",
                                        "landed",
                                        "land"};
  return std::any_of(std::begin(keywords), std::end(keywords),
                     [&](std::string_view keyword) {
                       return name.find(keyword) != std::string::npos;
                     });
}

auto is_jump_start_function(const std::string &name) -> bool {
  return name == "OnJumped" || name == "Multicast_OnJumped";
}

auto is_relevant_jump_function(const std::string &name) -> bool {
  return is_jump_start_function(name) || name == "OnLanded" ||
         name == "Multicast_OnLanded" ||
         name.find("MovementModeChanged") != std::string::npos ||
         name.find("Airborne") != std::string::npos;
}

auto is_relevant_skin_function(const std::string &name) -> bool {
  return name == "OnBodySkinAssigned" || name == "OnFaceSkinAssigned" ||
         name == "OnSkinAssignCompleted" ||
         name == "LoadCharacterCustomizationFromItemData" ||
         name == "SetCharacterCustomization";
}

auto read_filtered_jump_signals(UObject *owner, std::string_view role)
    -> std::unordered_map<std::string, std::string> {
  std::unordered_map<std::string, std::string> signals;
#if EXPEDITION_HAS_UE4SS_REFLECTION
  if (!object_is_valid(owner))
    return signals;
  std::size_t captured{};
  for (auto *property : RC::Unreal::TFieldRange<RC::Unreal::FProperty>(
           owner->GetClassPrivate(),
           RC::Unreal::EFieldIterationFlags::IncludeSuper |
               RC::Unreal::EFieldIterationFlags::IncludeDeprecated)) {
    const auto property_name = narrow(property->GetName());
    if (!has_jump_signal_keyword(property_name))
      continue;
    auto *value_container = property->ContainerPtrToValuePtr<void>(owner);
    if (!value_container)
      continue;
    RC::Unreal::FString value_text{};
    property->ExportTextItem(value_text, value_container, value_container,
                             owner, 0);
    const auto *text_value = *value_text;
    const auto value =
        text_value ? narrow(std::wstring_view{text_value}) : std::string{};
    signals.emplace(std::string(role) + "|" + object_name(owner) + "|" +
                        property_name,
                    value);
    if (++captured >= 128)
      break;
  }
#else
  (void)owner;
  (void)role;
#endif
  return signals;
}

auto read_filtered_skin_signals(UObject *owner, std::string_view role)
    -> std::unordered_map<std::string, std::string> {
  std::unordered_map<std::string, std::string> signals;
#if EXPEDITION_HAS_UE4SS_REFLECTION
  if (!object_is_valid(owner))
    return signals;
  std::size_t captured{};
  for (auto *property : RC::Unreal::TFieldRange<RC::Unreal::FProperty>(
           owner->GetClassPrivate(),
           RC::Unreal::EFieldIterationFlags::IncludeSuper |
               RC::Unreal::EFieldIterationFlags::IncludeDeprecated)) {
    const auto property_name = narrow(property->GetName());
    if (!has_visual_property_keyword(property_name))
      continue;
    auto *value_container = property->ContainerPtrToValuePtr<void>(owner);
    if (!value_container)
      continue;
    RC::Unreal::FString value_text{};
    property->ExportTextItem(value_text, value_container, value_container,
                             owner, 0);
    const auto *text_value = *value_text;
    signals.emplace(
        std::string(role) + "|" + object_name(owner) + "|" + property_name,
        text_value ? narrow(std::wstring_view{text_value}) : std::string{});
    if (++captured >= 128)
      break;
  }
#else
  (void)owner;
  (void)role;
#endif
  return signals;
}

} // namespace

GameBridge::GameBridge(const ClientConfig &config, NetworkClient &network,
                       Logger &logger)
    : config_(config), network_(network), logger_(logger) {}

GameBridge::~GameBridge() { shutdown(); }

auto GameBridge::tick() -> void {
  if (shutdown_ || in_bridge_tick)
    return;
  const auto now = std::chrono::steady_clock::now();
  if (now < next_bridge_tick_)
    return;
  next_bridge_tick_ = now + std::chrono::milliseconds(16);

  in_bridge_tick = true;
  const char *stage = "connection_state";
  try {
    const auto connected = network_.connected();
    if (network_was_connected_ && !connected) {
      (void)network_.drain_incoming();
      destroy_all_remotes();
      local_player_id_ = 0;
      resync_requested_ = true;
      logger_.warning("CONNECTION_STATE_RESET remotes_destroyed=true "
                      "reconnect_pending=true");
    }
    network_was_connected_ = connected;
    stage = "process_incoming";
    process_incoming();
    stage = "update_local_player";
    update_local_player();
    stage = "update_remote_players";
    update_remote_players();
  } catch (const std::exception &exception) {
    try {
      logger_.error(std::string("GAME_BRIDGE_EXCEPTION stage=") + stage +
                    " error=" + exception.what());
    } catch (...) {
    }
  } catch (...) {
    try {
      logger_.error(std::string("GAME_BRIDGE_EXCEPTION stage=") + stage +
                    " error=unknown");
    } catch (...) {
    }
  }
  in_bridge_tick = false;
}

auto GameBridge::observe_process_event(UObject *object, UFunction *function)
    -> void {
  auto *function_object = reinterpret_cast<UObject *>(function);
  if (shutdown_ || !object_is_valid(object) ||
      !object_is_valid(function_object))
    return;
  const auto skin = local_skin_objects_.find(object);
  const auto tracked = local_jump_objects_.find(object);
  if (skin == local_skin_objects_.end() && tracked == local_jump_objects_.end())
    return;
  const auto function_name = object_leaf_name(function_object);
  const auto now = std::chrono::steady_clock::now();

  if (skin != local_skin_objects_.end() &&
      is_relevant_skin_function(function_name)) {
    const auto signature =
        "skin|" + object_name(object) + "|" + object_name(function_object);
    const auto recent = local_jump_event_times_.find(signature);
    if (recent == local_jump_event_times_.end() ||
        now - recent->second >= std::chrono::milliseconds(100)) {
      local_jump_event_times_[signature] = now;
      logger_.info("LOCAL_SKIN_EVENT object=" + object_name(object) +
                   " class=" + object_name(object->GetClassPrivate()) +
                   " role=" + skin->second +
                   " function=" + object_name(function_object));
      log_character_skin_properties(logger_, "local", local_player_id_, object);
      next_appearance_capture_ = {};
    }
  }

  if (tracked == local_jump_objects_.end() ||
      !is_relevant_jump_function(function_name))
    return;
  const auto signature =
      object_name(object) + "|" + object_name(function_object);
  if (const auto found = local_jump_event_times_.find(signature);
      found != local_jump_event_times_.end() &&
      now - found->second < std::chrono::milliseconds(250)) {
    return;
  }
  local_jump_event_times_[signature] = now;
  logger_.info("LOCAL_JUMP_EVENT object=" + object_name(object) + " role=" +
               tracked->second + " function=" + object_name(function_object));
  std::string folded = function_name;
  std::transform(folded.begin(), folded.end(), folded.begin(),
                 [](unsigned char value) {
                   return static_cast<char>(std::tolower(value));
                 });
  if (folded.find("land") != std::string::npos) {
    logger_.info("LOCAL_LAND_SIGNAL owner=" + object_name(object) + " role=" +
                 tracked->second + " function=" + object_name(function_object));
  }
  if (tracked->second == "pawn" && is_jump_start_function(function_name) &&
      (last_local_jump_started_.time_since_epoch().count() == 0 ||
       now - last_local_jump_started_ >= std::chrono::milliseconds(250))) {
    last_local_jump_started_ = now;
    ++local_jump_sequence_;
    logger_.info(
        "LOCAL_JUMP_STARTED sequence=" + std::to_string(local_jump_sequence_) +
        " object=" + object_name(object) +
        " function=" + object_name(function_object));
    if (network_.connected() && local_player_id_ != 0) {
      network_.enqueue(protocol::make_frame(
          protocol::MessageType::jump_event, network_.next_sequence(),
          protocol::JumpEvent{local_player_id_, local_jump_sequence_}));
    }
  }
}

auto GameBridge::shutdown() -> void {
  if (shutdown_)
    return;
  shutdown_ = true;
  destroy_all_remotes();
}

auto GameBridge::process_incoming() -> void {
  for (const auto &frame : network_.drain_incoming()) {
    switch (frame.type) {
    case protocol::MessageType::welcome: {
      destroy_all_remotes();
      local_player_id_ = protocol::decode_welcome(frame.payload).player_id;
      last_sent_context_.reset();
      resync_requested_ = true;
      logger_.info("WELCOME player_id=" + std::to_string(local_player_id_));
      break;
    }
    case protocol::MessageType::zone_state: {
      const auto state = protocol::decode_zone_state(frame.payload);
      if (state.player_id == local_player_id_)
        break;
      auto &remote = remotes_[state.player_id];
      remote.zone = state.zone;
      if (!local_zone_.empty() && state.zone != local_zone_)
        destroy_remote(state.player_id);
      break;
    }
    case protocol::MessageType::player_joined: {
      const auto joined = protocol::decode_player_joined(frame.payload);
      if (joined.player_id != local_player_id_) {
        remotes_.try_emplace(joined.player_id);
        logger_.info(
            "REMOTE_PLAYER_JOINED player=" + std::to_string(joined.player_id) +
            " name=" + joined.player_name);
      }
      break;
    }
    case protocol::MessageType::appearance_state: {
      const auto state = protocol::decode_appearance_state(frame.payload);
      if (state.player_id == local_player_id_)
        break;
      auto &remote = remotes_[state.player_id];
      logger_.info("REMOTE_APPEARANCE_RECEIVED player=" +
                   std::to_string(state.player_id) +
                   " character_id=" + state.character_id +
                   " customization_skin=" + state.customization_skin +
                   " customization_face=" + state.customization_face);
      const auto old_character =
          remote.appearance ? remote.appearance->character_id : std::string{};
      const auto new_character = state.character_id;
      if (!old_character.empty() && old_character != new_character) {
        logger_.info("REMOTE_CHARACTER_CHANGED player=" +
                     std::to_string(state.player_id) + " old=" + old_character +
                     " new=" + new_character);
        destroy_remote_actor(state.player_id, remote, false);
        remote.character_respawn_pending = true;
      }
      remote.appearance = state;
      remote.expected_body_mesh = nullptr;
      remote.expected_hair_mesh = nullptr;
      remote.body_verification_pending = false;
      remote.hair_verification_pending = false;
      remote.appearance_dirty = true;
      remote.appearance_attempt_count = 0;
      remote.next_appearance_retry = {};
      remote.fallback_warning_logged = false;
      break;
    }
    case protocol::MessageType::player_context_state: {
      const auto state = protocol::decode_player_context_state(frame.payload);
      if (state.player_id == local_player_id_ ||
          !protocol::is_valid_player_context(state.context))
        break;
      auto &remote = remotes_[state.player_id];
      const auto previous = remote.context;
      if (logic::context_requires_actor_reset(previous, state.context)) {
        destroy_remote_actor(state.player_id, remote, true);
        remote.context = state.context;
        logger_.info(
            "REMOTE_CONTEXT_CHANGED player=" + std::to_string(state.player_id) +
            " old=" + protocol::player_context_name(previous) +
            " new=" + protocol::player_context_name(state.context) +
            " interpolation_reset=true");
      }
      break;
    }
    case protocol::MessageType::movement_state: {
      const auto state = protocol::decode_movement_state(frame.payload);
      if (state.player_id == local_player_id_)
        break;
      if (state.movement_mode > 6) {
        logger_.warning("REMOTE_MOVEMENT_STATE_REJECTED player=" +
                        std::to_string(state.player_id) +
                        " mode=" + std::to_string(state.movement_mode));
        break;
      }
      auto &remote = remotes_[state.player_id];
      remote.movement_state = state;
      remote.movement_state_dirty = true;
      break;
    }
    case protocol::MessageType::player_locomotion_state: {
      const auto state =
          protocol::decode_player_locomotion_state(frame.payload);
      if (state.player_id == local_player_id_ || state.movement_mode > 6 ||
          !std::isfinite(state.aim_pitch) ||
          std::fabs(state.aim_pitch) > 180.0F)
        break;
      auto &remote = remotes_[state.player_id];
      remote.locomotion_state = state;
      remote.locomotion_state_dirty = true;
      break;
    }
    case protocol::MessageType::jump_event: {
      const auto event = protocol::decode_jump_event(frame.payload);
      if (event.player_id == local_player_id_ || event.sequence == 0)
        break;
      auto &remote = remotes_[event.player_id];
      if (event.sequence <= remote.last_jump_sequence_received)
        break;
      remote.last_jump_sequence_received = event.sequence;
      remote.jump_events.push_back(event);
      while (remote.jump_events.size() > 8)
        remote.jump_events.pop_front();
      logger_.info(
          "REMOTE_JUMP_RECEIVED player=" + std::to_string(event.player_id) +
          " sequence=" + std::to_string(event.sequence));
      break;
    }
    case protocol::MessageType::transform_snapshot: {
      const auto state = protocol::decode_transform_snapshot(frame.payload);
      if (state.player_id == local_player_id_)
        break;
      auto &remote = remotes_[state.player_id];
      const auto advances_stream =
          remote.snapshots.empty() ||
          state.timestamp_ms >= remote.snapshots.back().timestamp_ms;
      if (advances_stream && !remote.snapshots.empty() &&
          logic::snapshot_exceeds_teleport_threshold(
              remote.snapshots.back(), state,
              config_.teleport_threshold_units)) {
        const auto previous = remote.snapshots.back();
        remote.snapshots.clear();
        remote.last_rendered_transform = state;
        remote.velocity_x = 0.0F;
        remote.velocity_y = 0.0F;
        remote.velocity_z = 0.0F;
        remote.speed = 0.0F;
        remote.clock_offset_initialized = false;
        logger_.info(
            "REMOTE_TELEPORT player=" + std::to_string(state.player_id) +
            " from=" + std::to_string(previous.x) + "," +
            std::to_string(previous.y) + "," + std::to_string(previous.z) +
            " to=" + std::to_string(state.x) + "," + std::to_string(state.y) +
            "," + std::to_string(state.z) +
            " threshold=" + std::to_string(config_.teleport_threshold_units));
      }
      logic::insert_snapshot(remote.snapshots, state);
      if (!advances_stream)
        break;
      const auto received_at_ms = wall_clock_ms();
      remote.last_transform_received_ms = received_at_ms;
      const auto offset_sample = static_cast<double>(received_at_ms) -
                                 static_cast<double>(state.timestamp_ms);
      if (!remote.clock_offset_initialized) {
        remote.clock_offset_ms = offset_sample;
        remote.clock_offset_initialized = true;
      } else {
        remote.clock_offset_ms =
            remote.clock_offset_ms * 0.9 + offset_sample * 0.1;
      }
      remote.velocity_x = 0.0F;
      remote.velocity_y = 0.0F;
      remote.velocity_z = 0.0F;
      remote.speed = 0.0F;
      if (remote.snapshots.size() >= 2) {
        const auto &previous = remote.snapshots[remote.snapshots.size() - 2];
        const auto &current = remote.snapshots.back();
        const auto delta_seconds =
            static_cast<float>(current.timestamp_ms - previous.timestamp_ms) /
            1000.0F;
        if (delta_seconds > 0.0F && delta_seconds <= 2.0F) {
          remote.velocity_x = (current.x - previous.x) / delta_seconds;
          remote.velocity_y = (current.y - previous.y) / delta_seconds;
          remote.velocity_z = (current.z - previous.z) / delta_seconds;
          remote.speed = horizontal_speed(remote.velocity_x, remote.velocity_y,
                                          remote.velocity_z);
          if (remote.speed < 1.0F) {
            remote.velocity_x = 0.0F;
            remote.velocity_y = 0.0F;
            remote.velocity_z = 0.0F;
            remote.speed = 0.0F;
          }
        }
      }
      break;
    }
    case protocol::MessageType::player_left:
      destroy_remote(protocol::decode_player_left(frame.payload).player_id);
      break;
    case protocol::MessageType::error: {
      const auto error = protocol::decode_error(frame.payload);
      logger_.error("SERVER_ERROR code=" + std::to_string(error.code) +
                    " message=" + error.message);
      break;
    }
    default:
      break;
    }
  }
}

auto GameBridge::update_local_player() -> void {
  auto *pawn = find_local_pawn();
  if (detected_context_ != local_context_) {
    const auto previous = local_context_;
    for (auto &[player_id, remote] : remotes_)
      destroy_remote_actor(player_id, remote, true);
    local_context_ = detected_context_;
    resync_requested_ = true;
    logger_.info(std::string("LOCAL_CONTEXT old=") +
                 protocol::player_context_name(previous) +
                 " new=" + protocol::player_context_name(local_context_) +
                 " interpolation_reset=true");
  }
  if (!pawn) {
    local_visual_pawn_ = nullptr;
    local_body_component_ = nullptr;
    local_hair_component_ = nullptr;
    local_movement_component_ = nullptr;
    local_visual_route_diagnostic_logged_ = false;
    local_movement_state_initialized_ = false;
    local_movement_state_.reset();
    local_locomotion_state_.reset();
    last_local_movement_signature_.clear();
    local_jump_signals_.clear();
    local_jump_objects_.clear();
    local_skin_objects_.clear();
    local_skin_signals_.clear();
    local_jump_event_times_.clear();
    next_local_jump_scan_ = {};
    if (exploration_available_) {
      exploration_available_ = false;
      logger_.info("EXPLORATION_UNAVAILABLE Pawn=nil; combat synchronization "
                   "is intentionally disabled");
    }
    const auto had_zone = !local_zone_.empty();
    local_zone_.clear();
    if (network_.connected() && local_player_id_ != 0 &&
        (resync_requested_ || had_zone || !last_sent_context_ ||
         *last_sent_context_ != local_context_)) {
      network_.enqueue(protocol::make_frame(
          protocol::MessageType::zone_state, network_.next_sequence(),
          protocol::ZoneState{local_player_id_, {}}));
      network_.enqueue(protocol::make_frame(
          protocol::MessageType::player_context_state, network_.next_sequence(),
          protocol::PlayerContextState{local_player_id_, local_context_}));
      last_sent_context_ = local_context_;
      resync_requested_ = false;
    }
    return;
  }

  if (!exploration_available_) {
    exploration_available_ = true;
    logger_.info("EXPLORATION_READY pawn=" + object_name(pawn));
  }

  const auto zone = current_zone(pawn);
  if (zone.empty())
    return;
  if (zone != local_zone_) {
    destroy_all_remotes();
    local_zone_ = zone;
    resync_requested_ = true;
    logger_.info("LOCAL_ZONE " + local_zone_);
  }

  if (network_.connected() && resync_requested_) {
    network_.enqueue(protocol::make_frame(
        protocol::MessageType::zone_state, network_.next_sequence(),
        protocol::ZoneState{local_player_id_, local_zone_}));
  }
  if (network_.connected() && (resync_requested_ || !last_sent_context_ ||
                               *last_sent_context_ != local_context_)) {
    network_.enqueue(protocol::make_frame(
        protocol::MessageType::player_context_state, network_.next_sequence(),
        protocol::PlayerContextState{local_player_id_, local_context_}));
    last_sent_context_ = local_context_;
  }

  const auto now = std::chrono::steady_clock::now();
  if (local_visual_pawn_ != pawn) {
    appearance_failure_count_ = 0;
    next_appearance_capture_ = {};
    local_movement_component_ = nullptr;
    local_movement_state_initialized_ = false;
    local_movement_state_.reset();
    local_locomotion_state_.reset();
    last_local_movement_signature_.clear();
    local_jump_objects_.clear();
    local_skin_objects_.clear();
    local_skin_signals_.clear();
  }
  bool appearance_changed{};
  if (local_context_ == protocol::PlayerContext::exploration &&
      now >= next_appearance_capture_) {
    const auto candidate = capture_appearance(pawn);
    const auto effective =
        logic::select_effective_appearance(local_appearance_, candidate);
    if (logic::appearance_is_ready(candidate)) {
      appearance_failure_count_ = 0;
      next_appearance_capture_ = now + std::chrono::seconds(1);
      appearance_changed =
          !local_appearance_ || !same_appearance(*local_appearance_, candidate);
      local_appearance_ = effective;
    } else {
      ++appearance_failure_count_;
      next_appearance_capture_ =
          now + std::chrono::milliseconds(logic::appearance_retry_delay_ms(
                    appearance_failure_count_));
      if (now >= next_appearance_pending_log_) {
        next_appearance_pending_log_ = now + std::chrono::seconds(2);
        logger_.info("LOCAL_APPEARANCE_PENDING reason=vanilla_ids_not_ready");
      }
    }
  }
  if (network_.connected() && local_appearance_ &&
      (resync_requested_ || appearance_changed)) {
    network_.enqueue(
        protocol::make_frame(protocol::MessageType::appearance_state,
                             network_.next_sequence(), *local_appearance_));
    logger_.info(
        "LOCAL_APPEARANCE character_id=" + local_appearance_->character_id +
        " customization_skin=" + local_appearance_->customization_skin +
        " customization_face=" + local_appearance_->customization_face);
    logger_.info("LOCAL_CHARACTER_ID value=" + local_appearance_->character_id);
    logger_.info(
        "LOCAL_CUSTOMIZATION skin=" + local_appearance_->customization_skin +
        " face=" + local_appearance_->customization_face);
  }
  const auto location = pawn->K2_GetActorLocation();
  const auto rotation = pawn->K2_GetActorRotation();

  if (!object_is_valid(local_movement_component_)) {
    local_movement_component_ = find_remote_movement_component(pawn);
  }
  FVector local_velocity{};
  if (!call_vector_return(pawn, "GetVelocity", local_velocity)) {
    if (auto *value = vector_property(local_movement_component_, "Velocity"))
      local_velocity = *value;
  }
  const auto *local_movement_mode =
      byte_property(local_movement_component_, "MovementMode");
  const auto *local_custom_movement_mode =
      byte_property(local_movement_component_, "CustomMovementMode");
  bool local_is_falling{};
  const auto has_local_is_falling = call_bool_return(
      local_movement_component_, "IsFalling", local_is_falling);
  const auto vertical_phase = logic::classify_vertical_movement(
      has_local_is_falling && local_is_falling, local_velocity.Z());
  const auto movement_mode_text = local_movement_mode
                                      ? std::to_string(*local_movement_mode)
                                      : "unavailable";
  const auto is_falling_text = has_local_is_falling
                                   ? (local_is_falling ? "true" : "false")
                                   : "unavailable";
  const auto movement_signature =
      movement_mode_text + '|' + is_falling_text + '|' +
      std::string(logic::vertical_movement_phase_name(vertical_phase));
  if (!local_movement_state_initialized_ ||
      movement_signature != last_local_movement_signature_) {
    local_movement_state_initialized_ = true;
    last_local_movement_signature_ = movement_signature;
    logger_.info(
        "LOCAL_MOVEMENT_STATE movement_mode=" + movement_mode_text +
        " is_falling=" + is_falling_text + " phase=" +
        std::string(logic::vertical_movement_phase_name(vertical_phase)) +
        " velocity=" + std::to_string(local_velocity.X()) + "," +
        std::to_string(local_velocity.Y()) + "," +
        std::to_string(local_velocity.Z()) + " speed=" +
        std::to_string(horizontal_speed(local_velocity.X(), local_velocity.Y(),
                                        local_velocity.Z())));
  }
  if (local_movement_mode) {
    protocol::MovementState current_movement{
        local_player_id_, *local_movement_mode,
        local_custom_movement_mode ? *local_custom_movement_mode
                                   : std::uint8_t{0}};
    const auto movement_changed = !local_movement_state_ ||
                                  local_movement_state_->movement_mode !=
                                      current_movement.movement_mode ||
                                  local_movement_state_->custom_movement_mode !=
                                      current_movement.custom_movement_mode;
    local_movement_state_ = current_movement;
    if (network_.connected() && (resync_requested_ || movement_changed)) {
      network_.enqueue(
          protocol::make_frame(protocol::MessageType::movement_state,
                               network_.next_sequence(), current_movement));
    }
  }
  if (config_.sync_locomotion_state) {
    const auto speed = horizontal_speed(local_velocity.X(), local_velocity.Y(),
                                        local_velocity.Z());
    const auto *reflected_locomotion = first_byte_property(
        pawn, {"LocomotionState", "CurrentLocomotionState", "MovementState"});
    const auto *reflected_gait =
        first_byte_property(pawn, {"DesiredGait", "Gait", "ActualGait"});
    const auto *reflected_stance =
        first_byte_property(pawn, {"DesiredStance", "Stance", "ActualStance"});
    const auto *reflected_sprinting = first_bool_property(
        pawn, {"bIsSprinting", "IsSprinting", "bSprinting"});
    const auto *reflected_crouching =
        first_bool_property(pawn, {"bIsCrouched", "IsCrouching", "bCrouching"});
    auto *free_aim = object_property(pawn, "BP_FreeAimControlComponent");
    if (!object_is_valid(free_aim))
      free_aim = object_property(pawn, "FreeAimControlComponent");
    bool aiming{};
    if (config_.sync_aim && object_is_valid(free_aim))
      (void)call_bool_return_validated(free_aim, "IsInFreeAimMode", aiming);
    float aim_pitch{};
    if (aiming) {
      if (const auto *aim_rotation =
              rotator_property(free_aim, "CachedAimingRotation"))
        aim_pitch = std::clamp(static_cast<float>(aim_rotation->GetPitch()),
                               -180.0F, 180.0F);
    }
    const auto fallback_locomotion =
        has_local_is_falling && local_is_falling
            ? std::uint8_t{2}
            : (speed >= 1.0F ? std::uint8_t{1} : std::uint8_t{0});
    const auto fallback_gait = speed < 1.0F     ? std::uint8_t{0}
                               : speed < 300.0F ? std::uint8_t{1}
                               : speed < 600.0F ? std::uint8_t{2}
                                                : std::uint8_t{3};
    const auto crouching =
        config_.sync_crouch && reflected_crouching && *reflected_crouching;
    protocol::PlayerLocomotionState current_locomotion{
        local_player_id_,
        local_movement_mode ? *local_movement_mode : std::uint8_t{0},
        reflected_locomotion ? *reflected_locomotion : fallback_locomotion,
        config_.sync_gait && reflected_gait ? *reflected_gait : fallback_gait,
        config_.sync_crouch && reflected_stance
            ? *reflected_stance
            : (crouching ? std::uint8_t{1} : std::uint8_t{0}),
        reflected_sprinting ? *reflected_sprinting : fallback_gait == 3,
        crouching,
        aiming,
        aim_pitch};
    const auto locomotion_changed =
        !local_locomotion_state_ ||
        !same_locomotion(*local_locomotion_state_, current_locomotion);
    local_locomotion_state_ = current_locomotion;
    if (network_.connected() && (resync_requested_ || locomotion_changed)) {
      network_.enqueue(
          protocol::make_frame(protocol::MessageType::player_locomotion_state,
                               network_.next_sequence(), current_locomotion));
      logger_.info(
          "LOCAL_LOCOMOTION movement_mode=" +
          std::to_string(current_locomotion.movement_mode) +
          " locomotion_state=" +
          std::to_string(current_locomotion.locomotion_state) +
          " gait=" + std::to_string(current_locomotion.gait) +
          " stance=" + std::to_string(current_locomotion.stance) +
          " sprinting=" + (current_locomotion.sprinting ? "true" : "false") +
          " crouching=" + (current_locomotion.crouching ? "true" : "false") +
          " aiming=" + (current_locomotion.aiming ? "true" : "false") +
          " aim_pitch=" + std::to_string(current_locomotion.aim_pitch));
    }
  }
  update_local_jump_diagnostics(pawn);
  resync_requested_ = false;
  if (now >= next_local_transform_log_) {
    next_local_transform_log_ = now + std::chrono::seconds(2);
    logger_.info("LOCAL_TRANSFORM x=" + std::to_string(location.X()) +
                 " y=" + std::to_string(location.Y()) +
                 " z=" + std::to_string(location.Z()) +
                 " yaw=" + std::to_string(rotation.GetYaw()));
  }

  if (!network_.connected() || now < next_snapshot_)
    return;
  next_snapshot_ = now + std::chrono::milliseconds(1000 / config_.snapshot_hz);

  const auto timestamp = static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::system_clock::now().time_since_epoch())
          .count());
  protocol::TransformSnapshot snapshot;
  snapshot.player_id = local_player_id_;
  snapshot.timestamp_ms = timestamp;
  snapshot.x = location.X();
  snapshot.y = location.Y();
  snapshot.z = location.Z();
  snapshot.pitch = rotation.GetPitch();
  snapshot.yaw = rotation.GetYaw();
  snapshot.roll = rotation.GetRoll();
  network_.enqueue(
      protocol::make_frame(protocol::MessageType::transform_snapshot,
                           network_.next_sequence(), snapshot));
}

auto GameBridge::update_remote_players() -> void {
  if (!exploration_available_ || local_zone_.empty() ||
      !logic::context_supports_remote_actor(local_context_))
    return;
  for (auto &[player_id, remote] : remotes_) {
    if (remote.zone != local_zone_ || remote.snapshots.empty() ||
        remote.context != local_context_ ||
        !logic::context_supports_remote_actor(remote.context)) {
      if (object_is_valid(remote.actor))
        destroy_remote_actor(player_id, remote, false);
      continue;
    }
    if (!ensure_remote_actor(player_id, remote))
      continue;
    configure_remote_network_authority(player_id, remote);
    if (remote.appearance_dirty)
      apply_remote_appearance(player_id, remote);
    if (remote.movement_state_dirty)
      apply_remote_movement_state(player_id, remote);
    if (remote.locomotion_state_dirty)
      apply_remote_locomotion_state(player_id, remote);
    apply_remote_jump_events(player_id, remote);
    apply_remote_transform(player_id, remote);
    verify_remote_visual_state(player_id, remote);
  }
}

auto GameBridge::find_local_pawn() -> AActor * {
  detected_context_ = protocol::PlayerContext::unavailable;
  const auto controller_name = widen(config_.controller_class);
  auto *controller = UObjectGlobals::FindFirstOf(controller_name.c_str());
  auto *pawn = object_property(controller, config_.pawn_property);
  if (object_is_valid(pawn)) {
    detected_context_ = protocol::PlayerContext::exploration;
    return static_cast<AActor *>(pawn);
  }
  if (config_.world_map_remote) {
    const auto world_map_controller = widen(config_.world_map_controller_class);
    controller = UObjectGlobals::FindFirstOf(world_map_controller.c_str());
    pawn = object_property(controller, config_.pawn_property);
    if (object_is_valid(pawn)) {
      detected_context_ = protocol::PlayerContext::world_map;
      return static_cast<AActor *>(pawn);
    }
  }
  return nullptr;
}

auto GameBridge::current_zone(AActor *pawn) -> std::string {
  if (!object_is_valid(pawn))
    return {};
  if (auto *level = pawn->GetLevel(); object_is_valid(level)) {
    if (auto *package = level->GetOutermost(); object_is_valid(package))
      return narrow(package->GetFullName());
    return narrow(level->GetFullName());
  }
  if (auto *world = pawn->GetWorld(); object_is_valid(world))
    return narrow(world->GetFullName());
  return {};
}

auto GameBridge::update_local_jump_diagnostics(AActor *pawn) -> void {
  const auto now = std::chrono::steady_clock::now();
  if (!object_is_valid(pawn) || now < next_local_jump_scan_)
    return;
  next_local_jump_scan_ = now + std::chrono::milliseconds(50);

  std::unordered_map<UObject *, std::string> objects;
  objects.emplace(pawn, "pawn");
  if (object_is_valid(local_movement_component_))
    objects.emplace(local_movement_component_, "movement_component");
  std::unordered_set<UObject *> candidate_meshes;
  if (object_is_valid(local_body_component_))
    candidate_meshes.emplace(local_body_component_);
  if (auto *mesh = object_property(pawn, "Mesh");
      is_skeletal_mesh_component(mesh))
    candidate_meshes.emplace(mesh);
  if (auto *mesh = find_owned_skeletal_component(pawn, "CharacterMesh0");
      is_skeletal_mesh_component(mesh))
    candidate_meshes.emplace(mesh);
  for (auto *component : candidate_meshes) {
    auto *anim_instance = call_object_return(component, "GetAnimInstance");
    if (is_locomotion_anim_instance(anim_instance)) {
      objects.emplace(anim_instance, "locomotion_anim_instance:" +
                                         object_leaf_name(component));
    }
  }
  local_jump_objects_ = objects;

  std::unordered_map<UObject *, std::string> skin_objects;
  auto *actor_component_class = UObjectGlobals::StaticFindObject<UClass *>(
      nullptr, nullptr, L"/Script/Engine.ActorComponent");
  if (object_is_valid(actor_component_class)) {
    for (const auto &reachable : collect_reachable_visual_actors(pawn)) {
      if (!object_is_valid(reachable.actor))
        continue;
      for (auto *component :
           reachable.actor->K2_GetComponentsByClass(actor_component_class)) {
        if (!is_character_skin_component(component))
          continue;
        skin_objects.emplace(component, "BP_CharacterSkinComponent");
        if (auto *policy = object_property(component, "SkinAssignPolicy");
            is_skin_runtime_object(policy)) {
          skin_objects.emplace(policy, "CSAP_SwapAssign");
        }
      }
    }
  }
  local_skin_objects_ = skin_objects;

  std::unordered_map<std::string, std::string> current;
  for (const auto &[object, role] : objects) {
    auto values = read_filtered_jump_signals(object, role);
    current.insert(values.begin(), values.end());
  }

  const auto log_change = [&](const std::string &key,
                              const std::string &old_value,
                              const std::string &new_value) {
    const auto first = key.find('|');
    const auto last = key.rfind('|');
    const auto role =
        first == std::string::npos ? "unknown" : key.substr(0, first);
    const auto owner = first == std::string::npos || last == first
                           ? "unknown"
                           : key.substr(first + 1, last - first - 1);
    const auto property =
        last == std::string::npos ? key : key.substr(last + 1);
    std::string folded = property;
    std::transform(folded.begin(), folded.end(), folded.begin(),
                   [](unsigned char value) {
                     return static_cast<char>(std::tolower(value));
                   });
    const auto prefix = folded.find("land") != std::string::npos
                            ? "LOCAL_LAND_SIGNAL"
                            : "LOCAL_JUMP_SIGNAL";
    logger_.info(std::string(prefix) + " owner=" + owner + " role=" + role +
                 " property=" + property + " old=" + old_value +
                 " new=" + new_value);
  };

  const auto jump_baseline = local_jump_signals_.empty();
  for (const auto &[key, value] : current) {
    const auto old = local_jump_signals_.find(key);
    if (old == local_jump_signals_.end() && !jump_baseline) {
      log_change(key, "<unset>", value);
    } else if (old->second != value) {
      log_change(key, old->second, value);
    }
  }
  for (const auto &[key, old_value] : local_jump_signals_) {
    if (!current.contains(key))
      log_change(key, old_value, "<unreachable>");
  }
  local_jump_signals_ = std::move(current);

  std::unordered_map<std::string, std::string> current_skin;
  for (const auto &[object, role] : skin_objects) {
    auto values = read_filtered_skin_signals(object, role);
    current_skin.insert(values.begin(), values.end());
  }
  const auto skin_baseline = local_skin_signals_.empty();
  for (const auto &[key, value] : current_skin) {
    const auto old = local_skin_signals_.find(key);
    if (old != local_skin_signals_.end() && old->second != value) {
      const auto first = key.find('|');
      const auto last = key.rfind('|');
      logger_.info("LOCAL_SKIN_PROPERTY_CHANGE owner=" +
                   key.substr(first + 1, last - first - 1) + " role=" +
                   key.substr(0, first) + " property=" + key.substr(last + 1) +
                   " old=" + old->second + " new=" + value);
    } else if (!skin_baseline && old == local_skin_signals_.end()) {
      logger_.info("LOCAL_SKIN_PROPERTY_CHANGE owner=new property=" + key +
                   " old=<unset> new=" + value);
    }
  }
  local_skin_signals_ = std::move(current_skin);
}

auto GameBridge::capture_appearance(AActor *pawn) -> protocol::AppearanceState {
  const auto scan_started = std::chrono::steady_clock::now();
  if (local_visual_pawn_ != pawn) {
    local_visual_pawn_ = pawn;
    local_body_component_ = nullptr;
    local_hair_component_ = nullptr;
    last_body_component_log_.clear();
    last_body_mesh_log_.clear();
    last_body_source_log_.clear();
    body_diagnostic_initialized_ = false;
    local_visual_route_diagnostic_logged_ = false;
  }

  // This is intentionally bounded to components owned by the local Pawn.
  // Global UObject scans during level streaming can stall the game thread.
  const auto body_selection = select_local_body_component(pawn, &logger_);
  local_body_component_ = body_selection.component;
  if (!object_is_valid(local_hair_component_)) {
    auto *hair_property =
        object_property(pawn, config_.hair_component_property);
    local_hair_component_ = is_skeletal_mesh_component(hair_property)
                                ? hair_property
                                : find_owned_skeletal_component(
                                      pawn, config_.hair_component_property);
  }

  std::string vanilla_reason;
  auto appearance =
      capture_vanilla_appearance(local_player_id_, vanilla_reason);
  auto *body_mesh = body_selection.mesh;
  if (appearance.character_id.empty() &&
      std::chrono::steady_clock::now() >= next_appearance_pending_log_) {
    logger_.info("LOCAL_VANILLA_APPEARANCE_PENDING reason=" + vanilla_reason);
  }

  const auto body_component_log = object_is_valid(local_body_component_)
                                      ? object_name(local_body_component_)
                                      : "nil";
  const auto body_mesh_log =
      object_is_valid(body_mesh) ? object_name(body_mesh) : "nil";
  if (!body_diagnostic_initialized_ ||
      body_component_log != last_body_component_log_ ||
      body_selection.source != last_body_source_log_) {
    logger_.info("APPEARANCE_BODY_COMPONENT component=" + body_component_log +
                 " route=" + body_selection.source);
    last_body_component_log_ = body_component_log;
  }
  if (!body_diagnostic_initialized_ || body_mesh_log != last_body_mesh_log_) {
    logger_.info("APPEARANCE_BODY_MESH mesh=" + body_mesh_log);
    last_body_mesh_log_ = body_mesh_log;
  }
  body_diagnostic_initialized_ = true;

  if (!object_is_valid(body_selection.component)) {
    last_body_source_log_.clear();
    if (!local_visual_route_diagnostic_logged_) {
      local_visual_route_diagnostic_logged_ = true;
      log_local_visual_diagnostic(logger_, pawn, body_selection);
    }
  } else {
    local_visual_route_diagnostic_logged_ = false;
    if (body_selection.source != last_body_source_log_) {
      logger_.info("APPEARANCE_VISUAL_ROUTE source=" + body_selection.source +
                   " component=" + object_name(body_selection.component));
    }
    last_body_source_log_ = body_selection.source;
  }

  const auto scan_finished = std::chrono::steady_clock::now();
  const auto duration_us =
      std::chrono::duration_cast<std::chrono::microseconds>(scan_finished -
                                                            scan_started)
          .count();
  if (scan_finished >= next_appearance_scan_log_) {
    next_appearance_scan_log_ = scan_finished + std::chrono::seconds(2);
    const auto message =
        "APPEARANCE_SCAN duration_us=" + std::to_string(duration_us) +
        " candidates=" + std::to_string(body_selection.candidates) +
        " source=" + body_selection.source;
    if (duration_us > 5000) {
      logger_.warning(message);
    } else {
      logger_.info(message);
    }
  }
  return appearance;
}

auto GameBridge::ensure_remote_actor(std::uint64_t player_id,
                                     RemotePlayer &remote) -> AActor * {
  if (object_is_valid(remote.actor))
    return remote.actor;
  auto *local_pawn = find_local_pawn();
  if (!local_pawn || remote.snapshots.empty())
    return nullptr;

  const auto wants_world_map =
      remote.context == protocol::PlayerContext::world_map;
  auto use_legacy =
      !wants_world_map && config_.remote_actor_mode == "ai_companion_legacy";
  if (!wants_world_map && !remote.appearance)
    return nullptr;
  std::string class_spec = wants_world_map ? config_.world_map_character_class
                                           : config_.world_character_class;
  std::string mapped_character =
      remote.appearance ? remote.appearance->character_id : std::string{};
  if (use_legacy) {
    class_spec = config_.default_companion_class;
    if (const auto found =
            config_.companion_by_character.find(mapped_character);
        found != config_.companion_by_character.end())
      class_spec = found->second;
  }

  const auto resolve_class = [&](const std::string &spec) -> UClass * {
    if (!spec.empty() && spec.front() == '/') {
      const auto class_path = widen(spec);
      if (auto *found = UObjectGlobals::StaticFindObject<UClass *>(
              nullptr, nullptr, class_path.c_str());
          object_is_valid(found))
        return found;
      const auto expected_leaf = class_leaf(spec);
      if (object_name(local_pawn->GetClassPrivate()).find(expected_leaf) !=
          std::string::npos)
        return local_pawn->GetClassPrivate();
      return nullptr;
    }
    const auto short_name = widen(spec);
    if (auto *template_actor = UObjectGlobals::FindFirstOf(short_name.c_str());
        object_is_valid(template_actor))
      return template_actor->GetClassPrivate();
    return nullptr;
  };

  auto *actor_class = resolve_class(class_spec);
  if (!actor_class && !wants_world_map && !use_legacy &&
      config_.fallback_ai_companion) {
    use_legacy = true;
    class_spec = config_.default_companion_class;
    if (const auto found =
            config_.companion_by_character.find(mapped_character);
        found != config_.companion_by_character.end())
      class_spec = found->second;
    actor_class = resolve_class(class_spec);
    logger_.warning(
        "REMOTE_BACKEND_FALLBACK player=" + std::to_string(player_id) +
        " from=world_character to=ai_companion_legacy reason="
        "world_character_class_unavailable");
  }
  if (!actor_class) {
    logger_.warning("REMOTE_SPAWN_WAIT player=" + std::to_string(player_id) +
                    " class=" + class_spec);
    return nullptr;
  }

  auto *world = local_pawn->GetWorld();
  if (!object_is_valid(world))
    return nullptr;
  const auto &state = remote.last_rendered_transform
                          ? *remote.last_rendered_transform
                          : remote.snapshots.back();
  FVector location(state.x, state.y, state.z);
  FRotator rotation(state.pitch, state.yaw, state.roll);

#if EXPEDITION_HAS_UE4SS_REFLECTION
  struct CurrentCharacterGuard {
    RC::Unreal::FName *property{};
    RC::Unreal::FName previous{};
    ~CurrentCharacterGuard() {
      if (property)
        *property = previous;
    }
  } character_guard;
  if (!use_legacy && !wants_world_map && remote.appearance) {
    auto *game_instance = UObjectGlobals::FindFirstOf(L"BP_jRPG_GI_Custom_C");
    character_guard.property =
        reflected_fname_property(game_instance, "CurrentCharacterWorld");
    if (!character_guard.property) {
      logger_.warning("REMOTE_SPAWN_WAIT player=" + std::to_string(player_id) +
                      " class=" + class_spec +
                      " reason=CurrentCharacterWorld_not_validated");
      return nullptr;
    }
    character_guard.previous = *character_guard.property;
    *character_guard.property = RC::Unreal::FName(
        widen(remote.appearance->character_id).c_str(), RC::Unreal::FNAME_Add);
  }
#else
  if (!use_legacy && !wants_world_map) {
    logger_.warning("REMOTE_SPAWN_WAIT player=" + std::to_string(player_id) +
                    " class=" + class_spec + " reason=reflection_unavailable");
    return nullptr;
  }
#endif
  remote.actor = world->SpawnActor(actor_class, &location, &rotation);
  if (!object_is_valid(remote.actor)) {
    remote.actor = nullptr;
    logger_.warning("REMOTE_SPAWN_FAILED player=" + std::to_string(player_id) +
                    " class=" + class_spec);
    return nullptr;
  }

  disable_remote_ai(remote.actor);
  remote.movement_component = find_remote_movement_component(remote.actor);
  remote.body_component = nullptr;
  remote.hair_component = nullptr;
  remote.expected_body_mesh = nullptr;
  remote.expected_hair_mesh = nullptr;
  remote.locomotion_anim_instance = nullptr;
  remote.skin_component = nullptr;
  remote.body_route.clear();
  remote.hair_route.clear();
  remote.spawned_character = mapped_character;
  remote.backend = wants_world_map ? "world_map"
                                   : (use_legacy ? "ai_companion_legacy"
                                                 : "world_character");
  remote.visual_mesh_snapshot.clear();
  remote.visual_snapshot_initialized = false;
  remote.body_verification_pending = false;
  remote.hair_verification_pending = false;
  remote.movement_warning_logged = false;
  remote.jump_target_warning_logged = false;
  remote.network_authority_configured = false;
  remote.transform_drift_initialized = false;
  remote.skeletal_diagnostic_logged = false;
  remote.appearance_attempt_count = 0;
  remote.next_appearance_retry = {};
  remote.next_visual_verification = {};
  auto *mesh_component = object_property(remote.actor, "Mesh");
  if (!is_skeletal_mesh_component(mesh_component)) {
    mesh_component = find_owned_skeletal_component(
        remote.actor, config_.body_component_property);
  }
  auto *anim_instance = call_object_return(mesh_component, "GetAnimInstance");
  if (is_locomotion_anim_instance(anim_instance))
    remote.locomotion_anim_instance = anim_instance;
  if (!object_is_valid(remote.locomotion_anim_instance))
    remote.locomotion_anim_instance =
        find_locomotion_anim_instance(remote.actor, mesh_component);
  logger_.info(
      "REMOTE_MOTION_SETUP player=" + std::to_string(player_id) +
      " movement_component=" + object_name(remote.movement_component) +
      " movement_class=" +
      object_name(object_is_valid(remote.movement_component)
                      ? remote.movement_component->GetClassPrivate()
                      : nullptr) +
      " anim_instance_class=" +
      object_name(object_is_valid(remote.locomotion_anim_instance)
                      ? remote.locomotion_anim_instance->GetClassPrivate()
                      : nullptr));
  remote.appearance_dirty = true;
  remote.movement_state_dirty = remote.movement_state.has_value();
  remote.locomotion_state_dirty = remote.locomotion_state.has_value();
  logger_.info("REMOTE_SPAWNED player=" + std::to_string(player_id) +
               " backend=" + remote.backend +
               " actor=" + object_name(remote.actor));
  if (remote.character_respawn_pending) {
    remote.character_respawn_pending = false;
    logger_.info(
        "REMOTE_CHARACTER_RESPAWN player=" + std::to_string(player_id) +
        " character=" +
        (remote.appearance ? remote.appearance->character_id : "Unknown") +
        " actor=" + object_name(remote.actor));
  }
  return remote.actor;
}

auto GameBridge::configure_remote_network_authority(std::uint64_t player_id,
                                                    RemotePlayer &remote)
    -> void {
  if (remote.network_authority_configured || !object_is_valid(remote.actor))
    return;
  if (!object_is_valid(remote.movement_component))
    remote.movement_component = find_remote_movement_component(remote.actor);
  if (!object_is_valid(remote.movement_component))
    return;

  const auto disable_movement_tick = logic::should_disable_remote_movement_tick(
      config_.remote_network_authority);
  auto policy_applied = true;
  if (disable_movement_tick) {
    policy_applied = call_bool_input(remote.movement_component,
                                     "SetComponentTickEnabled", false);
  }

  bool movement_tick{};
  const auto has_movement_tick = call_bool_return(
      remote.movement_component, "IsComponentTickEnabled", movement_tick);
  bool actor_tick{};
  const auto has_actor_tick =
      call_bool_return(remote.actor, "IsActorTickEnabled", actor_tick);
  auto *mesh_component = object_property(remote.actor, "Mesh");
  if (!is_skeletal_mesh_component(mesh_component))
    mesh_component =
        find_owned_skeletal_component(remote.actor, "CharacterMesh0");
  bool mesh_tick{};
  const auto has_mesh_tick =
      call_bool_return(mesh_component, "IsComponentTickEnabled", mesh_tick);

  logger_.info(
      "REMOTE_NETWORK_AUTHORITY player=" + std::to_string(player_id) +
      " enabled=" + (config_.remote_network_authority ? "true" : "false") +
      " movement_tick_enabled=" +
      (has_movement_tick ? (movement_tick ? "true" : "false") : "unavailable") +
      " actor_tick_enabled=" +
      (has_actor_tick ? (actor_tick ? "true" : "false") : "unavailable") +
      " mesh_tick_enabled=" +
      (has_mesh_tick ? (mesh_tick ? "true" : "false") : "unavailable") +
      " policy_applied=" + (policy_applied ? "true" : "false"));
  if (disable_movement_tick && !policy_applied) {
    logger_.warning(
        "REMOTE_NETWORK_AUTHORITY player=" + std::to_string(player_id) +
        " reason=SetComponentTickEnabled_unavailable");
  }
  remote.network_authority_configured = true;
}

auto GameBridge::apply_remote_jump_events(std::uint64_t player_id,
                                          RemotePlayer &remote) -> void {
  if (!object_is_valid(remote.actor) || remote.jump_events.empty())
    return;
  auto *preferred_component = object_is_valid(remote.body_component)
                                  ? remote.body_component
                                  : object_property(remote.actor, "Mesh");
  if (!is_locomotion_anim_instance(remote.locomotion_anim_instance)) {
    remote.locomotion_anim_instance =
        find_locomotion_anim_instance(remote.actor, preferred_component);
  }
  if (!is_locomotion_anim_instance(remote.locomotion_anim_instance)) {
    if (!remote.jump_target_warning_logged) {
      remote.jump_target_warning_logged = true;
      logger_.warning(
          "REMOTE_JUMP_EVENT player=" + std::to_string(player_id) +
          " sequence=" + std::to_string(remote.jump_events.front().sequence) +
          " target=nil function=OnJumped success=false "
          "reason=locomotion_anim_instance_not_ready");
    }
    return;
  }

  while (!remote.jump_events.empty()) {
    const auto event = remote.jump_events.front();
    remote.jump_events.pop_front();
    if (event.sequence <= remote.last_jump_sequence_applied)
      continue;
    remote.last_jump_sequence_applied = event.sequence;
    const auto success =
        call_no_args(remote.locomotion_anim_instance, "OnJumped");
    logger_.info("REMOTE_JUMP_EVENT player=" + std::to_string(player_id) +
                 " sequence=" + std::to_string(event.sequence) +
                 " target=" + object_name(remote.locomotion_anim_instance) +
                 " function=OnJumped success=" + (success ? "true" : "false"));

    const auto *jumped =
        bool_property(remote.locomotion_anim_instance, "bJumped");
    const auto *movement_mode =
        byte_property(remote.movement_component, "MovementMode");
    bool is_falling{};
    const auto has_is_falling =
        call_bool_return(remote.movement_component, "IsFalling", is_falling);
    logger_.info(
        "REMOTE_JUMP_ANIM_STATE player=" + std::to_string(player_id) +
        " sequence=" + std::to_string(event.sequence) +
        " bJumped=" + (jumped ? (*jumped ? "true" : "false") : "unavailable") +
        " movement_mode=" +
        (movement_mode ? std::to_string(*movement_mode) : "unavailable") +
        " is_falling=" +
        (has_is_falling ? (is_falling ? "true" : "false") : "unavailable"));
  }
}

auto GameBridge::apply_remote_transform(std::uint64_t player_id,
                                        RemotePlayer &remote) -> void {
  if (!object_is_valid(remote.actor) || remote.snapshots.empty())
    return;

  const auto current_wall_ms = wall_clock_ms();
  const auto estimated_remote_now =
      remote.clock_offset_initialized
          ? static_cast<double>(current_wall_ms) - remote.clock_offset_ms
          : static_cast<double>(remote.snapshots.back().timestamp_ms);
  const auto delayed_render_time =
      estimated_remote_now -
      static_cast<double>(config_.interpolation_delay_ms);
  const auto render_timestamp_ms =
      delayed_render_time > 0.0
          ? static_cast<std::uint64_t>(delayed_render_time)
          : std::uint64_t{};
  const auto sample =
      logic::sample_snapshot(remote.snapshots, render_timestamp_ms);
  if (!sample)
    return;
  const auto &state = sample->transform;
  remote.last_rendered_transform = state;
  const auto &target = remote.snapshots.back();
  FVector location(state.x, state.y, state.z);
  FRotator rotation(state.pitch, state.yaw, state.roll);
  const auto now = std::chrono::steady_clock::now();
  const auto before = remote.actor->K2_GetActorLocation();
  if (remote.transform_drift_initialized) {
    const auto delta_x = before.X() - remote.last_after_x;
    const auto delta_y = before.Y() - remote.last_after_y;
    const auto delta_z = before.Z() - remote.last_after_z;
    const auto drift =
        std::sqrt(delta_x * delta_x + delta_y * delta_y + delta_z * delta_z);
    if (drift > 0.75F && now >= remote.next_transform_drift_log) {
      remote.next_transform_drift_log = now + std::chrono::seconds(2);
      logger_.warning(
          "REMOTE_TRANSFORM_DRIFT player=" + std::to_string(player_id) +
          " network=" + std::to_string(remote.last_network_x) + "," +
          std::to_string(remote.last_network_y) + "," +
          std::to_string(remote.last_network_z) +
          " before=" + std::to_string(remote.last_before_x) + "," +
          std::to_string(remote.last_before_y) + "," +
          std::to_string(remote.last_before_z) +
          " after=" + std::to_string(remote.last_after_x) + "," +
          std::to_string(remote.last_after_y) + "," +
          std::to_string(remote.last_after_z) + " next_before=" +
          std::to_string(before.X()) + "," + std::to_string(before.Y()) + "," +
          std::to_string(before.Z()) + " delta=" + std::to_string(drift));
    }
  }
  FHitResult hit_result{};
  remote.actor->K2_SetActorLocationAndRotation(location, rotation, false,
                                               hit_result, true);
  const auto after = remote.actor->K2_GetActorLocation();
  remote.last_network_x = state.x;
  remote.last_network_y = state.y;
  remote.last_network_z = state.z;
  remote.last_before_x = before.X();
  remote.last_before_y = before.Y();
  remote.last_before_z = before.Z();
  remote.last_after_x = after.X();
  remote.last_after_y = after.Y();
  remote.last_after_z = after.Z();
  remote.transform_drift_initialized = true;

  if (!object_is_valid(remote.movement_component)) {
    remote.movement_component = find_remote_movement_component(remote.actor);
  }

  const auto snapshot_stale = logic::snapshot_stream_is_stale(
      current_wall_ms, remote.last_transform_received_ms);
  const auto velocity_x = snapshot_stale ? 0.0F : remote.velocity_x;
  const auto velocity_y = snapshot_stale ? 0.0F : remote.velocity_y;
  const auto velocity_z = snapshot_stale ? 0.0F : remote.velocity_z;
  const auto speed = snapshot_stale ? 0.0F : remote.speed;
  if (auto *velocity = vector_property(remote.movement_component, "Velocity")) {
    *velocity = FVector(velocity_x, velocity_y, velocity_z);
  } else if (!remote.movement_warning_logged) {
    remote.movement_warning_logged = true;
    logger_.warning("REMOTE_MOVEMENT_COMPONENT_MISSING player=" +
                    std::to_string(player_id));
  }

  if (now >= remote.next_movement_component_state_log) {
    remote.next_movement_component_state_log = now + std::chrono::seconds(2);
    bool movement_tick{};
    const auto has_movement_tick = call_bool_return(
        remote.movement_component, "IsComponentTickEnabled", movement_tick);
    bool root_motion{};
    const auto has_root_motion = call_bool_return(
        remote.movement_component, "HasAnimRootMotion", root_motion);
    const auto *movement_mode =
        byte_property(remote.movement_component, "MovementMode");
    logger_.info(
        "REMOTE_MOVEMENT_COMPONENT_STATE player=" + std::to_string(player_id) +
        " tick_enabled=" +
        (has_movement_tick ? (movement_tick ? "true" : "false")
                           : "unavailable") +
        " movement_mode=" +
        (movement_mode ? std::to_string(*movement_mode) : "unavailable") +
        " velocity=" + std::to_string(velocity_x) + "," +
        std::to_string(velocity_y) + "," + std::to_string(velocity_z) +
        " root_motion=" +
        (has_root_motion ? (root_motion ? "true" : "false") : "unavailable"));
  }

  if (now >= remote.next_motion_log) {
    remote.next_motion_log = now + std::chrono::seconds(2);
    FVector observed_velocity{};
    if (!call_vector_return(remote.actor, "GetVelocity", observed_velocity)) {
      if (auto *observed =
              vector_property(remote.movement_component, "Velocity"))
        observed_velocity = *observed;
    }
    const auto *movement_mode =
        byte_property(remote.movement_component, "MovementMode");
    bool is_falling{};
    const auto has_is_falling =
        call_bool_return(remote.movement_component, "IsFalling", is_falling);
    logger_.info(
        "REMOTE_MOTION player=" + std::to_string(player_id) + " speed=" +
        std::to_string(speed) + " velocity=" + std::to_string(velocity_x) +
        "," + std::to_string(velocity_y) + "," + std::to_string(velocity_z) +
        " observed=" + std::to_string(observed_velocity.X()) + "," +
        std::to_string(observed_velocity.Y()) + "," +
        std::to_string(observed_velocity.Z()) + " movement_mode=" +
        (movement_mode ? std::to_string(*movement_mode) : "unavailable") +
        " is_falling=" +
        (has_is_falling ? (is_falling ? "true" : "false") : "unavailable"));
  }

  if (now >= remote.next_interpolation_log) {
    remote.next_interpolation_log = now + std::chrono::seconds(2);
    logger_.info("REMOTE_INTERPOLATION player=" + std::to_string(player_id) +
                 " buffer=" + std::to_string(remote.snapshots.size()) +
                 " delay_ms=" + std::to_string(config_.interpolation_delay_ms) +
                 " alpha=" + std::to_string(sample->alpha) +
                 " render_xyz=" + std::to_string(state.x) + "," +
                 std::to_string(state.y) + "," + std::to_string(state.z) +
                 " target_xyz=" + std::to_string(target.x) + "," +
                 std::to_string(target.y) + "," + std::to_string(target.z));
  }
}

auto GameBridge::apply_remote_appearance(std::uint64_t player_id,
                                         RemotePlayer &remote) -> void {
  if (!object_is_valid(remote.actor) || !remote.appearance)
    return;
  const auto now = std::chrono::steady_clock::now();
  if (now < remote.next_appearance_retry)
    return;
  if (!config_.vanilla_customization || remote.backend != "world_character") {
    remote.appearance_dirty = false;
    logger_.warning(
        "REMOTE_APPEARANCE_FAIL_OPEN player=" + std::to_string(player_id) +
        " reason=" +
        (!config_.vanilla_customization ? "vanilla_customization_disabled"
                                        : "backend_not_world_character") +
        " backend=" + remote.backend);
    return;
  }

  ++remote.appearance_attempt_count;
  std::string reason;
  if (apply_vanilla_customization(remote.actor, *remote.appearance, reason)) {
    remote.appearance_dirty = false;
    logger_.info(
        "REMOTE_CUSTOMIZATION_APPLIED player=" + std::to_string(player_id) +
        " character_id=" + remote.appearance->character_id +
        " customization_skin=" + remote.appearance->customization_skin +
        " customization_face=" + remote.appearance->customization_face +
        " route=SetCharacterCustomization reflection_validated=true");
    return;
  }

  if (remote.appearance_attempt_count < 10) {
    const auto delay_ms =
        logic::appearance_retry_delay_ms(remote.appearance_attempt_count);
    remote.next_appearance_retry = now + std::chrono::milliseconds(delay_ms);
    logger_.info(
        "REMOTE_CUSTOMIZATION_RETRY player=" + std::to_string(player_id) +
        " attempt=" + std::to_string(remote.appearance_attempt_count) +
        " delay_ms=" + std::to_string(delay_ms) + " reason=" + reason);
    return;
  }
  remote.appearance_dirty = false;
  logger_.warning(
      "REMOTE_APPEARANCE_FAIL_OPEN player=" + std::to_string(player_id) +
      " route=SetCharacterCustomization attempts=" +
      std::to_string(remote.appearance_attempt_count) + " reason=" + reason);
}

#if 0
auto apply_remote_appearance_direct_legacy(std::uint64_t player_id,
                                           GameBridge::RemotePlayer &remote)
    -> void {
  if (!object_is_valid(remote.actor) || !remote.appearance)
    return;
  const auto now = std::chrono::steady_clock::now();
  if (now < remote.next_appearance_retry)
    return;

  const auto first_attempt = remote.appearance_attempt_count == 0;
  const auto requested_character =
      class_leaf(remote.appearance->character_class);
  const auto body_selection = select_remote_body_component(
      remote.actor, requested_character, first_attempt ? &logger_ : nullptr,
      player_id);
  const auto reachable_actors = body_selection.reachable_actors;
  remote.body_component = body_selection.component;
  remote.body_route = body_selection.source;
  if (!object_is_valid(remote.skin_component)) {
    auto *actor_component_class = UObjectGlobals::StaticFindObject<UClass *>(
        nullptr, nullptr, L"/Script/Engine.ActorComponent");
    if (object_is_valid(actor_component_class)) {
      for (const auto &reachable : reachable_actors) {
        if (!object_is_valid(reachable.actor))
          continue;
        for (auto *component :
             reachable.actor->K2_GetComponentsByClass(actor_component_class)) {
          if (is_character_skin_component(component)) {
            remote.skin_component = component;
            break;
          }
        }
        if (object_is_valid(remote.skin_component))
          break;
      }
    }
  }
  if (first_attempt && object_is_valid(remote.skin_component)) {
    logger_.info("REMOTE_SKIN_COMPONENT player=" + std::to_string(player_id) +
                 " component=" + object_name(remote.skin_component) +
                 " attached_body=" +
                 object_name(object_property(remote.skin_component,
                                             "AttachedBodyOnCharacter")) +
                 " policy=" +
                 object_name(object_property(remote.skin_component,
                                             "SkinAssignPolicy")) +
                 " policy_class=" +
                 object_name(object_property(remote.skin_component,
                                             "SkinAssignPolicyClass")) +
                 " spawned_skin=" +
                 object_name(object_property(remote.skin_component,
                                             "SpawnedCharacterSkin")));
    log_character_skin_properties(logger_, "remote", player_id,
                                  remote.skin_component);
  }
  if (!object_is_valid(remote.hair_component) ||
      object_leaf_name(remote.hair_component) !=
          config_.hair_component_property) {
    const auto selection = find_reachable_skeletal_component(
        reachable_actors, config_.hair_component_property);
    remote.hair_component = selection.component;
    remote.hair_route = selection.route;
  }

  if (first_attempt || object_is_valid(remote.body_component)) {
    logger_.info(
        "REMOTE_BODY_COMPONENT player=" + std::to_string(player_id) +
        " component=" +
        (object_is_valid(remote.body_component)
             ? object_name(remote.body_component)
             : "nil") +
        " route=" + (remote.body_route.empty() ? "none" : remote.body_route));
  }
  if (first_attempt || object_is_valid(remote.hair_component)) {
    logger_.info(
        "REMOTE_HAIR_COMPONENT player=" + std::to_string(player_id) +
        " component=" +
        (object_is_valid(remote.hair_component)
             ? object_name(remote.hair_component)
             : "nil") +
        " route=" + (remote.hair_route.empty() ? "none" : remote.hair_route));
  }
  if (!object_is_valid(remote.body_component) &&
      !remote.skeletal_diagnostic_logged &&
      remote.appearance_attempt_count >= 2) {
    remote.skeletal_diagnostic_logged = true;
    log_remote_skeletal_inventory(logger_, player_id, reachable_actors);
  }

  bool changed{};
  const auto body_requested = !remote.appearance->outfit_mesh.empty();
  const auto hair_requested = !remote.appearance->hair_mesh.empty();
  auto *body_mesh = body_requested
                        ? resolve_skeletal_mesh(remote.appearance->outfit_mesh,
                                                asset_cache_, logger_)
                        : nullptr;
  auto *hair_mesh = hair_requested
                        ? resolve_skeletal_mesh(remote.appearance->hair_mesh,
                                                asset_cache_, logger_)
                        : nullptr;
  remote.expected_body_mesh = body_mesh;
  remote.expected_hair_mesh = hair_mesh;
  auto body_ready = !body_requested;
  auto hair_ready = !hair_requested;

  if (logic::should_write_remote_visual(
          config_.unsafe_direct_appearance, object_is_valid(body_mesh),
          object_is_valid(remote.body_component))) {
    if (object_property(remote.body_component, config_.mesh_asset_property) !=
            body_mesh &&
        set_object_property(remote.body_component, config_.mesh_asset_property,
                            body_mesh)) {
      changed = true;
      call_no_args(remote.body_component, "MarkRenderStateDirty");
    }
    auto *observed =
        object_property(remote.body_component, config_.mesh_asset_property);
    body_ready = observed == body_mesh;
    if (body_ready) {
      logger_.info("REMOTE_OUTFIT_APPLIED player=" + std::to_string(player_id) +
                   " requested=" + object_name(body_mesh) +
                   " observed=" + object_name(observed));
      remote.body_verification_pending = true;
    }
  }
  if (logic::should_write_remote_visual(
          config_.unsafe_direct_hair, object_is_valid(hair_mesh),
          object_is_valid(remote.hair_component))) {
    if (object_property(remote.hair_component, config_.mesh_asset_property) !=
            hair_mesh &&
        set_object_property(remote.hair_component, config_.mesh_asset_property,
                            hair_mesh)) {
      changed = true;
      call_no_args(remote.hair_component, "MarkRenderStateDirty");
    }
    auto *observed =
        object_property(remote.hair_component, config_.mesh_asset_property);
    hair_ready = observed == hair_mesh;
    if (hair_ready) {
      logger_.info("REMOTE_HAIR_APPLIED player=" + std::to_string(player_id) +
                   " requested=" + object_name(hair_mesh) +
                   " observed=" + object_name(observed));
      remote.hair_verification_pending = true;
    }
  }

  ++remote.appearance_attempt_count;
  const auto character_ready = !requested_character.empty() &&
                               requested_character == remote.spawned_character;
  const auto body_inputs_ready =
      !body_requested ||
      (object_is_valid(body_mesh) && object_is_valid(remote.body_component));
  const auto hair_inputs_ready =
      !hair_requested ||
      (object_is_valid(hair_mesh) && object_is_valid(remote.hair_component));
  const auto body_deferred =
      body_requested && !config_.unsafe_direct_appearance;
  const auto hair_deferred = hair_requested && !config_.unsafe_direct_hair;
  if ((body_deferred || hair_deferred) && body_inputs_ready &&
      hair_inputs_ready && character_ready) {
    remote.appearance_dirty = false;
    remote.visual_mesh_snapshot =
        collect_visual_mesh_snapshot(reachable_actors);
    remote.visual_snapshot_initialized = true;
    remote.next_visual_verification = now + std::chrono::milliseconds(250);
    logger_.warning(
        "REMOTE_APPEARANCE_DEFERRED player=" + std::to_string(player_id) +
        " reason=unsafe_direct_write_disabled body=" +
        object_name(remote.body_component) +
        " requested_outfit=" + remote.appearance->outfit_mesh +
        " requested_hair=" + remote.appearance->hair_mesh +
        " unsafe_direct_appearance=" +
        (config_.unsafe_direct_appearance ? "true" : "false") +
        " unsafe_direct_hair=" +
        (config_.unsafe_direct_hair ? "true" : "false"));
    logger_.info(
        "REMOTE_APPEARANCE_APPLIED player=" + std::to_string(player_id) +
        " changed=" + (changed ? "true" : "false") +
        " complete=false deferred=true actor=" + object_name(remote.actor));
    return;
  }
  const auto decision = logic::appearance_apply_decision(
      body_requested || !character_ready, body_ready && character_ready,
      hair_requested, hair_ready, remote.appearance_attempt_count);
  if (decision == logic::AppearanceApplyDecision::retry) {
    const auto delay_ms =
        logic::appearance_retry_delay_ms(remote.appearance_attempt_count);
    remote.next_appearance_retry = now + std::chrono::milliseconds(delay_ms);
    if (first_attempt) {
      if (body_requested && !body_ready) {
        logger_.warning(
            "REMOTE_OUTFIT_FAIL_OPEN player=" + std::to_string(player_id) +
            " reason=" +
            (object_is_valid(body_mesh) ? "Body_component_not_ready"
                                        : "asset_not_loaded") +
            " mesh=" + remote.appearance->outfit_mesh + " retrying=true");
      }
      if (hair_requested && !hair_ready) {
        logger_.warning(
            "REMOTE_HAIR_FAIL_OPEN player=" + std::to_string(player_id) +
            " reason=" +
            (object_is_valid(hair_mesh)
                 ? "Haircut_SkeletalMesh_component_not_ready"
                 : "asset_not_loaded") +
            " mesh=" + remote.appearance->hair_mesh + " retrying=true");
      }
    }
    logger_.info("REMOTE_APPEARANCE_RETRY player=" + std::to_string(player_id) +
                 " attempt=" + std::to_string(remote.appearance_attempt_count) +
                 " delay_ms=" + std::to_string(delay_ms) +
                 " body_ready=" + (body_ready ? "true" : "false") +
                 " hair_ready=" + (hair_ready ? "true" : "false") +
                 " character_ready=" + (character_ready ? "true" : "false"));
    return;
  }

  remote.appearance_dirty = false;
  if (decision == logic::AppearanceApplyDecision::fail_open) {
    logger_.warning(
        "REMOTE_APPEARANCE_FAIL_OPEN player=" + std::to_string(player_id) +
        " attempts=" + std::to_string(remote.appearance_attempt_count) +
        " body_ready=" + (body_ready ? "true" : "false") +
        " hair_ready=" + (hair_ready ? "true" : "false") +
        " character_ready=" + (character_ready ? "true" : "false"));
  }
  remote.visual_mesh_snapshot = collect_visual_mesh_snapshot(reachable_actors);
  remote.visual_snapshot_initialized = true;
  remote.next_visual_verification = now + std::chrono::milliseconds(250);
  logger_.info("REMOTE_APPEARANCE_APPLIED player=" + std::to_string(player_id) +
               " changed=" + (changed ? "true" : "false") + " complete=" +
               (decision == logic::AppearanceApplyDecision::complete
                    ? "true"
                    : "false") +
               " actor=" + object_name(remote.actor));
}

#endif

auto GameBridge::apply_remote_movement_state(std::uint64_t player_id,
                                             RemotePlayer &remote) -> void {
  if (!object_is_valid(remote.actor) || !remote.movement_state)
    return;
  if (!object_is_valid(remote.movement_component))
    remote.movement_component = find_remote_movement_component(remote.actor);
  const auto &requested = *remote.movement_state;
  if (!call_set_movement_mode(remote.movement_component,
                              requested.movement_mode,
                              requested.custom_movement_mode)) {
    logger_.warning(
        "REMOTE_MOVEMENT_STATE player=" + std::to_string(player_id) +
        " requested_mode=" + std::to_string(requested.movement_mode) +
        " observed_mode=unavailable is_falling=unavailable "
        "reason=SetMovementMode_unavailable");
    remote.movement_state_dirty = false;
    return;
  }

  const auto *observed_mode =
      byte_property(remote.movement_component, "MovementMode");
  const auto *observed_custom =
      byte_property(remote.movement_component, "CustomMovementMode");
  bool is_falling{};
  const auto has_is_falling =
      call_bool_return(remote.movement_component, "IsFalling", is_falling);
  logger_.info(
      "REMOTE_MOVEMENT_STATE player=" + std::to_string(player_id) +
      " requested_mode=" + std::to_string(requested.movement_mode) +
      " requested_custom=" + std::to_string(requested.custom_movement_mode) +
      " observed_mode=" +
      (observed_mode ? std::to_string(*observed_mode) : "unavailable") +
      " observed_custom=" +
      (observed_custom ? std::to_string(*observed_custom) : "unavailable") +
      " is_falling=" +
      (has_is_falling ? (is_falling ? "true" : "false") : "unavailable"));
  remote.movement_state_dirty = false;
}

auto GameBridge::apply_remote_locomotion_state(std::uint64_t player_id,
                                               RemotePlayer &remote) -> void {
  if (!object_is_valid(remote.actor) || !remote.locomotion_state)
    return;
  const auto &state = *remote.locomotion_state;
  const auto movement_state_applied =
      config_.sync_locomotion_state &&
      call_byte_input_validated(remote.actor, "SetMovementState",
                                state.locomotion_state);
  const auto gait_applied =
      config_.sync_gait &&
      call_byte_input_validated(remote.actor, "SetDesiredGait", state.gait);
  const auto stance_applied =
      config_.sync_crouch &&
      call_byte_input_validated(remote.actor, "SetStance", state.stance);

  logger_.info("REMOTE_LOCOMOTION player=" + std::to_string(player_id) +
               " backend=" + remote.backend +
               " movement_mode=" + std::to_string(state.movement_mode) +
               " locomotion_state=" + std::to_string(state.locomotion_state) +
               " gait=" + std::to_string(state.gait) +
               " stance=" + std::to_string(state.stance) +
               " sprinting=" + (state.sprinting ? "true" : "false") +
               " crouching=" + (state.crouching ? "true" : "false") +
               " aiming=" + (state.aiming ? "true" : "false") + " aim_pitch=" +
               std::to_string(state.aim_pitch) + " SetMovementState=" +
               (movement_state_applied ? "applied" : "unavailable") +
               " SetDesiredGait=" + (gait_applied ? "applied" : "unavailable") +
               " SetStance=" + (stance_applied ? "applied" : "unavailable") +
               " movement_input=false");
  if (!movement_state_applied && !gait_applied && !stance_applied &&
      !remote.locomotion_warning_logged) {
    remote.locomotion_warning_logged = true;
    logger_.warning(
        "REMOTE_LOCOMOTION_FAIL_OPEN player=" + std::to_string(player_id) +
        " reason=validated_vanilla_functions_unavailable");
  }
  if (state.aiming && config_.sync_aim && !remote.locomotion_warning_logged) {
    remote.locomotion_warning_logged = true;
    logger_.warning("REMOTE_AIM_FAIL_OPEN player=" + std::to_string(player_id) +
                    " reason=visual_aim_setter_not_validated");
  }
  remote.locomotion_state_dirty = false;
}

auto GameBridge::verify_remote_visual_state(std::uint64_t player_id,
                                            RemotePlayer &remote) -> void {
  if (!object_is_valid(remote.actor))
    return;
  const auto now = std::chrono::steady_clock::now();
  if (now < remote.next_visual_verification)
    return;
  remote.next_visual_verification = now + std::chrono::milliseconds(250);

  const auto reachable_actors = collect_reachable_visual_actors(remote.actor);
  const auto current = collect_visual_mesh_snapshot(reachable_actors);
  if (!remote.visual_snapshot_initialized) {
    remote.visual_mesh_snapshot = current;
    remote.visual_snapshot_initialized = true;
  }

  bool visual_drift{};
  for (const auto &[key, new_mesh] : current) {
    const auto old = remote.visual_mesh_snapshot.find(key);
    if (old != remote.visual_mesh_snapshot.end() && old->second == new_mesh)
      continue;
    visual_drift = true;
    const auto separator = key.find('|');
    const auto component_name =
        separator == std::string::npos ? key : key.substr(separator + 1);
    std::string expected{"nil"};
    if (object_is_valid(remote.body_component) &&
        component_name == object_name(remote.body_component))
      expected = object_name(remote.expected_body_mesh);
    if (object_is_valid(remote.hair_component) &&
        component_name == object_name(remote.hair_component))
      expected = object_name(remote.expected_hair_mesh);
    logger_.warning(
        "REMOTE_VISUAL_DRIFT player=" + std::to_string(player_id) +
        " object=" + key.substr(0, separator) + " component=" + component_name +
        " old_mesh=" +
        (old == remote.visual_mesh_snapshot.end() ? "nil" : old->second) +
        " new_mesh=" + new_mesh + " expected=" + expected);
  }
  for (const auto &[key, old_mesh] : remote.visual_mesh_snapshot) {
    if (current.contains(key))
      continue;
    visual_drift = true;
    const auto separator = key.find('|');
    const auto component_name =
        separator == std::string::npos ? key : key.substr(separator + 1);
    std::string expected{"nil"};
    if (object_is_valid(remote.body_component) &&
        component_name == object_name(remote.body_component))
      expected = object_name(remote.expected_body_mesh);
    if (object_is_valid(remote.hair_component) &&
        component_name == object_name(remote.hair_component))
      expected = object_name(remote.expected_hair_mesh);
    logger_.warning("REMOTE_VISUAL_DRIFT player=" + std::to_string(player_id) +
                    " object=" + key.substr(0, separator) +
                    " component=" + component_name + " old_mesh=" + old_mesh +
                    " new_mesh=nil expected=" + expected);
  }

  if (visual_drift)
    log_remote_skeletal_inventory(logger_, player_id, reachable_actors);

#if 0
  if (config_.unsafe_direct_hair &&
      object_is_valid(remote.expected_hair_mesh) &&
      object_is_valid(remote.hair_component)) {
    auto *observed =
        object_property(remote.hair_component, config_.mesh_asset_property);
    if (remote.hair_verification_pending) {
      remote.hair_verification_pending = false;
      logger_.info("REMOTE_HAIR_APPLIED player=" + std::to_string(player_id) +
                   " verification=next_tick " +
                   "requested=" + object_name(remote.expected_hair_mesh) +
                   " observed=" + object_name(observed));
    }
    if (observed != remote.expected_hair_mesh) {
      logger_.warning("REMOTE_HAIR_DRIFT player=" + std::to_string(player_id) +
                      " expected=" + object_name(remote.expected_hair_mesh) +
                      " observed=" + object_name(observed));
      if (set_object_property(remote.hair_component,
                              config_.mesh_asset_property,
                              remote.expected_hair_mesh)) {
        call_no_args(remote.hair_component, "MarkRenderStateDirty");
        observed =
            object_property(remote.hair_component, config_.mesh_asset_property);
        logger_.info("REMOTE_HAIR_APPLIED player=" + std::to_string(player_id) +
                     " requested=" + object_name(remote.expected_hair_mesh) +
                     " observed=" + object_name(observed) +
                     " reason=drift_reapply");
        remote.hair_verification_pending = true;
      }
    }
  }

  if (config_.unsafe_direct_appearance &&
      object_is_valid(remote.expected_body_mesh) && remote.appearance) {
    const auto selection = select_remote_body_component(
        remote.actor, remote.appearance->character_id, nullptr,
        player_id);
    if (object_is_valid(selection.component)) {
      remote.body_component = selection.component;
      remote.body_route = selection.source;
    }
    if (object_is_valid(remote.body_component)) {
      auto *observed =
          object_property(remote.body_component, config_.mesh_asset_property);
      if (remote.body_verification_pending) {
        remote.body_verification_pending = false;
        logger_.info(
            "REMOTE_OUTFIT_APPLIED player=" + std::to_string(player_id) +
            " verification=next_tick requested=" +
            object_name(remote.expected_body_mesh) +
            " observed=" + object_name(observed));
      }
      if (observed != remote.expected_body_mesh) {
        logger_.warning(
            "REMOTE_OUTFIT_DRIFT player=" + std::to_string(player_id) +
            " expected=" + object_name(remote.expected_body_mesh) +
            " observed=" + object_name(observed));
        if (set_object_property(remote.body_component,
                                config_.mesh_asset_property,
                                remote.expected_body_mesh)) {
          call_no_args(remote.body_component, "MarkRenderStateDirty");
          observed = object_property(remote.body_component,
                                     config_.mesh_asset_property);
          logger_.info(
              "REMOTE_OUTFIT_APPLIED player=" + std::to_string(player_id) +
              " requested=" + object_name(remote.expected_body_mesh) +
              " observed=" + object_name(observed) + " reason=drift_reapply");
          remote.body_verification_pending = true;
        } else {
          remote.appearance_dirty = true;
          remote.appearance_attempt_count = 0;
          remote.next_appearance_retry = {};
        }
      }
    }
  }
#endif
  remote.visual_mesh_snapshot = collect_visual_mesh_snapshot(reachable_actors);
}

auto GameBridge::disable_remote_ai(AActor *actor) -> void {
  if (!object_is_valid(actor))
    return;
  actor->SetActorEnableCollision(false);
  actor->SetActorTickEnabled(true);
  if (auto *mesh = object_property(actor, "Mesh"); object_is_valid(mesh))
    (void)call_bool_input(mesh, "SetComponentTickEnabled", true);
  auto *companion_manager =
      object_property(actor, "BP_AICompanion_CompanionManager");
  if (!object_is_valid(companion_manager))
    companion_manager = object_property(actor, "AICompanion_CompanionManager");
  if (auto *enabled =
          bool_property(companion_manager, "SpawnCompanionsEnabled"))
    *enabled = false;
  (void)call_no_args(companion_manager, "UnspawnAICompanions");
  auto *controller = object_property(actor, config_.controller_property);
  call_no_args(controller, "StopMovement");
  if (object_is_valid(controller)) {
    static_cast<AActor *>(controller)->SetActorTickEnabled(false);
    auto *brain = object_property(controller, config_.brain_component_property);
    // Deactivate is the no-argument Blueprint-safe path for stopping the
    // BrainComponent; StopLogic itself requires an FString parameter.
    call_no_args(brain, "Deactivate");
  }
}

auto GameBridge::destroy_remote_actor(std::uint64_t player_id,
                                      RemotePlayer &remote,
                                      bool reset_interpolation) -> void {
  if (object_is_valid(remote.actor)) {
    remote.actor->K2_DestroyActor();
    logger_.info("REMOTE_ACTOR_DESTROYED player=" + std::to_string(player_id) +
                 " backend=" + remote.backend);
  }
  remote.actor = nullptr;
  remote.movement_component = nullptr;
  remote.body_component = nullptr;
  remote.hair_component = nullptr;
  remote.expected_body_mesh = nullptr;
  remote.expected_hair_mesh = nullptr;
  remote.locomotion_anim_instance = nullptr;
  remote.skin_component = nullptr;
  remote.body_route.clear();
  remote.hair_route.clear();
  remote.spawned_character.clear();
  remote.backend.clear();
  remote.visual_mesh_snapshot.clear();
  remote.visual_snapshot_initialized = false;
  remote.body_verification_pending = false;
  remote.hair_verification_pending = false;
  remote.network_authority_configured = false;
  remote.transform_drift_initialized = false;
  remote.locomotion_warning_logged = false;
  remote.jump_target_warning_logged = false;
  remote.appearance_attempt_count = 0;
  remote.next_appearance_retry = {};
  remote.appearance_dirty = remote.appearance.has_value();
  remote.movement_state_dirty = remote.movement_state.has_value();
  remote.locomotion_state_dirty = remote.locomotion_state.has_value();
  if (reset_interpolation) {
    remote.snapshots.clear();
    remote.last_rendered_transform.reset();
    remote.clock_offset_initialized = false;
    remote.last_transform_received_ms = 0;
    remote.velocity_x = 0.0F;
    remote.velocity_y = 0.0F;
    remote.velocity_z = 0.0F;
    remote.speed = 0.0F;
  }
}

auto GameBridge::destroy_remote(std::uint64_t player_id) -> void {
  const auto found = remotes_.find(player_id);
  if (found == remotes_.end())
    return;
  destroy_remote_actor(player_id, found->second, false);
  logger_.info("REMOTE_DESTROYED player=" + std::to_string(player_id));
  remotes_.erase(found);
}

auto GameBridge::destroy_all_remotes() -> void {
  for (auto &[player_id, remote] : remotes_) {
    (void)player_id;
    if (object_is_valid(remote.actor))
      remote.actor->K2_DestroyActor();
  }
  remotes_.clear();
}
} // namespace expedition_online::client
