#include "EnemyIconDiscovery.h"
#include "EnemyIdRegistry.h"
#include <GameTypes.h>
#include <HookUtils.h>

namespace app::IconUtil {
struct ICON_SETTING_DATA {
    int32_t Category;
    int32_t IconType;
    int32_t Color;
};
static_assert(sizeof(ICON_SETTING_DATA) == 12);
bool InstallHooks() {
    using FnGetEnemyIconSettingData =
        ICON_SETTING_DATA* (*)(ICON_SETTING_DATA*, void*, int32_t, bool);
    auto getEnemyIconSettingData = RE::get_method<FnGetEnemyIconSettingData>(
        "app.IconUtil",
        "getEnemyIconSettingData(app.EnemyDef.ID, System.Boolean)");
    if (!getEnemyIconSettingData)
        return false;
    return HookLambda(getEnemyIconSettingData,
                      [](ICON_SETTING_DATA* result, void* vmctx, int32_t id,
                         bool checkDiscovery) -> ICON_SETTING_DATA* {
                          if (EnemyRegistry::FindEnemyByRawId(id))
                              checkDiscovery = false;
                          return original(result, vmctx, id, checkDiscovery);
                      });
}
} // namespace app::IconUtil
