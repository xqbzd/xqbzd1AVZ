// ============================================================================
// PE 九六 四花 Claude Fable 5.1 Ultracode Ver.5
// LI5HDHvtAib/1GrXaldAXDjDyH3tQXziCjRpEJA1cUQ6MYyc5GU0aVAWEEg4RHbM7lBkGFJU7E1K6lY=
//
//  相对原 PE96（PE 九六 202609131832.cpp）的改动：
//    · 补忧郁菇 / 补大喷菇 / 补南瓜（原 aPlantFixer，Ver.4 换成快照版 PumpkinFixer）/ 预存寒冰菇（aIceFiller）沿用原脚本
//    · 卡片：删掉辣椒，换成小喷菇；后 3 张按本关情况选（补曾 / 小喷菇 / 西瓜 + 冰瓜，见 AScript 的选卡）
//    · 原本“按时间点用冰 / 用倭瓜 / 用樱桃 / 用辣椒”的帧运行全部删掉，
//      改成子力判断：某张卡 cd 好了以后，扫全场找“子力最高的操作位”，
//      子力 >= 对应阈值才落子（冰没有位置，子力是全场一个数）
//
//  子力口径（都在 zombie_threat_level.h / fodder_value.h 里实现）：
//    威胁值 = 威胁血量 × 距离系数(C·e^(-k·x)) × 种类修正 × 速度系数
//      威胁血量 = 本体 + 一类防具 − 即将到来的灰烬/炮弹伤害 − 自然输出（大喷菇/忧郁菇），下限 0
//    · 樱桃 / 倭瓜：某格子力 = Σ min(1800, 威胁血量) × 距离系数 × 种类修正 × 速度系数
//    · 冰        ：子力 = 用冰以后全场僵尸“威胁血量”总共掉多少（冻结 + 减速带来的自然输出增量
//                  + 冰本身的 20 点伤害），再乘距离/种类/速度系数
//                  + 气球项（Ver.4）：飞过阵前、还没减速的飞行气球，按“还有多久进家”折算的威胁值
//                  （见下面“气球的冰子力”一节）
//    · 小喷菇 / 大喷菇：垫材子力（fodder_value.h）= Σ 威胁度 × 挡住时长 × 0.01
//                  —— 注意单位跟上面四个不是一个 scale，所以阈值要分开调
//
//  距离系数：每行 C = 1、k = 1（按“格”衰减，即每格 e^-1）；要改去改 aThreatLevelRowParams
//  种类修正：本阵型只给 冰车 / 橄榄 / 红眼 / 白眼 / 小丑 记子力，其余全部为 0
//           气球只在冰的气球项里记（kBalloonIceTypeFactor，按“进家时间”算），aThreatTypeFactor 的
//           气球项仍是 0，所以樱桃 / 倭瓜 / 垫材一律忽略气球
//
//  ---------------------------------------------------------------------------
//  性能（本版）：
//    原来每帧对 54 格 × 4 种卡片逐格重新扫全场僵尸 / 植物（每格、每只僵尸都从头算阵前、
//    自然输出、即将到来的伤害），一帧要做上万次全场遍历。现在：
//    · 参数化控制测试文档/ 里的库函数加了“帧内缓存”：一帧只扫一遍僵尸 / 植物 / 子弹数组，
//      每只僵尸的威胁血量 / 自然输出 / 威胁度、每颗灰烬能炸到的格子都只算一次，
//      之后各格查询只剩下几何判定与查表。判定条件、遍历顺序、排序都与原来一致，
//      所以每格算出来的子力、每次落子的决定与原来相同。
//    · 本文件的补阵逻辑（PlantCnt / ZombieCnt / 铲南瓜）也改成在同一份快照上查，
//      条件逐句照抄原来的。
//    · 同一帧内种下 / 铲掉植物后立刻让缓存作废（AInvalidateFrameCache），
//      所以“先种樱桃、再算垫材”这种帧内先后关系也和原来一样。
//    · 编译器：AvZ 扩展默认不带 -O 参数（等于 -O0）。想再快几倍，把 VSCode 设置
//      avzConfigure.compileOptions 改成 ["-g", "-Wall", "-O2"] 即可，代码不用改。
//    · 屏幕底部显示 逻辑 / 绘制 每帧耗时（kShowPerfStats），方便对比。
//
//  Ver.2 再砍掉的重复计算（判断结果不变）：
//    · 种类修正为 0 的僵尸不算威胁度（它们在所有子力累加里的项都恰好是 0.0）
//    · 本帧没有任何卡能用到威胁度快照时（樱桃 / 倭瓜在 cd、冰放不出、无小丑在窗口）整段不算
//    · 快照里一条记录都没有时，樱桃 / 倭瓜的 54 格扫描直接跳过（每格必为 0，不可能落子）
//    · 垫材：没有僵尸贴着的格子直接跳过（子力必为 0、无副作用），省掉游戏函数 GetPlantRejectType
//    · 补阵的 PlantCnt / ZombieCnt / 铲南瓜 只看该格 / 该行的表，不再全表扫描
//    · 格子像素（GridToAbscissa / GridToOrdinate 游戏函数）一关只问一次，重建快照与每格命中框都查表
//  到这里脚本自己每帧的开销已经很小；再往上就是游戏引擎本身（僵尸 AI、动画、绘制）的成本，
//  脚本层面剩下的手段只有跳帧（ASkipTick，不绘制的帧不花渲染时间），见 AScript 里注释掉的那两行。
//
//  Ver.4：寒冰菇的子力把气球算进来
//    · 飞行中的气球大喷菇 / 忧郁菇打不到、冰也冻不住，但会被冰减速 2000cs（速度 ×0.4），
//      所以对气球来说冰的意义只有一个：把它进家的时间拖长。于是气球的威胁值不按“离阵前多远”
//      算，而按“还有多久进家”算（越快进家越高）；进家时间用 Ref/MGE 群曾 202609160000.cpp
//      里 BalloonΔX 的位移模型（原速 / 减速 ×0.4）反过来解
//    · 只有“正在飞（状态 73）、没减速、已经飞过本行阵前（含 Pe96RowFrontAllowed 的岸路 6 列
//      限制）、冰生效前来得及”的气球才计入冰的子力；被冰减速后它的进家时间变长、威胁值随之
//      降低，而且不再满足“没减速”，所以冰不会对同一只气球连着出手
//    · 樱桃 / 倭瓜 / 垫材 / 冰的地面项对气球的处理与 Ver.2 完全一样（气球的种类修正仍为 0，只有冰的气球项看它）
//    · 樱桃禁区：2-4 / 5-4 上放着梯子，樱桃放在 1~5 列会把梯子炸掉，所以这些格子的樱桃子力
//      一律记 0、落子扫描直接跳过（kCherryMinCol）
//    · 冰的间隔修正：两颗冰生效间隔太短会浪费减速，冰子力乘一个随“距上一颗冰生效多久”爬升的间隔系数；
//      下一颗冰（用 RealSeedCD 读冰卡的剩余冷却）来得及接上时不打折
//      （kIceMinIntervalCs / kIceIntervalRelaxWhenNextReady，见“冰的间隔修正”一节）
//    · 补阵用的计数 / 试种换成 Ref/MGE 群曾 202609160000.cpp 的接口：PlantCnt / ZombieCnt /
//      TryCard（参数都是列表，{} = 不限，可以一次传多个类型 / 格子 / 行），实现仍在本帧快照上
//      查表（指定格子 / 行时不全表扫描），见“补阵用的计数 / 试种”一节
//
//  Ver.4 再砍掉的重复计算（判断结果不变）：
//    · 小喷菇 / 大喷菇的垫材子力逐格完全相同，一帧只扫一遍；小喷菇真的落子了才为大喷菇重扫
//    · 樱桃 / 倭瓜：整行都炸不到“记了子力”的僵尸的行直接跳过（樱桃看 ±1 行、倭瓜看本行）
//    · 每格的命中预测 / 垫材估值写进复用的缓冲区，不再每格分配一次内存（库里加了 …Into 版本）
//    · 威胁度快照只重置“有没有记录”那张表，不再整表清零；“冰这一帧能不能放”一帧只判一次
//    · 库：重建快照 / 预处理命中表的临时数组改为复用；炮弹落点 / 行号反查改走格子像素表
//    · 补南瓜不再用 AvZ 的 aPlantFixer（它每帧建一个 std::map、再用过滤器把全场植物扫一遍），
//      换成本帧快照上逐句复刻的 PumpkinFixer，判断 / 落子顺序 / 启动顺序都和原来一样
//
//  Ver.5 再砍掉的重复计算（判断结果不变）：
//    · 补曾 / 补瓜共用一套骨架（FixCell）：三张卡都在 cd 时直接返回，不再每帧数三次僵尸
//    · 曾 / 冰瓜的总数一遍扫完，不再各扫一遍全场植物
// ============================================================================

#include "ShowWavelength/ShowWavelength.h"
#include "src/fodder_value.h"

#include <avz.h>
#include <dsl/shorthand.h>

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstddef>
#include <initializer_list>
#include <string>
#include <vector>

// ============================================================================
//  ↓↓↓ 平时只改这一块：五个子力阈值 ↓↓↓
// ============================================================================
// 冰（寒冰菇）：全场子力，单位 = “威胁值削减”
constexpr double kIceThreshold = 3000.0;
// 冰的气球项（Ver.4）：一只飞行气球的威胁值
//     = 威胁血量（本体 270，扣掉已经要炸到它的灰烬）× kBalloonIceTypeFactor × e^(−进家时间 / kBalloonHomeTimeScaleCs)
//   只有冰看这份威胁值；樱桃 / 倭瓜 / 垫材一律忽略气球（aThreatTypeFactor 的气球项保持 0）。
//   · kBalloonIceTypeFactor：气球的种类修正。飞到家门口（进家时间 0）时威胁值 = 270 × 它；50 → 13500
//   · kBalloonHomeTimeScaleCs：进家时间每多这么多 cs，威胁值打 e^-1 ≈ 0.37 折
//   一只没减速的气球单独让冰出手的时机（地面项为 0 时）：
//     进家时间 <= kBalloonHomeTimeScaleCs × ln(270 × kBalloonIceTypeFactor / kIceThreshold)
//     按默认值 = 2000 × ln(13500 / 3000) ≈ 3000cs；气球原速 v（内存 Speed()，一般约 0.2px/cs）时
//     相当于离家 3000 × v ≈ 600px，也就是差不多刚飞过岸路 6 列阵前的位置。
//     想让冰晚点出手就把 kBalloonIceTypeFactor 调小（例如 30 → 约 2000cs）。
constexpr double kBalloonIceTypeFactor = 50.0;
constexpr double kBalloonHomeTimeScaleCs = 2000.0;
// 冰的间隔修正（Ver.4）：冰的减速持续 2000cs，两颗冰生效间隔太短，后一颗的减速大半和前一颗重叠，白白浪费。
//   间隔系数 = min(1, (距上一颗冰生效的时间 + 冰的生效延迟 299) / kIceMinIntervalCs)
//   冰子力 = (地面项 + 气球项) × 间隔系数 —— 上一颗冰刚生效时系数接近 0，过了 kIceMinIntervalCs 回到 1。
//   上一颗冰什么时候生效的，脚本自己记（见“冰的间隔修正”一节，连被巨人砸引爆的存冰都算得到）。
//   kIceIntervalRelaxWhenNextReady：冰不缺的时候不必省着用 —— 这颗用掉以后，下一颗冰（存冰位里的第二颗，
//   或两张冰卡里先冷却完的那张补进来的，卡的剩余冷却用 RealSeedCD 读）能在这颗的减速结束前生效，就不打折。
//   不要这条例外就改成 false。
constexpr int kIceMinIntervalCs = 2000;
constexpr bool kIceIntervalRelaxWhenNextReady = true;
// 樱桃炸弹：单位 = “威胁值削减”（Σ min(1800, 威胁血量) × 三个系数）
constexpr double kCherryThreshold = 6000.0;
// 樱桃禁区：本阵 2-4 / 5-4 上放着梯子，樱桃（半径 115）放在 1~5 列会把梯子炸掉，
// 所以樱桃只允许放在这一列及更靠右的格子；1~5 列的樱桃子力一律记 0，落子扫描直接跳过
constexpr int kCherryMinCol = 6;
// 倭瓜：单位 = “威胁值削减”
constexpr double kSquashThreshold = 2000.0;
// 小喷菇：单位 = 垫材子力（Σ 威胁度 × 挡住时长 × 0.01）
constexpr double kPuffThreshold = 50;
// 大喷菇：单位 = 垫材子力（同上）
constexpr double kFumeThreshold = 2000.0;

// 打开后每次落子都会在日志里打印“卡片 / 子力 / 位置 / 阈值”，方便调阈值
constexpr bool kDebugValueLog = true;

// 格子上显示实时子力：
//   每格固定 4 行的位置 = 1 冰(深蓝) / 2 樱桃(红) / 3 倭瓜(白) / 4 小喷菇(紫)
//   冰的子力是全场的，所以只有存冰位画第 1 行，其它格子不画（保持各格第 2~4 行对齐）
//   小喷菇与 大喷菇 的垫材子力相同（血量都是 300、都不会自行消失、都能被车压），共用第 4 行
//   放不下去的格子（冰道 / 已有植物 / 水缺荷叶 / 墓碑…）一律画 0，和落子判断保持一致
constexpr bool kShowValueOverlay = false;

