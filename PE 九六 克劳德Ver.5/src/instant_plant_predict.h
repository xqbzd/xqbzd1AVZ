#ifndef __A_INSTANT_PLANT_PREDICT_H__
#define __A_INSTANT_PLANT_PREDICT_H__

// ============================================================================
//  即时输出型植物的命中预测（樱桃炸弹 / 火爆辣椒 / 毁灭菇 / 倭瓜）
//
//  用法：
//      auto hits = APredictInstantPlantHits(3, 6, ACHERRY_BOMB);
//      for (auto& hit : hits)
//          aLogger->Info("命中 {} 预计 {}cs 后", hit.zombie->Type(), hit.eta);
//
//      只要僵尸地址：
//      auto targets = APredictInstantPlantTargets(3, 6, ACHERRY_BOMB);
//
//  注意：必须在战斗界面（board 已建立）里调用，因为格子像素由游戏本身给出。
//
//  -- 数据来源（PvZ 1.0.0.1051 反编译，见本仓库 RustVsZombies/vendor/pvz-emulator）--
//    system/damage.cpp          take_instant_kill / activate_plant / can_be_attacked
//    system/plant/squash.cpp    倭瓜的选目标与砸下判定
//    object/common.cpp          rect::get_overlap_len / rect::is_overlap_with_circle
//    object/plant.cpp           plant::get_attack_box / plant::get_attack_flags
//
//  -- 与 AvZ 内存的对齐 --
//    1) 僵尸判定框 = { BulletAbscissa(0x8C), BulletOrdinate(0x90),
//                      HurtWidth(0x94), HurtHeight(0x98) }，即游戏里的 ZombieRect
//    2) 爆炸圆心 = 格子左上角 + (40, 40)，因为植物攻击框是 80x80
//    3) 格子像素一律走 AAsm::GridToAbscissa / GridToOrdinate，由游戏给出，
//       因此 5 行场地与 6 行场地的不同行距能自动适配：
//         白天/黑夜   y = 80 + 100 * row
//         泳池/浓雾   y = 80 +  85 * row
//         天台        y = 70 +  85 * row + 20 * (5 - col)
//       不要在代码里写死行距与 80/100，直接用游戏换算即可
//    4) 生效延迟 = AvZ 简写表实测值：樱桃/辣椒 100cs，毁灭菇 夜晚 100cs /
//       白天 299cs（需要咖啡豆），倭瓜 182cs；模仿者额外 +320cs
//
//  -- 攻击标记（plant::get_attack_flags）--
//    灰烬类（樱桃/辣椒/毁灭菇）= 0x7F：能打到气球、钻地矿工、水下潜水、播放动画的僵尸，
//      打不到被魅惑的僵尸，也打不到还停在 x > 800 的未进场僵尸
//    倭瓜 = 0x0D：打不到气球与动画中的僵尸
//
//  -- 帧内缓存（性能）--
//    脚本每帧要对全场 54 格 × 几种卡片反复调用这些预测函数，而同一帧内僵尸不会动，
//    所以把只跟僵尸有关、跟“放在哪一格”无关的量在一帧内只算一次：
//      · 存活僵尸 / 植物列表（与 aAliveZombieFilter / aAlivePlantFilter 同一份判定、同一个顺序）
//      · 僵尸判定框、按生效时间推演后的判定框、能否被该类植物打到
//      · 每只僵尸能被哪些格子的灰烬炸到（一次算出，再按格子查表）
//    见 aInstantPlant::AGetFrameSnapshot()。缓存以 (主对象指针, GameClock, GlobalClock,
//    版本号) 作标签，标签一变就整体重建；脚本在同一帧内种下 / 铲掉植物之后要调用
//    aInstantPlant::AInvalidateFrameCache()（本目录的库函数只读游戏状态，不会自己改它）。
//    每个格子的结果与原来逐次重新扫描完全一致：所有过滤条件、遍历顺序（僵尸内存数组
//    顺序）、排序方式都原样保留，只是把重复的扫描换成了查表。
// ============================================================================

#include <avz.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <vector>

// ---------------------------------------------------------------------------
// 预测结果
// ---------------------------------------------------------------------------
struct AInstantPlantHit {
    AZombie* zombie = nullptr;  // 僵尸地址，可直接用于后续操作
    int eta = 0;                // 预计被命中的时刻，单位 cs，相对当前帧
    bool lethal = false;        // 是否会被这一发直接击杀
    int damage = 1800;          // 预计伤害
    float abscissa = 0.0f;      // 命中时僵尸的横坐标（预测值）
    float offset = 0.0f;        // 命中点相对爆炸中心的横向距离，正数在中心右边
};

struct AInstantPlantPredictOptions {
    // 预测窗口（cs）。< 0 表示使用该植物的默认生效延迟（含模仿者的 320cs）
    int predictCs = -1;
    // 是否按僵尸当前速度推演位置（关掉则只看当前帧位置）
    bool useMotion = true;
    // 只返回会被直接击杀的僵尸（巨人本体血量 >= 1800，一发打不死）
    bool onlyLethal = false;
    // 仅倭瓜：额外向前推演多少 cs 寻找会进入攻击范围的僵尸（0 = 只看当前帧）
    int squashWaitingCs = 0;
    // 仅倭瓜：向前推演时的扫描步长（cs）
    int squashScanStepCs = 5;

    // 帧内缓存按“同一组参数”复用预处理结果，需要能比较
    bool operator==(const AInstantPlantPredictOptions&) const = default;
};

// ---------------------------------------------------------------------------
// 内部实现
// ---------------------------------------------------------------------------
namespace aInstantPlant {

// 僵尸判定框（游戏里的 ZombieRect）
struct AHitBox {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;

