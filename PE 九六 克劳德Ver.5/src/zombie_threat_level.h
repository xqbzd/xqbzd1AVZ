#ifndef __A_ZOMBIE_THREAT_LEVEL_H__
#define __A_ZOMBIE_THREAT_LEVEL_H__

// ============================================================================
//  僵尸威胁度
//
//      威胁度 = 威胁血量 × 距离系数 × 种类修正 × 僵尸速度系数
//
//  · 威胁血量：来自 zombie_threat_predict.h，口径是
//        本体血量 + 一类防具
//        − 即将到来的灰烬 / 炮弹伤害
//        − 自然输出（大喷菇 / 忧郁菇）在路上与阵前打掉的量
//      下限 0。自然输出已经并进威胁血量，不需要再单独扣。
//  · 距离系数：C * e^(-k * x)，C、k 逐行不同（见 aThreatLevelRowParams，
//      行号 1 起，默认 C=1、k=0 即先不做距离衰减，方便测试；
//      想按“每格衰减 1”就把 k 填 1/80，或把 distanceInTiles 置 true 后 k 填 1）
//      x = 僵尸距离“阵前”的像素数（还差多少 px 走到阵前，下限 0）
//  · 种类修正：默认 1（见 aThreatTypeFactor，按僵尸类型索引）
//  · 速度系数：僵尸当前速度 ÷ 该僵尸在当前状态下的“速度随机数平均值”
//
//  “阵前”的定义（在 zombie_threat_predict.h 里实现并共用）：
//      一行中“已唤醒的大喷菇 / 忧郁菇 / 冰瓜 / 玉米加农炮”里最靠右的那个植物；
//      若该植物所在格上有南瓜头，则阵前按南瓜头的命中框来算（取右边界）。
//      如果这一行没有上述植物，阵前就取“进家位置”（反编译
//      zombie_system::update_entering_home：按僵尸类型 int_x <= -100 / -130 / -150 / -175）。
//
//  反编译数据来源：
//    system/reanim.cpp                reanim::update_dx —— 各僵尸 / 状态的速度随机数
//        普通行走 rand(0.23,0.37) → 0.30；舞王/伴舞/跳跳/旗帜 0.45；
//        钻地/撑杆助跑/橄榄/潜水/小丑 rand(0.66,0.68) → 0.67；爬梯 0.80；
//        报纸冲刺/海豚 0.90；雪人 0.40、逃跑 0.80；矿工向右 0.12；潜水游泳 0.30
//    object/plant.cpp                 plant::get_hit_box —— 植物命中框
//    system/zombie/zombie_system.cpp  update_entering_home —— 进家判定
//
//  帧内缓存（性能）：同一帧、同一组 AThreatLevelOptions 下，一只僵尸的威胁度只算一次
//  （脚本的威胁度快照、垫材价值、显示 UI 都会反复问同一只僵尸）。
// ============================================================================

#include "zombie_threat_predict.h"

#include <avz.h>
#include <algorithm>
#include <cmath>
#include <vector>

// 每行的距离系数参数（行号 1 起，0 号不用）
struct AThreatLevelRowParams {
    double c = 1.0;  // C
    double k = 0.0;  // k（默认 0：不衰减，测试方便）
};

// 直接改这里即可，例如 aThreatLevelRowParams[1] = {1.5, 0.02};
inline AThreatLevelRowParams aThreatLevelRowParams[7];

// 种类修正系数（按僵尸类型索引，0 表示未设置 → 按 1 处理）
inline constexpr int A_THREAT_TYPE_SLOT = 40;
inline double aThreatTypeFactor[A_THREAT_TYPE_SLOT] = {};

