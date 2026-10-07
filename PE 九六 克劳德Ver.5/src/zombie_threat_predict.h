#ifndef __A_ZOMBIE_THREAT_PREDICT_H__
#define __A_ZOMBIE_THREAT_PREDICT_H__

// ============================================================================
//  僵尸“威胁血量”判定（反向查询）
//
//  用途：灰烬与玉米炮本质上都有延迟，如果只看当前血量就会出现“对着一堆即将被炸死的
//  僵尸继续浪费输出”的问题。本文件反过来站在僵尸视角：
//      “已经发射并指向它的炮弹 / 已经在等待生效的灰烬（含本帧刚发射、刚种下的）”
//      会在多少帧后打到它，并把即将到来的伤害从血量里扣掉。
//
//  血量口径（与 instant_plant_predict.h 一致）：
//      本体血量 + 一类防具（路障 / 铁桶 / 橄榄球头盔…）
//      二类防具（铁门 / 报纸…）不计入
//
//  威胁时间 threatTime（默认 -1）：
//      -1 ：把所有即将到来的伤害都扣掉（血量下限 0）
//      >0 ：只扣掉“生效时间 <= threatTime 帧”的伤害；
//           生效时间大于 threatTime 的威胁视为“来不及”，不扣
//
//  数据来源（PvZ 1.0.0.1051 反编译 + AvZ 结构对齐）：
//      · 灰烬植物：APlant::ExplodeCountdown()（偏移 0x50，游戏里的 do_special）就是
//        “还有多少帧生效”。AvZ 自己的 ASetPlantActiveTime 也是读写这个字段来改生效时间
//      · 玉米炮：发炮瞬间游戏把 plant+0x90（= APlant::ShootCountdown()）置为 206
//        （真实游戏 0x466d50：mov DWORD PTR [esi+0x90], 0xce），减到 1 时炮弹出膛，
//        所以发炮指令一发出就有“剩余出膛时间 = ShootCountdown - 1”；出膛后再飞 168cs，
//        合计就是 AvZ 简写表的 373cs 提前量（205cs 出膛 + 168cs 飞行）。
//        瞄准点 x / y 存在 plant+0x80 / 0x84，是【整数】（0x466d92 __ftol 后 mov 写入），
//        命中圆心 = 落点 x - 7、半径 115、行 ±1
//      · 已经在空中的炮弹：AProjectile::Type() == 11，用 ExistTime() 反推剩余 168-ExistTime
//        （这里的 0x80 是 float：CobTargetAbscissa() = float@0x80 + 87.5）
//      · 倭瓜：状态 3/4/5/6 = look / jump_up / stop_in_air / jump_down，
//        时长 80 / 45 / 50 / 10，砸下发生在 jump_down 的 countdown == 5
//      · 僵尸速度：基础速度 = AZombie::Speed()（0x34，含随机数）；
//        减速（0xAC SlowCountdown > 0）期间 ×0.4，冰冻（0xB4）/ 黄油（0xB0）期间原地不动
//        —— 与游戏 zombie_base::predict_after / zombie_system::update_x 一致
//
//  帧内缓存（性能，见 instant_plant_predict.h 的说明）：
//      威胁来源（正在等生效的灰烬 / 倭瓜 / 发射中的炮、空中的炮弹）、开盒小丑、
//      各行阵前、已唤醒的大喷菇 / 忧郁菇 / 冰瓜，都只跟场上植物有关、跟“问的是哪只僵尸”
//      无关，所以一帧只扫一遍植物 / 子弹数组（aThreat::AGetThreatFrameCache），每只僵尸
//      的查询只在这些短列表上做几何判定；每只僵尸的“即将到来的威胁列表”和“自然输出”
//      （默认口径）一帧也只算一次。所有判定条件与遍历顺序（植物数组顺序、子弹数组顺序）
//      都与逐次扫描时相同。
// ============================================================================

#include "instant_plant_predict.h"

#include <avz.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <vector>

// 一条“即将到来的伤害”
struct AZombieIncomingThreat {
    AZombie* zombie = nullptr;
    APlant* plant = nullptr;            // 灰烬 / 倭瓜植物，或发炮的炮台
    AProjectile* projectile = nullptr;  // 已经在空中的炮弹（出膛阶段为 nullptr）
    APlantType source = ACHERRY_BOMB;   // 威胁来源：灰烬用植物类型，炮用 ACOB_CANNON
    int eta = 0;                        // 还有多少帧（cs）生效
    int damage = 1800;                  // 预计伤害
    bool lethal = false;                // 是否足以直接击杀
};

// 自然输出（大喷菇 / 忧郁菇 / 冰瓜）的抽象参数
//   把它们的输出抽象成“对射程内所有僵尸匀速输出”，dps 用平均攻击节奏与伤害量换算：
//     大喷菇：20 伤害 / 150cs 单发 → 13.3 dps；只打本行
//     忧郁菇：20 伤害 × 4 发 / 200cs → 40 dps；打本行 ±1、横向左右各一格（3×3）
//     冰瓜：最简化口径 —— 本行每 300cs 固定 30 点、相邻行每 300cs 固定 20 点；
//           射程是整行（僵尸一进场就在射程内），所以整段走位 + 阵前时间都算
//   喷 / 曾的攻击框取自反编译 Plant::GetPlantAttackRect（0x467F90，Plant.cpp:5220-5223）：
//     大喷菇 Rect(mX + 60, mY, 340, mHeight)  → x ∈ [mX+60, mX+400]
//     忧郁菇 Rect(mX - 80, mY - 80, 240, 240) → x ∈ [mX-80, mX+160]，y 覆盖上下各一行
//   mX 就是 AvZ 的 plant.Abscissa()（Plant::PlantInitialize：mX = GridToPixelX = col*80 + 40）。
//   喷 / 曾命中判定见 Plant::DoRowAreaDamage（0x45ED00）：行闸门 + 攻击框与僵尸判定框的 x 重叠 > 0，
//   并要求僵尸 EffectedByDamage(9)（9 = 地面 | DOG）。9 打不到飞行气球 / 潜水 / 垂死 / 钻地。
//   冰瓜真实机制要复杂得多（Projectile::DoSplashDamage：80 主目标 + 26 溅射封顶 182、
//   120cs 抛物线、命中附带 1000cs 减速…），这里按上面的最简口径近似。
struct ANaturalOutputOptions {
    bool includeFume = true;
    bool includeGloom = true;
    bool includeWinterMelon = true;
    double fumeDps = 20.0 / 150.0 * 100.0;        // 单发 20 / 间隔 150cs
    double gloomDps = 20.0 * 4.0 / 200.0 * 100.0; // 4 发 ×20 / 循环 200cs
    double melonSameRowDps = 30.0 / 300.0 * 100.0;      // 冰瓜本行：30 伤害 / 300cs = 10 dps
    double melonAdjacentRowDps = 20.0 / 300.0 * 100.0;  // 冰瓜相邻行：20 伤害 / 300cs ≈ 6.67 dps
    double fumeRangeStartPx = 60.0;    // 大喷菇：本格内 60px 起（Rect 的 mX+60）
    double fumeRangeEndPx = 400.0;     // 大喷菇：框宽 340 → 到 mX+400
    double gloomRangeStartPx = -80.0;  // 忧郁菇 3×3：左一格
    double gloomRangeEndPx = 160.0;    // 右一格（含自身格子共 3 格）
    bool includeGloomAdjacentRows = true;  // 忧郁菇打 ±1 行
    int extraFrontlineCs = 0;          // 非巨人僵尸在阵前“攻击期间”额外计入的帧数
    double gargantuarWindupCs = 130.0; // 巨人还没举锤时，够到阵前后到砸下的估算
    // 估值用：假设“现在再冻 / 再减速”多少帧（用于寒冰菇的价值：用冰之后威胁血量会掉多少）
    //   0 = 只看当前状态
    int assumeFreezeCs = 0;
    int assumeSlowCs = 0;

