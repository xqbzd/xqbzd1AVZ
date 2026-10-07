// 临时对照测试：种一门玉米炮 → 用预测器选落点并预测 → 真的发炮 → 核对哪些僵尸掉血
#include "instant_plant_predict.h"

#include <avz.h>
#include <map>

namespace {
ATickRunner Logic;

constexpr int COB_ROW = 3;
constexpr int COB_COL = 1;
constexpr int COB_SUN = 500;

enum class Stage {
    WaitCard,
    WaitArm,
    WaitTarget,
    WaitImpact,
    Done,
};

Stage stage = Stage::WaitCard;
int lastHeartbeat = -1000;
int plantClock = -1;
int fireClock = -1;
int dropRow = 0;
float dropCol = 0.0f;
int flyCs = 0;
std::vector<AZombie*> predicted;
std::map<AZombie*, int> hpBefore;
std::vector<AZombie*> aliveAfter;

bool IsAliveNow(AZombie* target) {
    for (auto& z : aAliveZombieFilter)
        if (&z == target)
            return true;
    return false;
}

int TotalHp(AZombie* z) {
    return z->Hp() + z->OneHp() + z->TwoHp();
}

void LogicRun() {
    const int clock = AGetMainObject()->GameClock();

    if (stage == Stage::WaitCard) {
        const bool hasCob = AGetPlantPtr(COB_ROW, COB_COL, ACOB_CANNON) != nullptr;
        const bool usable = AIsSeedUsable(ACOB_CANNON);
        const int sun = AGetMainObject()->Sun();
        if (!hasCob && usable && sun >= COB_SUN &&
            AAsm::GetPlantRejectType(ACOB_CANNON, COB_ROW - 1, COB_COL - 1) == AAsm::NIL) {
            ACard(ACOB_CANNON, COB_ROW, COB_COL);
            plantClock = clock;
            stage = Stage::WaitArm;
            aLogger->Info("[CobTest] 种炮 ({},{}) clock={}", COB_ROW, COB_COL, clock);
        } else if (clock - lastHeartbeat > 500) {
            lastHeartbeat = clock;
            aLogger->Info("[CobTest] 等待种炮：已有炮={} 卡片可用={} 阳光={} 拒绝码={}", hasCob, usable, sun,
                          AAsm::GetPlantRejectType(ACOB_CANNON, COB_ROW - 1, COB_COL - 1));
        }
        return;
    }

    if (stage == Stage::WaitArm) {
        if (clock - plantClock > 520) {
            aLogger->Info("[CobTest] 炮已就绪 clock={}", clock);
            stage = Stage::WaitTarget;
        }
        return;
    }

    if (stage == Stage::WaitTarget) {
        AZombie* seedZombie = nullptr;
        for (auto& z : aAliveZombieFilter) {
            if (z.Abscissa() > 250 && z.Abscissa() < 640) {
                seedZombie = &z;
                break;
            }
        }
        if (seedZombie == nullptr)
        {
            if (clock - lastHeartbeat > 500) {
                lastHeartbeat = clock;
                aLogger->Info("[CobTest] 等待中场僵尸 clock={}", clock);
            }
            return;
        }

        const int row1 = seedZombie->Row() + 1;
        float bestCol = 0.0f;
        int bestValue = -1;
        std::vector<AInstantPlantHit> bestHits;
        for (float col = 1.0f; col <= 9.001f; col += 0.25f) {
            auto hits = APredictCobCannonHits(row1, col);
            int value = 0;
            for (const auto& hit : hits)
                value += std::min(1800, TotalHp(hit.zombie));
            if (value > bestValue) {
                bestValue = value;
                bestCol = col;
                bestHits = hits;
            }
        }
        if (bestValue <= 0)
            return;

        dropRow = row1;
        dropCol = bestCol;
        flyCs = aInstantPlant::AGetCobFlyCs(dropRow, dropCol, COB_COL);
        predicted.clear();
        hpBefore.clear();
        for (auto& z : aAliveZombieFilter)
            hpBefore[&z] = TotalHp(&z);
        for (const auto& hit : bestHits)
            predicted.push_back(hit.zombie);

        ACobManager::RawFire(COB_ROW, COB_COL, dropRow, dropCol);
        fireClock = clock;
        stage = Stage::WaitImpact;

        aLogger->Info("[CobTest] 发炮 clock={} -> 行{} 列{:.2f} 飞行{}cs 预测{}只", clock, dropRow, dropCol, flyCs,
                      predicted.size());
        for (const auto& hit : bestHits) {
            aLogger->Info("    预测 addr={} type={} row={} 命中x={} 血={} lethal={}",
                          static_cast<const void*>(hit.zombie), hit.zombie->Type(), hit.zombie->Row() + 1,
                          hit.abscissa, hpBefore[hit.zombie], hit.lethal);
        }
        return;
    }

    if (stage == Stage::WaitImpact) {
        if (clock - fireClock < flyCs + 3)
            return;

        aLogger->Info("[CobTest] 命中核对 clock={} (发炮后{}cs)", clock, clock - fireClock);
        int actualHit = 0;
        int missedPredict = 0;
        int extraHit = 0;
        for (const auto& [ptr, hp] : hpBefore) {
            const bool alive = IsAliveNow(ptr);
            const bool wasPredicted = std::find(predicted.begin(), predicted.end(), ptr) != predicted.end();
            const int newHp = alive ? TotalHp(ptr) : 0;
            const int lost = hp - newHp;
            const bool hit = (!alive || lost >= 1700);
            if (hit)
                ++actualHit;
            if (wasPredicted && !hit)
                ++missedPredict;
            if (!wasPredicted && hit)
                ++extraHit;
            aLogger->Info("    僵尸 type={} row={} 血{}->{}{} 预测{}", ptr->Type(), ptr->Row() + 1, hp, newHp,
                          alive ? "" : "(已消失)", wasPredicted ? "命中" : "未命中");
        }
        aLogger->Info("[CobTest] 结论：预测{}只 / 实际{}只 / 预测漏{}只 / 预测多{}只", predicted.size(), actualHit,
                      missedPredict, extraHit);
        stage = Stage::Done;
    }
}
}  // namespace

void AScript() {
    ASetGameSpeed(10);
    AEnterGame(AAsm::SURVIVAL_ENDLESS_STAGE_3, true);  // 泳池无尽，自动点掉“继续游戏”对话框
    ASelectCards({ACOB_CANNON, ACHERRY_BOMB, AJALAPENO, ADOOM_SHROOM, ASQUASH, ASUNFLOWER, APEASHOOTER});
    Logic.Start(LogicRun);
}
