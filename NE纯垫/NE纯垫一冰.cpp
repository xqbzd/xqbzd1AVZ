#include "../SmartRemove.h"
#include "../common.h"
#include "ShowWavelength/ShowWavelength.h"
#include <array>
#include <vector>

// LI5HzH3tAiZ/13pXSFw4Ud0cOkEMn+RCdrbkRlg5VuJS+FkM33DmRnXW/R9UlzBUrVROhFY=

namespace {
constexpr int ROW_COUNT = 5;
constexpr int PAD_COL = 7;
constexpr int LIMIT_X = PAD_COL * 80 + 30 + 40 + 1;

constexpr std::array<int, 4> meatshieldHYKscq = {20, 40, 75, 60};
constexpr std::array<int, 4> meatshieldHYBscq = {0, 100, 150, 400};
constexpr std::array<float, 4> meatshieldbantimescq = {0.65f, 0.65f, 0.65f, 0.9f};
constexpr std::array<int, 4> meatshieldGLKscq = {0, 5, 10, 5};
constexpr std::array<int, 4> meatshieldGLBscq = {0, 200, 400, 600};
constexpr std::array<int, 4> meatshieldCGCscq = {0, 30, 20, 10};
constexpr std::array<int, 5> COSTS = {0, 25, 50, 75, 100};
constexpr std::array<int, 5> GLOOM_ROWS = {3, 2, 4, 1, 5};
constexpr std::array<int, 5> ROWS_1_TO_5 = {1, 2, 3, 4, 5};
constexpr std::array<int, 5> START_ROWS = {1, 5, 2, 3, 4};
constexpr std::array<int, 2> START_COLS = {9, 8};

std::array<int, ROW_COUNT> meatshieldHYK = {};
std::array<int, ROW_COUNT> meatshieldHYB = {};
std::array<float, ROW_COUNT> meatshieldbantime = {};
std::array<int, ROW_COUNT> meatshieldGLK = {};
std::array<int, ROW_COUNT> meatshieldGLB = {};
std::array<int, ROW_COUNT> meatshieldCGC = {};

const std::array<AGrid, 15> PumpkinPOS1 = {{{2, 7}, {4, 7}, {3, 7}, {1, 7}, {5, 7}, {2, 1}, {3, 1}, {4, 1}, {5, 1}, {1, 1}, {1, 6}, {2, 6}, {3, 6}, {4, 6}, {5, 6}}};

struct ZombieInfo {
    int type;
    int state;
    int row;
    float abscissa;
    int hp;
    int oneHp;
    float speed;
    int slowCountdown;
    float circulationRate;
};

struct RowScore {
    int row;
    int score;
};

inline bool ValidRow(int row) {
    return 0 <= row && row < ROW_COUNT;
}

inline bool IsGiant(int type) {
    return type == AGIGA_GARGANTUAR || type == AGARGANTUAR;
}
} // namespace

