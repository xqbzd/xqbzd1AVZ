// LI5HDH3tBiZ/13rXcldAXJjACH/tATT/lA7S4QTFwRTFwVY0ZZXhbIH0zVdU/ipHCVI=
#include "ShowWavelength/ShowWavelength.h"
#include "SmartRemove.h"
#include <algorithm>
#include <avz.h>
#include <cfloat>
#include <climits>
#include <dsl/shorthand.h>
#include <vector>

// ===== 每帧对象缓存（性能优化）=====
// AvZ 的 aAliveZombieFilter / aAlivePlantFilter / aAlivePlaceItemFilter
// 每次遍历都要走一遍整个对象池，而且每个元素都要经 std::function 间接调用一次
// 活着判定；脚本一帧要遍历几十次，开销很大。
// 这里把它换成"每个游戏帧只重建一次"的指针列表，用法和原来完全一样：
//     for (auto& Zombie : Zombies) { Zombie.Type(); ... }
template <typename T>
class ACachedList {
public:
    struct Iterator {
        T** _ptr;
        T** _end;

        void SkipDead() {
            while (_ptr != _end && !__AFilterTrait<T>::IsAlive(*_ptr))
                ++_ptr;
        }

        T& operator*() const { return **_ptr; }
        T* operator->() const { return *_ptr; }
        Iterator& operator++() {
            ++_ptr;
            SkipDead();
            return *this;
        }
        bool operator!=(const Iterator& rhs) const { return _ptr != rhs._ptr; }
    };

    void Refresh(auto&& filter) {
        _data.clear();
        for (auto& obj : filter)
            _data.push_back(&obj);
    }

    Iterator begin() const {
        Iterator it {const_cast<T**>(_data.data()), const_cast<T**>(_data.data()) + _data.size()};
        it.SkipDead();
        return it;
    }

    Iterator end() const {
        return {const_cast<T**>(_data.data()) + _data.size(), const_cast<T**>(_data.data()) + _data.size()};
    }

    std::size_t size() const { return _data.size(); }

private:
    std::vector<T*> _data;
};

ACachedList<AZombie> Zombies;
ACachedList<APlant> Plants;
ACachedList<APlaceItem> PlaceItems;

// 同一游戏帧内只重建一次缓存
inline void RefreshCachedLists() {
    static int lastClock = INT_MIN;
    if (!AGetMainObject())
        return;
    const int clock = AGetMainObject()->GameClock();
    if (clock == lastClock)
        return;
    lastClock = clock;
    Zombies.Refresh(::aAliveZombieFilter);
    Plants.Refresh(::aAlivePlantFilter);
    PlaceItems.Refresh(::aAlivePlaceItemFilter);
}

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
// 数植物
int PlantCnt(std::initializer_list<int> Types = {}, std::initializer_list<AGrid> Grids = {}, std::initializer_list<int> HpRange = {}) {
    RefreshCachedLists();
    int result = 0;
    auto isTypeMatch = [](std::initializer_list<int> list, int value) { return list.size() == 0 || std::find(list.begin(), list.end(), value) != list.end(); };
    auto isGridMatch = [](std::initializer_list<AGrid> grids, int row, int col) {
        if (grids.size() == 0)
            return true;
        AGrid target(row, col);
        return std::find(grids.begin(), grids.end(), target) != grids.end();
    };

    const int* pHp = HpRange.begin();
    int minHp = HpRange.size() > 0 ? pHp[0] : 0;
    int maxHp = HpRange.size() > 1 ? pHp[1] : INT_MAX;

    for (auto& Plant : Plants) {
        if (isTypeMatch(Types, Plant.Type()) && isGridMatch(Grids, Plant.Row() + 1, Plant.Col() + 1) && minHp <= Plant.Hp() && Plant.Hp() <= maxHp) {
            ++result;
        }
    }
    return result;
}
// 数僵尸
int ZombieCnt(std::initializer_list<int> Types = {}, std::initializer_list<int> States = {}, std::initializer_list<int> Rows = {}, std::initializer_list<int> AbscissaRange = {}, std::initializer_list<int> HpRange = {}, std::initializer_list<int> Waves = {}) {
    RefreshCachedLists();
    int result = 0;
    auto isMatch = [](std::initializer_list<int> list, int value) { return list.size() == 0 || std::find(list.begin(), list.end(), value) != list.end(); };

    const int* pAbscissa = AbscissaRange.begin();
    int minAbscissa = AbscissaRange.size() > 0 ? pAbscissa[0] : -1000;
    int maxAbscissa = AbscissaRange.size() > 1 ? pAbscissa[1] : 1000;

    const int* pHp = HpRange.begin();
    int minHp = HpRange.size() > 0 ? pHp[0] : 0;
    int maxHp = HpRange.size() > 1 ? pHp[1] : INT_MAX;

    for (auto& Zombie : Zombies) {
        int totalHp = Zombie.Hp() + Zombie.OneHp() + Zombie.TwoHp();
        if (isMatch(Types, Zombie.Type()) && isMatch(States, Zombie.State()) && isMatch(Rows, Zombie.Row() + 1) && minAbscissa <= Zombie.Abscissa() && Zombie.Abscissa() <= maxAbscissa && minHp <= totalHp && totalHp <= maxHp && isMatch(Waves, Zombie.AtWave() + 1)) {
            ++result;
        }
    }
    return result;
}
// 与 AvZ 的 AGetPlantIndex 语义一致（同样的过滤与返回 -2 规则），
// 但只遍历本帧的植物缓存，省掉每次调用都构造 AObjSelector 的堆分配
int FastPlantIndex(int row, int col, int type = -1) {
    RefreshCachedLists();
    for (auto& plant : Plants) {
        if (plant.Row() != row - 1 || plant.Col() != col - 1)
            continue;
        if (plant.Type() == ASQUASH && plant.State() >= 5)
            continue;
        const int plantType = plant.Type();
        if (type == -1) {
            if (plantType != APUMPKIN && plantType != AFLOWER_POT && plantType != ALILY_PAD && plantType != ACOFFEE_BEAN)
                return plant.Index();
        } else {
            if (type == -2 || plantType == type)
                return plant.Index();
            else if (type != APUMPKIN && type != AFLOWER_POT && type != ALILY_PAD && type != ACOFFEE_BEAN && plantType != APUMPKIN && plantType != AFLOWER_POT && plantType != ALILY_PAD && plantType != ACOFFEE_BEAN)
                return -2;
        }
    }
    return -1;
}
bool isSeedUsableOrHolding(APlantType Type) { return AIsSeedUsable(Type) || AMRef<int>(0x6A9EC0, 0x768, 0x138, 0x28) == Type; }

