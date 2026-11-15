#include "../UserDataExtension/UserDataExtension.h"
#include "EnemyEnumConfig.h"
#include "EnemyIconDiscovery.h"
#include "EnemyIdRegistry.h"
#include "EnemyMessageOverrides.h"
#include "EnemyQuestVisibility.h"
#include "EnemyRegistrationHooks.h"
#include <FileUtils.h>
#include <GameTypes.h>
#include <LogUtils.h>
#include <Windows.h>
#include <stdexcept>

CI_REGISTER_PROVIDER(registration) {
    // 零类型CI provider：读取纯文件声明，不访问VM/RF SDK，不负责注入怪物类型。
    const auto gameRoot = FileUtils::GetExecutableDirectory();
    if (!EnemyRegistry::LoadConfigs(gameRoot / "reframework/data/EnemyRegistry"))
        throw std::runtime_error("EnemyRegistry configs failed");
    const auto& enemies = EnemyRegistry::GetRegisteredEnemies();
    // raw由同一冻结声明分配；在relocation后的CI构建窗口选择原常量，只改canonical名称。
    for (const auto& enemy : enemies)
        registration.ExtendEnum("app.EnemyDef.ID", {ci::EnumRename::FromValue(enemy.rawId, enemy.enumName.c_str())}, {});
    if (!enemies.empty()) {
        // MO2 may store UDE in another Mod's physical directory. Resolve its
        // virtual game path explicitly instead of relying on PE import search.
        const auto userDataExtensionPath = gameRoot / L"reframework" / L"plugins" / L"UserDataExtension.dll";
        const auto userDataExtension = LoadLibraryExW(userDataExtensionPath.c_str(), nullptr,
            LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        if (!userDataExtension)
            throw std::runtime_error("UserDataExtension DLL load failed: " + userDataExtensionPath.string() +
                                     " (" + std::to_string(GetLastError()) + ")");
        // Retain the module for the process lifetime, like CI providers.
        const auto registerUserDataExtensionJson = reinterpret_cast<decltype(&RegisterUserDataExtensionJson)>(
            GetProcAddress(userDataExtension, "RegisterUserDataExtensionJson"));
        if (!registerUserDataExtensionJson)
            throw std::runtime_error("UserDataExtension RegisterUserDataExtensionJson export missing");
        // 登记期 raw 已确定；UDE 同步保存配置，原生解码完成后再写 EmID。
        const auto config = EnemyRegistry::BuildEnumConfig(enemies);
        if (!registerUserDataExtensionJson("EnemyRegistry/EmID", config.c_str()))
            throw std::runtime_error("EnemyRegistry EmID extension registration failed");
        _MESSAGE("[Startup] EmID extension registered with UserDataExtension: enemies=%zu", enemies.size());
    }
}

extern "C" __declspec(dllexport) void ci_on_tdb_ready(const ci::ProviderContext* context) noexcept {
    if (!EnemyRegistry::RegistrationHooks::InstallHooks(context)) {
        _MESSAGE("[Startup] Failed to install enemy registration hooks");
        return;
    }
    _MESSAGE("[Startup] pre-cctor enemy registration ready; EmID and public USER extensions handled by UserDataExtension");
}

extern "C" __declspec(dllexport) void reframework_plugin_required_version(REFrameworkPluginVersion* version) {
    version->major = REFRAMEWORK_PLUGIN_VERSION_MAJOR;
    version->minor = REFRAMEWORK_PLUGIN_VERSION_MINOR;
    version->patch = REFRAMEWORK_PLUGIN_VERSION_PATCH;
}
extern "C" __declspec(dllexport) bool reframework_plugin_initialize(const REFrameworkPluginInitializeParam* parameter) {
    reframework::API::initialize(parameter);
    if (!EnemyRegistry::GetRegisteredEnemies().empty()) {
        if (!app::IconUtil::InstallHooks()) {
            _MESSAGE("[Startup] Failed to install IconUtil hooks");
            return false;
        }
        if (!app::QuestUtil::InstallHooks()) {
            _MESSAGE("[Startup] Failed to install quest visibility hook");
            return false;
        }
        if (!via::gui::message::InstallHooks()) {
            _MESSAGE("[Startup] Failed to install message hooks");
            return false;
        }
    }
    _MESSAGE("[Startup] Plugin initialized");
    return true;
}
BOOL APIENTRY DllMain(HMODULE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        initLogger("EnemyRegistry");
        _MESSAGE("[Startup] DLL loaded; standalone enemy registry CI provider");
    }
    return TRUE;
}