void Logic() {
    for (int row : GLOOM_ROWS) {
        Fix_Gloom(row, PAD_COL);
    }

    std::array<bool, ROW_COUNT> hasGloom7 = {};
    std::array<bool, ROW_COUNT> hasPumpkin7 = {};
    std::array<std::array<bool, 10>, ROW_COUNT> hasPlant = {};
    std::array<std::array<bool, 10>, ROW_COUNT> hasPumpkin = {};
    std::array<std::array<int, 10>, ROW_COUNT> pumpkinHp = {};
    for (auto& Plant : aAlivePlantFilter) {
        const int row = Plant.Row();
        const int col = Plant.Col() + 1;
        if (!ValidRow(row) || col < 1 || col > 9)
            continue;
        hasPlant[row][col] = true;
        if (Plant.Type() == AGLOOM_SHROOM && col == PAD_COL)
            hasGloom7[row] = true;
        if (Plant.Type() == APUMPKIN) {
            hasPumpkin[row][col] = true;
            pumpkinHp[row][col] = Plant.Hp();
            if (col == PAD_COL)
                hasPumpkin7[row] = true;
        }
    }

    for (int row = 0; row < ROW_COUNT; ++row) {
        int gloomCount = hasGloom7[row] ? 1 : 0;
        if (row == 0) {
            gloomCount += hasGloom7[1] ? 1 : 0;
        } else {
            gloomCount += hasGloom7[row - 1] ? 1 : 0;
            if (row + 1 < ROW_COUNT)
                gloomCount += hasGloom7[row + 1] ? 1 : 0;
        }
        meatshieldHYK[row] = meatshieldHYKscq[gloomCount];
        meatshieldHYB[row] = meatshieldHYBscq[gloomCount];
        meatshieldGLK[row] = meatshieldGLKscq[gloomCount];
        meatshieldGLB[row] = meatshieldGLBscq[gloomCount];
        meatshieldbantime[row] = meatshieldbantimescq[gloomCount];
        meatshieldCGC[row] = (hasGloom7[row] ? 1 : 0) * meatshieldCGCscq[gloomCount];
    }

    static std::vector<ZombieInfo> zombies;
    zombies.clear();
    zombies.reserve(64);
    std::array<int, ROW_COUNT> giantCount = {};
    std::array<float, ROW_COUNT> leftmostGiantX = {};
    std::array<bool, ROW_COUNT> canBlover = {true, true, true, true, true};
    leftmostGiantX.fill(10000.0f);

    for (auto& Zombie : aAliveZombieFilter) {
        const int type = Zombie.Type();
        const int state = Zombie.State();
        const int row = Zombie.Row();
        const float abscissa = Zombie.Abscissa();
        float circulationRate = 0.0f;
        if (IsGiant(type) && state == 70) {
            circulationRate = AGetPvzBase()->AnimationMain()->AnimationOffset()->AnimationArray()[Zombie.MRef<uint16_t>(0x118)].CirculationRate();
            if (ValidRow(row) && 0.4f <= circulationRate && circulationRate <= 0.65f)
                canBlover[row] = false;
        }
        if (IsGiant(type) && ValidRow(row) && -1000 <= abscissa && abscissa <= 1000) {
            ++giantCount[row];
            if (abscissa < leftmostGiantX[row])
                leftmostGiantX[row] = abscissa;
        }
        zombies.push_back({type, state, row, abscissa, Zombie.Hp(), Zombie.OneHp(), Zombie.Speed(), Zombie.SlowCountdown(), circulationRate});
    }

    for (auto& Item : aAlivePlaceItemFilter) {
        if (Item.Type() == 1) {
            Use_Card(AGRAVE_BUSTER, Item.Row() + 1, Item.Col() + 1);
            break;
        }
    }

    if (AIsSeedUsable(APUMPKIN)) {
        AGrid mingrid = {0, 0};
        int minhp = 2000;
        for (auto& Grid : PumpkinPOS1) {
            const int row = Grid.row - 1;
            if (!ValidRow(row))
                continue;
            if (leftmostGiantX[row] <= Grid.col * 80 + 40 + 1 + 40)
                continue;
            if (!hasPlant[row][Grid.col])
                continue;
            if (!hasPumpkin[row][Grid.col]) {
                minhp = 0;
                mingrid = Grid;
                break;
            }
            const int hp = pumpkinHp[row][Grid.col];
            if (hp < minhp) {
                minhp = hp;
                mingrid = Grid;
            }
        }
        if (minhp < 2000) {
            if (Use_Card(APUMPKIN, mingrid.row, mingrid.col) && mingrid.col == PAD_COL && ValidRow(mingrid.row - 1))
                hasPumpkin7[mingrid.row - 1] = true;
        }
    }

    std::array<bool, ROW_COUNT> banRow = {};
    for (auto& Zombie : zombies) {
        if (IsGiant(Zombie.type) && ValidRow(Zombie.row) && LIMIT_X < Zombie.abscissa && Zombie.abscissa < LIMIT_X + 50 && Zombie.state == 70 && 0.1f <= Zombie.circulationRate && Zombie.circulationRate <= meatshieldbantime[Zombie.row])
            banRow[Zombie.row] = true;
    }

    for (int Cost : COSTS) {
        std::array<int, ROW_COUNT> MaxHYpiancha = {};
        std::array<int, ROW_COUNT> MaxGLpiancha = {};
        std::array<int, ROW_COUNT> MaxIceHYpiancha = {};
        std::array<int, ROW_COUNT> MaxIceGLpiancha = {};
        const int extraCdSum = GetExtraCardCdSum();
        const double cdCorrection = 0.4 * extraCdSum / (7 * 750) - 0.2;
        const double hyFactor = 1 + Cost * 0.02 + cdCorrection;
        const double iceHyFactor = 1 + Cost * 0.02 - 5 * cdCorrection;

        for (auto& Zombie : zombies) {
            if (!ValidRow(Zombie.row))
                continue;
            if (IsGiant(Zombie.type) && Zombie.state == 0 && LIMIT_X - 20 <= Zombie.abscissa && Zombie.abscissa <= LIMIT_X + 80) {
                const int baseX = hasPumpkin7[Zombie.row] ? LIMIT_X : LIMIT_X - 20;
                const int HYpiancha = Zombie.hp - ((Zombie.abscissa - baseX) * meatshieldHYK[Zombie.row] * hyFactor + meatshieldHYB[Zombie.row]);
                if (HYpiancha > MaxHYpiancha[Zombie.row])
                    MaxHYpiancha[Zombie.row] = HYpiancha;
                const int IceHYpiancha = Zombie.hp - ((Zombie.abscissa - baseX) * meatshieldHYK[Zombie.row] * iceHyFactor + meatshieldHYB[Zombie.row]);
                if (IceHYpiancha > MaxIceHYpiancha[Zombie.row])
                    MaxIceHYpiancha[Zombie.row] = IceHYpiancha;
            } else if (Zombie.type == AFOOTBALL_ZOMBIE && LIMIT_X - 70 <= Zombie.abscissa && Zombie.abscissa <= LIMIT_X) {
                const int GLpiancha = Zombie.hp + Zombie.oneHp - ((Zombie.abscissa - (LIMIT_X - 70)) * meatshieldGLK[Zombie.row] * (1 + Cost * 0.5) + meatshieldGLB[Zombie.row]);
                if (GLpiancha > 0) {
                    if (!hasPumpkin7[Zombie.row]) {
                        MaxGLpiancha[Zombie.row] += GLpiancha * 4;
                        MaxIceGLpiancha[Zombie.row] += GLpiancha * 4;
                    } else {
                        MaxGLpiancha[Zombie.row] += GLpiancha;
                        MaxIceGLpiancha[Zombie.row] += GLpiancha;
                    }
                }
            } else if (Zombie.type == APOLE_VAULTING_ZOMBIE && 560 <= Zombie.abscissa && Zombie.abscissa <= 685 && Zombie.state == 11) {
                if (!hasPumpkin7[Zombie.row]) {
                    MaxGLpiancha[Zombie.row] -= meatshieldCGC[Zombie.row] * 4;
                    MaxIceGLpiancha[Zombie.row] -= meatshieldCGC[Zombie.row] * 4;
                } else {
                    MaxGLpiancha[Zombie.row] -= meatshieldCGC[Zombie.row];
                    MaxIceGLpiancha[Zombie.row] -= meatshieldCGC[Zombie.row];
                }
            }
        }

        std::array<RowScore, ROW_COUNT> Maxpiancha = {};
        std::array<RowScore, ROW_COUNT> MaxIcepiancha = {};
        for (int row = 0; row < ROW_COUNT; ++row) {
            Maxpiancha[row] = {row, MaxHYpiancha[row] * 8 + MaxGLpiancha[row]};
            MaxIcepiancha[row] = {row, MaxIceHYpiancha[row] * 8 + MaxIceGLpiancha[row]};
        }
        std::sort(Maxpiancha.begin(), Maxpiancha.end(), [](const RowScore& lhs, const RowScore& rhs) {
            return lhs.score > rhs.score;
        });
        std::sort(MaxIcepiancha.begin(), MaxIcepiancha.end(), [](const RowScore& lhs, const RowScore& rhs) {
            return lhs.score > rhs.score;
        });

        std::array<bool, ROW_COUNT> usedNormalShield = {};
        for (auto& Score : Maxpiancha) {
            if (!banRow[Score.row] && Score.score > 0)
                usedNormalShield[Score.row] = Use_Meatshield(Score.row + 1, 8, Cost);
            if (Score.score <= -80 && Check_Plant(ABLOVER, Score.row + 1, 8) == 0 && Check_Plant(-1, Score.row + 1, 8) > 0)
                AShovel(Score.row + 1, 8);
        }
        if (Cost == 0) {
            for (auto& Score : MaxIcepiancha) {
                if (!usedNormalShield[Score.row] && !banRow[Score.row] && Score.score > 0)
                    Use_IceShield(Score.row + 1, 8, Cost);
            }
        }
    }

    bool needblover = false;
    int jrCountMin = giantCount[0];
    for (int row = 1; row < ROW_COUNT; ++row) {
        if (giantCount[row] < jrCountMin)
            jrCountMin = giantCount[row];
    }
    for (auto& Zombie : zombies) {
        if (Zombie.type == ABALLOON_ZOMBIE && int(Zombie.abscissa - BalloonΔX(50 + jrCountMin * 120, Zombie.speed, Zombie.slowCountdown)) <= -100) {
            needblover = true;
            break;
        }
    }
    if (needblover) {
        for (int Row = 0; Row < ROW_COUNT; ++Row) {
            if (canBlover[Row]) {
                SafeCard(ABLOVER, Row + 1, 9, 51);
                SafeCard(ABLOVER, Row + 1, 8, 51);
            }
        }
    }
}

