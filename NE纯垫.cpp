#include "ShowWavelength/ShowWavelength.h"
#include "SmartRemove.h"  
#include "common.h"
// LI5HzH3tAiZ/13pXSFw4Ud0cOkEMn+RCdrbkRlg5VuJS+FkM33DmRnXW/R9UlzBUrVROhFY=

const std::vector<int> meatshieldHYKscq = {20, 40, 75, 60};
const std::vector<int> meatshieldHYBscq = {0, 100, 150, 400};
const std::vector<float> meatshieldbantimescq = {0.65, 0.65, 0.65, 0.9};
const std::vector<int> meatshieldGLKscq = {0, 5, 10, 5};
const std::vector<int> meatshieldGLBscq = {0, 200, 400, 600};
const std::vector<int> meatshieldCGCscq = {0, 30, 20, 10};

std::vector<int> meatshieldHYK = {0, 0, 0, 0, 0};
std::vector<int> meatshieldHYB = {0, 0, 0, 0, 0};
std::vector<float> meatshieldbantime = {0, 0, 0, 0, 0};
std::vector<int> meatshieldGLK = {0, 0, 0, 0, 0};
std::vector<int> meatshieldGLB = {0, 0, 0, 0, 0};
std::vector<int> meatshieldCGC = {0, 0, 0, 0, 0};
std::vector<AGrid> PumpkinPOS1 = {{2, 7}, {4, 7}, {3, 7}, {1, 7}, {5, 7}, {2, 1}, {3, 1}, {4, 1}, {5, 1}, {1, 1}, {1, 6}, {2, 6}, {3, 6}, {4, 6}, {5, 6}};