// 威胁度覆盖钩子：返回 > 0 时直接把它当这只僵尸的威胁度（无视距离 / 种类 / 速度修正）。
// 默认 nullptr = 不覆盖。用途举例：小丑马上要炸掉忧郁菇、而灰烬来得及先炸死它时，
// 给一个很大的值，逼灰烬优先去炸这只小丑。
// 和 aThreatLevelRowParams / aThreatTypeFactor / aThreat::aRowFrontFilter 一样是全局设置，
// 脚本在 AScript() 里设一次即可；库里不含任何具体阵型逻辑。
using AThreatValueOverride = double (*)(AZombie* zombie);

inline AThreatValueOverride aThreatValueOverride = nullptr;

struct AThreatLevelOptions {
    int threatTime = -1;               // 传给威胁血量：-1 = 所有即将到来的伤害都扣掉
    bool includeSquash = true;         // 倭瓜是否算威胁
    bool includeNaturalOutput = true;  // 威胁血量是否扣掉自然输出（大喷菇 / 忧郁菇）
    ANaturalOutputOptions naturalOptions;
    double speedFactorOverride = 0.0;  // > 0 时直接用它替代自动算出来的速度系数
    // x 的单位：false = 像素（默认），true = 格（80px）
    // k 默认为 0（不衰减）；要“每格衰减 1”：k=1/80 且这里 false，或 k=1 且这里 true
    bool distanceInTiles = false;
    // 种类修正的“默认值”：aThreatTypeFactor 里没写（=0）的僵尸类型用它。
    // 设成 0 就是“除表里写明的类型以外，其余僵尸种类价值全部为 0”
    double typeFactorDefault = 1.0;

    // 帧内缓存按“同一组参数”复用结果，需要能比较
    bool operator==(const AThreatLevelOptions&) const = default;
};

// 威胁度明细（方便调参 / 排查）
struct AZombieThreatLevel {
    AZombie* zombie = nullptr;
    int row = 1;             // 1 起始
    int hp = 0;              // 当前血量（本体 + 一类防具）
    int threatHp = 0;        // 威胁血量（已扣即将到来的伤害 + 自然输出）
    int naturalDamage = 0;   // 其中自然输出扣掉的部分（参考）
    bool doomed = false;     // 威胁血量 <= 0
    bool hasFront = false;   // 该行是否找到阵前
    bool isHouse = false;    // 没有阵前植物时，是否退化成“进家位置”
    int frontX = 0;          // 阵前像素
    double distance = 0.0;   // 距离阵前的像素（下限 0）
    double distanceFactor = 1.0;
    double typeFactor = 1.0;
    double speedFactor = 1.0;
    double value = 0.0;      // 最终威胁度
    double overrideValue = 0.0;  // >0：威胁度被脚本直接指定（无视距离 / 种类 / 速度）
};

