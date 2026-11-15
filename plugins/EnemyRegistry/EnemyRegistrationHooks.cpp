#include "EnemyRegistrationHooks.h"
#include "EnemyIdRegistry.h"
#include <HookUtils.h>
#include <LogUtils.h>
#include <cstdint>
#include <utility>
#include <vector>

namespace EnemyRegistry::RegistrationHooks {
void* ResolveMethodBody(const ci::ProviderContext* context, const char* typeName, const char* signature) {
    constexpr uint8_t REL32_JUMP_OPCODE = 0xE9;
    constexpr int32_t REL32_DISPLACEMENT_OFFSET = 1;
    constexpr uint32_t REL32_INSTRUCTION_SIZE = 5;
    const auto* typeDefinition = context->GetType(typeName);
    auto* methodBody = typeDefinition ? context->GetMethod(typeDefinition, signature) : nullptr;
    // 与现有NavigationCtor一致：只解TDB入口首指令的单层E9，原生caller可直达body。
    // 后面的局部扫描从这个body重新起算；不跨函数链追任意跳转。
    if (methodBody && *static_cast<const uint8_t*>(methodBody) == REL32_JUMP_OPCODE)
        methodBody = reinterpret_cast<void*>(HookUtils::ReadOffsetData(reinterpret_cast<uintptr_t>(methodBody), REL32_DISPLACEMENT_OFFSET, REL32_INSTRUCTION_SIZE));
    if (!methodBody)
        _MESSAGE("[EnemyRegistry] native method body missing: %s.%s", typeName, signature);
    return methodBody;
}
} // namespace EnemyRegistry::RegistrationHooks

namespace app::VariousDataManager {
using EnemyRegistry::RegistrationHooks::ResolveMethodBody;
static bool FindEnemyDataIndexBoundsCheck(const ci::ProviderContext* context, uintptr_t& createEnemyDataIndexRawIdCheckAddress) {
    constexpr uint32_t DIRECT_CALL_SIZE = 5;
    constexpr size_t ENEMY_INDEX_SEARCH_SIZE = 0xF0;
    constexpr uint32_t RAW_ID_CHECK_OFFSET = 8;
    constexpr uint32_t RAW_ID_INSTRUCTION_BYTES = 12;
    const auto createEnemyDataIndex = ResolveMethodBody(context, "app.VariousDataManager", "createEnemyDataIndex()");
    const auto getIDFromFixed = ResolveMethodBody(context, "app.EnemyDef", "getIDFromFixed(app.EnemyDef.ID_Fixed, app.EnemyDef.ID)");
    if (!createEnemyDataIndex || !getIDFromFixed)
        return false;
    const auto createEnemyDataIndexGetIDFromFixedCallAddress = reinterpret_cast<uintptr_t>(HookUtils::LookupCallee(createEnemyDataIndex, getIDFromFixed, true, ENEMY_INDEX_SEARCH_SIZE));
    // 只接受已核对的直接调用；0xF0 是查找窗口，不作为跳转目标的边界证明。
    if (!createEnemyDataIndexGetIDFromFixedCallAddress || !HookUtils::LookupFunctionPattern(reinterpret_cast<void*>(createEnemyDataIndexGetIDFromFixedCallAddress), "E8 ? ? ? ?", DIRECT_CALL_SIZE)) {
        _MESSAGE("[EnemyRegistry] enemy-data getIDFromFixed direct call missing");
        return false;
    }
    auto* getIDFromFixedReturnInstructions = reinterpret_cast<void*>(createEnemyDataIndexGetIDFromFixedCallAddress + DIRECT_CALL_SIZE);
    // 当前消费者：TEST AL,AL; JZ failure; MOV EAX,[RSP+2C]; TEST EAX,EAX; JS invalid。
    // 先确认栈中 raw ID 已读入 EAX，且后续仍按符号拒绝负值，再在 TEST EAX 前安装。
    // 两个分支的位移由原生控制；不解码、不要求目标相等，也不缓存跳转地址。
    if (!HookUtils::LookupFunctionPattern(getIDFromFixedReturnInstructions, "84 C0 74 ? 8B 44 24 2C 85 C0 78 ?", RAW_ID_INSTRUCTION_BYTES)) {
        _MESSAGE("[EnemyRegistry] enemy-data raw-ID load/negative-check contract changed");
        return false;
    }
    createEnemyDataIndexRawIdCheckAddress = reinterpret_cast<uintptr_t>(getIDFromFixedReturnInstructions) + RAW_ID_CHECK_OFFSET;
    return true;
}

// 扩容后原生索引写入仍只检查 raw ID 是否为负；另补注册表容量上界，避免越界写。
static bool InstallEnemyDataIndexBoundsHook(const ci::ProviderContext* context) {
    using EnemyRegistry::GetEnemyArrayCapacity;
    uintptr_t createEnemyDataIndexRawIdCheckAddress{};
    if (!FindEnemyDataIndexBoundsCheck(context, createEnemyDataIndexRawIdCheckAddress))
        return false;
    _MESSAGE("[EnemyRegistry] enemy-data bounds ready: indexCheck=%p", reinterpret_cast<void*>(createEnemyDataIndexRawIdCheckAddress));
    static safetyhook::MidHook createEnemyDataIndexValidateRawIdHook = safetyhook::create_mid(reinterpret_cast<void*>(createEnemyDataIndexRawIdCheckAddress), [](SafetyHookContext& registers) {
        // EAX 已读出待写索引的 raw ID，不是 getIDFromFixed 的 bool 返回值。
        // 越界时将低 32 位设为原生无效值 -1；随后的 TEST EAX,EAX / JS 自然跳过写入。
        // 有效 ID 保持原值，不改 RIP/标志位，不另造原生分支的继续地址。
        if (registers.rax >= GetEnemyArrayCapacity())
            registers.rax = UINT32_MAX;
    });
    if (!createEnemyDataIndexValidateRawIdHook) {
        _MESSAGE("[EnemyRegistry] Failed to install createEnemyDataIndexValidateRawIdHook");
        return false;
    }
    return true;
}
} // namespace app::VariousDataManager

