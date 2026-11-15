#include "EnemyQuestVisibility.h"
#include "EnemyIdRegistry.h"
#include <GameTypes.h>
#include <HookUtils.h>
#include <LogUtils.h>

namespace app::QuestUtil {
bool InstallHooks() {
    constexpr uint8_t REL32_JUMP_OPCODE = 0xE9;
    constexpr int32_t REL32_DISPLACEMENT_OFFSET = 1;
    constexpr uint32_t REL32_INSTRUCTION_SIZE = 5;
    using FnIsDrawQuestDetail = bool (*)(void*, int32_t);
    using FnIsOnReleaseTrigger = bool (*)(void*, int32_t, int32_t);
    const auto isDrawQuestDetail = RE::get_method<FnIsDrawQuestDetail>("app.QuestUtil", "isDrawQuestDetail(app.MissionIDList.ID)");
    const auto isOnReleaseTriggerMethod = RE::get_method<FnIsOnReleaseTrigger>("app.EnemyReportUtil", "isOnReleaseTrigger(app.EnemyDef.ID, app.EnemyReportDef.RELEASE_TYPE)");
    if (!isDrawQuestDetail || !isOnReleaseTriggerMethod) {
        _MESSAGE("[QuestVisibility] native quest/report method missing: quest=%p report=%p", isDrawQuestDetail, isOnReleaseTriggerMethod);
        return false;
    }
    auto* isDrawQuestDetailBody = reinterpret_cast<uint8_t*>(isDrawQuestDetail);
    auto* isOnReleaseTriggerBody = reinterpret_cast<uint8_t*>(isOnReleaseTriggerMethod);
    if (*isDrawQuestDetailBody == REL32_JUMP_OPCODE)
        isDrawQuestDetailBody = reinterpret_cast<uint8_t*>(HookUtils::ReadOffsetData(reinterpret_cast<uintptr_t>(isDrawQuestDetailBody), REL32_DISPLACEMENT_OFFSET, REL32_INSTRUCTION_SIZE));
    if (*isOnReleaseTriggerBody == REL32_JUMP_OPCODE)
        isOnReleaseTriggerBody = reinterpret_cast<uint8_t*>(HookUtils::ReadOffsetData(reinterpret_cast<uintptr_t>(isOnReleaseTriggerBody), REL32_DISPLACEMENT_OFFSET, REL32_INSTRUCTION_SIZE));
    auto* isOnReleaseTriggerCall = static_cast<uint8_t*>(HookUtils::LookupCallee(isDrawQuestDetailBody, isOnReleaseTriggerBody));
    if (!isOnReleaseTriggerCall || !HookUtils::LookupFunctionPattern(isOnReleaseTriggerCall, "E8 ? ? ? ?", REL32_INSTRUCTION_SIZE)) {
        _MESSAGE("[QuestVisibility] report E8 call missing in quest detail predicate: quest=%p report=%p call=%p", isDrawQuestDetailBody, isOnReleaseTriggerBody, isOnReleaseTriggerCall);
        return false;
    }
    const auto* releaseType = reframework::API::get()->tdb()->find_type("app.EnemyReportDef.RELEASE_TYPE");
    const auto* nameReleaseTypeField = releaseType ? releaseType->find_field("TYPE_1") : nullptr;
    if (!nameReleaseTypeField) {
        _MESSAGE("[QuestVisibility] report name release enum missing: TYPE_1");
        return false;
    }
    static const auto nameReleaseType = *static_cast<const int32_t*>(nameReleaseTypeField->get_data_raw(nullptr));
    static auto isOnReleaseTriggerCallHook = safetyhook::create_mid(isOnReleaseTriggerCall, [](SafetyHookContext& registers) {
        // At the call boundary Win64 ABI supplies id in EDX and releaseType in
        // R8D, independent of the caller's register allocation and stack layout.
        if (static_cast<int32_t>(registers.r8) != nameReleaseType || !EnemyRegistry::FindEnemyByRawId(static_cast<int32_t>(registers.rdx)))
            return;
        registers.rax = 1;
        // MidHook RIP points at relocated instructions. E8 rel32 is 5 bytes;
        // skip just that CALL, preserving any following relocated instructions.
        registers.rip += REL32_INSTRUCTION_SIZE;
    });
    _MESSAGE("[QuestVisibility] quest TYPE_1 call hook %s: call=%p report=%p; native report/save rules unchanged", isOnReleaseTriggerCallHook ? "ready" : "FAILED", isOnReleaseTriggerCall, isOnReleaseTriggerBody);
    return static_cast<bool>(isOnReleaseTriggerCallHook);
}
} // namespace app::QuestUtil
