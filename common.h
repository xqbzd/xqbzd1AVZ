#pragma once
#include "avz.h"
#include <algorithm>
#include <array>

inline std::pair<int, int> GetDefenseRange(APlantType type) {
    switch (type) {
    case ATALL_NUT:
        return {30, 70};
    case APUMPKIN:
        return {20, 80};
    case ACOB_CANNON:
        return {20, 120};
    default:
        return {30, 50};
    }
}

inline std::pair<int, int> GetAttackRange(AZombieType type) {
    switch (type) {
    case AGIGA_GARGANTUAR:
    case AGARGANTUAR:
        return {-30, 59};
    default:
        return {10, 143};
    }
}

inline bool isRangeOverlap(APlantType plant_type, AZombieType zombie_type, int plant_x, int zombie_x) {
    auto plant_range = GetDefenseRange(plant_type);
    auto zombie_range = GetAttackRange(zombie_type);
    return plant_x + plant_range.second >= zombie_x + zombie_range.first && plant_x + plant_range.first <= zombie_x + zombie_range.second;
}

inline bool isSeedUsableOrHolding(APlantType Type) {
    if (Type >= 49) {
        return AIsSeedUsable(Type) || AGetMainObject()->MouseAttribution()->MRef<int>(0x2C) == Type - 49;
    } else
        return AIsSeedUsable(Type) || AGetMainObject()->MouseAttribution()->MRef<int>(0x28) == Type;
}

inline std::pair<int, int> GetExplodeRange(APlantType Type) {
    switch (Type) {
    case ATALL_NUT:
        return {10, 90};
    case APUMPKIN:
        return {0, 100};
    case ACOB_CANNON:
        return {0, 140};
    default:
        return {10, 70};
    }
}

inline std::pair<int, int> MyGridToCoordinate(double Row, double Col) {
    if (ARangeIn(AGetMainObject()->Scene(), {0, 1, 6, 7, 8, 9}))
        return {40 + (Col - 1) * 80, 80 + (Row - 1) * 100};
    else if (ARangeIn(AGetMainObject()->Scene(), {2, 3, 10, 11}))
        return {40 + (Col - 1) * 80, 80 + (Row - 1) * 85};
    return {40 + (Col - 1) * 80, 70 + (Row - 1) * 85 + (Col < 6 ? (6 - Col) * 20 : 0)};
}

inline bool PredictExplode(AZombie* Zombie, int PlantRow, int PlantCol, APlantType PlantType) {
    int JackX = Zombie->Abscissa() + 60;
    int JackY = Zombie->Ordinate() + 60;
    auto PlantCoordinate = MyGridToCoordinate(PlantRow, PlantCol);
    int PlantX = PlantCoordinate.first, PlantY = PlantCoordinate.second;
    int YDistance = 0;
    if (JackY < PlantY)
        YDistance = PlantY - JackY;
    else if (JackY > PlantY + 80)
        YDistance = JackY - (PlantY + 80);
    if (YDistance > 90)
        return false;
    int XDistance = sqrt(90 * 90 - YDistance * YDistance);
    auto Range = GetExplodeRange(PlantType);
    return PlantX + Range.first - XDistance <= JackX && JackX <= PlantX + Range.second + XDistance;
}

#include <dsl/shorthand.h>

