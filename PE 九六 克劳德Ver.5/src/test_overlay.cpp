// 合并测试脚本：一个 ATickRunner 里同时画两部分
//   1) 每个格子里竖排显示「在这里放这种即时输出植物」的价值：
//        从上到下 = 樱桃炸弹(红) / 火爆辣椒(橙) / 毁灭菇(黑) / 倭瓜(白)
//      价值 = 对「威胁值」的削减能力：Σ min(1800, 威胁血量) × 距离系数 × 种类修正 × 速度系数
//      （威胁血量已扣掉即将到来的灰烬/炮弹伤害和自然输出，所以被锁死的僵尸不计价值；
//        若这颗灰烬在生效前会被小丑炸掉，价值直接记 0）
//   2) 红眼巨人(type 32) 与 橄榄球僵尸(type 7) 身上两行：
//        黄色 = 威胁值，红色 = 威胁血量
//
// 关于绘制（之前合并后「重影 + 卡死」就是这里踩的坑）：
//   PvZ 的 aPainter 只有一份字体状态和一份字形纹理缓存，字号一变就 ClearFont()
//   （删字体 + 清空字形纹理）。两部分字号不同（13 / 15），共用一个 painter 的话
//   字形纹理每帧反复重建；更糟的是 duration > 1 的文本会排队到下一帧再画一次，
//   而那时字号已经变成另一部分的字号，于是同一串数字被按两种字号各画一遍
//   （视觉上就是重影），同时每帧几百上千次字体/纹理创建（每次都要 CreateSurface）
//   把渲染拖死（表现为卡死）。
//   所以这里：两部分各用一个 APainter（字体与纹理缓存互不干扰），
//   并且只用 duration = 1 —— PAINT 模式下立即画一次、且不进队列。
//
// 参数（C / k、自然输出口径等）都在各自模块里，这里不改任何口径。
#include "natural_output.h"
#include <dsl/shorthand.h>
#include <avz.h>
#include <map>
#include <string>