// 屏幕底部显示每帧耗时：L = 逻辑帧运行（Logic）耗时，P = 子力显示绘制耗时，单位微秒，
// 各取最近 100 次的平均。只是看性能用，不影响任何判断。
constexpr bool kShowPerfStats = false;

// 存冰位（1 起）：存冰、冰释放（放咖啡豆的位置）、冰子力显示 三处共用这一份配置
// 现在设的是 3 行 4 列；要两个存冰位就写 {{3, 4}, {4, 4}}（存冰优先列表末尾那个，释放取第一个）
const std::vector<AGrid> kIceStoreGrids = {{4, 4}};
// ============================================================================
//  ↑↑↑ 阈值到这里 ↑↑↑
// ============================================================================

// ---------------- 冰的估值参数（反编译 damage.cpp 的 iceshroom） ----------------
// 能冻的僵尸：水里 300cs；原本就已经被冻/减速 300+rand(101)≈350cs；否则 400+rand(201)≈500cs
constexpr int kIceFreezeCsWater = 300;
constexpr int kIceFreezeCsAlready = 350;
constexpr int kIceFreezeCsFresh = 500;
constexpr int kIceSlowCs = 2000;     // 冰还会给全场 2000cs 减速（速度 ×0.4）
constexpr int kIceDamage = 20;       // 冰本身的 20 点伤害（忽略二类防具）
constexpr int kIceActiveCs = 299;    // 白天冰的生效时间（原脚本同款）
constexpr int kCoffeeBeanCdCs = 751; // 咖啡豆的冷却（7.51s）：下一颗存冰要等它才能唤醒（冰的间隔修正用）

using aInstantPlant::AFrameSnapshot;
using aInstantPlant::AGetFrameSnapshot;
using aInstantPlant::AInvalidateFrameCache;
using aInstantPlant::APlantSnapshot;
using aInstantPlant::AZombieSnapshot;