namespace app::EnemyDef {
using EnemyRegistry::RegistrationHooks::ResolveMethodBody;
struct TextDiscoveryHookAddresses {
    uintptr_t NameDiscoveryCheckReturnAddress;
    uintptr_t JpNameDiscoveryCheckReturnAddress;
    uintptr_t ExplainDiscoveryCheckReturnAddress;
    uintptr_t EmSpeciesNameDiscoveryCheckReturnAddress;
};

static bool FindTextDiscoveryHookAddresses(const ci::ProviderContext* context, TextDiscoveryHookAddresses& hookAddresses) {
    constexpr uint32_t DIRECT_CALL_SIZE = 5;
    constexpr size_t TEXT_DISCOVERY_SEARCH_SIZE = 0x120;
    const auto isOnReleaseTrigger = ResolveMethodBody(context, "app.EnemyReportUtil", "isOnReleaseTrigger(app.EnemyDef.ID, app.EnemyReportDef.RELEASE_TYPE)");
    if (!isOnReleaseTrigger)
        return false;
    for (const auto& [textMethodSignature, hookReturnAddress] : {
             std::pair{"Name(app.EnemyDef.ID, app.EnemyDef.ROLE_ID, app.EnemyDef.LEGENDARY_ID)", &hookAddresses.NameDiscoveryCheckReturnAddress},
             std::pair{"JpName(app.EnemyDef.ID, app.EnemyDef.ROLE_ID, app.EnemyDef.LEGENDARY_ID)", &hookAddresses.JpNameDiscoveryCheckReturnAddress},
             std::pair{"Explain(app.EnemyDef.ID, app.EnemyDef.ROLE_ID, app.EnemyDef.LEGENDARY_ID)", &hookAddresses.ExplainDiscoveryCheckReturnAddress},
             std::pair{"EmSpeciesName(app.EnemyDef.SPECIES, app.EnemyDef.ID)", &hookAddresses.EmSpeciesNameDiscoveryCheckReturnAddress}}) {
        const auto textMethodBody = ResolveMethodBody(context, "app.EnemyDef", textMethodSignature);
        const auto isOnReleaseTriggerCallAddress = textMethodBody ? reinterpret_cast<uintptr_t>(HookUtils::LookupCallee(textMethodBody, isOnReleaseTrigger, true, TEXT_DISCOVERY_SEARCH_SIZE)) : 0;
        if (!isOnReleaseTriggerCallAddress || !HookUtils::LookupFunctionPattern(reinterpret_cast<void*>(isOnReleaseTriggerCallAddress), "E8 ? ? ? ?", DIRECT_CALL_SIZE)) {
            _MESSAGE("[EnemyRegistry] text discovery check call missing: %s", textMethodSignature);
            return false;
        }
        *hookReturnAddress = isOnReleaseTriggerCallAddress + DIRECT_CALL_SIZE;
    }
    return true;
}

static bool InstallIdentityAndTextDiscoveryHooks(const ci::ProviderContext* context) {
    constexpr uintptr_t BYREF_ADDRESS_MASK = 0x7fffffffffffffffULL;
    using EnemyRegistry::FindEnemyByFixedId;
    using EnemyRegistry::FindEnemyByRawId;
    using EnemyRegistry::GetRegisteredEnemies;
    using FngetIDFromFixed = bool (*)(void*, int32_t, int32_t*);
    using FngetFixedFromID = bool (*)(void*, int32_t, int32_t*);
    using FnisValid = bool (*)(void*, int32_t);
    // CI Context是此阶段现有的TDB入口；RF API此时尚未初始化。
    auto getIDFromFixed = reinterpret_cast<FngetIDFromFixed>(ResolveMethodBody(context, "app.EnemyDef", "getIDFromFixed(app.EnemyDef.ID_Fixed, app.EnemyDef.ID)"));
    auto getFixedFromID = reinterpret_cast<FngetFixedFromID>(ResolveMethodBody(context, "app.EnemyDef", "getFixedFromID(app.EnemyDef.ID, app.EnemyDef.ID_Fixed)"));
    auto isValid = reinterpret_cast<FnisValid>(ResolveMethodBody(context, "app.EnemyDef", "isValid(app.EnemyDef.ID)"));
    TextDiscoveryHookAddresses textDiscoveryHookAddresses{};
    if (!getIDFromFixed || !getFixedFromID || !isValid || !FindTextDiscoveryHookAddresses(context, textDiscoveryHookAddresses))
        return false;
    for (const auto& enemy : GetRegisteredEnemies()) {
        int32_t nativeRawId{};
        // 已反编译确认是纯编译期转换，不读取vmctx、不分配对象；早期仅用于身份冲突检查。
        if (getIDFromFixed(nullptr, enemy.fixedId, &nativeRawId)) {
            _MESSAGE("[EnemyRegistry] fixed ID collides with native identity: "
                     "fixed=%d native raw=%d",
                     enemy.fixedId, nativeRawId);
            return false;
        }
    }
    // 原生编译期转换表不认识新增 fixed ID；注册表提供 fixed → raw，并隔离被占 raw 的旧别名。
    if (!HookLambda(getIDFromFixed, [](void* vmContext, int32_t fixedId, int32_t* rawIdOutput) -> bool {
            // 仅解引用时去掉输出指针的最高位标记；调用 original 仍传入未经修改的参数。
            auto* rawIdPointer = reinterpret_cast<int32_t*>(reinterpret_cast<uintptr_t>(rawIdOutput) & BYREF_ADDRESS_MASK);
            if (const auto* enemy = FindEnemyByFixedId(fixedId)) {
                if (!rawIdPointer)
                    return false;
                *rawIdPointer = enemy->rawId;
                return true;
            }
            const bool converted = original(vmContext, fixedId, rawIdOutput);
            if (converted && rawIdPointer && FindEnemyByRawId(*rawIdPointer)) {
                // 该 raw 槽已归注册怪物，原生旧 fixed ID 不应再指向它；输出失败哨兵并返回 false。
                *rawIdPointer = -1;
                return false;
            }
            return converted; // 仅隔离这个保留槽的旧fixed别名。
        })) {
        _MESSAGE("[EnemyRegistry] Failed to install getIDFromFixed hook");
        return false;
    }
    // raw → fixed 必须与上面的正向映射成对；非注册身份仍交给原生转换。
    if (!HookLambda(getFixedFromID, [](void* vmContext, int32_t rawId, int32_t* fixedIdOutput) -> bool {
            const auto* enemy = FindEnemyByRawId(rawId);
            if (!enemy)
                return original(vmContext, rawId, fixedIdOutput);
            auto* fixedIdPointer = reinterpret_cast<int32_t*>(reinterpret_cast<uintptr_t>(fixedIdOutput) & BYREF_ADDRESS_MASK);
            if (!fixedIdPointer)
                return false;
            *fixedIdPointer = enemy->fixedId;
            return true;
        })) {
        _MESSAGE("[EnemyRegistry] Failed to install getFixedFromID hook");
        return false;
    }
    // 原生有效性判断只覆盖原版 ID；让已注册 raw 通过，不扩大其它未知 ID 的有效范围。
    if (!HookLambda(isValid, [](void* vmContext, int32_t rawId) -> bool {
            return FindEnemyByRawId(rawId) || original(vmContext, rawId);
        })) {
        _MESSAGE("[EnemyRegistry] Failed to install isValid hook");
        return false;
    }
    // 新增身份没有原版图鉴存档槽：注册时名称即公开，仅修四个文本入口的
    // TYPE_1 查询结果，不全局解锁图鉴、不写存档；其它剧情隐藏与历战分支照常执行。
    // hook 在 isOnReleaseTrigger 调用后：RAX 是 bool 返回值；此时参数寄存器可能已被
    // callee 改写，raw ID 要读调用者保留的 EDI（Name/JpName/Explain），而不是 EDX。
    // 对注册怪物令 RAX=1，供后续原生条件分支消费；不改 RIP，其余文本选择逻辑继续执行。
    const auto AllowRegisteredEnemyText = [](SafetyHookContext& registers) {
        if (FindEnemyByRawId(static_cast<int32_t>(registers.rdi)))
            registers.rax = 1;
    };
    static auto NameDiscoveryCheckHook = safetyhook::create_mid(reinterpret_cast<void*>(textDiscoveryHookAddresses.NameDiscoveryCheckReturnAddress), AllowRegisteredEnemyText);
    static auto JpNameDiscoveryCheckHook = safetyhook::create_mid(reinterpret_cast<void*>(textDiscoveryHookAddresses.JpNameDiscoveryCheckReturnAddress), AllowRegisteredEnemyText);
    static auto ExplainDiscoveryCheckHook = safetyhook::create_mid(reinterpret_cast<void*>(textDiscoveryHookAddresses.ExplainDiscoveryCheckReturnAddress), AllowRegisteredEnemyText);
    static auto EmSpeciesNameDiscoveryCheckHook = safetyhook::create_mid(reinterpret_cast<void*>(textDiscoveryHookAddresses.EmSpeciesNameDiscoveryCheckReturnAddress), [](SafetyHookContext& registers) {
        // 物种文本入口将 raw ID 保存在 EBP（不是上面三个入口的 EDI）；同样只替换 RAX 的 bool。
        if (FindEnemyByRawId(static_cast<int32_t>(registers.rbp)))
            registers.rax = 1;
    });
    if (!NameDiscoveryCheckHook || !JpNameDiscoveryCheckHook || !ExplainDiscoveryCheckHook || !EmSpeciesNameDiscoveryCheckHook) {
        _MESSAGE("[EnemyRegistry] text discovery hooks FAILED: name=%d jp=%d explain=%d species=%d", bool(NameDiscoveryCheckHook), bool(JpNameDiscoveryCheckHook), bool(ExplainDiscoveryCheckHook), bool(EmSpeciesNameDiscoveryCheckHook));
        return false;
    }
    _MESSAGE("[EnemyRegistry] registered enemy text visible: name=%p jp=%p explain=%p species=%p; native report/save rules unchanged", reinterpret_cast<void*>(textDiscoveryHookAddresses.NameDiscoveryCheckReturnAddress), reinterpret_cast<void*>(textDiscoveryHookAddresses.JpNameDiscoveryCheckReturnAddress), reinterpret_cast<void*>(textDiscoveryHookAddresses.ExplainDiscoveryCheckReturnAddress), reinterpret_cast<void*>(textDiscoveryHookAddresses.EmSpeciesNameDiscoveryCheckReturnAddress));
    return true;
}
} // namespace app::EnemyDef

