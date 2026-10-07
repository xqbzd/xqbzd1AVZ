// 临时探针：验证阵前（玉米加农炮命中框 [x, x+140]、南瓜头、无植物→进家位置）
#include "zombie_threat_level.h"

#include <avz.h>

namespace {
ATickRunner Logic;

constexpr int COB_ROW = 2;
constexpr int COB_COL = 1;

enum class Stage {
    PlantCob,
    Report,
};

Stage stage = Stage::PlantCob;
int lastLog = -1000;

void LogFront(int row, const char* tag, int clock) {
    const auto front = aThreatLevel::RowFront(row);
    aLogger->Info("[LevelTest] {} clock={} 行{} 阵前 found={} x={} 植物={} 南瓜={}", tag, clock, row, front.found,
                  front.x, static_cast<const void*>(front.plant), static_cast<const void*>(front.pumpkin));
}

void Run() {
    AMainObject* main = AGetMainObject();
    const int clock = main->GameClock();

    if (stage == Stage::PlantCob) {
        if (main->Sun() >= 500 && AIsSeedUsable(ACOB_CANNON) &&
            AAsm::GetPlantRejectType(ACOB_CANNON, COB_ROW - 1, COB_COL - 1) == AAsm::NIL) {
            ACard(ACOB_CANNON, COB_ROW, COB_COL);
            aLogger->Info("[LevelTest] 种玉米加农炮 ({},{}) clock={}", COB_ROW, COB_COL, clock);
            LogFront(COB_ROW, "炮就位", clock);
            stage = Stage::Report;
            lastLog = clock;
        }
        return;
    }

    if (clock - lastLog < 300)
        return;
    lastLog = clock;

    for (int row = 1; row <= aFieldInfo.nRows; ++row)
        LogFront(row, "报告", clock);
    for (auto& zombie : aAliveZombieFilter)
        ALogZombieThreatLevel(&zombie);
}
}  // namespace

void AScript() {
    ASetGameSpeed(10);
    ASelectCards({ACOB_CANNON, AFUME_SHROOM, APUMPKIN, ACHERRY_BOMB, ACOFFEE_BEAN, ASUNFLOWER, APEASHOOTER});
    Logic.Start(Run);
}