ATickRunner T1;
int Speed = 10;
void AScript() {
    ASetReloadMode(AReloadMode::MAIN_UI_OR_FIGHT_UI);
    AMaidCheats::CallPartner();
    ASetGameSpeed(Speed);
    AGetInternalLogger()->SetLevel({});
    std::vector<int> zombielist = ACreateRandomTypeList("\u666E", "");
    std::erase(zombielist, 20);
    ASetZombies(zombielist, ASetZombieMode::INTERNAL);

    auto Zombie_Type = AGetMainObject()->ZombieTypeList();
    std::vector<int> Cardlist = {APUMPKIN, AGLOOM_SHROOM, AFUME_SHROOM, AM_PUFF_SHROOM, APUFF_SHROOM, AICE_SHROOM, AFLOWER_POT, ASUN_SHROOM};
    if (AGetMainObject()->CompletedRounds() % 2) {
        Cardlist.push_back(AGRAVE_BUSTER);
    }
    if (Zombie_Type[ABALLOON_ZOMBIE]) {
        Cardlist.push_back(ABLOVER);
    }
    Cardlist.push_back(ASCAREDY_SHROOM);
    Cardlist.push_back(ASUNFLOWER);
    Cardlist.resize(10);
    ASelectCards(Cardlist, 1);
    AConnect([] { return AGetMainObject()->GameClock() % 50 == 1; }, [] { ASkipTick([] { return AGetMainObject()->GameClock() % 50 != 0; }); });

    T1.Start(Logic, ATickRunner::GLOBAL);
    smart_remove.Start();
    ShowWavelength(false, 5, true);

    for (int i : START_ROWS) {
        for (int j : START_COLS) {
            OnWave(1, 10) At(-599 + 751 * 2)[=] { Use_Meatshield(i, j, 25); };
            OnWave(1, 10) At(-599 + 751)[=] { Use_Meatshield(i, j, 25); };
            OnWave(1, 10) At(-599)[=] { Use_Meatshield(i, j, 25); };
        }
    }

    if (AGetMainObject()->CompletedRounds() % 2) {
        for (int i : ROWS_1_TO_5) {
            OnWave(20) At(-752 + 751 * 2)[=] { Use_Meatshield(i, 8, 25); };
            OnWave(20) At(-752 + 751)[=] {
                Use_Meatshield(i, 9, 25);
            };
            OnWave(20) At(-752 + 751) Shovel(3, 8);
            OnWave(20) At(-752)[=] { Use_Meatshield(i, 9, 25); };
        }
    } else {
        for (int i : ROWS_1_TO_5) {
            OnWave(20) At(-750 + 751 * 2)[=] { Use_Meatshield(i, 9, 25); };
            OnWave(20) At(-750 + 751)[=] { Use_Meatshield(i, 9, 25); };
            OnWave(20) At(-750) Shovel(3, 9);
            OnWave(20) At(-750)[=] {
                Use_Meatshield(i, 8, 25);
            };
        }
    }

    OnWave(1) At(-20)[=] {
        AMaidCheats::Move();
    };
    OnWave(1) At(20)[=] {
        AMaidCheats::CallPartner();
    };

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