namespace app::IconDef {
using EnemyRegistry::RegistrationHooks::ResolveMethodBody;
// UDE 的 _BossIconType 存内部 iconFixedId；原生 UI 再将它转换成共享图集槽位。
// 原生转换表只认识原版编号，这里接通启动合图时生成的 iconFixedId → iconAtlasIndex。
static bool InstallAtlasIndexConversionHook(const ci::ProviderContext* context) {
    constexpr uintptr_t BYREF_ADDRESS_MASK = 0x7fffffffffffffffULL;
    using EnemyRegistry::Enemy;
    using EnemyRegistry::GetRegisteredEnemies;
    using FngetENEMYFromFixed = bool (*)(void*, int32_t, int32_t*);
    auto getENEMYFromFixed = reinterpret_cast<FngetENEMYFromFixed>(ResolveMethodBody(context, "app.IconDef", "getENEMYFromFixed(app.IconDef.ENEMY_Fixed, app.IconDef.ENEMY)"));
    if (!getENEMYFromFixed)
        return false;
    if (!HookLambda(getENEMYFromFixed, [](void* vmContext, int32_t iconFixedId, int32_t* atlasIndexOutput) -> bool {
            const Enemy* enemy = nullptr;
            for (const auto& registeredEnemy : GetRegisteredEnemies())
                if (registeredEnemy.iconFixedId >= 0 && registeredEnemy.iconFixedId == iconFixedId) {
                    enemy = &registeredEnemy;
                    break;
                }
            if (!enemy)
                return original(vmContext, iconFixedId, atlasIndexOutput);
            auto* atlasIndexPointer = reinterpret_cast<int32_t*>(reinterpret_cast<uintptr_t>(atlasIndexOutput) & BYREF_ADDRESS_MASK);
            if (!atlasIndexPointer)
                return false;
            // 写出已发布图集的槽位并返回成功，后续 UV/材质/绘制仍走原生链路；
            // 这里不是怪物 fixedId → rawId，也不在绘制时切换纹理或材质。
            *atlasIndexPointer = enemy->iconAtlasIndex;
            return true;
        })) {
        _MESSAGE("[EnemyRegistry] Failed to install getENEMYFromFixed hook");
        return false;
    }
    return true;
}
} // namespace app::IconDef

