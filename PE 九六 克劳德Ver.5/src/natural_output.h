#ifndef __A_NATURAL_OUTPUT_H__
#define __A_NATURAL_OUTPUT_H__

// ============================================================================
//  自然输出（大喷菇 / 忧郁菇 / 冰瓜）对僵尸威胁度的削减 —— 查看 / 调参用
//
//  注意：自然输出已经并进「威胁血量」的定义里了（zombie_threat_predict.h 的
//  AGetZombieThreatHp 会扣掉它，默认开启，可用 AThreatQueryOptions::includeNaturalOutput 关掉）。
//  本文件只是把这段削减单独摊开：路上走了多久、阵前多挨了多久、被扣了多少血、
//  威胁度从多少掉到多少、被削减多少，方便核对与调参。
//
//  抽象口径（都在 ANaturalOutputOptions 里可改）：
//     大喷菇：20 伤害 / 150cs 单发 → 13.3 dps；只打本行，攻击框 [x+60, x+400]
//     忧郁菇：20 伤害 × 4 发 / 200cs → 40 dps；打 3×3（本行 ±1、横向 [x-80, x+160]）
//     冰瓜：最简口径 —— 本行每 300cs 固定 30 点、相邻行每 300cs 固定 20 点；射程是整行，
//           所以整段走位 + 阵前时间都算
//  喷 / 曾攻击框出自反编译 Plant::GetPlantAttackRect；命中判定是行闸门 + 攻击框与僵尸判定框的 x 重叠 > 0，
//  并要求僵尸 EffectedByDamage(9 = 地面|DOG)；冰瓜是 13（多一个潜水），所以飞行气球打不到。
// ============================================================================

#include "zombie_threat_level.h"

#include <avz.h>
#include <algorithm>
#include <vector>

struct ANaturalOutputResult {
    bool valid = false;
    AZombie* zombie = nullptr;
    bool hasFront = false;
    bool isHouse = false;
    int frontX = 0;
    double walkCs = 0.0;          // 按当前速度走到阵前的时间
    double attackCs = 0.0;        // 在阵前攻击（巨人 = 锤击前摇）的额外时间
    int threatHpBefore = 0;       // 扣自然输出之前的威胁血量
    int threatHpAfter = 0;        // 扣完自然输出之后的威胁血量（= 现在威胁血量口径）
    double naturalDamage = 0.0;   // 自然输出打掉的量
    double threatBefore = 0.0;
    double threatAfter = 0.0;
    double threatCut = 0.0;       // ★ 自然输出削减的威胁度
    double fumeDps = 0.0;
    double gloomDps = 0.0;
    double melonDps = 0.0;
};

// 单个僵尸：自然输出的伤害明细 + 对威胁度的削减
inline ANaturalOutputResult AGetZombieNaturalOutput(AZombie* zombie, const ANaturalOutputOptions& options = {},
                                                    const AThreatLevelOptions& levelOptions = {}) {
    ANaturalOutputResult result;
    if (zombie == nullptr)
        return result;

    const aThreat::ANaturalOutputInfo nature = aThreat::AGetNaturalOutputInfo(zombie, options);
    if (!nature.valid)
        return result;

    result.valid = true;
    result.zombie = zombie;
    result.hasFront = nature.hasFront;
    result.isHouse = nature.isHouse;
    result.frontX = nature.frontX;
    result.walkCs = nature.walkCs;
    result.attackCs = nature.attackCs;
    result.naturalDamage = nature.damage;
    result.fumeDps = nature.fumeDps;
    result.gloomDps = nature.gloomDps;
    result.melonDps = nature.melonDps;

    // 扣自然输出之前的威胁血量（只扣了即将到来的灰烬 / 炮弹伤害）
    AThreatQueryOptions before;
    before.includeSquash = levelOptions.includeSquash;
    before.includeNaturalOutput = false;
    result.threatHpBefore = AGetZombieThreatHp(zombie, levelOptions.threatTime, before);

    // 现在的威胁度（威胁血量已含自然输出）
    AThreatLevelOptions level = levelOptions;
    level.includeNaturalOutput = true;
    const AZombieThreatLevel info = AGetZombieThreatLevel(zombie, level);
    result.threatHpAfter = info.threatHp;
    result.threatAfter = info.value;
    result.threatBefore =
        static_cast<double>(result.threatHpBefore) * info.distanceFactor * info.typeFactor * info.speedFactor;
    result.threatCut = std::max(0.0, result.threatBefore - result.threatAfter);
    return result;
}

// 整场：按“自然输出削减的威胁度”从大到小
inline std::vector<ANaturalOutputResult> AGetAllNaturalOutput(const ANaturalOutputOptions& options = {},
                                                             const AThreatLevelOptions& levelOptions = {}) {
    std::vector<ANaturalOutputResult> result;
    for (auto& zombie : aAliveZombieFilter) {
        auto info = AGetZombieNaturalOutput(&zombie, options, levelOptions);
        if (info.valid)
            result.push_back(info);
    }
    std::sort(result.begin(), result.end(),
              [](const ANaturalOutputResult& lhs, const ANaturalOutputResult& rhs) {
                  return lhs.threatCut > rhs.threatCut;
              });
    return result;
}

// 调参用日志
inline void ALogNaturalOutput(AZombie* zombie, const ANaturalOutputOptions& options = {}) {
    const ANaturalOutputResult info = AGetZombieNaturalOutput(zombie, options);
    if (!info.valid)
        return;
    aLogger->Info(
        "[Nat] type={} row={} 阵前={}{} 走={}cs 阵前多={}cs 大喷dps={:.1f} 忧郁dps={:.1f} 冰瓜dps={:.1f} 伤害={:.0f} "
        "威胁血量 {}→{} 威胁度 {:.1f}→{:.1f} 削减={:.1f}",
        zombie->Type(), zombie->Row() + 1, info.frontX, info.isHouse ? "(进家)" : "", static_cast<int>(info.walkCs),
        static_cast<int>(info.attackCs), info.fumeDps, info.gloomDps, info.melonDps, info.naturalDamage, info.threatHpBefore,
        info.threatHpAfter, info.threatBefore, info.threatAfter, info.threatCut);
}

#endif  // __A_NATURAL_OUTPUT_H__
