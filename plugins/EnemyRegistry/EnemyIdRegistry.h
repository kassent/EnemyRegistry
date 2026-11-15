#pragma once
#include <cstdint>
#include <filesystem>
#include <map>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace EnemyRegistry {
// fixed ID 是外部稳定身份；raw ID 仅是本次启动的原生数组索引。
inline constexpr int32_t FIRST_CUSTOM_RAW_ID = 150;
inline constexpr int32_t MAX_CUSTOM_RAW_ID =
    255; // 动态敌人 Context ID 的物种字段为 8 bit。
struct Message {
    // via.Language values, not app.LanguageDef.LANGUAGE_APP. Only the game's
    // 15 selectable text languages are exposed to authors; tags use BCP 47.
    static constexpr int32_t ENGLISH_LANGUAGE = 1;
    static constexpr int32_t LANGUAGE_COUNT = 33; // via.Language.Max
    static constexpr std::pair<int32_t, std::string_view> LANGUAGES[] = {
        {0, "ja"},
        {1, "en"},
        {2, "fr"},
        {3, "it"},
        {4, "de"},
        {5, "es"},
        {6, "ru"},
        {7, "pl"},
        {10, "pt-BR"},
        {11, "ko"},
        {12, "zh-Hant"},
        {13, "zh-Hans"},
        {21, "ar"},
        {26, "th"},
        {32, "es-419"},
    };
    std::string key;
    std::string guid;
    std::map<std::string, std::string, std::less<>> texts;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Message, key, guid, texts)

// Shared atlas/v1: 100px RGBA8 TEX tile with 3 prebuilt mips.
struct Icon {
    std::string texture;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Icon, texture)

struct Enemy {
    int32_t fixedId{};
    int32_t rawId{};
    std::string enumName;
    int32_t iconFixedId = -1;
    int32_t iconAtlasIndex = -1;
    Icon icon;
    std::vector<Message> messages;
};
// Optional fields keep defaults; ReadConfigs validates required identities after decoding.
// Runtime IDs are generated and intentionally excluded from the JSON codec.
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Enemy, fixedId, enumName, icon, messages)

// 收齐声明后按 fixed ID 排序分配；失败不发布部分注册表，安装后不动态增删。
std::vector<Enemy> ReadConfigs(const std::filesystem::path& directory);
bool LoadConfigs(const std::filesystem::path& directory);
const std::vector<Enemy>& GetRegisteredEnemies();
const Enemy* FindEnemyByRawId(int32_t rawId);
const Enemy* FindEnemyByFixedId(int32_t fixedId);
uint32_t GetEnemyArrayCapacity();
} // namespace EnemyRegistry
