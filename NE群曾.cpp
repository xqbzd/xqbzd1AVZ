#include "ShowWavelength/ShowWavelength.h"
#include <avz.h>
#include <dsl/shorthand.h>
#include <vector>

std::pair<int, int> MyGridToCoordinate(double Row, double Col) {
    if (ARangeIn(AGetMainObject()->Scene(), {0, 1, 6, 7, 8, 9}))
        return {40 + (Col - 1) * 80, 80 + (Row - 1) * 100};
    else if (ARangeIn(AGetMainObject()->Scene(), {2, 3, 10, 11}))
        return {40 + (Col - 1) * 80, 80 + (Row - 1) * 85};
    return {40 + (Col - 1) * 80, 70 + (Row - 1) * 85 + (Col < 6 ? (6 - Col) * 20 : 0)}; // 天台
}

std::pair<int, int> GetExplodeRange(APlantType Type) {
    switch (Type) {
    case ATALL_NUT:
        return {10, 90};
    case APUMPKIN:
        return {0, 100};
    case ACOB_CANNON:
        return {0, 140};
    default:
        return {10, 70}; // 普通
    }
}
// 检测植物
int Check_Plant(auto Type = -1, int Row = 0, int Col = 0, int HP = 10000) {
    int result = 0;
    for (auto& Plant : aAlivePlantFilter)
        if ((Type == -1 || Plant.Type() == Type) && (Row == 0 || Plant.Row() == Row - 1) && (Col == 0 || Plant.Col() == Col - 1) && Plant.Hp() <= HP)
            ++result;
    return result;
}
// 检测僵尸
int Check_Zombie(auto Type = -1, int State = -1, int Row = 0, int MinAbscissa = -1000, int MaxAbscissa = 1000, int HP = 0, int Wave = 0) {
    int result = 0;
    for (auto& Zombie : aAliveZombieFilter)
        if ((Type == -1 || Zombie.Type() == Type) && (State == -1 || Zombie.State() == State) && (Row == 0 || Zombie.Row() == Row - 1) && MinAbscissa <= Zombie.Abscissa() && Zombie.Abscissa() <= MaxAbscissa && (Zombie.Hp() + Zombie.OneHp() + Zombie.TwoHp()) >= HP && (Wave == 0 || Zombie.AtWave() == (Wave - 1)))
            ++result;
    return result;
}
// 用卡
bool Use_Card(auto Type, int Row, int Col) {
    if (AIsSeedUsable(Type) && AAsm::GetPlantRejectType(Type, Row - 1, Col - 1) == AAsm::NIL) {
        ACard(Type, Row, Col);
        return true;
    }
    return false;
}
// 补曾
void Fix_Gloom(int Row, int Col) {
    if (!Check_Zombie(AGIGA_GARGANTUAR, -1, Row, -1000, Col * 80 + 40 + 1 + 10) && !Check_Zombie(AGARGANTUAR, -1, Row, -1000, Col * 80 + 40 + 1 + 10) && !Check_Zombie(AZOMBONI, -1, Row, -1000, Col * 80 + 1 + 10) && !Check_Zombie(AFOOTBALL_ZOMBIE, -1, Row, -1000, Col * 80 - 40 + 1 + 10, 90)) {
        if (!Check_Plant(AFUME_SHROOM, Row, Col) && AIsSeedUsable(AFUME_SHROOM))
            Use_Card(AFUME_SHROOM, Row, Col);
        if (!Check_Plant(AGLOOM_SHROOM, Row, Col) && AIsSeedUsable(AGLOOM_SHROOM))
            Use_Card(AGLOOM_SHROOM, Row, Col);
    }
}
// 用垫
void Use_Meatshield(int Row, int Col, bool AllowBlover = true) {
    for (auto Meatshield : {APUFF_SHROOM, AFLOWER_POT, ASUN_SHROOM, ASCAREDY_SHROOM, ASUNFLOWER, AFUME_SHROOM, ABLOVER}) {
        if (Meatshield == ABLOVER && !AllowBlover)
            continue;
        if (!Check_Plant(AFLOWER_POT, Row, Col)) {
            if (!Check_Zombie(AZOMBONI, 0, Row, -1000, Col * 80 + 5) && !Check_Zombie(ACATAPULT_ZOMBIE, 0, Row, -1000, Col * 80 + 5)) {
                Use_Card(Meatshield, Row, Col);
            } else if (AllowBlover) {
                Use_Card(ABLOVER, Row, Col);
            }
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
bool BalloonWillEnterHomeIn(int Time) {
    for (auto& Zombie : aAliveZombieFilter) {
        if (Zombie.Type() == ABALLOON_ZOMBIE && int(Zombie.Abscissa() - BalloonΔX(Time, Zombie.Speed(), Zombie.SlowCountdown())) <= -100)
            return true;
    }
    return false;
}

bool GigaWillSmashIn50cs(int Row, int Col) {
    for (auto& Zombie : aAliveZombieFilter) {
        if ((Zombie.Type() != AGIGA_GARGANTUAR && Zombie.Type() != AGARGANTUAR) || Zombie.State() != 70 || Zombie.Row() != Row - 1)
            continue;
        const float x = Zombie.Abscissa();
        if (!(Col * 80 + 40 + 1 < x && x < Col * 80 + 30 + 40 + 1))
            continue;
        const float rate = Zombie.AnimationPtr()->CirculationRate();
        if (0.4f <= rate && rate <= 0.65f)
            return true;
    }
    return false;
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

// 预测小丑是否可炸到(Row, Col)格Type类型植物
bool PredictExplode(AZombie* Zombie, int PlantRow, int PlantCol, APlantType PlantType) {
    int JackX = Zombie->Abscissa() + 60;
    int JackY = Zombie->Ordinate() + 60; // 小丑爆心偏移
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

bool JudgeExplode(APlant* Plant, AZombie* Zombie) {
    int JackX = Zombie->Abscissa() + 60;
    int JackY = Zombie->Ordinate() + 60; // 小丑爆心偏移
    int PlantX = Plant->Abscissa();
    int PlantY = Plant->Ordinate();
    int YDistance = 0;
    if (JackY < PlantY)
        YDistance = PlantY - JackY;
    else if (JackY > PlantY + 80)
        YDistance = JackY - (PlantY + 80);
    if (YDistance > 90)
        return false;
    int XDistance = sqrt(90 * 90 - YDistance * YDistance);
    auto Range = GetExplodeRange(APlantType(Plant->Type()));
    return PlantX + Range.first - XDistance <= JackX && JackX <= PlantX + Range.second + XDistance;
}

bool isSeedUsableOrHolding(APlantType Type) { return AIsSeedUsable(Type) || AMRef<int>(0x6A9EC0, 0x768, 0x138, 0x28) == Type; }

void KillJack() {
    for (auto& Zombie : aAliveZombieFilter) {
        if (Zombie.Type() == AJACK_IN_THE_BOX_ZOMBIE && Zombie.State() == 16 && Zombie.StateCountdown() == 110 && (isSeedUsableOrHolding(ACHERRY_BOMB) || isSeedUsableOrHolding(AJALAPENO) || (isSeedUsableOrHolding(ADOOM_SHROOM) && aFieldInfo.isNight))) {
            for (auto& Plant : aAlivePlantFilter) {
                if (Plant.Type() == AYYG_42) {
                    if (Plant.Row() == 2 - 1 && Plant.Col() == 6 - 1 && JudgeExplode(&Plant, &Zombie)) {
                        ARemovePlant(2, 7);
                        Use_Card(ACHERRY_BOMB, 2, 7);
                        return;
                    }
                    if (Plant.Row() == 3 - 1 && Plant.Col() == 7 - 1 && JudgeExplode(&Plant, &Zombie)) {
                        ARemovePlant(3, 8);
                        Use_Card(ACHERRY_BOMB, 3, 8);
                        return;
                    }
                    if (Plant.Row() == 4 - 1 && Plant.Col() == 6 - 1 && JudgeExplode(&Plant, &Zombie)) {
                        ARemovePlant(4, 7);
                        Use_Card(ACHERRY_BOMB, 4, 7);
                        return;
                    }
                }
            }
        }
    }
}

// 拖延至不会被小丑炸的情况下再用卡
// 需存活时间NeedTime应填写≥1的数，默认卡片需存活至99cs后
// 延迟铲除ShovelDelay默认0为不铲除
void SafeCard(APlantType PlantType, int Row, int Col, int NeedTime = 99, int ShovelDelay = 0) {
    if (AGetCardIndex(PlantType) < 0 || AGetCardIndex(PlantType) > 9 || !isSeedUsableOrHolding(PlantType))
        return; // 卡片需要带了且CD是好的
    for (auto& Zombie : aAliveZombieFilter) {
        if (Zombie.Type() == AJACK_IN_THE_BOX_ZOMBIE && Zombie.State() == 16 && PredictExplode(&Zombie, Row, Col, PlantType) && Zombie.StateCountdown() <= NeedTime) {
            AConnect(ANowDelayTime(1), [=] { SafeCard(PlantType, Row, Col, NeedTime, ShovelDelay); });
            return; // 小丑倒计时≤NeedTime，则延迟到下一帧重新判断
        }
    }
    ACard(PlantType, Row, Col); // 查完小丑后发现一切正常，则用卡
    if (ShovelDelay == 0)
        return;
    AConnect(ANowDelayTime(ShovelDelay), [=] { ARemovePlant(Row, Col, PlantType); }); // 以种植时间为参照进行延迟铲除
}
namespace {
constexpr int ROW_COUNT = 5;
std::array<int, ROW_COUNT> meatc = {};
}

int cardclock = 0;

void BalloonCaption() {
    static const std::array<AGrid, 8> BloverPositions = {{{1, 5}, {5, 5}, {1, 6}, {5, 6}, {2, 7}, {4, 7}, {1, 4}, {5, 4}}};

    auto GiantWillSmash = [](int Row, int Col) {
        for (auto& Zombie : aAliveZombieFilter) {
            if (Zombie.Row() != Row - 1 || (Zombie.Type() != AGIGA_GARGANTUAR && Zombie.Type() != AGARGANTUAR) || Zombie.State() != 70)
                continue;
            const float x = Zombie.Abscissa();
            if (!(Col * 80 + 40 + 1 < x && x < Col * 80 + 30 + 40 + 1))
                continue;
            const float rate = Zombie.AnimationPtr()->CirculationRate();
            if (0.4f <= rate && rate <= 0.65f)
                return true;
        }
        return false;
    };

    auto HasGrave = [](int Row, int Col) {
        for (auto& Item : aAlivePlaceItemFilter) {
            if (Item.Type() == APlaceItemType::GRAVESTONE && Item.Row() == Row - 1 && Item.Col() == Col - 1)
                return true;
        }
        return false;
    };

    for (auto& Zombie : aAliveZombieFilter) {
        if ((Zombie.Type() == ABALLOON_ZOMBIE && isSeedUsableOrHolding(ABLOVER)) && ((int(Zombie.Abscissa() - BalloonΔX(470, Zombie.Speed(), Zombie.SlowCountdown())) <= -100) || ANowTime(21) == -1)) {
            for (const auto& Grid : BloverPositions) {
                if (GiantWillSmash(Grid.row, Grid.col) || HasGrave(Grid.row, Grid.col) || AGetPlantIndex(Grid.row, Grid.col) >= 0)
                    continue;
                ACard(ABLOVER, Grid.row, Grid.col);
                return;
            }
            for (const auto& Grid : BloverPositions) {
                if (GiantWillSmash(Grid.row, Grid.col) || HasGrave(Grid.row, Grid.col) || AGetPlantIndex(Grid.row, Grid.col) < 0)
                    continue;
                ARemovePlant(Grid.row, Grid.col);
                ACard(ABLOVER, Grid.row, Grid.col);
                return;
            }
        }
    }
}

bool CopyIceGiantDanger(int Row, int Col) {
    const int PlantX = 40 + (Col - 1) * 80;
    const int DefenseFront = PlantX + 50;

    for (auto& Zombie : aAliveZombieFilter) {
        if (Zombie.Row() != Row - 1 || Zombie.Type() != AGIGA_GARGANTUAR)
            continue;
        const float x = Zombie.Abscissa();
        if (Zombie.State() == 0 && DefenseFront < x && x <= DefenseFront + 80)
            return true;
        if (Zombie.State() == 69 && DefenseFront < x && x <= DefenseFront + 50)
            return true;
        if (Zombie.State() == 69 && DefenseFront < x && x <= DefenseFront + 50)
            return true;
    }
    return false;
}

bool TryPlaceCopyIce(bool AllowShovel) {
    static const std::array<AGrid, 6> CopyIcePositions = {{{1, 5}, {5, 5}, {1, 6}, {5, 6}, {2, 7}, {4, 7}}};

    for (const auto& Grid : CopyIcePositions) {
        if (CopyIceGiantDanger(Grid.row, Grid.col) || AGetPlantIndex(Grid.row, Grid.col) >= 0)
            continue;
        if (AAsm::GetPlantRejectType(AM_ICE_SHROOM, Grid.row - 1, Grid.col - 1) != AAsm::NIL)
            continue;
        if (Use_Card(AM_ICE_SHROOM, Grid.row, Grid.col))
            return true;
    }

    if (!AllowShovel || !AIsSeedUsable(AM_ICE_SHROOM))
        return false;

    for (const auto& Grid : CopyIcePositions) {
        if (CopyIceGiantDanger(Grid.row, Grid.col) || AGetPlantIndex(Grid.row, Grid.col) < 0)
            continue;
        ARemovePlant(Grid.row, Grid.col);
        return false;
    }
    return false;
}

bool TryFallbackCopyIce() {
    if (!AIsSeedUsable(AM_ICE_SHROOM))
        return false;

    for (int Row : {1, 5}) {
        if (CopyIceGiantDanger(Row, 4))
            continue;
        for (auto& Plant : aAlivePlantFilter) {
            if (Plant.Row() == Row - 1 && Plant.Col() == 3 && Plant.Type() != APUMPKIN) {
                ARemovePlant(Row, 4, Plant.Type());
                return false;
            }
        }
        if (AAsm::GetPlantRejectType(AM_ICE_SHROOM, Row - 1, 3) == AAsm::NIL && Use_Card(AM_ICE_SHROOM, Row, 4))
            return true;
    }
    return false;
}

void Fix_Pumpkins() {
    if (!AIsSeedUsable(APUMPKIN))
        return;

    static const std::array<AGrid, 11> positions = {{{1, 1}, {2, 1}, {3, 1}, {4, 1}, {5, 1}, {3, 6}, {3, 7}, {2, 5}, {4, 5}}};

    std::array<std::array<bool, 10>, ROW_COUNT> hasPlant = {};
    std::array<std::array<bool, 10>, ROW_COUNT> hasPumpkin = {};
    std::array<std::array<int, 10>, ROW_COUNT> pumpkinHp = {};
    std::array<float, ROW_COUNT> leftmostGiantX;
    leftmostGiantX.fill(10000.0f);

    for (auto& Plant : aAlivePlantFilter) {
        const int row = Plant.Row();
        const int col = Plant.Col() + 1;
        if (row < 0 || ROW_COUNT <= row || col < 1 || 9 < col)
            continue;

        hasPlant[row][col] = true;
        if (Plant.Type() == APUMPKIN) {
            hasPumpkin[row][col] = true;
            pumpkinHp[row][col] = Plant.Hp();
        }
    }

    for (auto& Zombie : aAliveZombieFilter) {
        if (Zombie.Type() != AGIGA_GARGANTUAR && Zombie.Type() != AGARGANTUAR)
            continue;

        const int row = Zombie.Row();
        const float x = Zombie.Abscissa();
        if (row < 0 || ROW_COUNT <= row || x < -1000 || 1000 < x)
            continue;

        if (x < leftmostGiantX[row])
            leftmostGiantX[row] = x;
    }

    const bool OuterPumpkinLow = (hasPlant[0][1] && (!hasPumpkin[0][1] || pumpkinHp[0][1] < 800)) || (hasPlant[4][1] && (!hasPumpkin[4][1] || pumpkinHp[4][1] < 800));

    AGrid best = {0, 0};
    int minHp = 10000;

    for (auto& Grid : positions) {
        const int row = Grid.row - 1;
        if (row < 0 || ROW_COUNT <= row)
            continue;

        // 该格前方有巨人时，不补南瓜，避免刚补上就被砸掉
        if (leftmostGiantX[row] <= Grid.col * 80 + 40 + 1 + 40)
            continue;

        // 只给已经有植物的格子套南瓜
        if (!hasPlant[row][Grid.col])
            continue;

        // 1-1/5-1 南瓜低于 800 时，暂时跳过这三个位置
        if (OuterPumpkinLow && ((Grid.row == 3 && Grid.col == 7) || (Grid.row == 2 && Grid.col == 5) || (Grid.row == 4 && Grid.col == 5)))
            continue;

        const bool IsOuter = (Grid.row == 1 || Grid.row == 5) && Grid.col == 1;
        const int Threshold = IsOuter ? 2300 : 1700;
        const int Hp = hasPumpkin[row][Grid.col] ? pumpkinHp[row][Grid.col] : 0;

        // 按各自阈值选择血量最低的南瓜
        if (Hp < Threshold && Hp < minHp) {
            minHp = Hp;
            best = Grid;
        }
    }

    if (best.row != 0)
        Use_Card(APUMPKIN, best.row, best.col);
}
// 设定Dance
inline void SetDance(bool state) {
    asm volatile("movl 0x6A9EC0, %%eax;"
                 "movl 0x768(%%eax), %%ecx;"
                 "pushl %%ecx;"
                 "movl %[state], %%ebx;"
                 "movl $0x41AFD0, %%eax;"
                 "calll *%%eax;"
        :
        : [state] "rm"(unsigned(state))
        : "eax", "ebx", "ecx");
}

// -1 = 正常，0 = 加速，1 = 减速
static int DCState = -1;
inline void DanceCheat() {
    if (DCState != -1)
        SetDance(DCState);
}

void Logic() {
    DCState = 1;
    DanceCheat();
    bool FixPumpkin = true;
    auto Zombie_Type = AGetMainObject()->ZombieTypeList();
    int inclock = cardclock % 5001;
    bool clockwork = true;
    const bool NoGiga = Check_Zombie(AGIGA_GARGANTUAR) == 0;
    const bool NoGargantuar = Check_Zombie(AGARGANTUAR) == 0;
    const bool HasFootball = Check_Zombie(AFOOTBALL_ZOMBIE) > 0;
    const bool FootballNoPumpkin = HasFootball && (AGetPlantIndex(2, 6, APUMPKIN) < 0 || AGetPlantIndex(4, 6, APUMPKIN) < 0);
    const bool NeedIce = !(NoGiga && NoGargantuar) || FootballNoPumpkin;
    const bool NoGigaInLevel = AGetMainObject()->ZombieTypeList()[AGIGA_GARGANTUAR] == 0;
    // 墓碑
    for (auto& Item : aAlivePlaceItemFilter) {
        if (Item.Type() == 1) {
            Use_Card(AGRAVE_BUSTER, Item.Row() + 1, Item.Col() + 1);
            break;
        }
    }

    // 窝瓜
    if (NoGigaInLevel && !Zombie_Type[AGARGANTUAR] && !Zombie_Type[AFOOTBALL_ZOMBIE]) {
    } else if (((Check_Zombie(AGIGA_GARGANTUAR, -1, 3) == 0) && (Check_Zombie(AGARGANTUAR, -1, 3) + Check_Zombie(AFOOTBALL_ZOMBIE, -1, 3) == 0) && !(ARangeIn(ANowWave(), {1, 9, 19, 20}))) || ((ARangeIn(ANowWave(), {1, 9, 19, 20})) && (Check_Zombie(-1, -1, 3) - Check_Zombie(ABACKUP_DANCER, -1, 3, 800, 1000) > 0) && (Check_Zombie(AGIGA_GARGANTUAR, -1, 3) + Check_Zombie(AGARGANTUAR, -1, 3) == 0))) {
        if (Check_Zombie(AGIGA_GARGANTUAR, -1, 1, 0, 520) > 0)
            Use_Card(ASQUASH, 1, 5);
    } else {
        Use_Card(ASQUASH, 3, 9);
        Use_Card(ASQUASH, 3, 8);
    }

    // 灰烬内时钟循环
    if (inclock == 1) {
        if (NoGiga) {
        } else if (Check_Zombie(AGIGA_GARGANTUAR, -1, 5, -1000, 1000, 500) == 0) {
            Use_Card(ADOOM_SHROOM, 1, 8);
            Use_Card(ADOOM_SHROOM, 1, 9);
            Use_Card(ADOOM_SHROOM, 2, 8);
            Use_Card(ADOOM_SHROOM, 2, 9);
        } else if (Check_Zombie(AGIGA_GARGANTUAR, -1, 1, -1000, 1000, 2000) == 0) {
            Use_Card(ADOOM_SHROOM, 4, 8);
            Use_Card(ADOOM_SHROOM, 4, 9);
            Use_Card(ADOOM_SHROOM, 2, 8);
            Use_Card(ADOOM_SHROOM, 2, 9);
        } else {
            Use_Card(ADOOM_SHROOM, 2, 8);
            Use_Card(ADOOM_SHROOM, 2, 9);
            Use_Card(ADOOM_SHROOM, 4, 8);
            Use_Card(ADOOM_SHROOM, 4, 9);
            Use_Card(ADOOM_SHROOM, 1, 8);
            Use_Card(ADOOM_SHROOM, 1, 9);
        }
    }
    static int CopyIceWaitStart = -1;
    static bool CopyIceUseFallback = false;
    if (inclock != 1 + 101 || !NeedIce) {
        CopyIceWaitStart = -1;
        CopyIceUseFallback = false;
    }
    if (inclock == 1 + 101 && NeedIce) {
        const int Now = AGetMainObject()->GameClock();
        if (CopyIceWaitStart >= 0 && Now < CopyIceWaitStart)
            CopyIceWaitStart = -1;

        if (CopyIceUseFallback) {
            if (TryFallbackCopyIce()) {
                CopyIceWaitStart = -1;
                CopyIceUseFallback = false;
                clockwork = true;
            } else {
                clockwork = false;
            }
        } else if (TryPlaceCopyIce(true)) {
            CopyIceWaitStart = -1;
            clockwork = true;
        } else {
            if (CopyIceWaitStart < 0)
                CopyIceWaitStart = Now;

            if (Now - CopyIceWaitStart >= 200) {
                CopyIceUseFallback = true;
                if (TryFallbackCopyIce()) {
                    CopyIceWaitStart = -1;
                    CopyIceUseFallback = false;
                    clockwork = true;
                } else {
                    clockwork = false;
                }
            } else {
                clockwork = false;
            }
        }
    }
    bool EarlyIce = false;
    for (auto& Plant : aAlivePlantFilter) {
        if (Plant.Type() != APUMPKIN || Plant.Col() != 0 || Plant.Hp() >= 300)
            continue;
        const int Row = Plant.Row() + 1;
        if ((Row == 1 || Row == 5) && (Check_Zombie(ADIGGER_ZOMBIE, 36, Row) > 0 || Check_Zombie(ADIGGER_ZOMBIE, 37, Row) > 0)) {
            EarlyIce = true;
            break;
        }
    }

    if (EarlyIce) {
        if (!Use_Card(AICE_SHROOM, 5, 6) && !Use_Card(AICE_SHROOM, 1, 6))
            clockwork = false;
        cardclock += (1 + 2921) - inclock;
        inclock = 1 + 2921;
    } else if (inclock == 1 + 2921 && NeedIce) {
        if (!Use_Card(AICE_SHROOM, 5, 6) && !Use_Card(AICE_SHROOM, 1, 6))
            clockwork = false;
    }
    if (inclock == 1 + 2500) {
        if (Check_Zombie(AGIGA_GARGANTUAR) - Check_Zombie(AGIGA_GARGANTUAR, -1, 1) - Check_Zombie(AGIGA_GARGANTUAR, -1, 5) == 0 && Check_Zombie(AGARGANTUAR, -1, 3) == 0) {
        } else {
            Use_Card(ACHERRY_BOMB, 3, 9);
            Use_Card(ACHERRY_BOMB, 3, 8);
        }
    }

    // 根据刷新自动校准循环
    for (int wave = 1; wave <= 20; ++wave) {
        if ((ANowWave() == wave || ANowWave() == wave - 1) && (ANowTime(wave) < 257) && (ANowTime(wave) >= -600) && ((inclock == 0) || (inclock == 2921) || (inclock == 2500))) {
            clockwork = false;
        }
    }

    cardclock += clockwork;

    // 垫材
    constexpr int padCol[ROW_COUNT] = {5, 7, 8, 7, 5};
    constexpr int x0Table[ROW_COUNT] = {360, 520, 630, 520, 360};
    constexpr int x1Table[ROW_COUNT] = {430, 590, 700, 590, 430};
    constexpr int hp0Table[ROW_COUNT] = {500, 500, 200, 500, 500};
    constexpr int hpKTable[ROW_COUNT] = {60, 50, 30, 50, 60};
    for (auto& value : meatc)
        value = 0;
    for (int rowIndex = 0; rowIndex < ROW_COUNT; ++rowIndex) {
        const int Row = rowIndex + 1;
        const int Col = padCol[rowIndex];
        const int x0 = x0Table[rowIndex];
        const int x1 = x1Table[rowIndex];
        const int hp0 = hp0Table[rowIndex];
        const int hpK = hpKTable[rowIndex];

        for (auto& Zombie : aAliveZombieFilter) {
            if ((Zombie.Type() != AGIGA_GARGANTUAR && Zombie.Type() != AGARGANTUAR) || Zombie.State() != 0 || Zombie.Row() != rowIndex)
                continue;

            const float zx = Zombie.Abscissa();
            if (zx < x0 || x1 < zx)
                continue;

            const int step = static_cast<int>(zx - x0);
            const int needHp = hp0 + step * hpK;
            const int threat = Zombie.Hp() + Zombie.OneHp() + Zombie.TwoHp() - needHp;

            if (threat > meatc[rowIndex])
                meatc[rowIndex] = threat;
        }
    }
    const bool BalloonNeedBlover = BalloonWillEnterHomeIn(810);
    for (int i = 0; i < ROW_COUNT; ++i) {
        int bestRow = -1;
        for (int rowIndex = 0; rowIndex < ROW_COUNT; ++rowIndex) {
            if (meatc[rowIndex] > 0 && (bestRow == -1 || meatc[rowIndex] > meatc[bestRow]))
                bestRow = rowIndex;
        }

        if (bestRow == -1)
            break;

        const bool SaveBlover = BalloonNeedBlover && GigaWillSmashIn50cs(bestRow + 1, padCol[bestRow]);
        Use_Meatshield(bestRow + 1, padCol[bestRow], !SaveBlover);
        meatc[bestRow] = 0;
    }

    // 垫橄榄
    if (NoGiga && AGetPlantIndex(2, 6, APUMPKIN) < 0 && AGetPlantIndex(4, 6, APUMPKIN) < 0) {
        for (int Row : {2, 4}) {
            for (int x : {520, 530, 540, 550, 560, 570, 580, 590}) {
                if (Check_Zombie(AFOOTBALL_ZOMBIE, 0, Row, x, x + 10, 500)) {
                    Use_Meatshield(Row, 7);
                    break;
                }
            }
        }
    }

    Fix_Gloom(2, 5);
    Fix_Gloom(4, 5);
    Fix_Gloom(2, 6);
    Fix_Gloom(4, 6);
    Fix_Gloom(3, 7);
    Fix_Gloom(3, 6);

    // 补喷
    const bool HasJackOrGigaInLevel = Zombie_Type[AJACK_IN_THE_BOX_ZOMBIE] || Zombie_Type[AGIGA_GARGANTUAR];
    const bool UseSunShroom = AGetCardIndex(ASUN_SHROOM) >= 0 && !HasJackOrGigaInLevel;
    if (UseSunShroom) {
        if (AGetPlantIndex(5, 4, AFUME_SHROOM) >= 0) {
            ARemovePlant(5, 4, AFUME_SHROOM);
        }
        if (AGetPlantIndex(1, 4, AFUME_SHROOM) >= 0) {
            ARemovePlant(1, 4, AFUME_SHROOM);
        }
        Use_Card(ASUN_SHROOM, 5, 4);
        Use_Card(ASUN_SHROOM, 1, 4);
    } else if (HasJackOrGigaInLevel) {
        if (AGetPlantIndex(5, 4, ASUN_SHROOM) >= 0) {
            ARemovePlant(5, 4, ASUN_SHROOM);
        }
        if (AGetPlantIndex(1, 4, ASUN_SHROOM) >= 0) {
            ARemovePlant(1, 4, ASUN_SHROOM);
        }
        Use_Card(AFUME_SHROOM, 5, 4);
        Use_Card(AFUME_SHROOM, 1, 4);
    }

    // 秒炸
    if (Check_Zombie(AGIGA_GARGANTUAR) - Check_Zombie(AGIGA_GARGANTUAR, -1, 1) - Check_Zombie(AGIGA_GARGANTUAR, -1, 5) == 0 && Check_Zombie(AGARGANTUAR, -1, 3) == 0) {
        KillJack();
    }

    if (NoGiga && NoGargantuar && !Zombie_Type[AGIGA_GARGANTUAR] && !Zombie_Type[AGARGANTUAR]) {
        if (HasFootball || Zombie_Type[AFOOTBALL_ZOMBIE]) {
            Use_Card(APUMPKIN, 2, 6);
            Use_Card(APUMPKIN, 4, 6);
        }
    }
    // 自动补南瓜
    Fix_Pumpkins();

    // 自动铲套
    for (int Row = 1; Row <= 6; ++Row) {
        for (int Col = 1; Col <= 9; ++Col) {
            if (AGetPlantIndex(Row, Col, APUMPKIN) >= 0) {
                int result = 0;
                for (auto& Zombie : aAliveZombieFilter) {
                    if ((Zombie.Type() == AZOMBONI || Zombie.Type() == ACATAPULT_ZOMBIE) && Zombie.Row() == Row - 1 && Col * 80 + 1 < Zombie.Abscissa() && Zombie.Abscissa() < Col * 80 + 30 + 1) {
                        ARemovePlant(Row, Col, APUMPKIN);
                        FixPumpkin = false;
                        break;
                    }
                    if ((Zombie.Type() == AGIGA_GARGANTUAR || Zombie.Type() == AGARGANTUAR) && Zombie.Row() == Row - 1 && Col * 80 + 40 + 1 < Zombie.Abscissa() && Zombie.Abscissa() < Col * 80 + 30 + 40 + 1 && Zombie.State() == 70 && 0.64242 <= AGetPvzBase()->AnimationMain()->AnimationOffset()->AnimationArray()[Zombie.MRef<uint16_t>(0x118)].CirculationRate() && AGetPvzBase()->AnimationMain()->AnimationOffset()->AnimationArray()[Zombie.MRef<uint16_t>(0x118)].CirculationRate() <= 0.64485) {
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

    // 自动吹气球
    BalloonCaption();

    // 偷花
    if (NoGiga && !HasFootball) {
        Use_Card(ASUN_SHROOM, 1, 5);
        Use_Card(ASUN_SHROOM, 5, 5);
    }

    // 补花
    Use_Card(ASUNFLOWER, 1, 1);
    Use_Card(ASUNFLOWER, 3, 1);
    Use_Card(ASUNFLOWER, 5, 1);
    Use_Card(ATWIN_SUNFLOWER, 1, 1);
    Use_Card(ATWIN_SUNFLOWER, 3, 1);
    Use_Card(ATWIN_SUNFLOWER, 5, 1);
}

ATickRunner T1;
ATickRunner PumpkinFixer;
bool tickskipcontroller = true;

void AScript() {

    ASetReloadMode(AReloadMode::MAIN_UI_OR_FIGHT_UI);
    AMaidCheats::CallPartner();
    AGetInternalLogger()->SetLevel({});
    ASetZombies(ACreateRandomTypeList("普", "偷"), ASetZombieMode::INTERNAL);
    ASetGameSpeed(10);

    ShowWavelength(false, 3, true);

    std::vector<int> Cardlist = {AICE_SHROOM, AM_ICE_SHROOM, ADOOM_SHROOM, ACHERRY_BOMB, ASQUASH, APUMPKIN, AFUME_SHROOM};
    auto Zombie_Type = AGetMainObject()->ZombieTypeList();
    const int SelectSun = AGetMainObject()->Sun();

    if (Check_Plant(AGLOOM_SHROOM) < 10 || SelectSun >= 5000 || (Zombie_Type[AJACK_IN_THE_BOX_ZOMBIE] && SelectSun >= 2000))
        Cardlist.push_back(AGLOOM_SHROOM);
    if (AGetMainObject()->CompletedRounds() % 2)
        Cardlist.push_back(AGRAVE_BUSTER);
    if (Zombie_Type[ABALLOON_ZOMBIE])
        Cardlist.push_back(ABLOVER);
    if (Check_Plant(ATWIN_SUNFLOWER) < 5)
        Cardlist.push_back(ASUNFLOWER);
    if (Zombie_Type[AGIGA_GARGANTUAR]) {
        Cardlist.push_back(APUFF_SHROOM);
    }
    if (Check_Plant(ASUNFLOWER) > 0) {
        Cardlist.push_back(ATWIN_SUNFLOWER);
        std::erase(Cardlist, ASUNFLOWER);
    }
    if (!Zombie_Type[AGIGA_GARGANTUAR]) {
        if (!Zombie_Type[AGARGANTUAR]) {
            std::erase(Cardlist, ADOOM_SHROOM);
            if (!Zombie_Type[AJACK_IN_THE_BOX_ZOMBIE]) {
                std::erase(Cardlist, ACHERRY_BOMB);
            }
        }
        for (auto Type : {ASUN_SHROOM, APUFF_SHROOM, AFLOWER_POT, ASCAREDY_SHROOM}) {
            if (Cardlist.size() >= 10)
                break;
            Cardlist.push_back(Type);
        }
    } else {
        for (auto Type : {ASUN_SHROOM, AFLOWER_POT, ASCAREDY_SHROOM}) {
            if (Cardlist.size() >= 10)
                break;
            Cardlist.push_back(Type);
        }
    }
    Cardlist.resize(10);
    if (AGetMainObject()->CompletedRounds() != 1500)
        ASelectCards(Cardlist, 1);
    T1.Start(Logic);
    // T2.Start(DrawInfo, ATickRunner::PAINT);
    PumpkinFixer.Start(Fix_Pumpkins);
    // ASkipTick(20, 0); // 跳帧

    AConnect([] { return AGetMainObject()->GameClock() % 10 == 1; }, [] { ASkipTick([] { return (AGetMainObject()->GameClock() % 10 != 0 && tickskipcontroller); }); });
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