void Logic() {
    Fix_Gloom(3, 7);
    Fix_Gloom(2, 7);
    Fix_Gloom(4, 7);
    Fix_Gloom(1, 7);
    Fix_Gloom(5, 7);
    meatshieldHYK[0] = meatshieldHYKscq[Check_Plant(AGLOOM_SHROOM, 1, 7) + Check_Plant(AGLOOM_SHROOM, 2, 7)];
    meatshieldHYB[0] = meatshieldHYBscq[Check_Plant(AGLOOM_SHROOM, 1, 7) + Check_Plant(AGLOOM_SHROOM, 2, 7)];
    meatshieldGLK[0] = meatshieldGLKscq[Check_Plant(AGLOOM_SHROOM, 1, 7) + Check_Plant(AGLOOM_SHROOM, 2, 7)];
    meatshieldGLB[0] = meatshieldGLBscq[Check_Plant(AGLOOM_SHROOM, 1, 7) + Check_Plant(AGLOOM_SHROOM, 2, 7)];
    meatshieldbantime[0] = meatshieldbantimescq[Check_Plant(AGLOOM_SHROOM, 1, 7) + Check_Plant(AGLOOM_SHROOM, 2, 7)];
    meatshieldCGC[0] = Check_Plant(AGLOOM_SHROOM, 1, 7) * (meatshieldCGCscq[Check_Plant(AGLOOM_SHROOM, 1, 7) + Check_Plant(AGLOOM_SHROOM, 2, 7)]);
    for (int i : {1, 2, 3, 4}) {
        meatshieldHYK[i] = meatshieldHYKscq[Check_Plant(AGLOOM_SHROOM, i, 7) + Check_Plant(AGLOOM_SHROOM, i + 1, 7) + Check_Plant(AGLOOM_SHROOM, i + 2, 7)];
        meatshieldHYB[i] = meatshieldHYBscq[Check_Plant(AGLOOM_SHROOM, i, 7) + Check_Plant(AGLOOM_SHROOM, i + 1, 7) + Check_Plant(AGLOOM_SHROOM, i + 2, 7)];
        meatshieldGLK[i] = meatshieldGLKscq[Check_Plant(AGLOOM_SHROOM, i, 7) + Check_Plant(AGLOOM_SHROOM, i + 1, 7) + Check_Plant(AGLOOM_SHROOM, i + 2, 7)];
        meatshieldGLB[i] = meatshieldGLBscq[Check_Plant(AGLOOM_SHROOM, i, 7) + Check_Plant(AGLOOM_SHROOM, i + 1, 7) + Check_Plant(AGLOOM_SHROOM, i + 2, 7)];
        meatshieldbantime[i] = meatshieldbantimescq[Check_Plant(AGLOOM_SHROOM, i, 7) + Check_Plant(AGLOOM_SHROOM, i + 1, 7) + Check_Plant(AGLOOM_SHROOM, i + 2, 7)];
        meatshieldCGC[i] = Check_Plant(AGLOOM_SHROOM, i + 1, 7) * meatshieldCGCscq[Check_Plant(AGLOOM_SHROOM, i, 7) + Check_Plant(AGLOOM_SHROOM, i + 1, 7) + Check_Plant(AGLOOM_SHROOM, i + 2, 7)];
    }

    for (auto& Item : aAlivePlaceItemFilter) {
        if (Item.Type() == 1) {
            Use_Card(AGRAVE_BUSTER, Item.Row() + 1, Item.Col() + 1);
            break;
        }
    }

    if (AIsSeedUsable(APUMPKIN)) {
        std::vector<AGrid> PumpkinPOS1Now = {};
        for (auto& Grid : PumpkinPOS1) {
            if (Check_Zombie(AGIGA_GARGANTUAR, -1, Grid.row, -1000, Grid.col * 80 + 40 + 1 + 40) || Check_Zombie(AGARGANTUAR, -1, Grid.row, -1000, Grid.col * 80 + 40 + 1 + 40)) {
                continue;
            }
            if (Check_Plant(-1, Grid.row, Grid.col) == 0) {
                continue;
            }
            PumpkinPOS1Now.push_back(Grid);
        }
        AGrid mingrid = {0, 0};
        int minhp = 2000;
        for (auto& Grid : PumpkinPOS1Now) {
            if (Check_Plant(APUMPKIN, Grid.row, Grid.col) == 0) {
                minhp = 0;
                mingrid = Grid;
                break;
            }
            if (AGetPlantPtr(Grid.row, Grid.col, APUMPKIN)->Hp() < minhp) {
                minhp = AGetPlantPtr(Grid.row, Grid.col, APUMPKIN)->Hp();
                mingrid = Grid;
            }
        }
        if (minhp < 2000) {
            Use_Card(APUMPKIN, mingrid.row, mingrid.col);
        }
    }

    for (int Cost : {0, 25, 50, 75, 100}) {
        int Maxpiancha[5][2] = {{0, 0}, {1, 0}, {2, 0}, {3, 0}, {4, 0}};
        int MaxHYpiancha[5] = {0, 0, 0, 0, 0};
        int MaxGLpiancha[5] = {0, 0, 0, 0, 0};
        int Col = 7;
        int LimitX = Col * 80 + 30 + 40 + 1;

        for (auto& Zombie : aAliveZombieFilter) {
            if ((Zombie.Type() == AGIGA_GARGANTUAR || Zombie.Type() == AGARGANTUAR) && (Zombie.State() == 0) && LimitX - 20 <= Zombie.Abscissa() && Zombie.Abscissa() <= LimitX + 80) {
                int HYpiancha;
                if (Check_Plant(APUMPKIN, Zombie.Row() + 1, 7) == 1)
                    HYpiancha = (Zombie.Hp() - ((Zombie.Abscissa() - LimitX) * meatshieldHYK[Zombie.Row()] * (1 + Cost * 0.02 + 0.4 * GetExtraCardCdSum() / (7 * 750) - 0.2) + meatshieldHYB[Zombie.Row()]));
                else
                    HYpiancha = (Zombie.Hp() - ((Zombie.Abscissa() - (LimitX - 20)) * meatshieldHYK[Zombie.Row()] * (1 + Cost * 0.02 + 0.4 * GetExtraCardCdSum() / (7 * 750) - 0.2) + meatshieldHYB[Zombie.Row()]));
                if (HYpiancha > MaxHYpiancha[Zombie.Row()]) {
                    MaxHYpiancha[Zombie.Row()] = HYpiancha;
                }
            } else if ((Zombie.Type() == AFOOTBALL_ZOMBIE) && (LimitX - 70) <= Zombie.Abscissa() && Zombie.Abscissa() <= LimitX) {
                int GLpiancha = (Zombie.Hp() + Zombie.OneHp() - ((Zombie.Abscissa() - (LimitX - 70)) * meatshieldGLK[Zombie.Row()] * (1 + Cost * 0.5) + meatshieldGLB[Zombie.Row()]));
                if (GLpiancha > 0) {
                    if (Check_Plant(APUMPKIN, Zombie.Row() + 1, 7) == 0) {
                        MaxGLpiancha[Zombie.Row()] += GLpiancha * 4;
                    } else {
                        MaxGLpiancha[Zombie.Row()] += GLpiancha;
                    }
                }
            } else if ((Zombie.Type() == APOLE_VAULTING_ZOMBIE) && (560) <= Zombie.Abscissa() && Zombie.Abscissa() <= 685 && Zombie.State() == 11) {
                if (Check_Plant(APUMPKIN, Zombie.Row() + 1, 7) == 0) {
                    MaxGLpiancha[Zombie.Row()] -= meatshieldCGC[Zombie.Row()] * 4;
                } else {
                    MaxGLpiancha[Zombie.Row()] -= meatshieldCGC[Zombie.Row()];
                }
            }
        }
        for (int i : {0, 1, 2, 3, 4}) {
            Maxpiancha[i][1] = MaxHYpiancha[i] * 8 + MaxGLpiancha[i];
        }
        for (int i = 0; i < 5; ++i) {
            for (int j = i + 1; j < 5; ++j) {
                if (Maxpiancha[i][1] < Maxpiancha[j][1]) {
                    int tmp0 = Maxpiancha[i][0], tmp1 = Maxpiancha[i][1];
                    Maxpiancha[i][0] = Maxpiancha[j][0];
                    Maxpiancha[i][1] = Maxpiancha[j][1];
                    Maxpiancha[j][0] = tmp0;
                    Maxpiancha[j][1] = tmp1;
                }
            }
        }

        for (int Order : {0, 1, 2, 3, 4}) {
            bool ban = false;
            for (auto& Zombie : aAliveZombieFilter) {
                if ((Zombie.Type() == AGIGA_GARGANTUAR || Zombie.Type() == AGARGANTUAR) && Zombie.Row() == Maxpiancha[Order][0] && LimitX < Zombie.Abscissa() && Zombie.Abscissa() < LimitX + 50 && Zombie.State() == 70 && 0.1 <= AGetPvzBase()->AnimationMain()->AnimationOffset()->AnimationArray()[Zombie.MRef<uint16_t>(0x118)].CirculationRate() && AGetPvzBase()->AnimationMain()->AnimationOffset()->AnimationArray()[Zombie.MRef<uint16_t>(0x118)].CirculationRate() <= meatshieldbantime[Maxpiancha[Order][0]]) {
                    ban = 1;
                    break;
                }
            }
            if (!ban) {
                if (Maxpiancha[Order][1] > 0)
                    Use_Meatshield(Maxpiancha[Order][0] + 1, 8, Cost);
            }
            if (Maxpiancha[Order][1] <= -80 && (Check_Plant(ABLOVER, Maxpiancha[Order][0] + 1, 8) == 0) && (Check_Plant(-1, Maxpiancha[Order][0] + 1, 8) > 0))
                AShovel(Maxpiancha[Order][0] + 1, 8);
        }
    }

    bool needblover = false;
    std::vector<bool> canblover = {true, true, true, true, true};
    for (auto& Zombie : aAliveZombieFilter) {
        if (Zombie.Type() == ABALLOON_ZOMBIE && int(Zombie.Abscissa() - BalloonΔX(50 + GetJRcountmin() * 120, Zombie.Speed(), Zombie.SlowCountdown())) <= -100) {
            needblover = true;
        }
    }
    if (needblover) {
        for (auto& Zombie : aAliveZombieFilter) {
            if ((Zombie.Type() == AGIGA_GARGANTUAR || Zombie.Type() == AGARGANTUAR) && Zombie.State() == 70 && 0.4 <= AGetPvzBase()->AnimationMain()->AnimationOffset()->AnimationArray()[Zombie.MRef<uint16_t>(0x118)].CirculationRate() && AGetPvzBase()->AnimationMain()->AnimationOffset()->AnimationArray()[Zombie.MRef<uint16_t>(0x118)].CirculationRate() <= 0.65) {
                canblover[Zombie.Row()] = false;
            }
        }
        for (int Row : {0, 1, 2, 3, 4}) {
            if (canblover[Row] == true) {
                SafeCard(ABLOVER, Row + 1, 9, 51);
                SafeCard(ABLOVER, Row + 1, 8, 51);
            }
        }
    }
}