    int Right() const { return x + width; }
    int Bottom() const { return y + height; }
};

// 反编译 plant::get_attack_flags / damage::can_be_attacked 里的位标记
enum : unsigned {
    FLAG_GROUND = 0x01,
    FLAG_FLYING_BALLOON = 0x02,
    FLAG_LURKING_SNORKEL = 0x04,
    FLAG_DOG = 0x08,
    FLAG_ANIMATING = 0x10,
    FLAG_DYING = 0x20,
    FLAG_DIGGING_DIGGER = 0x40,
    FLAG_HYPNO = 0x80,
    // 樱桃 / 辣椒 / 毁灭菇
    FLAG_ASH = FLAG_GROUND | FLAG_FLYING_BALLOON | FLAG_LURKING_SNORKEL | FLAG_DOG | FLAG_ANIMATING |
               FLAG_DYING | FLAG_DIGGING_DIGGER,
    // 倭瓜
    FLAG_SQUASH = FLAG_GROUND | FLAG_LURKING_SNORKEL | FLAG_DOG,
};

// 反编译 object/zombie.h 的 zombie_status
enum : int {
    STATE_WALKING = 0x00,
    STATE_DYING = 0x01,
    STATE_DYING_FROM_INSTANT_KILL = 0x02,
    STATE_DYING_FROM_LAWNMOWER = 0x03,
    STATE_BUNGEE_IDLE_AFTER_DROP = 0x06,
    STATE_BUNGEE_GRAB = 0x07,
    STATE_POLE_RUNNING = 0x0B,
    STATE_POLE_JUMPING = 0x0C,
    STATE_POLE_WALKING = 0x0D,
    STATE_RISING_FROM_GROUND = 0x0E,
    STATE_DIGGER_DIG = 0x20,
    STATE_DIGGER_DRILL = 0x21,
    STATE_DIGGER_LOST_DIG = 0x22,
    STATE_DIGGER_LANDING = 0x23,
    STATE_DIGGER_DIZZY = 0x24,
    STATE_DIGGER_WALK_RIGHT = 0x25,
    STATE_BACKUP_SPAWNING = 0x32,
    STATE_DOLPHIN_JUMP_IN_POOL = 0x34,
    STATE_DOLPHIN_RIDE = 0x35,
    STATE_DOLPHIN_JUMP = 0x36,
    STATE_DOLPHIN_WALK_IN_POOL = 0x37,
    STATE_SNORKEL_JUMP_IN_POOL = 0x3A,
    STATE_IMP_FLYING = 0x47,
    STATE_IMP_LANDING = 0x48,
    STATE_BALLOON_FLYING = 0x49,
    STATE_BALLOON_FALLING = 0x4A,
};

// 僵尸结构里 AvZ 没直接暴露、但判定需要的字段
enum : uintptr_t {
    OFFSET_IN_POOL = 0xBD,          // 是否在水里
    OFFSET_HAS_OBJECT = 0xBC,       // 雪人掉头后为 false，此时向右走
    OFFSET_MIND_CONTROLLED = 0xB8,  // 被魅惑（会向右走）
};

// “还在场外（打不到）”的边界。
//   游戏 Zombie::EffectedByDamage 里就一句：
//       if (mZombieType != ZOMBIE_BOBSLED && GetZombieRect().mX > WIDE_BOARD_WIDTH) return false;
//   WIDE_BOARD_WIDTH = 800（GameConstants.h）。两个要点：
//     · 比的是僵尸“判定框左边界”= mX + mZombieRect.mX（普通僵尸 +36、小丑也是 +36），
//       不是僵尸本体 mX；
//     · 这个判断是在“伤害结算那一帧”做的，所以有生效延迟的植物（灰烬 / 炮）必须用
//       预测帧的判定框，否则会把正在进场、生效时已经走进来的僵尸误判成打不到。
//   默认就是游戏值；若实机量到不同门槛，改这一处即可。
inline int aOffBoardDamageX = 800;

// 僵尸判定框的镜像锚点（反编译 zombie_base::init 里的 hit_box.offset_x）
constexpr int HIT_BOX_MIRROR_ANCHOR = 120;

// 倭瓜的时间轴（复刻 system/plant/squash.cpp 的 update）
enum : int {
    SQUASH_LOOK_CS = 80,                 // squash_look
    SQUASH_JUMP_UP_CS = 45,              // squash_jump_up
    SQUASH_AIR_CS = 50,                  // squash_stop_in_the_air
    SQUASH_LAND_CS = 5,                  // squash_jump_down 到 countdown == 5 时结算伤害
    SQUASH_TOTAL_CS = SQUASH_LOOK_CS + SQUASH_JUMP_UP_CS + SQUASH_AIR_CS + SQUASH_LAND_CS,  // 180
    SQUASH_AIM_PREDICT_CS = 30,          // 起跳结束时用 predict_after(z, 30) 算落点
    SQUASH_ATTACK_X_OFFSET = 20,         // 攻击框 = x + 20，宽 45
    SQUASH_ATTACK_WIDTH = 45,            // attack_box.width(80) - 35
    SQUASH_TRIGGER_RANGE = 70,           // 普通僵尸的触发距离
    SQUASH_TRIGGER_RANGE_EATING = 110,   // 正在啃食时的触发距离
    SQUASH_FOOTBALL_GRACE = 20,          // 橄榄球僵尸的砸下判定放宽 20px
};

// 场地行距（仅作说明，代码里不参与换算）
constexpr int ROW_HEIGHT_5_ROW = 100;  // 白天 / 黑夜
constexpr int ROW_HEIGHT_6_ROW = 85;   // 泳池 / 浓雾 / 天台

enum class Kind {
    Cherry,
    Jalapeno,
    Doom,
    Squash,
};

struct Rule {
    Kind kind = Kind::Cherry;
    int rowRange = 1;       // 允许的行偏移
    int radius = 115;       // 圆形判定半径
    bool wholeRow = false;  // 是否整行判定（辣椒）
    int effectCs = 100;     // 生效延迟
    unsigned flags = FLAG_ASH;
};

inline bool AGetRule(APlantType type, bool imitator, Rule& rule) {
    switch (static_cast<int>(type)) {
    case ACHERRY_BOMB:
        rule = Rule{Kind::Cherry, 1, 115, false, 100, FLAG_ASH};
        break;
    case AJALAPENO:
        rule = Rule{Kind::Jalapeno, 0, 0, true, 100, FLAG_ASH};
        break;
    case ADOOM_SHROOM:
        // 白天没有咖啡豆不会生效，这里按“有咖啡豆”给出 299cs
        rule = Rule{Kind::Doom, 3, 250, false, aFieldInfo.isNight ? 100 : 299, FLAG_ASH};
        break;
    case ASQUASH:
        rule = Rule{Kind::Squash, 0, 0, false, 182, FLAG_SQUASH};
        break;
    default:
        return false;
    }
    if (imitator)
        rule.effectCs += 320;  // IMITATOR_MORPH_DELAY
    return true;
}

// 植物真实类型。模仿者在游戏里有两种记录方式（AIMITATOR + 基础类型 / +1），
// 变身中的模仿者则把目标类型放在 0x138
// （原来定义在 zombie_threat_predict.h 的 aThreat 里；帧快照要用，所以挪到这里，
//   aThreat::APlantBaseType 仍然可用）
inline int APlantBaseType(APlant* plant) {
    const int type = plant->Type();
    if (type == AIMITATOR)
        return plant->MRef<int>(0x138);
    if (type > AIMITATOR) {
        const int candidates[2] = {type - AIMITATOR, type - AIMITATOR - 1};
        for (int base : candidates) {
            if (base == ACHERRY_BOMB || base == AJALAPENO || base == ADOOM_SHROOM || base == ASQUASH ||
                base == ACOB_CANNON)
                return base;
        }
        return type - AIMITATOR;
    }
    return type;
}

// 僵尸是否向右走（复刻 object/zombie.cpp 的 zombie::is_walk_right）
inline bool AIsWalkingRight(AZombie* zombie) {
    if (zombie->MRef<bool>(OFFSET_MIND_CONTROLLED))
        return true;

    const int state = zombie->State();
    if (zombie->Type() == ADIGGER_ZOMBIE &&
        (state == STATE_DIGGER_DRILL || state == STATE_DIGGER_DIZZY || state == STATE_DIGGER_WALK_RIGHT))
        return true;

    if (zombie->Type() == AZOMBIE_YETI && !zombie->MRef<bool>(OFFSET_HAS_OBJECT))
        return true;

    return false;
}

// 僵尸判定框（游戏里的 ZombieRect）
// 注意 0x8C / 0x90 存的是相对本体坐标的偏移（每种僵尸不同：普通 36、矿工 50、巨人 -17…），
// 必须加上 Abscissa / Ordinate 才是屏幕坐标，这正是 Zombie::GetZombieRect 的算法。
inline AHitBox AGetZombieHitBox(AZombie* zombie) {
    AHitBox box;
    box.width = zombie->HurtWidth();    // 0x94
    box.height = zombie->HurtHeight();  // 0x98

    int offsetX = zombie->BulletAbscissa();  // 0x8C，相对偏移
    if (AIsWalkingRight(zombie))
        offsetX = HIT_BOX_MIRROR_ANCHOR - box.width - offsetX;

    box.x = static_cast<int>(zombie->Abscissa()) + offsetX;
    box.y = static_cast<int>(zombie->Ordinate()) + zombie->BulletOrdinate();  // 0x90，相对偏移

    if (box.width <= 0 || box.height <= 0) {
        // 数据异常时退化为“本体位置 + 普通僵尸身材”
        box.x = static_cast<int>(zombie->Abscissa()) + 36;
        box.y = static_cast<int>(zombie->Ordinate());
        box.width = 42;
        box.height = 115;
    }
    return box;
}

// 复刻 object/common.cpp 的 rect::get_overlap_len：正数表示重叠长度，负数表示相距 -overlap
inline float AHitBoxOverlapLenX(const AHitBox& lhs, const AHitBox& rhs) {
    const float selfX = static_cast<float>(lhs.x);
    const float selfRight = static_cast<float>(lhs.Right());
    const float otherX = static_cast<float>(rhs.x);
    const float otherRight = static_cast<float>(rhs.Right());

    float nearEdge, farEdge, farX;
    if (otherX >= selfX) {
        nearEdge = selfRight;
        farEdge = otherRight;
        farX = otherX;
    } else {
        nearEdge = otherRight;
        farEdge = selfRight;
        farX = selfX;
    }

    if (nearEdge > farX && nearEdge > farEdge)
        return farEdge - farX;
    return nearEdge - farX;
}

// 复刻 object/common.cpp 的 rect::is_overlap_with_circle
inline bool AHitBoxOverlapCircle(const AHitBox& box, int px, int py, int radius) {
    const bool xInRange = box.x <= px && px <= box.Right();
    const bool yInRange = box.y <= py && py <= box.Bottom();
    const int64_t r = radius;

    if (xInRange && yInRange)
        return true;
    if (xInRange) {
        const int64_t up = std::abs(py - box.y);
        const int64_t down = std::abs(py - box.Bottom());
        return std::min(up, down) <= r;
    }
    if (yInRange) {
        const int64_t left = std::abs(px - box.x);
        const int64_t right = std::abs(px - box.Right());
        return std::min(left, right) <= r;
    }

    const int64_t xl = std::min<int64_t>(std::abs(px - box.x), std::abs(px - box.Right()));
    const int64_t yl = std::min<int64_t>(std::abs(py - box.y), std::abs(py - box.Bottom()));
    return xl * xl + yl * yl <= r * r;
}

// AHitBoxOverlapCircle 的“按行一次算完”版本：py 固定时，使判定为真的 px 是一段闭区间
// [lo, hi]（返回 false 表示这一行没有任何 px 能命中）。
//   推导：AHitBoxOverlapCircle 就是“点到矩形的距离 <= r”（矩形内 0、矩形外取到最近边的
//   直线 / 斜线距离），它对 px 是连续的一段：
//     · py 落在矩形高度范围内：xInRange 直接为真；矩形外只看到最近竖边的距离 <= r
//       → px ∈ [x - r, Right + r]
//     · py 在矩形上下之外：yl = 到最近横边的距离。yl > r 时 xInRange 分支也为假，整行为空；
//       否则矩形内的 px 为真（xInRange 分支：yl <= r），矩形外要 xl² + yl² <= r²，
//       xl 是整数 → xl <= floor(sqrt(r² - yl²)) =: m → px ∈ [x - m, Right + m]
//   （已用逐点对照的方式验证与 AHitBoxOverlapCircle 完全一致）
inline bool AHitBoxCircleXRange(const AHitBox& box, int py, int radius, int& lo, int& hi) {
    const bool yInRange = box.y <= py && py <= box.Bottom();
    int64_t reach = 0;
    if (yInRange) {
        reach = radius;
    } else {
        const int64_t r = radius;
        const int64_t yl = std::min<int64_t>(std::abs(py - box.y), std::abs(py - box.Bottom()));
        if (yl > r)
            return false;
        const int64_t rest = r * r - yl * yl;
        int64_t m = static_cast<int64_t>(std::sqrt(static_cast<double>(rest)));
        while (m * m > rest)
            --m;
        while ((m + 1) * (m + 1) <= rest)
            ++m;
        reach = m;
    }
    lo = static_cast<int>(box.x - reach);
    hi = static_cast<int>(box.Right() + reach);
    return true;
}

// 复刻 Zombie::ZombieNotWalking()：这些状态下僵尸这一帧不产生位移 —— 速度字段（0x34 mVelX）
// 还是旧值，但游戏根本不推进它。最典型的就是开盒小丑（16 = PHASE_JACK_IN_THE_BOX_POPPING）：
// 它站着开盒，如果还照 speed × cs 推演，判定框会被算到左边去，灰烬就会选错格子。
//   下表就是反编译 Lawn/Zombie.cpp 里的阶段列表；末尾的“高度”那几种 AvZ 没暴露、
//   PE 场地也不会出现，暂不处理。
inline bool AIsZombieNotWalking(AZombie* zombie) {
    if (zombie == nullptr)
        return false;
    if (zombie->IsEat() || zombie->FreezeCountdown() > 0 || zombie->FixationCountdown() > 0)
        return true;  // mIsEating || IsImmobilizied()

    switch (zombie->State()) {
    case 16:  // PHASE_JACK_IN_THE_BOX_POPPING
    case 30:  // PHASE_NEWSPAPER_MADDENING
    case 33:  // PHASE_DIGGER_RISING
    case 34:  // PHASE_DIGGER_TUNNELING_PAUSE_WITHOUT_AXE
    case 35:  // PHASE_DIGGER_RISE_WITHOUT_AXE
    case 36:  // PHASE_DIGGER_STUNNED
    case 41:  // PHASE_DANCER_SNAPPING_FINGERS
    case 42:  // PHASE_DANCER_SNAPPING_FINGERS_WITH_LIGHT
    case 43:  // PHASE_DANCER_SNAPPING_FINGERS_HOLD
    case 45:  // PHASE_DANCER_WALK_TO_RAISE
    case 46:  // PHASE_DANCER_RAISE_LEFT_1
    case 47:  // PHASE_DANCER_RAISE_RIGHT_1
    case 48:  // PHASE_DANCER_RAISE_LEFT_2
    case 49:  // PHASE_DANCER_RAISE_RIGHT_2
    case 50:  // PHASE_DANCER_RISING
    case 67:  // PHASE_CATAPULT_LAUNCHING
    case 68:  // PHASE_CATAPULT_RELOADING
    case 69:  // PHASE_GARGANTUAR_THROWING
    case 70:  // PHASE_GARGANTUAR_SMASHING
    case 71:  // PHASE_IMP_GETTING_THROWN
    case 72:  // PHASE_IMP_LANDING
    case 77:  // PHASE_LADDER_PLACING
        return true;
    default:
        break;
    }

    const int type = zombie->Type();
    return type == ABUNGEE_ZOMBIE || type == ADR_ZOMBOSS;  // ZOMBIE_BUNGEE / ZOMBIE_BOSS
}

// 僵尸在 cs 帧内沿行走方向走过的像素距离（与 AvZ 社区 BalloonDX 的写法一致，并补上 debuff）
//   减速：countdown.slow > 0 的那些帧速度 ×0.4（复刻游戏 zombie_base::predict_after / update_x）
//   冰冻 / 黄油：countdown.freeze / countdown.butter > 0 期间完全不移动（游戏里 fps 被置 0）
inline float ADisplacement(AZombie* zombie, int cs) {
    if (cs <= 0)
        return 0.0f;
    if (AIsZombieNotWalking(zombie))
        return 0.0f;  // 这一帧压根不走路（开盒小丑 / 举锤巨人 / 跳舞 / 被固定…）
    // 冰冻 / 黄油期间原地不动：这部分帧数直接不产生位移
    const int standCs = std::max(0, std::max(zombie->FreezeCountdown(), zombie->FixationCountdown()));
    cs -= standCs;
    if (cs <= 0)
        return 0.0f;
    const float speed = zombie->Speed();
    const int slow = std::max(0, zombie->SlowCountdown() - standCs);  // 冰冻期间减速倒计时也在走
    if (slow <= 0)
        return speed * cs;
    if (slow > cs)
        return 0.4f * speed * cs;
    return 0.4f * speed * (slow - 1) + speed * (cs - (slow - 1));
}

// 把判定框沿行走方向推演 cs 帧
inline AHitBox AHitBoxAfterCs(const AHitBox& box, AZombie* zombie, int cs) {
    if (cs <= 0)
        return box;

    const float displacement = ADisplacement(zombie, cs);
    AHitBox result = box;
    result.x += static_cast<int>(AIsWalkingRight(zombie) ? displacement : -displacement);
    return result;
}

// 僵尸是否处于空中（气球飞 / 气球坠落）
inline bool AIsFlyingOrFalling(AZombie* zombie) {
    const int state = zombie->State();
    return state == STATE_BALLOON_FLYING || state == STATE_BALLOON_FALLING;
}

inline bool AIsDead(AZombie* zombie) {
    const int state = zombie->State();
    return state == STATE_DYING || state == STATE_DYING_FROM_INSTANT_KILL || state == STATE_DYING_FROM_LAWNMOWER;
}

// 复刻 damage::can_be_attacked
//   boxOverride：可选。游戏是在“伤害结算那一帧”才调 can_be_attacked 的，所以灰烬 / 炮这类
//   有生效延迟的攻击，应该把“预计命中时”的判定框传进来做“还在场外”的检查（不传就用当前帧）。
inline bool ACanBeAttacked(AZombie* zombie, unsigned flags, const AHitBox* boxOverride = nullptr) {
    const int state = zombie->State();

    if (!(flags & FLAG_DYING) && AIsDead(zombie))
        return false;

    const bool hypno = zombie->MRef<bool>(OFFSET_MIND_CONTROLLED);
    if (flags & FLAG_HYPNO) {
        if (!hypno)
            return false;
    } else if (hypno) {
        return false;
    }

    if (zombie->Type() == ABUNGEE_ZOMBIE && state != STATE_BUNGEE_IDLE_AFTER_DROP && state != STATE_BUNGEE_GRAB)
        return false;

    switch (state) {
    case STATE_POLE_JUMPING:
    case STATE_IMP_FLYING:
    case STATE_DIGGER_DRILL:
    case STATE_DIGGER_LOST_DIG:
    case STATE_DIGGER_LANDING:
    case STATE_DOLPHIN_JUMP_IN_POOL:
    case STATE_DOLPHIN_JUMP:
    case STATE_SNORKEL_JUMP_IN_POOL:
    case STATE_BALLOON_FALLING:
    case STATE_RISING_FROM_GROUND:
    case STATE_BACKUP_SPAWNING:
        // 只有灰烬类（带 Animating 标记）能打到这些状态
        return (flags & FLAG_ANIMATING) != 0;
    default:
        break;
    }

    const AHitBox box = boxOverride != nullptr ? *boxOverride : AGetZombieHitBox(zombie);
    if (box.x > aOffBoardDamageX)  // 还在场外
        return false;

    const bool lurkingSnorkel =
        zombie->Type() == ASNORKEL_ZOMBIE && !zombie->IsEat() && zombie->MRef<bool>(OFFSET_IN_POOL);

    if ((flags & FLAG_LURKING_SNORKEL) && lurkingSnorkel)
        return true;
    if ((flags & FLAG_DIGGING_DIGGER) && state == STATE_DIGGER_DIG)
        return true;
    if ((flags & FLAG_FLYING_BALLOON) && AIsFlyingOrFalling(zombie))
        return true;
    if ((flags & FLAG_GROUND) && !AIsFlyingOrFalling(zombie) && !lurkingSnorkel && state != STATE_DIGGER_DIG)
        return true;

    return false;
}

// 这一发是否足以直接击杀（本体血量 >= 1800 的巨人 / 红眼一发打不死）
// 血量口径：僵尸“本体血量 + 一类防具”（路障 / 铁桶 / 橄榄球头盔…），
// 二类防具（铁门 / 报纸…）不计入 —— 与 zombie_threat_predict.h 保持一致
inline int AZombieHp(AZombie* zombie) {
    return zombie->Hp() + zombie->OneHp();
}

inline bool AIsLethal(AZombie* zombie, Kind kind) {
    (void)kind;
    // 灰烬与炮：本体血量 < 1800 直接击杀（饰品一并摧毁）；
    // 倭瓜：伤害会被一类饰品吸收，等效判断相同
    return AZombieHp(zombie) < 1800;
}

// ---------------------------------------------------------------------------
// 玉米加农炮
// ---------------------------------------------------------------------------
// 反编译确认（object/plant_system 的炮分支 + projectile_system::parabola_do_attack）：
//   · 发射时植物记录 cannon.x = 目标x - 47
//   · 弹体的 cannon_x = cannon.x - 40，落下时命中圆心 x = 弹体x + 80 = 目标x - 7
//   · 命中圆半径 115、行范围 ±1、走灰烬伤害（本体血量 < 1800 直接击杀，否则扣 1800）
// 因此“落点列”允许小数：目标x = 落点列 * 80（AvZ 的 Fire(row, col) 同款换算）。
enum : int {
    COB_IMPACT_RADIUS = 115,
    COB_IMPACT_X_OFFSET = -7,
    COB_ROW_RANGE = 1,
    COB_FLY_CS_LAND = 373,     // 平地经典提前量（炮弹飞行时间）
    COB_FLY_CS_WATER = 378,    // 泳池 / 浓雾的水路
    COB_FLY_CS_ROOF_REF = 387  // 屋顶参考值
};

// 屋顶按炮列校准的 {最短落点 x, 最短飞行时间}（第 1..8 列炮）
inline constexpr int COB_ROOF_MIN_DROP_X[8] = {515, 499, 515, 499, 515, 499, 511, 511};
inline constexpr int COB_ROOF_MIN_FLY_CS[8] = {359, 362, 364, 367, 369, 372, 373, 373};

// 炮弹飞行时间（发炮指令 → 命中），屋顶需要知道是哪一列炮发射的
inline int AGetCobFlyCs(int dropRow, float dropCol, int cobCol) {
    if (aFieldInfo.isRoof) {
        const int col = std::clamp(cobCol, 1, 8);
        const int dropX = static_cast<int>(dropCol * 80.0f);
        const int minDropX = COB_ROOF_MIN_DROP_X[col - 1];
        const int minFlyCs = COB_ROOF_MIN_FLY_CS[col - 1];
        if (dropX >= minDropX)
            return minFlyCs;
        return minFlyCs + 1 - (dropX - (minDropX - 1)) / 32;
    }
    if (aFieldInfo.hasPool && dropRow >= 1 && dropRow <= 6 && aFieldInfo.rowType[dropRow] == ARowType::POOL)
        return COB_FLY_CS_WATER;
    return COB_FLY_CS_LAND;
}

// ===========================================================================
//  帧内缓存
// ===========================================================================

// 缓存标签：主对象指针 + 两个时钟 + 脚本手动递增的版本号。
// 游戏逻辑每跑一帧时钟就变；同一帧内只有脚本自己种 / 铲植物会改变场上状态，
// 那时脚本调用 AInvalidateFrameCache() 让版本号 +1 即可。
struct AFrameStamp {
    AMainObject* mainObject = nullptr;
    int gameClock = -1;
    int globalClock = -1;
    unsigned version = 0;