namespace {

// ============================================================================
//  阵前筛选：PE96 在红眼关把岸路锁到 6 列
//    2-7 / 5-7 放的是临时喷，本身保不住、守它的代价比它多打那点输出还高，
//    所以不让 7 列的临时喷把“阵前”推前（否则威胁距离、自然输出都会按 7 列算）。
//    红眼关（本关出怪列表里有红眼）时，岸路（2 / 5 行）只认 6 列及更靠后的植物当阵前；
//    其它行、非红眼关都不限制。这是库提供的通用钩子，库里不含任何 PE96 逻辑，
//    换成别的阵型只要换一个这样的函数即可。
// ============================================================================
bool Pe96RowFrontAllowed(APlant* plant, int row) {
    if (row != 2 && row != 5)
        return true; // 只有岸路受限
    AMainObject* main = AGetMainObject();
    if (main == nullptr || !main->ZombieTypeList()[AGIGA_GARGANTUAR])
        return true;              // 不是红眼关
    return plant->Col() + 1 <= 6; // 6 列及更靠后才算阵前
}

// ============================================================================
//  小丑“必炸”威胁：保护忧郁菇
//    反编译 Zombie::UpdateZombieJackInTheBox：进入开盒状态时 mPhaseCounter = 110，
//    减到 0 才爆炸；灰烬生效 100cs（樱桃 / 辣椒 / 夜晚毁灭菇）。所以开盒倒计时落在
//    (100, 110] 这段时，现在种下去的灰烬能赶在小丑爆炸前生效，把它炸死。
//    若这只小丑的爆炸会波及已经存在的忧郁菇，就把它的威胁度顶到 10 万
//    （无视距离 / 种类 / 速度修正），这样灰烬一定会优先去炸它。
// ============================================================================
constexpr int kAshEffectCs = 100;            // 樱桃 / 辣椒 / 夜晚毁灭菇的生效时间
constexpr int kJackPopCs = 110;              // 小丑开盒状态的总倒计时（进入时置 110）
constexpr double kJackForceValue = 100000.0; // “必炸”目标的威胁度

// 小丑这次爆炸会不会波及已有的忧郁菇（爆炸圆心、半径与库里的 AIsPlantJacked 一致）
bool JackExplosionHitsGloom(AZombie* jack) {
    const int centerX = static_cast<int>(jack->Abscissa()) + 60;
    const int centerY = static_cast<int>(jack->Ordinate()) + 60;
    for (const APlantSnapshot& plant : AGetFrameSnapshot().plants) {
        if (plant.baseType != AGLOOM_SHROOM)
            continue;
        if (aThreat::AHitBoxOverlapCircle(aThreat::AGetPlantHitBox(plant.plant), centerX, centerY,
                aThreat::JACK_PLANT_KILL_RADIUS))
            return true;
    }
    return false;
}

double Pe96ThreatOverride(AZombie* zombie) {
    if (zombie->Type() != AJACK_IN_THE_BOX_ZOMBIE)
        return 0.0;
    if (zombie->State() != aThreat::JACKBOX_POP_STATE)
        return 0.0;
    const int countdown = zombie->StateCountdown();
    if (countdown <= kAshEffectCs || countdown > kJackPopCs)
        return 0.0; // 不在“灰烬能先炸”的窗口里
    if (!JackExplosionHitsGloom(zombie))
        return 0.0;
    return kJackForceValue;
}

// 调参用：小丑进入“灰烬能先炸”的窗口时，把判定过程打出来（窗口只有 10 帧，不会刷屏）。
//   如果小丑明明在开盒、日志里却一行 [丑] 都没有，说明倒计时没落在窗口里；
//   如果打了 [丑] 但“能炸到它的樱桃格”一行都没有，说明是命中判定/可种植位置的问题。
int gJackLogCountdown = -1;

bool IsPlantable(APlantType type, int row, int col);  // 定义在后面
double AshValueAt(int row, int col, APlantType type); // 定义在后面

// 正在飞的气球僵尸（状态 73）：只有冰的气球项按进家时间给它记威胁值，其它子力一律忽略它；
// 坠落 / 落地后的气球僵尸是普通地面僵尸，按种类修正 0 处理
bool IsFlyingBalloon(const AZombieSnapshot& snap) {
    return snap.type == ABALLOON_ZOMBIE && snap.state == aInstantPlant::STATE_BALLOON_FLYING && !snap.dead;
}

// ============================================================================
//  威胁度口径：本帧快照 + 本阵型参数
//    ThreatCache 是 Logic 在“子力判断”开始时对全场僵尸拍的一份快照，按僵尸内存数组
//    下标索引（原来是 std::map<AZombie*, ...>，现在改成数组下标直接查，内容一样）。
// ============================================================================
std::vector<AZombieThreatLevel> ThreatCache; // 下标 = 僵尸内存数组下标
std::vector<char> ThreatCacheHas;            // 该下标是否有记录
int ThreatCacheCount = 0;                    // 记录条数（= 原来 ThreatCache.size()）
int ThreatCacheRowCount[6] = {};             // 每行（0 起）有几条记录：灰烬扫描用来整行跳过

const AZombieThreatLevel* FindThreat(AZombie* zombie) {
    AMainObject* main = AGetMainObject();
    if (main == nullptr || zombie == nullptr)
        return nullptr;
    const std::ptrdiff_t index = zombie - main->ZombieArray();
    if (index < 0 || index >= static_cast<std::ptrdiff_t>(ThreatCache.size()) || !ThreatCacheHas[index])
        return nullptr;
    return &ThreatCache[index];
}

AThreatLevelOptions ValueOptions() {
    AThreatLevelOptions options;
    // k = 1 按“格”衰减；要按像素就把这里改 false，并把 aThreatLevelRowParams 的 k 填 1.0 / 80
    options.distanceInTiles = true;
    // 种类修正：aThreatTypeFactor 里没写（=0）的僵尸类型用这个默认值 → 本阵型全按 0
    options.typeFactorDefault = 0.0;
    return options;
}

// 这种僵尸在本阵型的子力口径下会不会产生非零的项。
//   樱桃 / 倭瓜 / 冰的子力都是 “伤害 × 距离系数 × 种类修正 × 速度系数” 的累加，种类修正为 0
//   的僵尸每一项都恰好是 0.0，加不加进去结果位级相同；唯一的例外是威胁度覆盖（Pe96ThreatOverride
//   只对小丑给覆盖值），所以小丑一律算。这样普僵 / 路障 / 铁桶 / 撑杆等就不必算阵前、自然输出。
//   气球的种类修正也是 0（不进这份快照）：它只在冰的气球项里按 kBalloonIceTypeFactor 记子力，
//   BalloonIceValue 自己扫快照，不经过 ThreatCache；樱桃 / 倭瓜 / 垫材因此都看不到气球。
bool CountsForValue(int zombieType, double typeFactorDefault) {
    if (zombieType == AJACK_IN_THE_BOX_ZOMBIE)
        return true;
    return aThreatLevel::TypeFactor(zombieType, typeFactorDefault) != 0.0;
}

void ClearThreatCache() {
    ThreatCache.clear();
    ThreatCacheHas.clear();
    ThreatCacheCount = 0;
    for (int& count : ThreatCacheRowCount)
        count = 0;
}

void RefreshThreatCache() {
    const AThreatLevelOptions options = ValueOptions();
    const AFrameSnapshot& snap = AGetFrameSnapshot();
    const int total = snap.stamp.mainObject != nullptr ? snap.stamp.mainObject->ZombieTotal() : 0;
    // 只重置“有没有记录”那张表；ThreatCache 里没记录的下标从来不会被读（FindThreat 先查 Has），
    // 所以不必整表清零（僵尸数组几百个槽 × 一百多字节，每帧都清一遍是白做的）
    ThreatCache.resize(total > 0 ? total : 0);
    ThreatCacheHas.assign(total > 0 ? total : 0, 0);
    ThreatCacheCount = 0;
    for (int& count : ThreatCacheRowCount)
        count = 0;
    for (const AZombieSnapshot& zombie : snap.zombies) {
        if (!CountsForValue(zombie.type, options.typeFactorDefault))
            continue; // 种类修正 0（含气球）：所有子力项都是 0，没有记录 == 加了一个 0
        ThreatCache[zombie.index] = AGetZombieThreatLevel(zombie.zombie, options);
        ThreatCacheHas[zombie.index] = 1;
        ++ThreatCacheCount;
        if (zombie.row >= 0 && zombie.row < 6)
            ++ThreatCacheRowCount[zombie.row];
    }
}

// 本帧子力判断阶段有没有人会用到 ThreatCache：
//   AshValueAt（樱桃 / 倭瓜卡可用时）、IceValue（冰能放时，iceReady 由 LogicBody 一帧算一次传进来）、
//   LogJackState（小丑在窗口里时）。都没有就不必算（垫材子力走库自己的帧内缓存，不用 ThreatCache）。
bool ThreatCacheNeeded(bool iceReady) {
    if (AIsSeedUsable(ACHERRY_BOMB) || AIsSeedUsable(ASQUASH))
        return true;
    if (iceReady)
        return true;
    if (kDebugValueLog) {
        for (const AZombieSnapshot& zombie : AGetFrameSnapshot().zombies) {
            if (zombie.type != AJACK_IN_THE_BOX_ZOMBIE || zombie.state != aThreat::JACKBOX_POP_STATE)
                continue;
            const int countdown = zombie.zombie->StateCountdown();
            if (countdown > kAshEffectCs && countdown <= kJackPopCs)
                return true;
        }
    }
    return false;
}

// 读一张卡还有几 cs 冷却完（照 Ref/MGE 群曾 202609160000.cpp 的 RealSeedCD）：
//   0 = 现在就能用（含正拿在手上的那张）；>0 = 还要这么多帧；-1 = 卡槽里没有这张卡。
//   模仿者卡也能查（传 AM_ 开头的类型）。冰的间隔修正用它读两张冰卡的剩余冷却，小丑日志用它读樱桃的
int RealSeedCD(APlantType Type) {
    if (AMRef<int>(0x6A9EC0, 0x768, 0x138, 0x28) == Type) // 手上正拿着这张卡
        return 0;
    for (auto&& Seed : ABasicFilter<ASeed>()) {
        if (Seed.Type() == Type || Seed.ImitatorType() == (Type - 49)) {
            if (Seed.IsUsable())
                return 0;
            if (Seed.InitialCd() == 0)
                return 0;
            return (Seed.InitialCd() + 1 - Seed.Cd());
        }
    }
    return -1;
}

void LogJackState() {
    if (!kDebugValueLog)
        return;
    bool inWindow = false;
    const AFrameSnapshot& snap = AGetFrameSnapshot();
    for (const AZombieSnapshot& zombieSnap : snap.zombies) {
        AZombie& zombie = *zombieSnap.zombie;
        // 类型 / 状态直接用快照里的（本帧同一份内存读出来的），不再逐只读内存
        if (zombieSnap.type != AJACK_IN_THE_BOX_ZOMBIE || zombieSnap.state != aThreat::JACKBOX_POP_STATE)
            continue;
        const int countdown = zombie.StateCountdown();
        if (countdown <= kAshEffectCs || countdown > kJackPopCs)
            continue;

        inWindow = true;
        if (countdown == gJackLogCountdown)
            continue; // 同一个倒计时值只打一次
        gJackLogCountdown = countdown;

        const double forced = Pe96ThreatOverride(&zombie);
        aLogger->Info("[丑] x={} {}行 倒计时={} 威胁忧郁菇={} 覆盖值={} 樱桃可用={} 樱桃CD={} 阳光={}",
            static_cast<int>(zombie.Abscissa()), zombie.Row() + 1, countdown,
            JackExplosionHitsGloom(&zombie), static_cast<int>(forced), AIsSeedUsable(ACHERRY_BOMB),
            RealSeedCD(ACHERRY_BOMB), AGetMainObject()->Sun());

        // 威胁度缓存里这只小丑的记录（AshValueAt 用的就是这份）
        const AZombieThreatLevel* cached = FindThreat(&zombie);
        if (cached == nullptr) {
            aLogger->Info("    缓存里没有这只小丑");
        } else {
            aLogger->Info("    缓存: 威胁血量={} 种类={} 覆盖值={}", cached->threatHp,
                cached->typeFactor, static_cast<int>(cached->overrideValue));
        }

        // 窗口第一帧时，把场上其它小丑的状态也列出来 —— 还在开盒（状态 16、倒计时<=100）的
        // 别的小丑会把灰烬格子“炸掉”，AshValueAt 会直接返回 0，这些在只看窗口的日志里看不见。
        if (countdown == kJackPopCs) {
            for (const AZombieSnapshot& otherSnap : snap.zombies) {
                AZombie& other = *otherSnap.zombie;
                if (&other == &zombie || other.Type() != AJACK_IN_THE_BOX_ZOMBIE)
                    continue;
                aLogger->Info("    场上另一只小丑: x={} {}行 状态={} 倒计时={}", static_cast<int>(other.Abscissa()),
                    other.Row() + 1, other.State(), other.StateCountdown());
            }
        }
        if (forced <= 0.0)
            continue;

        // 覆盖值生效了，顺手列出当前哪些樱桃格能炸到它（含可种植检查）
        for (int row = 1; row <= aFieldInfo.nRows; ++row) {
            for (int col = 1; col <= 9; ++col) {
                if (!IsPlantable(ACHERRY_BOMB, row, col))
                    continue;
                for (const auto& hit : APredictInstantPlantHits(row, col, ACHERRY_BOMB))
                    if (hit.zombie == &zombie)
                        aLogger->Info("    樱桃 {}行{}列 能炸到它 AshValueAt={} 会被小丑炸={}", row, col,
                            static_cast<int>(AshValueAt(row, col, ACHERRY_BOMB)),
                            aThreat::AIsPlantJacked(aThreat::APlantHitBoxAt(row, col), hit.eta));
            }
        }
    }
    if (!inWindow)
        gJackLogCountdown = -1;
}

// ============================================================================
//  冰能不能影响这只僵尸（复刻 zombie::can_be_slowed / can_be_freezed 的要点）
// ============================================================================
bool CanIceSlow(AZombie* zombie) {
    if (zombie->Type() == AZOMBONI) // 冰车免疫减速
        return false;
    const int state = zombie->State();
    if (state >= aInstantPlant::STATE_DIGGER_DIG && state <= aInstantPlant::STATE_DIGGER_WALK_RIGHT)
        return false; // 钻地系列
    if (state == aInstantPlant::STATE_RISING_FROM_GROUND)
        return false;
    return !aInstantPlant::AIsDead(zombie);
}

bool CanIceFreeze(AZombie* zombie) {
    if (!CanIceSlow(zombie))
        return false;
    const int type = zombie->Type();
    if (type == ABALLOON_ZOMBIE || type == ABUNGEE_ZOMBIE)
        return false;
    const int state = zombie->State();
    if (state == aInstantPlant::STATE_BALLOON_FLYING || state == aInstantPlant::STATE_BALLOON_FALLING)
        return false;
    if (state == aInstantPlant::STATE_POLE_JUMPING || state == aInstantPlant::STATE_DOLPHIN_RIDE || state == aInstantPlant::STATE_DOLPHIN_JUMP || state == aInstantPlant::STATE_DOLPHIN_JUMP_IN_POOL || state == aInstantPlant::STATE_SNORKEL_JUMP_IN_POOL)
        return false;
    return true;
}

// ============================================================================
//  单格子力：樱桃 / 倭瓜
// ============================================================================

// 这张灰烬卡允不允许放在这一格（樱桃：1~5 列是梯子禁区，见 kCherryMinCol；倭瓜不限）
bool AshCellAllowed(APlantType type, int row, int col) {
    (void)row;
    return type != ACHERRY_BOMB || col >= kCherryMinCol;
}

// 这一行的格子放这张灰烬卡，有没有可能炸到“记了子力”的僵尸：樱桃打 ±1 行、倭瓜只打本行
// （行范围照库里的规则表 AGetRule 取）。没有的话该行每格的 AshValueAt 必为 0，整行可以直接跳过
bool AshRowMayScore(APlantType type, int row) {
    aInstantPlant::Rule rule;
    if (!aInstantPlant::AGetRule(type, false, rule))
        return false;
    const int range = rule.wholeRow ? 0 : rule.rowRange;
    for (int r = row - range; r <= row + range; ++r)
        if (r >= 1 && r <= 6 && ThreatCacheRowCount[r - 1] > 0)
            return true;
    return false;
}

double AshValueAt(int row, int col, APlantType type) {
    if (!AshCellAllowed(type, row, col))
        return 0.0; // 禁区：子力记 0（显示 / 日志 / 落子三处一致）

    // 复用容量：每格不再分配一次（本函数不会重入：里面只查表、不再调预测）
    static std::vector<AInstantPlantHit> hits;
    APredictInstantPlantHitsInto(row, col, type, hits);
    if (hits.empty())
        return 0.0;

    // 生效前会被小丑炸掉 → 这颗灰烬根本不会炸，子力 0
    if (aThreat::AIsPlantJacked(aThreat::APlantHitBoxAt(row, col), hits.front().eta))
        return 0.0;

    double total = 0.0;
    for (const auto& hit : hits) {
        const AZombieThreatLevel* info = FindThreat(hit.zombie);
        if (info == nullptr)
            continue;
        // 被脚本指定为“必炸目标”的（例如马上要炸到忧郁菇的开盒小丑）：直接给覆盖值。
        // 这一条要排在“威胁血量 <= 0”前面 —— 威胁血量是“走到阵前这一路被自然输出打掉多少”
        // 的长远估计，对几十帧内就会爆炸、根本走不到阵前的小丑没有意义。
        if (info->overrideValue > 0.0) {
            total += info->overrideValue; // 被脚本指定为必炸目标（无视距离 / 种类 / 速度）
            continue;
        }
        if (info->threatHp <= 0)
            continue; // 已经被别的东西锁死，打它是浪费
        total += AValueThreatDamage(*info, std::min(1800, info->threatHp));
    }
    return total;
}

// ============================================================================
//  冰的子力 = 地面项 + 气球项
//    地面项 = Σ 用冰后掉掉的威胁血量 × 距离系数 × 种类修正 × 速度系数（Ver.2 原样）
//    气球项 = Σ 满足条件的飞行气球的威胁值（按进家时间算，见下面“气球的冰子力”一节）
// ============================================================================
int FreezeCsFor(AZombie* zombie) {
    if (zombie->FreezeCountdown() > 0 || zombie->SlowCountdown() > 0)
        return kIceFreezeCsAlready;
    const bool inWater = aFieldInfo.hasPool && aFieldInfo.rowType[zombie->Row() + 1] == ARowType::POOL;
    return inWater ? kIceFreezeCsWater : kIceFreezeCsFresh;
}

// 地面项：只看威胁度快照里有记录的僵尸（气球不在里面，见 CountsForValue；冰对气球的价值在气球项里算）
double GroundIceValue() {
    const AThreatLevelOptions level = ValueOptions();
    double total = 0.0;

    const AFrameSnapshot& snap = AGetFrameSnapshot();
    for (const AZombieSnapshot& zombieSnap : snap.zombies) {
        AZombie* zombie = zombieSnap.zombie;
        const AZombieThreatLevel* info = FindThreat(zombie);
        if (info == nullptr)
            continue;
        if (info->threatHp <= 0)
            continue; // 已经被锁死的僵尸，用冰没意义

        const bool canSlow = CanIceSlow(zombie);
        const bool canFreeze = CanIceFreeze(zombie);
        if (!canSlow && !canFreeze)
            continue;

        AThreatQueryOptions after;
        after.includeSquash = level.includeSquash;
        after.includeNaturalOutput = level.includeNaturalOutput;
        after.naturalOptions = level.naturalOptions;
        if (canFreeze)
            after.naturalOptions.assumeFreezeCs = FreezeCsFor(zombie);
        if (canSlow)
            after.naturalOptions.assumeSlowCs = kIceSlowCs;

        int hpAfter = AGetZombieThreatHp(zombie, level.threatTime, after);
        if (canFreeze)
            hpAfter -= kIceDamage;
        hpAfter = std::max(0, hpAfter);

        const double reduce = static_cast<double>(info->threatHp - hpAfter);
        if (reduce <= 0.0)
            continue;
        // 冰期间打掉的血量（冻结 + 减速带来的自然输出增量 + 冰本身的 20 点）
        // 和灰烬走同一套折算：伤害 × 距离系数 × 种类修正 × 速度系数
        total += AValueThreatDamage(*info, reduce);
    }
    return total;
}

// ============================================================================
//  气球的冰子力（Ver.4）
//    飞行中的气球（状态 73）大喷菇 / 忧郁菇打不到、倭瓜砸不到、冰也冻不住（CanIceFreeze 为 false），
//    但会被冰减速 2000cs（速度 ×0.4），所以对气球来说冰的意义只有一个：把它进家的时间拖长。
//    于是气球的威胁值不按“离阵前多远”算，而按“还有多久进家”算：
//        气球威胁值 = 威胁血量 × kBalloonIceTypeFactor × e^(−进家时间 / kBalloonHomeTimeScaleCs)
//      · 进家时间越短威胁值越高，飞到家门口（进家时间 0）时 = 威胁血量 × kBalloonIceTypeFactor
//      · 只有冰看这份威胁值；樱桃 / 倭瓜 / 垫材一律忽略气球（它们用的威胁度快照里没有气球）
//      · 威胁血量 = 本体血量 − 即将到来的灰烬伤害（已经有樱桃要炸到它就是 0，不必再用冰）；
//        自然输出 / 倭瓜都打不到飞行气球，所以不扣
//      · 不乘速度系数：气球飞得快慢已经完全体现在进家时间里了
//
//    进家时间怎么算（参考 Ref/MGE 群曾 202609160000.cpp 的 BalloonΔX，反过来解时间）：
//        没减速           ：位移 = 原速 × 时间
//        减速倒计时 S > 0 ：前 S−1 帧速度 ×0.4，之后恢复原速（倒计时先减后走，所以是 S−1）
//      进家位置 = 本体坐标 Abscissa <= −100（反编译 update_entering_home，BalloonΔX 也是这么用的）
//
//    哪些气球计入冰的子力（四个条件都要满足）：
//      1. 正在飞（State() == 73）且没在减速（SlowCountdown() == 0）—— 已经被冰减速的不再重复算
//      2. 已经飞过本行的“阵前”（aThreat::ARowFront，带 Pe96RowFrontAllowed 的筛选：红眼关岸路
//         只认 6 列及更靠后的植物）：气球命中框左边 <= 阵前 x。该行连一颗阵前植物都没有时，
//         前面没有任何东西挡它，视为已经飞过
//      3. 冰来得及：咖啡豆下去后冰要 kIceActiveCs 帧才生效，这段时间气球还是原速在飞；
//         生效前就进家的气球用冰救不了，不算
//      4. 威胁血量 > 0
//    被冰减速之后进家时间变长，威胁值自然就低了（日志里把“冰后”的进家时间 / 威胁值一起打出来，
//    方便调参）；而且它这时处于减速状态，不再满足条件 1，冰不会对同一只气球连着出手。
// ============================================================================

// BalloonΔX（Ref/MGE 群曾 202609160000.cpp）：从现在起飞 cs 帧的位移（px）
//   没减速：原速 × 时间；减速倒计时 S > 0：前 S−1 帧 ×0.4，之后原速
//   （和库里 ADisplacement 的减速分支是同一个公式；飞行气球不会被冻 / 黄油，这里不用管那两项）
double BalloonFlightPx(int cs, double speed, int slowCs) {
    if (cs <= 0)
        return 0.0;
    if (slowCs <= 0)
        return speed * cs; // 原速 × 总时间
    if (slowCs > cs)
        return aThreat::ZOMBIE_SLOW_SPEED_FACTOR * speed * cs; // 全程都在减速
    // 减速 × (减速倒计时 − 1) + 原速 × (总时间 − (减速倒计时 − 1))
    return aThreat::ZOMBIE_SLOW_SPEED_FACTOR * speed * (slowCs - 1) + speed * (cs - (slowCs - 1));
}

// BalloonΔX 反过来解：还差 distancePx 要飞多少 cs
double BalloonFlightCs(double distancePx, double speed, int slowCs) {
    if (distancePx <= 0.0)
        return 0.0;
    if (slowCs <= 0)
        return distancePx / speed;
    const double slowedCs = slowCs - 1; // 减速段的帧数（同 BalloonΔX 的 S−1）
    const double slowedPx = aThreat::ZOMBIE_SLOW_SPEED_FACTOR * speed * slowedCs;
    if (distancePx <= slowedPx)
        return distancePx / (aThreat::ZOMBIE_SLOW_SPEED_FACTOR * speed); // 减速段里就到了
    return slowedCs + (distancePx - slowedPx) / speed;                   // 减速段飞完，剩下的按原速
}

// 进家时间 → 时间系数（相当于地面僵尸的距离系数）
double BalloonTimeFactor(double homeCs) {
    return std::exp(-std::max(0.0, homeCs) / kBalloonHomeTimeScaleCs);
}

// 一只气球的威胁值：进家时间 → 威胁值（越快进家越高，到家门口 = 威胁血量 × kBalloonIceTypeFactor）
double BalloonThreatAt(int threatHp, double homeCs) {
    if (threatHp <= 0)
        return 0.0;
    return static_cast<double>(threatHp) * kBalloonIceTypeFactor * BalloonTimeFactor(homeCs);
}

struct BalloonIceEntry {
    AZombie* zombie = nullptr;
    int row = 0;                 // 1 起
    int x = 0;                   // 本体坐标
    int slowCs = 0;              // 当前减速倒计时
    int threatHp = 0;            // 本体血量 − 即将到来的灰烬伤害
    bool pastFront = false;      // 已飞过本行阵前
    bool iceInTime = false;      // 冰生效前它还没进家
    bool counted = false;        // 四个条件都满足，计入冰子力
    double homeCsNow = 0.0;      // 现在起还有多久进家
    double homeCsAfterIce = 0.0; // 现在用冰的话还有多久进家
    double threatNow = 0.0;      // 现在的威胁值（计入冰子力的就是它）
    double threatAfterIce = 0.0; // 冰减速后的威胁值（只用于日志 / 调参，看冰把它拖慢了多少）
};

// 一只飞行气球的明细：进家时间 / 威胁值 / 冰的各项条件
BalloonIceEntry ComputeBalloonEntry(const AZombieSnapshot& snap) {
    const AThreatLevelOptions level = ValueOptions();
    const int homeX = aThreat::AZombieHomeX(ABALLOON_ZOMBIE); // −100
    AThreatQueryOptions query;
    query.includeSquash = false;        // 倭瓜砸不到飞行气球
    query.includeNaturalOutput = false; // 大喷菇 / 忧郁菇也打不到
    AZombie* zombie = snap.zombie;

    BalloonIceEntry entry;
    entry.zombie = zombie;
    entry.row = snap.row + 1;
    entry.x = static_cast<int>(zombie->Abscissa());
    entry.slowCs = std::max(0, zombie->SlowCountdown());
    entry.threatHp = AGetZombieThreatHp(zombie, level.threatTime, query);

    // 进家：气球按本体坐标 <= −100 判（和 BalloonΔX 的用法一致）
    const double speed = std::max(0.05f, std::fabs(zombie->Speed())); // 原速（0x34 mVelX）
    const double distancePx = zombie->Abscissa() - homeX;             // 还差多少 px 进家
    entry.homeCsNow = BalloonFlightCs(distancePx, speed, entry.slowCs);

    // 现在用冰：咖啡豆下去后 kIceActiveCs 帧才生效，这段按当前状态飞；
    // 生效那一刻减速倒计时变成 max(剩余减速, 2000)（游戏 ApplyChill 取大）
    const double flownBeforeIce = BalloonFlightPx(kIceActiveCs, speed, entry.slowCs);
    const double leftAtIce = distancePx - flownBeforeIce;
    entry.iceInTime = leftAtIce > 0.0;
    if (entry.iceInTime) {
        const int slowAtIce = std::max(entry.slowCs - kIceActiveCs, kIceSlowCs);
        entry.homeCsAfterIce = kIceActiveCs + BalloonFlightCs(leftAtIce, speed, slowAtIce);
    } else {
        entry.homeCsAfterIce = entry.homeCsNow; // 冰生效前就进家了，冰对它没有任何影响
    }

    // 阵前：库里的 ARowFront（已套用 Pe96RowFrontAllowed）；命中框左边 <= 阵前 x 即算飞过
    const aThreat::ARowFrontInfo front = aThreat::ARowFront(entry.row);
    entry.pastFront = !front.found || aInstantPlant::AGetZombieHitBox(zombie).x <= front.x;

    entry.threatNow = BalloonThreatAt(entry.threatHp, entry.homeCsNow);
    entry.threatAfterIce = BalloonThreatAt(entry.threatHp, entry.homeCsAfterIce);
    entry.counted = entry.slowCs == 0 && entry.pastFront && entry.iceInTime && entry.threatHp > 0;
    return entry;
}

// 冰的气球项：把场上每只飞行气球算一遍，返回计入的威胁值总和；entries 不为空时把明细也写出来
double BalloonIceValue(std::vector<BalloonIceEntry>* entries = nullptr) {
    double total = 0.0;
    for (const AZombieSnapshot& snap : AGetFrameSnapshot().zombies) {
        if (!IsFlyingBalloon(snap))
            continue; // 坠落 / 落地后的气球僵尸是地面僵尸，不归这里
        const BalloonIceEntry entry = ComputeBalloonEntry(snap);
        if (entry.counted)
            total += entry.threatNow;
        if (entries != nullptr)
            entries->push_back(entry);
    }
    return total;
}

// （冰的总子力 IceValue 定义在“冰的间隔修正”一节之后，因为还要乘间隔系数）

// ============================================================================
//  落子：在“子力最高的操作位”执行
// ============================================================================
void LogUse(APlantType type, int row, int col, double value, double threshold) {
    if (!kDebugValueLog)
        return;
    aLogger->Info("[子力] {} @{}行{}列 子力={} 阈值={}", type, row, col, static_cast<int>(value + 0.5),
        static_cast<int>(threshold + 0.5));
}

// 这个格子现在能不能种下这张卡。AvZ 的 GetPlantRejectType 就是游戏的 Board::CanPlantAt
// （0x40E020），冰道（IsIceAt）、墓碑、水缺荷叶、已有植物等都在那里被拒。
// 比较一个操作的不同位置之前，先用它把“实际上放不下”的格子过滤掉。
bool IsPlantable(APlantType type, int row, int col) {
    return AAsm::GetPlantRejectType(type, row - 1, col - 1) == AAsm::NIL;
}

// 樱桃 / 倭瓜：全格扫描
//   子力只跟僵尸有关，IsPlantable 是游戏函数调用；两个条件都要满足才更新“最优格”，
//   所以先算子力、只对能刷新最优的格子再问游戏能不能种 —— 选出的格子与原来逐格先问
//   再算完全相同，只是少调了几十次游戏函数。
void TryUseAshCard(APlantType type, double threshold) {
    if (!AIsSeedUsable(type))
        return;
    // 威胁度快照里一条记录都没有（场上没有任何“记子力”的僵尸）：每格的 AshValueAt 都是 0，
    // 不可能刷新 best，也就不会落子 —— 整个 54 格扫描直接省掉
    if (ThreatCacheCount == 0)
        return;

    double best = 0.0;
    int bestRow = 0;
    int bestCol = 0;
    for (int row = 1; row <= aFieldInfo.nRows; ++row) {
        if (!AshRowMayScore(type, row))
            continue; // 这一行炸不到任何记了子力的僵尸：每格必为 0，整行跳过
        for (int col = 1; col <= 9; ++col) {
            if (!AshCellAllowed(type, row, col))
                continue; // 禁区（樱桃 1~5 列）：子力必为 0，连预测都省掉
            const double value = AshValueAt(row, col, type);
            if (value > best && IsPlantable(type, row, col)) {
                best = value;
                bestRow = row;
                bestCol = col;
            }
        }
    }
    if (bestRow == 0 || best < threshold)
        return;
    LogUse(type, bestRow, bestCol, best, threshold);
    ACard(type, bestRow, bestCol);
    AInvalidateFrameCache(); // 场上多了一颗植物，帧内缓存作废
}

// 小喷菇 / 大喷菇：垫材子力
//   两张卡的垫材子力逐格完全相同（血量都是 300、都不会自行消失、都能被车压、能不能种的规则也一样），
//   所以一帧只扫一遍：先按小喷菇的阈值决定；小喷菇没落子的话这份结果原样给大喷菇用，
//   只有小喷菇真的种下去了（场面变了）才为大喷菇再扫一遍。落子结果与原来“两张卡各扫一遍”完全相同。
struct FodderScan {
    double best = 0.0;
    int row = 0;
    int col = 0;
};

FodderScan ScanFodderCells(APlantType type) {
    AFodderOptions fodderOptions;
    fodderOptions.levelOptions = ValueOptions();
    static AFodderValue info; // 复用容量：每格不再为 zombies / endReason 分配一次

    FodderScan scan;
    for (int row = 1; row <= aFieldInfo.nRows; ++row) {
        for (int col = 1; col <= 9; ++col) {
            // 这一格没有僵尸贴着：垫材子力必为 0、AGetFodderValue 也没有副作用，直接跳过
            // （省掉一次游戏函数 GetPlantRejectType 和一次 AGetFodderValue）
            if (!AFodderCellHasZombie(row, col))
                continue;
            if (!IsPlantable(type, row, col))
                continue;
            AGetFodderValueInto(info, type, row, col, fodderOptions);
            if (info.valid && info.value > scan.best) {
                scan.best = info.value;
                scan.row = row;
                scan.col = col;
            }
        }
    }
    return scan;
}

// 按扫描结果落子；返回有没有种下
bool UseFodderCard(APlantType type, double threshold, const FodderScan& scan) {
    if (scan.row == 0 || scan.best < threshold)
        return false;
    LogUse(type, scan.row, scan.col, scan.best, threshold);
    ACard(type, scan.row, scan.col);
    AInvalidateFrameCache(); // 场上多了一颗植物，帧内缓存作废
    return true;
}

void TryUseFodderCards() {
    const bool puffUsable = AIsSeedUsable(APUFF_SHROOM);
    if (!puffUsable && !AIsSeedUsable(AFUME_SHROOM))
        return;

    FodderScan scan = ScanFodderCells(puffUsable ? APUFF_SHROOM : AFUME_SHROOM);
    bool puffPlanted = false;
    if (puffUsable)
        puffPlanted = UseFodderCard(APUFF_SHROOM, kPuffThreshold, scan);
    // 大喷菇要在小喷菇之后再判一次能不能用（小喷菇落子花了阳光，大喷菇可能就不够了 —— 原来的顺序也是这样）
    if (!AIsSeedUsable(AFUME_SHROOM))
        return;
    if (puffPlanted)
        scan = ScanFodderCells(AFUME_SHROOM); // 场面变了，重扫
    UseFodderCard(AFUME_SHROOM, kFumeThreshold, scan);
}

// 冰：全场子力，没有“位置”
bool IceWaiting() {
    for (const APlantSnapshot& plant : AGetFrameSnapshot().plants) {
        if ((plant.type == AICE_SHROOM || plant.type == AM_ICE_SHROOM) && plant.plant->ExplodeCountdown() > 0)
            return true; // 已经有冰在等生效
    }
    return false;
}

// 存冰位上现在有几颗冰（寒冰菇 / 模仿寒冰菇）
int StoredIceCount() {
    const AFrameSnapshot& snap = AGetFrameSnapshot();
    int count = 0;
    for (const AGrid& grid : aIceFiller.GetList()) {
        if (aInstantPlant::AFindPlantPtr(snap, grid.row, grid.col, AICE_SHROOM) || aInstantPlant::AFindPlantPtr(snap, grid.row, grid.col, AM_ICE_SHROOM))
            ++count;
    }
    return count;
}

bool HasStoredIce() {
    return StoredIceCount() > 0;
}

// ============================================================================
//  冰的间隔修正（Ver.4）
//    冰的减速持续 kIceSlowCs = 2000cs。两颗冰生效间隔太短，后一颗的减速大半和前一颗重叠，白白浪费。
//    修正项：间隔系数 = min(1, (距上一颗冰生效的时间 + kIceActiveCs) / kIceMinIntervalCs)
//            冰子力 = (地面项 + 气球项) × 间隔系数
//      （“+ kIceActiveCs”是因为现在放咖啡豆，冰要 299cs 后才生效，算间隔要按两颗冰的生效时刻算）
//    上一颗冰的生效时刻怎么知道（gLastIceEffectClock，GameClock 计）：
//      · 脚本自己用冰时记下预计生效时刻（现在 + kIceActiveCs）；
//      · 另外每帧看有没有僵尸的减速倒计时正好是 2000（差一帧则 1999）—— 游戏里只有寒冰菇给 2000
//        （ApplyChill(true)），冰瓜 / 寒冰射手给的是 1000 —— 看到就说明上一帧有冰生效。这样存冰被巨人
//        砸引爆之类脚本没主动做的生效也算得到。
//    冰不缺的时候不必省着用（kIceIntervalRelaxWhenNextReady）：这颗用掉以后，下一颗冰若能在这颗的减速
//    结束前生效 —— 存冰位里还有第二颗；或两张冰卡里先冷却完的那张（RealSeedCD 读剩余冷却，aIceFiller
//    会在卡一好时把冰补进存冰位）加上咖啡豆的冷却，不超过 kIceMinIntervalCs —— 间隔系数按 1 算。
// ============================================================================
constexpr int kNoIceEffectClock = INT_MIN / 2; // “还没有冰生效过”
int gLastIceEffectClock = kNoIceEffectClock;   // 上一颗冰的生效时刻（GameClock）

// 每帧调用：探测“上一帧有冰生效”（有僵尸的减速倒计时 = 2000 / 1999）
void TrackIceEffect() {
    AMainObject* main = AGetMainObject();
    if (main == nullptr)
        return;
    for (const AZombieSnapshot& zombie : AGetFrameSnapshot().zombies) {
        if (zombie.zombie->SlowCountdown() >= kIceSlowCs - 1) {
            gLastIceEffectClock = main->GameClock();
            return;
        }
    }
}

// 这颗存冰用掉以后，下一颗冰的生效时刻最快能比这颗晚多少 cs（INT_MAX = 不会再有冰）
//   存冰位里还有第二颗 → 只等咖啡豆冷却；否则等两张冰卡里先冷却完的那张，再等咖啡豆。
//   两颗冰各自的生效延迟 kIceActiveCs 相同，相减抵消，这里不用加
int NextIceGapCs() {
    int iceReadyCs = 0;
    if (StoredIceCount() < 2) {
        iceReadyCs = INT_MAX;
        for (APlantType type : {AICE_SHROOM, AM_ICE_SHROOM}) {
            const int cd = RealSeedCD(type);
            if (cd >= 0)
                iceReadyCs = std::min(iceReadyCs, cd);
        }
        if (iceReadyCs == INT_MAX)
            return INT_MAX; // 两张冰卡都不在卡槽里
    }
    return std::max(iceReadyCs, kCoffeeBeanCdCs);
}

struct IceIntervalInfo {
    bool hasLast = false;    // 这局有没有冰生效过
    int sinceLastCs = 0;     // 距上一颗冰生效过了多久
    int nextGapCs = INT_MAX; // 下一颗冰的生效最快能比这颗晚多少
    bool relaxed = false;    // 下一颗接得上，不打折
    double factor = 1.0;     // 间隔系数
};

IceIntervalInfo ComputeIceInterval() {
    IceIntervalInfo info;
    AMainObject* main = AGetMainObject();
    if (main == nullptr)
        return info;
    info.nextGapCs = NextIceGapCs();
    info.hasLast = gLastIceEffectClock != kNoIceEffectClock;
    if (!info.hasLast)
        return info; // 这局还没用过冰：不打折
    info.sinceLastCs = main->GameClock() - gLastIceEffectClock;
    // 这颗冰生效时（kIceActiveCs 后）距上一颗冰生效多久
    const double gapCs = static_cast<double>(info.sinceLastCs) + kIceActiveCs;
    info.factor = std::clamp(gapCs / kIceMinIntervalCs, 0.0, 1.0);
    info.relaxed = kIceIntervalRelaxWhenNextReady && info.nextGapCs <= kIceMinIntervalCs;
    if (info.relaxed)
        info.factor = 1.0;
    return info;
}

// 冰的总子力：(地面项 + 气球项) × 间隔系数（落子判断与子力显示共用）
double IceValue() {
    return (GroundIceValue() + BalloonIceValue()) * ComputeIceInterval().factor;
}

// 冰这一帧能不能放：没有冰在等生效、存冰位上有冰、咖啡豆可用
// （ThreatCacheNeeded 与 TryUseIce 都要用，LogicBody 一帧算一次传给两边）
bool IceReady() {
    return !IceWaiting() && HasStoredIce() && AIsSeedUsable(ACOFFEE_BEAN);
}

void TryUseIce(bool iceReady) {
    if (!iceReady)
        return;

    // (地面项 + 气球项) × 间隔系数（= IceValue()，这里分开算是为了把各部分和每只气球的明细写进日志）
    const double groundValue = GroundIceValue();
    std::vector<BalloonIceEntry> balloons;
    const double balloonValue = BalloonIceValue(kDebugValueLog ? &balloons : nullptr);
    const IceIntervalInfo interval = ComputeIceInterval();
    const double value = (groundValue + balloonValue) * interval.factor;
    if (value < kIceThreshold)
        return;
    if (kDebugValueLog) {
        aLogger->Info("[子力] 用冰 子力={} = (地面 {} + 气球 {}) × 间隔系数 {:.2f}（上一颗冰 {}cs 前生效，下一颗最快晚 {}cs{}） 阈值={}",
            static_cast<int>(value + 0.5), static_cast<int>(groundValue + 0.5), static_cast<int>(balloonValue + 0.5),
            interval.factor, interval.hasLast ? interval.sinceLastCs : -1,
            interval.nextGapCs == INT_MAX ? -1 : interval.nextGapCs, interval.relaxed ? "，接得上不打折" : "",
            static_cast<int>(kIceThreshold + 0.5));
        for (const BalloonIceEntry& b : balloons)
            aLogger->Info("    气球 {}行 x={} 减速={} 威胁血量={} 进家 {}cs→冰后 {}cs 威胁值 {}→{} 过阵前={} 来得及={} 计入={}",
                b.row, b.x, b.slowCs, b.threatHp, static_cast<int>(b.homeCsNow + 0.5),
                static_cast<int>(b.homeCsAfterIce + 0.5), static_cast<int>(b.threatNow + 0.5),
                static_cast<int>(b.threatAfterIce + 0.5), b.pastFront, b.iceInTime, b.counted);
    }
    // 主动释放：在存冰位（kIceStoreGrids 的第一个）放咖啡豆唤醒存冰
    if (!kIceStoreGrids.empty())
        aIceFiller.Coffee(kIceStoreGrids.front().row, kIceStoreGrids.front().col);
    AInvalidateFrameCache();                        // 放了咖啡豆，帧内缓存作废
    ASetPlantActiveTime(AICE_SHROOM, kIceActiveCs); // 白天冰的生效时间
    // 间隔修正：记下这颗冰的预计生效时刻（真正生效那一帧 TrackIceEffect 还会再对一次）
    gLastIceEffectClock = AGetMainObject()->GameClock() + kIceActiveCs;
}

// ============================================================================
//  “原阵要补的大喷菇”位置（1 起）
//   只有这些格子上的大喷菇才点咖啡豆唤醒；子力逻辑摆到前面的垫材大喷菇不点
//   —— 原脚本是“按行”唤醒（0/1/4/5 行所有大喷）的，加了垫材之后会把垫材喷也点上
// ============================================================================
const std::vector<AGrid> kFormationFumeGrids = {
    {1, 4}, {6, 4}, {1, 5}, {6, 5}, {2, 7}, {5, 7}, // 补大喷菇的那几格
    {2, 5}, {5, 5}, {2, 6}, {5, 6},                 // 补曾时垫在曾下面的大喷（1/2/5/6 行）
};

bool IsFormationFumeGrid(int row, int col) {
    for (const AGrid& grid : kFormationFumeGrids)
        if (grid.row == row && grid.col == col)
            return true;
    return false;
}

// ============================================================================
//  每帧耗时统计（kShowPerfStats）
// ============================================================================
struct PerfAverage {
    double sum = 0.0;
    int count = 0;
    double averageUs = 0.0; // 最近 100 次的平均，微秒