    // 帧内缓存按“同一组参数”复用结果，需要能比较
    bool operator==(const ANaturalOutputOptions&) const = default;
};

struct AThreatQueryOptions {
    // 是否把倭瓜也算作威胁（倭瓜不是灰烬，落地判定也不同，默认计入）
    bool includeSquash = true;
    // 睡觉的植物（白天没咖啡豆的毁灭菇、寒冰菇）不算威胁
    bool ignoreSleeping = true;
    // 威胁血量是否再扣掉自然输出（大喷菇 / 忧郁菇）
    bool includeNaturalOutput = true;
    ANaturalOutputOptions naturalOptions;

    bool operator==(const AThreatQueryOptions&) const = default;
};

namespace aThreat {

using namespace aInstantPlant;

// 炮弹（玉米炮）在 AProjectile::Type() 里的编号
constexpr int COB_PROJECTILE_TYPE = 11;

// 玉米炮时间轴：发炮 → 出膛 205cs，出膛 → 命中 168cs，合计 373cs
constexpr int COB_MUZZLE_CS = 205;
constexpr int COB_SHELL_FLIGHT_CS = 168;

// 僵尸被减速（寒冰菇 / 冰瓜 / 冰豆）时的速度系数。
// 游戏自己的预测函数 zombie_base::predict_after 与移动函数 update_x 都是：
//     dx = z.dx * (countdown.slow > 0 ? 0.4 : 1)
// 冰冻（countdown.freeze）/ 黄油（countdown.butter）期间则完全不移动（fps 置 0）。
constexpr double ZOMBIE_SLOW_SPEED_FACTOR = 0.4;

// 倭瓜状态（反编译 plant_status）
enum : int {
    SQUASH_STATE_IDLE = 0,       // 刚种下（游戏还没跑 find_target，目标 id 暂时是空的）
    SQUASH_STATE_LOOK = 3,       // 起跳前凝视（80cs）
    SQUASH_STATE_JUMP_UP = 4,    // 起跳（45cs）
    SQUASH_STATE_AIR = 5,        // 空中停留（50cs）
    SQUASH_STATE_JUMP_DOWN = 6,  // 下落（countdown == 5 时结算伤害）
    SQUASH_STATE_CRUSHED = 7,    // 已砸下
};

// 植物真实类型（实现已挪到 instant_plant_predict.h，这里保留原名）
using aInstantPlant::APlantBaseType;

// 植物命中框（反编译 object/plant.cpp 的 plant::get_hit_box）
//   普通植物 / 高坚果：rect.x = x + 10
//   南瓜头：          rect.x = x
//   玉米加农炮：      rect.x = x，且宽度被硬编码为 140（内存里的 HurtWidth 只有 80）
inline int APlantHitBoxOffset(int baseType) {
    return (baseType == APUMPKIN || baseType == ACOB_CANNON) ? 0 : 10;
}

inline AHitBox AGetPlantHitBox(APlant* plant) {
    const int base = APlantBaseType(plant);
    AHitBox box;
    box.x = plant->Abscissa() + APlantHitBoxOffset(base);
    box.y = plant->Ordinate();
    box.width = (base == ACOB_CANNON) ? 140 : plant->HurtWidth();
    box.height = plant->HurtHeight();
    return box;
}

// 假设在 (row, col) 刚种下一颗普通体型的植物（樱桃 / 辣椒 / 毁灭菇 / 倭瓜都是）时的命中框
//   格子像素走 AGetCellPixelTable（一关只问游戏一次，值与 AAsm::GridToAbscissa / GridToOrdinate 相同）；
//   格子不在表里（正常不会发生）才直接问游戏
inline AHitBox APlantHitBoxAt(int row, int col) {
    AHitBox box;
    const ACellPixelTable& table = AGetCellPixelTable();
    if (table.mainObject != nullptr && row >= 1 && row <= table.nRows && col >= 1 && col <= 9) {
        box.x = table.cellX[row - 1][col - 1] + 10;       // 默认体型：[x+10, ...]
        box.y = table.cellY[row - 1][col - 1];
    } else {
        box.x = AAsm::GridToAbscissa(row - 1, col - 1) + 10;
        box.y = AAsm::GridToOrdinate(row - 1, col - 1);
    }
    box.width = 60;                                       // attack_box.width(80) - 20
    box.height = 80;
    return box;
}

// ---- 小丑僵尸（Jack-in-the-Box）----
// 反编译 system/zombie/jack_in_the_box.cpp：
//   · 状态 jackbox_pop(=16) 时 countdown.action 递减，减到 0 引爆
//   · 引爆点 = (int_x + 60, int_y + 60)，以半径 90 摧毁植物（is_overlap_with_circle）
//   · 同一帧对僵尸是半径 115、行 ±1 的灰烬判定
constexpr int JACKBOX_POP_STATE = 16;
constexpr int JACK_PLANT_KILL_RADIUS = 90;

// 本帧处于引爆状态的小丑（帧内缓存用）
struct AJackEntry {
    AZombie* zombie = nullptr;
    int countdown = 0;   // StateCountdown：还有多少帧引爆
    int centerX = 0;     // 引爆圆心
    int centerY = 0;
};

// 这个植物会不会在 eta 帧内被小丑炸掉（被炸掉就不会生效了）—— 在给定的小丑列表上判
inline bool __AIsPlantJackedBy(const std::vector<AJackEntry>& jacks, const AHitBox& box, int eta) {
    if (eta < 0)
        return false;
    for (const AJackEntry& jack : jacks) {
        if (jack.countdown > eta)  // 引爆比生效时间晚 → 不影响
            continue;
        if (AHitBoxOverlapCircle(box, jack.centerX, jack.centerY, JACK_PLANT_KILL_RADIUS))
            return true;
    }
    return false;
}

// 这个植物会不会在 eta 帧内被小丑炸掉（被炸掉就不会生效了）
inline bool AIsPlantJacked(const AHitBox& box, int eta);  // 实现在帧内缓存之后

// 由行的像素 y 反查行号（行距 85/100，容差 40）
//   行像素走 AGetCellPixelTable（一关只问游戏一次，值与 AAsm::GridToOrdinate 相同）
inline int ARowFromTileY(int tileY) {
    int best = -1;
    int bestDiff = 1 << 30;
    const ACellPixelTable& table = AGetCellPixelTable();
    for (int row = 0; row < aFieldInfo.nRows; ++row) {
        const int rowY = (table.mainObject != nullptr && row < table.nRows) ? table.cellY[row][0]
                                                                            : AAsm::GridToOrdinate(row, 0);
        const int diff = std::abs(rowY - tileY);
        if (diff < bestDiff) {
            bestDiff = diff;
            best = row;
        }
    }
    return bestDiff <= 40 ? best : -1;
}

// ---- 阵前筛选钩子 ----
// 默认 nullptr：本行最靠右的“已唤醒大喷菇 / 忧郁菇 / 冰瓜 / 玉米加农炮”就是阵前。
// 需要时可以设一个函数，返回 false 表示“这颗植物不当作阵前”——例如临时垫出来、
// 不值得为它防守的喷菇，就不该让它把阵前推前。
// 和 aThreatLevelRowParams / aThreatTypeFactor 一样是全局设置，脚本在 AScript() 里设一次即可：
//     aThreat::aRowFrontFilter = MyFilter;   // bool MyFilter(APlant*, int row)
// 库的其它接口不变，所有走阵前的地方（威胁度距离、自然输出、垫材）都会自动生效。
using ARowFrontFilter = bool (*)(APlant* plant, int row);

inline ARowFrontFilter aRowFrontFilter = nullptr;

// ---- 阵前 / 进家位置（威胁血量与自然输出共用）----
struct ARowFrontInfo {
    bool found = false;
    int x = 0;                 // 阵前像素（命中框右边界）
    APlant* plant = nullptr;   // 触发阵前的植物
    APlant* pumpkin = nullptr; // 该格上的南瓜头（没有则为 nullptr）
};

// 一行“阵前”：已唤醒的大喷菇 / 忧郁菇 / 冰瓜 / 玉米加农炮里最靠右的那个；
// 该格有南瓜头就按南瓜头命中框算（右边界）—— 在快照上直接算
inline ARowFrontInfo __AComputeRowFront(const AFrameSnapshot& snap, int row) {
    ARowFrontInfo front;
    const APlantSnapshot* best = nullptr;
    const ARowFrontFilter filter = aRowFrontFilter;
    for (const APlantSnapshot& plant : snap.plants) {
        if (plant.row + 1 != row)
            continue;
        const int base = plant.baseType;
        if (base != AFUME_SHROOM && base != AGLOOM_SHROOM && base != AWINTER_MELON && base != ACOB_CANNON)
            continue;
        if (plant.sleeping)  // 白天没咖啡豆的蘑菇不算已唤醒
            continue;
        if (filter != nullptr && !filter(plant.plant, row))  // 被脚本排除的不算阵前
            continue;
        if (best == nullptr || plant.x > best->x)
            best = &plant;
    }
    if (best == nullptr)
        return front;

    front.found = true;
    front.plant = best->plant;
    front.pumpkin = AFindPlantPtr(snap, row, best->col + 1, APUMPKIN);  // = AGetPlantPtr(row, col, APUMPKIN)
    APlant* source = front.pumpkin != nullptr ? front.pumpkin : best->plant;
    front.x = AGetPlantHitBox(source).Right();
    return front;
}

// 一行“阵前”（帧内缓存版，实现在后面）
inline ARowFrontInfo ARowFront(int row);

// 僵尸的“进家位置” x（反编译 zombie_system::update_entering_home）
inline int AZombieHomeX(int zombieType) {
    switch (zombieType) {
    case AGARGANTUAR:
    case AGIGA_GARGANTUAR:
    case APOLE_VAULTING_ZOMBIE:
        return -150;
    case ACATAPULT_ZOMBIE:
    case AFOOTBALL_ZOMBIE:
    case AZOMBONI:
        return -175;
    case ABACKUP_DANCER:
    case ADANCING_ZOMBIE:
    case ASNORKEL_ZOMBIE:
        return -130;
    default:
        return -100;
    }
}

// ---- 巨人砸击相位（读内存动画相位；减速把动画拉长会自动体现）----
// AvZ: State() == 70（gargantuar_smash = 0x46），结算在动画进度 0.64 那一帧
constexpr int GARGANTUAR_SMASH_STATE = 70;

struct AGargantuarAnimSample {
    int clock = -1;
    float rate = 0.0f;
    double ratePerCs = 0.0;  // 上次量到的“进度/帧速度”，同帧内多次调用时复用
};

inline std::map<AZombie*, AGargantuarAnimSample>& GargantuarAnimCache() {
    static std::map<AZombie*, AGargantuarAnimSample> cache;
    if (cache.size() > 32)
        cache.clear();
    return cache;
}

struct AGargantuarSmashInfo {
    bool valid = false;
    bool smashing = false;
    double impactCs = 0.0;   // 距离砸下还有多少 cs
    double cycleCs = 206.0;  // 一整个锤击循环时长
};

inline bool IsGargantuarZombie(int zombieType) {
    return zombieType == AGARGANTUAR || zombieType == AGIGA_GARGANTUAR;
}

inline AGargantuarSmashInfo AGetGargantuarSmashInfo(AZombie* gargantuar, double arriveCs = 0.0,
                                                   double windupCs = 130.0, double smashImpactRate = 0.64,
                                                   double fallbackCycleCs = 206.0) {
    AGargantuarSmashInfo result;
    if (gargantuar == nullptr || !IsGargantuarZombie(gargantuar->Type()))
        return result;

    result.valid = true;
    result.cycleCs = fallbackCycleCs;
    if (gargantuar->State() != GARGANTUAR_SMASH_STATE) {
        result.impactCs = arriveCs + windupCs;
        return result;
    }

    result.smashing = true;
    const int clock = AGetMainObject()->GameClock();
    const float rate = gargantuar->AnimationPtr()->CirculationRate();
    auto& cache = GargantuarAnimCache();
    const auto it = cache.find(gargantuar);
    double ratePerCs = 0.0;
    if (it != cache.end()) {
        if (clock > it->second.clock && rate > it->second.rate)
            ratePerCs = (rate - it->second.rate) / (clock - it->second.clock);
        else if (clock == it->second.clock)
            ratePerCs = it->second.ratePerCs;
    }
    if (ratePerCs > 0.0) {
        result.impactCs = (smashImpactRate - rate) / ratePerCs;
        result.cycleCs = 1.0 / ratePerCs;
        if (result.impactCs < 0.0)
            result.impactCs = 0.0;
    } else {
        result.impactCs = windupCs;
    }
    cache[gargantuar] = AGargantuarAnimSample{clock, rate, ratePerCs};
    return result;
}

// ---- 自然输出（大喷菇 / 忧郁菇）对某个僵尸的伤害 ----
struct ANaturalOutputInfo {
    bool valid = false;
    bool hasFront = false;
    bool isHouse = false;     // 本行没有阵前植物，退化成进家位置
    int frontX = 0;
    double walkCs = 0.0;      // 按当前速度走到阵前的时间
    double attackCs = 0.0;    // 在阵前攻击（巨人 = 锤击前摇）的额外时间
    double damage = 0.0;      // 这段时间被大喷菇 / 忧郁菇 / 冰瓜打掉的血量
    double fumeDps = 0.0;
    double gloomDps = 0.0;
    double melonDps = 0.0;
};

// 僵尸命中框左边界从 leftNow 走到 leftTarget 期间，落在 [rangeLeft, rangeRight] 的像素长度
inline double ATraversedOverlapPx(double leftNow, double leftTarget, double boxWidth, double rangeLeft,
                                  double rangeRight) {
    const double lo = std::max(std::min(leftNow, leftTarget), rangeLeft - boxWidth);
    const double hi = std::min(std::max(leftNow, leftTarget), rangeRight);
    return hi > lo ? hi - lo : 0.0;
}

// 走 distancePx 像素需要多少帧（含减速 / 冰冻；是 instant_plant_predict.h 里 ADisplacement 的逆运算）
inline double ATravelCsForPx(AZombie* zombie, double distancePx) {
    if (zombie == nullptr || distancePx <= 0.0)
        return 0.0;
    const double baseSpeed = std::max(0.05f, std::fabs(zombie->Speed()));
    const double slowSpeed = baseSpeed * ZOMBIE_SLOW_SPEED_FACTOR;
    const double standCs = std::max(0, std::max(zombie->FreezeCountdown(), zombie->FixationCountdown()));
    const double slowRemainCs = std::max(0.0, std::max(0, zombie->SlowCountdown()) - standCs);
    const double slowPx = slowSpeed * slowRemainCs;  // 减速期间能走的像素
    if (distancePx <= slowPx)
        return standCs + distancePx / slowSpeed;
    return standCs + slowRemainCs + (distancePx - slowPx) / baseSpeed;
}

// ===========================================================================
//  帧内缓存：只跟场上植物 / 子弹有关的东西，一帧只扫一遍
// ===========================================================================

// 已唤醒的大喷菇 / 忧郁菇（植物数组顺序）
struct ANaturalPlantEntry {
    bool isGloom = false;
    int row = 0;   // 0 起
    int x = 0;     // Abscissa
};

// 已唤醒的冰瓜（植物数组顺序）
struct AMelonEntry {
    int row = 0;
};

// 刚种下（IDLE）的倭瓜按预测模块推出来的命中列表
struct ASquashIdleHit {
    AZombie* zombie = nullptr;
    int eta = 0;
    bool jacked = false;   // AIsPlantJacked(倭瓜命中框, eta)
};

// 一个威胁来源：正在等生效的灰烬 / 倭瓜 / 出膛前的炮（植物数组顺序）
struct AThreatSourceEntry {
    enum Kind : int { ASH, SQUASH, COB };
    Kind kind = ASH;
    APlant* plant = nullptr;
    int row = 0;             // 植物所在行（0 起）
    int base = 0;            // APlantBaseType
    bool sleeping = false;
    int eta = 0;             // 生效时间
    bool jacked = false;     // 生效前会被小丑炸掉
    bool valid = false;      // 倭瓜 / 炮：与僵尸无关的范围检查是否通过
    // 灰烬
    int rowRange = 0;
    int centerX = 0;
    int centerY = 0;
    int radius = 0;
    // 倭瓜
    int state = 0;
    int landingX = 0;
    int targetId = 0;
    int y = 0;               // Ordinate
    std::vector<ASquashIdleHit> idleHits;
    // 炮
    int targetX = 0;
    int targetY = 0;
    int targetRow = 0;
};

// 已经在空中的炮弹（子弹数组顺序）
struct AShellEntry {
    AProjectile* projectile = nullptr;
    int row = 0;
    int eta = 0;
    int centerX = 0;
    int centerY = 0;
};

// 每只僵尸一帧只算一次的结果
struct AZombieThreatMemo {
    bool hasThreats = false;                       // threatTime = -1、includeSquash、ignoreSleeping 的列表
    std::vector<AZombieIncomingThreat> threats;
    bool hasNatural = false;                       // 默认 ANaturalOutputOptions 的自然输出
    ANaturalOutputInfo natural;
};

struct AThreatFrameCache {
    AFrameStamp stamp;
    bool built = false;
    std::vector<AJackEntry> jacks;
    ARowFrontInfo rowFront[7];                     // 行号 1 起，0 号不用
    std::vector<ANaturalPlantEntry> naturalPlants;
    std::vector<AMelonEntry> melons;
    std::vector<AThreatSourceEntry> sources;
    std::vector<AShellEntry> shells;
    std::vector<AZombieThreatMemo> zombieMemo;     // 下标 = AFrameSnapshot::zombies 下标
};

inline void __ARebuildThreatFrameCache(AThreatFrameCache& cache, AFrameSnapshot& snap) {
    cache.stamp = snap.stamp;
    cache.built = true;

    // ---- 开盒小丑（僵尸数组顺序）----
    cache.jacks.clear();
    for (const AZombieSnapshot& zombie : snap.zombies) {
        if (zombie.type != AJACK_IN_THE_BOX_ZOMBIE || zombie.state != JACKBOX_POP_STATE)
            continue;
        AJackEntry jack;
        jack.zombie = zombie.zombie;
        jack.countdown = zombie.zombie->StateCountdown();
        jack.centerX = static_cast<int>(zombie.zombie->Abscissa()) + 60;
        jack.centerY = static_cast<int>(zombie.zombie->Ordinate()) + 60;
        cache.jacks.push_back(jack);
    }

    // ---- 各行阵前：一遍扫完 6 行（每行取“数组顺序里第一个 x 最大”的候选，与逐行扫描相同）----
    {
        const APlantSnapshot* best[7] = {};
        const ARowFrontFilter filter = aRowFrontFilter;
        for (const APlantSnapshot& plant : snap.plants) {
            const int row = plant.row + 1;
            if (row < 1 || row > 6)
                continue;
            const int base = plant.baseType;
            if (base != AFUME_SHROOM && base != AGLOOM_SHROOM && base != AWINTER_MELON && base != ACOB_CANNON)
                continue;
            if (plant.sleeping)  // 白天没咖啡豆的蘑菇不算已唤醒
                continue;
            if (filter != nullptr && !filter(plant.plant, row))  // 被脚本排除的不算阵前
                continue;
            if (best[row] == nullptr || plant.x > best[row]->x)
                best[row] = &plant;
        }
        for (int row = 0; row < 7; ++row) {
            ARowFrontInfo front;
            if (best[row] != nullptr) {
                front.found = true;
                front.plant = best[row]->plant;
                front.pumpkin = AFindPlantPtr(snap, row, best[row]->col + 1, APUMPKIN);
                APlant* source = front.pumpkin != nullptr ? front.pumpkin : best[row]->plant;
                front.x = AGetPlantHitBox(source).Right();
            }
            cache.rowFront[row] = front;
        }
    }

    // ---- 已唤醒的大喷菇 / 忧郁菇 / 冰瓜（植物数组顺序）----
    cache.naturalPlants.clear();
    cache.melons.clear();
    for (const APlantSnapshot& plant : snap.plants) {
        if (plant.sleeping)  // 白天没咖啡豆的蘑菇不算输出
            continue;
        if (plant.baseType == AGLOOM_SHROOM || plant.baseType == AFUME_SHROOM) {
            ANaturalPlantEntry entry;
            entry.isGloom = plant.baseType == AGLOOM_SHROOM;
            entry.row = plant.row;
            entry.x = plant.x;
            cache.naturalPlants.push_back(entry);
        } else if (plant.baseType == AWINTER_MELON) {
            AMelonEntry entry;
            entry.row = plant.row;
            cache.melons.push_back(entry);
        }
    }

    // ---- 威胁来源：灰烬 / 倭瓜 / 出膛前的炮（植物数组顺序）----
    cache.sources.clear();
    for (const APlantSnapshot& plantSnap : snap.plants) {
        APlant* plant = plantSnap.plant;
        const int base = plantSnap.baseType;

        // 灰烬：樱桃 / 辣椒 / 毁灭菇
        if (base == ACHERRY_BOMB || base == AJALAPENO || base == ADOOM_SHROOM) {
            AThreatSourceEntry source;
            source.kind = AThreatSourceEntry::ASH;
            source.plant = plant;
            source.row = plantSnap.row;
            source.base = base;
            source.sleeping = plantSnap.sleeping;
            source.eta = plant->ExplodeCountdown();
            source.rowRange = base == ADOOM_SHROOM ? 3 : (base == AJALAPENO ? 0 : 1);
            // 生效前会被小丑炸掉的话，这颗灰烬根本不会炸，不构成威胁
            source.jacked = source.eta > 0 && __AIsPlantJackedBy(cache.jacks, AGetPlantHitBox(plant), source.eta);
            source.centerX = plantSnap.x + 40;
            source.centerY = plantSnap.y + 40;
            source.radius = base == ADOOM_SHROOM ? 250 : 115;
            cache.sources.push_back(std::move(source));
            continue;
        }

        // 倭瓜
        if (base == ASQUASH) {
            AThreatSourceEntry source;
            source.kind = AThreatSourceEntry::SQUASH;
            source.plant = plant;
            source.row = plantSnap.row;
            source.base = base;
            source.state = plantSnap.state;
            source.y = plantSnap.y;
            const int state = plantSnap.state;
            const int countdown = plant->StateCountdown();

            if (state == SQUASH_STATE_AIR || state == SQUASH_STATE_JUMP_DOWN) {
                // 落点已定：p.cannon.x（偏移 0x80）就是砸下时倭瓜自己的 x，游戏里存的是整数
                source.landingX = plant->MRef<int>(0x80);
                source.valid = !(source.landingX < -80 || source.landingX > 880);  // 明显不合理，可能不是这个状态
                if (source.valid) {
                    source.eta = state == SQUASH_STATE_AIR ? countdown + 5 : countdown - 5;
                    source.valid = !(source.eta < 0 || source.eta > 200);
                }
                if (source.valid)
                    source.jacked = __AIsPlantJackedBy(cache.jacks, AGetPlantHitBox(plant), source.eta);
            } else if (state == SQUASH_STATE_LOOK || state == SQUASH_STATE_JUMP_UP) {
                // 已锁定目标但落点还没算出来：用植物记录的目标僵尸 id 判定（偏移 0x12C）
                source.targetId = plant->MRef<int>(0x12C);
                source.eta = state == SQUASH_STATE_LOOK ? countdown + SQUASH_JUMP_UP_CS + SQUASH_AIR_CS +
                                                              SQUASH_LAND_CS
                                                        : countdown + SQUASH_AIR_CS + SQUASH_LAND_CS;
                source.valid = !(source.eta < 0 || source.eta > 200);
                if (source.valid)
                    source.jacked = __AIsPlantJackedBy(cache.jacks, AGetPlantHitBox(plant), source.eta);
            } else if (state == SQUASH_STATE_IDLE) {
                // 刚种下的那一帧：游戏要等下一帧的 update 才会跑 find_target 锁定目标
                // （反编译 plant_squash::update 的 idle 分支：找到目标后才写入目标 id、
                //  cannon.x，并切到 squash_look(countdown 80)），所以这一帧 0x12C 里没有目标 id，
                // 上面两个分支都判不出来。这里用预测模块现推一次，避免“倭瓜放下要过一帧才算威胁”。
                // 预测出来的 eta = 0 + SQUASH_TOTAL_CS = 180，与下一帧 look 分支的
                // countdown(80) + 45 + 50 + 5 = 180 完全衔接，不会跳变。
                // （预测结果只跟场上僵尸有关、跟“问的是哪只僵尸”无关，所以一帧只推一次）
                const AHitBox plantBox = AGetPlantHitBox(plant);
                for (const auto& hit : APredictInstantPlantHits(plantSnap.row + 1, plantSnap.col + 1, ASQUASH)) {
                    ASquashIdleHit idleHit;
                    idleHit.zombie = hit.zombie;
                    idleHit.eta = hit.eta;
                    idleHit.jacked = __AIsPlantJackedBy(cache.jacks, plantBox, hit.eta);
                    source.idleHits.push_back(idleHit);
                }
            }
            cache.sources.push_back(std::move(source));
            continue;
        }

        // 玉米炮：发炮指令一发出就算威胁（炮弹还没出膛的那 205cs）
        //   发炮瞬间游戏把 plant+0x90 置为 206，减到 1 时炮弹出膛（真实游戏 0x466d50 处
        //   mov DWORD PTR [esi+0x90], 0xce），所以剩余出膛时间 = shootCountdown - 1；
        //   出膛之后交给下面“已经在空中的炮弹”那一段接着算。
        if (base == ACOB_CANNON) {
            AThreatSourceEntry source;
            source.kind = AThreatSourceEntry::COB;
            source.plant = plant;
            source.row = plantSnap.row;
            source.base = base;
            const int shootCountdown = plant->ShootCountdown();
            source.valid = shootCountdown > 1;  // 没在发射，或者已经出膛（交给空中的炮弹去算）
            if (source.valid) {
                // 0x80 / 0x84 是瞄准点的 x / y，游戏里存的是【整数】
                // （0x466d92：__ftol 之后 mov DWORD PTR [esi+0x80], eax），不能按 float 读
                source.targetX = plant->MRef<int>(0x80) + 47;
                source.targetY = plant->MRef<int>(0x84);
                source.valid = !(source.targetX < 0 || source.targetX > 880 || source.targetY < 0 || source.targetY > 600);
            }
            if (source.valid) {
                source.targetRow = ARowFromTileY(source.targetY);
                source.valid = source.targetRow >= 0;
            }
            if (source.valid) {
                source.eta = (shootCountdown - 1) + COB_SHELL_FLIGHT_CS;
                // 炮台在出膛前就被炸掉的话炮弹也不会出来（用整个 eta 保守判定）
                source.jacked = __AIsPlantJackedBy(cache.jacks, AGetPlantHitBox(plant), source.eta);
            }
            cache.sources.push_back(std::move(source));
            continue;
        }
    }

    // ---- 已经在空中的炮弹（子弹数组顺序）----
    // （AvZ 只预定义了植物 / 僵尸 / 卡槽等的 alive 过滤器，炮弹要自己遍历）
    cache.shells.clear();
    AMainObject* mainObject = snap.stamp.mainObject;
    if (mainObject != nullptr) {
        AProjectile* projectileArray = mainObject->ProjectileArray();
        const int projectileCount = std::min(mainObject->ProjectileTotal(), mainObject->ProjectileLimit());
        for (int i = 0; i < projectileCount; ++i) {
            AProjectile& projectile = projectileArray[i];
            if (projectile.IsDisappeared())
                continue;
            if (projectile.Type() != COB_PROJECTILE_TYPE)
                continue;
            const int row = projectile.CobTargetRow();
            if (row < 0 || row >= aFieldInfo.nRows)
                continue;

            AShellEntry shell;
            shell.projectile = &projectile;
            shell.row = row;
            shell.eta = COB_SHELL_FLIGHT_CS - projectile.ExistTime();
            if (shell.eta < 0)
                shell.eta = 0;
            shell.centerX = projectile.CobTargetAbscissa() + COB_IMPACT_X_OFFSET;
            const int nearCol = std::clamp((shell.centerX - 40) / 80, 0, 8);
            // 格子像素查快照里的表（值与 AAsm::GridToOrdinate 相同）
            shell.centerY = (row < snap.nRows ? snap.cellY[row][nearCol] : AAsm::GridToOrdinate(row, nearCol)) + 40;
            cache.shells.push_back(shell);
        }
    }

    // ---- 每只僵尸的备忘录 ----
    cache.zombieMemo.assign(snap.zombies.size(), AZombieThreatMemo{});
}

// 本帧的威胁缓存；跟随 AGetFrameSnapshot 的标签重建
inline AThreatFrameCache& AGetThreatFrameCache() {
    static AThreatFrameCache cache;
    AFrameSnapshot& snap = AGetFrameSnapshot();
    if (!cache.built || !(cache.stamp == snap.stamp))
        __ARebuildThreatFrameCache(cache, snap);
    return cache;
}

inline bool AIsPlantJacked(const AHitBox& box, int eta) {
    return __AIsPlantJackedBy(AGetThreatFrameCache().jacks, box, eta);
}

inline ARowFrontInfo ARowFront(int row) {
    if (row >= 1 && row <= 6)
        return AGetThreatFrameCache().rowFront[row];
    return __AComputeRowFront(AGetFrameSnapshot(), row);  // 行号不在 1..6：原逻辑找不到任何植物
}

inline ARowFrontInfo __ACachedRowFront(const AThreatFrameCache& cache, int row) {
    if (row >= 1 && row <= 6)
        return cache.rowFront[row];
    return ARowFrontInfo{};
}

// 自然输出：在给定的帧缓存上直接算（与逐次扫描全场植物的写法逐句对应）
inline ANaturalOutputInfo __AComputeNaturalOutput(AZombie* zombie, const ANaturalOutputOptions& options,
                                                  const AThreatFrameCache& cache) {
    ANaturalOutputInfo result;
    if (zombie == nullptr)
        return result;

    const ARowFrontInfo front = __ACachedRowFront(cache, zombie->Row() + 1);
    result.valid = true;
    result.hasFront = front.found;
    if (front.found) {
        result.frontX = front.x;
    } else {
        result.frontX = AZombieHomeX(zombie->Type());
        result.isHouse = true;
    }

    const AHitBox box = AGetZombieHitBox(zombie);
    const double boxWidth = box.width;
    const double leftNow = box.x;
    const double leftFront = result.frontX;

    // ---- 有效速度：基础速度（含随机数）+ 减速 / 冰冻 ----
    // 减速期间速度 ×0.4，走完减速剩余帧数后恢复原速；
    // 冰冻 / 黄油固定期间原地不动（这段时间也要吃输出）。
    const double baseSpeed = std::max(0.05f, std::fabs(zombie->Speed()));
    const double slowSpeed = baseSpeed * ZOMBIE_SLOW_SPEED_FACTOR;
    // 当前 debuff；options 里给了 assume*（“假设现在用冰”）时取更长的那一份
    const int debuffNow = std::max(zombie->FreezeCountdown(), zombie->FixationCountdown());
    const double standCs = std::max(0, std::max(debuffNow, options.assumeFreezeCs));
    const double slowRemainCs =
        std::max(0.0, std::max(std::max(0, zombie->SlowCountdown()), options.assumeSlowCs) - standCs);
    const double slowPx = slowSpeed * slowRemainCs;  // 减速期间能走的像素

    // 走 distance 像素用掉多少帧（先减速段、后原速段）
    const auto travelCs = [&](double distance) {
        if (distance <= 0.0)
            return 0.0;
        if (distance <= slowPx)
            return distance / slowSpeed;
        return slowRemainCs + (distance - slowPx) / baseSpeed;
    };

    const double walkPx = std::max(0.0, leftNow - leftFront);  // 到阵前还有多少像素
    result.walkCs = standCs + travelCs(walkPx);

    if (IsGargantuarZombie(zombie->Type())) {
        // 走到阵前后还要等锤击落地，这段时间同样吃输出
        const AGargantuarSmashInfo smash = AGetGargantuarSmashInfo(zombie, 0.0, options.gargantuarWindupCs);
        result.attackCs = std::max(0.0, smash.impactCs);
    } else {
        result.attackCs = std::max(0.0, static_cast<double>(options.extraFrontlineCs));
    }

    // 大喷菇 / 忧郁菇的伤害标记是 9（DAMAGES_GROUND | DAMAGES_DOG），冰瓜是 13（多一个
    // DAMAGES_SUBMERGED，能打水里的潜水僵尸）。按游戏 Zombie::EffectedByDamage 判一次：
    // 这一帧打不到的僵尸（飞行中的气球、垂死、钻地、被魅惑等）不算输出；走位 / 阵前时间照旧算，
    // 供显示与调参看。这是游戏对该伤害类型的通用规则，不限定僵尸种类 —— 凡是被打得到的僵尸都计入；
    // 某类僵尸值多少权重由调用方用 aThreatTypeFactor / typeFactorDefault 决定。
    const bool hittableByFumeGloom = ACanBeAttacked(zombie, FLAG_GROUND | FLAG_DOG);
    const bool hittableByMelon = ACanBeAttacked(zombie, FLAG_GROUND | FLAG_LURKING_SNORKEL | FLAG_DOG);

    if (hittableByFumeGloom) {
        const int zombieRow = zombie->Row();
        for (const ANaturalPlantEntry& plant : cache.naturalPlants) {
            const bool isGloom = plant.isGloom;
            if (isGloom ? !options.includeGloom : !options.includeFume)
                continue;

            const int rowDiff = std::abs(plant.row - zombieRow);
            if (isGloom) {
                const int limit = options.includeGloomAdjacentRows ? 1 : 0;
                if (rowDiff > limit)
                    continue;
            } else if (rowDiff != 0) {
                continue;  // 大喷菇只打自己这行
            }

            const double rangeLeft =
                plant.x + (isGloom ? options.gloomRangeStartPx : options.fumeRangeStartPx);
            const double rangeRight =
                plant.x + (isGloom ? options.gloomRangeEndPx : options.fumeRangeEndPx);
            const double dps = isGloom ? options.gloomDps : options.fumeDps;

            // 路上经过攻击框的时间：判定框左边从 leftNow 走到 leftFront 期间，
            // 落在射程内的那一段用“走过的像素距离”换算成时间（含减速段）
            const double dLo = std::max(0.0, leftNow - rangeRight);
            const double dHi = std::min(walkPx, leftNow - rangeLeft + boxWidth);
            const double walkInRangeCs = dHi > dLo ? travelCs(dHi) - travelCs(dLo) : 0.0;
            // 冰冻 / 黄油原地不动时，只要已经在射程里就继续吃输出
            const bool inRangeNow = leftNow <= rangeRight && leftNow + boxWidth >= rangeLeft;
            const double standInRangeCs = (standCs > 0.0 && inRangeNow) ? standCs : 0.0;
            // 走到阵前之后（含巨人锤击前摇）仍在其范围内的时间
            const double stayPx = ATraversedOverlapPx(leftFront, leftFront, boxWidth, rangeLeft, rangeRight);
            const double stayInRangeCs = stayPx > 0.0 ? result.attackCs : 0.0;

            result.damage +=
                dps * (standInRangeCs + walkInRangeCs + stayInRangeCs) / 100.0;  // dps 是每秒伤害，cs 要 /100
            if (isGloom)
                result.gloomDps += dps;
            else
                result.fumeDps += dps;
        }
    }

    // ---- 冰瓜：最简口径 ----
    //   射程是整行（反编译里投手走 GetPlantAttackRect 的 default：Rect(mX+60, mY, 800, mHeight)），
    //   僵尸一进场就在射程内，所以整段“走位 + 阵前”时间都算输出：
    //     本行每株每 300cs 给 30 点；相邻行每株每 300cs 给 20 点；其它行不算。
    if (options.includeWinterMelon && hittableByMelon) {
        const double exposedCs = result.walkCs + result.attackCs;  // 全程都在射程内
        const int zombieRow = zombie->Row();
        for (const AMelonEntry& plant : cache.melons) {
            const int rowDiff = std::abs(plant.row - zombieRow);
            double dps = 0.0;
            if (rowDiff == 0)
                dps = options.melonSameRowDps;
            else if (rowDiff == 1)
                dps = options.melonAdjacentRowDps;
            else
                continue;
            result.damage += dps * exposedCs / 100.0;
            result.melonDps += dps;
        }
    }
    return result;
}

// 自然输出（大喷菇 / 忧郁菇 / 冰瓜）对某个僵尸的伤害；默认口径一帧只算一次
inline ANaturalOutputInfo AGetNaturalOutputInfo(AZombie* zombie, const ANaturalOutputOptions& options = {}) {
    ANaturalOutputInfo result;
    if (zombie == nullptr)
        return result;

    static const ANaturalOutputOptions defaultOptions{};
    AThreatFrameCache& cache = AGetThreatFrameCache();
    const int slot = AGetFrameSnapshot().Slot(zombie);
    if (slot >= 0 && options == defaultOptions) {
        if (!cache.zombieMemo[slot].hasNatural) {
            const ANaturalOutputInfo computed = __AComputeNaturalOutput(zombie, options, cache);
            AZombieThreatMemo& memo = cache.zombieMemo[slot];
            memo.natural = computed;
            memo.hasNatural = true;
        }
        return cache.zombieMemo[slot].natural;
    }
    return __AComputeNaturalOutput(zombie, options, cache);
}

// 列出该僵尸即将受到的即时输出威胁（按生效时间升序），在给定的帧缓存上直接算
inline void __ACollectIncomingThreats(AZombie* zombie, int threatTime, const AThreatQueryOptions& options,
                                      const AThreatFrameCache& cache, std::vector<AZombieIncomingThreat>& result) {
    result.clear();
    const int hp = AZombieHp(zombie);
    auto pushThreat = [&](APlant* plant, AProjectile* projectile, APlantType source, int eta, int damage) {
        if (eta < 0)
            return;
        if (threatTime >= 0 && eta > threatTime)
            return;
        AZombieIncomingThreat threat;
        threat.zombie = zombie;
        threat.plant = plant;
        threat.projectile = projectile;
        threat.source = source;
        threat.eta = eta;
        threat.damage = damage;
        threat.lethal = hp < damage;
        result.push_back(threat);
    };

    const int zombieRow = zombie->Row();
    const AHitBox zombieBox = AGetZombieHitBox(zombie);

    // ---- 1) 场上植物：灰烬 / 倭瓜 / 炮（出膛阶段），植物数组顺序 ----
    for (const AThreatSourceEntry& source : cache.sources) {
        switch (source.kind) {
        case AThreatSourceEntry::ASH: {
            if (options.ignoreSleeping && source.sleeping)
                continue;
            if (source.eta <= 0)
                continue;
            if (std::abs(zombieRow - source.row) > source.rowRange)
                continue;
            if (source.jacked)  // 生效前会被小丑炸掉的话，这颗灰烬根本不会炸，不构成威胁
                continue;
            if (source.base != AJALAPENO) {  // 辣椒是整行判定，不做几何
                const AHitBox box = AHitBoxAfterCs(zombieBox, zombie, source.eta);
                if (!AHitBoxOverlapCircle(box, source.centerX, source.centerY, source.radius))
                    continue;
            }
            pushThreat(source.plant, nullptr, static_cast<APlantType>(source.base), source.eta, 1800);
            continue;
        }
        case AThreatSourceEntry::SQUASH: {
            if (!options.includeSquash)
                continue;
            if (zombieRow != source.row)  // 倭瓜只砸自己这一行
                continue;
            const int state = source.state;
            if (state == SQUASH_STATE_AIR || state == SQUASH_STATE_JUMP_DOWN) {
                if (!source.valid || source.jacked)
                    continue;
                AHitBox killBox;
                killBox.x = source.landingX + SQUASH_ATTACK_X_OFFSET;
                killBox.y = source.y;
                killBox.width = SQUASH_ATTACK_WIDTH;
                killBox.height = 80;
                const AHitBox box = AHitBoxAfterCs(zombieBox, zombie, source.eta);
                const float need = zombie->Type() == AFOOTBALL_ZOMBIE ? -static_cast<float>(SQUASH_FOOTBALL_GRACE)
                                                                      : 0.0f;
                if (!(AHitBoxOverlapLenX(killBox, box) > need))
                    continue;
                pushThreat(source.plant, nullptr, ASQUASH, source.eta, 1800);
            } else if (state == SQUASH_STATE_LOOK || state == SQUASH_STATE_JUMP_UP) {
                if (source.targetId != static_cast<int>(zombie->Id()))
                    continue;
                if (!source.valid || source.jacked)
                    continue;
                pushThreat(source.plant, nullptr, ASQUASH, source.eta, 1800);
            } else if (state == SQUASH_STATE_IDLE) {
                for (const ASquashIdleHit& hit : source.idleHits) {
                    if (hit.zombie != zombie)
                        continue;
                    if (hit.jacked)
                        break;
                    pushThreat(source.plant, nullptr, ASQUASH, hit.eta, 1800);
                    break;
                }
            }
            continue;
        }
        case AThreatSourceEntry::COB: {
            if (!source.valid)
                continue;
            if (std::abs(zombieRow - source.targetRow) > COB_ROW_RANGE)
                continue;
            if (source.jacked)
                continue;
            const AHitBox box = AHitBoxAfterCs(zombieBox, zombie, source.eta);
            if (!AHitBoxOverlapCircle(box, source.targetX + COB_IMPACT_X_OFFSET, source.targetY + 40, COB_IMPACT_RADIUS))
                continue;
            pushThreat(source.plant, nullptr, ACOB_CANNON, source.eta, 1800);
            continue;
        }
        }
    }

    // ---- 2) 已经在空中的炮弹（子弹数组顺序）----
    for (const AShellEntry& shell : cache.shells) {
        if (std::abs(zombieRow - shell.row) > COB_ROW_RANGE)
            continue;
        if (threatTime >= 0 && shell.eta > threatTime)
            continue;
        const AHitBox box = AHitBoxAfterCs(zombieBox, zombie, shell.eta);
        if (!AHitBoxOverlapCircle(box, shell.centerX, shell.centerY, COB_IMPACT_RADIUS))
            continue;
        pushThreat(nullptr, shell.projectile, ACOB_CANNON, shell.eta, 1800);
    }

    std::sort(result.begin(), result.end(),
              [](const AZombieIncomingThreat& lhs, const AZombieIncomingThreat& rhs) { return lhs.eta < rhs.eta; });
}

// 能不能用“每只僵尸一帧算一次”的那份列表（threatTime < 0 且不过滤倭瓜 / 睡觉植物）
inline bool __ACanUseThreatMemo(int threatTime, const AThreatQueryOptions& options) {
    return threatTime < 0 && options.includeSquash && options.ignoreSleeping;
}

// 该僵尸即将受到的伤害总和（AGetZombieThreatHp 用；不复制列表）
inline int __AIncomingDamageSum(AZombie* zombie, int threatTime, const AThreatQueryOptions& options) {
    AThreatFrameCache& cache = AGetThreatFrameCache();
    const int slot = AGetFrameSnapshot().Slot(zombie);
    int total = 0;
    if (slot >= 0 && __ACanUseThreatMemo(threatTime, options)) {
        if (!cache.zombieMemo[slot].hasThreats) {
            std::vector<AZombieIncomingThreat> computed;
            __ACollectIncomingThreats(zombie, -1, options, cache, computed);
            AZombieThreatMemo& memo = cache.zombieMemo[slot];
            memo.threats = std::move(computed);
            memo.hasThreats = true;
        }
        for (const auto& threat : cache.zombieMemo[slot].threats)
            total += threat.damage;
        return total;
    }
    std::vector<AZombieIncomingThreat> threats;
    __ACollectIncomingThreats(zombie, threatTime, options, cache, threats);
    for (const auto& threat : threats)
        total += threat.damage;
    return total;
}

}  // namespace aThreat