    bool operator==(const AFrameStamp&) const = default;
};

inline unsigned aFrameCacheVersion = 0;

// 让所有帧内缓存作废（下次用到时重建）。脚本在同一帧里种下 / 铲掉植物后调用。
inline void AInvalidateFrameCache() {
    ++aFrameCacheVersion;
}

inline AFrameStamp ACurrentFrameStamp() {
    AFrameStamp stamp;
    APvzBase* base = AGetPvzBase();
    stamp.mainObject = base != nullptr ? base->MainObject() : nullptr;
    if (stamp.mainObject != nullptr) {
        stamp.gameClock = stamp.mainObject->GameClock();
        stamp.globalClock = stamp.mainObject->GlobalClock();
    }
    stamp.version = aFrameCacheVersion;
    return stamp;
}

// ---- 格子像素表：一关只问游戏一次 ----
// AAsm::GridToAbscissa / GridToOrdinate 是游戏函数调用（Board::GridToPixelX / Y），一关之内
// 每格的像素固定不变，只跟场地（Scene）和行数有关。原来每次重建快照都要调 108 次，
// 每格植物命中框（APlantHitBoxAt）又各调 2 次；现在按 (主对象, 场地, 行数) 记一份表，
// 换关才重新问游戏。值与直接调游戏函数完全相同。
struct ACellPixelTable {
    AMainObject* mainObject = nullptr;
    int scene = -1;
    int nRows = 0;
    bool built = false;
    int cellX[6][9] = {};       // 格子左上角像素
    int cellY[6][9] = {};
    bool rowYUniform[6] = {};   // 该行 9 格的 y 是否相同（屋顶不同列 y 不同）
};

inline const ACellPixelTable& AGetCellPixelTable() {
    static ACellPixelTable table;
    APvzBase* base = AGetPvzBase();
    AMainObject* mainObject = base != nullptr ? base->MainObject() : nullptr;
    const int nRows = std::clamp(aFieldInfo.nRows, 0, 6);
    const int scene = mainObject != nullptr ? mainObject->Scene() : -1;
    if (!table.built || table.mainObject != mainObject || table.scene != scene || table.nRows != nRows) {
        table.built = true;
        table.mainObject = mainObject;
        table.scene = scene;
        table.nRows = nRows;
        for (int row = 0; row < 6; ++row) {
            bool uniform = true;
            for (int col = 0; col < 9; ++col) {
                if (mainObject != nullptr && row < nRows) {
                    table.cellX[row][col] = AAsm::GridToAbscissa(row, col);
                    table.cellY[row][col] = AAsm::GridToOrdinate(row, col);
                } else {
                    table.cellX[row][col] = 0;
                    table.cellY[row][col] = 0;
                }
                if (table.cellY[row][col] != table.cellY[row][0])
                    uniform = false;
            }
            table.rowYUniform[row] = uniform;
        }
    }
    return table;
}

// 一只存活僵尸在本帧的快照（都是只跟僵尸自身有关、跟“放哪一格”无关的量）
struct AZombieSnapshot {
    AZombie* zombie = nullptr;
    int index = 0;              // 僵尸内存数组下标
    int row = 0;                // 0 起
    int type = 0;
    int state = 0;
    bool isEat = false;
    bool walkingRight = false;  // AIsWalkingRight
    bool dead = false;          // AIsDead（存活过滤之后恒为 false，保留只为与原逻辑逐句对应）
    bool lethal = false;        // AIsLethal：本体 + 一类防具 < 1800
    AHitBox box;                // 当前帧判定框（AGetZombieHitBox）

