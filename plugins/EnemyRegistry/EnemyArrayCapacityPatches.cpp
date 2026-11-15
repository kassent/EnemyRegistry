#include "EnemyIdRegistry.h"
#include "EnemyRegistrationHooks.h"
#include <HookUtils.h>
#include <LogUtils.h>
#include <cstddef>

namespace EnemyRegistry::RegistrationHooks {
struct CapacityPatchContract {
    void* method;
    const char* pattern;
    uint32_t scanSize;
    uint32_t immediateOffset;
    uint32_t replacement;
    size_t expectedCount = 1;
};

// 各组对应已反编译核实的原生消费者，只匹配特定指令与立即数。
// 全部匹配数量核对通过之后再统一写入，不无差别替换整个函数中的0x96。
static bool ResolveCapacityImmediatePatches(std::vector<CapacityImmediatePatch>& patches, const CapacityPatchContract& contract) {
    if (!contract.method)
        return false;
    const auto end = reinterpret_cast<uintptr_t>(contract.method) + contract.scanSize;
    auto cursor = reinterpret_cast<uintptr_t>(contract.method);
    size_t count = 0;
    while (cursor < end) {
        const auto address = HookUtils::LookupFunctionPattern(reinterpret_cast<void*>(cursor), contract.pattern, static_cast<uint32_t>(end - cursor));
        if (!address)
            break;
        patches.push_back({address + contract.immediateOffset, contract.replacement});
        ++count;
        cursor = address + contract.immediateOffset + sizeof(uint32_t);
    }
    if (count != contract.expectedCount) {
        _MESSAGE("[EnemyRegistry] patch contract changed: method=%p pattern=%s "
                 "matches=%zu expected=%zu",
                 contract.method, contract.pattern, count, contract.expectedCount);
        return false;
    }
    return true;
}

bool ResolveEnemyCapacityPatches(const ci::ProviderContext* context, std::vector<CapacityImmediatePatch>& patches) {
    constexpr uint32_t CORPS_PER_ENEMY = 30;
    constexpr uint32_t WEAK_ATTRIBUTE_COUNT = 6; // HitDef.ATTR::MAX，包括 NONE。
    constexpr uint32_t ENEMY_ROLE_COUNT = 6;     // EnemyDef.ROLE_ID::MAX，与属性维度不是同一概念。
    constexpr auto ARRAY_HEADER_INT_COUNT = static_cast<uint32_t>(offsetof(RE::SystemArray<int32_t>, elements) / sizeof(int32_t));
    const auto ENEMY_COUNT = GetEnemyArrayCapacity();
    const auto CORPS_CAPACITY = ENEMY_COUNT * CORPS_PER_ENEMY;
    const auto LAST_RAW_ID = ENEMY_COUNT - 1;
    const auto createEnemyDataIndex = ResolveMethodBody(context, "app.VariousDataManager", "createEnemyDataIndex()");
    const auto ctorEnemyCatalogHolder = ResolveMethodBody(context, "app.cEnemyCatalogHolder", ".ctor()");
    const auto ctorEnemyManager = ResolveMethodBody(context, "app.EnemyManager", ".ctor()");
    const auto awakeGA = ResolveMethodBody(context, "app.EnemyManager", "awakeGA()");
    const auto update = ResolveMethodBody(context, "app.EnemyManager", "update()");
    const auto refreshValidEmids = ResolveMethodBody(context, "app.EnemyManager", "refreshValidEmids()");
    const auto onLoadEnemyIndividualCommonData = ResolveMethodBody(context, "app.user_data.EnemyIndividualCommonData", "onLoad()");
    const auto ctorBattleMusicManager = ResolveMethodBody(context, "app.BattleMusicManager", ".ctor()");
    const auto ctorBattleMusicCountManager = ResolveMethodBody(context, "app.BattleMusicManager.CountManager", ".ctor()");
    const auto ctorEnemyWorldAnalyzer = ResolveMethodBody(context, "app.EnemyWorldAnalyzer", ".ctor()");
    const auto awakeGAEnemyWorldAnalyzer = ResolveMethodBody(context, "app.EnemyWorldAnalyzer", "awakeGA()");
    const auto ctorEmParamWeakAttr = ResolveMethodBody(context, "app.user_data.EmParamWeakAttr", ".ctor()");
    const auto onLoadEmParamWeakAttr = ResolveMethodBody(context, "app.user_data.EmParamWeakAttr", "onLoad()");
    const auto ctorEmParamDanger = ResolveMethodBody(context, "app.user_data.EmParamDanger", ".ctor()");
    const auto onLoadEmParamDanger = ResolveMethodBody(context, "app.user_data.EmParamDanger", "onLoad()");
    const auto ctorEnemyCheckBoxBase = ResolveMethodBody(context, "app.cEnemyCheckBoxBase", ".ctor()");
    const auto setupEnemyCheckBoxBase = ResolveMethodBody(context, "app.cEnemyCheckBoxBase", "setup()");
    const auto onLoadEmParamCombatEmConditionPreset = ResolveMethodBody(context, "app.user_data.EmParamCombatEmConditionPreset", "onLoad()");
    const auto ctorEnemyRewardDataSetting = ResolveMethodBody(context, "app.user_data.EnemyRewardDataSetting", ".ctor()");
    const auto onLoadEnemyRewardDataSetting = ResolveMethodBody(context, "app.user_data.EnemyRewardDataSetting", "onLoad()");
    const auto Data = ResolveMethodBody(context, "app.EnemyDef", "Data(app.EnemyDef.ID)");
    const auto cctorEnemyUtil = ResolveMethodBody(context, "app.EnemyUtil", ".cctor()");
    const auto initExtendTypes = ResolveMethodBody(context, "app.EnemyUtil", "initExtendTypes()");
    const auto initContextUniqueTypes = ResolveMethodBody(context, "app.EnemyUtil", "initContextUniqueTypes()");
    // 以下范围均已逐组对TU5对应body核实；Manager ctor只扫描含容量分配的前0x1B00，
    // 不扫描后续整个大构造。每组数量变化立即报告具体消费者，不继续写补丁。
    // TU5: MOV R8D,150为ArrayNew长度（Manager另有三次Bitset构造参数）。
    // Catalog三表由原生addCatalog写入，不在addCatalog前临时换数组。
    const CapacityPatchContract contracts[] = {
        {ctorEnemyRewardDataSetting, "41 B8 96 00 00 00", 0x110, 2, ENEMY_COUNT},
        {onLoadEnemyRewardDataSetting, "49 81 FC 96 00 00 00 0F 85", 0x140, 3, ENEMY_COUNT},
        {createEnemyDataIndex, "41 B8 96 00 00 00", 0x70, 2, ENEMY_COUNT},
        // 该循环另有真实源数组长度检查；-1不可达，使所有追加元数据行都被扫描。
        {createEnemyDataIndex, "49 81 FE 96 00 00 00 75", 0xF0, 3, UINT32_MAX},
        {ctorEnemyCatalogHolder, "41 B8 96 00 00 00", 0x140, 2, ENEMY_COUNT, 3},
        // Counter/PackageHolder/TargetPoint/StrollPoint/IsSync/CorpsIndex/CorpsCount + 3 Bitsets。
        {ctorEnemyManager, "41 B8 96 00 00 00", 0x1B00, 2, ENEMY_COUNT, 10},
        {ctorEnemyManager, "41 B8 94 11 00 00", 0x1B00, 2, CORPS_CAPACITY},
        // awakeGA保留原生TargetPointCategoryPack分配/13类点表清空与尸体分类转换。
        {awakeGA, "49 81 FC 96 00 00 00 0F 85", 0xB60, 3, ENEMY_COUNT},
        {awakeGA, "48 81 FB 96 00 00 00 75", 0xB60, 3, ENEMY_COUNT},
        // TU5 update+0xB2F: INC R14; CMP R14,150; MOV RDI,[RSP+50]; JNZ update+0x8F0.
        // isChangePointList分支按raw重建TargetPointListTbl；保留13类别循环、owner.isEnable和原生List写入。
        // awakeGA只分配pack，不能替代此发布循环。仅扩raw上限，不赋予任何新点位membership。
        {update, "49 81 FE 96 00 00 00", 0xB50, 3, ENEMY_COUNT},
        // 从数组头后第8个int开始写-1，终点为4500+8。
        {awakeGA, "48 3D 9C 11 00 00 75", 0xB60, 2, CORPS_CAPACITY + ARRAY_HEADER_INT_COUNT},
        {refreshValidEmids, "41 B8 96 00 00 00 C5 F8 77", 0x110, 2, ENEMY_COUNT},
        {refreshValidEmids, "49 81 FE 96 00 00 00 75", 0x110, 3, ENEMY_COUNT},
        {onLoadEnemyIndividualCommonData, "41 B8 96 00 00 00", 0x120, 2, ENEMY_COUNT},
        {onLoadEnemyIndividualCommonData, "48 81 FB 96 00 00 00 75", 0x120, 3, ENEMY_COUNT},
        {ctorBattleMusicManager, "41 B8 96 00 00 00", 0x490, 2, ENEMY_COUNT},
        {ctorBattleMusicCountManager, "41 B8 96 00 00 00", 0x140, 2, ENEMY_COUNT, 3},
        {ctorEnemyWorldAnalyzer, "41 B8 96 00 00 00", 0x500, 2, ENEMY_COUNT, 3},
        {awakeGAEnemyWorldAnalyzer, "49 81 FE 96 00 00 00 0F 85", 0x710, 3, ENEMY_COUNT},
        // Boolean二维矩阵：总元素=rawCount*6；维度描述=rawCount；属性列不变。
        {ctorEmParamWeakAttr, "41 B8 84 03 00 00", 0x150, 2, ENEMY_COUNT * WEAK_ATTRIBUTE_COUNT},
        {ctorEmParamWeakAttr, "42 C7 44 0F 20 96 00 00 00", 0x150, 5, ENEMY_COUNT},
        {onLoadEmParamWeakAttr, "41 B9 84 03 00 00 E8", 0x290, 2, ENEMY_COUNT * WEAK_ATTRIBUTE_COUNT},
        {onLoadEmParamWeakAttr, "81 F9 95 00 00 00 0F 8F", 0x290, 2, LAST_RAW_ID},
        // Danger也是[raw,role]二维缓存（每元素28字节，6种role），onLoad不做raw上界检查。
        // 新增raw写入旧900元素数组会覆盖紧邻的维度描述，后续onLoad甚至会在首行读到坏stride。
        {ctorEmParamDanger, "41 B8 84 03 00 00", 0x240, 2, ENEMY_COUNT * ENEMY_ROLE_COUNT},
        {ctorEmParamDanger, "42 C7 44 0F 20 96 00 00 00", 0x240, 5, ENEMY_COUNT},
        {onLoadEmParamDanger, "41 B9 84 03 00 00 E8", 0x80, 2, ENEMY_COUNT * ENEMY_ROLE_COUNT},
        // 只扩raw缓存。原生fixed→ID_BIT查询/类别选择仍决定结果，不伪造新bit或默认放行。
        {ctorEnemyCheckBoxBase, "41 B8 96 00 00 00", 0xE0, 2, ENEMY_COUNT},
        {setupEnemyCheckBoxBase, "48 81 FB 96 00 00 00 75", 0xA0, 3, ENEMY_COUNT},
        // 无pair的新增raw也由原生onLoad创建空List；保留全部源pair匹配与字典生命周期。
        {onLoadEmParamCombatEmConditionPreset, "81 FD 96 00 00 00 0F 85", 0x1C0, 2, ENEMY_COUNT},
        // CMP raw,150; JE fallback; JNC fallback 共用比较。改为注册容量，允许新增raw原生查索引。
        {Data, "81 FA 96 00 00 00", 0x60, 2, ENEMY_COUNT},
        {cctorEnemyUtil, "41 B8 96 00 00 00", 0x160, 2, ENEMY_COUNT, 2},
        {initExtendTypes, "49 81 FF 96 00 00 00 0F 85", 0x190, 3, ENEMY_COUNT},
        {initContextUniqueTypes, "49 81 FF 96 00 00 00 0F 85", 0x190, 3, ENEMY_COUNT},
    };
    for (const auto& contract : contracts) {
        if (!ResolveCapacityImmediatePatches(patches, contract))
            return false;
    }
    struct MethodCapacityContract {
        const char* type;
        const char* signature;
        const char* pattern;
        uint32_t scanSize;
        uint32_t immediateOffset;
        uint32_t replacement;
        size_t expectedCount = 1;
    };
    // 镜头、群组、联网初始化、任务元数据及点位缓存：只调整已核原生 raw 维度。
    // 保留固定ID转换、点位/关系membership、独立个体语义和原生资源生命周期。
    const MethodCapacityContract runtimeContracts[] = {
        {.type = "app.user_data.EmCameraParamPackList", .signature = "onLoad()", .pattern = "41 B8 96 00 00 00", .scanSize = 0x45, .immediateOffset = 2, .replacement = ENEMY_COUNT},
        // raw类别×30群组。旧4500还兼任独立个体哨兵，容量与所有编码/解码入口必须一起移动。
        // 700个并行个体池与uniqueIndex中的200进制不属于物种维度，保持不变。
        {.type = "app.cEnemyCorpsList", .signature = ".ctor()", .pattern = "41 B8 94 11 00 00", .scanSize = 0x110, .immediateOffset = 2, .replacement = CORPS_CAPACITY},
        {.type = "app.EnemyManager", .signature = "createNoneSquad(app.cEnemyManageInfo)", .pattern = "41 B9 94 11 00 00 E8", .scanSize = 0x110, .immediateOffset = 2, .replacement = CORPS_CAPACITY},
        {.type = "app.cEnemyCorpsList", .signature = "pushSingleOfLife(app.cEnemySquadManageInfo)", .pattern = "41 B9 94 11 00 00 E8", .scanSize = 0x30, .immediateOffset = 2, .replacement = CORPS_CAPACITY},
        {.type = "app.cEnemyCorpsList", .signature = "getSquad(System.Int32)", .pattern = "48 3D 94 11 00 00 75", .scanSize = 0x90, .immediateOffset = 2, .replacement = CORPS_CAPACITY},
        // 当前版本把getSquad的哨兵识别内联到这两个入口，不能仅修改独立getSquad。
        {.type = "app.cEm1163_00Extend", .signature = "getLeader()", .pattern = "48 81 F9 94 11 00 00 75", .scanSize = 0x110, .immediateOffset = 3, .replacement = CORPS_CAPACITY},
        {.type = "app.cEm1163_00Extend", .signature = "isLeader()", .pattern = "48 81 F9 94 11 00 00 75", .scanSize = 0x110, .immediateOffset = 3, .replacement = CORPS_CAPACITY},
        {.type = "app.EnemyManager", .signature = "start()", .pattern = "49 81 FE 96 00 00 00 75", .scanSize = 0x240, .immediateOffset = 3, .replacement = ENEMY_COUNT},
        {.type = "app.VariousDataManager", .signature = "createEnemyQuestDataIndex()", .pattern = "41 B8 96 00 00 00", .scanSize = 0x70, .immediateOffset = 2, .replacement = ENEMY_COUNT},
        // 源表物理行数不是raw容量：保留真实源Length检查，解除前150行截断。
        {.type = "app.VariousDataManager", .signature = "createEnemyQuestDataIndex()", .pattern = "49 81 FE 96 00 00 00 75", .scanSize = 0xF0, .immediateOffset = 3, .replacement = UINT32_MAX},
        {.type = "app.EnemyDef", .signature = "QuestData(app.EnemyDef.ID)", .pattern = "81 FA 96 00 00 00", .scanSize = 0x40, .immediateOffset = 2, .replacement = ENEMY_COUNT},
        {.type = "app.VariousDataManager", .signature = "createEnemyCountDataIndex()", .pattern = "41 B8 96 00 00 00", .scanSize = 0x70, .immediateOffset = 2, .replacement = ENEMY_COUNT},
        {.type = "app.EnemyManager", .signature = "addStrollPoint(via.pointgraph.PointGraph, app.FieldDef.STAGE, app.EnemyDef.CATEGORY)", .pattern = "49 81 FF 96 00 00 00 0F 85", .scanSize = 0x600, .immediateOffset = 3, .replacement = ENEMY_COUNT},
        {.type = "app.EnemyManager", .signature = "clearStrollPoint(app.FieldDef.STAGE, app.EnemyDef.CATEGORY)", .pattern = "48 81 FD 96 00 00 00 75", .scanSize = 0x150, .immediateOffset = 3, .replacement = ENEMY_COUNT},
        {.type = "app.point_graph_data.enemy_target_point.EnemySleepPoint", .signature = "uniqueSetup(System.Int32)", .pattern = "48 81 FE 96 00 00 00 75", .scanSize = 0xA0, .immediateOffset = 3, .replacement = ENEMY_COUNT},
        {.type = "app.user_data.EmParamRelation.cData", .signature = "setupCache()", .pattern = "81 FD 96 00 00 00 75", .scanSize = 0x80, .immediateOffset = 2, .replacement = ENEMY_COUNT},
        // raw150已可被注册为新物种，不再作为旧MAX哨兵；其余资格判断保持原样。
        {.type = "app.cEnemyCheckBox", .signature = "isEnable(app.EnemyDef.ID)", .pattern = "41 81 F8 96 00 00 00 0F 84", .scanSize = 0x50, .immediateOffset = 3, .replacement = ENEMY_COUNT},
        {.type = "app.EnemyManager", .signature = "requestReturnCenter(via.vec3, System.Single, app.EnemyDef.ID, System.Int32, System.Int32)", .pattern = "41 81 FE 96 00 00 00 0F 84", .scanSize = 0x140, .immediateOffset = 3, .replacement = ENEMY_COUNT},
        {.type = "app.EnemyManager", .signature = "requestGetOutOfHere_EmID(via.vec3, System.Single, app.EnemyDef.ID, System.Boolean)", .pattern = "3D 96 00 00 00 74", .scanSize = 0x40, .immediateOffset = 1, .replacement = ENEMY_COUNT},
        {.type = "app.EnemyManager", .signature = "requestGetOutOfHere_OtherThanTheEmID(via.vec3, System.Single, app.EnemyDef.ID[], System.Boolean, System.Boolean, System.Boolean, System.Boolean)", .pattern = "41 81 F8 96 00 00 00 0F 84", .scanSize = 0x70, .immediateOffset = 3, .replacement = ENEMY_COUNT},
        // 两个包通知位图和区域移动使用位图，均由原生Bitset构造器按新容量计算桶数。
        {.type = "app.EnemyCharacter", .signature = ".ctor()", .pattern = "41 B8 96 00 00 00 E8", .scanSize = 0x4B0, .immediateOffset = 2, .replacement = ENEMY_COUNT, .expectedCount = 2},
        {.type = "app.mcEnemyAreaMoveCoordinator.cSchedulePatternInfo", .signature = ".ctor(app.EnemyDef.ID, app.user_data.EmParamAreaMove, System.Int32, System.Single)", .pattern = "41 B8 96 00 00 00 E8", .scanSize = 0x70, .immediateOffset = 2, .replacement = ENEMY_COUNT},
        {.type = "app.GUI020001PanelParams", .signature = "getCurrentEnemyType()", .pattern = "3D 96 00 00 00 74", .scanSize = 0x60, .immediateOffset = 1, .replacement = ENEMY_COUNT},
        {.type = "app.GUI020001PanelParams", .signature = "getCurrentEnemyType()", .pattern = "3D 95 00 00 00 77", .scanSize = 0x60, .immediateOffset = 1, .replacement = LAST_RAW_ID},
    };
    for (const auto& contract : runtimeContracts) {
        const auto method = ResolveMethodBody(context, contract.type, contract.signature);
        if (!ResolveCapacityImmediatePatches(patches, {.method = method, .pattern = contract.pattern, .scanSize = contract.scanSize, .immediateOffset = contract.immediateOffset, .replacement = contract.replacement, .expectedCount = contract.expectedCount})) {
            _MESSAGE("[EnemyRegistry] runtime raw-ID contract failed: %s.%s", contract.type, contract.signature);
            return false;
        }
    }
    _MESSAGE("[EnemyRegistry] runtime raw-ID contracts verified: contracts=%zu count=%u corps=%u", std::size(runtimeContracts), ENEMY_COUNT, CORPS_CAPACITY);
    // 图鉴只扩运行时 raw 索引及原生消费者；不改变 _Boss[100] 和发现演出[128]的存档格式。
    // 以下契约来自当前 TDB 入口与物理 EXE 指令逐项核对，保留原生分类、找槽、解锁和记账。
    const MethodCapacityContract reportContracts[] = {
        {.type = "app.EnemyReportUtil", .signature = "setReleaseTriggerEvent(app.EnemyDef.ID, app.EnemyReportDef.RELEASE_TYPE)", .pattern = "81 FB 96 00 00 00 0F 84", .scanSize = 0x21b, .immediateOffset = 2, .replacement = ENEMY_COUNT},
        {.type = "app.EnemyReportUtil", .signature = "setReleaseTrigger(app.EnemyDef.ID, app.EnemyReportUtil.RELEASE_TRIGGER_TYPE, app.EnemyReportUtil.HUNT_TARGET_TYPE)", .pattern = "81 FA 96 00 00 00 0F 84", .scanSize = 0x329, .immediateOffset = 2, .replacement = ENEMY_COUNT},
        {.type = "app.EnemyReportUtil", .signature = "isForceDisableChatLog(app.EnemyDef.ID)", .pattern = "81 FA 96 00 00 00 0F 84", .scanSize = 0x1b7, .immediateOffset = 2, .replacement = ENEMY_COUNT},
        {.type = "app.EnemyReportUtil", .signature = "setReleaseTriggerCommon(app.EnemyDef.ID, app.EnemyReportDef.RELEASE_TYPE, System.Boolean)", .pattern = "81 FA 96 00 00 00 0F 84", .scanSize = 0x5cb, .immediateOffset = 2, .replacement = ENEMY_COUNT},
        {.type = "app.EnemyReportUtil", .signature = "isOnReleaseTrigger(app.EnemyDef.ID, app.EnemyReportDef.RELEASE_TYPE)", .pattern = "81 FF 96 00 00 00 0F 84", .scanSize = 0x1fd, .immediateOffset = 2, .replacement = ENEMY_COUNT},
        {.type = "app.EnemyReportUtil", .signature = "isOnReleaseTriggerPrivate(app.EnemyDef.ID, app.EnemyReportDef.RELEASE_TYPE)", .pattern = "81 FA 96 00 00 00 0F 84", .scanSize = 0x1e9, .immediateOffset = 2, .replacement = ENEMY_COUNT},
        {.type = "app.EnemyReportUtil", .signature = "getEnemyReportState(app.EnemyDef.ID)", .pattern = "81 FA 96 00 00 00 0F 84", .scanSize = 0x184, .immediateOffset = 2, .replacement = ENEMY_COUNT},
        {.type = "app.EnemyReportUtil", .signature = "setEnemyReportState(app.EnemyDef.ID, app.EnemyReportState.STATE, System.Boolean)", .pattern = "81 FA 96 00 00 00 0F 84", .scanSize = 0x202, .immediateOffset = 2, .replacement = ENEMY_COUNT},
        {.type = "app.EnemyReportUtil", .signature = "setWriteRecord(app.EnemyDef.ID, app.EnemyReportUtil.HUNT_RECORD_TYPE, System.Int32, System.Int32)", .pattern = "81 FA 96 00 00 00 0F 84", .scanSize = 0x3b1, .immediateOffset = 2, .replacement = ENEMY_COUNT},
        {.type = "app.EnemyReportUtil", .signature = "getSlayingNum(app.EnemyDef.ID, System.Boolean)", .pattern = "81 FA 96 00 00 00 0F 84", .scanSize = 0x37e, .immediateOffset = 2, .replacement = ENEMY_COUNT},
        {.type = "app.EnemyReportUtil", .signature = "getCaptureNum(app.EnemyDef.ID, System.Boolean)", .pattern = "81 FA 96 00 00 00 0F 84", .scanSize = 0x400, .immediateOffset = 2, .replacement = ENEMY_COUNT},
        {.type = "app.EnemyReportUtil", .signature = "getHuntingNum(app.EnemyDef.ID)", .pattern = "81 FA 96 00 00 00 0F 84", .scanSize = 0x170, .immediateOffset = 2, .replacement = ENEMY_COUNT},
        {.type = "app.EnemyReportUtil", .signature = "getMinSize(app.EnemyDef.ID)", .pattern = "81 FA 96 00 00 00 0F 84", .scanSize = 0x170, .immediateOffset = 2, .replacement = ENEMY_COUNT},
        {.type = "app.EnemyReportUtil", .signature = "getMaxSize(app.EnemyDef.ID)", .pattern = "81 FA 96 00 00 00 0F 84", .scanSize = 0x188, .immediateOffset = 2, .replacement = ENEMY_COUNT},
        {.type = "app.EnemyReportUtil", .signature = "getMaxWeight(app.EnemyDef.ID)", .pattern = "81 FA 96 00 00 00 0F 84", .scanSize = 0x173, .immediateOffset = 2, .replacement = ENEMY_COUNT},
        {.type = "app.EnemyReportUtil", .signature = "isPackEnemy(app.EnemyDef.ID)", .pattern = "81 FA 96 00 00 00 74", .scanSize = 0x8e, .immediateOffset = 2, .replacement = ENEMY_COUNT},
        {.type = "app.EnemyReportUtil", .signature = "getPackEnemyInfo(app.EnemyDef.ID, System.Guid, app.EnemyDef.ID)", .pattern = "81 FB 96 00 00 00 74", .scanSize = 0x138, .immediateOffset = 2, .replacement = ENEMY_COUNT},
        {.type = "app.EnemyReportUtil", .signature = "getPackEnemyZakoInfo(app.EnemyDef.ID)", .pattern = "81 FA 96 00 00 00 0F 84", .scanSize = 0x193, .immediateOffset = 2, .replacement = ENEMY_COUNT},
        {.type = "app.EnemyReportUtil", .signature = "getPackEnemyAnimalInfo(app.EnemyDef.ID)", .pattern = "81 FA 96 00 00 00 0F 84", .scanSize = 0x193, .immediateOffset = 2, .replacement = ENEMY_COUNT},
        {.type = "app.EnemyReportUtil", .signature = "isPackTargetEnemy(app.EnemyDef.ID)", .pattern = "81 FA 96 00 00 00 74", .scanSize = 0x6d, .immediateOffset = 2, .replacement = ENEMY_COUNT},
        {.type = "app.EnemyReportUtil", .signature = "getPackEnemyZakoEmIDList(app.EnemyDef.ID)", .pattern = "81 FB 96 00 00 00 0F 84", .scanSize = 0x1e2, .immediateOffset = 2, .replacement = ENEMY_COUNT},
        {.type = "app.EnemyReportUtil", .signature = "getPackEnemyAnimalEmIDList(app.EnemyDef.ID)", .pattern = "81 FB 96 00 00 00 0F 84", .scanSize = 0x1e2, .immediateOffset = 2, .replacement = ENEMY_COUNT},
        {.type = "app.EnemyReportUtil", .signature = "setEnemyReportPageState(app.EnemyDef.ID, System.UInt32, app.EnemyReportUtil.PAGE_STATE_VALUE)", .pattern = "81 FA 96 00 00 00 0F 84", .scanSize = 0x246, .immediateOffset = 2, .replacement = ENEMY_COUNT},
        {.type = "app.EnemyReportUtil", .signature = "setEnemyReportEnemyUnreadState(app.EnemyDef.ID)", .pattern = "81 FA 96 00 00 00 0F 84", .scanSize = 0xc3, .immediateOffset = 2, .replacement = ENEMY_COUNT},
        {.type = "app.EnemyReportUtil", .signature = "createTargetEmBossList()", .pattern = "41 81 F8 96 00 00 00 74", .scanSize = 0x183, .immediateOffset = 3, .replacement = ENEMY_COUNT},
        {.type = "app.EnemyReportUtil", .signature = "createTargetEmZakoList()", .pattern = "41 81 F8 96 00 00 00 74", .scanSize = 0x186, .immediateOffset = 3, .replacement = ENEMY_COUNT},
        {.type = "app.EnemyReportUtil", .signature = "createTargetEmAnimalList()", .pattern = "41 81 F8 96 00 00 00 74", .scanSize = 0x1b9, .immediateOffset = 3, .replacement = ENEMY_COUNT},
        {.type = "app.EnemyReportUtil", .signature = "createTargetEmAnimalFishingList()", .pattern = "81 FA 96 00 00 00 74", .scanSize = 0x1ba, .immediateOffset = 2, .replacement = ENEMY_COUNT},
        {.type = "app.EnemyReportUtil", .signature = "createTargetEmAnimalFishingList()", .pattern = "81 FA 95 00 00 00 77", .scanSize = 0x1ba, .immediateOffset = 2, .replacement = LAST_RAW_ID},
        {.type = "app.EnemyReportUtil", .signature = "getEnemyReportEnemyState(app.EnemyDef.ID)", .pattern = "81 FA 96 00 00 00 0F 84", .scanSize = 0xcd, .immediateOffset = 2, .replacement = ENEMY_COUNT},
        {.type = "app.EnemyReportUtil", .signature = "getEnemyReportPageState(app.EnemyDef.ID, System.UInt32)", .pattern = "81 FA 96 00 00 00 0F 84", .scanSize = 0x1e6, .immediateOffset = 2, .replacement = ENEMY_COUNT},
        {.type = "app.EnemyReportUtil", .signature = "getNameSortNo(app.EnemyDef.ID, via.Language)", .pattern = "81 FD 96 00 00 00 0F 84", .scanSize = 0x38a, .immediateOffset = 2, .replacement = ENEMY_COUNT},
        {.type = "app.savedata.cEnemyReportParam", .signature = "setupForContinue()", .pattern = "41 B8 96 00 00 00 48", .scanSize = 0x442, .immediateOffset = 2, .replacement = ENEMY_COUNT},
        {.type = "app.savedata.cEnemyReportParam", .signature = "setupForContinue()", .pattern = "48 81 FB 96 00 00 00 0F 85", .scanSize = 0x442, .immediateOffset = 3, .replacement = ENEMY_COUNT},
        {.type = "app.savedata.cEnemyReportParam", .signature = "getBossParam(app.EnemyDef.ID, System.Boolean)", .pattern = "41 81 F8 96 00 00 00 74", .scanSize = 0x16e, .immediateOffset = 3, .replacement = ENEMY_COUNT},
        {.type = "app.savedata.cEnemyReportParam", .signature = "getZakoParam(app.EnemyDef.ID, System.Boolean)", .pattern = "41 81 F8 96 00 00 00 0F 84", .scanSize = 0x16c, .immediateOffset = 3, .replacement = ENEMY_COUNT},
        {.type = "app.savedata.cEnemyReportParam", .signature = "getAnimalParam(app.EnemyDef.ID, System.Boolean)", .pattern = "41 81 F8 96 00 00 00 0F 84", .scanSize = 0x16c, .immediateOffset = 3, .replacement = ENEMY_COUNT},
        {.type = "app.savedata.cEnemyReportParam", .signature = "getAnimalFishingParam(app.EnemyDef.ID, System.Boolean)", .pattern = "41 81 F8 96 00 00 00 0F 84", .scanSize = 0x18a, .immediateOffset = 3, .replacement = ENEMY_COUNT},
        {.type = "app.cFirstDiscoverySaveHelper", .signature = ".ctor()", .pattern = "41 B8 96 00 00 00 41", .scanSize = 0x6d, .immediateOffset = 2, .replacement = ENEMY_COUNT},
        // 实际 CameraManager 持有的 helper 由 Operator 内联构造，不经过上面的独立 .ctor。
        {.type = "app.cFirstDiscoveryOperator", .signature = ".ctor()", .pattern = "41 B8 96 00 00 00", .scanSize = 0x115, .immediateOffset = 2, .replacement = ENEMY_COUNT},
        // getOrCreateTracker按raw直接索引；只扩IndexTable，不改变16个并行tracker的池容量。
        {.type = "app.cFirstDiscoveryOperator.cTargetTrackerContainer", .signature = ".ctor()", .pattern = "41 B8 96 00 00 00", .scanSize = 0xcf, .immediateOffset = 2, .replacement = ENEMY_COUNT},
        {.type = "app.cGUIPartsEnemyReportBossEcology", .signature = "onPageActive()", .pattern = "81 FA 96 00 00 00", .scanSize = 0x20a, .immediateOffset = 2, .replacement = ENEMY_COUNT},
        {.type = "app.cGUIPartsEnemyReportBossEcology", .signature = "onPageActive()", .pattern = "81 FA 95 00 00 00", .scanSize = 0x20a, .immediateOffset = 2, .replacement = LAST_RAW_ID},
        {.type = "app.cGUIPartsEnemyReportBossGuidance", .signature = "onPageActive()", .pattern = "81 FA 96 00 00 00", .scanSize = 0x1fa, .immediateOffset = 2, .replacement = ENEMY_COUNT},
        {.type = "app.cGUIPartsEnemyReportBossGuidance", .signature = "onPageActive()", .pattern = "81 FA 95 00 00 00", .scanSize = 0x1fa, .immediateOffset = 2, .replacement = LAST_RAW_ID},
        {.type = "app.cGUIPartsEnemyReportBossMeasureInfo", .signature = "onPageActive()", .pattern = "3D 96 00 00 00", .scanSize = 0x10d, .immediateOffset = 1, .replacement = ENEMY_COUNT},
        {.type = "app.cGUIPartsEnemyReportBossMeasureInfo", .signature = "onPageActive()", .pattern = "3D 95 00 00 00", .scanSize = 0x10d, .immediateOffset = 1, .replacement = LAST_RAW_ID},
        {.type = "app.cGUIPartsEnemyReportBossMaterialList", .signature = "onPageActive()", .pattern = "3D 96 00 00 00", .scanSize = 0x4d2, .immediateOffset = 1, .replacement = ENEMY_COUNT},
        {.type = "app.cGUIPartsEnemyReportBossMaterialList", .signature = "onPageActive()", .pattern = "3D 95 00 00 00", .scanSize = 0x4d2, .immediateOffset = 1, .replacement = LAST_RAW_ID},
        {.type = "app.cGUIPartsEnemyReportBossRecord", .signature = "onPageActive()", .pattern = "81 FA 96 00 00 00", .scanSize = 0x1c9, .immediateOffset = 2, .replacement = ENEMY_COUNT},
        {.type = "app.cGUIPartsEnemyReportBossRecord", .signature = "onPageActive()", .pattern = "81 FA 95 00 00 00", .scanSize = 0x1c9, .immediateOffset = 2, .replacement = LAST_RAW_ID},
    };
    for (const auto& contract : reportContracts) {
        const auto method = ResolveMethodBody(context, contract.type, contract.signature);
        if (!ResolveCapacityImmediatePatches(patches, {.method = method, .pattern = contract.pattern, .scanSize = contract.scanSize, .immediateOffset = contract.immediateOffset, .replacement = contract.replacement})) {
            _MESSAGE("[EnemyRegistry] report raw-ID contract failed: %s.%s", contract.type, contract.signature);
            return false;
        }
    }
    _MESSAGE("[EnemyRegistry] report raw-ID contracts verified: patches=%zu count=%u; native save capacities unchanged", std::size(reportContracts), ENEMY_COUNT);
    // 这些文本入口各自内联了 raw 范围判断，不经过上面已扩容的 Data()。
    // 只改共用的 MAX 比较，保留 -1、真实数组长度、名字隐藏及 role/历战选择。
    struct TextCapacityContract {
        const char* signature;
        const char* pattern;
        uint32_t scanSize;
        uint32_t immediateOffset;
    };
    const TextCapacityContract textContracts[] = {
        // 图标和NPC分类各自内联EnemyData索引；不经Data()，也不能回落到源表第0行。
        {.signature = "ZakoIconType(app.EnemyDef.ID)", .pattern = "81 FA 96 00 00 00", .scanSize = 0x40, .immediateOffset = 2},
        {.signature = "ItemIconType(app.EnemyDef.ID)", .pattern = "81 FA 96 00 00 00", .scanSize = 0x40, .immediateOffset = 2},
        {.signature = "MapIconType(app.EnemyDef.ID)", .pattern = "81 FA 96 00 00 00", .scanSize = 0x40, .immediateOffset = 2},
        {.signature = "AnimalIconType(app.EnemyDef.ID)", .pattern = "81 FA 96 00 00 00", .scanSize = 0x40, .immediateOffset = 2},
        {.signature = "IsMapDraw(app.EnemyDef.ID)", .pattern = "81 FA 96 00 00 00", .scanSize = 0x40, .immediateOffset = 2},
        {.signature = "CategoryForNPC(app.EnemyDef.ID)", .pattern = "81 FA 96 00 00 00", .scanSize = 0x40, .immediateOffset = 2},
        {.signature = "JpEnemyName(app.EnemyDef.ID)", .pattern = "41 81 F8 96 00 00 00", .scanSize = 0x40, .immediateOffset = 3},
        {.signature = "EnemyName(app.EnemyDef.ID)", .pattern = "41 81 F8 96 00 00 00", .scanSize = 0x40, .immediateOffset = 3},
        {.signature = "EnemyExp(app.EnemyDef.ID)", .pattern = "41 81 F8 96 00 00 00", .scanSize = 0x40, .immediateOffset = 3},
        {.signature = "EnemyExtraName(app.EnemyDef.ID)", .pattern = "41 81 F8 96 00 00 00", .scanSize = 0x40, .immediateOffset = 3},
        {.signature = "EnemyBossExp(app.EnemyDef.ID)", .pattern = "41 81 F8 96 00 00 00", .scanSize = 0x40, .immediateOffset = 3},
        {.signature = "EnemyFrenzyName(app.EnemyDef.ID)", .pattern = "41 81 F8 96 00 00 00", .scanSize = 0x40, .immediateOffset = 3},
        {.signature = "EnemyLegendaryName(app.EnemyDef.ID)", .pattern = "41 81 F8 96 00 00 00", .scanSize = 0x40, .immediateOffset = 3},
        {.signature = "EnemyLegendaryKingName(app.EnemyDef.ID)", .pattern = "41 81 F8 96 00 00 00", .scanSize = 0x40, .immediateOffset = 3},
        {.signature = "EnemyFeatures(app.EnemyDef.ID)", .pattern = "41 81 F8 96 00 00 00", .scanSize = 0x40, .immediateOffset = 3},
        {.signature = "EnemyTips(app.EnemyDef.ID)", .pattern = "41 81 F8 96 00 00 00", .scanSize = 0x40, .immediateOffset = 3},
        {.signature = "FirstCapture(app.EnemyDef.ID)", .pattern = "41 81 F8 96 00 00 00", .scanSize = 0x40, .immediateOffset = 3},
        {.signature = "Memo(app.EnemyDef.ID)", .pattern = "41 81 F8 96 00 00 00", .scanSize = 0x40, .immediateOffset = 3},
        {.signature = "Grammar(app.EnemyDef.ID)", .pattern = "41 81 F8 96 00 00 00", .scanSize = 0x40, .immediateOffset = 3},
        {.signature = "Name(app.EnemyDef.ID, app.EnemyDef.ROLE_ID, app.EnemyDef.LEGENDARY_ID)", .pattern = "41 81 F8 96 00 00 00", .scanSize = 0x70, .immediateOffset = 3},
        {.signature = "NameForceDisp(app.EnemyDef.ID, app.EnemyDef.ROLE_ID, app.EnemyDef.LEGENDARY_ID)", .pattern = "41 81 F8 96 00 00 00", .scanSize = 0x60, .immediateOffset = 3},
        {.signature = "JpName(app.EnemyDef.ID, app.EnemyDef.ROLE_ID, app.EnemyDef.LEGENDARY_ID)", .pattern = "81 FF 96 00 00 00", .scanSize = 0xD0, .immediateOffset = 2},
        {.signature = "Explain(app.EnemyDef.ID, app.EnemyDef.ROLE_ID, app.EnemyDef.LEGENDARY_ID)", .pattern = "41 81 F8 96 00 00 00", .scanSize = 0x70, .immediateOffset = 3},
        {.signature = "CountJP(app.EnemyDef.ID)", .pattern = "41 81 F8 96 00 00 00", .scanSize = 0x40, .immediateOffset = 3},
        {.signature = "CountCN(app.EnemyDef.ID)", .pattern = "41 81 F8 96 00 00 00", .scanSize = 0x40, .immediateOffset = 3},
        {.signature = "CountTW(app.EnemyDef.ID)", .pattern = "41 81 F8 96 00 00 00", .scanSize = 0x40, .immediateOffset = 3},
        {.signature = "CountKO(app.EnemyDef.ID)", .pattern = "41 81 F8 96 00 00 00", .scanSize = 0x40, .immediateOffset = 3},
        {.signature = "EmSpeciesName(app.EnemyDef.SPECIES, app.EnemyDef.ID)", .pattern = "81 FD 96 00 00 00", .scanSize = 0x50, .immediateOffset = 2},
    };
    for (const auto& contract : textContracts) {
        const auto method = ResolveMethodBody(context, "app.EnemyDef", contract.signature);
        if (!ResolveCapacityImmediatePatches(patches, {.method = method, .pattern = contract.pattern, .scanSize = contract.scanSize, .immediateOffset = contract.immediateOffset, .replacement = ENEMY_COUNT})) {
            _MESSAGE("[EnemyRegistry] text raw-ID contract failed: %s", contract.signature);
            return false;
        }
    }
    _MESSAGE("[EnemyRegistry] text raw-ID contracts verified: consumers=%zu count=%u", std::size(textContracts), ENEMY_COUNT);
    return true;
}

} // namespace EnemyRegistry::RegistrationHooks
