#pragma once
#include "ClassProvider.h"
#include <vector>
namespace EnemyRegistry::RegistrationHooks {
struct CapacityImmediatePatch {
    uintptr_t address;
    uint32_t value;
};
void* ResolveMethodBody(const ci::ProviderContext* context, const char* typeName, const char* signature);
bool ResolveEnemyCapacityPatches(const ci::ProviderContext* context, std::vector<CapacityImmediatePatch>& patches);

// CI预静态构造边界中一次安装；类型仍由各怪物provider注入。
bool InstallHooks(const ci::ProviderContext* context);
} // namespace EnemyRegistry::RegistrationHooks