// 试种
bool TryCard(auto Type, int Row, int Col) {
    if (AIsSeedUsable(Type) && AAsm::GetPlantRejectType(Type, Row - 1, Col - 1) == AAsm::NIL) {
        ACard(Type, Row, Col);
        return true;
    }
    return false;
}

// 多重试种
std::vector<APlant*> TryCard(const std::vector<APlantType>& Types, const std::vector<AGrid>& Grids) {
    std::vector<APlant*> ret;
    for (APlantType Type : Types) {
        if (!AIsSeedUsable(Type))
            continue;
        for (const auto& Grid : Grids) {
            if (AAsm::GetPlantRejectType(Type, Grid.row - 1, Grid.col - 1) == AAsm::NIL) {
                APlant* plantPtr = ACard(Type, Grid.row, Grid.col);
                if (plantPtr != nullptr) {
                    ret.push_back(plantPtr);
                }
                break;
            }
        }
    }
    return ret;
}

// 补曾
// NeedPumpkin = true 时，该格必须已经有南瓜、或者南瓜卡当前可用，两者满足其一才补（3-7 用）
// 只有一个都没有（没南瓜、南瓜卡又不可用）时才不补，避免种下去的曾直接被吃
void FixGloom(int Row, int Col, bool NeedPumpkin = false) {
    if (NeedPumpkin && FastPlantIndex(Row, Col, APUMPKIN) < 0 && !AIsSeedUsable(APUMPKIN))
        return;

    if (!ZombieCnt({AGIGA_GARGANTUAR}, {}, {Row}, {-1000, Col * 80 + 40 + 1 + 10}) && !ZombieCnt({AGARGANTUAR}, {}, {Row}, {-1000, Col * 80 + 40 + 1 + 10}) && !ZombieCnt({AZOMBONI}, {}, {Row}, {-1000, Col * 80 + 1 + 10}) && !ZombieCnt({AFOOTBALL_ZOMBIE}, {}, {Row}, {-1000, Col * 80 - 40 + 1 + 10}, {90})) {
        if (!PlantCnt({AFUME_SHROOM}, {{Row, Col}}) && AIsSeedUsable(AFUME_SHROOM))
            TryCard({AFUME_SHROOM}, {{Row, Col}});
        if (!PlantCnt({AGLOOM_SHROOM}, {{Row, Col}}) && AIsSeedUsable(AGLOOM_SHROOM))
            TryCard({AGLOOM_SHROOM}, {{Row, Col}});
    }
}
// 用垫
void TryMeatshield(int Row, int Col, bool AllowBlover = true, bool AllowSpike = true) {
    for (auto Meatshield : {APUFF_SHROOM, AFLOWER_POT, ASUN_SHROOM, ASCAREDY_SHROOM, ASUNFLOWER, AFUME_SHROOM, ASPIKEWEED, ABLOVER}) {
        if (Meatshield == ABLOVER && !AllowBlover)
            continue;
        if (Meatshield == ASPIKEWEED && !AllowSpike)
            continue;
        if (!PlantCnt({AFLOWER_POT}, {{Row, Col}})) {
            if (!ZombieCnt({AZOMBONI}, {0}, {Row}, {-1000, Col * 80 + 5}) && !ZombieCnt({ACATAPULT_ZOMBIE}, {0}, {Row}, {-1000, Col * 80 + 5})) {
                TryCard({Meatshield}, {{Row, Col}});
            } else {
                if (AllowSpike)
                    TryCard({ASPIKEWEED}, {{Row, Col}});

                if (AllowBlover)
                    TryCard({ABLOVER}, {{Row, Col}});
            }
        }
    }
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

float BalloonΔX(int Time, float Speed, int SlowCountdown = 0) {
    if (!SlowCountdown)
        return Speed * Time; // 原速 × 总时间
    if (SlowCountdown > Time)
        return 0.4 * Speed * Time; // 减速 × 总时间
    return 0.4 * Speed * (SlowCountdown - 1) + Speed * (Time - (SlowCountdown - 1));
    // 减速 × (减速倒计时 - 1) + 原速 × (总时间 - (减速倒计时 - 1))
}
bool BalloonWillEnterHomeIn(int Time) {
    for (auto& Zombie : Zombies) {
        if (Zombie.Type() == ABALLOON_ZOMBIE && int(Zombie.Abscissa() - BalloonΔX(Time, Zombie.Speed(), Zombie.SlowCountdown())) <= -100)
            return true;
    }
    return false;
}

bool GigaWillSmashIn50cs(int Row, int Col) {
    for (auto& Zombie : Zombies) {
        if ((Zombie.Type() != AGIGA_GARGANTUAR && Zombie.Type() != AGARGANTUAR) || Zombie.State() != 70 || Zombie.Row() != Row - 1)
            continue;
        const float x = Zombie.Abscissa();
        if (!(Col * 80 + 40 + 1 < x && x < Col * 80 + 30 + 40 + 1))
            continue;
        const float rate = Zombie.AnimationPtr()->CirculationRate();
        if (Zombie.SlowCountdown() > 0) {
            if (0.426666f <= rate && rate <= 0.65f)
                return true;
        } else {
            if (0.402424f <= rate && rate <= 0.65f)
                return true;
        }
    }
    return false;
}

bool GigaWillSmashIn26cs(int Row, int Col) {
    for (auto& Zombie : Zombies) {
        if ((Zombie.Type() != AGIGA_GARGANTUAR && Zombie.Type() != AGARGANTUAR) || Zombie.State() != 70 || Zombie.Row() != Row - 1)
            continue;
        const float x = Zombie.Abscissa();
        if (!(Col * 80 - 70 + 1 < x && x < Col * 80 + 40 + 1))
            continue;
        const float rate = Zombie.AnimationPtr()->CirculationRate();
        if (Zombie.SlowCountdown() > 0) {
            if (0.581818f <= rate && rate <= 0.65f)
                return true;
        } else {
            if (0.518787f <= rate && rate <= 0.65f)
                return true;
        }
    }
    return false;
}

// 该格是否有巨人正处于落锤前后（判定与吹三叶里的 GiantWillSmash 一致）
// 用于垫材：这个位置马上就要被砸，垫了也是白垫，这一帧先跳过
bool GiantWillSmashAt(int Row, int Col, float ratebegin = 0.4f, float rateend = 0.65f) {
    for (auto& Zombie : Zombies) {
        if ((Zombie.Type() != AGIGA_GARGANTUAR && Zombie.Type() != AGARGANTUAR) || Zombie.State() != 70 || Zombie.Row() != Row - 1)
            continue;
        const float x = Zombie.Abscissa();
        if (!(Col * 80 + 40 + 1 < x && x < Col * 80 + 30 + 40 + 1))
            continue;
        const float rate = Zombie.AnimationPtr()->CirculationRate();
        if (ratebegin <= rate && rate <= rateend)
            return true;
    }
    return false;
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

void KillJack() {
    for (auto& Zombie : Zombies) {
        if (Zombie.Type() == AJACK_IN_THE_BOX_ZOMBIE && Zombie.State() == 16 && Zombie.StateCountdown() == 110 && (isSeedUsableOrHolding(ACHERRY_BOMB) || isSeedUsableOrHolding(AJALAPENO) || (isSeedUsableOrHolding(ADOOM_SHROOM) && aFieldInfo.isNight))) {
            for (auto& Plant : Plants) {
                if (Plant.Type() == AYYG_42) {
                    if (Plant.Row() == 2 - 1 && Plant.Col() == 6 - 1 && JudgeExplode(&Plant, &Zombie)) {
                        ARemovePlant(2, 7);
                        TryCard({ACHERRY_BOMB}, {{2, 7}});
                        return;
                    }
                    if (Plant.Row() == 3 - 1 && Plant.Col() == 7 - 1 && JudgeExplode(&Plant, &Zombie)) {
                        ARemovePlant(3, 8);
                        TryCard({ACHERRY_BOMB}, {{3, 8}});
                        return;
                    }
                    if (Plant.Row() == 4 - 1 && Plant.Col() == 6 - 1 && JudgeExplode(&Plant, &Zombie)) {
                        ARemovePlant(4, 7);
                        TryCard({ACHERRY_BOMB}, {{4, 7}});
                        return;
                    }
                }
            }
        }
    }
}

// 本帧内一次性完成判定与用卡：不安全则不用卡并返回 false，安全则本帧立即用卡并返回 true
// 需存活时间NeedTime应填写≥1的数，默认卡片需存活至99cs后
// 延迟铲除ShovelDelay默认0为不铲除
bool SafeCard(APlantType PlantType, int Row, int Col, int NeedTime = 99, int ShovelDelay = 0) {
    if (AGetCardIndex(PlantType) < 0 || AGetCardIndex(PlantType) > 9 || !isSeedUsableOrHolding(PlantType))
        return false; // 卡片需要带了且CD是好的
    for (auto& Zombie : Zombies) {
        if (Zombie.Type() == AJACK_IN_THE_BOX_ZOMBIE && Zombie.State() == 16 && PredictExplode(&Zombie, Row, Col, PlantType) && Zombie.StateCountdown() <= NeedTime)
            return false; // 小丑倒计时≤NeedTime，本帧放卡会被炸，直接不放卡
    }
    ACard(PlantType, Row, Col); // 查完小丑后发现一切正常，本帧立即用卡
    if (ShovelDelay != 0)
        AConnect(ANowDelayTime(ShovelDelay), [=] { ARemovePlant(Row, Col, PlantType); }); // 以种植时间为参照进行延迟铲除
    return true;
}
namespace {
constexpr int ROW_COUNT = 5;
std::array<int, ROW_COUNT> meatc = {};
}

int cardclock = 0;

void BalloonCaption() {
    static const std::array<AGrid, 10> BloverPositions = {{{1, 5}, {5, 5}, {1, 6}, {5, 6}, {2, 7}, {4, 7}, {1, 4}, {5, 4}, {3, 8}, {3, 9}}};

    auto GiantWillSmash = [](int Row, int Col) {
        for (auto& Zombie : Zombies) {
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
        for (auto& Item : PlaceItems) {
            if (Item.Type() == APlaceItemType::GRAVESTONE && Item.Row() == Row - 1 && Item.Col() == Col - 1)
                return true;
        }
        return false;
    };

    for (auto& Zombie : Zombies) {
        if ((Zombie.Type() == ABALLOON_ZOMBIE && isSeedUsableOrHolding(ABLOVER)) && (int(Zombie.Abscissa() - BalloonΔX(470, Zombie.Speed(), Zombie.SlowCountdown())) <= -50)) {

            for (const auto& Grid : BloverPositions) {
                if (GiantWillSmash(Grid.row, Grid.col) || HasGrave(Grid.row, Grid.col) || FastPlantIndex(Grid.row, Grid.col) >= 0)
                    continue;
                SafeCard(ABLOVER, Grid.row, Grid.col);
            }
            for (const auto& Grid : BloverPositions) {
                if (GiantWillSmash(Grid.row, Grid.col) || HasGrave(Grid.row, Grid.col) || FastPlantIndex(Grid.row, Grid.col) < 0)
                    continue;
                ARemovePlant(Grid.row, Grid.col);
                SafeCard(ABLOVER, Grid.row, Grid.col, 51);
            }
        }
    }
    return;
}

bool CopyIceGiantDanger(int Row, int Col) {
    const int PlantX = 40 + (Col - 1) * 80;
    const int DefenseFront = PlantX + 50;

    for (auto& Zombie : Zombies) {
        if (Zombie.Row() != Row - 1)
            continue;
        const float x = Zombie.Abscissa();
        if (Zombie.Type() == AGIGA_GARGANTUAR || Zombie.Type() == AGARGANTUAR) {
            if (Zombie.State() == 0 && DefenseFront < x && x <= DefenseFront + 80)
                return true;
            if (Zombie.State() == 69 && DefenseFront < x && x <= DefenseFront + 50)
                return true;
            if (Zombie.State() == 70 && DefenseFront < x && x <= DefenseFront + 50)
                return true;
        } else if (Zombie.Type() == AZOMBIE) {
            if (x <= DefenseFront + 100)
                return true;
        }
    }
    return false;
}

bool TryPlaceCopyIce(bool AllowShovel) {
    static const std::array<AGrid, 8> CopyIcePositions = {{{1, 5}, {5, 5}, {1, 4}, {5, 4}, {1, 6}, {5, 6}, {2, 7}, {4, 7}}};

    for (const auto& Grid : CopyIcePositions) {
        if (CopyIceGiantDanger(Grid.row, Grid.col) || FastPlantIndex(Grid.row, Grid.col) >= 0)
            continue;
        if (AAsm::GetPlantRejectType(AM_ICE_SHROOM, Grid.row - 1, Grid.col - 1) != AAsm::NIL)
            continue;
        if (!TryCard({AM_ICE_SHROOM}, {{Grid.row, Grid.col}}).empty())
            return true;
    }

    if (!AllowShovel || !AIsSeedUsable(AM_ICE_SHROOM))
        return false;

    for (const auto& Grid : CopyIcePositions) {
        if (CopyIceGiantDanger(Grid.row, Grid.col) || FastPlantIndex(Grid.row, Grid.col) < 0)
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
        for (auto& Plant : Plants) {
            if (Plant.Row() == Row - 1 && Plant.Col() == 3 && Plant.Type() != APUMPKIN) {
                ARemovePlant(Row, 4, Plant.Type());
                return false;
            }
        }
        if (AAsm::GetPlantRejectType(AM_ICE_SHROOM, Row - 1, 3) == AAsm::NIL && !TryCard({AM_ICE_SHROOM}, {{Row, 4}}).empty())
            return true;
    }
    return false;
}

// ===== 补南瓜参数（手动调整）=====
// 预期寿命 = 当前南瓜血量 / 该格损耗速率，数值越小表示这一格越快损耗完
// 第一预期寿命限制：预期寿命最低的那格低于它，直接补这一格
constexpr float PumpkinFirstLifeLimit = 150;
// 第二预期寿命限制：预期寿命第二低的那格低于它，立刻补预期寿命最低的那格
constexpr float PumpkinSecondLifeLimit = 300;

// 每个南瓜格子专属的损耗速率：行 0-based（对应草坪第 1~5 行），列 1-based（与 positions 写法一致），下标 0 不用
// 先全部填 1.0f，之后按实测手动给每个格子指定；填 0 表示这一格不参与自动补南瓜
constexpr int PumpkinLossRate[ROW_COUNT][10] = {
    {0, 8, 0, 0, 1, 0, 0, 0, 0, 0},
    {0, 3, 0, 0, 1, 1, 2, 0, 0, 0},
    {0, 1, 0, 0, 1, 0, 2, 7, 0, 0},
    {0, 3, 0, 0, 1, 1, 2, 0, 0, 0},
    {0, 8, 0, 0, 1, 0, 0, 0, 0, 0},
};

void Fix_Pumpkins() {
    if (!AIsSeedUsable(APUMPKIN))
        return;

    auto Zombie_Type = AGetMainObject()->ZombieTypeList();
    const bool NoGiga = ZombieCnt({AGIGA_GARGANTUAR}) == 0;
    const bool NoGargantuar = ZombieCnt({AGARGANTUAR}) == 0;
    const bool HasFootball = ZombieCnt({AFOOTBALL_ZOMBIE}) > 0;

    static std::vector<AGrid> positions; // static 复用容量，避免每帧重新申请内存
    positions.assign({{1, 1}, {2, 1}, {3, 1}, {4, 1}, {5, 1}, {3, 6}, {3, 7}, {2, 5}, {4, 5}, {2, 4}, {3, 4}, {4, 4}});
    if (NoGiga && NoGargantuar && !Zombie_Type[AGIGA_GARGANTUAR] && !Zombie_Type[AGARGANTUAR] && !Zombie_Type[AZOMBONI]) {
        if (HasFootball || Zombie_Type[AFOOTBALL_ZOMBIE]) {
            positions.push_back({2, 6});
            positions.push_back({4, 6});
        }
    }
    if (NoGiga) {
        positions.push_back({1, 4});
        positions.push_back({5, 4});
    }

    std::array<std::array<bool, 10>, ROW_COUNT> hasPlant = {};
    std::array<std::array<bool, 10>, ROW_COUNT> hasInnerPlant = {}; // 该格有南瓜以外的植物
    std::array<std::array<bool, 10>, ROW_COUNT> hasPumpkin = {};
    std::array<std::array<int, 10>, ROW_COUNT> pumpkinHp = {};
    std::array<float, ROW_COUNT> leftmostGiantX;
    leftmostGiantX.fill(10000.0f);

    for (auto& Plant : Plants) {
        const int row = Plant.Row();
        const int col = Plant.Col() + 1;
        if (row < 0 || ROW_COUNT <= row || col < 1 || 9 < col)
            continue;

        hasPlant[row][col] = true;
        if (Plant.Type() == APUMPKIN) {
            hasPumpkin[row][col] = true;
            pumpkinHp[row][col] = Plant.Hp();
        } else {
            hasInnerPlant[row][col] = true;
        }
    }

    for (auto& Zombie : Zombies) {
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
    float minLife = FLT_MAX;    // 所有需补南瓜里预期寿命最低的
    float secondLife = FLT_MAX; // 预期寿命第二低的（候选不足两个时保持 FLT_MAX）

    for (auto& Grid : positions) {
        const int row = Grid.row - 1;
        if (row < 0 || ROW_COUNT <= row)
            continue;

        // 该格前方有巨人时，不补南瓜，避免刚补上就被砸掉
        if (leftmostGiantX[row] <= Grid.col * 80 + 40 + 1 + 40)
            continue;

        // 只给有南瓜以外植物的格子套南瓜，里面已经空了的南瓜壳不再维护
        if (!hasInnerPlant[row][Grid.col])
            continue;

        // 1-1/5-1 南瓜低于 800 时，暂时跳过这三个位置
        if (OuterPumpkinLow && ((Grid.row == 3 && Grid.col == 7) || (Grid.row == 2 && Grid.col == 5) || (Grid.row == 4 && Grid.col == 5)))
            continue;

        const float LossRate = PumpkinLossRate[row][Grid.col];
        if (LossRate <= 0.0f)
            continue;

        const int Hp = hasPumpkin[row][Grid.col] ? pumpkinHp[row][Grid.col] : 0;
        const float Life = Hp / LossRate;

        if (Life < minLife) {
            secondLife = minLife;
            minLife = Life;
            best = Grid;
        } else if (Life < secondLife) {
            secondLife = Life;
        }
    }

    // 第二低的预期寿命已经到限 → 现在就把预期寿命最低的那格补上
    // 或最低的预期寿命自己跌破第一限制 → 直接补最低
    if (best.row != 0 && (minLife <= PumpkinFirstLifeLimit || secondLife <= PumpkinSecondLifeLimit))
        TryCard({APUMPKIN}, {{best.row, best.col}});
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
    bool AllowSpike = true;
    const bool NoGiga = ZombieCnt({AGIGA_GARGANTUAR}) == 0;
    const bool NoGargantuar = ZombieCnt({AGARGANTUAR}) == 0;
    const bool HasFootball = ZombieCnt({AFOOTBALL_ZOMBIE}) > 0;
    const bool FootballNoPumpkin = HasFootball && (FastPlantIndex(2, 6, APUMPKIN) < 0 || FastPlantIndex(4, 6, APUMPKIN) < 0);
    const bool StopIceInWave20 = ANowWave() == 20;
    const bool NeedIce = !StopIceInWave20 && (!(NoGiga && NoGargantuar) || FootballNoPumpkin);
    const bool NoGigaInLevel = AGetMainObject()->ZombieTypeList()[AGIGA_GARGANTUAR] == 0;
    // 墓碑
    for (auto& Item : PlaceItems) {
        if (Item.Type() == 1) {
            TryCard({AGRAVE_BUSTER}, {{Item.Row() + 1, Item.Col() + 1}});
            break;
        }
    }

    // 窝瓜
    if (NoGigaInLevel && !Zombie_Type[AGARGANTUAR] && !Zombie_Type[AFOOTBALL_ZOMBIE]) {
    } else if (((ZombieCnt({AGIGA_GARGANTUAR, AGARGANTUAR, AFOOTBALL_ZOMBIE}, {}, {3}) == 0) && !(ARangeIn(ANowWave(), {1, 9, 19, 20}))) || ((ARangeIn(ANowWave(), {1, 9, 19, 20})) && (ZombieCnt({}, {}, {3}) - ZombieCnt({ABACKUP_DANCER}, {}, {3}, {800, 1000}) > 0) && (ZombieCnt({AGIGA_GARGANTUAR}, {}, {3}) + ZombieCnt({AGARGANTUAR}, {}, {3}) == 0))) {
        if (ZombieCnt({AGIGA_GARGANTUAR}, {}, {1}, {0, 520}) > 0)
            TryCard({ASQUASH}, {{1, 5}});
    } else {
        if (NoGigaInLevel && !Zombie_Type[AGARGANTUAR]) {
            TryCard({ASQUASH}, {{3, 8}, {3, 9}});
        } else {
            TryCard({ASQUASH}, {{3, 9}, {3, 8}});
        }
    }

    // 灰烬内时钟循环
    if (inclock == 1) {
        if (ZombieCnt({AGIGA_GARGANTUAR}) + ZombieCnt({AGARGANTUAR, AZOMBIE}, {}, {3}) == 0) {
            // 不做事
        } else if (ZombieCnt({AGIGA_GARGANTUAR}, {}, {5}, {}, {500}) == 0) {
            TryCard({ADOOM_SHROOM}, {{1, 8}, {1, 9}, {1, 7}, {2, 8}, {2, 9}, {5, 8}, {5, 9}, {5, 7}, {2, 7}, {4, 7}});
        } else if (ZombieCnt({AGIGA_GARGANTUAR}, {}, {1}, {}, {2000}) == 0) {
            TryCard({ADOOM_SHROOM}, {{4, 8}, {4, 9}, {2, 8}, {2, 9}, {1, 7}, {5, 8}, {5, 9}, {5, 7}, {2, 7}, {4, 7}});
        } else if (ZombieCnt({AGIGA_GARGANTUAR}) > 0) {
            TryCard({ADOOM_SHROOM}, {{2, 8}, {2, 9}, {4, 8}, {4, 9}, {1, 8}, {1, 9}, {1, 7}, {5, 8}, {5, 9}, {5, 7}, {2, 7}, {4, 7}, {3, 9}});
        } else {
            TryCard({ADOOM_SHROOM}, {{4, 8}, {4, 9}, {1, 8}, {1, 9}, {1, 7}, {5, 8}, {5, 9}, {5, 7}, {2, 7}, {4, 7}});
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
    for (auto& Plant : Plants) {
        if (Plant.Type() != APUMPKIN || Plant.Col() != 0 || Plant.Hp() >= 300)
            continue;
        const int Row = Plant.Row() + 1;
        if ((Row == 1 || Row == 5) && (ZombieCnt({ADIGGER_ZOMBIE}, {36}, {Row}) > 0 || ZombieCnt({ADIGGER_ZOMBIE}, {37}, {Row}) > 0)) {
            EarlyIce = true;
            break;
        }
    }

    if (EarlyIce && !StopIceInWave20) {
        if (TryCard({AICE_SHROOM}, {{5, 6}}).empty() && TryCard({AICE_SHROOM}, {{1, 6}}).empty())
            clockwork = false;
        cardclock += (1 + 3421) - inclock;
        inclock = 1 + 3421;
    } else if (inclock == 1 + 3421 && NeedIce) {
        if (TryCard({AICE_SHROOM}, {{5, 6}}).empty() && TryCard({AICE_SHROOM}, {{1, 6}}).empty())
            clockwork = false;
    }
    if (inclock == 1 + 2500) {
        if (Zombie_Type[AGIGA_GARGANTUAR] || Zombie_Type[AZOMBONI]) {
            if (ZombieCnt({AZOMBONI}, {}, {3}) + ZombieCnt({AGIGA_GARGANTUAR}, {}, {2, 3, 4}) + ZombieCnt({AGARGANTUAR}, {}, {3}) > 0) {
                TryCard({ACHERRY_BOMB}, {{3, 9}, {3, 8}});
            }
        }
    }

    // 根据刷新自动校准循环
    // 刷新前禁用地刺垫
    for (int wave = 1; wave <= 20; ++wave) {
        if ((ANowWave() == wave || ANowWave() == wave - 1) && (ANowTime(wave) < 257) && (ANowTime(wave) >= -600) && ((inclock == 0) || (inclock == 3421) || (inclock == 2500))) {
            clockwork = false;
        }
        if (((ANowWave() == wave - 1) && (ANowTime(wave) < 1) && (ANowTime(wave) >= -200)) || ZombieCnt({AZOMBIE}, {}, {3}, {-1000, 1000}))
            AllowSpike = false;
    }

    // 扎车
    if ((((200 < inclock && inclock < 2450) || (2600 < inclock && inclock < 4950)) && PlantCnt({ADOOM_SHROOM, ACHERRY_BOMB}) == 0) || AGetCardIndex(ADOOM_SHROOM) < 0) {
        if (ZombieCnt({AZOMBONI}, {}, {3}, {639, 727}) && !GigaWillSmashIn26cs(3, 9)) {
            TryCard({ASPIKEWEED}, {{3, 9}});
        }
        if (ZombieCnt({AZOMBONI}, {}, {3}, {555, 647}) && !GigaWillSmashIn26cs(3, 8)) {
            TryCard({ASPIKEWEED}, {{3, 8}});
        }
        if (ZombieCnt({AZOMBONI}, {}, {1}, {319, 357})) {
            TryCard({ASPIKEWEED}, {{1, 5}});
        }
        if (ZombieCnt({AZOMBONI}, {}, {5}, {319, 357})) {
            TryCard({ASPIKEWEED}, {{5, 5}});
        }
    }

    cardclock += clockwork;

    // 垫材
    constexpr int padCol[ROW_COUNT] = {5, 7, 8, 7, 5};
    constexpr int x0Table[ROW_COUNT] = {360, 520, 630, 520, 360};
    constexpr int x1Table[ROW_COUNT] = {430, 590, 700, 590, 430};
    constexpr int hp0Table[ROW_COUNT] = {300, 500, 100, 500, 300};
    constexpr int hpKTable[ROW_COUNT] = {40, 40, 70, 40, 40};
    for (auto& value : meatc)
        value = 0;
    for (int rowIndex = 0; rowIndex < ROW_COUNT; ++rowIndex) {
        const int Row = rowIndex + 1;
        const int Col = padCol[rowIndex];
        const int x0 = x0Table[rowIndex];
        const int x1 = x1Table[rowIndex];
        const int hp0 = hp0Table[rowIndex];
        const int hpK = hpKTable[rowIndex];

        // 这个垫材位上有巨人即将落锤时先不垫，否则刚放上去就被砸掉
        if (GiantWillSmashAt(Row, Col, 0.2f, 0.65f))
            continue;

        for (auto& Zombie : Zombies) {
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
        TryMeatshield(bestRow + 1, padCol[bestRow], !SaveBlover, AllowSpike);
        meatc[bestRow] = 0;
    }

    // 垫橄榄
    if (NoGiga && FastPlantIndex(2, 6, APUMPKIN) < 0 && FastPlantIndex(4, 6, APUMPKIN) < 0) {
        for (int Row : {2, 4}) {
            for (int x : {520, 530, 540}) {
                if (ZombieCnt({AFOOTBALL_ZOMBIE}, {0}, {Row}, {x, x + 10}, {500})) {
                    TryMeatshield(Row, 7, true, false);
                    break;
                }
            }
        }
    }

    FixGloom(3, 6);
    FixGloom(2, 5);
    FixGloom(4, 5);
    FixGloom(3, 7, true);
    if (FastPlantIndex(3, 7, AGLOOM_SHROOM) >= 0) {
        FixGloom(2, 6);
        FixGloom(4, 6);
    }

    // 补喷
    const bool HasJackOrGigaInLevel = Zombie_Type[AJACK_IN_THE_BOX_ZOMBIE] || Zombie_Type[AGIGA_GARGANTUAR];
    const bool UseSunShroom = AGetCardIndex(ASUN_SHROOM) >= 0 && !HasJackOrGigaInLevel;
    if (UseSunShroom) {
        if (FastPlantIndex(5, 4, AFUME_SHROOM) >= 0) {
            ARemovePlant(5, 4, AFUME_SHROOM);
        }
        if (FastPlantIndex(1, 4, AFUME_SHROOM) >= 0) {
            ARemovePlant(1, 4, AFUME_SHROOM);
        }
        TryCard({ASUN_SHROOM}, {{5, 4}, {1, 4}});
    } else if (HasJackOrGigaInLevel) {
        if (FastPlantIndex(5, 4, ASUN_SHROOM) >= 0) {
            ARemovePlant(5, 4, ASUN_SHROOM);
        }
        if (FastPlantIndex(1, 4, ASUN_SHROOM) >= 0) {
            ARemovePlant(1, 4, ASUN_SHROOM);
        }
        TryCard({AFUME_SHROOM}, {{5, 4}, {1, 4}});
    }

    // 秒炸
    if (ZombieCnt({AGIGA_GARGANTUAR}) - ZombieCnt({AGIGA_GARGANTUAR}, {}, {1}) - ZombieCnt({AGIGA_GARGANTUAR}, {}, {5}) == 0 && ZombieCnt({AGARGANTUAR}, {}, {3}) == 0) {
        KillJack();
    }

    if (NoGiga && Zombie_Type[AFOOTBALL_ZOMBIE] && PlantCnt({APUMPKIN}, {{3, 7}}, {0, 500}) > 0) {
        TryCard({ACHERRY_BOMB}, {{3, 8}});
    }
    // 自动补南瓜交给独立的 tick runner（PumpkinFixer）负责，这里不再重复调用

    // 垫南瓜
    for (int Row : {2, 4}) {
        if (FixPumpkin && ZombieCnt({AGIGA_GARGANTUAR}, {0}, {Row}, {-600, 522}, {500, 6000}) && !ZombieCnt({AZOMBIE}, {}, {Row}, {-600, 511}))
            TryCard({APUMPKIN}, {{Row, 6}});
    }

    // 自动吹气球
    BalloonCaption();

    // 偷花
    if (NoGiga && !HasFootball) {
        TryCard({ASUN_SHROOM}, {{1, 5}, {5, 5}});
        TryCard({ASUN_SHROOM}, {});
    }

    // 补花
    TryCard({ASUNFLOWER}, {{1, 1}, {3, 1}, {5, 1}});
    TryCard({ATWIN_SUNFLOWER}, {{1, 1}, {3, 1}, {5, 1}});
}

ATickRunner T1;
ATickRunner PumpkinFixer;
bool tickskipcontroller = true;

void AScript() {

    ASetReloadMode(AReloadMode::MAIN_UI_OR_FIGHT_UI);
    AMaidCheats::CallPartner();
    AGetInternalLogger()->SetLevel({});
    std::vector<int> Zombielist = ACreateRandomTypeList("普", "");
    std::erase(Zombielist, ABUNGEE_ZOMBIE);
    ASetZombies(Zombielist, ASetZombieMode::INTERNAL);
    ASetGameSpeed(10);

    ShowWavelength(false, 3, true);

    std::vector<int> Cardlist = {AICE_SHROOM, AM_ICE_SHROOM, ACHERRY_BOMB, ASQUASH, APUMPKIN, AFUME_SHROOM};
    auto Zombie_Type = AGetMainObject()->ZombieTypeList();
    const int SelectSun = AGetMainObject()->Sun();

    if (PlantCnt({AGLOOM_SHROOM}) < 9 || SelectSun >= 5000 || ((Zombie_Type[AJACK_IN_THE_BOX_ZOMBIE] || Zombie_Type[AZOMBIE]) && SelectSun >= 2000))
        Cardlist.push_back(AGLOOM_SHROOM);
    if (Zombie_Type[AZOMBONI])
        Cardlist.push_back(ASPIKEWEED);
    if (Zombie_Type[ABALLOON_ZOMBIE])
        Cardlist.push_back(ABLOVER);
    if (Zombie_Type[AGIGA_GARGANTUAR] || Zombie_Type[AGARGANTUAR])
        Cardlist.push_back(ADOOM_SHROOM);
    if (PlantCnt({ATWIN_SUNFLOWER}) < 6)
        Cardlist.push_back(ASUNFLOWER);
    if (Zombie_Type[AGIGA_GARGANTUAR]) {
        Cardlist.push_back(APUFF_SHROOM);
    }
    if (PlantCnt({ASUNFLOWER}) > 0) {
        Cardlist.push_back(ATWIN_SUNFLOWER);
        std::erase(Cardlist, ASUNFLOWER);
    }
    if (!Zombie_Type[AGIGA_GARGANTUAR]) {
        if (!Zombie_Type[AGARGANTUAR] && !Zombie_Type[AZOMBONI]) {
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
    smart_remove.Start();
    // T2.Start(DrawInfo, ATickRunner::PAINT);
    PumpkinFixer.Start(Fix_Pumpkins);
    // ASkipTick(20, 0); // 大跳帧

    AConnect([] { return AGetMainObject()->GameClock() % 20 == 1; }, [] { ASkipTick([] { return (AGetMainObject()->GameClock() % 20 != 0 && tickskipcontroller); }); });
    // static bool isPaused = false;
    // At('Z')[] {
    //     isPaused = !isPaused;
    //     ASetAdvancedPause(isPaused, 0, 0);
    // };
    // At('X')[] {
    //     isPaused = false;
    //     ASetAdvancedPause(isPaused, 0, 0);
    //     AConnect(ANowDelayTime(1), [] {
    //         isPaused = !isPaused;
    //         ASetAdvancedPause(isPaused, 0, 0);
    //     });
    // };
    // At('C')[] { AGetPvzBase()->TickMs() = AGetPvzBase()->TickMs() == 1 ? 10 : 1; };
    // At('V')[] { AMRef<int>(0x416DBE) = AMRef<int>(0x416DBE) == 699999 ? 100001 : 699999; };
}