namespace aThreatLevel {

using namespace aInstantPlant;

// 阵前 / 进家位置：实现已下沉到 zombie_threat_predict.h，这里只做转发
using aThreat::ARowFrontInfo;

inline ARowFrontInfo RowFront(int row) {
    return aThreat::ARowFront(row);
}

inline int ZombieHomeX(int zombieType) {
    return aThreat::AZombieHomeX(zombieType);
}

// 该僵尸在当前状态下的“速度随机数平均值”（反编译 reanim::update_dx）
inline float AverageZombieSpeed(AZombie* zombie) {
    const int state = zombie->State();
    const int type = zombie->Type();

    if (state == 0x3B)  // snorkel_swim
        return 0.30f;
    if (state == 0x25)  // digger_walk_right
        return 0.12f;
    if (state == 0x5B)  // yeti_escape
        return 0.80f;
    if (type == AZOMBIE_YETI)
        return 0.40f;
    if (type == ADANCING_ZOMBIE || type == ABACKUP_DANCER || type == APOGO_ZOMBIE || type == AFLAG_ZOMBIE)
        return 0.45f;
    if (state == 0x20 ||              // digger_dig
        state == 0x0B ||              // pole_valuting_running
        type == AFOOTBALL_ZOMBIE || type == ASNORKEL_ZOMBIE || type == AJACK_IN_THE_BOX_ZOMBIE)
        return 0.67f;
    if (state == 0x4C)  // ladder_walking
        return 0.80f;
    if (state == 0x1F ||  // newspaper_running
        state == 0x33 ||  // dolphin_walk_with_dolphin
        state == 0x38)    // dolphin_walk_without_dolphin
        return 0.90f;
    return 0.30f;  // 普通行走 rand(0.23, 0.37) 的平均
}

inline double TypeFactor(int type, double defaultValue = 1.0) {
    if (type < 0 || type >= A_THREAT_TYPE_SLOT)
        return defaultValue;
    const double value = aThreatTypeFactor[type];
    return value > 0.0 ? value : defaultValue;
}

// ---- 帧内缓存：每只僵尸在“同一组参数”下的威胁度一帧只算一次 ----
struct ALevelMemo {
    bool has = false;
    AThreatLevelOptions options;
    AZombieThreatLevel result;
};

struct ALevelFrameCache {
    AFrameStamp stamp;
    bool built = false;
    std::vector<ALevelMemo> memo;  // 下标 = AFrameSnapshot::zombies 下标
};

inline ALevelFrameCache& AGetLevelFrameCache() {
    static ALevelFrameCache cache;
    const AFrameSnapshot& snap = AGetFrameSnapshot();
    if (!cache.built || !(cache.stamp == snap.stamp)) {
        cache.stamp = snap.stamp;
        cache.built = true;
        cache.memo.assign(snap.zombies.size(), ALevelMemo{});
    }
    return cache;
}

// 计算单个僵尸的威胁度（不走缓存的原始算法）
inline AZombieThreatLevel __AComputeThreatLevel(AZombie* zombie, const AThreatLevelOptions& options) {
    AZombieThreatLevel result;
    result.zombie = zombie;
    result.row = zombie->Row() + 1;
    result.hp = AZombieHp(zombie);

    AThreatQueryOptions threatOptions;
    threatOptions.includeSquash = options.includeSquash;
    threatOptions.includeNaturalOutput = options.includeNaturalOutput;
    threatOptions.naturalOptions = options.naturalOptions;
    result.threatHp = AGetZombieThreatHp(zombie, options.threatTime, threatOptions);
    result.doomed = result.threatHp <= 0;
    if (options.includeNaturalOutput)
        result.naturalDamage =
            static_cast<int>(std::ceil(aThreat::AGetNaturalOutputInfo(zombie, options.naturalOptions).damage));

    // 距离系数
    const ARowFrontInfo front = RowFront(result.row);
    result.hasFront = front.found;
    if (front.found) {
        result.frontX = front.x;
    } else {
        // 本行没有阵前植物：阵前退化为“进家位置”
        result.isHouse = true;
        result.frontX = ZombieHomeX(zombie->Type());
    }
    {
        const int zombieLeft = AGetZombieHitBox(zombie).x;
        const double distance = static_cast<double>(zombieLeft - result.frontX);
        result.distance = distance > 0.0 ? distance : 0.0;
    }
    const int rowIndex = (result.row >= 1 && result.row <= 6) ? result.row : 0;
    const AThreatLevelRowParams& params = aThreatLevelRowParams[rowIndex];
    const double distanceForExp = options.distanceInTiles ? result.distance / 80.0 : result.distance;
    result.distanceFactor = params.c * std::exp(-params.k * distanceForExp);

    // 种类修正
    result.typeFactor = TypeFactor(zombie->Type(), options.typeFactorDefault);

    // 速度系数
    if (options.speedFactorOverride > 0.0) {
        result.speedFactor = options.speedFactorOverride;
    } else {
        const float current = zombie->Speed();
        const float average = AverageZombieSpeed(zombie);
        result.speedFactor = (current > 0.0f && average > 0.0f) ? (current / average) : 1.0;
    }

    result.value = static_cast<double>(result.threatHp) * result.distanceFactor * result.typeFactor * result.speedFactor;

    // 脚本的威胁度覆盖（例如“必须先用灰烬炸掉的小丑”）：直接顶掉，无视三个系数
    if (aThreatValueOverride != nullptr) {
        const double forced = aThreatValueOverride(zombie);
        if (forced > 0.0) {
            result.overrideValue = forced;
            result.value = forced;
        }
    }
    return result;
}

}  // namespace aThreatLevel