// ---------------------------------------------------------------------------
// 对外接口
// ---------------------------------------------------------------------------

// 列出该僵尸即将受到的即时输出威胁（按生效时间升序）
// threatTime < 0：不过滤；threatTime >= 0：只返回 eta <= threatTime 的威胁
inline std::vector<AZombieIncomingThreat> AGetZombieIncomingThreats(
    AZombie* zombie, int threatTime = -1, const AThreatQueryOptions& options = {}) {
    using namespace aThreat;

    std::vector<AZombieIncomingThreat> result;
    if (zombie == nullptr)
        return result;

    AThreatFrameCache& cache = AGetThreatFrameCache();
    const int slot = AGetFrameSnapshot().Slot(zombie);
    if (slot >= 0 && __ACanUseThreatMemo(threatTime, options)) {
        if (!cache.zombieMemo[slot].hasThreats) {
            std::vector<AZombieIncomingThreat> computed;
            __ACollectIncomingThreats(zombie, -1, options, cache, computed);
            AZombieThreatMemo& memo = cache.zombieMemo[slot];
            memo.threats = std::move(computed);
            memo.hasThreats = true;
        }
        return cache.zombieMemo[slot].threats;
    }
    __ACollectIncomingThreats(zombie, threatTime, options, cache, result);
    return result;
}

