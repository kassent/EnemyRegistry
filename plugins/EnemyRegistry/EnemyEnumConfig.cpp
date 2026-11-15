#include "EnemyEnumConfig.h"
#include <nlohmann/json.hpp>
#include <utility>

namespace EnemyRegistry {
// 三个批次依次处理 MAX、缺行追加、身份覆盖；同批字段之间不依赖执行顺序。
std::string BuildEnumConfig(std::span<const Enemy> enemies) {
    using Json = nlohmann::json;
    const auto capacity = FIRST_CUSTOM_RAW_ID + static_cast<int32_t>(enemies.size());
    Json patches = Json::array();
    // 先把哨兵移到全部新 raw 之外，避免 raw210 与原 MAX210 在 append 去重时撞键。
    patches.push_back({{"ace.user_data.EnumMaker", {{"_DataList[_EnumName=\"MAX\"]._EnumValue", {{"op", "max"}, {"value", capacity}}}}}});
    Json rows = Json::array();
    Json identityFields = Json::object();
    for (const auto& enemy : enemies) {
        rows.push_back({{"ace.user_data.EnumMaker.cData", {{"_EnumValue", enemy.rawId}, {"_FixedID", enemy.fixedId}, {"_EnumName", enemy.enumName}}}});
        const auto rowPath = "_DataList[_EnumValue=" + std::to_string(enemy.rawId) + "]";
        identityFields[rowPath + "._FixedID"] = {{"op", "set"}, {"value", enemy.fixedId}};
        identityFields[rowPath + "._EnumName"] = {{"op", "set"}, {"value", enemy.enumName}};
    }
    // 原表已占用的 raw 不追加副本；下一批次原地更新身份，保留原行对象。
    patches.push_back({{"ace.user_data.EnumMaker", {{"_DataList", {{"op", "append"}, {"key", "_EnumValue"}, {"items", std::move(rows)}}}}}});
    patches.push_back({{"ace.user_data.EnumMaker", std::move(identityFields)}});
    const Json config{{"format", "UserDataExtension/v1"}, {"extensions", Json::array({{{"target", "GameDesign/Enemy/CommonData/EnumMaker/EmID.user"}, {"patches", std::move(patches)}}})}};
    return config.dump();
}
} // namespace EnemyRegistry
