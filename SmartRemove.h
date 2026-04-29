/*
 * @Description：A-TAS智能铲除
 */
#pragma once
#ifndef __SMART_REMOVE_H__
#define __SMART_REMOVE_H__
#include "avz.h"

namespace A_TAS {

// 在当帧更新植物动画的颜色
inline void UpdateReanimColor(int plant_index) {
    asm volatile(
        "movl 0x6A9EC0, %%eax;"
        "movl 0x768(%%eax), %%eax;"
        "movl 0xAC(%%eax), %%eax;"
        "movl $0x14C, %%ecx;"
        "imull %[plant_index], %%ecx;"
        "addl %%ecx, %%eax;"
        "pushl %%eax;"
        "movl $0x4635C0, %%edx;"
        "calll *%%edx;"
        :
        : [plant_index] "m"(plant_index)
        : "eax", "ecx", "edx");
}

// 得到植物防御域
inline std::pair<int, int> GetDefenseRange(APlantType type) {
    switch (type) {
    case ATALL_NUT:
        return {30, 70};
    case APUMPKIN:
        return {20, 80};
    case ACOB_CANNON:
        return {20, 120};
    default:
        return {30, 50}; // 普通
    }
}

// 得到僵尸攻击域
inline std::pair<int, int> GetAttackRange(AZombieType type) {
    switch (type) {
    case AGIGA_GARGANTUAR:
    case AGARGANTUAR:
        return {-30, 59};
    default:
        return {10, 143}; // 双车
    }
}

// 判断某僵尸攻击域和某植物防御域是否重叠
// 植物坐标+植物防御域右伸>=僵尸坐标+僵尸攻击域左伸，且植物坐标+植物防御域左伸<=僵尸坐标+僵尸攻击域右伸
inline bool isRangeOverlap(APlantType plant_type, AZombieType zombie_type, int plant_x, int zombie_x) {
    auto [p_left, p_right] = GetDefenseRange(plant_type);
    auto [z_left, z_right] = GetAttackRange(zombie_type);
    return plant_x + p_right >= zombie_x + z_left && plant_x + p_left <= zombie_x + z_right;
}

#define IsDanger(plant, zombie) (plant != nullptr && isRangeOverlap(APlantType(plant->Type()), AZombieType(zombie->Type()), int(plant->Abscissa()), int(zombie->Abscissa())))

// 检测下一帧植物会不会自然消失
// 目前只考虑樱桃 辣椒 核武 三叶 窝瓜 墓碑
inline bool isPlantDisappearedImmediately(APlant* p) {
    if (p == nullptr)
        return true;

    return (ARangeIn(p->Type(), {ACHERRY_BOMB, AJALAPENO, ADOOM_SHROOM}) && p->ExplodeCountdown() == 1)
        || (p->Type() == ABLOVER && p->State() == 2 && p->BloverCountdown() == 1)
        || (p->Type() == ASQUASH && p->State() == 4 && p->StateCountdown() == 1)
        || (p->Type() == AGRAVE_BUSTER && p->State() == 9 && p->StateCountdown() == 1);
}

class SmartRemove : public ATickRunnerWithNoStart {
public:
    void Start() {
        ATickRunnerWithNoStart::_Start([this] {
            _UpdatePlantMap();
            _UpdateZombie();
            _Run();
        },
            ATickRunner::ONLY_FIGHT);
    }

    void SetSparkle(bool IsSparkle) { _isSparkle = IsSparkle; }

    // 检查指定帧后是否有冰菇生效。应填写1~100的整数。没变身的模仿者不统计。
    static bool isCertainTickIce(int delayTime = 1) {
        for (auto& p : aAlivePlantFilter) {
            if (p.Type() == AICE_SHROOM && p.ExplodeCountdown() == delayTime)
                return true;
        }
        return false;
    }

    enum GridPlantType {
        unknown = -1,  // 未知
        container = 0, // 容器
        pumpkin,       // 南瓜
        coffee,        // 咖啡
        common,        // 常规
        fly,           // 飞行窝瓜
    };

    std::array<APlant*, 5> Grid(int row, int col) {
        return _PlantMap[GridToIndex(row - 1, col - 1)];
    }