    void Add(double seconds) {
        sum += seconds;
        if (++count >= 100) {
            averageUs = sum / count * 1e6;
            sum = 0.0;
            count = 0;
        }
    }
};

PerfAverage gLogicPerf;
PerfAverage gPaintPerf;

// ============================================================================
//  子力显示 UI
//    数值与落子判断用的是同一套函数、同一份威胁度快照（ThreatCache）
// ============================================================================
constexpr int CELL_FONT_SIZE = 13;
constexpr int CELL_LINE_HEIGHT = 18;
constexpr int CELL_TEXT_WIDTH = 4; // 粗估字宽，用于在格子里居中

// 单独一个 painter：不同字号共用一个 painter 会让字形纹理每帧重建（重影 + 卡顿）
APainter ValuePainter;
ATickRunner ValueOverlay;

std::string FormatCellValue(double value) {
    if (!(value > 0.0))
        return "0";
    if (value >= 10.0)
        return std::to_string(static_cast<int>(value + 0.5));
    const int scaled = static_cast<int>(value * 10.0 + 0.5); // 保留一位小数
    return std::to_string(scaled / 10) + "." + std::to_string(scaled % 10);
}

bool IsIceStoreGrid(int row, int col) {
    for (const AGrid& grid : kIceStoreGrids)
        if (grid.row == row && grid.col == col)
            return true;
    return false;
}

void DrawValueCells() {
    if (AGetMainObject() == nullptr || aFieldInfo.nRows <= 0)
        return;
    // 绘制发生在本帧游戏逻辑更新之后，僵尸位置已经变了：先让帧内缓存按当前状态重建
    AInvalidateFrameCache();
    if (ThreatCacheCount == 0) // 万一这帧还没轮到 Logic 刷新
        RefreshThreatCache();

    // 冰是全场一个数，只算一次
    const double iceValue = IceValue();
    AFodderOptions fodderOptions;
    fodderOptions.levelOptions = ValueOptions();

    const DWORD colorIce = AArgb(0xff, 0x00, 0x00, 0x8b);    // 冰：深蓝（浅蓝在草地上看不清）
    const DWORD colorCherry = AArgb(0xff, 0xff, 0x00, 0x00); // 樱桃：红
    const DWORD colorSquash = AArgb(0xff, 0xff, 0xff, 0xff); // 倭瓜：白
    const DWORD colorPuff = AArgb(0xff, 0xbf, 0x40, 0xff);   // 小喷菇 / 大喷菇：紫

    ValuePainter.SetFontSize(CELL_FONT_SIZE);
    const int blockHeight = CELL_FONT_SIZE + CELL_LINE_HEIGHT * 3;
    const aInstantPlant::ACellPixelTable& cellTable = aInstantPlant::AGetCellPixelTable(); // 格子像素查表

    for (int row = 1; row <= aFieldInfo.nRows; ++row) {
        for (int col = 1; col <= 9; ++col) {
            const bool showIce = IsIceStoreGrid(row, col);
            // 放不下去的格子（冰道、已有植物、水缺荷叶…）子力记 0，不参与比较
            const double values[4] = {
                iceValue,
                IsPlantable(ACHERRY_BOMB, row, col) ? AshValueAt(row, col, ACHERRY_BOMB) : 0.0,
                IsPlantable(ASQUASH, row, col) ? AshValueAt(row, col, ASQUASH) : 0.0,
                IsPlantable(APUFF_SHROOM, row, col)
                    ? AGetFodderValue(APUFF_SHROOM, row, col, fodderOptions).value
                    : 0.0,
            };
            const DWORD colors[4] = {colorIce, colorCherry, colorSquash, colorPuff};

            const int cellX = cellTable.cellX[row - 1][col - 1]; // = AAsm::GridToAbscissa(row - 1, col - 1)
            const int cellY = cellTable.cellY[row - 1][col - 1]; // = AAsm::GridToOrdinate(row - 1, col - 1)
            int y = cellY + (aFieldInfo.rowHeight - blockHeight) / 2 + CELL_FONT_SIZE;

            for (int i = 0; i < 4; ++i) {
                if (i == 0 && !showIce) { // 冰子力只画在存冰位
                    y += CELL_LINE_HEIGHT;
                    continue;
                }
                const std::string text = FormatCellValue(values[i]);
                // RIGHT_TOP 表示 (x, y) 是文本左下角
                const int x = cellX + 40 - static_cast<int>(text.size()) * CELL_TEXT_WIDTH;
                ValuePainter.SetTextColor(colors[i]);
                // duration = 1：PAINT 模式下立即画一次、不进队列（不进队列才不会有重影）
                ValuePainter.Draw(AText(text, x, y, APos::RIGHT_TOP, false), 1);
                y += CELL_LINE_HEIGHT;
            }
        }
    }
}

void DrawPerfStats() {
    if (AGetMainObject() == nullptr)
        return;
    ValuePainter.SetFontSize(CELL_FONT_SIZE);
    ValuePainter.SetTextColor(AArgb(0xff, 0xff, 0xff, 0xff));
    const std::string text = "L " + std::to_string(static_cast<int>(gLogicPerf.averageUs + 0.5)) + "us P " + std::to_string(static_cast<int>(gPaintPerf.averageUs + 0.5)) + "us";
    // 底部中间（ShowWavelength 的波长条右边、旗帜进度条左边的空白处）；RIGHT_TOP 表示 (x, y) 是文本左下角
    ValuePainter.Draw(AText(text, 410, 590, APos::RIGHT_TOP, false), 1);
}

void DrawValueOverlay() {
    if (kShowValueOverlay) {
        const double begin = kShowPerfStats ? __AProfiler::CurrentTime() : 0.0;
        DrawValueCells();
        if (kShowPerfStats)
            gPaintPerf.Add(__AProfiler::CurrentTime() - begin);
    }
    if (kShowPerfStats)
        DrawPerfStats();
}

} // namespace