inline void SafeCard(APlantType PlantType, int Row, int Col, int NeedTime = 99) {
    if (!isSeedUsableOrHolding(PlantType))
        return;
    bool RoofUnderCarsExplode = false;
    if (AAsm::GetPlantRejectType(ACHERRY_BOMB, Row - 1, Col - 1) == AAsm::NEEDS_POT) {
        auto Coordinate = MyGridToCoordinate(Row, Col);
        int Abscissa = Coordinate.first;
        for (auto& Zombie : aAliveZombieFilter) {
            if (ARangeIn(Zombie.Type(), {AZOMBONI, ACATAPULT_ZOMBIE}) && Zombie.Row() + 1 == Row && isRangeOverlap(ACHERRY_BOMB, AZOMBONI, Abscissa, int(Zombie.Abscissa()))) {
                RoofUnderCarsExplode = true;
                break;
            }
        }
    }
    for (auto& Zombie : aAliveZombieFilter) {
        if (Zombie.Type() == AJACK_IN_THE_BOX_ZOMBIE && Zombie.State() == 16 && PredictExplode(&Zombie, Row, Col, PlantType)) {
            if (RoofUnderCarsExplode) {
                if (Zombie.StateCountdown() == 1) {
                    AConnect(ANowDelayTime(1), [=] { SafeCard(PlantType, Row, Col, NeedTime); });
                    return;
                }
            } else if (Zombie.StateCountdown() <= NeedTime) {
                AConnect(ANowDelayTime(1), [=] { SafeCard(PlantType, Row, Col, NeedTime); });
                return;
            }
        }
    }
    At(now) Card(PlantType, Row, Col);
}

inline void SafeCardByJackOnly(APlantType PlantType, int Row, int Col, int NeedTime = 99) {
    if (!isSeedUsableOrHolding(PlantType))
        return;
    for (auto& Zombie : aAliveZombieFilter) {
        if (Zombie.Type() == AJACK_IN_THE_BOX_ZOMBIE && Zombie.State() == 16 && PredictExplode(&Zombie, Row, Col, PlantType) && Zombie.StateCountdown() <= NeedTime) {
            AConnect(ANowDelayTime(1), [=] { SafeCardByJackOnly(PlantType, Row, Col, NeedTime); });
            return;
        }
    }
    At(now) Card(PlantType, Row, Col);
}

inline int Check_Plant(auto Type = -1, int Row = 0, int Col = 0, int HP = 10000) {
    int result = 0;
    for (auto& Plant : aAlivePlantFilter)
        if ((Type == -1 || Plant.Type() == Type) && (Row == 0 || Plant.Row() == Row - 1) && (Col == 0 || Plant.Col() == Col - 1) && Plant.Hp() <= HP)
            ++result;
    return result;
}

inline int Check_Zombie(auto Type = -1, int State = -1, int Row = 0, int MinAbscissa = -1000, int MaxAbscissa = 1000, int HP = 0, int Wave = 0) {
    int result = 0;
    for (auto& Zombie : aAliveZombieFilter)
        if ((Type == -1 || Zombie.Type() == Type) && (State == -1 || Zombie.State() == State) && (Row == 0 || Zombie.Row() == Row - 1) && MinAbscissa <= Zombie.Abscissa() && Zombie.Abscissa() <= MaxAbscissa && (Zombie.Hp() + Zombie.OneHp() + Zombie.TwoHp()) >= HP && (Wave == 0 || Zombie.AtWave() == (Wave - 1)))
            ++result;
    return result;
}

inline bool Use_Card(auto Type, int Row, int Col) {
    if (AIsSeedUsable(Type) && AAsm::GetPlantRejectType(Type, Row - 1, Col - 1) == AAsm::NIL) {
        ACard(Type, Row, Col);
        return true;
    }
    return false;
}

inline void Fix_Gloom(int Row, int Col) {
    if (!Check_Zombie(AGIGA_GARGANTUAR, -1, Row, -1000, Col * 80 + 40 + 1 + 10) && !Check_Zombie(AGARGANTUAR, -1, Row, -1000, Col * 80 + 40 + 1 + 10) && !Check_Zombie(AZOMBONI, -1, Row, -1000, Col * 80 + 1 + 10) && !Check_Zombie(AFOOTBALL_ZOMBIE, -1, Row, -1000, Col * 80 - 40 + 1 + 10, 90)) {
        if (!Check_Plant(AFUME_SHROOM, Row, Col) && AIsSeedUsable(AFUME_SHROOM))
            Use_Card(AFUME_SHROOM, Row, Col);
        if (!Check_Plant(AGLOOM_SHROOM, Row, Col) && AIsSeedUsable(AGLOOM_SHROOM))
            Use_Card(AGLOOM_SHROOM, Row, Col);
    }
}