    bool alive(int row, int col, GridPlantType type) {
        return Grid(row, col)[type] != nullptr;
    }

protected:
    constexpr static int ROWS = 6, COLS = 9;
    std::array<std::array<APlant*, 5>, ROWS * COLS> _PlantMap;

    size_t GridToIndex(int row, int col) {
        return row * COLS + col;
    }

private:
    std::vector<AZombie*> _HammerVec;
    std::vector<AZombie*> _crushVec;
    std::vector<AZombie*> _sparkleVec;

    GridPlantType right_plant_map_type; // 右格植物类型
    int right_index;                    // 右格植物栈位
    int index;                          // 本格植物栈位
    int gargantuar_amount;              // 统计下一帧有多少巨人能砸到右格植物，仅在数量为1时考虑栈位垫
    bool remove;                        // 初次筛选后，若下一帧至少有一个巨人可以被合法骗锤，正开关为真
    bool sparkle;                       // 初次筛选后，若脚本预估至少有一个巨人可以被合法骗锤，正开关为真

    bool _isSparkle = true;

    void _UpdatePlantMap() {
        for (auto& ptrs : _PlantMap) {
            ptrs.fill(nullptr);
        }
        for (auto& Plant : aAlivePlantFilter) {
            switch (Plant.Type()) {
            case ALILY_PAD:
            case AFLOWER_POT:
                _PlantMap[GridToIndex(Plant.Row(), Plant.Col())][container] = &Plant;
                break;
            case APUMPKIN:
                _PlantMap[GridToIndex(Plant.Row(), Plant.Col())][pumpkin] = &Plant;
                break;
            case ACOFFEE_BEAN:
                _PlantMap[GridToIndex(Plant.Row(), Plant.Col())][coffee] = &Plant;
                break;
            case ASQUASH:
                if (Plant.State() == 5 || Plant.State() == 6) // 5-上升 6-下落
                    _PlantMap[GridToIndex(Plant.Row(), Plant.Col())][fly] = &Plant;
                else
                    _PlantMap[GridToIndex(Plant.Row(), Plant.Col())][common] = &Plant;
                break;
            default:
                _PlantMap[GridToIndex(Plant.Row(), Plant.Col())][common] = &Plant;
                break;
            }
        }
    }

    void _UpdateZombie() {
        _crushVec.clear();
        _HammerVec.clear();
        if (_isSparkle) {
            _sparkleVec.clear();
        }
        bool canIce4 = false; // 下一帧是否可能Ice4（即是否有冰菇生效）。默认玩家不会自铲
        bool isCanIce4Verified = false;
        for (auto& zombie : aAliveZombieFilter) {
            if ((zombie.Type() == AGIGA_GARGANTUAR || zombie.Type() == AGARGANTUAR) && zombie.State() == 70) {
                if (!isCanIce4Verified) {
                    canIce4 = isCertainTickIce(1);
                    isCanIce4Verified = true; // 没巨人举锤时不查冰菇，有巨人举锤时只查一次
                }
                float cr = zombie.AnimationPtr()->CirculationRate();
                float crLast = zombie.AnimationPtr()->MRef<float>(0x94); // 上一帧动画循环率
                bool isFrozen = zombie.FreezeCountdown() ? true : false; // 僵尸是否被冻结
                if (!isFrozen && !canIce4 && ((0.641 < cr && cr < 0.643) || (0.643 < cr && cr < 0.645 && 0.639 < crLast && crLast < 0.641)))
                    _HammerVec.push_back(&zombie); // 包含了原速转减速
                else if (_isSparkle && (cr < 0.641 || (0.641 < cr && cr < 0.643 && !isFrozen) || (0.643 < cr && cr < 0.645 && 0.639 < crLast && crLast < 0.641)))
                    _sparkleVec.push_back(&zombie); // 包含了原速转减速
            }
            if ((zombie.Type() == ACATAPULT_ZOMBIE || zombie.Type() == AZOMBONI) && zombie.State() == 0) {
                _crushVec.push_back(&zombie);
            }
        }
    }