// ============================================================================
//  以下沿用原 PE96 的补阵逻辑
// ============================================================================
ATickRunner T1;

AOnAfterInject({
    *(uint8_t*)0x486B0A = 0xEB;
    // 简化部分非战斗逻辑，加速游戏运行（实验性，建议配合跳帧使用）
    *(std::array<uint8_t, 3>*)0x40EF00 = {0xC2, 0x04, 0x00};
    *(std::array<uint8_t, 1>*)0x455930 = {0xC3};
    *(std::array<uint8_t, 1>*)0x45B260 = {0xC3};
    *(std::array<uint8_t, 2>*)0x461123 = {0xEB, 0x6D};
    *(std::array<uint8_t, 3>*)0x4635C0 = {0xC2, 0x04, 0x00};
    *(std::array<uint8_t, 3>*)0x465040 = {0xD9, 0xEE, 0xC3};
    *(std::array<uint8_t, 3>*)0x471DCF = {0xEB, 0x24, 0x90};
    *(std::array<uint8_t, 3>*)0x515020 = {0xC2, 0x04, 0x00};
    *(std::array<uint8_t, 1>*)0x5336D1 = {0xEB};
    *(std::array<uint8_t, 2>*)0x517670 = {0x90, 0x90};
    *(std::array<uint8_t, 2>*)0x51767B = {0x90, 0x90};
    *(std::array<uint8_t, 1>*)0x6A66F4 = {0x00};
});

