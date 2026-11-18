# EnemyRegistry 作者指南

[English](README.md) | [简体中文](README.zh-CN.md) | [源码构建](BUILDING.md)

EnemyRegistry 用于在 **《怪物猎人：荒野》（Windows x64／TDB81）**中登记自定义怪物。你可以为怪物指定稳定的身份，并添加多语言文本和图标。怪物类型、素材与玩法数据由你的 Mod 提供。

## 安装

1. 安装 REFramework，以及版本配套的 [REClassInjector](https://github.com/kassent/REClassInjector) 和 [UserDataExtension](https://github.com/kassent/UserDataExtension)。
2. 将 `EnemyRegistry.dll` 与配套的 `EnemyRegistry.toml` 一起放入 `reframework/plugins/`。即使 TOML 只有注释，也要保留。
3. 将怪物声明直接放在 `reframework/data/EnemyRegistry/` 下，每只怪物使用一份 `.enemy.json` 文件。

使用 MO2 时，在各个已启用的 Mod 内沿用相同的相对路径。框架和怪物 Mod 可以分开安装。

## 登记怪物

创建 `reframework/data/EnemyRegistry/910001.enemy.json`：

```json
{
  "format": "EnemyRegistry.Enemy/v4",
  "fixedId": 910001,
  "enumName": "EM1999_00_0"
}
```

示例用于说明声明结构。实际接入时，使用你自己的怪物身份、类型和素材。

| 字段 | 填写内容 |
|---|---|
| `format` | `EnemyRegistry.Enemy/v4` |
| `fixedId` | 唯一的有符号 32 位整数，`0` 和 `-1` 为保留值。发布后保持稳定，用于存档和跨 Mod 引用。 |
| `enumName` | 唯一的枚举名，格式为 `EMdddd_dd_d`，每个 `d` 代表一位数字。 |
| `messages` | 可选，按 GUID 关联的多语言文本。见[添加多语言文本](#添加多语言文本)。 |
| `icon.texture` | 可选，填写以 `gui/` 开头、以 `.tex` 结尾的资源路径。见[添加图标](#添加图标)。 |

### 配套类型与数据

通过 REClassInjector 提供 Extend 和 ContextUnique 类型。以 `EM1999_00_0` 为例，类型名使用 `Em1999_00`：

| 所需类型 | 父类型 |
|---|---|
| `app.cEm1999_00Extend` | `app.cEnemyBossZakoExtend` |
| `app.cEm1999_00ContextUnique` | `app.cEnemyContextUnique` |

Holder、PFB 和奖励按游戏约定使用完整枚举名。将相应素材与怪物私有 USER 文件随 Mod 一同发布。

EnemyRegistry 会为怪物补齐公共 `EmID` 表中的身份记录。怪物所需的其他公共条目，如目录或音乐条目，由作者通过 UserDataExtension 添加。

EnemyRegistry 在启动时自动分配运行时索引，最多登记 **106** 只自定义怪物。安装的 Mod 变化时，运行时索引可能变化；持久身份以 `fixedId` 为准。

## 添加多语言文本

在声明中添加 `messages` 数组，把同一个 `guid` 填入需要展示该文本的资源字段。`key` 是便于作者识别的可选标签。

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

每条消息使用唯一的 GUID。按下表填写语言代码，注意大小写，并提供非空的英文文本（`en`）。译文缺失或为空时显示英文。

| 语言 | 代码 | 语言 | 代码 | 语言 | 代码 |
|---|---|---|---|---|---|
| 日语 | `ja` | 英语 | `en` | 法语 | `fr` |
| 意大利语 | `it` | 德语 | `de` | 西班牙语（西班牙） | `es` |
| 俄语 | `ru` | 波兰语 | `pl` | 葡萄牙语（巴西） | `pt-BR` |
| 韩语 | `ko` | 繁体中文 | `zh-Hant` | 简体中文 | `zh-Hans` |
| 阿拉伯语 | `ar` | 泰语 | `th` | 西班牙语（拉丁美洲） | `es-419` |

[查看示例声明](examples/910001.enemy.json)。

## 添加图标

在同一份 `.enemy.json` 中加入 `icon` 对象：

```json
{
  "icon": {
    "texture": "gui/custom/examplewyvern/icon.tex"
  }
}
```

1. 将 `natives/stm/gui/custom/examplewyvern/icon.tex.241106027` 作为散文件随 Mod 提供。小图使用 **100×100、RGBA8_UNORM、非流式的 Wilds TEX**，内含预生成的 **100×100、50×50、25×25** 三级 mip。
2. 确保框架所需的 **2048×2048 RGBA** 基础图集可从 `reframework/data/EnemyRegistry/atlas/base.tex.241106027` 作为散文件读取。从源码构建时需另行取得或准备。
3. 通过 UserDataExtension 添加你的 `EnemyData` 行，EnemyRegistry 会将图标绑定到该行。

EnemyRegistry 自动生成共享图集并分配图标编号，最多支持 **63** 只怪物使用自定义图标。作者发布自己的小图和声明，生成后的图集由框架管理。

## 进游戏验证

安装或修改声明后，重启游戏。进入会用到该怪物的场景，确认怪物能够出现并正常行动。如果添加了文本或图标，请在对应的游戏界面确认显示效果。

| 现象 | 检查项 |
|---|---|
| 怪物未登记 | 在 `logs/HIDWrapper.log` 和 `logs/EnemyRegistry.log` 中找第一条报错。核对 DLL／TOML 配对、声明路径和格式，以及 `fixedId`、`enumName` 是否唯一。 |
| 已登记但怪物未出现 | 核对 CI 类型名、父类型和怪物资源，并在 UDE 日志中确认 `EnemyRegistry/EmID` 与自定义公共条目已应用。 |
| 未显示名称或描述 | 核对资源字段引用的消息 GUID，以及非空的英文回退文本。 |
| 未显示图标 | 核对小图和基础图集的散文件路径、小图格式，以及作者提供的 `EnemyData` 行。 |

使用 MO2 时，当次日志也可能出现在 `overwrite/logs` 中。
