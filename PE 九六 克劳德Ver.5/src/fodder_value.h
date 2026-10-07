#ifndef __A_FODDER_VALUE_H__
#define __A_FODDER_VALUE_H__

// ============================================================================
//  垫材价值
//
//  垫材 = 放一个植物去拦僵尸。先算「垫材存活时间」，再由存活时间算「垫材价值」：
//
//  “本帧能交互”指垫材防御框（aThreat::APlantHitBoxAt）与僵尸攻击框（AZombieAttackBox）
//  现在重合；还在外面走的、已经走过去的一概不算 —— 垫材只为“眼前这只”服务。
//
//  一、存活时间 = min 以下几个“结束事件”（植物种类不同会忽略其中一些，见 SelfEffectCs /
//      CanBeCrushed 两张表）：
//      · 自身生效：灰烬 / 寒冰菇 / 倭瓜 / 三叶草会自己消失（SelfEffectCs），<0 表示不会
//      · 被啃完：本帧就在啃的僵尸一起啃，时长 = ceil(垫材血量 / 每帧总啃食量)。
//                减速让啃食速度减半；冰冻 / 黄油期间被固定，根本不啃
//      · 被巨人砸：只看本帧能砸到这块垫材的巨人（红眼 / 白眼共用同一套），正在举锤的
//                读它当前的砸击动画相位算“还有多少帧砸下来”；还在走的按“举锤到砸下”兜底
//      · 被车碾压：冰车 / 篮球车压过来直接毁掉（0 帧）；灰烬 / 倭瓜 / 寒冰菇等种子类压不坏
//      · 被小丑炸：小丑进入引爆状态、爆炸半径盖到这块垫材时，按它的引爆倒计时算
//
//  二、垫材价值 = 拖住的这段时间里，自然输出（大喷菇 / 忧郁菇，按平均 dps）能打掉多少血，
//      按威胁血量封顶，再乘 距离系数 × 种类修正 × 速度系数（和樱桃 / 倭瓜 / 冰同一套折算）
//      · 啃食僵尸：拖住时长 = 垫材存活时间（一直挡着它啃到垫材死）
//      · 巨人：砸下发生在存活时间内（被垫到）的，每垫到一个固定拖延它一个完整锤击循环
//
//  —— 反编译依据（PvZ 1.0.0.1051，见 PlantsVsZombies-decompilation）——
//   · 啃食：Zombie::CheckIfPreyCaught —— 每 4cs 啃一口、每口 4 点（Zombie.h 的
//     TICKS_BETWEEN_EATS = DAMAGE_PER_EAT = 4），即正常 1 点/帧；mChilledCounter > 0 时
//     间隔翻倍（0.5 点/帧）；mIceTrapCounter / mButteredCounter > 0 时 IsImmobilizied()
//     为真，连 CheckIfPreyCaught 都不调用（0 点/帧）
//   · 冰车 / 篮球车：zombie_system::crush_plant → damage::set_smashed，
//     被压的植物直接毁掉；但 damage::can_attack_plant(crush) 明确排除了
//       樱桃 / 辣椒 / 三叶草 / 倭瓜 / （醒着的）毁灭菇 / 寒冰菇
//     —— 这些植物不会被车秒杀，其中倭瓜的存活时间就是它的生效时间
//   · 灰烬与寒冰菇被巨人砸击（gargantuar.cpp → set_smashed）会被直接引爆，
//     仍然正常生效，所以不用改灰烬价值那一套
//   · 巨人砸击结算时机：gargantuar_smash 状态下 reanim 进度过 0.64 那一帧；
//     相位估算已下沉到 zombie_threat_predict.h（aThreat::AGetGargantuarSmashInfo），
//     读内存动画相位并按逐帧进度差换算，减速把动画拉长会自动体现
//   · 地刺 / 地刺王：冰车、篮球车压过来时由地刺自己的 range_attack 处理，
//     攻击后地刺自身消失（地刺王掉一层）
//   · 三叶草寿命 250cs（同时它也怕砸）
//   · 小丑：走 kill_plants，所有垫材都可能被小丑的爆炸清掉
//
//  帧内缓存（性能）：僵尸攻击框一帧只算一次；每格只遍历本行的僵尸（原逻辑也只收本行的）；
//  威胁度 / 自然输出走 zombie_threat_level.h / zombie_threat_predict.h 的帧内缓存。
// ============================================================================

#include "zombie_threat_level.h"

#include <avz.h>
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