ATickRunner T1;
int Speed = 5;
void AScript() {
    ASetReloadMode(AReloadMode::MAIN_UI_OR_FIGHT_UI);
    AMaidCheats::CallPartner();
    ASetGameSpeed(Speed);
    AGetInternalLogger()->SetLevel({});
    std::vector<int> zombielist = ACreateRandomTypeList("普", "");
    std::erase(zombielist, 20);
    ASetZombies(zombielist, ASetZombieMode::INTERNAL);

    auto Zombie_Type = AGetMainObject()->ZombieTypeList();
    std::vector<int> Cardlist = {APUMPKIN, AGLOOM_SHROOM, AFUME_SHROOM, AM_PUFF_SHROOM, APUFF_SHROOM, AFLOWER_POT, ASUN_SHROOM, ASCAREDY_SHROOM};
    if (AGetMainObject()->CompletedRounds() % 2) {
        Cardlist.push_back(AGRAVE_BUSTER);
    }
    if (Zombie_Type[ABALLOON_ZOMBIE]) {
        Cardlist.push_back(ABLOVER);
    }
    Cardlist.push_back(ASUNFLOWER);
    Cardlist.push_back(AGARLIC);
    Cardlist.resize(10);
    ASelectCards(Cardlist, 1);

    T1.Start(Logic, ATickRunner::GLOBAL);
    smart_remove.Start();
    ShowWavelength(false, 5, true);

    for (int i : {1, 5, 2, 3, 4}) {
        for (int j : {9, 8}) {
            OnWave(1, 10) At(-599 + 751 * 2)[=] { Use_Meatshield(i, j, 25); };
            OnWave(1, 10) At(-599 + 751)[=] { Use_Meatshield(i, j, 25); };
            OnWave(1, 10) At(-599)[=] { Use_Meatshield(i, j, 25); };
        }
    }

    if (AGetMainObject()->CompletedRounds() % 2) {
        for (int i : {1, 2, 3, 4, 5}) {
            OnWave(20) At(-752 + 751 * 2)[=] { Use_Meatshield(i, 8, 25); };
            OnWave(20) At(-752 + 751)[=] { Use_Meatshield(i, 9, 25); };
            OnWave(20) At(-752)[=] { Use_Meatshield(i, 9, 25); };
        }
    } else {
        for (int i : {1, 2, 3, 4, 5}) {
            OnWave(20) At(-750 + 751 * 2)[=] { Use_Meatshield(i, 9, 25); };
            OnWave(20) At(-750 + 751)[=] { Use_Meatshield(i, 9, 25); };
            OnWave(20) At(-750)[=] { Use_Meatshield(i, 8, 25); };
        }
    }

    OnWave(1) {
        At(-20)[=] {AMaidCheats::Move();
}
}
;
OnWave(1) {
    At(20)[=] {AMaidCheats::CallPartner();
}
}
;

static bool isPaused = false;
AConnect('Z', [] {
    isPaused = !isPaused;
    ASetAdvancedPause(isPaused, 0, 0);
});
AConnect('X', [] {
    isPaused = false;
    ASetAdvancedPause(isPaused, 0, 0);
    AConnect(ANowDelayTime(1), [] {
        isPaused = !isPaused;
        ASetAdvancedPause(isPaused, 0, 0);
    });
});
AConnect('C', [] { AGetPvzBase()->TickMs() = AGetPvzBase()->TickMs() == 10 / Speed ? 10 : 10 / Speed; });
AConnect('V', [] { AMRef<int>(0x416DBE) = AMRef<int>(0x416DBE) == 699999 ? 100001 : 699999; });
}