// ---------------------------------------------------------------------------
// 对外接口
// ---------------------------------------------------------------------------

// 计算单个僵尸的威胁度（含全部中间量，便于调参）
inline AZombieThreatLevel AGetZombieThreatLevel(AZombie* zombie, const AThreatLevelOptions& options = {}) {
    using namespace aThreatLevel;

    if (zombie == nullptr)
        return AZombieThreatLevel{};

    const int slot = AGetFrameSnapshot().Slot(zombie);
    if (slot >= 0) {
        {
            const ALevelMemo& memo = AGetLevelFrameCache().memo[slot];
            if (memo.has && memo.options == options)
                return memo.result;
        }
        const AZombieThreatLevel result = __AComputeThreatLevel(zombie, options);
        ALevelMemo& memo = AGetLevelFrameCache().memo[slot];
        memo.has = true;
        memo.options = options;
        memo.result = result;
        return result;
    }
    return __AComputeThreatLevel(zombie, options);
}

// 只要数值的简版
inline double AGetZombieThreatLevelValue(AZombie* zombie, const AThreatLevelOptions& options = {}) {
    return AGetZombieThreatLevel(zombie, options).value;
}

// 对某只僵尸造成 damage 点伤害，折算成能削减多少威胁值：
//     伤害 × 距离系数 × 种类修正 × 速度系数
// 灰烬（樱桃 / 倭瓜）、冰、垫材价值都走这一套，保证“打掉的血量”口径统一；
// damage 怎么封顶（灰烬 1800、垫材按威胁血量、冰按冻结前后差）由调用方决定。
inline double AValueThreatDamage(double damage, double distanceFactor, double typeFactor, double speedFactor) {
    if (!(damage > 0.0))
        return 0.0;
    return damage * distanceFactor * typeFactor * speedFactor;
}

inline double AValueThreatDamage(const AZombieThreatLevel& info, double damage) {
    return AValueThreatDamage(damage, info.distanceFactor, info.typeFactor, info.speedFactor);
}

// 整场威胁度（按威胁度从大到小），可直接拿来做目标选择
inline std::vector<AZombieThreatLevel> AGetAllThreatLevels(const AThreatLevelOptions& options = {}) {
    std::vector<AZombieThreatLevel> result;
    for (auto& zombie : aAliveZombieFilter)
        result.push_back(AGetZombieThreatLevel(&zombie, options));
    std::sort(result.begin(), result.end(),
              [](const AZombieThreatLevel& lhs, const AZombieThreatLevel& rhs) { return lhs.value > rhs.value; });
    return result;
}

// 调参用日志
inline void ALogZombieThreatLevel(AZombie* zombie, const AThreatLevelOptions& options = {}) {
    const AZombieThreatLevel info = AGetZombieThreatLevel(zombie, options);
    if (info.zombie == nullptr)
        return;
    aLogger->Info(
        "[Lv] addr={} type={} row={} 血量={} 威胁血量={} (自然输出扣 {}) 阵前={}{} 距离={} 距离系数={:.3f} "
        "种类={:.2f} 速度={:.3f} → 威胁度={:.1f}",
        static_cast<const void*>(zombie), zombie->Type(), info.row, info.hp, info.threatHp, info.naturalDamage,
        info.frontX, info.isHouse ? "(进家)" : "", static_cast<int>(info.distance), info.distanceFactor,
        info.typeFactor, info.speedFactor, info.value);
}

#endif  // __A_ZOMBIE_THREAT_LEVEL_H__