// ============================================================================
//  补阵用的计数 / 试种
//    接口照 Ref/MGE 群曾 202609160000.cpp 的 PlantCnt / ZombieCnt / TryCard：参数都是列表，
//    {} 表示不限，可以一次传多个类型 / 多个格子 / 多行。实现在本帧快照上查：
//      · PlantCnt 指定了格子时只看这些格子的植物表（cellPlantBegin / cellPlantEntries）；
//      · ZombieCnt 指定了行时只看这些行的僵尸表（rowZombies）；
//      · 类型 / 状态 / 行直接用快照里的字段（同一帧同一份内存），血量 / 横坐标 / 波数才读内存；
//      不指定格子 / 行才全表扫描。判断条件与 MGE 原版逐句相同，只是省掉了每次调用的全表扫描。
//    和 Ver.2 的 Check_Plant / Check_Zombie / Use_Card 相比：
//      · 参数从“单个值、0 / -1 表示不限”改成列表；
//      · 僵尸血量口径改成 MGE 的“本体 + 一类 + 二类饰品 ∈ [min, max]”（旧版是本体 >= HP）。
//        本阵型只在橄榄那条用到 {90}：橄榄没有二类饰品、头盔没掉之前本体不掉血，
//        所以两种口径的判断结果相同；
//      · TryCard 种下后一样让帧内缓存作废（Use_Card 原来就这么做），另外多了一个返回值。
// ============================================================================

// 列表为空 = 不限；否则值必须在列表里
bool ListHas(std::initializer_list<int> list, int value) {
    if (list.size() == 0)
        return true;
    for (int item : list)
        if (item == value)
            return true;
    return false;
}

// 数植物：Types = 植物类型列表，Grids = 格子列表（1 起），HpRange = {最低血量[, 最高血量]}；{} = 不限
int PlantCnt(std::initializer_list<int> Types = {}, std::initializer_list<AGrid> Grids = {}, std::initializer_list<int> HpRange = {}) {
    const int* pHp = HpRange.begin();
    const int minHp = HpRange.size() > 0 ? pHp[0] : 0;
    const int maxHp = HpRange.size() > 1 ? pHp[1] : INT_MAX;
    const AFrameSnapshot& snap = AGetFrameSnapshot();

    auto match = [&](const APlantSnapshot& plant) {
        if (!ListHas(Types, plant.type))
            return false;
        const int hp = plant.plant->Hp();
        return minHp <= hp && hp <= maxHp;
    };

    int result = 0;
    if (Grids.size() > 0 && !snap.cellPlantBegin.empty()) {
        // 指定了格子：只数这些格子上的植物。一颗植物只属于一格，逐格累加不会重复；
        // 列表里重复写的格子只数一次（和 MGE 逐颗植物判“在不在列表里”的结果一致）
        for (const AGrid* it = Grids.begin(); it != Grids.end(); ++it) {
            const AGrid& grid = *it;
            bool duplicated = false;
            for (const AGrid* prev = Grids.begin(); prev != it; ++prev)
                if (*prev == grid)
                    duplicated = true;
            if (duplicated || grid.row < 1 || grid.row > 6 || grid.col < 1 || grid.col > 9)
                continue; // 场外的格子上不会有植物
            const int cell = (grid.row - 1) * 9 + (grid.col - 1);
            for (int i = snap.cellPlantBegin[cell]; i < snap.cellPlantBegin[cell + 1]; ++i)
                if (match(snap.plants[snap.cellPlantEntries[i]]))
                    ++result;
        }
        return result;
    }
    // 没指定格子（或快照还没建过）：全表扫描
    for (const APlantSnapshot& plant : snap.plants) {
        if (!match(plant))
            continue;
        if (Grids.size() > 0 && std::find(Grids.begin(), Grids.end(), AGrid(plant.row + 1, plant.col + 1)) == Grids.end())
            continue;
        ++result;
    }
    return result;
}

// 数僵尸：Types / States / Rows（1 起）/ Waves（1 起）都是列表；
//   AbscissaRange = {最小 x[, 最大 x]}（默认 -1000 ~ 1000），HpRange = {最低[, 最高]}，
//   血量口径 = 本体 + 一类饰品 + 二类饰品（同 MGE）
int ZombieCnt(std::initializer_list<int> Types = {}, std::initializer_list<int> States = {}, std::initializer_list<int> Rows = {}, std::initializer_list<int> AbscissaRange = {}, std::initializer_list<int> HpRange = {}, std::initializer_list<int> Waves = {}) {
    const int* pX = AbscissaRange.begin();
    const int minX = AbscissaRange.size() > 0 ? pX[0] : -1000;
    const int maxX = AbscissaRange.size() > 1 ? pX[1] : 1000;
    const int* pHp = HpRange.begin();
    const int minHp = HpRange.size() > 0 ? pHp[0] : 0;
    const int maxHp = HpRange.size() > 1 ? pHp[1] : INT_MAX;
    const AFrameSnapshot& snap = AGetFrameSnapshot();

    auto match = [&](const AZombieSnapshot& zombieSnap) {
        if (!ListHas(Types, zombieSnap.type) || !ListHas(States, zombieSnap.state))
            return false;
        AZombie& zombie = *zombieSnap.zombie;
        const float x = zombie.Abscissa();
        if (!(minX <= x && x <= maxX))
            return false;
        const int totalHp = zombie.Hp() + zombie.OneHp() + zombie.TwoHp();
        if (!(minHp <= totalHp && totalHp <= maxHp))
            return false;
        return ListHas(Waves, zombie.AtWave() + 1);
    };

    int result = 0;
    if (Rows.size() > 0) {
        // 指定了行：只看这些行的僵尸表。一只僵尸只在一行里，逐行累加不会重复；重复写的行只数一次
        for (const int* it = Rows.begin(); it != Rows.end(); ++it) {
            const int row = *it;
            bool duplicated = false;
            for (const int* prev = Rows.begin(); prev != it; ++prev)
                if (*prev == row)
                    duplicated = true;
            if (duplicated || row < 1 || row > 6)
                continue; // 不存在的行里没有僵尸
            for (int slot : snap.rowZombies[row - 1])
                if (match(snap.zombies[slot]))
                    ++result;
        }
        return result;
    }
    for (const AZombieSnapshot& zombieSnap : snap.zombies)
        if (match(zombieSnap))
            ++result;
    return result;
}

// 试种：卡可用且该格能种才种；种下后帧内缓存作废。返回有没有种下
bool TryCard(APlantType Type, int Row, int Col) {
    if (AIsSeedUsable(Type) && AAsm::GetPlantRejectType(Type, Row - 1, Col - 1) == AAsm::NIL) {
        ACard(Type, Row, Col);
        AInvalidateFrameCache(); // 场上多了一颗植物，帧内缓存作废
        return true;
    }
    return false;
}

// 多重试种（照 MGE）：每种卡在格子列表里挑第一个能种的位置种下，返回种下的植物
//   Types / Grids 直接写花括号列表（不会为参数分配内存），也可以传 std::vector
template <class TypeList, class GridList>
std::vector<APlant*> TryCardList(const TypeList& Types, const GridList& Grids) {
    std::vector<APlant*> ret;
    for (APlantType Type : Types) {
        if (!AIsSeedUsable(Type))
            continue;
        for (const AGrid& Grid : Grids) {
            if (AAsm::GetPlantRejectType(Type, Grid.row - 1, Grid.col - 1) == AAsm::NIL) {
                APlant* plantPtr = ACard(Type, Grid.row, Grid.col);
                AInvalidateFrameCache(); // 场上多了一颗植物，帧内缓存作废
                if (plantPtr != nullptr)
                    ret.push_back(plantPtr);
                break;
            }
        }
    }
    return ret;
}

std::vector<APlant*> TryCard(std::initializer_list<APlantType> Types, std::initializer_list<AGrid> Grids) {
    return TryCardList(Types, Grids);
}

std::vector<APlant*> TryCard(const std::vector<APlantType>& Types, const std::vector<AGrid>& Grids) {
    return TryCardList(Types, Grids);
}

// 与 AvZ 的 AGetPlantIndex(row, col, type) 同义（在本帧快照上查）
int PlantIndexAt(int Row, int Col, int Type = -1) {
    return aInstantPlant::AFindPlantIndex(AGetFrameSnapshot(), Row, Col, Type);
}

// 这一格附近有没有会把刚种下的植物弄掉的僵尸（红眼 / 白眼快砸到、冰车快压到、橄榄快撞到）
//   红眼 / 白眼的横坐标条件相同，合成一次 ZombieCnt（“两个都没有”和“合计为 0”是一回事）
bool FixCellSafe(int Row, int Col) {
    return !ZombieCnt({AGIGA_GARGANTUAR, AGARGANTUAR}, {}, {Row}, {-1000, Col * 80 + 40 + 1 + 10}) && !ZombieCnt({AZOMBONI}, {}, {Row}, {-1000, Col * 80 + 1 + 10}) && !ZombieCnt({AFOOTBALL_ZOMBIE}, {}, {Row}, {-1000, Col * 80 - 40 + 1 + 10}, {90});
}

// 补曾 / 补瓜共用的骨架：荷叶（3 / 4 行）→ 底座（大喷菇 / 西瓜）→ 目标（曾 / 冰瓜），
// 每种只在“卡能用、该格还没有”时种。
//   先看三张卡有没有任何一张能用：都在 cd 时（补瓜的两张 30s 卡绝大多数帧都是这样），
//   后面的僵尸计数 / 植物计数一律不必做。卡能不能用、有没有僵尸、格上有没有植物都是纯判断，
//   这里只是把最便宜的放最前面，结果与原来逐条判断相同（TryCard 落子前还会再读一次卡能不能用，
//   所以前面种荷叶花掉阳光让后面那张卡变得不能用时，也和原来一样不会种）。
void FixCell(int Row, int Col, APlantType baseType, APlantType targetType) {
    const bool lilyUsable = (Row == 3 || Row == 4) && AIsSeedUsable(ALILY_PAD);
    const bool baseUsable = AIsSeedUsable(baseType);
    const bool targetUsable = AIsSeedUsable(targetType);
    if (!lilyUsable && !baseUsable && !targetUsable)
        return; // 三张卡都用不了，这一格什么都做不了
    if (!FixCellSafe(Row, Col))
        return;
    if (lilyUsable && !PlantCnt({ALILY_PAD}, {{Row, Col}}))
        TryCard(ALILY_PAD, Row, Col);
    if (baseUsable && !PlantCnt({baseType}, {{Row, Col}}))
        TryCard(baseType, Row, Col);
    if (targetUsable && !PlantCnt({targetType}, {{Row, Col}}))
        TryCard(targetType, Row, Col);
}

// 补曾（忧郁菇）：荷叶 → 大喷菇 → 曾
void Fix_Gloom(int Row, int Col) {
    FixCell(Row, Col, AFUME_SHROOM, AGLOOM_SHROOM);
}

// 补冰瓜：荷叶 → 西瓜 → 冰瓜
void Fix_Melon(int Row, int Col) {
    FixCell(Row, Col, AMELON_PULT, AWINTER_MELON);
}

