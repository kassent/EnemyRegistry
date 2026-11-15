#pragma once
#include "EnemyRegistrationHookAddressTests.h"
#include <Windows.h>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>

// 执行真实生产定位器和回调，验证 relocated TEST/JS；不冒充游戏资源或实机验收。
namespace EnemyDataIndexMidHookTests {
static void Run() {
    // 先构造静态代码所有者，后初始化生产 static hook，保证退出时先卸 hook 再释放代码。
    using FnReleaseCode = void (*)(uint8_t*);
    static std::unique_ptr<uint8_t, FnReleaseCode> code{static_cast<uint8_t*>(VirtualAlloc(nullptr, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE)), +[](uint8_t* memory) {
                                                            VirtualFree(memory, 0, MEM_RELEASE);
                                                        }};
    if (!code)
        throw std::runtime_error("enemy-data fixture allocation failed");
    auto* function = code.get();
    auto* converter = function + 0x100;
    auto* writtenRawId = reinterpret_cast<uint32_t*>(function + 0x200);
    auto* writeCount = reinterpret_cast<uint32_t*>(function + 0x204);
    const uint8_t instructions[]{
        0x48, 0x83, 0xEC, 0x38,        // SUB RSP,38: shadow space and raw-ID local.
        0x4C, 0x8D, 0x44, 0x24, 0x2C,  // LEA R8,[RSP+2C]: conversion output.
        0xE8, 0, 0, 0, 0,              // CALL converter.
        0x84, 0xC0, 0x74, 0x14,        // TEST AL,AL; JZ skipWrite.
        0x8B, 0x44, 0x24, 0x2C,        // MOV EAX,[RSP+2C].
        0x85, 0xC0, 0x78, 0x0C,        // TEST EAX,EAX; JS skipWrite: production hook.
        0x89, 0x05, 0, 0, 0, 0,        // MOV [writtenRawId],EAX.
        0xFF, 0x05, 0, 0, 0, 0,        // INC [writeCount].
        0x48, 0x83, 0xC4, 0x38, 0xC3}; // skipWrite: ADD RSP,38; RET.
    const uint8_t conversion[]{
        0x41, 0x89, 0x10,          // MOV [R8],EDX: raw ID supplied by test.
        0x31, 0xC0,                // XOR EAX,EAX.
        0x81, 0xFA, 0, 0, 0, 0x80, // CMP EDX,INT32_MIN: explicit conversion failure.
        0x0F, 0x95, 0xC0, 0xC3};   // SETNE AL; RET.
    std::memcpy(function, instructions, sizeof(instructions));
    std::memcpy(converter, conversion, sizeof(conversion));
    const auto WriteDisplacement = [](uint8_t* instruction, size_t length, uint8_t* target, size_t offset) {
        const auto displacement = static_cast<int32_t>(target - (instruction + length));
        std::memcpy(instruction + offset, &displacement, sizeof(displacement));
    };
    WriteDisplacement(function + 9, 5, converter, 1);
    WriteDisplacement(function + 26, 6, reinterpret_cast<uint8_t*>(writtenRawId), 2);
    WriteDisplacement(function + 32, 6, reinterpret_cast<uint8_t*>(writeCount), 2);
    FlushInstructionCache(GetCurrentProcess(), function, 0x1000);
    using FnCreateEnemyDataIndex = uint32_t (*)(void*, int32_t);
    const auto InvokeEnemyDataIndex = reinterpret_cast<FnCreateEnemyDataIndex>(function);
    const auto capacity = EnemyRegistry::GetEnemyArrayCapacity();
    // 原生只有负值检查：上界值在安装前会进入写路径。
    InvokeEnemyDataIndex(nullptr, static_cast<int32_t>(capacity));
    if (*writeCount != 1 || *writtenRawId != capacity)
        throw std::runtime_error("unhooked enemy-data fixture did not enter the write path");
    static ci::Type type;
    const ci::ProviderContext context{
        .GetType = [](const char*) -> const ci::Type* {
            return &type;
        },
        .GetMethod = [](const ci::Type*, const char* signature) -> void* {
            if (std::strcmp(signature, "createEnemyDataIndex()") == 0)
                return code.get();
            if (std::strcmp(signature, "getIDFromFixed(app.EnemyDef.ID_Fixed, app.EnemyDef.ID)") == 0)
                return code.get() + 0x100;
            return nullptr;
        }};
    if (!app::VariousDataManager::InstallEnemyDataIndexBoundsHook(&context))
        throw std::runtime_error("production enemy-data bounds hook installation failed");
    for (const auto rawId : {0, 149, 150, static_cast<int32_t>(capacity) - 1,
                             static_cast<int32_t>(capacity), static_cast<int32_t>(capacity) + 1,
                             INT32_MAX, -1, INT32_MIN}) {
        *writeCount = 0;
        *writtenRawId = 0x12345678;
        const auto returnedRawId = InvokeEnemyDataIndex(nullptr, rawId);
        const auto valid = rawId >= 0 && static_cast<uint32_t>(rawId) < capacity;
        if (*writeCount != (valid ? 1U : 0U) || *writtenRawId != (valid ? static_cast<uint32_t>(rawId) : 0x12345678U))
            throw std::runtime_error("production enemy-data hook changed a valid write or failed to skip an invalid write");
        if (valid && returnedRawId != static_cast<uint32_t>(rawId))
            throw std::runtime_error("production enemy-data hook changed a valid raw ID");
        if (!valid && rawId != INT32_MIN && returnedRawId != UINT32_MAX)
            throw std::runtime_error("production enemy-data hook did not use the native negative sentinel");
    }
    std::cout << "PASS: production enemy-data hook, relocated native TEST/JS, capacity boundaries and conversion failure\n";
}
} // namespace EnemyDataIndexMidHookTests