    // 推演 cs 帧后的判定框，按 cs 记忆几组（樱桃 100 / 倭瓜 155、180 / 毁灭菇 299…）
    static constexpr int BOX_MEMO_SIZE = 4;
    int boxMemoCs[BOX_MEMO_SIZE] = {};
    AHitBox boxMemo[BOX_MEMO_SIZE];
    int boxMemoCount = 0;

    // ACanBeAttacked(zombie, FLAG_SQUASH)：-1 = 还没算
    signed char squashAttackable = -1;
};

// 一颗存活植物在本帧的快照
struct APlantSnapshot {
    APlant* plant = nullptr;
    int index = 0;      // APlant::Index()
    int row = 0;        // 0 起
    int col = 0;        // 0 起
    int type = 0;       // APlant::Type()
    int baseType = 0;   // APlantBaseType
    int state = 0;
    int x = 0;          // Abscissa
    int y = 0;          // Ordinate
    bool sleeping = false;
};

// 某种即时植物 + 某组预测参数的预处理结果：每只“能被打到”的僵尸的预测判定框，
// 以及每个格子能炸到哪些僵尸（CSR 格式，格内按僵尸数组顺序）
struct APreparedHitEntry {
    int slot = 0;          // AFrameSnapshot::zombies 下标
    AHitBox box;           // 命中那一帧的判定框
    float abscissa = 0.0f; // box.x + box.width / 2
    bool lethal = false;
};

struct APreparedInstantPlant {
    bool used = false;
    APlantType type = ACHERRY_BOMB;
    bool imitator = false;
    AInstantPlantPredictOptions options;
    Rule rule;
    int predictCs = 0;
    std::vector<APreparedHitEntry> entries;  // 数组顺序
    std::vector<int> cellBegin;              // 大小 nRows * 9 + 1
    std::vector<int> cellEntries;            // entries 下标
};

struct AFrameSnapshot {
    AFrameStamp stamp;
    bool built = false;

