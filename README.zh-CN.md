# EnemyRegistry 开发者指南

[English](README.md) | [简体中文](README.zh-CN.md) | [源码构建](BUILDING.md)

**为自定义怪物提供统一的登记入口，而不是让每个 Mod 各自维护一套 ID 与数组扩容补丁。**

EnemyRegistry 是面向 **Monster Hunter Wilds、Windows x64／TDB81** 的原生敌人注册插件，负责稳定作者身份、启动期原生索引、早期容量适配、原生命名契约、多语言消息与共享怪物图标。

它是已有自定义怪物的接入基础设施，**不是怪物转换器、AI 框架、资源生成器或刷怪命令**。只写一份 JSON，不会自动得到可以正常运行的怪物。

## 目录

1. [职责与依赖](#1-职责与依赖)
2. [安装与文件布局](#2-安装与文件布局)
3. [作者接入流程](#3-作者接入流程)
4. [声明字段参考](#4-声明字段参考)
5. [原生命名与稳定身份](#5-原生命名与稳定身份)
6. [多语言消息](#6-多语言消息)
7. [自定义图标与共享图集](#7-自定义图标与共享图集)
8. [UserDataExtension 与初始化时序](#8-userdataextension-与初始化时序)
9. [验收与排错](#9-验收与排错)
10. [源码职责与能力边界](#10-源码职责与能力边界)

## 1. 职责与依赖

| 组件 | 负责什么 |
|---|---|
| [REClassInjector](https://github.com/kassent/REClassInjector) | 提前加载 Provider、扩展 TDB 类型／枚举，在原生静态构造前通知插件 |
| **EnemyRegistry** | 分配原生敌人索引、接入身份转换与容量消费者，安装消息、图标及任务详情的局部接入 |
| [UserDataExtension](https://github.com/kassent/UserDataExtension) | 在目标 USER 解码后、`onLoad()` 初始化前按顺序应用 JSON 增量 |
| 你的怪物 Mod | 自定义类型及原生实现、模型、动作、AI、参数、私有 USER／PFB，以及怪物所需的公共表声明 |
| [WildsCommonLib](https://github.com/kassent/WildsCommonLib) | 编译期公共代码，不是需要玩家另外安装的 DLL |

请使用相互匹配的框架版本。EnemyRegistry 是**零类型 CI Provider**：同名 TOML 用于激活登记，但插件不会替作者生成 Extend 或 ContextUnique 类型。

非空敌人登记仍然依赖 UDE。EnemyRegistry 在登记阶段通过游戏虚拟完整路径显式加载 `UserDataExtension.dll`，解析并调用 JSON 登记导出，不再依赖 PE 导入表。因此可以将两个框架安装在不同的 MO2 Mod 中；这不表示可以不安装或不启用 UDE。

## 2. 安装与文件布局

先安装 REFramework 及配套 CI／UDE。合并后的游戏视图通常如下：

```text
MonsterHunterWilds/
  hid.dll                                      集成 ClassInjector 的宿主
  reframework/
    plugins/
      EnemyRegistry.dll
      EnemyRegistry.toml                       保留同名标记文件
      UserDataExtension.dll
      UserDataExtension.toml
      YourMonster.dll                          自定义类型需要原生实现时提供
      YourMonster.toml
    data/
      EnemyRegistry/
        910001.enemy.json
        atlas/base.tex.241106027                存在自定义图标时需要的框架输入
      UserDataExtension/
        YourMonster/your-monster.userdata.json  作者自己的公共表增量
  natives/stm/                                 作者自有游戏资源
  licenses/EnemyRegistry.bc7e.LICENSE.txt
```

`EnemyRegistry.toml` 可以只有构建生成的注释；它必须与 DLL 同目录、同名，CI 才会识别该 Provider。不要把敌人 JSON 当成这个 TOML，也不要把敌人声明放进 `reframework/plugins`。

只扫描 `reframework/data/EnemyRegistry/` **直接子级、以 `.enemy.json` 结尾的文件**；不递归子目录，不登记其他文件名。MO2 Mod 内也使用相同的游戏相对目录，框架与各个怪物可以分包安装。

源码仓库不附带游戏派生的基础图集、模型、USER、EXE 或 TDB 快照。因此仅从源码编译出 DLL，并不等于已经准备好可用的图标运行包；配套框架输入需要另行取得或生成。

## 3. 作者接入流程

以下流程以已有自定义类型与资源为前提：

1. 选择唯一、稳定的 `fixedId`，以及符合原生命名规则的 `enumName`。
2. 用 CI 提供所需 Extend／ContextUnique 类型，准备原生怪物工厂实际消费的资源；命名遵守第 5 节。
3. 新增 `reframework/data/EnemyRegistry/910001.enemy.json`，最小结构如下：

```json
{
  "format": "EnemyRegistry.Enemy/v4",
  "fixedId": 910001,
  "enumName": "EM1999_00_0"
}
```

4. 输入齐备后，再添加可选消息与图标。带消息的完整声明见 [examples/910001.enemy.json](examples/910001.enemy.json)。
5. 公共 USER 增量放到自己的 UDE JSON；怪物私有 USER／PFB 作为独立原生资源提供。EnemyRegistry 不会从身份声明自动推导全部公共表。
6. 冷启动后依次确认早期登记、类型查询、USER 加载，再验证实际创建与连续生命周期。

`910001`、`EM1999_00_0` 是**结构示例，不是保留编号，也不对应本仓库提供的成品怪物**。仅安装示例不代表工厂接入成功。同一身份只交一份声明，不要同时安装最小版与消息版，造成重复身份。

声明在启动时收集并冻结。游戏运行中修改 JSON，不会重新分配 ID、重建原生数组或更新消息；这些变更需要重启。

## 4. 声明字段参考

| 字段 | 含义与当前契约 |
|---|---|
| `format` | 必填，固定为 `EnemyRegistry.Enemy/v4` |
| `fixedId` | 唯一的有符号 Int32 作者身份；禁止 `0`、`-1`，其他负数可用于有符号哈希 ID |
| `enumName` | 唯一名称，必须匹配 `EMdddd_dd_d`，各 `d` 均为数字 |
| `icon` | 可选对象；提供时包含 `texture`。省略或空纹理 URI 表示不配置图标 |
| `icon.texture` | 以 `gui/` 开头、`.tex` 结尾的游戏相对资源 URI，详见第 7 节 |
| `messages` | 可选数组，默认没有自定义消息 |
| `messages[].key` | 可选的作者可读标签；运行时按 GUID 查询，不按这个标签匹配 |
| `messages[].guid` | 非空合法 GUID，安装的自定义消息之间不得冲突 |
| `messages[].texts` | 语言代码字典；必须有非空 `en` |

不要填写 `rawId`、`iconFixedId`、`iconAtlasIndex`：这些是运行时生成状态，JSON 编解码不会把输入写入这些成员。不要把额外 JSON 字段当成有效配置。旧 `extendType`、`contextType`、`resourcePrefix`、`holderName`、`rewardName` 会被显式拒绝；v3 身份格式与旧内联 `sources`／合并片段不是当前声明方式。

格式、身份冲突等错误会使该 Provider 本次登记失败，而不是悄悄只登记其中一部分。应先处理最早的配置错误，不要拿不完整的登记结果继续测试下游功能。

## 5. 原生命名与稳定身份

以 `enumName = EM1999_00_0` 为例：

| 消费者 | 对应名称 |
|---|---|
| 敌人枚举身份 | `EM1999_00_0` |
| Extend 类型 | `app.cEm1999_00Extend` |
| ContextUnique 类型 | `app.cEm1999_00ContextUnique` |
| Holder／PFB／奖励命名 | 原生消费者使用完整枚举名；实际资源仍须满足各自的路径与内容契约 |

前 9 个字符形成类型前缀：`EM1999_00` → `Em1999_00`。Extend 必须派生自 `app.cEnemyBossZakoExtend`，ContextUnique 必须派生自 `app.cEnemyContextUnique`。EnemyRegistry 在发布原生容量／身份接入前核对这两项继承关系，不会伪造缺失类型或资源文件。

**fixed ID 不是 raw ID。** 全部声明按有符号 `fixedId` 排序，从 raw150 开始分配；当前最大 raw 为255，因此最多 **106 个**自定义敌人声明。新增其他 Mod 后，既有怪物的 raw 可能变化；存档与跨 Mod 持久引用应使用 fixed ID，不缓存某次启动分配的索引。不要硬编码“某只怪永远是 raw150”。

这些是当前实现限制，不是引擎官方保留的 Mod 槽位，更不是跨游戏版本保证。只给某个数组写入编号、或只改一个枚举名，都不等于完成原生敌人注册。

## 6. 多语言消息

带消息的声明：

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

请为自己的消息分配唯一 GUID；需要显示该文本的资源字段，也必须引用同一个 GUID。起一个可读 `key` 不会自动替换怪物名称或所有文本字段。无需为新增这些字符串覆盖整份公共 MSG。

支持以下大小写敏感的语言代码：

| 语言 | 代码 | 语言 | 代码 | 语言 | 代码 |
|---|---|---|---|---|---|
| 日语 | `ja` | 英语 | `en` | 法语 | `fr` |
| 意大利语 | `it` | 德语 | `de` | 西班牙语（西班牙） | `es` |
| 俄语 | `ru` | 波兰语 | `pl` | 葡萄牙语（巴西） | `pt-BR` |
| 韩语 | `ko` | 繁体中文 | `zh-Hant` | 简体中文 | `zh-Hans` |
| 阿拉伯语 | `ar` | 泰语 | `th` | 西班牙语（拉丁美洲） | `es-419` |

缺少或为空的译文回退到该消息的英文。未知语言代码会报配置错误，`zh-CN`、`pt_BR` 不是兼容别名；译文值必须是字符串，不使用 `null`。底层对应引擎 `via.Language`，不是 `app.LanguageDef.LANGUAGE_APP`，作者只填写代码，不填数字语言 ID。

显式语言查询使用调用方指定的语言；默认查询读取当前 GUI 消息语言，因此游戏内切换语言不依赖另一个翻译注册表。字体与布局仍由游戏负责；当前自定义消息是单页路径，不实现相对页0之外的多页消息。

## 7. 自定义图标与共享图集

在声明中添加可选 `icon` 字段：

```json
{
  "icon": {
    "texture": "gui/custom/examplewyvern/icon.tex"
  }
}
```

它对应 `natives/stm/gui/custom/examplewyvern/icon.tex.241106027`。URI 内不写 `natives/stm/`，也不写版本后缀。

输入必须为 **100×100、RGBA8_UNORM、非流式、版本241106027的 Wilds TEX**，带 **100／50／25 三级预生成 mip**，并符合读取器要求的 raw-chunk／mip 布局。PNG 改后缀、BC7 输入小图、只有首级 mip，都不符合该契约。输入小图与框架2048×2048 RGBA基础图必须可作为 loose 文件读取；只存在于游戏 PAK 中，不能满足启动期文件读取器。

基础图固定放在 `reframework/data/EnemyRegistry/atlas/base.tex.241106027`。普通怪物作者交自己的小图与声明，不各自复制一份生成后的共享大图。仓库附带的 `prepare_shared_icon_atlas.py` 是可选 staging 工具，需要 Pillow 和通过 `--converter` 指定的外部兼容 TEX 转换器；这里不附带现成转换器或游戏纹理。

框架自动分配图标 ID 与 **37～99 槽位**，最多 **63 份带图标声明**，与106个敌人身份上限分别计数。作者不分配槽位或编号。启动期复制各级 mip 像素行，默认通过静态链接的 SIMD 编码器生成 BC7；玩家不需要 ISPC 或额外编码器 DLL。

框架管理的生成文件：

```text
natives/stm/gui/ui_texture/tex000000/tex000201_1_imlm4.tex.241106027
reframework/data/UserDataExtension/zz_EnemyRegistry/icons.userdata.json
reframework/data/EnemyRegistry/atlas/cache.json
```

生成的 UDE 绑定只修改已经由作者建立的 EnemyData 行，不会替你追加一条字段完整的怪物记录。因此自己的公共数据声明应先创建该行，再由这份较晚的绑定修改图标。

缓存依据输入内容、输出格式与编码器身份判定；命中后跳过合图／编码。没有自定义图标时，框架会移除自己生成的图集、绑定与缓存。不要手改或把旧生成缓存／大图作为另一个 Mod 的权威输出；该共享输出路径不是某个怪物的私有纹理位置。

## 8. UserDataExtension 与初始化时序

```text
CI 加载 EnemyRegistry 并调用登记导出
  → 收集声明，分配 raw，准备图标文件
  → 向 CI 登记敌人枚举 canonical 改名
  → 显式加载 UDE，登记生成的 EmID JSON
CI 发布运行时类型与原生方法入口
  → EnemyRegistry ci_on_tdb_ready 安装原生容量／身份接入
  → UDE ci_on_tdb_ready 关闭配置登记并安装 USER 解码 hook
原生 USER 解码
  → UDE 在目标 USER 的 onLoad() 前应用有序修改
REFramework 插件初始化
  → 接入消息、图标发现及任务详情消费者
```

EnemyRegistry 自动生成的仅是 **EmID 身份增量**：先提升 `MAX` 哨兵的下限，再按 raw 去重追加缺行，最后更新 fixed ID／名称。哨兵不是普通怪物行，原来更高的 `MAX` 也不会被缩小到当前怪物数。

其他公共参数、目录、音乐、镜头及放置资格，仍由作者的 UDE 声明／原生资源负责。兼容的原版预设优先复用；确需扩展时做精确 UDE 增量，不覆盖整份公共 USER。怪物私有参数应是独立 USER 资源，不以长期覆盖公共表替代私有资源建设。

这不表示每个新游戏版本或任意原生消费者均已完成适配。缺失的初始化／资源契约需要在真实消费者处修正；收到 CI 回调，不等于动作、战斗、奖励、存档或联机都正常。

## 9. 验收与排错

冷启动后按顺序检查：

1. DLL／TOML 实际来自预期物理目录或 MO2 Mod，且为配套版本。
2. `logs/HIDWrapper.log` 接受并通知 EnemyRegistry Provider。
3. `logs/EnemyRegistry.log` 有身份冻结、容量安装和 pre-cctor 登记完成记录。
4. UDE 有 `EnemyRegistry/EmID` 的登记／应用记录，并应用自己的公共表增量。
5. 两个托管类型存在，实际怪物创建成功，生命周期持续推进。
6. 对受影响的 UI、本地化、战斗、退出清理与多人场景分别验收。

MO2 可能把当次插件日志重定向到 `overwrite/logs`，应核时间戳，不读旧物理日志下结论。Mod 列表覆盖优先级不是 DLL 依赖加载顺序。稍后 REFramework 列出某个 DLL，也不证明 CI 已在更早的窗口完成登记。

| 现象 | 优先检查 |
|---|---|
| 声明未参与登记 | 数据目录、直接子级、`.enemy.json` 后缀和 v4 格式 |
| 身份冲突 | 重复 fixed ID／枚举名、装了两份示例、使用保留值0／-1 |
| 派生工厂契约失败 | 类型拼写、CI 声明与实现、所需父类 |
| `failed opening atlas hash input` | 日志指出的 loose 基础图／小图及真实合并路径，不只看 PAK |
| 启动时音乐／数组崩溃 | 更早的注册、EmID、容量失败；仅追加音乐数据而没有配套原生接入并不成立 |
| 图标错误／EnemyData 行不存在 | TEX 输入契约、建行顺序、共享输出／缓存，以及其他 Mod 的同路径覆盖 |
| 消息缺失或安装失败 | GUID 合法且一致、全局唯一、英文回退非空、语言代码大小写 |
| 物理直启正常但 MO2 异常 | 实载 DLL SHA／路径、标记文件、依赖是否启用，以及早期登记日志 |

不要把已初始化游戏中的 DLL 热加载当成冷启动替代；游玩中删除声明也不会自动移除活怪或修复既有引擎缓存。

## 10. 源码职责与能力边界

独立构建、输出及离线测试见 [BUILDING.md](BUILDING.md)。

| 源码 | 职责 |
|---|---|
| `Plugin.cpp` | CI／RF 启动编排与显式 UDE 绑定 |
| `EnemyIdRegistry.*` | 解析声明、分配索引、不可变身份查询 |
| `EnemyRegistrationHooks.*` | 原生身份消费者、命名派生工厂类型校验与补丁安装 |
| `EnemyArrayCapacityPatches.cpp` | 收集并核对限定范围的容量／循环／立即数契约 |
| `EnemyEnumConfig.*` | 生成有序 EmID UDE JSON |
| `EnemyIconAtlas.*` | 校验输入、共享图集合成／编码／缓存及 UDE 绑定生成 |
| `EnemyMessageOverrides.*` | GUID 消息查询、语言选择、稳定文本缓冲生命周期 |
| `EnemyIconDiscovery.*`、`EnemyQuestVisibility.*` | 图标／任务详情的局部接入，不是全局解锁 |

内部 C++ 头不是另行版本化的跨 DLL C API。不要假定能用 `GetProcAddress` 获取 `FindEnemyByFixedId` 等内部函数；对接引擎使用原生身份转换、CI／REFramework 元数据，持久身份保留 fixed ID。

兼容性与游戏版本绑定。离线测试只验证列明的代码／指令契约，不附带或执行游戏快照，不替代完整怪物作者工具链，也不证明所有界面显示或在线场景通过。
