#include <expedition_online/build_info.hpp>
#include <expedition_online/client/config.hpp>
#include <expedition_online/client/game_bridge.hpp>
#include <expedition_online/client/logger.hpp>
#include <expedition_online/client/network_client.hpp>

#include <filesystem>
#include <memory>
#include <stdexcept>

#include <Windows.h>

#include <DynamicOutput/Output.hpp>
#include <Mod/CppUserModBase.hpp>
#include <Unreal/Hooks/Hooks.hpp>

extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace expedition_online::client {
namespace {
auto mod_root() -> std::filesystem::path {
  std::wstring path(32768, L'\0');
  const auto size =
      GetModuleFileNameW(reinterpret_cast<HMODULE>(&__ImageBase), path.data(),
                         static_cast<DWORD>(path.size()));
  if (size == 0 || size == path.size())
    throw std::runtime_error("GetModuleFileNameW failed");
  path.resize(size);
  return std::filesystem::path(path).parent_path().parent_path();
}
} // namespace

class ExpeditionOnlineMod final : public RC::CppUserModBase {
public:
  ExpeditionOnlineMod() {
    ModName = STR("ExpeditionOnline");
    ModVersion = STR("0.5.0-rc1");
    ModDescription = STR("Exploration-only online co-op relay prototype for "
                         "Clair Obscur: Expedition 33");
    ModAuthors = STR("ExpeditionOnline contributors");
    RC::Output::send<RC::LogLevel::Verbose>(
        STR("[ExpeditionOnline] native mod loaded\n"));
  }

  ~ExpeditionOnlineMod() override {
    if (hook_id_ != RC::Unreal::Hook::ERROR_ID) {
      RC::Unreal::Hook::UnregisterCallback(hook_id_);
      hook_id_ = RC::Unreal::Hook::ERROR_ID;
    }
    if (bridge_)
      bridge_->shutdown();
    if (network_)
      network_->stop();
    bridge_.reset();
    network_.reset();
    logger_.reset();
    RC::Output::send<RC::LogLevel::Verbose>(
        STR("[ExpeditionOnline] native mod unloaded\n"));
  }

  auto on_unreal_init() -> void override {
    try {
      const auto root = mod_root();
      logger_ = std::make_unique<Logger>(root / "ExpeditionOnline.log");
      config_ = load_client_config(root / "config" / "config.ini");
      logger_->info(build_info::identity("Client", protocol::kProtocolVersion) +
                    " server=" + config_.host + ':' +
                    std::to_string(config_.port) +
                    " remote_network_authority=" +
                    (config_.remote_network_authority ? "true" : "false") +
                    " unsafe_direct_appearance=" +
                    (config_.unsafe_direct_appearance ? "true" : "false") +
                    " unsafe_direct_hair=" +
                    (config_.unsafe_direct_hair ? "true" : "false"));
      network_ = std::make_unique<NetworkClient>(config_, *logger_);
      bridge_ = std::make_unique<GameBridge>(config_, *network_, *logger_);
      network_->start();

      hook_id_ = RC::Unreal::Hook::RegisterProcessEventPostCallback(
          [this](auto &, RC::Unreal::UObject *object,
                 RC::Unreal::UFunction *function, void *) {
            if (bridge_) {
              bridge_->observe_process_event(object, function);
              bridge_->tick();
            }
          },
          {false, false, STR("ExpeditionOnline"), STR("GameThreadBridge")});

      if (hook_id_ == RC::Unreal::Hook::ERROR_ID) {
        throw std::runtime_error(
            "could not register ProcessEvent game-thread callback");
      }
      RC::Output::send<RC::LogLevel::Verbose>(
          STR("[ExpeditionOnline] initialized; see ExpeditionOnline.log\n"));
    } catch (const std::exception &exception) {
      if (logger_)
        logger_->error(std::string("INIT_FAILED ") + exception.what());
      RC::Output::send<RC::LogLevel::Error>(
          STR("[ExpeditionOnline] initialization failed\n"));
    }
  }

private:
  ClientConfig config_;
  std::unique_ptr<Logger> logger_;
  std::unique_ptr<NetworkClient> network_;
  std::unique_ptr<GameBridge> bridge_;
  RC::Unreal::Hook::GlobalCallbackId hook_id_{RC::Unreal::Hook::ERROR_ID};
};
} // namespace expedition_online::client

#define EXPEDITION_ONLINE_API __declspec(dllexport)
extern "C" {
EXPEDITION_ONLINE_API RC::CppUserModBase *start_mod() {
  return new expedition_online::client::ExpeditionOnlineMod();
}

EXPEDITION_ONLINE_API void uninstall_mod(RC::CppUserModBase *mod) {
  delete mod;
}
}
