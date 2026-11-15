#pragma once
#include "EnemyIdRegistry.h"
#include <filesystem>
#include <vector>

namespace EnemyRegistry {
// File-only startup step, before publishing the immutable enemy registry.
void BuildSharedIconAtlas(const std::filesystem::path& gameRoot, std::vector<Enemy>& configs);
}
