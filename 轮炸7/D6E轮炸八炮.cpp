#include <avz.h>
#include <dsl/shorthand.h>

ATickRunner tickAfterInject;
ATickRunner T1;
ATickRunner ShovelCheckRunner;

std::vector<AZombie*> ZombieCache;

void RefreshZombieCache() {
    ZombieCache.clear();
    for (auto& Zombie : aAliveZombieFilter)
        ZombieCache.push_back(&Zombie);
}

int Check_Plant(auto Type = -1, int Row = 0, int Col = 0, int HP = 10000) {
    int result = 0;
    for (auto& Plant : aAlivePlantFilter)
        if ((Type == -1 || Plant.Type() == Type) && (Row == 0 || Plant.Row() == Row - 1) && (Col == 0 || Plant.Col() == Col - 1) && Plant.Hp() <= HP)
            ++result;
    return result;
}

int Check_Zombie(auto Type = -1, int State = -1, int Row = 0, int MinAbscissa = -1000, int MaxAbscissa = 1000, int HP = 0, int Wave = 0) {
    int result = 0;
    for (auto* Zombie : ZombieCache)
        if ((Type == -1 || Zombie->Type() == Type) && (State == -1 || Zombie->State() == State) && (Row == 0 || Zombie->Row() == Row - 1) && MinAbscissa <= Zombie->Abscissa() && Zombie->Abscissa() <= MaxAbscissa && Zombie->Hp() >= HP && (Wave == 0 || Zombie->AtWave() == (Wave - 1)))
            ++result;
    return result;
}

void Use_Card(auto Type, int Row, int Col) {
    if (AIsSeedUsable(Type) && AAsm::GetPlantRejectType(Type, Row - 1, Col - 1) == AAsm::NIL)
        ACard(Type, Row, Col);
}

void Use_Meatshield(int Row, int Col) {
    for (auto Meatshield : {AM_PUFF_SHROOM, APUFF_SHROOM}) {
        if (!Check_Plant(AFLOWER_POT, Row, Col) && !Check_Zombie(AZOMBONI, 0, Row, -1000, Col * 80 + 5) && !Check_Zombie(ACATAPULT_ZOMBIE, 0, Row, -1000, Col * 80 + 5)) {
            Use_Card(Meatshield, Row, Col);
        }
    }
}

float BalloonΔX(int Time, float Speed, int SlowCountdown = 0) {
    if (!SlowCountdown)
        return Speed * Time; // 原速 × 总时间
    if (SlowCountdown > Time)
        return 0.4 * Speed * Time; // 减速 × 总时间
    return 0.4 * Speed * (SlowCountdown - 1) + Speed * (Time - (SlowCountdown - 1));
    // 减速 × (减速倒计时 - 1) + 原速 × (总时间 - (减速倒计时 - 1))
}

