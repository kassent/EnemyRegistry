#include "EnemyIdRegistry.h"
#include "EnemyIconAtlas.h"
#include <LogUtils.h>
#include <algorithm>
#include <fstream>
#include <nlohmann/json.hpp>
#include <set>
#include <stdexcept>

namespace EnemyRegistry {
static std::vector<Enemy> enemies;

std::vector<Enemy> ReadConfigs(const std::filesystem::path& directory) {
    using Json = nlohmann::json;
    std::vector<Enemy> configs;
    if (!std::filesystem::exists(directory))
        return configs;
    std::set<int32_t> fixedIds;
    std::set<std::string> enumNames;
    for (const auto& file : std::filesystem::directory_iterator(directory)) {
        if (!file.is_regular_file() || !file.path().filename().string().ends_with(".enemy.json"))
            continue;
        std::ifstream stream(file.path());
        try {
            const auto declaration = Json::parse(stream);
            if (declaration.value("format", "") != "EnemyRegistry.Enemy/v4")
                throw std::runtime_error("enemy declaration requires EnemyRegistry.Enemy/v4: " + file.path().string());
            for (const auto* key : {"extendType", "contextType", "resourcePrefix", "holderName", "rewardName"})
                if (declaration.contains(key))
                    throw std::runtime_error("derived enemy identity is not an author field: " + std::string(key) + " in " + file.path().string());
            auto enemy = declaration.get<Enemy>();
            // 原生工厂使用前9字符，Holder/Prefab/奖励使用完整名，固定同一命名契约。
            const auto& name = enemy.enumName;
            bool nativeName = name.size() == 11 && name.starts_with("EM") && name[6] == '_' && name[9] == '_';
            if (nativeName)
                for (const auto position : {2, 3, 4, 5, 7, 8, 10})
                    nativeName &= name[position] >= '0' && name[position] <= '9';
            if (!nativeName)
                throw std::runtime_error("enumName requires EMdddd_dd_d: " + file.path().string());
            const auto& texture = enemy.icon.texture;
            if (!texture.empty() && (!texture.starts_with("gui/") || !texture.ends_with(".tex")))
                throw std::runtime_error("invalid icon texture URI: " + file.path().string());
            // EnumMaker.createConvertTable 跳过 FixedID=-1；其他负数仍是合法的哈希 ID。
            if (enemy.fixedId == -1)
                throw std::runtime_error("fixedId -1 is reserved by EnumMaker: " + file.path().string());
            if (!enemy.fixedId || !fixedIds.insert(enemy.fixedId).second || !enumNames.insert(enemy.enumName).second)
                throw std::runtime_error("missing, empty or conflicting enemy identity: " +
                                         file.path().string());
            for (const auto& message : enemy.messages) {
                const auto english = message.texts.find("en");
                if (message.guid.empty() || english == message.texts.end() || english->second.empty())
                    throw std::runtime_error("message requires GUID and texts.en fallback: " + file.path().string());
                for (const auto& [tag, text] : message.texts)
                    if (std::ranges::none_of(Message::LANGUAGES, [&](const auto& language) {
                            return language.second == tag;
                        }))
                        throw std::runtime_error("unsupported message language '" + tag + "': " + message.guid + " in " + file.path().string());
            }
            configs.push_back(std::move(enemy));
        } catch (const Json::exception& error) {
            throw std::runtime_error("invalid enemy config: " + file.path().string() + ": " + error.what());
        }
    }
    std::ranges::sort(configs, {}, &Enemy::fixedId);
    if (configs.size() > size_t(MAX_CUSTOM_RAW_ID - FIRST_CUSTOM_RAW_ID + 1))
        throw std::runtime_error("custom enemy raw ID capacity exceeded");
    for (size_t i = 0; i < configs.size(); ++i)
        configs[i].rawId = FIRST_CUSTOM_RAW_ID + static_cast<int32_t>(i);
    return configs;
}

bool LoadConfigs(const std::filesystem::path& directory) {
    try {
        auto configs = ReadConfigs(directory);
        BuildSharedIconAtlas(directory.parent_path().parent_path().parent_path(), configs);
        enemies = std::move(configs);
        for (const auto& enemy : enemies)
            _MESSAGE("[EnemyIdRegistry] fixed=%d -> raw=%d enum=%s", enemy.fixedId, enemy.rawId, enemy.enumName.c_str());
        _MESSAGE("[EnemyIdRegistry] configs frozen: enemies=%zu native capacity=%u",
                 enemies.size(), GetEnemyArrayCapacity());
        return true;
    } catch (const std::exception& error) {
        _MESSAGE("[EnemyIdRegistry] config load FAILED: %s", error.what());
        return false;
    }
}
const std::vector<Enemy>& GetRegisteredEnemies() {
    return enemies;
}
const Enemy* FindEnemyByRawId(int32_t rawId) {
    if (rawId < FIRST_CUSTOM_RAW_ID)
        return nullptr;
    const auto index = size_t(rawId - FIRST_CUSTOM_RAW_ID);
    return index < enemies.size() ? &enemies[index] : nullptr;
}
const Enemy* FindEnemyByFixedId(int32_t fixedId) {
    const auto found =
        std::ranges::lower_bound(enemies, fixedId, {}, &Enemy::fixedId);
    return found != enemies.end() && found->fixedId == fixedId ? &*found : nullptr;
}
uint32_t GetEnemyArrayCapacity() {
    return FIRST_CUSTOM_RAW_ID + static_cast<uint32_t>(enemies.size());
}
} // namespace EnemyRegistry
