# EnemyRegistry Developer Guide

[English](README.md) | [简体中文](README.zh-CN.md) | [Build from source](BUILDING.md)

**Give custom monsters a shared registration path instead of building a separate ID and capacity patch set for every mod.**

EnemyRegistry is a native enemy-registration plugin for **Monster Hunter Wilds, Windows x64 / TDB81**. It coordinates stable author identities, startup-local native indices, early native capacity changes, enemy naming contracts, localized messages, and a shared monster-icon atlas.

It is infrastructure for an already authored custom monster, **not a monster converter, AI framework, resource generator, or spawn command**. A JSON declaration alone does not create a usable monster.

## Contents

1. [Responsibilities and dependencies](#1-responsibilities-and-dependencies)
2. [Installation and file layout](#2-installation-and-file-layout)
3. [Authoring workflow](#3-authoring-workflow)
4. [Declaration reference](#4-declaration-reference)
5. [Native naming and stable identity](#5-native-naming-and-stable-identity)
6. [Localized messages](#6-localized-messages)
7. [Custom icons and the shared atlas](#7-custom-icons-and-the-shared-atlas)
8. [UserDataExtension and native initialization](#8-userdataextension-and-native-initialization)
9. [Validation and troubleshooting](#9-validation-and-troubleshooting)
10. [Source layout and limits](#10-source-layout-and-limits)

## 1. Responsibilities and dependencies

| Component | Responsibility |
|---|---|
| [REClassInjector](https://github.com/kassent/REClassInjector) | Loads providers early, extends TDB types/enums, and notifies them before native static constructors |
| **EnemyRegistry** | Assigns native enemy indices, integrates identity conversions and native capacity consumers, and installs message/icon/quest-detail integration |
| [UserDataExtension](https://github.com/kassent/UserDataExtension) | Applies ordered JSON changes to native USER data before its `onLoad()` initialization |
| Your monster mod | Provides managed types/native implementations, models, animations, AI, parameters, private USER/PFB resources, and the public-data declarations needed by the monster |
| [WildsCommonLib](https://github.com/kassent/WildsCommonLib) | Build-time shared code; not an extra DLL to install |

Use matching framework releases. EnemyRegistry is a **zero-type CI provider**: its TOML marker activates registration, but the plugin does not declare your monster's Extend or ContextUnique types for you.

UDE remains a required runtime dependency for non-empty enemy registration. EnemyRegistry resolves `UserDataExtension.dll` through the game's full virtual path and calls its JSON-registration export explicitly; UDE is not a PE import dependency. The framework plugins can therefore be installed as separate MO2 mods. This does not remove the requirement to install/enable UDE.

## 2. Installation and file layout

Install REFramework and the matching CI/UDE packages first. A typical merged game view is:

```text
MonsterHunterWilds/
  hid.dll                                      ClassInjector integrated host
  reframework/
    plugins/
      EnemyRegistry.dll
      EnemyRegistry.toml                       keep the matching marker file
      UserDataExtension.dll
      UserDataExtension.toml
      YourMonster.dll                          if your custom types need native code
      YourMonster.toml
    data/
      EnemyRegistry/
        910001.enemy.json
        atlas/base.tex.241106027                framework input; needed when icons exist
      UserDataExtension/
        YourMonster/your-monster.userdata.json  your public-data changes
  natives/stm/                                 your own authored game resources
  licenses/EnemyRegistry.bc7e.LICENSE.txt
```

`EnemyRegistry.toml` can contain only the generated comment. It must remain beside the DLL with the same basename so CI recognizes the provider. Do not replace it with an enemy JSON or place declarations in `reframework/plugins`.

Only **direct child files ending in `.enemy.json`** under `reframework/data/EnemyRegistry/` are scanned. Nested folders and other filenames are not registered. MO2 paths inside an enabled mod use the same game-relative layout; the framework release and each monster mod may be separate packages.

The source repository contains no game-derived base atlas, model, USER, EXE, or TDB snapshot. A source-built DLL is not a complete icon-enabled runtime package; obtain or prepare the matching framework inputs separately.

## 3. Authoring workflow

This workflow assumes your monster types and assets already exist.

1. Choose a unique stable `fixedId` and a matching native-style `enumName`.
2. Implement the required Extend/ContextUnique types through CI and prepare the resources consumed by the native monster factory. Keep their names consistent with section 5.
3. Add one declaration such as `reframework/data/EnemyRegistry/910001.enemy.json`:

```json
{
  "format": "EnemyRegistry.Enemy/v4",
  "fixedId": 910001,
  "enumName": "EM1999_00_0"
}
```

4. Add optional messages and an optional icon only after their inputs are ready. The complete message example is [examples/910001.enemy.json](examples/910001.enemy.json).
5. Put shared/public USER changes in your own UDE JSON; supply monster-private USER/PFB resources as separate native assets. EnemyRegistry does not infer these tables from the identity declaration.
6. Cold-start the game and verify early registration, type lookup, USER loading, and only then the monster's real creation/lifecycle.

`910001` and `EM1999_00_0` are **illustrative author identities**, not reserved IDs or a bundled sample monster. Installing this example alone is not a successful factory integration. Copy only one declaration for an identity; installing both a minimal and a message-enabled copy creates a duplicate.

Enemy declarations are collected and frozen at startup. Editing JSON while the game runs does not reassign IDs, rebuild engine arrays, or reload messages; restart for these changes.

## 4. Declaration reference

| Field | Meaning and current contract |
|---|---|
| `format` | Required, exactly `EnemyRegistry.Enemy/v4` |
| `fixedId` | Unique signed Int32 author identity; `0` and `-1` are invalid. Other negative values can represent signed hashes |
| `enumName` | Unique name matching `EMdddd_dd_d`, with digits at every `d` position |
| `icon` | Optional object; when present it contains `texture`. Omit it or use an empty texture to opt out |
| `icon.texture` | Game-relative resource URI beginning with `gui/` and ending in `.tex`; see section 7 |
| `messages` | Optional array; defaults to no custom messages |
| `messages[].key` | Optional human-readable label; runtime lookup uses the GUID, not this label |
| `messages[].guid` | Non-empty valid GUID, unique across installed custom messages |
| `messages[].texts` | Language-tag dictionary; non-empty `en` is required |

Do not author `rawId`, `iconFixedId`, or `iconAtlasIndex`: they are generated runtime state and are not read into those fields by the JSON codec. Do not rely on extra JSON fields as configuration. The obsolete `extendType`, `contextType`, `resourcePrefix`, `holderName`, and `rewardName` fields are explicitly rejected. v3 identities and old inline `sources`/merge declarations are not the current format.

A malformed or conflicting declaration fails the provider's registration; it is not silently accepted as a partially working monster set. Check the first configuration error rather than testing later consumers against an incomplete setup.

## 5. Native naming and stable identity

For `enumName = EM1999_00_0`:

| Consumer | Derived name |
|---|---|
| Enemy enum identity | `EM1999_00_0` |
| Extend type | `app.cEm1999_00Extend` |
| ContextUnique type | `app.cEm1999_00ContextUnique` |
| Holder/PFB/reward naming | The native consumers use the full enum name; your assets must satisfy their corresponding resource contracts |

The first nine characters produce the type stem: `EM1999_00` → `Em1999_00`. The Extend type must derive from `app.cEnemyBossZakoExtend`; ContextUnique must derive from `app.cEnemyContextUnique`. EnemyRegistry checks these relationships before publishing the native capacity/identity integration. It does not fabricate missing types or resource files.

**fixed ID is not raw ID.** Declarations are sorted by signed `fixedId` and assigned raw indices starting at 150. The current maximum raw ID is 255, so at most **106** custom enemy declarations fit. Adding another mod can shift the raw index of an existing monster; save persistent references and coordinate between mods with fixed IDs, not cached startup indices. Do not hardcode that a particular monster is always raw150.

These are implementation limits, not engine-reserved official mod slots or a compatibility guarantee across game versions. EnemyRegistry coordinates the necessary native consumers; copying an ID into one array or only renaming an enum is not equivalent to full registration.

## 6. Localized messages

A message-enabled declaration:

```json
{
  "format": "EnemyRegistry.Enemy/v4",
  "fixedId": 910001,
  "enumName": "EM1999_00_0",
  "messages": [
    {
      "key": "ExampleWyvern.Name",
      "guid": "c78f7ed3-13ed-4b38-89d0-5ef41e701c3c",
      "texts": {
        "en": "Example Wyvern",
        "ja": "サンプル竜",
        "zh-Hans": "示例龙",
        "zh-Hant": "範例龍"
      }
    }
  ]
}
```

Use your own unique GUIDs. Resource fields that should display this message must reference the same GUID; inventing a readable `key` does not automatically replace an enemy name or all its text fields. Do not deploy a whole shared MSG just to add these strings.

Supported, case-sensitive tags:

| Language | Tag | Language | Tag | Language | Tag |
|---|---|---|---|---|---|
| Japanese | `ja` | English | `en` | French | `fr` |
| Italian | `it` | German | `de` | Spanish (Spain) | `es` |
| Russian | `ru` | Polish | `pl` | Portuguese (Brazil) | `pt-BR` |
| Korean | `ko` | Traditional Chinese | `zh-Hant` | Simplified Chinese | `zh-Hans` |
| Arabic | `ar` | Thai | `th` | Spanish (Latin America) | `es-419` |

Missing/empty translations fall back to that message's English string. Unknown language tags are configuration errors; `zh-CN` and `pt_BR` are not aliases. Values must be strings, not `null`. The lookup uses the engine's `via.Language` values, not `app.LanguageDef.LANGUAGE_APP`; authors supply tags rather than numeric language IDs.

Explicit-language native lookups use the requested language. Default lookups read the current GUI message language, so changing the game's language does not require a separate translation registry. Fonts and layout remain native responsibilities; message pages beyond relative page 0 are not implemented by this single-page custom-message path.

## 7. Custom icons and the shared atlas

Add this optional `icon` field to the declaration:

```json
{
  "icon": {
    "texture": "gui/custom/examplewyvern/icon.tex"
  }
}
```

It resolves to `natives/stm/gui/custom/examplewyvern/icon.tex.241106027`. Do not include `natives/stm/` or the version suffix in the URI.

Author input is **100×100, RGBA8_UNORM, non-streaming Wilds TEX version 241106027**, with three prebuilt mip levels **100/50/25** and the expected raw-chunk/mip layout. A PNG renamed to `.tex`, a BC7 input tile, or only the first mip does not satisfy this contract. The input texture and the framework's 2048×2048 RGBA base must be available as loose file inputs; being present only inside a game PAK is insufficient for the startup file reader.

The shared base belongs at `reframework/data/EnemyRegistry/atlas/base.tex.241106027`. Ordinary monster authors provide only their own tile and declaration, not a private copy of the generated shared atlas. The included `prepare_shared_icon_atlas.py` is an optional staging tool: it needs Pillow and an external compatible TEX converter passed with `--converter`; no ready-to-run converter or game texture is bundled here.

EnemyRegistry assigns icon IDs and **slots 37–99**, supporting at most **63 icon-bearing declarations**, independently of the 106-enemy identity limit. Authors do not allocate atlas slots or icon IDs. Startup composes mip rows and, by default, encodes BC7 with the statically linked SIMD encoder. Players do not need ISPC or an encoder DLL.

Framework-managed outputs:

```text
natives/stm/gui/ui_texture/tex000000/tex000201_1_imlm4.tex.241106027
reframework/data/UserDataExtension/zz_EnemyRegistry/icons.userdata.json
reframework/data/EnemyRegistry/atlas/cache.json
```

The generated UDE binding patches existing authored EnemyData rows; it does not create a complete row for your monster. Ensure your own public-data declaration adds that row before the late binding applies.

Input content, output format, and encoder identity determine cache validity. A hit skips composition/encoding; no custom icons causes the framework to remove its generated atlas, binding, and cache. Do not hand-edit or ship a stale generated cache/atlas as another mod's authoritative output. The reserved shared output path is not a private texture location for individual monster mods.

## 8. UserDataExtension and native initialization

```text
CI loads EnemyRegistry and calls its registration export
  → collect declarations; assign raw indices; prepare icon files
  → register canonical enemy-enum changes with CI
  → explicitly load UDE and register the generated EmID JSON
CI publishes runtime types and native method entries
  → EnemyRegistry ci_on_tdb_ready installs native capacity/identity integration
  → UDE ci_on_tdb_ready closes registration and installs the USER decode hook
Native USER decoding
  → UDE applies ordered changes before that USER's onLoad()
REFramework plugin initialization
  → install message, icon-discovery and quest-detail consumers
```

EnemyRegistry generates **only the EmID identity extension**: raise the `MAX` sentinel's lower bound, append missing raw-keyed rows, then update their fixed IDs/names. The sentinel is not an ordinary monster, and an already higher `MAX` is not reduced to the current enemy count.

Other public parameters, catalogs, music entries, camera data and placement permissions remain author-controlled UDE declarations/native resources. Reuse compatible original presets where possible; add precise UDE changes when necessary instead of replacing an entire public USER. Monster-private parameters should be separate USER resources, not permanent blanket overrides of a shared table.

There is no promise that every new game version or arbitrary native consumer has already been audited. Missing initialization or resource contracts must be resolved at their actual consumer; do not treat a successful CI callback as proof of animation, combat, rewards, save, or multiplayer correctness.

## 9. Validation and troubleshooting

For a cold-start validation, verify this order:

1. The matching DLL and TOML are actually loaded from the intended physical/MO2 source.
2. `logs/HIDWrapper.log` accepts and notifies the EnemyRegistry provider.
3. `logs/EnemyRegistry.log` reports frozen identities, capacity installation and pre-cctor registration readiness.
4. UDE reports `EnemyRegistry/EmID` registration/application and your own public-data changes.
5. Both managed types resolve, actual monster creation succeeds, and its lifecycle continues normally.
6. Validate the affected UI, localization, combat, teardown and any multiplayer scenario separately.

With MO2, current plugin logs may be redirected into `overwrite/logs`; correlate timestamps rather than reading an older physical-root log. Mod list priority is not DLL dependency order. A DLL listed by REFramework later does not establish that CI registered it in the earlier initialization window.

| Symptom | First checks |
|---|---|
| Declaration not found | Exact data directory, direct-child placement, `.enemy.json` suffix and v4 format |
| Identity conflict | Duplicate fixed IDs/enum names, duplicated examples, reserved `0`/`-1` |
| Derived factory contract failed | Type spelling, CI declaration/implementation, required base classes |
| `failed opening atlas hash input` | Named loose base/tile files and the actual merged paths, not only PAK contents |
| Music/array crash during startup | Earlier registration/EmID/capacity failure; adding music data without the corresponding native integration is not valid |
| Wrong icon/missing EnemyData row | Input TEX contract, row creation order, shared output/cache and other mods overriding the same texture |
| Message missing or installation failed | Matching/valid GUID, GUID uniqueness, non-empty English and correct case-sensitive language tags |
| Works from physical root but not MO2 | Actual module SHA/path, marker files, enabled dependencies and early registration logs |

Do not hot-reload this plugin into an already initialized game as a substitute for a cold start. Removing a monster declaration mid-play also does not remove live objects or repair existing engine caches.

## 10. Source layout and limits

See [BUILDING.md](BUILDING.md) for an independent build and the included offline tests.

| Source | Responsibility |
|---|---|
| `Plugin.cpp` | CI/RF startup integration and explicit UDE binding |
| `EnemyIdRegistry.*` | Declaration parsing, allocation and immutable identity queries |
| `EnemyRegistrationHooks.*` | Native identity consumers, named factory type checks and patch installation |
| `EnemyArrayCapacityPatches.cpp` | Collect and verify bounded native capacity/loop/immediate contracts |
| `EnemyEnumConfig.*` | Generate ordered EmID UDE JSON |
| `EnemyIconAtlas.*` | Validate inputs, compose/encode/cache the shared atlas and write UDE bindings |
| `EnemyMessageOverrides.*` | GUID message lookup, translation selection and stable text buffers |
| `EnemyIconDiscovery.*`, `EnemyQuestVisibility.*` | Targeted icon/quest-detail integration, not global unlocks |

These internal C++ headers are not a separately versioned cross-DLL C API. Mods should not resolve `FindEnemyByFixedId` with `GetProcAddress` or assume its symbols are exported. Use native identity conversions/CI/REFramework metadata for engine integration, and keep your persisted identity as a fixed ID.

Runtime compatibility is version-specific. Offline tests verify their listed code/contracts; they do not ship or execute game snapshots, replace a complete monster authoring pipeline, or certify every rendered UI/online scenario.
