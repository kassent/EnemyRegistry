# EnemyRegistry Author Guide

[English](README.md) | [简体中文](README.zh-CN.md) | [Build from source](BUILDING.md)

EnemyRegistry registers custom monsters for **Monster Hunter Wilds (Windows x64, TDB81)**. Give your monster a stable identity and optionally add translated text and an icon. Your mod supplies the monster's types, assets, and gameplay data.

## Install

1. Install REFramework and the matching versions of [REClassInjector](https://github.com/kassent/REClassInjector) and [UserDataExtension](https://github.com/kassent/UserDataExtension).
2. Place `EnemyRegistry.dll` and its matching `EnemyRegistry.toml` together in `reframework/plugins/`. Keep the TOML even if it contains only a comment.
3. Place monster declarations directly in `reframework/data/EnemyRegistry/`. Use one `.enemy.json` file per monster.

In MO2, use the same paths relative to each enabled mod's root. The frameworks and the monster mod can be separate packages.

## Register a monster

Create `reframework/data/EnemyRegistry/910001.enemy.json`:

```json
{
  "format": "EnemyRegistry.Enemy/v4",
  "fixedId": 910001,
  "enumName": "EM1999_00_0"
}
```

This example shows the declaration structure. Use the identity, types, and assets from your own mod.

| Field | What to enter |
|---|---|
| `format` | `EnemyRegistry.Enemy/v4` |
| `fixedId` | A unique signed 32-bit integer other than `0` or `-1`. Keep it stable across releases and use it for saved or cross-mod references. |
| `enumName` | A unique name matching `EMdddd_dd_d`, where each `d` is a digit. |
| `messages` | Optional translations keyed by GUID. See [Add localized text](#add-localized-text). |
| `icon.texture` | Optional texture URI starting with `gui/` and ending in `.tex`. See [Add an icon](#add-an-icon). |

### Match the monster types and data

Provide the Extend and ContextUnique types through REClassInjector. For `EM1999_00_0`, the type stem is `Em1999_00`:

| Required type | Base type |
|---|---|
| `app.cEm1999_00Extend` | `app.cEnemyBossZakoExtend` |
| `app.cEm1999_00ContextUnique` | `app.cEnemyContextUnique` |

Use the full enum name for the game's Holder, PFB, and reward naming conventions. Ship the corresponding assets and monster-specific USER files with your mod.

EnemyRegistry adds the monster to the shared `EmID` table. Add any other shared entries your monster needs, such as catalog or music rows, through UserDataExtension.

EnemyRegistry assigns up to **106** custom enemy indices at startup. These indices can change when the installed mod set changes; `fixedId` remains the monster's persistent identity.

## Add localized text

Add a `messages` array to your declaration. Reference each message's `guid` from the resource field that displays it. `key` is an optional label for authors.

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

Generate a unique GUID for each message. Use the exact language tags below and provide a non-empty English (`en`) string. Missing or empty translations fall back to English.

| Language | Tag | Language | Tag | Language | Tag |
|---|---|---|---|---|---|
| Japanese | `ja` | English | `en` | French | `fr` |
| Italian | `it` | German | `de` | Spanish (Spain) | `es` |
| Russian | `ru` | Polish | `pl` | Portuguese (Brazil) | `pt-BR` |
| Korean | `ko` | Traditional Chinese | `zh-Hant` | Simplified Chinese | `zh-Hans` |
| Arabic | `ar` | Thai | `th` | Spanish (Latin America) | `es-419` |

[View the example declaration](examples/910001.enemy.json).

## Add an icon

Add the `icon` object to the same `.enemy.json` file:

```json
{
  "icon": {
    "texture": "gui/custom/examplewyvern/icon.tex"
  }
}
```

1. Ship `natives/stm/gui/custom/examplewyvern/icon.tex.241106027` as a loose file. The tile must be a **100×100, RGBA8_UNORM, non-streaming Wilds TEX** with prebuilt **100×100, 50×50, and 25×25** mip levels.
2. Make the framework's **2048×2048 RGBA** base atlas available as a loose file at `reframework/data/EnemyRegistry/atlas/base.tex.241106027`. Obtain or prepare it separately when building from source.
3. Add your monster's `EnemyData` row through UserDataExtension so EnemyRegistry can attach the icon to it.

EnemyRegistry builds the shared atlas and assigns icon IDs automatically. Up to **63** installed monsters can have custom icons. Distribute your own tile and declaration; the framework manages the generated atlas.

## Verify in game

Restart the game after installing or editing declarations. Load a scene that uses the monster and verify that it spawns and behaves as expected. If you added text or an icon, check the relevant UI in the game.

| If you see… | Check… |
|---|---|
| The monster is not registered | `logs/HIDWrapper.log` and `logs/EnemyRegistry.log` for the first error. Check the DLL/TOML pair, the declaration path and format, and whether `fixedId` and `enumName` are unique. |
| Registration succeeds but the monster does not spawn | The CI type names and base types, the monster assets, and the UDE logs for `EnemyRegistry/EmID` and your own shared-data entries. |
| The name or description is missing | The resource field references the message GUID and the English fallback is present. |
| The icon is missing | The loose tile and base atlas paths, the tile format, and the authored `EnemyData` row. |

MO2 may place the current run's logs in `overwrite/logs`.