struct AFodderOptions {
    double biteDamage = 4.0;        // 每次啃食判定 4 点（Zombie.h 的 DAMAGE_PER_EAT）
    int bitePeriod = 4;             // 每 4cs 判定一次（TICKS_BETWEEN_EATS）→ 正常 1 点/帧
    bool slowedBiteHalf = true;     // 减速僵尸间隔翻倍（啃食速度减半）
    int vehicleSurvivalCs = 0;      // 会被车秒杀的植物，存活时间按这个算（默认 0）
    int gargantuarWindupCs = 130;   // 巨人还在走、够到植物后到砸下的时间（兜底）
    double smashImpactRate = 0.64;  // 砸击动画进度到多少时结算（反编译 gargantuar.cpp）
    double gargantuarFallbackCycleCs = 206.0;  // 量不到动画速度时的“一个锤击循环”兜底
    int plantHpOverride = 0;        // > 0 时直接用它当垫材血量
    // 垫材价值里用到的“威胁度”口径（种类修正默认值 / 距离单位等），让垫材价值和其它价值同口径
    AThreatLevelOptions levelOptions;
    // 自然输出口径（大喷菇 / 忧郁菇的平均 dps），价值就是“这段拖延时间里自然输出打掉多少血”
    ANaturalOutputOptions naturalOptions;
};

struct AFodderZombieInfo {
    AZombie* zombie = nullptr;
    double threatHp = 0.0;      // 威胁血量
    double distanceFactor = 1.0;
    double typeFactor = 1.0;
    double speedFactor = 1.0;
    double naturalDps = 0.0;    // 当前能打到它的自然输出（大喷菇 + 忧郁菇）合计 dps
    int holdCs = 0;             // 这块垫材能拖住它多少帧（啃食 = 存活时间；巨人 = 一个锤击循环）
    double damage = 0.0;        // 拖延时间里自然输出能打掉它多少血（按威胁血量封顶）
    bool isVehicle = false;     // 冰车 / 篮球车
    bool isGargantuar = false;  // 巨人（红眼 / 白眼）
    int smashImpactCs = 0;      // 巨人：读动画相位得到“这次举槌还有多少帧砸下来”
    double smashCycleCs = 0.0;  // 巨人：一整个锤击循环时长（价值里按这个算拖延）
};

struct AFodderValue {
    bool valid = false;
    APlantType type = APUFF_SHROOM;
    int row = 1;
    int col = 1;
    int plantHp = 0;          // 垫材血量
    int selfEffectCs = -1;    // 自身生效时间（<0 表示不会自己消失）
    int surviveCs = 0;        // ★ 垫材存活时间
    std::string endReason;    // 存活时间被什么决定
    std::vector<AFodderZombieInfo> zombies;
    double value = 0.0;       // ★ 垫材价值
};