namespace {
ATickRunner Overlay;

// 两部分可以单独开关，方便测试
constexpr bool kShowAshValue = true;
constexpr bool kShowZombieStats = true;
// 打开后每 100 帧在日志里打印一次本显示的耗时（排查卡顿用）
constexpr bool kLogPerf = false;

// 两部分字号不同，必须各用一个 painter，否则字形纹理缓存会互相踩
APainter ashPainter;
APainter statsPainter;

// ---------------- 灰烬价值（每格四行） ----------------
constexpr int ASH_DAMAGE = 1800;
constexpr int CELL_FONT_SIZE = 13;
constexpr int CELL_LINE_HEIGHT = 18;
constexpr int CELL_TEXT_WIDTH = 4;  // 粗估字宽，用于在格子里居中

struct ValueLine {
    APlantType type;
    DWORD color;
};

constexpr ValueLine VALUE_LINES[] = {
    {ACHERRY_BOMB, AArgb(0xff, 0xff, 0x00, 0x00)},  // 樱桃炸弹：红
    {AJALAPENO, AArgb(0xff, 0xff, 0xa5, 0x00)},      // 火爆辣椒：橙
    {ADOOM_SHROOM, AArgb(0xff, 0x00, 0x00, 0x00)},   // 毁灭菇：黑
    {ASQUASH, AArgb(0xff, 0xff, 0xff, 0xff)},        // 倭瓜：白
};

// 本帧所有僵尸的威胁度明细（威胁血量 + 三个系数），避免每个格子重复算
std::map<AZombie*, AZombieThreatLevel> threatCache;

std::string FormatCellValue(double value) {
    if (!(value > 0.0))
        return "0";
    if (value >= 10.0)
        return std::to_string(static_cast<int>(value + 0.5));
    const int scaled = static_cast<int>(value * 10.0 + 0.5);  // 保留一位小数
    return std::to_string(scaled / 10) + "." + std::to_string(scaled % 10);
}

// 在 (row, col) 放 type 这颗植物能削减多少威胁值
double AshValueAt(int row, int col, APlantType type) {
    const auto hits = APredictInstantPlantHits(row, col, type);
    if (hits.empty())
        return 0.0;

    // 生效前被小丑炸掉 → 根本不会爆炸，价值为 0
    if (aThreat::AIsPlantJacked(aThreat::APlantHitBoxAt(row, col), hits.front().eta))
        return 0.0;

    double total = 0.0;
    for (const auto& hit : hits) {
        const auto it = threatCache.find(hit.zombie);
        if (it == threatCache.end())
            continue;
        const AZombieThreatLevel& info = it->second;
        if (info.threatHp <= 0)
            continue;  // 已经被别的东西锁死，价值为 0
        const int dealt = std::min(ASH_DAMAGE, info.threatHp);
        total += static_cast<double>(dealt) * info.distanceFactor * info.typeFactor * info.speedFactor;
    }
    return total;
}

void DrawAshValue() {
    if (aFieldInfo.nRows <= 0)  // 关卡切换那几帧没有场地信息
        return;

    ashPainter.SetFontSize(CELL_FONT_SIZE);

    // 四条文字整体在格子内垂直居中
    const int blockHeight = CELL_FONT_SIZE + CELL_LINE_HEIGHT * (std::size(VALUE_LINES) - 1);

    for (int row = 1; row <= aFieldInfo.nRows; ++row) {
        for (int col = 1; col <= 9; ++col) {
            const int cellX = AAsm::GridToAbscissa(row - 1, col - 1);
            const int cellY = AAsm::GridToOrdinate(row - 1, col - 1);
            int y = cellY + (aFieldInfo.rowHeight - blockHeight) / 2 + CELL_FONT_SIZE;

            for (const auto& line : VALUE_LINES) {
                const std::string text = FormatCellValue(AshValueAt(row, col, line.type));
                // RIGHT_TOP 表示 (x, y) 是文本左下角
                const int x = cellX + 40 - static_cast<int>(text.size()) * CELL_TEXT_WIDTH;
                ashPainter.SetTextColor(line.color);
                // duration = 1：PAINT 模式下立即画一次，不进队列（不进队列才不会重影）
                ashPainter.Draw(AText(text, x, y, APos::RIGHT_TOP, false), 1);
                y += CELL_LINE_HEIGHT;
            }
        }
    }
}

// ---------------- 红眼 / 橄榄 的威胁血量与威胁值 ----------------
constexpr int ZOMBIE_FONT_SIZE = 15;
constexpr int ZOMBIE_LINE_GAP = 17;
constexpr int ZOMBIE_TEXT_WIDTH = 5;  // 粗估字宽，用于在僵尸身上居中

constexpr DWORD kHpColor = AArgb(0xff, 0xff, 0x00, 0x00);     // 威胁血量：红
constexpr DWORD kLevelColor = AArgb(0xff, 0xff, 0xff, 0x00);  // 威胁值：黄

// 威胁值保留一位小数
std::string FormatLevelText(double value) {
    const int scaled = static_cast<int>(value * 10.0 + 0.5);
    return std::to_string(scaled / 10) + "." + std::to_string(scaled % 10);
}

void DrawZombieStats() {
    statsPainter.SetFontSize(ZOMBIE_FONT_SIZE);

    for (auto& zombie : aAliveZombieFilter) {
        const int type = zombie.Type();
        if (type != AGIGA_GARGANTUAR && type != AFOOTBALL_ZOMBIE)
            continue;

        // 优先复用本帧缓存，保证两处显示的数字完全一致
        const auto cached = threatCache.find(&zombie);
        const AZombieThreatLevel info = cached != threatCache.end() ? cached->second : AGetZombieThreatLevel(&zombie);
        const aInstantPlant::AHitBox box = aInstantPlant::AGetZombieHitBox(&zombie);
        const int centerX = box.x + box.width / 2;
        const int top = box.y;

        const std::string hpText = std::to_string(info.threatHp);
        const std::string levelText = FormatLevelText(info.value);

        // 两行都贴在僵尸判定框上边（也就是头顶位置），按字数粗略居中
        statsPainter.SetTextColor(kLevelColor);
        statsPainter.Draw(AText(levelText,
                                centerX - static_cast<int>(levelText.size()) * ZOMBIE_TEXT_WIDTH / 2 - 2,
                                top - ZOMBIE_LINE_GAP, APos::RIGHT_TOP, false),
                          1);

        statsPainter.SetTextColor(kHpColor);
        statsPainter.Draw(AText(hpText,
                                centerX - static_cast<int>(hpText.size()) * ZOMBIE_TEXT_WIDTH / 2 - 2, top,
                                APos::RIGHT_TOP, false),
                          1);
    }
}

// ---------------- 每帧总入口 ----------------
double perfSeconds = 0.0;
int perfFrames = 0;

void FlushPerf(double cost) {
    perfSeconds += cost;
    if (++perfFrames < 100)
        return;
    aLogger->Info("test_overlay: 100 帧平均 {} 微秒", static_cast<int>(perfSeconds / perfFrames * 1e6));
    perfSeconds = 0.0;
    perfFrames = 0;
}

void DrawTestOverlay() {
    // 关卡切换 / 退出战斗的那几帧场上对象会被清空，先挡一下
    if (AGetMainObject() == nullptr)
        return;

    const double begin = kLogPerf ? __AProfiler::CurrentTime() : 0.0;

    if (kShowAshValue) {
        // 先刷新威胁度缓存（含威胁血量、距离系数、种类修正、速度系数）
        AThreatLevelOptions levelOptions;  // 需要时在这里调，例如 levelOptions.distanceInTiles = true;
        threatCache.clear();
        for (auto& zombie : aAliveZombieFilter)
            threatCache.emplace(&zombie, AGetZombieThreatLevel(&zombie, levelOptions));
        DrawAshValue();
    }
    if (kShowZombieStats)
        DrawZombieStats();

    if (kLogPerf)
        FlushPerf(__AProfiler::CurrentTime() - begin);
}
}  // namespace

void AScript() {
    ASetGameSpeed(1);
    for (int row = 1; row <= 6; ++row)
        aThreatLevelRowParams[row] = {1.0, 1.0 / 80.0};
    Overlay.Start(DrawTestOverlay, ATickRunner::PAINT);
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