    std::vector<AZombieSnapshot> zombies;   // 存活僵尸（aAliveZombieFilter 同款判定），内存数组顺序
    std::vector<int> zombieSlot;            // 内存数组下标 -> zombies 下标，-1 = 不存活
    std::vector<int> rowZombies[6];         // 每行的存活僵尸（zombies 下标），仍是数组顺序
    std::vector<APlantSnapshot> plants;     // 存活植物（aAlivePlantFilter 同款判定），内存数组顺序

    // 每格上的存活植物（plants 下标，格内仍是数组顺序），CSR：cellPlantBegin[row*9+col .. +1)
    // 行 / 列不在 6×9 之内的植物（正常不会有）不进表，查询时退回全表扫描
    std::vector<int> cellPlantBegin;        // 大小 6*9 + 1
    std::vector<int> cellPlantEntries;

    int nRows = 0;                          // 快照时的 aFieldInfo.nRows
    int cellX[6][9] = {};                   // 格子左上角像素（AAsm::GridToAbscissa / GridToOrdinate）
    int cellY[6][9] = {};
    bool rowYUniform[6] = {};               // 该行 9 格的 y 是否相同（屋顶不同列 y 不同）

    static constexpr int PREPARED_SIZE = 4;
    std::array<APreparedInstantPlant, PREPARED_SIZE> prepared;
    int preparedNext = 0;