namespace aFodder {

using namespace aInstantPlant;

// 垫材血量表（PvZ 常规数值；表里没有的按 300 处理，可用 options.plantHpOverride 覆盖）
inline int PlantHp(APlantType type) {
    switch (type) {
    case AWALL_NUT:
    case APUMPKIN:
        return 4000;
    case ATALL_NUT:
        return 8000;
    case AGARLIC:
        return 400;
    case ASPIKEWEED:
    case ASPIKEROCK:
        return 100;
    default:
        return 300;  // 小喷菇 / 阳光菇 / 向日葵 / 豌豆 / 灰烬 / 倭瓜… 都是 300
    }
}

// 自身生效（或自行消失）的时间；<0 表示不会自己消失
//   灰烬 100/100/（毁灭菇夜晚 100、白天 299）；寒冰菇 夜晚 100、白天 299；
//   倭瓜 182；三叶草 250
inline int SelfEffectCs(APlantType type, bool imitator) {
    int cs = -1;
    switch (type) {
    case ACHERRY_BOMB:
    case AJALAPENO:
        cs = 100;
        break;
    case ADOOM_SHROOM:
    case AICE_SHROOM:
        cs = aFieldInfo.isNight ? 100 : 299;
        break;
    case ASQUASH:
        cs = 182;
        break;
    case ABLOVER:
        cs = 250;
        break;
    default:
        cs = -1;
        break;
    }
    if (cs > 0 && imitator)
        cs += 320;  // 模仿者变身延迟
    return cs;
}

// 会不会被冰车 / 篮球车秒杀（damage::can_attack_plant 的 crush 分支取反）
inline bool CanBeCrushed(APlantType type) {
    switch (type) {
    case ACHERRY_BOMB:
    case AJALAPENO:
    case ABLOVER:
    case ASQUASH:
    case ADOOM_SHROOM:
    case AICE_SHROOM:
        return false;
    default:
        return true;
    }
}

inline bool IsVehicle(int zombieType) {
    return zombieType == AZOMBONI || zombieType == ACATAPULT_ZOMBIE;
}

inline bool IsGargantuar(int zombieType) {
    return aThreat::IsGargantuarZombie(zombieType);
}

// 两个矩形框有没有重合（用来判断垫材防御框 ∩ 僵尸攻击框）
inline bool ARectOverlap(const aThreat::AHitBox& lhs, const aThreat::AHitBox& rhs) {
    return lhs.x < rhs.Right() && rhs.x < lhs.Right() && lhs.y < rhs.Bottom() && rhs.y < lhs.Bottom();
}

// 僵尸攻击框：复刻 object/zombie.cpp::get_attack_box
//   绝对坐标 = 僵尸本体坐标（int_x = 0x08、int_y = 0x0C）+ 该类型 attack_box 的 (x, y) 偏移。
//   注意：游戏【不把这个框存在内存里】（AvZ 的 AttackAbscissa(0x9C) 读出来是空的，
//   之前按它算就恒为 0），所以必须按类型查表算。表来自各 zombie 的 init：
//     基类默认                          {x:50,  y:0,   w:20, h:115}
//     普通/旗帜/路障/铁桶/铁门/鸭子圈     {x:20,  y:0,   w:50, h:115}
//     梯子 {10,0,50,115}；报纸/小丑/雪人 {20,0,50,115}；气球 {20,30,50,115}；跳跳 {20,17,50,115}
//     撑杆 {50,0,20,115}；潜水 {-5,0,55,115}；海豚 {30,0,30,115}
//     冰车 / 篮球车 {10,-13,133,140}；巨人（白眼/红眼）{-30,-38,89,154}；蹦极 {0,0,0,0}
//   腾空状态（撑杆跳 / 海豚跳）会被覆盖成 {-40, 0, 100, 115}
inline aThreat::AHitBox AZombieAttackBox(AZombie* zombie) {
    using namespace aThreat;
    AHitBox box;
    if (zombie == nullptr) {
        box.width = box.height = 0;
        return box;
    }

    int x = 50, y = 0, w = 20, h = 115;  // 基类默认
    switch (zombie->Type()) {
    case AZOMBIE:
    case AFLAG_ZOMBIE:
    case ACONEHEAD_ZOMBIE:
    case ABUCKETHEAD_ZOMBIE:
    case ASCREEN_DOOR_ZOMBIE:
    case ADUCKY_TUBE_ZOMBIE:
    case ANEWSPAPER_ZOMBIE:
    case AJACK_IN_THE_BOX_ZOMBIE:
    case AZOMBIE_YETI:
        x = 20;
        w = 50;
        break;
    case ALADDER_ZOMBIE:
        x = 10;
        w = 50;
        break;
    case ABALLOON_ZOMBIE:
        x = 20;
        y = 30;
        w = 50;
        break;
    case APOGO_ZOMBIE:
        x = 20;
        y = 17;
        w = 50;
        break;
    case APOLE_VAULTING_ZOMBIE:
        x = 50;
        w = 20;
        break;
    case ASNORKEL_ZOMBIE:
        x = -5;
        w = 55;
        break;
    case ADOLPHIN_RIDER_ZOMBIE:
        x = 30;
        w = 30;
        break;
    case AZOMBONI:
    case ACATAPULT_ZOMBIE:
        x = 10;
        y = -13;
        w = 133;
        h = 140;
        break;
    case AGARGANTUAR:
    case AGIGA_GARGANTUAR:
        x = -30;
        y = -38;
        w = 89;
        h = 154;
        break;
    case ABUNGEE_ZOMBIE:
        x = 0;
        y = 0;
        w = 0;
        h = 0;
        break;
    default:
        break;
    }

    const int state = zombie->State();
    if (state == aInstantPlant::STATE_POLE_JUMPING || state == aInstantPlant::STATE_DOLPHIN_JUMP) {
        x = -40;
        y = 0;
        w = 100;
        h = 115;
    }

    int left = x;
    if (aInstantPlant::AIsWalkingRight(zombie))  // 镜像（get_attack_box 里的 hit_box.offset_x = 120）
        left = aInstantPlant::HIT_BOX_MIRROR_ANCHOR - w - x;

    box.x = zombie->MRef<int>(0x8) + left;  // 僵尸本体 int_x
    box.y = zombie->MRef<int>(0xC) + y;     // 僵尸本体 int_y
    box.width = w;
    box.height = h;
    return box;
}

// 该僵尸当前一帧啃掉的垫材血量（复刻 Zombie::CheckIfPreyCaught）
//   · 冰冻（FreezeCountdown = mIceTrapCounter）/ 黄油（FixationCountdown = mButteredCounter）
//     期间 IsImmobilizied() 为真，游戏根本不跑啃食逻辑 → 0
//   · 减速（SlowCountdown = mChilledCounter）期间啃食间隔翻倍 → ×0.5
inline double BitePerFrame(AZombie* zombie, const AFodderOptions& options) {
    if (zombie->FreezeCountdown() > 0 || zombie->FixationCountdown() > 0)
        return 0.0;
    double damage = options.biteDamage;
    if (options.slowedBiteHalf && zombie->SlowCountdown() > 0)
        damage *= 0.5;
    return damage / static_cast<double>(std::max(1, options.bitePeriod));
}

// ---- 帧内缓存：僵尸攻击框一帧只算一次 ----
struct AFodderFrameCache {
    AFrameStamp stamp;
    bool built = false;
    std::vector<AHitBox> attackBox;  // 下标 = AFrameSnapshot::zombies 下标
    std::vector<char> hasAttackBox;
};

inline AFodderFrameCache& AGetFodderFrameCache() {
    static AFodderFrameCache cache;
    const AFrameSnapshot& snap = AGetFrameSnapshot();
    if (!cache.built || !(cache.stamp == snap.stamp)) {
        cache.stamp = snap.stamp;
        cache.built = true;
        cache.attackBox.assign(snap.zombies.size(), AHitBox{});
        cache.hasAttackBox.assign(snap.zombies.size(), 0);
    }
    return cache;
}

inline const AHitBox& ACachedAttackBox(AFodderFrameCache& cache, const AZombieSnapshot& zombie, int slot) {
    if (!cache.hasAttackBox[slot]) {
        cache.attackBox[slot] = AZombieAttackBox(zombie.zombie);
        cache.hasAttackBox[slot] = 1;
    }
    return cache.attackBox[slot];
}

}  // namespace aFodder