inline void Fix_Pumpkin(int Row, int Col) {
    if (!Check_Zombie(AGIGA_GARGANTUAR, -1, Row, -1000, Col * 80 + 40 + 1 + 40) && !Check_Zombie(AGARGANTUAR, -1, Row, -1000, Col * 80 + 40 + 1 + 40)) {
        Use_Card(APUMPKIN, Row, Col);
    }
}

inline int BloverSunValfixed(auto Type) {
    if (Type == ABLOVER) {
        int minabscisa = 820;
        for (auto& Zombie : aAliveZombieFilter) {
            if (Zombie.Type() == ABALLOON_ZOMBIE && int(Zombie.Abscissa()) <= minabscisa) {
                minabscisa = int(Zombie.Abscissa());
            }
        }
        return (int(100 * (minabscisa + 100) / 920 - 12));
    } else {
        return (AGetSeedSunVal(Type));
    }
}

inline bool CanUseShieldAt(int Row, int Col) {
    return !Check_Plant(AFLOWER_POT, Row, Col);
}

inline bool HasVehicleThreat(int Row, int Col) {
    return Check_Zombie(AZOMBONI, 0, Row, -1000, Col * 80 + 5) || Check_Zombie(ACATAPULT_ZOMBIE, 0, Row, -1000, Col * 80 + 5);
}

inline bool Use_Meatshield(int Row, int Col, int MaxCost = 1000) {
    if (!CanUseShieldAt(Row, Col))
        return false;

    if (HasVehicleThreat(Row, Col)) {
        return Use_Card(ABLOVER, Row, Col);
    }

    static constexpr std::array<APlantType, 9> meatshields = {AM_PUFF_SHROOM, APUFF_SHROOM, AFLOWER_POT, ASCAREDY_SHROOM, ASUN_SHROOM, ASUNFLOWER, AGARLIC, AFUME_SHROOM, ABLOVER};
    for (auto Meatshield : meatshields) {
        const int sunVal = Meatshield == ABLOVER ? BloverSunValfixed(Meatshield) : AGetSeedSunVal(Meatshield);
        if (sunVal > MaxCost)
            continue;
        if (Meatshield == ABLOVER) {
            if (isSeedUsableOrHolding(Meatshield)) {
                SafeCard(Meatshield, Row, Col, 51);
                return true;
            }
        } else if (Use_Card(Meatshield, Row, Col)) {
            return true;
        }
    }
    return false;
}

inline bool Use_IceShield(int Row, int Col, int = 1000) {
    if (!CanUseShieldAt(Row, Col))
        return false;
    if (isSeedUsableOrHolding(AICE_SHROOM)) {
        SafeCardByJackOnly(AICE_SHROOM, Row, Col, 100);
        return true;
    }
    return false;
}

inline float BalloonΔX(int Time, float Speed, int SlowCountdown = 0) {
    if (!SlowCountdown)
        return Speed * Time;
    if (SlowCountdown > Time)
        return 0.4 * Speed * Time;
    return 0.4 * Speed * (SlowCountdown - 1) + Speed * (Time - (SlowCountdown - 1));
}

inline int RealSeedCD(APlantType Type) {
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

inline int GetExtraCardCdSum() {
    int sum = 0;
    for (auto seed : {AM_PUFF_SHROOM, APUFF_SHROOM, AFLOWER_POT, ASCAREDY_SHROOM, ASUN_SHROOM, ASUNFLOWER, AGARLIC, AFUME_SHROOM, ABLOVER}) {
        sum += std::max(RealSeedCD(seed), 0);
    }
    return sum;
}

inline int GetJRcountmin() {
    int result = 10000;
    for (int i = 1; i <= 5; ++i) {
        int temp = Check_Zombie(AGIGA_GARGANTUAR, -1, i) + Check_Zombie(AGARGANTUAR, -1, i);
        if (result > temp) {
            result = temp;
        }
    }
    return result;
}
