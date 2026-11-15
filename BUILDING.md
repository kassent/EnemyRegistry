# Building EnemyRegistry / 构建 EnemyRegistry

## Requirements / 环境

- Windows x64; Visual Studio 2022 C++ tools and Windows SDK; CMake 3.21+.
- vcpkg: spdlog, fmt, nlohmann-json, zlib; static MSVC runtime.
- ISPC on PATH, or set `BC7E_ISPC_EXECUTABLE` explicitly. ISPC 1.32.0 is the verified compiler; players do not need it.
- Python 3 is optional for the point-capacity instruction test. The separate icon authoring script additionally needs Pillow and a compatible external TEX converter; that converter and game-derived base textures are not part of this repository.

## Build / 构建

```powershell
git clone --recurse-submodules https://github.com/kassent/EnemyRegistry.git
cd EnemyRegistry
cmake -S . -B build -A x64 `
  "-DCMAKE_TOOLCHAIN_FILE=$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" `
  -DVCPKG_TARGET_TRIPLET=x64-windows-static `
  "-DBC7E_ISPC_EXECUTABLE=$env:ISPC_EXE"
cmake --build build --config Release --target EnemyRegistry
cmake --build build --config Release --target check
```

Set `VCPKG_ROOT` and `ISPC_EXE` to your installed tools. If ISPC is on PATH, omit the final configure argument. For an existing clone, run `git submodule update --init --recursive`.

`common/` pins [WildsCommonLib](https://github.com/kassent/WildsCommonLib) to a tested commit. Only the matching CI and UDE public headers are copied into `plugins/ClassInjector/include` and `plugins/UserDataExtension`; their implementations are not linked or built here. UDE is loaded explicitly at runtime, not through PE imports.

`common/` 是固定提交的 submodule，不是公共库源码的另一个副本。CI/UDE 目录只保留所需 SDK 头；不构建第二份宿主、不静态导入 UDE。

## Output and tests / 输出与测试

`build/bin/Release/` contains `EnemyRegistry.dll`, `EnemyRegistry.toml`, and `EnemyRegistry.bc7e.LICENSE.txt`. Install the first two in `reframework/plugins/` and the license in `licenses/`. Default builds never deploy into a game installation.

The bc7e source, its license and upstream identity are retained under `plugins/EnemyRegistry/third_party/bc7e/`. SSE2/SSE4/AVX2 objects and their dispatcher are linked into the plugin, not deployed as an extra DLL. `ENEMY_ICON_ATLAS_USE_BC7` defaults to ON; OFF chooses RGBA8 output, not a different author input format. The current CMake recipe still requires ISPC when configuring either mode.

`check` builds the production plugin and test tools, then runs registration/native-instruction contracts, common contracts, and the Python point-capacity test when available. `EnemyIconAtlasTests` is a separate file-pipeline harness requiring an authored fixture root; building it alone does not validate game assets or native rendering.

`check` 不启动游戏；测试通过不等于新怪资源、AI、联机、图标全界面或任务结算已验收。源码仓库不携带真实游戏 TDB/EXE、纹理或私有开发历史。

Original production C++ is copied unchanged; public CMake only adapts the build entry and explicit deployment destination. `SOURCE_SYNC.json` records upstream source identities using CRLF→LF normalized SHA256. Existing third-party notices are preserved; no new project license is selected by this extraction.