// ---------------------------------------------------------------------------
// 对外接口
// ---------------------------------------------------------------------------

// 这一格现在有没有僵尸“本帧能交互”（攻击框与垫材防御框重合），判定与 AGetFodderValue
// 收集僵尸的那一步完全相同。没有的话 AGetFodderValue 的价值必为 0、也没有任何副作用，
// 调用方可以直接跳过这一格（省掉 GetPlantRejectType 等游戏函数调用）。
inline bool AFodderCellHasZombie(int row, int col) {
    using namespace aFodder;
    if (row < 1 || row > aFieldInfo.nRows || col < 1 || col > 9)
        return false;
    const aThreat::AHitBox plantBox = aThreat::APlantHitBoxAt(row, col);
    const AFrameSnapshot& snap = AGetFrameSnapshot();
    AFodderFrameCache& frameCache = AGetFodderFrameCache();
    if (row - 1 < 0 || row - 1 >= 6)
        return false;
    for (int slot : snap.rowZombies[row - 1]) {
        if (ARectOverlap(plantBox, ACachedAttackBox(frameCache, snap.zombies[slot], slot)))
            return true;
    }
    return false;
}

// 输入垫材类型 + 位置（1 起始），把存活时间与价值写进 result。
// 这是“不分配内存”的版本：各字段先复位，zombies / endReason 的容量复用 —— 脚本每帧对几十个格子
// 各问一次，用一个静态 / 复用的 result 就不必每格都分配一次。内容与 AGetFodderValue 完全相同。
inline void AGetFodderValueInto(AFodderValue& result, APlantType type, int row, int col,
                                const AFodderOptions& options = {}, bool imitator = false) {
    using namespace aFodder;

    // 复位成刚构造出来的样子（AFodderValue 的默认值），只是不释放容量
    result.valid = false;
    result.type = APUFF_SHROOM;
    result.row = 1;
    result.col = 1;
    result.plantHp = 0;
    result.selfEffectCs = -1;
    result.surviveCs = 0;
    result.endReason.clear();
    result.zombies.clear();
    result.value = 0.0;
    if (row < 1 || row > aFieldInfo.nRows || col < 1 || col > 9)
        return;

    result.valid = true;
    result.type = type;
    result.row = row;
    result.col = col;
    result.plantHp = options.plantHpOverride > 0 ? options.plantHpOverride : PlantHp(type);
    result.selfEffectCs = SelfEffectCs(type, imitator);

    // 假设种下后的命中框（普通体型）
    const aThreat::AHitBox plantBox = aThreat::APlantHitBoxAt(row, col);

    // 收集“本帧攻击框就和垫材防御框重合”的僵尸：啃食用。
    // 还在外面走的、已经走过去的一概不算 —— 垫材只为“眼前这只”服务。
    // 巨人（红眼 / 白眼）也在这里收：它们不啃，后面按砸击相位单独处理。
    // （原逻辑逐只判 zombie.Row() + 1 != row，这里直接取本行列表，行内仍是数组顺序）
    const AFrameSnapshot& snap = AGetFrameSnapshot();
    AFodderFrameCache& frameCache = AGetFodderFrameCache();
    if (row - 1 >= 0 && row - 1 < 6) {
        for (int slot : snap.rowZombies[row - 1]) {
            const AZombieSnapshot& zombieSnap = snap.zombies[slot];
            AZombie& zombie = *zombieSnap.zombie;
            if (!ARectOverlap(plantBox, ACachedAttackBox(frameCache, zombieSnap, slot)))
                continue;

            AFodderZombieInfo info;
            info.zombie = &zombie;
            const AZombieThreatLevel level = AGetZombieThreatLevel(&zombie, options.levelOptions);
            const aThreat::ANaturalOutputInfo nature =
                aThreat::AGetNaturalOutputInfo(&zombie, options.naturalOptions);
            info.threatHp = level.threatHp;
            info.distanceFactor = level.distanceFactor;
            info.typeFactor = level.typeFactor;
            info.speedFactor = level.speedFactor;
            info.naturalDps = nature.fumeDps + nature.gloomDps + nature.melonDps;
            info.isVehicle = IsVehicle(zombie.Type());
            info.isGargantuar = IsGargantuar(zombie.Type());
            result.zombies.push_back(info);
        }
    }

    // ---- 存活时间：取各种“结束事件”的最早值 ----
    int survive = result.selfEffectCs >= 0 ? result.selfEffectCs : 1 << 30;
    result.endReason = result.selfEffectCs >= 0 ? "自生效" : "存活";

    // 1) 被车碾压 / 地刺反杀车
    for (const auto& info : result.zombies) {
        if (!info.isVehicle)
            continue;
        if (type == ASPIKEWEED || type == ASPIKEROCK) {
            // 地刺 / 地刺王：车到的时候自己攻击完就消失（不是被压死）
            if (survive > 0) {
                survive = 0;
                result.endReason = "地刺反杀车";
            }
            continue;
        }
        if (!CanBeCrushed(type))
            continue;  // 樱桃 / 辣椒 / 倭瓜 / 三叶草 / 毁灭菇 / 寒冰菇：车压不坏
        if (options.vehicleSurvivalCs < survive) {
            survive = options.vehicleSurvivalCs;
            result.endReason = "被车秒杀";
        }
    }

    // 2) 被巨人砸：只看本帧能砸到这块垫材的巨人（红眼 / 白眼共用同一套）。
    //    正在举锤的读它当前的动画相位；还在走的按“举锤到砸下”的兜底时间算。
    //    减速拉长动画会自然体现在相位里。
    for (auto& info : result.zombies) {
        if (!info.isGargantuar)
            continue;
        const aThreat::AGargantuarSmashInfo smash =
            aThreat::AGetGargantuarSmashInfo(info.zombie, 0.0, options.gargantuarWindupCs,
                                             options.smashImpactRate, options.gargantuarFallbackCycleCs);
        if (!smash.smashing)
            aThreat::GargantuarAnimCache().erase(info.zombie);
        info.smashImpactCs = static_cast<int>(std::ceil(smash.impactCs));
        info.smashCycleCs = smash.cycleCs;
        if (info.smashImpactCs < survive) {
            survive = info.smashImpactCs;
            // 灰烬与寒冰菇被砸会被引爆，仍然正常生效
            const bool stillWorks =
                (type == ACHERRY_BOMB || type == AJALAPENO || type == ADOOM_SHROOM || type == AICE_SHROOM);
            result.endReason = stillWorks ? "被巨人砸（引爆生效）" : "被巨人砸";
        }
    }

    // 3) 被小丑炸：所有垫材都可能被小丑的爆炸清掉（本帧的开盒小丑列表见帧内缓存）
    for (const aThreat::AJackEntry& jack : aThreat::AGetThreatFrameCache().jacks) {
        const int explodeCs = jack.countdown;
        if (explodeCs >= survive)
            continue;
        if (!aThreat::AHitBoxOverlapCircle(plantBox, jack.centerX, jack.centerY, aThreat::JACK_PLANT_KILL_RADIUS))
            continue;
        survive = explodeCs;
        result.endReason = "被小丑炸";
    }

    // 4) 被啃完：收到的都是“现在就贴着”的啃食僵尸，从现在起一起啃（地刺不会被啃）
    if (type != ASPIKEWEED && type != ASPIKEROCK) {
        double rate = 0.0;  // 每帧总啃食量（减速减半、冰冻 / 黄油不啃）
        for (auto& info : result.zombies) {
            if (info.isVehicle || info.isGargantuar)
                continue;
            rate += BitePerFrame(info.zombie, options);
        }
        if (rate > 0.0) {
            const int eatenCs = static_cast<int>(std::ceil(result.plantHp / rate));
            if (eatenCs < survive) {
                survive = eatenCs;
                result.endReason = "被啃完";
            }
        }
    }

    result.surviveCs = survive >= (1 << 30) ? -1 : std::max(0, survive);  // -1 = 没人能吃掉它

    // ---- 垫材价值 ----
    // 拖住的这段时间里，自然输出（大喷菇 / 忧郁菇，按平均 dps）能打掉多少血，按威胁血量封顶，
    // 再乘 距离系数 × 种类修正 × 速度系数（和樱桃 / 倭瓜 / 冰同一套折算）。
    //   · 啃食僵尸：拖住时长 = 垫材存活时间（一直挡着它啃到垫材死）
    //   · 巨人：砸下发生在存活时间内（被垫到）的，每垫到一个固定拖延它一个完整锤击循环
    double value = 0.0;
    const int lifeCs = result.surviveCs < 0 ? 0 : result.surviveCs;
    for (auto& info : result.zombies) {
        if (info.isVehicle)
            continue;  // 车不会被挡住

        if (info.isGargantuar) {
            if (info.smashImpactCs > lifeCs)
                continue;  // 垫材在它砸下来之前就没了，垫不到
            info.holdCs = static_cast<int>(std::ceil(info.smashCycleCs));
        } else {
            info.holdCs = lifeCs;
        }
        if (info.holdCs <= 0)
            continue;
        info.damage = std::min(info.threatHp, info.naturalDps * info.holdCs / 100.0);
        value += AValueThreatDamage(info.damage, info.distanceFactor, info.typeFactor, info.speedFactor);
    }
    result.value = value;
}

