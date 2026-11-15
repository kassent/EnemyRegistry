#include "EnemyMessageOverrides.h"
#include "EnemyIdRegistry.h"
#include <GameTypes.h>
#include <HookUtils.h>
#include <LogUtils.h>
#include <array>
#include <cstring>
#include <map>
#include <string>
#include <utility>

namespace via::gui::message {
using MessageKey = std::array<uint8_t, sizeof(RE::SystemGuid)>;
struct MessageText {
    RE::SystemGuid guid;
    std::array<std::wstring, EnemyRegistry::Message::LANGUAGE_COUNT> texts;

    MessageText(RE::SystemGuid id, const EnemyRegistry::Message& message)
        : guid{id} {
        // Convert once at installation. Native callbacks only select a stable
        // UTF-16 buffer, with no JSON lookup, allocation or encoding work.
        for (const auto& [language, tag] : EnemyRegistry::Message::LANGUAGES)
            if (const auto found = message.texts.find(tag); found != message.texts.end())
                texts[language] = RE::widen(found->second);
    }

    const wchar_t* GetText(int32_t language) const {
        if (language >= 0 && language < EnemyRegistry::Message::LANGUAGE_COUNT && !texts[language].empty())
            return texts[language].c_str();
        return texts[EnemyRegistry::Message::ENGLISH_LANGUAGE].c_str();
    }
};
bool InstallHooks() {
    constexpr int32_t CALL_DISPLACEMENT_OFFSET = 1;
    constexpr uint32_t DIRECT_CALL_SIZE = 5;
    constexpr uint32_t GET_WITH_LANGUAGE_SEARCH_SIZE = 0x200;
    constexpr uint32_t GET_SEARCH_SIZE = 0x90;
    constexpr uint32_t GET_MESSAGE_ID_SEARCH_SIZE = 0x70;
    std::map<MessageKey, MessageText> resolvedMessages;
    for (const auto& enemy : EnemyRegistry::GetRegisteredEnemies()) {
        for (const auto& message : enemy.messages) {
            RE::SystemGuid guid;
            if (!guid.from_str(message.guid)) {
                _MESSAGE("[Messages] invalid GUID: fixed=%d guid=%s",
                         enemy.fixedId, message.guid.c_str());
                return false;
            }
            MessageKey key{};
            std::memcpy(key.data(), &guid, sizeof(guid));
            if (!resolvedMessages.try_emplace(key, guid, message).second) {
                _MESSAGE("[Messages] conflicting custom GUID: %s",
                         message.guid.c_str());
                return false;
            }
        }
    }
    if (resolvedMessages.empty())
        return true;
    // 复用EnhancedOuterItem/UnlimitedTransmog已验证的原生消息消费者；不替换公共MSG文件。
    // TDB 的两个入口都叫 get；WithLanguage 仅用于区分带 via.Language 的重载。
    // 内部调用目标没有对应的 TDB 方法名，按返回内容命名为 getText / getTextWithLanguage。
    using FnGetWithLanguage = RE::SystemString* (*)(void*, RE::SystemGuid*, int32_t);
    auto getWithLanguage = RE::get_method<FnGetWithLanguage>("via.gui.message", "get(System.Guid, via.Language)");
    const auto getTextWithLanguageCall = getWithLanguage ? HookUtils::LookupFunctionPattern(getWithLanguage, "E8 ? ? ? ? 48 85 C0", GET_WITH_LANGUAGE_SEARCH_SIZE) : 0;
    using FnGetTextWithLanguage = const wchar_t* (*)(void*, RE::SystemGuid*, int32_t, void*);
    auto getTextWithLanguage = getTextWithLanguageCall ? reinterpret_cast<FnGetTextWithLanguage>(HookUtils::ReadOffsetData(getTextWithLanguageCall, CALL_DISPLACEMENT_OFFSET, DIRECT_CALL_SIZE)) : nullptr;
    using FnGet = RE::SystemString* (*)(void*, RE::SystemGuid*);
    auto get = RE::get_method<FnGet>("via.gui.message", "get(System.Guid)");
    const auto getTextCall = get ? HookUtils::LookupFunctionPattern(get, "E8 ? ? ? ? 48 85 C0", GET_SEARCH_SIZE) : 0;
    using FnGetText = const wchar_t* (*)(void*, RE::SystemGuid*, void*);
    auto getText = getTextCall ? reinterpret_cast<FnGetText>(HookUtils::ReadOffsetData(getTextCall, CALL_DISPLACEMENT_OFFSET, DIRECT_CALL_SIZE)) : nullptr;
    using FnGet_MessageLanguage = int32_t (*)(void*);
    const auto get_MessageLanguageAddress = RE::get_method<FnGet_MessageLanguage>("via.gui.GUISystem", "get_MessageLanguage()");
    using FnGetMessageId = RE::SystemGuid* (*)(RE::SystemGuid*, void*, RE::SystemGuid*, int32_t);
    auto getMessageId = RE::get_method<FnGetMessageId>("via.gui.message", "getMessageId(System.Guid, System.Int32)");
    // getMessageId 内部按相对页号解析 GUID；返回的是借用指针，而非 TDB 入口的值类型返回缓冲。
    const auto getMessagePageIdCall = getMessageId ? HookUtils::LookupFunctionPattern(getMessageId, "E8 ? ? ? ? 8B 08 89 0E", GET_MESSAGE_ID_SEARCH_SIZE) : 0;
    using FnGetMessagePageId = const RE::SystemGuid* (*)(void*, RE::SystemGuid*, int32_t);
    auto getMessagePageId = getMessagePageIdCall ? reinterpret_cast<FnGetMessagePageId>(HookUtils::ReadOffsetData(getMessagePageIdCall, CALL_DISPLACEMENT_OFFSET, DIRECT_CALL_SIZE)) : nullptr;
    if (!getTextWithLanguage || !getText || !get_MessageLanguageAddress || !getMessagePageId) {
        _MESSAGE("[Messages] native text consumer missing: explicit=%p current=%p language=%p page=%p", getTextWithLanguage, getText, get_MessageLanguageAddress, getMessagePageId);
        return false;
    }
    // 原生消息系统借用返回的 UTF-16 指针；成功解析后发布不可变文本，保留到进程结束。
    static const auto messagesByGuid = std::move(resolvedMessages);
    // 该 TDB 静态入口只读取原生 GUI 语言，无托管对象创建；每次查询，支持语言切换。
    static const auto get_MessageLanguage = get_MessageLanguageAddress;
    // 三个内部消费者的首参是原生消息库，不是 TDB 方法的 VM context。
    const bool getTextWithLanguageHookInstalled = HookLambda(getTextWithLanguage, [](void* messageDatabase, RE::SystemGuid* guid, int32_t language, void* unused) -> const wchar_t* {
        MessageKey key{};
        std::memcpy(key.data(), guid, sizeof(*guid));
        if (const auto found = messagesByGuid.find(key); found != messagesByGuid.end()) {
            return found->second.GetText(language);
        }
        return original(messageDatabase, guid, language, unused);
    });
    // 默认重载及原生 GUI 的 GUID 文本消费者走另一入口，不会经过显式语言版本。
    const bool getTextHookInstalled = HookLambda(getText, [](void* messageDatabase, RE::SystemGuid* guid, void* unused) -> const wchar_t* {
        MessageKey key{};
        std::memcpy(key.data(), guid, sizeof(*guid));
        if (const auto found = messagesByGuid.find(key); found != messagesByGuid.end())
            return found->second.GetText(get_MessageLanguage(nullptr));
        return original(messageDatabase, guid, unused);
    });
    // app.MessageUtil/GUIMessageManager 先解析相对页 GUID，再取文本；无原生 MSG 行时
    // 该步骤原本把有效自有 GUID 变成全零。声明只有单页，只承接相对页 0。
    const bool getMessagePageIdHookInstalled = HookLambda(getMessagePageId, [](void* messageDatabase, RE::SystemGuid* guid, int32_t relativePage) -> const RE::SystemGuid* {
        if (relativePage == 0) {
            MessageKey key{};
            std::memcpy(key.data(), guid, sizeof(*guid));
            if (const auto found = messagesByGuid.find(key); found != messagesByGuid.end())
                return &found->second.guid;
        }
        return original(messageDatabase, guid, relativePage);
    });
    _MESSAGE("[Messages] explicit=%s current=%s page=%s: custom GUIDs=%zu; public EnemyText.msg unchanged", getTextWithLanguageHookInstalled ? "ready" : "FAILED", getTextHookInstalled ? "ready" : "FAILED", getMessagePageIdHookInstalled ? "ready" : "FAILED", messagesByGuid.size());
    return getTextWithLanguageHookInstalled && getTextHookInstalled && getMessagePageIdHookInstalled;
}
} // namespace via::gui::message