    // 僵尸指针 -> zombies 下标；不在快照里（不存活 / 非法指针）返回 -1
    int Slot(AZombie* zombie) const {
        if (stamp.mainObject == nullptr || zombie == nullptr)
            return -1;
        const std::ptrdiff_t index = zombie - stamp.mainObject->ZombieArray();
        if (index < 0 || index >= static_cast<std::ptrdiff_t>(zombieSlot.size()))
            return -1;
        return zombieSlot[static_cast<std::size_t>(index)];
    }
};

inline void __ARebuildFrameSnapshot(AFrameSnapshot& snap, const AFrameStamp& stamp) {
    snap.stamp = stamp;
    snap.built = true;
    snap.zombies.clear();
    snap.zombieSlot.clear();
    for (auto& rowList : snap.rowZombies)
        rowList.clear();
    snap.plants.clear();
    snap.nRows = 0;
    for (auto& prep : snap.prepared)
        prep.used = false;
    snap.preparedNext = 0;

    AMainObject* mainObject = stamp.mainObject;
    if (mainObject == nullptr)
        return;

    // ---- 僵尸：与 __AFilterTrait<AZombie>::IsAlive（aAliveZombieFilter 的判定）完全一致 ----
    AZombie* zombieArray = mainObject->ZombieArray();
    const int zombieTotal = mainObject->ZombieTotal();
    snap.zombieSlot.assign(zombieTotal > 0 ? zombieTotal : 0, -1);
    for (int i = 0; i < zombieTotal; ++i) {
        AZombie* zombie = zombieArray + i;
        if (!__AFilterTrait<AZombie>::IsAlive(zombie))
            continue;
        AZombieSnapshot entry;
        entry.zombie = zombie;
        entry.index = i;
        entry.row = zombie->Row();
        entry.type = zombie->Type();
        entry.state = zombie->State();
        entry.isEat = zombie->IsEat();
        entry.walkingRight = AIsWalkingRight(zombie);
        entry.dead = AIsDead(zombie);
        entry.lethal = AZombieHp(zombie) < 1800;
        entry.box = AGetZombieHitBox(zombie);
        const int slot = static_cast<int>(snap.zombies.size());
        snap.zombieSlot[i] = slot;
        if (entry.row >= 0 && entry.row < 6)
            snap.rowZombies[entry.row].push_back(slot);
        snap.zombies.push_back(entry);
    }

    // ---- 植物：与 __AFilterTrait<APlant>::IsAlive（aAlivePlantFilter 的判定）完全一致 ----
    APlant* plantArray = mainObject->PlantArray();
    const int plantTotal = mainObject->PlantTotal();
    for (int i = 0; i < plantTotal; ++i) {
        APlant* plant = plantArray + i;
        if (!__AFilterTrait<APlant>::IsAlive(plant))
            continue;
        APlantSnapshot entry;
        entry.plant = plant;
        entry.index = plant->Index();
        entry.row = plant->Row();
        entry.col = plant->Col();
        entry.type = plant->Type();
        entry.baseType = APlantBaseType(plant);
        entry.state = plant->State();
        entry.x = plant->Abscissa();
        entry.y = plant->Ordinate();
        entry.sleeping = plant->IsSleeping();
        snap.plants.push_back(entry);
    }

    // ---- 每格植物表（计数排序成 CSR，格内保持数组顺序）----
    snap.cellPlantBegin.assign(6 * 9 + 1, 0);
    for (const APlantSnapshot& plant : snap.plants)
        if (plant.row >= 0 && plant.row < 6 && plant.col >= 0 && plant.col < 9)
            ++snap.cellPlantBegin[plant.row * 9 + plant.col + 1];
    for (int cell = 0; cell < 6 * 9; ++cell)
        snap.cellPlantBegin[cell + 1] += snap.cellPlantBegin[cell];
    snap.cellPlantEntries.assign(snap.cellPlantBegin.back(), 0);
    {
        static std::vector<int> cursor;  // 复用容量，免得每次重建快照都分配一次
        cursor.assign(snap.cellPlantBegin.begin(), snap.cellPlantBegin.end() - 1);
        for (std::size_t i = 0; i < snap.plants.size(); ++i) {
            const APlantSnapshot& plant = snap.plants[i];
            if (plant.row >= 0 && plant.row < 6 && plant.col >= 0 && plant.col < 9)
                snap.cellPlantEntries[cursor[plant.row * 9 + plant.col]++] = static_cast<int>(i);
        }
    }

    // ---- 格子像素：从“一关一份”的表里抄，不再每帧调 108 次游戏函数 ----
    const ACellPixelTable& table = AGetCellPixelTable();
    snap.nRows = table.nRows;
    for (int row = 0; row < 6; ++row) {
        for (int col = 0; col < 9; ++col) {
            snap.cellX[row][col] = table.cellX[row][col];
            snap.cellY[row][col] = table.cellY[row][col];
        }
        snap.rowYUniform[row] = table.rowYUniform[row];
    }
}

// 本帧快照；标签变了才重建
inline AFrameSnapshot& AGetFrameSnapshot() {
    static AFrameSnapshot snap;
    const AFrameStamp stamp = ACurrentFrameStamp();
    if (!snap.built || !(snap.stamp == stamp))
        __ARebuildFrameSnapshot(snap, stamp);
    return snap;
}

// 判定框推演 cs 帧后的位置（= AHitBoxAfterCs(AGetZombieHitBox(z), z, cs)），同一 cs 只算一次
inline AHitBox ABoxAfterCs(AZombieSnapshot& entry, int cs) {
    if (cs <= 0)
        return entry.box;
    for (int i = 0; i < entry.boxMemoCount; ++i)
        if (entry.boxMemoCs[i] == cs)
            return entry.boxMemo[i];
    const AHitBox box = AHitBoxAfterCs(entry.box, entry.zombie, cs);
    if (entry.boxMemoCount < AZombieSnapshot::BOX_MEMO_SIZE) {
        entry.boxMemoCs[entry.boxMemoCount] = cs;
        entry.boxMemo[entry.boxMemoCount] = box;
        ++entry.boxMemoCount;
    }
    return box;
}

// ACanBeAttacked(zombie, FLAG_SQUASH)，一帧只算一次
inline bool ASquashAttackable(AZombieSnapshot& entry) {
    if (entry.squashAttackable < 0)
        entry.squashAttackable = ACanBeAttacked(entry.zombie, FLAG_SQUASH, &entry.box) ? 1 : 0;
    return entry.squashAttackable != 0;
}

// 与 AvZ 的 AGetPlantIndex(row, col, type) 逐句同义，只是在快照上查
//   type == -1：无视南瓜 / 花盆 / 荷叶 / 咖啡豆；type == -2：只要有植物即可；
//   其它：该类型植物的对象序列，有别的植物返回 -2，没有植物返回 -1
inline int __AMatchPlantIndex(const APlantSnapshot& plant, int type, bool& decided) {
    decided = true;
    const int plantType = plant.type;
    if (type == -1) {
        if ((plantType != APUMPKIN) && (plantType != AFLOWER_POT) && (plantType != ALILY_PAD) && (plantType != ACOFFEE_BEAN))
            return plant.index;
    } else {
        if (type == -2 || plantType == type)
            return plant.index;
        else if (type != APUMPKIN && type != AFLOWER_POT && type != ALILY_PAD && type != ACOFFEE_BEAN && plantType != APUMPKIN && plantType != AFLOWER_POT && plantType != ALILY_PAD && plantType != ACOFFEE_BEAN)
            return -2;
    }
    decided = false;
    return -1;
}

inline int AFindPlantIndex(const AFrameSnapshot& snap, int row, int col, int type = -1) {
    // 格子在 6×9 之内：只看该格的植物（顺序与全表一致）
    if (row >= 1 && row <= 6 && col >= 1 && col <= 9 && !snap.cellPlantBegin.empty()) {
        const int cell = (row - 1) * 9 + (col - 1);
        for (int i = snap.cellPlantBegin[cell]; i < snap.cellPlantBegin[cell + 1]; ++i) {
            const APlantSnapshot& plant = snap.plants[snap.cellPlantEntries[i]];
            if (plant.type == ASQUASH && plant.state >= 5)
                continue;
            bool decided = false;
            const int result = __AMatchPlantIndex(plant, type, decided);
            if (decided)
                return result;
        }
        return -1;
    }
    // 格子不在表里（正常不会发生）：全表扫描，逐句同 AGetPlantIndex
    for (const APlantSnapshot& plant : snap.plants) {
        if (plant.row != row - 1 || plant.col != col - 1)
            continue;
        if (plant.type == ASQUASH && plant.state >= 5)
            continue;
        bool decided = false;
        const int result = __AMatchPlantIndex(plant, type, decided);
        if (decided)
            return result;
    }
    return -1;
}

// 与 AvZ 的 AGetPlantPtr(row, col, type) 同义
inline APlant* AFindPlantPtr(const AFrameSnapshot& snap, int row, int col, int type = -1) {
    const int index = AFindPlantIndex(snap, row, col, type);
    if (index < 0 || snap.stamp.mainObject == nullptr)
        return nullptr;
    return snap.stamp.mainObject->PlantArray() + index;
}

// 圆形 / 整行判定的植物（樱桃 / 辣椒 / 毁灭菇）：一次算出每只僵尸的预测判定框、能否被打到，
// 以及它落在哪些格子的判定范围里。之后每格查询只是把该格的列表按原来的顺序取出来。
inline void __APrepareCircleRule(AFrameSnapshot& snap, APreparedInstantPlant& prep) {
    const int nRows = snap.nRows;
    const int cellCount = nRows * 9;
    prep.entries.clear();
    prep.cellBegin.assign(cellCount + 1, 0);
    prep.cellEntries.clear();

    const int rowRange = prep.rule.rowRange;
    const bool wholeRow = prep.rule.wholeRow;
    const int radius = prep.rule.radius;
    const unsigned flags = prep.rule.flags;
    const bool useMotion = prep.options.useMotion;
    const int predictCs = prep.predictCs;

    // (格子, entries 下标)，按僵尸数组顺序追加 → 同一格内自然就是数组顺序
    // （静态复用容量：这个函数不会重入，每帧最多为几种卡各跑一次，不必每次都分配）
    static std::vector<std::pair<int, int>> pairs;
    pairs.clear();
    for (std::size_t slot = 0; slot < snap.zombies.size(); ++slot) {
        AZombieSnapshot& zombie = snap.zombies[slot];
        // 判定框按“命中那一帧”的位置算；连“还在场外（x > 800）”也要用这个框判
        const AHitBox box = useMotion ? ABoxAfterCs(zombie, predictCs) : zombie.box;
        if (!ACanBeAttacked(zombie.zombie, flags, &box))
            continue;

        const int entryIndex = static_cast<int>(prep.entries.size());
        APreparedHitEntry entry;
        entry.slot = static_cast<int>(slot);
        entry.box = box;
        entry.abscissa = static_cast<float>(box.x + box.width / 2);
        entry.lethal = zombie.lethal;
        prep.entries.push_back(entry);

        for (int delta = -rowRange; delta <= rowRange; ++delta) {
            const int row = zombie.row + delta;  // 格子行 = 僵尸行 + delta，即 |zombie.Row() - gridRow| <= rowRange
            if (row < 0 || row >= nRows)
                continue;
            if (wholeRow) {
                for (int col = 0; col < 9; ++col)
                    pairs.emplace_back(row * 9 + col, entryIndex);
            } else if (snap.rowYUniform[row]) {
                int lo = 0, hi = 0;
                if (!AHitBoxCircleXRange(box, snap.cellY[row][0] + 40, radius, lo, hi))
                    continue;
                for (int col = 0; col < 9; ++col) {
                    const int px = snap.cellX[row][col] + 40;
                    if (lo <= px && px <= hi)
                        pairs.emplace_back(row * 9 + col, entryIndex);
                }
            } else {
                for (int col = 0; col < 9; ++col) {
                    if (AHitBoxOverlapCircle(box, snap.cellX[row][col] + 40, snap.cellY[row][col] + 40, radius))
                        pairs.emplace_back(row * 9 + col, entryIndex);
                }
            }
        }
    }

    // 计数排序成 CSR（稳定：同一格内保持追加顺序）
    for (const auto& pair : pairs)
        ++prep.cellBegin[pair.first + 1];
    for (int cell = 0; cell < cellCount; ++cell)
        prep.cellBegin[cell + 1] += prep.cellBegin[cell];
    prep.cellEntries.assign(pairs.size(), 0);
    static std::vector<int> cursor;  // 同上，复用容量
    cursor.assign(prep.cellBegin.begin(), prep.cellBegin.end() - 1);
    for (const auto& pair : pairs)
        prep.cellEntries[cursor[pair.first]++] = pair.second;
}

inline APreparedInstantPlant& __AGetPreparedInstantPlant(AFrameSnapshot& snap, APlantType type, bool imitator,
                                                         const AInstantPlantPredictOptions& options,
                                                         const Rule& rule, int predictCs) {
    for (auto& prep : snap.prepared) {
        if (prep.used && prep.type == type && prep.imitator == imitator && prep.options == options)
            return prep;
    }
    APreparedInstantPlant& prep = snap.prepared[snap.preparedNext];
    snap.preparedNext = (snap.preparedNext + 1) % AFrameSnapshot::PREPARED_SIZE;
    prep.used = true;
    prep.type = type;
    prep.imitator = imitator;
    prep.options = options;
    prep.rule = rule;
    prep.predictCs = predictCs;
    __APrepareCircleRule(snap, prep);
    return prep;
}

}  // namespace aInstantPlant

