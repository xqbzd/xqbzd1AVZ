// 临时探针：在僵尸旁边种一颗樱桃，然后打印各僵尸的威胁血量与实时生效倒计时，
// 用来核对 ExplodeCountdown 与命中几何是否与实际掉血一致
#include "zombie_threat_predict.h"

#include <avz.h>

namespace {
ATickRunner Logic;

enum class Stage {
    WaitCard,
    Watch,
    Done,
};

Stage stage = Stage::WaitCard;
int plantClock = -1;
APlant* planted = nullptr;
int logClock = -1000;

void Run() {
    AMainObject* main = AGetMainObject();
    const int clock = main->GameClock();

    if (stage == Stage::WaitCard) {
        if (!AIsSeedUsable(ACHERRY_BOMB) || main->Sun() < 150)
            return;
        AZombie* seed = nullptr;
        for (auto& z : aAliveZombieFilter) {
            if (z.Abscissa() > 280 && z.Abscissa() < 600) {
                seed = &z;
                break;
            }
        }
        if (seed == nullptr)
            return;

        const int row = seed->Row() + 1;
        const int col = std::clamp(static_cast<int>((seed->Abscissa() + 64.0f) / 80.0f), 1, 9);
        if (AAsm::GetPlantRejectType(ACHERRY_BOMB, row - 1, col - 1) != AAsm::NIL)
            return;

        planted = ACard(ACHERRY_BOMB, row, col);
        plantClock = clock;
        stage = Stage::Watch;
        aLogger->Info("[ThreatTest] 种樱桃 ({},{}) clock={} 目标 x={}", row, col, clock, seed->Abscissa());
        for (auto& z : aAliveZombieFilter)
            ALogZombieThreats(&z, -1);
        return;
    }

    if (stage == Stage::Watch) {
        if (clock - logClock >= 20) {
            logClock = clock;
            aLogger->Info("[ThreatTest] clock={} 樱桃剩余={} 场上僵尸={}", clock,
                          planted ? planted->ExplodeCountdown() : -1, static_cast<int>(aAliveZombieFilter.Count()));
            for (auto& z : aAliveZombieFilter)
                ALogZombieThreats(&z, -1);
        }
        if (clock - plantClock > 200) {
            aLogger->Info("[ThreatTest] 结束：clock={} 场上僵尸={}", clock, static_cast<int>(aAliveZombieFilter.Count()));
            stage = Stage::Done;
        }
    }
}
}  // namespace

void AScript() {
    ASetGameSpeed(10);
    ASelectCards({ACHERRY_BOMB, AJALAPENO, ADOOM_SHROOM, ASQUASH, ACOB_CANNON, ASUNFLOWER, APEASHOOTER});
    Logic.Start(Run);
}
