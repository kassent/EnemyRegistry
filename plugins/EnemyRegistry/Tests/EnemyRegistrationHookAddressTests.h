#pragma once
#include <GameTypes.h>
#include <array>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string_view>

// 将文件内定位与安装函数编入测试；安装入口仅测试失败路径，不对替身代码安装 hook。
#include "../EnemyRegistrationHooks.cpp"

namespace EnemyRegistrationHookAddressTests {
enum MethodIndex {
    GET_ID_FROM_FIXED,
    ENEMY_DATA_INDEX,
    IS_ON_RELEASE_TRIGGER,
    ENEMY_NAME,
    ENEMY_JP_NAME,
    ENEMY_EXPLAIN,
    ENEMY_SPECIES_NAME,
    METHOD_COUNT,
};
static std::array<std::array<uint8_t, 0x400>, METHOD_COUNT> methods;
static ci::Type type;

// 仅提供仍由当前生产安装器消费的 TDB 入口。
static void* GetMethod(const ci::Type*, const char* signature) {
    const std::string_view name{signature};
    for (const auto& [candidate, index] : {
             std::pair{"getIDFromFixed(app.EnemyDef.ID_Fixed, app.EnemyDef.ID)", GET_ID_FROM_FIXED},
             std::pair{"createEnemyDataIndex()", ENEMY_DATA_INDEX},
             std::pair{"isOnReleaseTrigger(app.EnemyDef.ID, app.EnemyReportDef.RELEASE_TYPE)", IS_ON_RELEASE_TRIGGER},
             std::pair{"Name(app.EnemyDef.ID, app.EnemyDef.ROLE_ID, app.EnemyDef.LEGENDARY_ID)", ENEMY_NAME},
             std::pair{"JpName(app.EnemyDef.ID, app.EnemyDef.ROLE_ID, app.EnemyDef.LEGENDARY_ID)", ENEMY_JP_NAME},
             std::pair{"Explain(app.EnemyDef.ID, app.EnemyDef.ROLE_ID, app.EnemyDef.LEGENDARY_ID)", ENEMY_EXPLAIN},
             std::pair{"EmSpeciesName(app.EnemyDef.SPECIES, app.EnemyDef.ID)", ENEMY_SPECIES_NAME}}) {
        if (name == candidate)
            return methods[index].data();
    }
    return nullptr;
}

// 在同一静态缓冲区组中写入 rel32 调用/跳转，只供生产扫描器读取，不执行这些字节。
static void WriteRelativeBranch(MethodIndex caller, size_t offset, MethodIndex callee, uint8_t opcode = 0xE8) {
    auto* site = methods[caller].data() + offset;
    const auto distance = reinterpret_cast<intptr_t>(methods[callee].data()) - reinterpret_cast<intptr_t>(site + 5);
    if (distance < INT32_MIN || distance > INT32_MAX)
        throw std::runtime_error("fixture rel32 is out of range");
    const auto displacement = static_cast<int32_t>(distance);
    site[0] = opcode;
    memcpy(site + 1, &displacement, sizeof(displacement));
}

// 只构造当前目标 CALL 与实际验证的 raw-ID 分支契约。
static void InitializeMethods() {
    for (auto& code : methods)
        code.fill(0x90);
    WriteRelativeBranch(ENEMY_DATA_INDEX, 0x40, GET_ID_FROM_FIXED);
    constexpr uint8_t RAW_BRANCHES[]{0x84, 0xC0, 0x74, 0x20, 0x8B, 0x44, 0x24, 0x2C, 0x85, 0xC0, 0x78, 0x18};
    memcpy(methods[ENEMY_DATA_INDEX].data() + 0x45, RAW_BRANCHES, sizeof(RAW_BRANCHES));
    for (const auto index : {ENEMY_NAME, ENEMY_JP_NAME, ENEMY_EXPLAIN, ENEMY_SPECIES_NAME})
        WriteRelativeBranch(index, 0x50, IS_ON_RELEASE_TRIGGER);
}

// 覆盖调用点、缺失调用和原生分支目标；不把替身结果当实机证明。
static void Run() {
    InitializeMethods();
    const ci::ProviderContext context{.GetType = [](const char*) -> const ci::Type* {
                                  return &type;
                              },
                              .GetMethod = GetMethod};
    uintptr_t createEnemyDataIndexRawIdCheckAddress{};
    const auto GetAddress = [](MethodIndex index, size_t offset) {
        return reinterpret_cast<uintptr_t>(methods[index].data() + offset);
    };
    if (!app::VariousDataManager::FindEnemyDataIndexBoundsCheck(&context, createEnemyDataIndexRawIdCheckAddress) || createEnemyDataIndexRawIdCheckAddress != GetAddress(ENEMY_DATA_INDEX, 0x4D))
        throw std::runtime_error("enemy-data raw-ID instruction contract failed");
    InitializeMethods();
    methods[ENEMY_DATA_INDEX][0x40] = 0x90;
    if (app::VariousDataManager::FindEnemyDataIndexBoundsCheck(&context, createEnemyDataIndexRawIdCheckAddress))
        throw std::runtime_error("enemy-data accepted a missing target call");
    for (const auto offset : {0, 1, 2, 4, 5, 6, 7, 8, 9, 10}) {
        InitializeMethods();
        methods[ENEMY_DATA_INDEX][0x45 + offset] ^= 1;
        if (app::VariousDataManager::FindEnemyDataIndexBoundsCheck(&context, createEnemyDataIndexRawIdCheckAddress))
            throw std::runtime_error("enemy-data accepted a changed raw-ID instruction contract");
    }
    // 分支目的地不属于 hook 状态：原生有不同失败路径时仍按各自的原生分支执行。
    InitializeMethods();
    methods[ENEMY_DATA_INDEX][0x45 + 11] = 0x19;
    if (!app::VariousDataManager::FindEnemyDataIndexBoundsCheck(&context, createEnemyDataIndexRawIdCheckAddress) || createEnemyDataIndexRawIdCheckAddress != GetAddress(ENEMY_DATA_INDEX, 0x4D))
        throw std::runtime_error("enemy-data depends on matching native branch destinations");
    InitializeMethods();
    methods[ENEMY_DATA_INDEX][0x40] = 0xE9;
    if (app::VariousDataManager::FindEnemyDataIndexBoundsCheck(&context, createEnemyDataIndexRawIdCheckAddress))
        throw std::runtime_error("enemy-data accepted JMP instead of call");
    app::EnemyDef::TextDiscoveryHookAddresses textDiscoveryHookAddresses{};
    if (!app::EnemyDef::FindTextDiscoveryHookAddresses(&context, textDiscoveryHookAddresses) || textDiscoveryHookAddresses.NameDiscoveryCheckReturnAddress != GetAddress(ENEMY_NAME, 0x55) || textDiscoveryHookAddresses.JpNameDiscoveryCheckReturnAddress != GetAddress(ENEMY_JP_NAME, 0x55) || textDiscoveryHookAddresses.ExplainDiscoveryCheckReturnAddress != GetAddress(ENEMY_EXPLAIN, 0x55) || textDiscoveryHookAddresses.EmSpeciesNameDiscoveryCheckReturnAddress != GetAddress(ENEMY_SPECIES_NAME, 0x55))
        throw std::runtime_error("text discovery hook addresses differ from TDB target calls");
    for (const auto index : {ENEMY_NAME, ENEMY_JP_NAME, ENEMY_EXPLAIN, ENEMY_SPECIES_NAME}) {
        for (const auto opcode : {0x90, 0xE9}) {
            InitializeMethods();
            methods[index][0x50] = static_cast<uint8_t>(opcode);
            if (app::EnemyDef::FindTextDiscoveryHookAddresses(&context, textDiscoveryHookAddresses))
                throw std::runtime_error("name visibility accepted a missing release query or JMP");
        }
    }
    std::cout << "PASS: TDB target calls, missing/JMP rejection and raw-ID instruction contracts\n";
}
} // namespace EnemyRegistrationHookAddressTests