// ---------------------------------------------------------------------------
// 对外接口
// ---------------------------------------------------------------------------

// 输入放置位置（行、列，均从 1 开始，与 ACard 一致）、植物类型、是否模仿者，
// 把预计会被该植物攻击到的僵尸写进 result（按命中时间升序，同一时刻按横坐标从右到左）。
// 这是“不分配内存”的版本：result 先清空再填，容量复用 —— 脚本每帧对几十个格子各问一次，
// 用一个静态 / 复用的 result 就不必每格都分配一次。内容与 APredictInstantPlantHits 完全相同。
inline void APredictInstantPlantHitsInto(
    int row, int col, APlantType type, std::vector<AInstantPlantHit>& result, bool imitator = false,
    const AInstantPlantPredictOptions& options = {}) {
    using namespace aInstantPlant;

    result.clear();

    Rule rule;
    if (!AGetRule(type, imitator, rule))
        return;
    if (row < 1 || row > aFieldInfo.nRows || col < 1 || col > 9)
        return;

    AFrameSnapshot& snap = AGetFrameSnapshot();
    if (snap.stamp.mainObject == nullptr || row > snap.nRows)
        return;

    const int gridRow = row - 1;
    const int gridCol = col - 1;

    // 格子左上角由游戏给出：5 行 / 6 行 / 天台的坐标与行距都能自动适配
    const int tileX = snap.cellX[gridRow][gridCol];
    const int tileY = snap.cellY[gridRow][gridCol];
    const int centerX = tileX + 40;

    const int predictCs = options.predictCs >= 0 ? options.predictCs : rule.effectCs;

    if (rule.kind != Kind::Squash) {
        // 每只僵尸的预测判定框 / 能否被打到 / 落在哪些格子里，一帧只算一次（见 __APrepareCircleRule）
        const APreparedInstantPlant& prep = __AGetPreparedInstantPlant(snap, type, imitator, options, rule, predictCs);
        const int cell = gridRow * 9 + gridCol;
        for (int i = prep.cellBegin[cell]; i < prep.cellBegin[cell + 1]; ++i) {
            const APreparedHitEntry& hit = prep.entries[prep.cellEntries[i]];
            AInstantPlantHit entry;
            entry.zombie = snap.zombies[hit.slot].zombie;
            entry.eta = predictCs;
            entry.damage = 1800;
            entry.lethal = hit.lethal;
            entry.abscissa = hit.abscissa;
            entry.offset = entry.abscissa - static_cast<float>(centerX);
            if (options.onlyLethal && !entry.lethal)
                continue;
            result.push_back(entry);
        }
    } else {
        // ---- 倭瓜 ----
        // 攻击框固定在格子前方：x + 20，宽 45，高 80
        AHitBox attackBox;
        attackBox.x = tileX + SQUASH_ATTACK_X_OFFSET;
        attackBox.y = tileY;
        attackBox.width = SQUASH_ATTACK_WIDTH;
        attackBox.height = 80;

        const int horizon = std::max(0, options.squashWaitingCs);
        const int step = std::max(1, options.squashScanStepCs);

        int triggerCs = -1;
        int targetSlot = -1;

        // 只有本行的僵尸参与（原逻辑逐只判 zombie.Row() != gridRow），行内仍是数组顺序
        const std::vector<int>& rowSlots = snap.rowZombies[gridRow];

        for (int cs = 0; cs <= horizon; cs += step) {
            float bestOverlap = 0.0f;
            int bestSlot = -1;

            for (int slot : rowSlots) {
                AZombieSnapshot& zombie = snap.zombies[slot];
                if (zombie.dead)
                    continue;
                if (!ASquashAttackable(zombie))
                    continue;

                // 撑杆跳 / 潜水跳 / 海豚跳这些腾空状态不会被倭瓜锁定
                const int state = zombie.state;
                if (state == STATE_POLE_JUMPING || state == STATE_SNORKEL_JUMP_IN_POOL ||
                    state == STATE_DOLPHIN_JUMP_IN_POOL || state == STATE_DOLPHIN_RIDE ||
                    state == STATE_DOLPHIN_JUMP)
                    continue;

                const AHitBox box = options.useMotion ? ABoxAfterCs(zombie, cs) : zombie.box;

                // 撑杆跳助跑阶段，僵尸还在攻击框左边时不会被锁定
                if (state == STATE_POLE_RUNNING && box.x < tileX + 20)
                    continue;

                const float overlap = AHitBoxOverlapLenX(attackBox, box);
                const int threshold = zombie.isEat ? SQUASH_TRIGGER_RANGE_EATING : SQUASH_TRIGGER_RANGE;
                if (-overlap > static_cast<float>(threshold))
                    continue;

                int frontEdge = attackBox.x;
                if (state == STATE_POLE_WALKING || state == STATE_POLE_RUNNING ||
                    state == STATE_DOLPHIN_WALK_IN_POOL || zombie.type == AIMP ||
                    zombie.type == AFOOTBALL_ZOMBIE)
                    frontEdge -= 60;

                if (!zombie.walkingRight && box.Right() < frontEdge)
                    continue;

                // 复刻 squash::find_target：先重叠最深的，再距离最近的
                if (bestSlot < 0 || overlap < bestOverlap) {
                    bestOverlap = overlap;
                    bestSlot = slot;
                }
            }

            if (bestSlot >= 0) {
                triggerCs = cs;
                targetSlot = bestSlot;
                break;
            }
        }

        if (targetSlot >= 0) {
            // 起跳结束时用 predict_after(target, 30) 定落点，砸下发生在触发后 180cs
            const int aimCs = triggerCs + SQUASH_LOOK_CS + SQUASH_JUMP_UP_CS + SQUASH_AIM_PREDICT_CS;
            const AHitBox aimBox = ABoxAfterCs(snap.zombies[targetSlot], aimCs);
            const int aimCenter = aimBox.x + aimBox.width / 2;
            const int landingX = aimCenter - 40;  // attack_box.width / 2

            AHitBox killBox = attackBox;
            killBox.x = landingX + SQUASH_ATTACK_X_OFFSET;

            const int killCs = triggerCs + SQUASH_TOTAL_CS;
            const int killCenter = killBox.x + killBox.width / 2;

            for (int slot : rowSlots) {
                AZombieSnapshot& zombie = snap.zombies[slot];
                if (!ASquashAttackable(zombie))
                    continue;

                const AHitBox box = options.useMotion ? ABoxAfterCs(zombie, killCs) : zombie.box;

                const float overlap = AHitBoxOverlapLenX(killBox, box);
                const float need = zombie.type == AFOOTBALL_ZOMBIE ? -static_cast<float>(SQUASH_FOOTBALL_GRACE) : 0.0f;
                if (!(overlap > need))
                    continue;

                AInstantPlantHit entry;
                entry.zombie = zombie.zombie;
                entry.eta = killCs;
                entry.damage = 1800;
                entry.lethal = zombie.lethal;
                entry.abscissa = static_cast<float>(box.x + box.width / 2);
                entry.offset = entry.abscissa - static_cast<float>(killCenter);
                if (options.onlyLethal && !entry.lethal)
                    continue;
                result.push_back(entry);
            }
        }
    }

    std::sort(result.begin(), result.end(), [](const AInstantPlantHit& lhs, const AInstantPlantHit& rhs) {
        if (lhs.eta != rhs.eta)
            return lhs.eta < rhs.eta;
        return lhs.abscissa > rhs.abscissa;
    });
}