namespace EnemyRegistry::RegistrationHooks {
bool InstallHooks(const ci::ProviderContext* context) {
    if (!context || !context->GetType || !context->GetMethod || !context->IsDerivedFrom)
        return false;
    if (GetRegisteredEnemies().empty())
        return true;
    std::vector<CapacityImmediatePatch> capacityPatches;
    if (!ResolveEnemyCapacityPatches(context, capacityPatches))
        return false;
    // 只核对命名派生的类型；原生工厂自行ToString/Substring/GetType，不替换返回值。
    for (const auto& enemy : GetRegisteredEnemies()) {
        auto stem = enemy.enumName.substr(0, 9);
        stem[1] = 'm';
        const auto extend = "app.c" + stem + "Extend";
        const auto contextType = "app.c" + stem + "ContextUnique";
        if (!context->IsDerivedFrom(context->GetType(extend.c_str()), context->GetType("app.cEnemyBossZakoExtend")) ||
            !context->IsDerivedFrom(context->GetType(contextType.c_str()), context->GetType("app.cEnemyContextUnique"))) {
            _MESSAGE("[EnemyRegistry] derived factory type contract failed: %s / %s", extend.c_str(), contextType.c_str());
            return false;
        }
    }
    // 必须在 EnemyDef hook 改写入口前定位原始 getIDFromFixed 调用。
    if (!app::VariousDataManager::InstallEnemyDataIndexBoundsHook(context))
        return false;
    // 容量和原生消费者先就绪，最后才公开新增 raw ID。
    for (const auto& patch : capacityPatches)
        HookUtils::SafeWrite32(patch.address, patch.value);
    if (!app::IconDef::InstallAtlasIndexConversionHook(context))
        return false;
    if (!app::EnemyDef::InstallIdentityAndTextDiscoveryHooks(context))
        return false;
    _MESSAGE("[EnemyRegistry] early ID/icon/native-name contract and %zu capacity "
             "instructions ready: enemies=%zu count=%u",
             capacityPatches.size(), GetRegisteredEnemies().size(), GetEnemyArrayCapacity());
    return true;
}
} // namespace EnemyRegistry::RegistrationHooks