    void _Run() {
        for (auto& Plant : aAlivePlantFilter) {
            int row = Plant.Row() + 1;
            int col = Plant.Col() + 1;
            gargantuar_amount = 0;
            remove = false;
            index = Plant.Index();
            right_index = -1;
            right_plant_map_type = unknown;
            // 特判右格不是下一帧生效的毁灭菇
            if (col <= 8 && !(alive(row, col + 1, common) && Grid(row, col + 1)[common]->Type() == ADOOM_SHROOM && Grid(row, col + 1)[common]->ExplodeCountdown() == 1)) {
                // 按照南瓜→常规→花盆的顺序依次判断得到右侧格可被巨人索敌的植物
                if (alive(row, col + 1, pumpkin)) {
                    right_index = Grid(row, col + 1)[pumpkin]->Index();
                    right_plant_map_type = pumpkin;
                } else if (alive(row, col + 1, common) && !isPlantDisappearedImmediately(Grid(row, col + 1)[common])) { // 植物自然消失
                    right_index = Grid(row, col + 1)[common]->Index();
                    right_plant_map_type = common;
                } else if (alive(row, col + 1, container)) {
                    right_index = Grid(row, col + 1)[container]->Index();
                    right_plant_map_type = container;
                }
            }
            if (right_index != -1) {
                // 判断右格植物下一帧会不会被巨人砸
                for (const auto& ptr : _HammerVec) {
                    gargantuar_amount += (IsDanger(Grid(row, col + 1)[right_plant_map_type], ptr) && ptr->Row() + 1 == row);
                    if (gargantuar_amount >= 2) {
                        right_index = -1; // 当有至少两个巨人下一帧砸的到右格植物，判定为右格植物消失
                        break;
                    }
                }
            }
            // 每帧先重置所有在上一帧高亮的植物的闪光
            if (_isSparkle) {
                sparkle = false;
                if (Plant.MRef<int>(0xB8) == 59) {
                    Plant.MRef<int>(0xB8) = 0;
                    UpdateReanimColor(index);
                }
            }
            // 本格南瓜且同格有花盆或常规
            if (Plant.Type() == APUMPKIN && (alive(row, col, container) || alive(row, col, common))) {
                for (const auto& ptr : _HammerVec) {
                    if (ptr->Row() + 1 == row && IsDanger((&Plant), ptr)) { // 本格南瓜在下一帧会被锤击
                        if (IsDanger(Grid(row, col)[container], ptr) && IsDanger(Grid(row, col)[common], ptr)) {
                            remove = false;
                            break; // 本格的花盆和常规都无法在下一帧活下来，以上为判断是否为栈位垫之前的“初次筛选”
                        } else
                            remove = true;
                    }
                }
                if (remove) {
                    if (right_index == -1) { // 如果右侧不存在植物，必定GG，铲南瓜
                        AAsm::RemovePlant(&Plant);
                        continue;
                    } else if (index < right_index) { // 若右侧存在植物，则仅当其为高栈时才铲南瓜
                        AAsm::RemovePlant(&Plant);
                        continue;
                    }
                }
                if (_isSparkle) {
                    for (const auto& ptr : _sparkleVec) {
                        if (ptr->Row() + 1 == row && IsDanger((&Plant), ptr)) { // 本格南瓜正在被举锤
                            if (IsDanger(Grid(row, col)[container], ptr) && IsDanger(Grid(row, col)[common], ptr)) {
                                sparkle = false;
                                break; // 脚本预估本格的花盆和常规都无法在落锤后活下来，以上为判断是否为栈位垫之前的“初次筛选”
                            } else
                                sparkle = true;
                        }
                    }
                    // 将僵尸啃食植物的闪光倒计时设为游戏不会自然产生的60。光效此时近似铲子预瞄植物，且覆盖僵尸啃食植物的闪光
                    if (sparkle) {
                        if (right_index == -1) { // 若右侧不存在植物，必定无法栈位垫，脚本预估南瓜是危险的
                            Plant.MRef<int>(0xB8) = 60;
                            UpdateReanimColor(index);
                        } else if (index < right_index) { // 若右侧存在植物，则仅当其为高栈时，脚本才预估南瓜是危险的
                            Plant.MRef<int>(0xB8) = 60;
                            UpdateReanimColor(index);
                        }
                    }
                }
                for (const auto& ptr : _crushVec) {
                    // 本格南瓜在下一帧会被碾压，且本格的花盆和常规至少有一个可以在下一帧活下来
                    if (IsDanger((&Plant), ptr) && ptr->Row() + 1 == row
                        && (!IsDanger(Grid(row, col)[container], ptr) || !IsDanger(Grid(row, col)[common], ptr))) {
                        AAsm::RemovePlant(&Plant);
                        break;
                    }
                }
                // 本格高坚果/偏右小喷菇/偏右阳光菇且同格有花盆
            } else if ((Plant.Type() == ATALL_NUT
                           || ((Plant.Type() == APUFF_SHROOM || Plant.Type() == ASUN_SHROOM) && 1 <= Plant.Abscissa() % 10 && Plant.Abscissa() % 10 <= 4))
                && alive(row, col, container)) {
                // 若本格有南瓜，获取南瓜的栈位
                int pumpkin_index = alive(row, col, pumpkin) ? Grid(row, col)[pumpkin]->Index() : -1;
                for (const auto& ptr : _HammerVec) {
                    if (ptr->Row() + 1 == row && IsDanger((&Plant), ptr)) { // 本格偏右植物在下一帧会被锤击
                        if (IsDanger(Grid(row, col)[container], ptr)) {
                            remove = false;
                            break; // 本格的花盆无法在下一帧活下来，以上为判断是否为栈位垫之前的“初次筛选”
                        } else
                            remove = true;
                    }
                }
                if (remove) {
                    if (right_index == -1) { // 如果右侧不存在植物，必定GG，铲偏右植物
                        AAsm::RemovePlant(&Plant);
                        continue;
                    } else {                           // 若右侧存在植物，
                        if (pumpkin_index == -1) {     // 本格无南瓜
                            if (index < right_index) { // 右格植物高栈，铲偏右植物
                                AAsm::RemovePlant(&Plant);
                                continue;
                            }
                            // 若本格有南瓜且右格植物同时相对于南瓜和偏右植物高栈，铲偏右植物
                        } else if (pumpkin_index < right_index && index < right_index) {
                            AAsm::RemovePlant(&Plant);
                            continue;
                        }
                    }
                }
                if (_isSparkle) {
                    for (const auto& ptr : _sparkleVec) {
                        if (ptr->Row() + 1 == row && IsDanger((&Plant), ptr)) { // 本格偏右植物正在被举锤
                            if (IsDanger(Grid(row, col)[container], ptr)) {
                                sparkle = false;
                                break; // 脚本预估本格的花盆无法在落锤后活下来，以上为判断是否为栈位垫之前的“初次筛选”
                            } else
                                sparkle = true;
                        }
                    }
                    if (sparkle) {
                        if (right_index == -1) { // 如果右侧不存在植物，必定GG，脚本预估偏右植物是危险的
                            Plant.MRef<int>(0xB8) = 60;
                            UpdateReanimColor(index);
                        } else {                       // 若右侧存在植物，
                            if (pumpkin_index == -1) { // 本格无南瓜
                                if (index < right_index) {
                                    Plant.MRef<int>(0xB8) = 60;
                                    UpdateReanimColor(index);
                                } // 右格植物高栈，脚本预估偏右植物是危险的
                            } else if (pumpkin_index < right_index && index < right_index) {
                                Plant.MRef<int>(0xB8) = 60;
                                UpdateReanimColor(index);
                            } // 若本格有南瓜且右格植物同时相对于南瓜和偏右植物高栈，脚本预估偏右植物是危险的
                        } // 将僵尸啃食植物的闪光倒计时设为游戏不会自然产生的60。光效此时近似铲子预瞄植物，且覆盖僵尸啃食植物的闪光
                    }
                }
                for (const auto& ptr : _crushVec) {
                    // 本格偏右植物在下一帧会被碾压，且本格的花盆可以在下一帧活下来
                    if (ptr->Row() + 1 == row && IsDanger((&Plant), ptr)
                        && !IsDanger(Grid(row, col)[container], ptr)) {
                        AAsm::RemovePlant(&Plant);
                        break;
                    }
                }
            }
        }
    }
};
inline SmartRemove smart_remove;

}

using A_TAS::smart_remove;

#endif //!__SMART_REMOVE_H__