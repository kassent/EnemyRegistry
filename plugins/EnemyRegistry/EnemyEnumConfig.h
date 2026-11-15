#pragma once
#include "EnemyIdRegistry.h"
#include <span>
#include <string>

namespace EnemyRegistry {
// 将已完成连续 raw 分配的非空身份列表编译为 UDE 配置，不访问 VM 或写临时文件。
std::string BuildEnumConfig(std::span<const Enemy> enemies);
} // namespace EnemyRegistry