// 威胁血量：当前血量（本体 + 一类防具）
//          − 即将到来的灰烬 / 炮弹伤害
//          − 自然输出（大喷菇 / 忧郁菇）在路上与阵前打掉的量
//          下限 0
inline int AGetZombieThreatHp(AZombie* zombie, int threatTime = -1, const AThreatQueryOptions& options = {}) {
    if (zombie == nullptr)
        return 0;
    int hp = aInstantPlant::AZombieHp(zombie);
    hp -= aThreat::__AIncomingDamageSum(zombie, threatTime, options);
    if (options.includeNaturalOutput)
        hp -= static_cast<int>(std::ceil(aThreat::AGetNaturalOutputInfo(zombie, options.naturalOptions).damage));
    return std::max(0, hp);
}

// 该僵尸是否已经被后面的输出锁定（威胁血量 <= 0）
inline bool AIsZombieDoomed(AZombie* zombie, int threatTime = -1, const AThreatQueryOptions& options = {}) {
    if (zombie == nullptr)
        return false;
    return AGetZombieThreatHp(zombie, threatTime, options) <= 0;
}

// 整场扫描：已经被锁定的僵尸（可以拿去做“不要再对它输出”的过滤）
inline std::vector<AZombie*> AGetDoomedZombies(int threatTime = -1, const AThreatQueryOptions& options = {}) {
    std::vector<AZombie*> result;
    for (auto& zombie : aAliveZombieFilter) {
        if (AIsZombieDoomed(&zombie, threatTime, options))
            result.push_back(&zombie);
    }
    return result;
}

// 调参用：把某个僵尸的威胁明细打到日志
inline void ALogZombieThreats(AZombie* zombie, int threatTime = -1, const AThreatQueryOptions& options = {}) {
    if (zombie == nullptr)
        return;
    const auto threats = AGetZombieIncomingThreats(zombie, threatTime, options);
    aLogger->Info("[Threat] 僵尸 addr={} type={} row={} 血量={} 威胁数={} 威胁血量={}",
                  static_cast<const void*>(zombie), zombie->Type(), zombie->Row() + 1,
                  aInstantPlant::AZombieHp(zombie), threats.size(), AGetZombieThreatHp(zombie, threatTime, options));
    for (const auto& threat : threats) {
        aLogger->Info("    来源={} eta={}cs 伤害={} lethal={} 植物={} 炮弹={}", threat.source, threat.eta, threat.damage,
                      threat.lethal, static_cast<const void*>(threat.plant), static_cast<const void*>(threat.projectile));
    }
}

#endif  // __A_ZOMBIE_THREAT_PREDICT_H__
