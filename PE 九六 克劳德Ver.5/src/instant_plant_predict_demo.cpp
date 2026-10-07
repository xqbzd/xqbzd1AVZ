// 只读自测脚本：不种任何植物，只在战斗界面里每 100cs 打印一次命中预测
#include "instant_plant_predict.h"
#include <dsl/shorthand.h>
#include <avz.h>

namespace {
ATickRunner PredictLogger;

void LogAllPredictions() {
    if (AGetMainObject()->GameClock() % 100 != 1)
        return;

    // 以 (3, 5) 为例，四种植物各看一次；模仿者会多 320cs 的变身延迟
    for (APlantType type : {ACHERRY_BOMB, AJALAPENO, ADOOM_SHROOM, ASQUASH}) {
        ALogInstantPlantPrediction(3, 5, type, false);
        ALogInstantPlantPrediction(3, 5, type, true);
    }

    // 倭瓜：额外看看 200cs 内会自己走进攻击范围的僵尸
    AInstantPlantPredictOptions squashOption;
    squashOption.squashWaitingCs = 200;
    ALogInstantPlantPrediction(3, 5, ASQUASH, false, squashOption);

    // 只要地址的用法
    auto targets = APredictInstantPlantTargets(3, 5, ACHERRY_BOMB);
    for (AZombie* zombie : targets)
        aLogger->Info("[Predict] cherry target addr={}", static_cast<const void*>(zombie));
}
}  // namespace

void AScript() {
    ASetGameSpeed(10);
    PredictLogger.Start(LogAllPredictions);
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
