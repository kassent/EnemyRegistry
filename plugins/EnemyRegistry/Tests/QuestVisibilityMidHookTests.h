#pragma once
#include <Windows.h>
#include <safetyhook.hpp>
#include <cstring>
#include <memory>
#include <stdexcept>

// Executable hook-mechanics fixtures, not a game or quest-visibility acceptance test.
namespace EnemyQuestVisibilityTests {
static void CheckCallBoundary() {
    std::unique_ptr<uint8_t, void (*)(uint8_t*)> code(static_cast<uint8_t*>(VirtualAlloc(nullptr, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE)), +[](uint8_t* memory) { VirtualFree(memory, 0, MEM_RELEASE); });
    if (!code)
        throw std::runtime_error("call-boundary fixture allocation failed");
    auto* function = code.get();
    auto* call = function + 4;
    auto* callee = function + 0x100;
    auto* counter = reinterpret_cast<uint32_t*>(function + 0x200);
    const uint8_t prologue[] = {0x48, 0x83, 0xEC, 0x28}; // Win64 shadow space/alignment.
    const uint8_t epilogue[] = {0x48, 0x83, 0xC4, 0x28, 0xC3};
    std::memcpy(function, prologue, sizeof(prologue));
    const auto writeDisplacement = [](uint8_t* instruction, size_t length, uint8_t* target, size_t offset) {
        const auto value = static_cast<int32_t>(target - (instruction + length));
        std::memcpy(instruction + offset, &value, sizeof(value));
    };
    call[0] = 0xE8;
    writeDisplacement(call, 5, callee, 1);
    if (!HookUtils::LookupFunctionPattern(call, "E8 ? ? ? ?", 5))
        throw std::runtime_error("call-boundary fixture pattern failed");
    std::memcpy(call + 5, epilogue, sizeof(epilogue));
    callee[0] = 0xFF; callee[1] = 0x05; // Count actual callee executions.
    writeDisplacement(callee, 6, reinterpret_cast<uint8_t*>(counter), 2);
    callee[6] = 0x31; callee[7] = 0xC0; callee[8] = 0xC3;
    FlushInstructionCache(GetCurrentProcess(), function, 0x1000);
    using Fn = bool (*)(void*, int32_t, int32_t);
    const auto invoke = reinterpret_cast<Fn>(function);
    if (invoke(nullptr, 150, 0) || *counter != 1)
        throw std::runtime_error("unhooked fixture did not execute the callee");
    auto hook = safetyhook::create_mid(call, [](SafetyHookContext& registers) {
        if (static_cast<int32_t>(registers.r8) == 0 && EnemyRegistry::FindEnemyByRawId(static_cast<int32_t>(registers.rdx))) {
            registers.rax = 1;
            registers.rip += 5;
        }
    });
    if (!hook || !invoke(nullptr, 150, 0) || *counter != 1 || invoke(nullptr, 19, 0) || *counter != 2 || invoke(nullptr, 150, 1) || *counter != 3)
        throw std::runtime_error("call-boundary ABI/relocated CALL skip failed");
}
} // namespace EnemyQuestVisibilityTests