// 返回新 vector 的版本（原接口）：内容与 APredictInstantPlantHitsInto 完全相同
inline std::vector<AInstantPlantHit> APredictInstantPlantHits(
    int row, int col, APlantType type, bool imitator = false,
    const AInstantPlantPredictOptions& options = {}) {
    std::vector<AInstantPlantHit> result;
    APredictInstantPlantHitsInto(row, col, type, result, imitator, options);
    return result;
}

// 只要僵尸地址的简版
inline std::vector<AZombie*> APredictInstantPlantTargets(
    int row, int col, APlantType type, bool imitator = false,
    const AInstantPlantPredictOptions& options = {}) {
    std::vector<AZombie*> result;
    for (const auto& hit : APredictInstantPlantHits(row, col, type, imitator, options))
        result.push_back(hit.zombie);
    return result;
}

// 玉米加农炮：落点 (row, col) 允许小数（与 ACobManager::RawFire 的 dropCol 一致）
// cobCol 只在屋顶用来查炮弹飞行时间（0 = 未知，按平地 373cs 处理）
inline std::vector<AInstantPlantHit> APredictCobCannonHits(
    int row, float col, int cobCol = 0, const AInstantPlantPredictOptions& options = {}) {
    using namespace aInstantPlant;

    std::vector<AInstantPlantHit> result;
    if (row < 1 || row > aFieldInfo.nRows || !(col > 0.0f))
        return result;

    // 与 AvZ 的 AGridToCoordinate / 游戏内取行高的口径一致
    const int x = std::clamp(static_cast<int>(col * 80.0f + 0.5f), 0, 799);
    const int nearCol = std::clamp(static_cast<int>(col + 0.5f), 1, 9);
    const int hitX = x + COB_IMPACT_X_OFFSET;
    const int flyCs = options.predictCs >= 0 ? options.predictCs : AGetCobFlyCs(row, col, cobCol);

    AFrameSnapshot& snap = AGetFrameSnapshot();
    // 格子像素查快照里的表（值与 AAsm::GridToOrdinate 相同），行不在表里才问游戏
    const int hitY = (snap.stamp.mainObject != nullptr && row - 1 < snap.nRows)
                         ? snap.cellY[row - 1][nearCol - 1] + 40
                         : AAsm::GridToOrdinate(row - 1, nearCol - 1) + 40;
    for (AZombieSnapshot& zombie : snap.zombies) {
        const int rowDelta = zombie.row - (row - 1);
        if (rowDelta > COB_ROW_RANGE || rowDelta < -COB_ROW_RANGE)
            continue;

        const AHitBox box = options.useMotion ? ABoxAfterCs(zombie, flyCs) : zombie.box;
        if (!ACanBeAttacked(zombie.zombie, FLAG_ASH, &box))
            continue;
        if (!AHitBoxOverlapCircle(box, hitX, hitY, COB_IMPACT_RADIUS))
            continue;

        AInstantPlantHit entry;
        entry.zombie = zombie.zombie;
        entry.eta = flyCs;
        entry.damage = 1800;
        entry.lethal = zombie.lethal;  // 炮走灰烬伤害
        entry.abscissa = static_cast<float>(box.x + box.width / 2);
        entry.offset = entry.abscissa - static_cast<float>(hitX);
        if (options.onlyLethal && !entry.lethal)
            continue;
        result.push_back(entry);
    }

    std::sort(result.begin(), result.end(), [](const AInstantPlantHit& lhs, const AInstantPlantHit& rhs) {
        if (lhs.eta != rhs.eta)
            return lhs.eta < rhs.eta;
        return lhs.abscissa > rhs.abscissa;
    });
    return result;
}

inline std::vector<AZombie*> APredictCobCannonTargets(
    int row, float col, int cobCol = 0, const AInstantPlantPredictOptions& options = {}) {
    std::vector<AZombie*> result;
    for (const auto& hit : APredictCobCannonHits(row, col, cobCol, options))
        result.push_back(hit.zombie);
    return result;
}

// 把预测结果打到日志里，方便调参
inline void ALogInstantPlantPrediction(
    int row, int col, APlantType type, bool imitator = false,
    const AInstantPlantPredictOptions& options = {}) {
    const auto hits = APredictInstantPlantHits(row, col, type, imitator, options);
    aLogger->Info("[Predict] {} ({},{}) imitator={} -> {} 只僵尸", type, row, col, imitator, hits.size());
    for (const auto& hit : hits) {
        aLogger->Info("    zombie={} type={} row={} hp={} eta={}cs lethal={} x={}",
                      static_cast<const void*>(hit.zombie), hit.zombie->Type(), hit.zombie->Row() + 1,
                      hit.zombie->Hp() + hit.zombie->OneHp() + hit.zombie->TwoHp(), hit.eta, hit.lethal, hit.abscissa);
    }
}

#endif  // __A_INSTANT_PLANT_PREDICT_H__
