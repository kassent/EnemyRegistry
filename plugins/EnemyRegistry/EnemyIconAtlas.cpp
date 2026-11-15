#include "EnemyIconAtlas.h"
#include "EnemyIconAtlasBuild.h"
#include <FileUtils.h>
#include <LogUtils.h>
#include <Windows.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <dxgiformat.h>
#include <fstream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <zlib.h>

#if ENEMY_ICON_ATLAS_USE_BC7
#include <bc7e_ispc.h>
#endif

namespace EnemyRegistry {
#if ENEMY_ICON_ATLAS_USE_BC7
static constexpr uint32_t ATLAS_OUTPUT_FORMAT = DXGI_FORMAT_BC7_UNORM;
static constexpr uint32_t BC7_BLOCK_DIMENSION = 4;
static constexpr uint32_t BC7_BYTES_PER_BLOCK = 16;
#else
static constexpr uint32_t ATLAS_OUTPUT_FORMAT = DXGI_FORMAT_R8G8B8A8_UNORM;
#endif

static constexpr int32_t FIRST_ICON_SLOT = 37;
static constexpr int32_t ICON_SLOT_COUNT = 63;
static constexpr uint16_t ATLAS_DIMENSION = 2048;
static constexpr uint16_t ICON_DIMENSION = 100;
static constexpr int32_t ICON_ATLAS_COLUMNS = 20;
static constexpr int32_t MONSTER_ICON_ORIGIN_Y = 800;
static constexpr int32_t FIRST_CUSTOM_ICON_FIXED_ID = 0x60000000;
static constexpr size_t MIP_COUNT = 3;
static constexpr uint32_t RGBA_BYTES_PER_PIXEL = 4;
static constexpr size_t TEX_HEADER_SIZE = 40;
static constexpr size_t FORMAT_OFFSET = 16;
static constexpr size_t MIP_HEADER_SIZE = 16;
static constexpr size_t MIP_PITCH_OFFSET = 8;
static constexpr size_t MIP_SIZE_OFFSET = 12;
static constexpr size_t CHUNK_HEADER_SIZE = 8;
static constexpr size_t CHUNK_DATA_OFFSET_FIELD = 4;
static constexpr size_t MIP_TABLE_SIZE = MIP_COUNT * MIP_HEADER_SIZE;
static constexpr size_t CHUNK_TABLE_OFFSET = TEX_HEADER_SIZE + MIP_TABLE_SIZE;
static constexpr size_t PAYLOAD_OFFSET = CHUNK_TABLE_OFFSET + MIP_COUNT * CHUNK_HEADER_SIZE;

struct AtlasCache {
    uint32_t inputHash;
    uint32_t atlasHash;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(AtlasCache, inputHash, atlasHash)

static uint32_t ComputeCrc32(std::istream& stream) {
    constexpr size_t STREAM_BUFFER_SIZE = 64 * 1024;
    auto crc = crc32(0, Z_NULL, 0);
    std::array<unsigned char, STREAM_BUFFER_SIZE> buffer;
    while (stream.read(reinterpret_cast<char*>(buffer.data()), buffer.size()), stream.gcount())
        crc = crc32(crc, buffer.data(), static_cast<uInt>(stream.gcount()));
    if (!stream.eof() || stream.bad())
        throw std::runtime_error("failed reading atlas hash input");
    return static_cast<uint32_t>(crc);
}

static uint32_t ComputeFileCrc32(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream)
        throw std::runtime_error("failed opening atlas hash input: " + path.string());
    return ComputeCrc32(stream);
}

static std::optional<AtlasCache> ReadAtlasCache(const std::filesystem::path& path) {
    std::ifstream stream(path);
    if (!stream)
        return std::nullopt;
    try {
        return nlohmann::json::parse(stream).get<AtlasCache>();
    } catch (const nlohmann::json::exception& error) {
        _MESSAGE("[IconAtlas] invalid cache, rebuilding: %s", error.what());
        return std::nullopt;
    }
}

struct RgbaMip {
    size_t offset;
    uint32_t pitch;
    uint32_t width;
    uint32_t height;
};
struct RgbaTexture {
    std::string bytes;
    std::array<RgbaMip, MIP_COUNT> mips;
};

template <class T>
static T ReadValue(const std::string& bytes, size_t offset) {
    if (offset > bytes.size() || sizeof(T) > bytes.size() - offset)
        throw std::runtime_error("truncated RGBA8 TEX");
    T value;
    std::memcpy(&value, bytes.data() + offset, sizeof(value));
    return value;
}

static RgbaTexture ReadRgbaTexture(const std::filesystem::path& path, uint16_t dimension) {
    // 对应 REE-Lib Tex.Header；本读取器只接受 Wilds 的单图、三级 mip、RGBA8 raw-chunk 布局。
    constexpr size_t MAGIC_OFFSET = 0;
    constexpr size_t VERSION_OFFSET = 4;
    constexpr size_t WIDTH_OFFSET = 8;
    constexpr size_t HEIGHT_OFFSET = 10;
    constexpr size_t DEPTH_OFFSET = 12;
    constexpr size_t IMAGE_COUNT_OFFSET = 14;
    constexpr size_t MIP_TABLE_SIZE_OFFSET = 15;
    constexpr size_t FLAGS_OFFSET = 28;
    constexpr uint32_t TEX_MAGIC = 0x00584554; // TEX\0
    constexpr uint32_t TEX_VERSION = 241106027;
    constexpr uint16_t TEXTURE_2D_DEPTH = 1;
    constexpr uint8_t SINGLE_IMAGE_COUNT = 1;
    constexpr uint32_t STREAMING_FLAG = 0x1;
    constexpr uint32_t STREAMED_MIP_MASK = 0xe000; // REE-Lib TexFlags.StreamedMipBits，位 13～15。

    constexpr uint32_t ROW_PITCH_ALIGNMENT = 256;

    RgbaTexture texture{FileUtils::ReadBinaryFile(path)};
    const auto& bytes = texture.bytes;
    if (bytes.size() < PAYLOAD_OFFSET)
        throw std::runtime_error("missing/truncated RGBA8 TEX: " + path.string());
    if (ReadValue<uint32_t>(bytes, MAGIC_OFFSET) != TEX_MAGIC || ReadValue<uint32_t>(bytes, VERSION_OFFSET) != TEX_VERSION ||
        ReadValue<uint16_t>(bytes, WIDTH_OFFSET) != dimension || ReadValue<uint16_t>(bytes, HEIGHT_OFFSET) != dimension ||
        ReadValue<uint16_t>(bytes, DEPTH_OFFSET) != TEXTURE_2D_DEPTH || ReadValue<uint8_t>(bytes, IMAGE_COUNT_OFFSET) != SINGLE_IMAGE_COUNT ||
        ReadValue<uint8_t>(bytes, MIP_TABLE_SIZE_OFFSET) != MIP_TABLE_SIZE || ReadValue<uint32_t>(bytes, FORMAT_OFFSET) != DXGI_FORMAT_R8G8B8A8_UNORM ||
        (ReadValue<uint32_t>(bytes, FLAGS_OFFSET) & (STREAMING_FLAG | STREAMED_MIP_MASK)) != 0)
        throw std::runtime_error("expected non-streaming RGBA8_UNORM TEX, 3 mips: " + path.string());
    uint64_t virtualOffset = CHUNK_TABLE_OFFSET;
    size_t physicalOffset = PAYLOAD_OFFSET;
    for (size_t level = 0; level < MIP_COUNT; ++level) {
        const auto width = uint32_t(dimension >> level);
        const auto pitch = (width * RGBA_BYTES_PER_PIXEL + ROW_PITCH_ALIGNMENT - 1) & ~(ROW_PITCH_ALIGNMENT - 1);
        const auto size = pitch * width;
        const auto mipHeaderOffset = TEX_HEADER_SIZE + level * MIP_HEADER_SIZE;
        const auto chunkHeaderOffset = CHUNK_TABLE_OFFSET + level * CHUNK_HEADER_SIZE;
        // mip 描述为虚拟数据偏移/pitch/大小；raw chunk 描述为大小/相对 payload 的物理偏移。
        if (ReadValue<uint64_t>(bytes, mipHeaderOffset) != virtualOffset ||
            ReadValue<uint32_t>(bytes, mipHeaderOffset + MIP_PITCH_OFFSET) != pitch ||
            ReadValue<uint32_t>(bytes, mipHeaderOffset + MIP_SIZE_OFFSET) != size ||
            ReadValue<uint32_t>(bytes, chunkHeaderOffset) != size ||
            ReadValue<uint32_t>(bytes, chunkHeaderOffset + CHUNK_DATA_OFFSET_FIELD) != physicalOffset - PAYLOAD_OFFSET ||
            physicalOffset > bytes.size() || size > bytes.size() - physicalOffset)
            throw std::runtime_error("expected REE-Lib uncompressed TEX mip layout: " + path.string());
        texture.mips[level] = {physicalOffset, pitch, width, width};
        virtualOffset += size;
        physicalOffset += size;
    }
    if (physicalOffset != bytes.size())
        throw std::runtime_error("unexpected RGBA8 TEX trailing data: " + path.string());
    return texture;
}

static std::filesystem::path ResolveAtlasPath(const std::filesystem::path& root, const std::string& relative) {
    const auto path = std::filesystem::path(relative);
    if (relative.empty() || path.is_absolute() || relative.find_first_of("\\:") != std::string::npos ||
        relative.find('\0') != std::string::npos)
        throw std::runtime_error("atlas requires a game-relative path");
    for (const auto& part : path)
        if (part == "..")
            throw std::runtime_error("atlas path traverses game root");
    return root / path;
}

static void WriteAtlasFile(const std::filesystem::path& path, const std::string& bytes) {
    if (std::filesystem::exists(path) && FileUtils::ReadBinaryFile(path) == bytes)
        return;
    auto temporary = path;
    temporary += "." + std::to_string(GetCurrentProcessId()) + ".tmp";
    try {
        if (!FileUtils::WriteBinaryFile(temporary, bytes) ||
            !MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("failed publishing atlas file: " + path.string());
    } catch (...) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        throw;
    }
}

static void ClearReservedSlots(RgbaTexture& atlas) {
    for (size_t level = 0; level < atlas.mips.size(); ++level) {
        const auto& mip = atlas.mips[level];
        const auto size = uint32_t(ICON_DIMENSION) >> level;
        for (int32_t slot = FIRST_ICON_SLOT; slot < FIRST_ICON_SLOT + ICON_SLOT_COUNT; ++slot) {
            const auto left = (slot % ICON_ATLAS_COLUMNS * ICON_DIMENSION) >> level;
            const auto top = (MONSTER_ICON_ORIGIN_Y + slot / ICON_ATLAS_COLUMNS * ICON_DIMENSION) >> level;
            for (size_t row = 0; row < size; ++row)
                std::memset(atlas.bytes.data() + mip.offset + (top + row) * mip.pitch + left * RGBA_BYTES_PER_PIXEL, 0, size * RGBA_BYTES_PER_PIXEL);
        }
    }
}

static void CopyIconMipRows(RgbaTexture& atlas, const RgbaTexture& tile, uint32_t x, uint32_t y) {
    for (size_t level = 0; level < MIP_COUNT; ++level) {
        const auto& source = tile.mips[level];
        const auto& target = atlas.mips[level];
        const auto left = x >> level, top = y >> level;
        if (left + source.width > target.width || top + source.height > target.height)
            throw std::runtime_error("icon rectangle exceeds atlas mip");
        for (size_t row = 0; row < source.height; ++row)
            std::memcpy(atlas.bytes.data() + target.offset + (top + row) * target.pitch + left * RGBA_BYTES_PER_PIXEL,
                        tile.bytes.data() + source.offset + row * source.pitch, source.width * RGBA_BYTES_PER_PIXEL);
    }
}

#if ENEMY_ICON_ATLAS_USE_BC7
static std::string EncodeBc7Texture(const RgbaTexture& atlas) {
    const auto start = std::chrono::steady_clock::now();
    // BuildSharedIconAtlas runs once in CI's file stage, before resource loading.
    // Initialization builds bc7e's lookup tables; no additional ready flag or runtime DLL.
    ispc::bc7e_compress_block_init();
    ispc::bc7e_compress_block_params parameters;
    ispc::bc7e_compress_block_params_init_basic(&parameters, false);
    std::string bytes = atlas.bytes.substr(0, PAYLOAD_OFFSET);
    const uint32_t format = DXGI_FORMAT_BC7_UNORM;
    std::memcpy(bytes.data() + FORMAT_OFFSET, &format, sizeof(format));
    uint64_t virtualOffset = CHUNK_TABLE_OFFSET;
    for (size_t level = 0; level < MIP_COUNT; ++level) {
        const auto& mip = atlas.mips[level];
        const auto blocksPerRow = mip.width / BC7_BLOCK_DIMENSION;
        const auto blockRows = mip.height / BC7_BLOCK_DIMENSION;
        const auto pitch = blocksPerRow * BC7_BYTES_PER_BLOCK;
        const auto size = pitch * blockRows;
        const auto mipHeaderOffset = TEX_HEADER_SIZE + level * MIP_HEADER_SIZE;
        const auto chunkHeaderOffset = CHUNK_TABLE_OFFSET + level * CHUNK_HEADER_SIZE;
        const auto physicalOffset = bytes.size();
        const auto chunkOffset = static_cast<uint32_t>(physicalOffset - PAYLOAD_OFFSET);
        bytes.resize(physicalOffset + size);
        // Preserve the native raw-chunk container, but rebuild every size and
        // offset. BC7 rows describe 4x4 blocks, not individual RGBA pixel rows.
        std::memcpy(bytes.data() + mipHeaderOffset, &virtualOffset, sizeof(virtualOffset));
        std::memcpy(bytes.data() + mipHeaderOffset + MIP_PITCH_OFFSET, &pitch, sizeof(pitch));
        std::memcpy(bytes.data() + mipHeaderOffset + MIP_SIZE_OFFSET, &size, sizeof(size));
        std::memcpy(bytes.data() + chunkHeaderOffset, &size, sizeof(size));
        std::memcpy(bytes.data() + chunkHeaderOffset + CHUNK_DATA_OFFSET_FIELD, &chunkOffset, sizeof(chunkOffset));
        std::vector<uint32_t> pixels(blocksPerRow * BC7_BLOCK_DIMENSION * BC7_BLOCK_DIMENSION);
        std::vector<uint64_t> blocks(pitch / sizeof(uint64_t));
        for (uint32_t row = 0; row < blockRows; ++row) {
            for (uint32_t column = 0; column < blocksPerRow; ++column)
                for (uint32_t y = 0; y < BC7_BLOCK_DIMENSION; ++y)
                    std::memcpy(pixels.data() + (column * BC7_BLOCK_DIMENSION + y) * BC7_BLOCK_DIMENSION,
                                atlas.bytes.data() + mip.offset + (row * BC7_BLOCK_DIMENSION + y) * mip.pitch + column * BC7_BLOCK_DIMENSION * RGBA_BYTES_PER_PIXEL,
                                BC7_BLOCK_DIMENSION * RGBA_BYTES_PER_PIXEL);
            // Batch a whole block row for SIMD. uint64_t storage supplies the
            // output alignment required by bc7e, independently of string alignment.
            ispc::bc7e_compress_blocks(blocksPerRow, blocks.data(), pixels.data(), &parameters);
            std::memcpy(bytes.data() + physicalOffset + row * pitch, blocks.data(), pitch);
        }
        virtualOffset += size;
    }
    const auto milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    _MESSAGE("[IconAtlas] bc7e basic/linear SSE2/SSE4/AVX2: mips=%zu input=%zu output=%zu encodeMs=%.2f", MIP_COUNT, atlas.bytes.size(), bytes.size(), milliseconds);
    return bytes;
}
#endif

void BuildSharedIconAtlas(const std::filesystem::path& gameRoot, std::vector<Enemy>& configs) {
    using Json = nlohmann::json;
    const auto start = std::chrono::steady_clock::now();
    const auto basePath = gameRoot / "reframework/data/EnemyRegistry/atlas/base.tex.241106027";
    const auto outputPath = gameRoot / "natives/stm/gui/ui_texture/tex000000/tex000201_1_imlm4.tex.241106027";
    const auto bindingPath = gameRoot / "reframework/data/UserDataExtension/zz_EnemyRegistry/icons.userdata.json";
    const auto cachePath = gameRoot / "reframework/data/EnemyRegistry/atlas/cache.json";
    const auto iconCount =
        std::ranges::count_if(configs, [](const Enemy& enemy) {
            return !enemy.icon.texture.empty();
        });
    // These are managed outputs, not author inputs. Remove the loose override
    // even if the base or cache was uninstalled first; never encode an empty atlas.
    if (!iconCount) {
        std::filesystem::remove(outputPath);
        std::filesystem::remove(bindingPath);
        std::filesystem::remove(cachePath);
        _MESSAGE("[IconAtlas] no custom icons: generated atlas, binding and cache removed");
        return;
    }
    if (iconCount > ICON_SLOT_COUNT)
        throw std::runtime_error("shared enemy atlas slot capacity exceeded");
    Json inputs{{"encoder", ATLAS_ENCODER_KEY}, {"format", ATLAS_OUTPUT_FORMAT}, {"base", ComputeFileCrc32(basePath)}, {"icons", Json::array()}};
    Json fields = Json::object();
    size_t index = 0;
    for (auto& enemy : configs) {
        if (enemy.icon.texture.empty())
            continue;
        const auto tilePath = ResolveAtlasPath(gameRoot, "natives/stm/" + enemy.icon.texture + ".241106027");
        if (std::filesystem::weakly_canonical(tilePath) == std::filesystem::weakly_canonical(outputPath))
            throw std::runtime_error("icon input aliases generated atlas");
        const auto slot = FIRST_ICON_SLOT + static_cast<int32_t>(index);
        // IDs are startup-private; authors supply neither number. Rows are already sorted by fixedId.
        enemy.iconFixedId = FIRST_CUSTOM_ICON_FIXED_ID + static_cast<int32_t>(index++);
        enemy.iconAtlasIndex = slot;
        // Only logical identities and actual file bytes enter the key. Neither
        // MO2's physical paths nor message/AI changes affect atlas contents.
        inputs["icons"].push_back({{"fixedId", enemy.fixedId}, {"iconFixedId", enemy.iconFixedId}, {"slot", slot}, {"texture", enemy.icon.texture}, {"hash", ComputeFileCrc32(tilePath)}});
        fields["_Values[_enemyId=" + std::to_string(enemy.fixedId) + "]._BossIconType"] = {
            {"op", "set"}, {"value", enemy.iconFixedId}};
        _MESSAGE("[IconAtlas] fixed=%d iconFixed=%d pattern=%d tile=%s", enemy.fixedId, enemy.iconFixedId, slot,
                 enemy.icon.texture.c_str());
    }
    // File configs execute after JSON API configs. This owned late file
    // patches existing authored EnemyData rows, never appends partial duplicate rows.
    const Json binding{
        {"format", "UserDataExtension/v1"},
        {"extensions", Json::array({{{"target", "GameDesign/Common/Enemy/EnemyData.user"},
                                     {"patches", Json::array({{{"app.user_data.EnemyData", std::move(fields)}}})}}})}};
    std::istringstream inputDescription(inputs.dump());
    const auto inputHash = ComputeCrc32(inputDescription);
    const auto cache = ReadAtlasCache(cachePath);
    const bool cacheHit = cache && cache->inputHash == inputHash && std::filesystem::is_regular_file(outputPath) && cache->atlasHash == ComputeFileCrc32(outputPath);
    if (!cacheHit) {
        _MESSAGE("[IconAtlas] cache miss: composing RGBA; outputFormat=%u", ATLAS_OUTPUT_FORMAT);
        auto atlas = ReadRgbaTexture(basePath, ATLAS_DIMENSION);
        ClearReservedSlots(atlas);
        for (const auto& enemy : configs) {
            if (enemy.icon.texture.empty())
                continue;
            const auto tile = ReadRgbaTexture(gameRoot / "natives/stm" / (enemy.icon.texture + ".241106027"), ICON_DIMENSION);
            const auto slot = enemy.iconAtlasIndex;
            CopyIconMipRows(atlas, tile, slot % ICON_ATLAS_COLUMNS * ICON_DIMENSION, MONSTER_ICON_ORIGIN_Y + slot / ICON_ATLAS_COLUMNS * ICON_DIMENSION);
        }
#if ENEMY_ICON_ATLAS_USE_BC7
        WriteAtlasFile(outputPath, EncodeBc7Texture(atlas));
#else
        WriteAtlasFile(outputPath, atlas.bytes);
#endif
    }
    // IDs and the binding are regenerated even on a hit. A deleted/changed UDE
    // output is repaired without paying for RGBA composition or output encoding.
    WriteAtlasFile(bindingPath, binding.dump(2));
    if (!cacheHit)
        WriteAtlasFile(cachePath, Json(AtlasCache{inputHash, ComputeFileCrc32(outputPath)}).dump(2));
    const auto milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    _MESSAGE("[IconAtlas] format=%u cache=%s icons=%zu elapsedMs=%.2f%s", ATLAS_OUTPUT_FORMAT, cacheHit ? "hit" : "rebuilt", index, milliseconds,
             cacheHit ? "; RGBA composition and output encoding skipped" : "; shared atlas published before resource loading");
}
} // namespace EnemyRegistry