void LogicBody() {
    if (AGetMainObject() == nullptr || aFieldInfo.nRows <= 0)
        return;

    // 每帧开始先让帧内缓存按当前场面重建（上一帧到现在游戏可能有过手动操作）
    AInvalidateFrameCache();
    TrackIceEffect(); // 冰的间隔修正：探测上一帧有没有冰生效

    // ---------- 1) 补阵（沿用原 PE96） ----------

    // 曾 / 冰瓜的总数一遍扫完（口径同 PlantCnt：按 Type() 数、血量 >= 0），不再各扫一遍全场植物。
    // 补曾那一段只会种荷叶 / 大喷菇 / 曾，不会改变冰瓜的数量，所以两个数都可以在这里先算好
    int gloomCount = 0;
    int winterMelonCount = 0;
    for (const APlantSnapshot& Plant : AGetFrameSnapshot().plants) {
        if (Plant.type == AGLOOM_SHROOM) {
            if (Plant.plant->Hp() >= 0)
                ++gloomCount;
        } else if (Plant.type == AWINTER_MELON) {
            if (Plant.plant->Hp() >= 0)
                ++winterMelonCount;
        }
    }

    if (gloomCount < 16) {
        // 补七五 -> 七六 -> 九六
        if (!PlantCnt({AGLOOM_SHROOM}, {{3, 7}})) {
            Fix_Gloom(3, 7);
        } else if (!PlantCnt({AGLOOM_SHROOM}, {{3, 6}})) {
            Fix_Gloom(3, 6);
        } else if (!PlantCnt({AGLOOM_SHROOM}, {{2, 5}})) {
            Fix_Gloom(2, 5);
        }
        if (!PlantCnt({AGLOOM_SHROOM}, {{4, 7}})) {
            Fix_Gloom(4, 7);
        } else if (!PlantCnt({AGLOOM_SHROOM}, {{4, 6}})) {
            Fix_Gloom(4, 6);
        } else if (!PlantCnt({AGLOOM_SHROOM}, {{5, 5}})) {
            Fix_Gloom(5, 5);
        }
        // 补七六
        if (!PlantCnt({AGLOOM_SHROOM}, {{2, 6}}) && PlantCnt({AGLOOM_SHROOM}, {{3, 7}}) && PlantCnt({AGLOOM_SHROOM}, {{3, 6}}) && PlantCnt({AGLOOM_SHROOM}, {{2, 5}})) {
            Fix_Gloom(2, 6);
        }
        if (!PlantCnt({AGLOOM_SHROOM}, {{5, 6}}) && PlantCnt({AGLOOM_SHROOM}, {{4, 7}}) && PlantCnt({AGLOOM_SHROOM}, {{4, 6}}) && PlantCnt({AGLOOM_SHROOM}, {{5, 5}})) {
            Fix_Gloom(5, 6);
        }
        // 补九六
        if (!PlantCnt({AGLOOM_SHROOM}, {{3, 9}}) && PlantCnt({AGLOOM_SHROOM}, {{3, 7}}) && PlantCnt({AGLOOM_SHROOM}, {{2, 6}})) {
            Fix_Gloom(3, 9);
        }
        if (!PlantCnt({AGLOOM_SHROOM}, {{4, 9}}) && PlantCnt({AGLOOM_SHROOM}, {{4, 7}}) && PlantCnt({AGLOOM_SHROOM}, {{5, 6}})) {
            Fix_Gloom(4, 9);
        }
        if (!PlantCnt({AGLOOM_SHROOM}, {{3, 8}}) && PlantCnt({AGLOOM_SHROOM}, {{3, 9}}) && PlantCnt({AGLOOM_SHROOM}, {{4, 9}})) {
            Fix_Gloom(3, 8);
        }
        if (!PlantCnt({AGLOOM_SHROOM}, {{4, 8}}) && PlantCnt({AGLOOM_SHROOM}, {{3, 9}}) && PlantCnt({AGLOOM_SHROOM}, {{4, 9}})) {
            Fix_Gloom(4, 8);
        }

        Fix_Gloom(1, 1);
        Fix_Gloom(6, 1);
    }

    if (winterMelonCount < 10) {
        Fix_Melon(2, 1);
        Fix_Melon(5, 1);
    }

    // 补大喷菇（沿用原 PE96 的固定位置）
    //   卡不可用时后面的计数不用做，所以把 AIsSeedUsable 挪到最前（都是纯判断，结果不变）
    for (int Row : {6, 1}) {
        if (AIsSeedUsable(AFUME_SHROOM) && !ZombieCnt({AGIGA_GARGANTUAR}, {}, {Row}, {-1000, 400}) && !PlantCnt({AFUME_SHROOM}, {{Row, 4}}))
            TryCard(AFUME_SHROOM, Row, 4);
    }
    if (AGetMainObject()->Sun() > 3000 && AIsSeedUsable(AFUME_SHROOM)) {
        if (!ZombieCnt({AGIGA_GARGANTUAR}, {}, {1}, {-1000, 700}) && !PlantCnt({AFUME_SHROOM}, {{1, 5}}) && PlantCnt({AGLOOM_SHROOM}, {{2, 6}}))
            TryCard(AFUME_SHROOM, 1, 5);
        if (!ZombieCnt({AGIGA_GARGANTUAR}, {}, {6}, {-1000, 700}) && !PlantCnt({AFUME_SHROOM}, {{6, 5}}) && PlantCnt({AGLOOM_SHROOM}, {{5, 6}}))
            TryCard(AFUME_SHROOM, 6, 5);
    }

    // 唤醒：只唤醒曾 / 喷
    //   先在快照上把要点的格子收齐（植物数组顺序），再逐个种咖啡豆：咖啡豆不会改变别的植物的
    //   睡眠状态，所以“先收集再种”和原来“边扫边种”的结果一样，只是省掉一遍 AvZ 过滤器
    static std::vector<AGrid> wakeGrids; // 复用容量
    wakeGrids.clear();
    for (const APlantSnapshot& Plant : AGetFrameSnapshot().plants) {
        if (!Plant.sleeping)
            continue;
        // 曾一律唤醒；大喷菇只唤醒“原阵要补的”（kFormationFumeGrids），
        // 子力逻辑摆到前面的垫材大喷菇不点咖啡豆
        if (Plant.type == AGLOOM_SHROOM || (Plant.type == AFUME_SHROOM && IsFormationFumeGrid(Plant.row + 1, Plant.col + 1)))
            wakeGrids.push_back(AGrid(Plant.row + 1, Plant.col + 1));
    }
    for (const AGrid& grid : wakeGrids)
        TryCard(ACOFFEE_BEAN, grid.row, grid.col);

    // 铲南瓜（冰车 / 篮球车压到、巨人砸到）
    //   两个判定都要求 Zombie.Row() == Row - 1，所以只遍历本行的僵尸（行内仍是数组顺序）；
    //   遍历前把指针拷出来，铲掉南瓜后重建快照也不会影响正在进行的遍历
    bool FixPumpkin = true;
    for (int Row = 1; Row <= 6; ++Row) {
        for (int Col = 1; Col <= 9; ++Col) {
            if (PlantIndexAt(Row, Col, APUMPKIN) >= 0) {
                int result = 0;
                static std::vector<AZombie*> rowZombies; // 复用容量，免得每个南瓜格都分配一次
                rowZombies.clear();
                {
                    const AFrameSnapshot& snap = AGetFrameSnapshot();
                    for (int slot : snap.rowZombies[Row - 1])
                        rowZombies.push_back(snap.zombies[slot].zombie);
                }
                for (AZombie* ZombiePtr : rowZombies) {
                    AZombie& Zombie = *ZombiePtr;
                    if ((Zombie.Type() == AZOMBONI || Zombie.Type() == ACATAPULT_ZOMBIE) && Zombie.Row() == Row - 1 && Col * 80 + 1 < Zombie.Abscissa() && Zombie.Abscissa() < Col * 80 + 30 + 1) {
                        ARemovePlant(Row, Col, APUMPKIN);
                        AInvalidateFrameCache();
                        FixPumpkin = false;
                        break;
                    }
                    if ((Zombie.Type() == AGIGA_GARGANTUAR || Zombie.Type() == AGARGANTUAR) && Zombie.Row() == Row - 1 && Col * 80 + 40 + 1 < Zombie.Abscissa() && Zombie.Abscissa() < Col * 80 + 30 + 40 + 1 && Zombie.State() == 70 && 0.64242 <= AGetPvzBase()->AnimationMain()->AnimationOffset()->AnimationArray()[Zombie.MRef<uint16_t>(0x118)].CirculationRate() && AGetPvzBase()->AnimationMain()->AnimationOffset()->AnimationArray()[Zombie.MRef<uint16_t>(0x118)].CirculationRate() <= 0.64485) {
                        if (PlantIndexAt(Row, Col + 1) < 0) {
                            ARemovePlant(Row, Col, APUMPKIN);
                            AInvalidateFrameCache();
                            FixPumpkin = false;
                            break;
                        } else {
                            ++result;
                        }
                    }
                }
                if (result >= 2 || (result == 1 && PlantIndexAt(Row, Col, APUMPKIN) < PlantIndexAt(Row, Col + 1))) {
                    ARemovePlant(Row, Col, APUMPKIN);
                    AInvalidateFrameCache();
                    FixPumpkin = false;
                    break;
                }
            }
        }
    }
    (void)FixPumpkin;

    // 补南瓜（七列 + 九列）
    if (AIsSeedUsable(APUMPKIN)) {
        const AFrameSnapshot& snap = AGetFrameSnapshot();
        APlant* pumpkin37 = aInstantPlant::AFindPlantPtr(snap, 3, 7, APUMPKIN);
        APlant* pumpkin47 = aInstantPlant::AFindPlantPtr(snap, 4, 7, APUMPKIN);
        if (!pumpkin37) {
            TryCard(APUMPKIN, 3, 7);
        } else if (!pumpkin47) {
            TryCard(APUMPKIN, 4, 7);
        } else if (pumpkin37->Hp() < 1500 || pumpkin47->Hp() < 1500) {
            if (pumpkin37->Hp() < pumpkin47->Hp())
                TryCard(APUMPKIN, 3, 7);
            else
                TryCard(APUMPKIN, 4, 7);
        }
    }

    // 气球僵尸掉到 2 / 5 行的 6 列或 5 列的曾上（状态 74 = 坠落中、75 = 落地行走），该格有曾、没南瓜、
    // 南瓜卡可用，且附近没有巨人 / 冰车要压过来 → 给这颗曾补南瓜。
    //   条件与原来逐句相同，只是“本行有没有掉在这格窗口里的气球”改用 ZombieCnt 数（只看本行的僵尸表），
    //   不再每帧把全场僵尸扫两遍；南瓜卡不可用时（绝大多数帧）连数都不数。
    //   气球的横坐标窗口：Col 列 = [(Col−1)×80, (Col−1)×80 + 70]，即 6 列 400~470、5 列 320~390
    //   （原来写的是 400 <= x < 471 / 320 <= x < 391，只差 (470, 471) 这不到一个像素的小数区间）。
    //   两格同时都有气球时先补 6 列（原来按僵尸数组顺序，哪只先遍历到就先补哪格）。
    for (int Row : {2, 5}) {
        for (int Col : {6, 5}) {
            if (AIsSeedUsable(APUMPKIN) && ZombieCnt({ABALLOON_ZOMBIE}, {74, 75}, {Row}, {(Col - 1) * 80, (Col - 1) * 80 + 70}) && PlantCnt({AGLOOM_SHROOM}, {{Row, Col}}) && !PlantCnt({APUMPKIN}, {{Row, Col}}) && !ZombieCnt({AGIGA_GARGANTUAR, AGARGANTUAR}, {}, {Row}, {-1000, Col * 80 + 40 + 30 + 1 + 40}) && !ZombieCnt({AZOMBONI}, {}, {Row}, {-1000, Col * 80 + 30 + 1 + 40}))
                TryCard(APUMPKIN, Row, Col);
        }
    }

    // 1 / 6 行 4 列：没有大喷就把这格原有的植物铲掉补大喷；有大喷、没南瓜就补南瓜
    for (int Row : {1, 6}) {
        if (AIsSeedUsable(AFUME_SHROOM) && !PlantCnt({AFUME_SHROOM}, {{Row, 4}}) && !ZombieCnt({AGIGA_GARGANTUAR, AGARGANTUAR}, {}, {Row}, {-1000, 4 * 80 + 40 + 1 + 40}) && !ZombieCnt({AZOMBONI}, {}, {2}, {-1000, 4 * 80 + 1 + 40})) {
            ARemovePlant(Row, 4);
            AInvalidateFrameCache(); // 铲掉了植物，帧内缓存作废（否则后面的计数还会看到它）
            TryCard(AFUME_SHROOM, Row, 4);
        }
        if (AIsSeedUsable(APUMPKIN) && PlantCnt({AFUME_SHROOM}, {{Row, 4}}) && !PlantCnt({APUMPKIN}, {{Row, 4}}) && !ZombieCnt({AGIGA_GARGANTUAR, AGARGANTUAR}, {}, {Row}, {-1000, 4 * 80 + 40 + 30 + 1 + 40}) && !ZombieCnt({AZOMBONI}, {}, {2}, {-1000, 4 * 80 + 30 + 1 + 40}))
            TryCard(APUMPKIN, Row, 4);
    }

    // 冰车 / 小丑局：给二列七行、五行七行补喷
    auto Zombie_Type = AGetMainObject()->ZombieTypeList();
    if ((Zombie_Type[AZOMBONI] || Zombie_Type[AJACK_IN_THE_BOX_ZOMBIE]) && AGetMainObject()->Sun() > 3000 && AIsSeedUsable(AFUME_SHROOM)) {
        if (!PlantCnt({AFUME_SHROOM}, {{2, 7}}) && PlantCnt({AGLOOM_SHROOM}, {{3, 9}}) && !ZombieCnt({AGIGA_GARGANTUAR}) && !ZombieCnt({AGARGANTUAR}, {}, {2}, {-1000, 7 * 80 + 40 + 1 + 10}) && !ZombieCnt({AZOMBONI}, {}, {2}, {-1000, 7 * 80 + 1 + 10}) && !ZombieCnt({AFOOTBALL_ZOMBIE}, {}, {2}, {-1000, 7 * 80 - 40 + 1 + 10}, {90}) && !ZombieCnt({ADANCING_ZOMBIE}, {}, {2}))
            TryCard(AFUME_SHROOM, 2, 7);
        if (!PlantCnt({AFUME_SHROOM}, {{5, 7}}) && PlantCnt({AGLOOM_SHROOM}, {{4, 9}}) && !ZombieCnt({AGIGA_GARGANTUAR}) && !ZombieCnt({AGARGANTUAR}, {}, {5}, {-1000, 7 * 80 + 40 + 1 + 10}) && !ZombieCnt({AZOMBONI}, {}, {5}, {-1000, 7 * 80 + 1 + 10}) && !ZombieCnt({AFOOTBALL_ZOMBIE}, {}, {5}, {-1000, 7 * 80 - 40 + 1 + 10}, {90}) && !ZombieCnt({ADANCING_ZOMBIE}, {}, {5}))
            TryCard(AFUME_SHROOM, 5, 7);
    }

    // ---------- 2) 子力判断：cd 好了就找子力最高的操作位 ----------
    const bool iceReady = IceReady(); // 一帧只判一次（补阵已经结束，到 TryUseIce 之间没有人再动场面）
    if (ThreatCacheNeeded(iceReady)) {
        RefreshThreatCache();
    } else {
        // 本帧没人用得到威胁度快照，省掉这一遍；清空以保持“上一帧的快照不带到下一帧”
        ClearThreatCache();
    }
    LogJackState(); // 调参用：开盒小丑进入窗口时打印判定过程
    TryUseIce(iceReady);
    TryUseAshCard(ACHERRY_BOMB, kCherryThreshold);
    TryUseAshCard(ASQUASH, kSquashThreshold);
    TryUseFodderCards(); // 小喷菇 → 大喷菇（两张卡共用一次扫描）
}