int RealSeedCD(APlantType Type) {
    if (AMRef<int>(0x6A9EC0, 0x768, 0x138, 0x28) == Type)
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

void ShovelCheck() {
    if (AGetMainObject()->GameClock() % 100 != 0)
        return;

    int CobCount = 0;
    for (auto& Plant : aAlivePlantFilter)
        if (Plant.Type() == ACOB_CANNON)
            ++CobCount;
    if (CobCount < 8 || RealSeedCD(ACOB_CANNON) != 0)
        return;

    int MinHp = 200;
    int MinHpRow = 0;
    int MinHpCol = 0;
    for (auto [Row, Col] : std::vector<std::pair<int, int>> {{1, 5}, {2, 5}, {3, 5}, {4, 5}, {5, 5}, {6, 5}, {3, 1}, {5, 1}}) {
        int CobIndex = AGetPlantIndex(Row, Col, ACOB_CANNON);
        if (CobIndex < 0 || AGetMainObject()->Sun() <= 9000)
            continue;

        auto Cob = AGetMainObject()->PlantArray() + CobIndex;
        // 只铲正在装填/空炮蓄能、且剩余装填超过 20s 的炮；正在发射和准备就绪都不铲。
        if (Cob->State() == 37 || Cob->State() == 38)
            continue;
        if (AGetCobRecoverTime(CobIndex) <= 2000 || Cob->Hp() >= MinHp)
            continue;

        MinHp = Cob->Hp();
        MinHpRow = Row;
        MinHpCol = Col;
    }
    if (MinHpRow)
        ARemovePlant(MinHpRow, MinHpCol, ACOB_CANNON);
}

void Logic() {
    for (int Row : {1, 6, 5, 3, 2, 4}) {
        for (int Col : {5, 6})
            Use_Card(AKERNEL_PULT, Row, Col);
        Use_Card(ACOB_CANNON, Row, 5);
    }

    // 新增 2-1、4-1 两门炮：补玉米再升级成炮。
    for (int Row : {3, 5}) {
        Use_Card(AKERNEL_PULT, Row, 1);
        Use_Card(AKERNEL_PULT, Row, 2);
        Use_Card(ACOB_CANNON, Row, 1);
    }

    if (RealSeedCD(AGLOOM_SHROOM) == 0) {
        // 原有 3-7 曾，并新增 1-1、3-1、5-1 曾。
        for (auto [Row, Col] : std::vector<std::pair<int, int>> {{3, 7}, {2, 7}, {4, 7}, {5, 7}, {4, 1}, {6, 1}}) {
            Use_Card(AFUME_SHROOM, Row, Col);
            Use_Card(AGLOOM_SHROOM, Row, Col);
            Use_Card(ACOFFEE_BEAN, Row, Col);
        }
    }

    aCobManager.AutoSetList();
    if (AGetMainObject()->GameClock() % 870 == 0)
        aCobManager.Fire(2, 8.7);
    if (AGetMainObject()->GameClock() % 870 == 435)
        aCobManager.Fire(5, 8.7);

    RefreshZombieCache();

    bool FixPumpkin = true;

    for (int Row = 1; Row <= 6; ++Row) {
        for (int Col = 1; Col <= 9; ++Col) {
            if (AGetPlantIndex(Row, Col, APUMPKIN) >= 0) {
                int result = 0;
                for (auto* Zombie : ZombieCache) {
                    if ((Zombie->Type() == AZOMBONI || Zombie->Type() == ACATAPULT_ZOMBIE) && Zombie->Row() == Row - 1 && Col * 80 + 1 < Zombie->Abscissa() && Zombie->Abscissa() < Col * 80 + 30 + 1) {
                        ARemovePlant(Row, Col, APUMPKIN);
                        FixPumpkin = false;
                        break;
                    }
                    if ((Zombie->Type() == AGIGA_GARGANTUAR || Zombie->Type() == AGARGANTUAR) && Zombie->Row() == Row - 1 && Col * 80 + 40 + 1 < Zombie->Abscissa() && Zombie->Abscissa() < Col * 80 + 30 + 40 + 1 && Zombie->State() == 70 && 0.64242 <= AGetPvzBase()->AnimationMain()->AnimationOffset()->AnimationArray()[Zombie->MRef<uint16_t>(0x118)].CirculationRate() && AGetPvzBase()->AnimationMain()->AnimationOffset()->AnimationArray()[Zombie->MRef<uint16_t>(0x118)].CirculationRate() <= 0.64485) {
                        if (AGetPlantIndex(Row, Col + 1) < 0) {
                            ARemovePlant(Row, Col, APUMPKIN);
                            FixPumpkin = false;
                            break;
                        } else {
                            ++result;
                        }
                    }
                }
                if (result >= 2 || (result == 1 && AGetPlantIndex(Row, Col, APUMPKIN) < AGetPlantIndex(Row, Col + 1))) {
                    ARemovePlant(Row, Col, APUMPKIN);
                    FixPumpkin = false;
                    break;
                }
            }
        }
    }
    for (int Row : {2, 3, 4, 5}) {
        if (FixPumpkin && AIsSeedUsable(APUMPKIN) && !Check_Zombie(AZOMBONI, 0, Row, -1000, 591)) {
            if (AGetPlantIndex(Row, 7, APUMPKIN) >= 0) {
                if (AGetPlantPtr(Row, 7, APUMPKIN)->Hp() < 1000)
                    Use_Card(APUMPKIN, Row, 7);
            } else {
                Use_Card(APUMPKIN, Row, 7);
            }
        }
    }

    // 吹气球
    for (auto* Zombie : ZombieCache) {
        if (Zombie->Type() == ABALLOON_ZOMBIE && int(Zombie->Abscissa() - BalloonΔX(851, Zombie->Speed(), Zombie->SlowCountdown())) <= -100) {
            if (!Check_Plant(ABLOVER, 1, 7))
                ARemovePlant(1, 7);
            Use_Card(ABLOVER, 1, 7);
            Use_Card(ABLOVER, 6, 7);
        }
    }
    for (int Row : {1, 6})
        if (Check_Zombie(APOLE_VAULTING_ZOMBIE, 11, Row, -1000, 605)) {
            if (!Check_Plant(ABLOVER, Row, 7) && !Check_Plant(ACHERRY_BOMB, Row, 7))
                ARemovePlant(Row, 7);
        } else if (Check_Zombie(AFOOTBALL_ZOMBIE, 0, Row, 480, 520) || Check_Zombie(AGIGA_GARGANTUAR, 0, Row, -1000, 600) || Check_Zombie(AJACK_IN_THE_BOX_ZOMBIE, 15, Row, -1000, 550) || Check_Zombie(ALADDER_ZOMBIE, 76, Row, -1000, 560))
            Use_Meatshield(Row, 7);
    for (int Row : {2, 5, 3, 4, 1, 6})
        if (Check_Zombie(AFOOTBALL_ZOMBIE, 0, Row, 560, 600) || Check_Zombie(AGIGA_GARGANTUAR, 0, Row, 600, 680) || Check_Zombie(AJACK_IN_THE_BOX_ZOMBIE, 15, Row, 580, 630) || Check_Zombie(ALADDER_ZOMBIE, 76, Row, 580, 640))
            Use_Meatshield(Row, 8);

    if (Check_Zombie(AFOOTBALL_ZOMBIE, 0, 1, -1000, 450) || Check_Zombie(AFOOTBALL_ZOMBIE, 0, 2, -1000, 450) || Check_Zombie(AZOMBONI, 0, 1, -1000, 485) || Check_Zombie(AGIGA_GARGANTUAR, 0, 1, -1000, 511)
        || Check_Zombie(ALADDER_ZOMBIE, 76, 1, -1000, 480) || Check_Zombie(AJACK_IN_THE_BOX_ZOMBIE, 15, 1, -1000, 470)) {
        if (!Check_Plant(ABLOVER, 2, 8) && !Check_Plant(ACHERRY_BOMB, 2, 8))
            ARemovePlant(2, 8);
        Use_Card(ACHERRY_BOMB, 2, 8);
    }

    if (Check_Zombie(AFOOTBALL_ZOMBIE, 0, 6, -1000, 450) || Check_Zombie(AFOOTBALL_ZOMBIE, 0, 6, -1000, 450) || Check_Zombie(AZOMBONI, 0, 6, -1000, 485) || Check_Zombie(AGIGA_GARGANTUAR, 0, 6, -1000, 511)
        || Check_Zombie(ALADDER_ZOMBIE, 76, 6, -1000, 480) || Check_Zombie(AJACK_IN_THE_BOX_ZOMBIE, 15, 6, -1000, 470)) {
        if (!Check_Plant(ABLOVER, 5, 8) && !Check_Plant(ACHERRY_BOMB, 5, 8))
            ARemovePlant(5, 8);
        Use_Card(ACHERRY_BOMB, 5, 8);
    }
}

void AScript() {
    ASetReloadMode(AReloadMode::MAIN_UI_OR_FIGHT_UI);
    ASetGameSpeed(10);
    AGetInternalLogger()->SetLevel({});
    AMaidCheats::CallPartner();
    std::vector<int> zombielist = ACreateRandomTypeList("\u666E", "");
    std::erase(zombielist, 20);
    ASetZombies(zombielist, ASetZombieMode::INTERNAL);
    std::vector<int> Cardlist = {ACOFFEE_BEAN, AFUME_SHROOM, AGLOOM_SHROOM, AKERNEL_PULT, ACOB_CANNON, APUMPKIN, ACHERRY_BOMB, ABLOVER, APUFF_SHROOM, AM_PUFF_SHROOM};
    ASelectCards(Cardlist, 1);
    AConnect([] { return AGetMainObject()->GameClock() % 10 == 1; }, [] { ASkipTick([] { return AGetMainObject()->GameClock() % 10 != 0; }); });
    T1.Start(Logic, ATickRunner::GLOBAL);
    AConnect(VK_F5, [] {
        ABackToMain();
        AEnterGame(AMRef<int>(0x6A9EC0, 0x7F8)); });
    AConnect(VK_END, [] { ATerminate(); });
    // 2-1、4-1 现在种炮，不再由南瓜修补器覆盖。
    aPlantFixer.Start(APUMPKIN, {{1, 1}, {2, 1}, {4, 1}, {6, 1}}, 1000);

    ShovelCheckRunner.Start(ShovelCheck, ATickRunner::ONLY_FIGHT);
}
