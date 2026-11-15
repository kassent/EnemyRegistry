#include "../EnemyIdRegistry.h"
#include "EnemyIconAtlasBuild.h"
#include <LogUtils.h>
#include <bc7e_ispc.h>
#include <fstream>
#include <iostream>
#include <string_view>

// Independent test encoder: gather pixels one at a time across the entire
// image, then make one SIMD call. Production gathers and encodes block rows.
static int EncodeReferenceBlocks(const char* input, const char* output, uint32_t width, uint32_t height) {
    if (!width || !height || width % 4 || height % 4)
        return 2;
    std::vector<uint32_t> pixels(size_t(width) * height), grouped(pixels.size());
    std::ifstream stream(input, std::ios::binary);
    if (!stream.read(reinterpret_cast<char*>(pixels.data()), pixels.size() * sizeof(uint32_t)))
        return 2;
    for (uint32_t y = 0; y < height; ++y)
        for (uint32_t x = 0; x < width; ++x)
            grouped[((y / 4) * (width / 4) + x / 4) * 16 + (y % 4) * 4 + x % 4] = pixels[size_t(y) * width + x];
    std::vector<uint64_t> blocks(pixels.size() / 8);
    ispc::bc7e_compress_block_init();
    ispc::bc7e_compress_block_params parameters;
    ispc::bc7e_compress_block_params_init_basic(&parameters, false);
    ispc::bc7e_compress_blocks(static_cast<uint32_t>(pixels.size() / 16), blocks.data(), grouped.data(), &parameters);
    return std::ofstream(output, std::ios::binary).write(reinterpret_cast<const char*>(blocks.data()), blocks.size() * sizeof(uint64_t)) ? 0 : 1;
}

// Runs the real pre-publication file pipeline against a disposable fixture root.
int main(int argc, char** argv) {
    if (argc == 2 && std::string_view(argv[1]) == "--encoder-key") {
        std::cout << EnemyRegistry::ATLAS_ENCODER_KEY;
        return 0;
    }
    if (argc == 2 && std::string_view(argv[1]) == "--output-format") {
        std::cout << (ENEMY_ICON_ATLAS_USE_BC7 ? 98 : 28);
        return 0;
    }
    if (argc == 6 && std::string_view(argv[1]) == "--encode-reference")
        return EncodeReferenceBlocks(argv[2], argv[3], static_cast<uint32_t>(std::stoul(argv[4])), static_cast<uint32_t>(std::stoul(argv[5])));
    if (argc != 2)
        return 2;
    initLogger("EnemyIconAtlasTests");
    if (!EnemyRegistry::LoadConfigs(std::filesystem::path(argv[1]) / "reframework/data/EnemyRegistry"))
        return 1;
    for (const auto& enemy : EnemyRegistry::GetRegisteredEnemies())
        std::cout << enemy.fixedId << " " << enemy.rawId << " " << enemy.iconFixedId << " " << enemy.iconAtlasIndex << "\n";
    return 0;
}