// 返回新对象的版本（原接口）：内容与 AGetFodderValueInto 完全相同
inline AFodderValue AGetFodderValue(APlantType type, int row, int col, const AFodderOptions& options = {},
                                    bool imitator = false) {
    AFodderValue result;
    AGetFodderValueInto(result, type, row, col, options, imitator);
    return result;
}

// 已经种下去过的垫材：血量直接读内存，最准
inline AFodderValue AGetFodderValue(APlant* plant, const AFodderOptions& options = {}) {
    if (plant == nullptr)
        return {};
    AFodderOptions local = options;
    local.plantHpOverride = plant->Hp();
    const int base = aThreat::APlantBaseType(plant);
    return AGetFodderValue(static_cast<APlantType>(base), plant->Row() + 1, plant->Col() + 1, local);
}

// 调参用日志
inline void ALogFodderValue(APlantType type, int row, int col, const AFodderOptions& options = {}) {
    const AFodderValue info = AGetFodderValue(type, row, col, options);
    if (!info.valid)
        return;
    aLogger->Info("[Fodder] {} ({},{}) 血量={} 自生效={} 存活时间={}cs 原因={} 价值={}", type, row, col, info.plantHp,
                  info.selfEffectCs, info.surviveCs, info.endReason, static_cast<int>(info.value + 0.5));
    for (const auto& z : info.zombies) {
        aLogger->Info("    僵尸 type={} row={} 威胁血量={} 自然dps={} 拖住={}cs 打掉={} 车={} 巨人={}", z.zombie->Type(),
                      z.zombie->Row() + 1, static_cast<int>(z.threatHp), static_cast<int>(z.naturalDps + 0.5), z.holdCs,
                      static_cast<int>(z.damage + 0.5), z.isVehicle, z.isGargantuar);
    }
}

#endif  // __A_FODDER_VALUE_H__