// ============================================================================
//  补南瓜：aPlantFixer 的快照版（逐句复刻 AvZ APlantFixer::_Run，判断结果相同）
//    AvZ 的 APlantFixer 每帧都调 AGetPlantIndices：先建一个 std::map<格子, vector>（十几次内存分配），
//    再用 aAlivePlantFilter 把全场植物扫一遍、每颗植物查一次 map —— 这是脚本每帧剩下最贵的一件事。
//    这里改成在本帧快照上按格子查表（AFindPlantIndex 与 AGetPlantIndex 逐句同义），其余逻辑照抄：
//      · 卡：南瓜卡（原版在前、模仿者在后，取第一张能用的；能不能用直接读卡槽内存，和 AvZ 一样），
//        没有能用的就什么都不做
//      · 按列表顺序：某格没有南瓜 → 该格能种就种（一帧只种一颗，种完就返回）；
//        有南瓜 → 记下血量最低、且低于 fixHp 的那一格，列表扫完后铲掉重种
//    运行模式 / 优先级 / 启动位置与原来 aPlantFixer.Start 一样（ONLY_FIGHT、优先级 0、在 aIceFiller
//    之后启动；AvZ 的帧运行按“模式 → 优先级 → 启动先后”执行），所以每帧的执行顺序也不变。
//    快照在本帧已由 Logic 建好，这里只是查表；aIceFiller 夹在中间种的存冰不在南瓜列表的格子上，
//    不影响这里的结果。
// ============================================================================
struct PumpkinFixer {
    std::vector<AGrid> grids;
    std::vector<int> seedIdx; // 南瓜卡在卡槽里的下标（0 起）：原版在前、模仿者在后
    int fixHp = 0;
    ATickRunner runner;

    void Start(const std::vector<AGrid>& list, int hp) {
        grids = list;
        fixHp = hp;
        seedIdx.clear();
        for (bool imitator : {false, true}) {
            const int idx = AGetSeedIndex(APUMPKIN, imitator);
            if (idx != -1)
                seedIdx.push_back(idx);
        }
        if (seedIdx.empty())
            aLogger->Error("PumpkinFixer：卡槽里没有南瓜卡");
        runner.Start([this] { Run(); }, ATickRunner::ONLY_FIGHT);
    }

    void Run() {
        if (seedIdx.empty())
            return;
        // 第一张能用的南瓜卡
        ASeed* seedArray = AGetMainObject()->SeedArray();
        int seed = -1;
        for (int idx : seedIdx) {
            if (AIsSeedUsable(seedArray + idx)) {
                seed = idx;
                break;
            }
        }
        if (seed < 0)
            return;

        const AFrameSnapshot& snap = AGetFrameSnapshot();
        APlant* plantArray = AGetMainObject()->PlantArray();
        AGrid needGrid {0, 0};
        int minHp = fixHp;
        for (const AGrid& grid : grids) {
            const int plantIdx = aInstantPlant::AFindPlantIndex(snap, grid.row, grid.col, APUMPKIN);
            if (plantIdx == -2)
                continue; // 该格是别的植物（查南瓜时不会出现 -2，照抄保留）
            if (plantIdx == -1) {
                // 没有南瓜：能种就种，一帧只种一颗
                if (AAsm::GetPlantRejectType(APUMPKIN, grid.row - 1, grid.col - 1) != AAsm::NIL)
                    continue;
                ACard(seed + 1, grid.row, grid.col);
                AInvalidateFrameCache(); // 场上多了一颗植物，帧内缓存作废
                return;
            }
            const int hp = (plantArray + plantIdx)->Hp();
            if (hp < minHp) { // 血量最低的那一格（严格小于，先到先得）
                minHp = hp;
                needGrid = grid;
            }
        }
        if (needGrid.row) {
            // 铲掉血量最低的南瓜再种（AvZ 原版也是 {南瓜, 南瓜} 这个优先级列表）
            ARemovePlant(needGrid.row, needGrid.col, {APUMPKIN, APUMPKIN});
            ACard(seed + 1, needGrid.row, needGrid.col);
            AInvalidateFrameCache(); // 铲了又种，帧内缓存作废
        }
    }
};
PumpkinFixer gPumpkinFixer;

// 设定Dance
inline void SetDance(bool state) {
    asm volatile("movl 0x6A9EC0, %%eax;"
                 "movl 0x768(%%eax), %%ecx;"
                 "pushl %%ecx;"
                 "movl %[state], %%ebx;"
                 "movl $0x41AFD0, %%eax;"
                 "calll *%%eax;"
                 :
                 : [state] "rm"(unsigned(state))
                 : "eax", "ebx", "ecx");
}

// -1 = 正常，0 = 加速，1 = 减速
static int DCState = -1;
inline void DanceCheat() {
    if (DCState != -1)
        SetDance(DCState);
}

void Logic() {
    DCState = 1;
    DanceCheat();
    const double begin = kShowPerfStats ? __AProfiler::CurrentTime() : 0.0;
    LogicBody();
    if (kShowPerfStats)
        gLogicPerf.Add(__AProfiler::CurrentTime() - begin);
}

// ============================================================================
//  日志输出
//    AvZ 默认的 aLogger 是 ALogger<AMsgBox>（每条日志弹一个对话框，见 avz_script.cpp），
//    所以原脚本用 AGetInternalLogger()->SetLevel({}) 把它整个关掉了 —— 结果 [子力] /
//    [丑] 这些调试日志全被吞掉，看不到。
//    这里换成文件 logger，追加写到下面这个路径，方便对数值 / 事后翻。
//    （想直接画在游戏画面上就把类型换成 ALogger<APvzGui>。）
// ============================================================================
// ALogger<AFile> gValueLog("D:\\RSvz\\pe96_value.log");

void AScript() {
    ASetReloadMode(AReloadMode::MAIN_UI_OR_FIGHT_UI);
    AMaidCheats::Dancing();
    AGetPvzBase()->TickMs() = 10;
    // ASetInternalLogger(gValueLog);
    // aLogger->SetLevel({ALogLevel::INFO, ALogLevel::WARNING, ALogLevel::ERROR});
    AGetInternalLogger()->SetLevel({});

    // 每行 C = 1、k = 1（配合 ValueOptions() 的 distanceInTiles = true，即“每格衰减 1”）
    for (int row : {1, 6})
        aThreatLevelRowParams[row] = {1.8, 0.8};
    for (int row : {2, 5})
        aThreatLevelRowParams[row] = {1.0, 0.8};

    // 种类修正：本阵型只认 冰车 / 橄榄 / 红眼 / 白眼 / 小丑，其余（含默认值）为 0
    //   气球不在这张表里（保持 0）：它只在冰的气球项按 kBalloonIceTypeFactor 记子力（见 BalloonIceValue），
    //   这样樱桃 / 倭瓜 / 垫材 / 冰的地面项都不会因为气球而改变判断
    for (int type = 0; type < A_THREAT_TYPE_SLOT; ++type)
        aThreatTypeFactor[type] = 0.0;
    aThreatTypeFactor[AZOMBONI] = 0.2;
    aThreatTypeFactor[AFOOTBALL_ZOMBIE] = 0.5;
    aThreatTypeFactor[AGIGA_GARGANTUAR] = 1.0;
    aThreatTypeFactor[AGARGANTUAR] = 1.0;
    aThreatTypeFactor[AJACK_IN_THE_BOX_ZOMBIE] = 0.5;

    // 阵前筛选：红眼关时岸路（2 / 5 行）不许 7 列的临时喷当阵前（见 Pe96RowFrontAllowed）
    aThreat::aRowFrontFilter = Pe96RowFrontAllowed;

    // 威胁度覆盖：快要炸到忧郁菇、且灰烬来得及先炸死的小丑，威胁度直接顶到 10 万
    aThreatValueOverride = Pe96ThreatOverride;

    // 新的一局：上一局残留的帧内缓存 / 威胁度快照 / 上一颗冰的生效时刻全部作废
    AInvalidateFrameCache();
    ClearThreatCache();
    gLastIceEffectClock = kNoIceEffectClock;

    std::vector<int> Zombielist = ACreateRandomTypeList("普", "");
    // std::vector<int> Zombielist = ACreateRandomTypeList("普气偷", "白桶");
    // std::erase(Zombielist, ABUNGEE_ZOMBIE);
    ASetZombies(Zombielist, ASetZombieMode::INTERNAL);
    // ASetZombies("普报红偷气丑舞梯跳", ASetZombieMode::INTERNAL);
    ASetGameSpeed(10);
    AConnect([] { return AGetMainObject()->GameClock() % 16 == 1; }, [] { ASkipTick([] { return (AGetMainObject()->GameClock() % 16 != 0); }); });

    // ---------------- 选卡：按优先级排候选，去重后取前 10 张 ----------------
    //   固定 7 张 → 曾没补满 16 颗 / 小丑关 / 海豚关先带荷叶 + 曾 → 红眼关带小喷菇
    //   → 阳光够 8000、冰瓜不满 10 颗时带（西瓜 +）冰瓜 → 最后用荷叶 / 曾 / 小喷菇补满 10 张。
    //   同一张卡会被推进候选好几次（荷叶 / 曾 / 小喷菇既在条件项也在补位项），所以最后按
    //   “先出现的优先”去重再截到 10 张，不会再重复选卡。固定 7 张和 3 张补位互不相同，
    //   去重后至少 10 张，所以永远选满 10 张。
    //   （进入选卡界面时本关的僵尸种类表已经确定；PlantCnt 查的是选卡时的场面，帧内缓存刚作废过）
    //   想同时带模仿者小喷菇就把 AM_PUFF_SHROOM 也推进候选，注意卡槽只有 10 个

    auto Zombie_Type = AGetMainObject()->ZombieTypeList();
    const int SelectSun = AGetMainObject()->Sun();
    const int gloomCount = PlantCnt({AGLOOM_SHROOM});
    const int winterMelonCount = PlantCnt({AWINTER_MELON});

    std::vector<int> cardCandidates = {AM_ICE_SHROOM, AICE_SHROOM, ACHERRY_BOMB, ASQUASH, ACOFFEE_BEAN, APUMPKIN, AFUME_SHROOM};

    if (gloomCount < 16 || Zombie_Type[AJACK_IN_THE_BOX_ZOMBIE] || Zombie_Type[ADOLPHIN_RIDER_ZOMBIE]) {
        cardCandidates.push_back(ALILY_PAD);
        cardCandidates.push_back(AGLOOM_SHROOM);
    }
    if (Zombie_Type[AGIGA_GARGANTUAR]) {
        cardCandidates.push_back(APUFF_SHROOM);
    }
    if (winterMelonCount < 10 && SelectSun >= 8000) {
        if (PlantCnt({AMELON_PULT}) + winterMelonCount == 10) {
            cardCandidates.push_back(AWINTER_MELON); // 西瓜已经摆满，只要升级卡
        } else {
            cardCandidates.push_back(AMELON_PULT);
            cardCandidates.push_back(AWINTER_MELON);
        }
    }
    cardCandidates.push_back(AGLOOM_SHROOM);
    cardCandidates.push_back(ALILY_PAD);
    cardCandidates.push_back(APUFF_SHROOM);

    // 去重（先出现的优先）后取前 10 张
    std::vector<int> Cardlist;
    for (int card : cardCandidates) {
        if (std::find(Cardlist.begin(), Cardlist.end(), card) != Cardlist.end())
            continue; // 已经选过
        Cardlist.push_back(card);
        if (Cardlist.size() == 10)
            break;
    }
    ASelectCards(Cardlist, 1);

    // ShowWavelength(false, 3, true);

    T1.Start(Logic, ATickRunner::GLOBAL);
    if (kShowValueOverlay || kShowPerfStats)
        ValueOverlay.Start(DrawValueOverlay, ATickRunner::PAINT); // 子力显示 UI / 耗时显示
    aIceFiller.Start(kIceStoreGrids);                             // 预存寒冰菇（位置与冰子力显示共用 kIceStoreGrids）
    // 补南瓜：快照版的 aPlantFixer（见 PumpkinFixer），参数与原来 aPlantFixer.Start(APUMPKIN, ..., 4000 / 3) 相同
    gPumpkinFixer.Start({{3, 7}, {4, 7}, {1, 1}, {2, 1}, {5, 1}, {6, 1}, {3, 9}, {4, 9}, {3, 8}, {4, 8}, {2, 5}, {5, 5}, {3, 6}, {4, 6}}, 4000 / 3);

    static bool isPaused = false;
    At('Z')[] {
        isPaused = !isPaused;
        ASetAdvancedPause(isPaused, 0, 0);
    };
    At('X')[] {
        isPaused = false;
        ASetAdvancedPause(isPaused, 0, 0);
        AConnect(ANowDelayTime(1), [] {
            isPaused = !isPaused;
            ASetAdvancedPause(isPaused, 0, 0);
        });
    };
    At('C')[] { AGetPvzBase()->TickMs() = AGetPvzBase()->TickMs() == 1 ? 10 : 1; };
    At('V')[] { AMRef<int>(0x416DBE) = AMRef<int>(0x416DBE) == 699999 ? 100001 : 699999; };
}
