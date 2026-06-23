#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>

// termbox2 单头文件库：恰好这一个 .c 编译其实现在
#define TB_IMPL
#include "tui.h"
#include "termbox2.h"
#include "sqlite/sqlite3.h"
#include "deploy.h"
#include "dbAPI.h"

// ──────────────────────────────────────────────────────────────
// 全局单例
// ──────────────────────────────────────────────────────────────
TuiState g_tui;

// Controller 层持有的数据库句柄（用于分类切换时重新加载软件列表）
static struct sqlite3 *g_db = NULL;

void tuiInitState(void) {
    memset(&g_tui, 0, sizeof(g_tui));

    g_tui.focus = TUI_FOCUS_CATEGORIES;
    g_tui.view  = TUI_VIEW_BROWSE;

    g_tui.selectedCategoryId    = -1;
    g_tui.selectedCategoryIndex = -1;
    g_tui.selectedAppIndex      = -1;
    g_tui.selectedConfigIndex   = -1;
    g_tui.dialogResult          = -1;

#if defined(_WIN32) || defined(_WIN64)
    strncpy(g_tui.currentPlatform, "Windows", sizeof(g_tui.currentPlatform) - 1);
#elif defined(__APPLE__)
    strncpy(g_tui.currentPlatform, "macOS",   sizeof(g_tui.currentPlatform) - 1);
#elif defined(__linux__)
    strncpy(g_tui.currentPlatform, "Linux",   sizeof(g_tui.currentPlatform) - 1);
#else
    g_tui.currentPlatform[0] = '\0';
#endif
}

void tuiFreeState(void) {
    free(g_tui.categories);
    free(g_tui.apps);
    free(g_tui.configs);
    memset(&g_tui, 0, sizeof(g_tui));
}

// ──────────────────────────────────────────────────────────────
// 渲染辅助
// ──────────────────────────────────────────────────────────────

static uintattr_t borderFg(void) {
    return TB_GREEN | TB_BOLD;
}

static uintattr_t dividerFg(int active) {
    return active ? (TB_GREEN | TB_BOLD) : TB_DEFAULT;
}

// 在 (x,y) 画一个 UTF-8 字符
static void putChar(int x, int y, uint32_t ch, uintattr_t fg, uintattr_t bg) {
    tb_set_cell(x, y, ch, fg, bg);
}

// 画一行水平线段（从 x0 到 x1，不含终点）
static void drawHLine(int x0, int x1, int y, uintattr_t fg, uintattr_t bg) {
    for (int x = x0; x < x1; x++)
        putChar(x, y, '-', fg, bg);
}

// ──────────────────────────────────────────────────────────────
// 布局常量（每次 render 重新计算）
// ──────────────────────────────────────────────────────────────

#define LEFT_W_RATIO 30   // 左栏宽度百分比
#define RIGHT_TOP_RATIO 60 // 右栏上部高度百分比
#define LEFT_W_MIN 18

static int  L;           // 左栏内容宽度
static int  LX;          // 分隔符所在列
static int  RY;          // 右栏水平分隔线行号
static int  BY;          // 底部边框行号
static int  SY;          // 状态栏行号

static void recalcLayout(void) {
    int W = g_tui.screenW;
    int H = g_tui.screenH;
    L  = W * LEFT_W_RATIO / 100;
    if (L < LEFT_W_MIN) L = LEFT_W_MIN;
    if (L > W - 12)     L = W - 12;
    LX = L;                          // 分隔符列
    BY = H - 2;                      // 底部边框
    SY = H - 1;                      // 状态栏
    RY = L + (BY - L) * RIGHT_TOP_RATIO / 100;
    if (RY < L + 4)  RY = L + 4;
    if (RY > BY - 4) RY = BY - 4;
}

// ──────────────────────────────────────────────────────────────
// drawTuiLayout — 三段式响应式分栏边框
// ──────────────────────────────────────────────────────────────

static void drawTuiLayout(void) {
    int W = g_tui.screenW;
    int H = g_tui.screenH;
    uintattr_t fg = borderFg();
    uintattr_t bg = TB_DEFAULT;

    // 判断分隔符活跃度
    int vertActive  = (g_tui.focus == TUI_FOCUS_CATEGORIES ||
                       g_tui.focus == TUI_FOCUS_APPS);
    int horizActive = (g_tui.focus == TUI_FOCUS_APPS ||
                       g_tui.focus == TUI_FOCUS_CONFIGS);

    uintattr_t vfg = dividerFg(vertActive);
    uintattr_t hfg = dividerFg(horizActive);

    // ---- 第 0 行：上边框 ----
    putChar(0,     0, '+', fg, bg);   // ┌
    drawHLine(1, LX, 0, fg, bg);
    putChar(LX,    0, '+', fg, bg);   // ┬
    drawHLine(LX+1, W-1, 0, fg, bg);
    putChar(W-1,   0, '+', fg, bg);   // ┐

    // ---- 内容区左右竖线 ----
    for (int y = 1; y < BY; y++) {
        putChar(0,   y, '|', fg, bg); // 左外
        if (y != RY)
            putChar(LX, y, '|', vfg, bg);  // 中分
        putChar(W-1, y, '|', fg, bg); // 右外
    }

    // ---- 水平分隔线 ----
    putChar(0,  RY, '+', fg, bg);     // ├（左外）
    drawHLine(1, LX, RY, fg, bg);
    putChar(LX, RY, '+', hfg, bg);    // ┼（中分）
    drawHLine(LX+1, W-1, RY, hfg, bg);
    putChar(W-1, RY, '+', hfg, bg);   // ┤（右外）

    // ---- 第 BY 行：下边框 ----
    putChar(0,   BY, '+', fg, bg);    // └
    drawHLine(1, LX, BY, fg, bg);
    putChar(LX,  BY, '+', fg, bg);    // ┴
    drawHLine(LX+1, W-1, BY, fg, bg);
    putChar(W-1, BY, '+', fg, bg);    // ┘

    // ---- 面板标题 ----
    uintattr_t titleFg = TB_CYAN | TB_BOLD;
    tb_print(2,                 0, titleFg, bg, "Categories");
    tb_print(LX + 2,            0, titleFg, bg, "Apps");
    tb_print(LX + 2,          RY, titleFg, bg, "Config / Script");
}

// ──────────────────────────────────────────────────────────────
// drawCategoryPanel — 左侧分类列表
// ──────────────────────────────────────────────────────────────

static void drawCategoryPanel(void) {
    uintattr_t bg = TB_DEFAULT;
    int focused = (g_tui.focus == TUI_FOCUS_CATEGORIES);

    for (int i = 0; i < g_tui.categoryCount; i++) {
        int y = 2 + i;
        if (y >= RY) break;

        int selected = (i == g_tui.selectedCategoryIndex);
        uintattr_t fg, nameBg;
        if (selected && focused) {
            fg     = TB_BLACK | TB_BOLD;
            nameBg = TB_GREEN;
        } else if (selected) {
            fg     = TB_WHITE | TB_REVERSE;
            nameBg = TB_DEFAULT;
        } else {
            fg     = TB_WHITE;
            nameBg = TB_DEFAULT;
        }

        tb_printf(2, y, fg, nameBg, "%s", g_tui.categories[i].name);

        uintattr_t cntFg = (selected && focused) ? (TB_BLACK | TB_BOLD) : TB_GREEN;
        uintattr_t cntBg = (selected && focused) ? TB_GREEN : TB_DEFAULT;
        tb_printf(L - 4, y, cntFg, cntBg, "%d", g_tui.categories[i].appCount);
    }
}

// ──────────────────────────────────────────────────────────────
// drawAppPanel — 右侧软件列表
// ──────────────────────────────────────────────────────────────

static void drawAppPanel(void) {
    uintattr_t bg = TB_DEFAULT;
    int x0 = LX + 2;
    int focused = (g_tui.focus == TUI_FOCUS_APPS);
    int drawIdx = 0;

    for (int i = 0; i < g_tui.appCount; i++) {
        // 搜索过滤：不匹配的跳过不画
        if (g_tui.searchQueryLen > 0 &&
            strcasestr(g_tui.apps[i].name, g_tui.searchQuery) == NULL)
            continue;

        int y = 2 + drawIdx;
        if (y >= RY) break;

        int selected = (i == g_tui.selectedAppIndex);
        uintattr_t fg, nameBg;
        if (selected && focused) {
            fg     = TB_BLACK | TB_BOLD;
            nameBg = TB_GREEN;
        } else if (selected) {
            fg     = TB_WHITE | TB_REVERSE;
            nameBg = TB_DEFAULT;
        } else {
            fg     = TB_WHITE;
            nameBg = TB_DEFAULT;
        }

        // 勾选框
        uintattr_t ckFg;
        if (g_tui.apps[i].isChecked)
            ckFg = (selected && focused) ? (TB_BLACK | TB_BOLD) : (TB_GREEN | TB_BOLD);
        else
            ckFg = TB_DEFAULT;
        tb_printf(x0, y, ckFg, nameBg, "%s", g_tui.apps[i].isChecked ? "[X]" : "[ ]");

        // 软件名
        tb_printf(x0 + 4, y, fg, nameBg, "%s", g_tui.apps[i].name);

        drawIdx++;
    }
}

// ──────────────────────────────────────────────────────────────
// drawConfigPanel — 下方配置/脚本明细
// ──────────────────────────────────────────────────────────────

static void drawConfigPanel(void) {
    uintattr_t bg = TB_DEFAULT;
    int x0 = LX + 2;
    int y0 = RY + 2;

    if (g_tui.selectedAppIndex < 0 || g_tui.selectedAppIndex >= g_tui.appCount) {
        tb_print(x0, y0, TB_DEFAULT, bg, "Select an app to view details");
        return;
    }

    TuiApp *app = &g_tui.apps[g_tui.selectedAppIndex];
    tb_printf(x0, y0,   TB_CYAN, bg, "App: %s", app->name);
    tb_printf(x0, y0+1, TB_DEFAULT, bg, "Platform: %s", app->platform);

    if (app->configCount > 0) {
        tb_printf(x0, y0+2, TB_DEFAULT, bg, "Configs: %d item(s)", app->configCount);
    } else {
        tb_print(x0, y0+2, TB_DEFAULT, bg, "No config / script records.");
    }
}

// ──────────────────────────────────────────────────────────────
// drawStatusBar — 底部状态栏
// ──────────────────────────────────────────────────────────────

static void drawStatusBar(void) {
    int W = g_tui.screenW;
    uintattr_t bg = TB_BLACK;
    uintattr_t fg = TB_WHITE | TB_BOLD;

    // 整行反白背景
    for (int x = 0; x < W; x++)
        putChar(x, SY, ' ', TB_DEFAULT, TB_WHITE);

    if (g_tui.view == TUI_VIEW_SEARCH) {
        // 搜索输入栏
        char searchBar[64];
        int n = snprintf(searchBar, sizeof(searchBar), "Search: %s_",
                         g_tui.searchQuery);
        tb_printf(0, SY, TB_BLACK | TB_BOLD, TB_CYAN, "%s", searchBar);
        // 剩余行用空格覆盖
        if (n < W)
            drawHLine(n, W, SY, TB_DEFAULT, TB_WHITE);
        return;
    }

    // 平台标签
    tb_printf(0, SY, TB_BLACK | TB_BOLD, TB_CYAN, "[%s]",
              g_tui.currentPlatform[0] ? g_tui.currentPlatform : "ALL");

    // 快捷键提示
    const char *hints;
    if (g_tui.focus == TUI_FOCUS_DIALOG)
        hints = "Y=Yes  N=No  ESC=Cancel";
    else
        hints = "\xe2\x86\x91\xe2\x86\x93 Navigate  Tab Switch  Space/Enter Check  Ctrl+X Deploy  / Search  q/ESC Quit";

    tb_printf(10, SY, TB_BLACK, TB_WHITE, "%s", hints);
}

// ──────────────────────────────────────────────────────────────
// drawDeployMode — 一键装机全屏日志看板
// ──────────────────────────────────────────────────────────────

static void drawDeployMode(void) {
    int W = g_tui.screenW;
    int H = g_tui.screenH;
    uintattr_t bg = TB_DEFAULT;

    // 清屏
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
            putChar(x, y, ' ', TB_DEFAULT, bg);

    // 外框区域
    int padX = 2, padY = 1;
    int bx = padX, by = padY;
    int bw = W - 2 * padX;
    int bh = H - 2 * padY;
    if (bw < 40) { bx = 0; bw = W; }
    if (bh < 10) { by = 0; bh = H; }

    uintattr_t boxFg = TB_GREEN | TB_BOLD;

    // 边框
    putChar(bx, by, '+', boxFg, bg);
    drawHLine(bx + 1, bx + bw - 1, by, boxFg, bg);
    putChar(bx + bw - 1, by, '+', boxFg, bg);
    putChar(bx, by + bh - 1, '+', boxFg, bg);
    drawHLine(bx + 1, bx + bw - 1, by + bh - 1, boxFg, bg);
    putChar(bx + bw - 1, by + bh - 1, '+', boxFg, bg);
    for (int y = by + 1; y < by + bh - 1; y++) {
        putChar(bx, y, '|', boxFg, bg);
        putChar(bx + bw - 1, y, '|', boxFg, bg);
    }

    // 标题
    const char *title = "[=== \xf0\x9f\x92\xbe Sisyphus \xe8\x87\xaa\xe5\x8a\xa8\xe5\x8c\x96\xe8\xa3\x85\xe6\x9c\xba\xe4\xb8\x8e\xe9\x85\x8d\xe7\xbd\xae\xe8\xbf\x98\xe5\x8e\x9f\xe5\xbc\x95\xe6\x93\x8e ===]";
    int titleLen = strlen(title);
    int titleX = bx + (bw - titleLen) / 2;
    if (titleX < bx + 1) titleX = bx + 1;
    tb_printf(titleX, by + 1, TB_CYAN | TB_BOLD, bg, "%s", title);

    // 标题下分隔线
    int sepY = by + 2;
    putChar(bx, sepY, '|', boxFg, bg);
    for (int x = bx + 1; x < bx + bw - 1; x++)
        putChar(x, sepY, '-', TB_GREEN, bg);
    putChar(bx + bw - 1, sepY, '|', boxFg, bg);

    // 日志内容区
    int logY0 = sepY + 1;
    int logY1 = by + bh - 2;
    int maxLines = logY1 - logY0;
    if (maxLines < 0) maxLines = 0;

    // 环形缓冲区 → 按时间顺序提取最后 N 行
    int total = g_tui.deployLogCount;
    int start = (total > 24) ? (total % 24) : 0;
    int show  = (total < 24) ? total : 24;
    if (show > maxLines) show = maxLines;

    for (int i = 0; i < show; i++) {
        int idx = (start + i) % 24;
        int y = logY0 + i;
        const char *line = g_tui.deployLogs[idx];

        uintattr_t color = TB_WHITE;
        if      (strncmp(line, "[START]", 7)  == 0) color = TB_CYAN;
        else if (strncmp(line, "[DONE]",  6)  == 0) color = TB_GREEN | TB_BOLD;
        else if (strncmp(line, "[FAIL]",  6)  == 0) color = TB_RED | TB_BOLD;
        else if (strncmp(line, "[FINISH]",8)  == 0) color = TB_YELLOW | TB_BOLD;
        else if (strncmp(line, "[WARN]",  6)  == 0) color = TB_MAGENTA;

        tb_printf(bx + 2, y, color, bg, "%s", line);
    }

    // 底部提示
    if (!g_tui.isDeploying && total > 0) {
        tb_printf(bx + 2, by + bh - 2, TB_GREEN | TB_BOLD, bg,
                  "--- Press any key to return ---");
    }
}

// ──────────────────────────────────────────────────────────────
// drawDialogOverlay — 退出确认对话框
// ──────────────────────────────────────────────────────────────

static void drawDialogOverlay(void) {
    int W = g_tui.screenW;
    int H = g_tui.screenH;
    uintattr_t bg = TB_DEFAULT;

    // 半透明遮罩（用空格覆盖）
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
            putChar(x, y, ' ', TB_DEFAULT, TB_DEFAULT);

    // 对话框尺寸
    int dw = 56;
    int dh = 7;
    int dx = (W - dw) / 2;
    int dy = (H - dh) / 2;

    uintattr_t fg = TB_YELLOW | TB_BOLD;

    putChar(dx,     dy,      '+', fg, bg);
    putChar(dx+dw-1, dy,      '+', fg, bg);
    putChar(dx,     dy+dh-1, '+', fg, bg);
    putChar(dx+dw-1, dy+dh-1, '+', fg, bg);
    drawHLine(dx+1, dx+dw-1, dy,        fg, bg);
    drawHLine(dx+1, dx+dw-1, dy+dh-1,   fg, bg);
    for (int y = dy+1; y < dy+dh-1; y++) {
        putChar(dx,        y, '|', fg, bg);
        putChar(dx+dw-1, y, '|', fg, bg);
    }

    // 标题
    tb_printf(dx + 2, dy + 1, TB_CYAN | TB_BOLD, bg, "%s", g_tui.dialogTitle);

    // 消息（自动折行暂不处理，单行显示）
    tb_printf(dx + 2, dy + 3, TB_WHITE, bg, "%s", g_tui.dialogMessage);

    // 按钮提示
    tb_printf(dx + 2, dy + 5, TB_GREEN | TB_BOLD, bg, "[Y] Yes");
    tb_printf(dx + 18, dy + 5, TB_RED | TB_BOLD, bg, "[N] No");
    tb_printf(dx + 34, dy + 5, TB_DEFAULT, bg, "[ESC] Cancel");
}

// ──────────────────────────────────────────────────────────────
// drawInputOverlay — 文本输入弹窗（新增分类/软件）
// ──────────────────────────────────────────────────────────────

static void drawInputOverlay(void) {
    int W = g_tui.screenW;
    int H = g_tui.screenH;
    uintattr_t bg = TB_DEFAULT;

    // 遮罩
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
            putChar(x, y, ' ', TB_DEFAULT, bg);

    uintattr_t fg = TB_CYAN | TB_BOLD;
    int dw = 52, dh = 8;
    int dx = (W - dw) / 2, dy = (H - dh) / 2;
    if (dx < 0) { dx = 0; dw = W; }
    if (dy < 0) { dy = 0; dh = H; }

    // 外框
    putChar(dx, dy, '+', fg, bg);
    drawHLine(dx + 1, dx + dw - 1, dy, fg, bg);
    putChar(dx + dw - 1, dy, '+', fg, bg);
    putChar(dx, dy + dh - 1, '+', fg, bg);
    drawHLine(dx + 1, dx + dw - 1, dy + dh - 1, fg, bg);
    putChar(dx + dw - 1, dy + dh - 1, '+', fg, bg);
    for (int y = dy + 1; y < dy + dh - 1; y++) {
        putChar(dx, y, '|', fg, bg);
        putChar(dx + dw - 1, y, '|', fg, bg);
    }

    // 标题
    const char *prompt = (g_tui.focus == TUI_FOCUS_APPS)
        ? "Add Software" : "Add Category";
    tb_printf(dx + 2, dy + 1, TB_YELLOW | TB_BOLD, bg, "%s", prompt);

    // 输入域
    char inputLine[80];
    snprintf(inputLine, sizeof(inputLine), "> %s_", g_tui.inputBuffer);
    tb_printf(dx + 2, dy + 3, TB_WHITE | TB_BOLD, bg, "%s", inputLine);

    // 提示
    tb_printf(dx + 2, dy + 5, TB_GREEN | TB_BOLD, bg, "[Enter] confirm");
    tb_printf(dx + 20, dy + 5, TB_DEFAULT, bg, "[ESC] cancel");
}

// ──────────────────────────────────────────────────────────────
// drawConfirmOverlay — 操作确认弹窗（删除确认）
// ──────────────────────────────────────────────────────────────

static void drawConfirmOverlay(void) {
    int W = g_tui.screenW;
    int H = g_tui.screenH;
    uintattr_t bg = TB_DEFAULT;

    // 遮罩
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
            putChar(x, y, ' ', TB_DEFAULT, bg);

    uintattr_t fg = TB_YELLOW | TB_BOLD;
    int dw = 54, dh = 8;
    int dx = (W - dw) / 2, dy = (H - dh) / 2;
    if (dx < 0) { dx = 0; dw = W; }
    if (dy < 0) { dy = 0; dh = H; }

    // 外框
    putChar(dx, dy, '+', fg, bg);
    drawHLine(dx + 1, dx + dw - 1, dy, fg, bg);
    putChar(dx + dw - 1, dy, '+', fg, bg);
    putChar(dx, dy + dh - 1, '+', fg, bg);
    drawHLine(dx + 1, dx + dw - 1, dy + dh - 1, fg, bg);
    putChar(dx + dw - 1, dy + dh - 1, '+', fg, bg);
    for (int y = dy + 1; y < dy + dh - 1; y++) {
        putChar(dx, y, '|', fg, bg);
        putChar(dx + dw - 1, y, '|', fg, bg);
    }

    // 标题
    const char *title = (g_tui.focus == TUI_FOCUS_APPS)
        ? "Delete Software" : "Delete Category";
    tb_printf(dx + 2, dy + 1, TB_RED | TB_BOLD, bg, "%s", title);

    // 消息
    const char *target = "";
    if (g_tui.focus == TUI_FOCUS_CATEGORIES && g_tui.selectedCategoryIndex >= 0)
        target = g_tui.categories[g_tui.selectedCategoryIndex].name;
    else if (g_tui.focus == TUI_FOCUS_APPS && g_tui.selectedAppIndex >= 0)
        target = g_tui.apps[g_tui.selectedAppIndex].name;

    char msg[64];
    snprintf(msg, sizeof(msg), "Delete \"%s\"?", target);
    tb_printf(dx + 2, dy + 3, TB_WHITE, bg, "%s", msg);

    // 选项
    tb_printf(dx + 2,  dy + 5, TB_GREEN | TB_BOLD, bg, "[Y] Yes");
    tb_printf(dx + 14, dy + 5, TB_RED | TB_BOLD,   bg, "[N] No");
    tb_printf(dx + 26, dy + 5, TB_DEFAULT,          bg, "[ESC] Cancel");
}

// ──────────────────────────────────────────────────────────────
// renderTuiScene — 主渲染入口
// ──────────────────────────────────────────────────────────────

void renderTuiScene(void) {
    g_tui.screenW = tb_width();
    g_tui.screenH = tb_height();

    if (g_tui.screenW < 1 || g_tui.screenH < 1)
        return;

    recalcLayout();

    if (g_tui.view == TUI_VIEW_DEPLOY) {
        drawDeployMode();
        tb_present();
        return;
    }

    if (g_tui.view == TUI_VIEW_INPUT) {
        drawInputOverlay();
        tb_present();
        return;
    }

    if (g_tui.view == TUI_VIEW_CONFIRM) {
        drawConfirmOverlay();
        tb_present();
        return;
    }

    if (g_tui.focus == TUI_FOCUS_DIALOG) {
        drawDialogOverlay();
        tb_present();
        return;
    }

    // ---- 正常浏览模式 ----
    // 用空格清背景
    for (int y = 0; y < g_tui.screenH; y++)
        for (int x = 0; x < g_tui.screenW; x++)
            putChar(x, y, ' ', TB_DEFAULT, TB_DEFAULT);

    drawTuiLayout();
    drawCategoryPanel();
    drawAppPanel();
    drawConfigPanel();
    drawStatusBar();

    tb_present();
}

// ──────────────────────────────────────────────────────────────
// 事件处理
// ──────────────────────────────────────────────────────────────

// 搜索过滤下将选中校正到第一个可见行
static void tuiSnapAppToVisible(void) {
    if (g_tui.appCount == 0) { g_tui.selectedAppIndex = -1; return; }
    if (g_tui.searchQueryLen == 0) return;
    if (g_tui.selectedAppIndex >= 0 &&
        strcasestr(g_tui.apps[g_tui.selectedAppIndex].name,
                   g_tui.searchQuery) != NULL)
        return;
    for (int i = 0; i < g_tui.appCount; i++) {
        if (strcasestr(g_tui.apps[i].name, g_tui.searchQuery) != NULL) {
            g_tui.selectedAppIndex = i;
            return;
        }
    }
    g_tui.selectedAppIndex = -1;
}

// ↑↓ 在过滤模式下跳过不可见条目
static int tuiDeltaAppIndex(int delta) {
    if (g_tui.appCount == 0) return -1;
    int cur = g_tui.selectedAppIndex;
    if (cur < 0) cur = 0;
    int n = g_tui.appCount;
    for (int step = 0; step < n; step++) {
        cur = (cur + delta + n) % n;
        if (g_tui.searchQueryLen == 0 ||
            strcasestr(g_tui.apps[cur].name, g_tui.searchQuery) != NULL)
            return cur;
    }
    return g_tui.selectedAppIndex;
}

static void tuiReloadApps(void);
static int  tuiRefreshData(struct sqlite3 *db);
static void deployLogPush(const char *line);
static void runDeployEngine(struct sqlite3 *db);

static void handleKeyEvent(struct tb_event *ev) {
    if (g_tui.focus == TUI_FOCUS_DIALOG) {
        // 对话框：Y / Enter 确认，N / ESC 取消
        if (ev->key == TB_KEY_ESC || ev->ch == 'n' || ev->ch == 'N') {
            g_tui.dialogResult = 0;  // 取消
            g_tui.focus = TUI_FOCUS_CATEGORIES;
            g_tui.view  = TUI_VIEW_BROWSE;
        } else if (ev->ch == 'y' || ev->ch == 'Y' || ev->key == TB_KEY_ENTER) {
            g_tui.dialogResult = 1;  // 确认
        }
        return;
    }

    if (g_tui.view == TUI_VIEW_SEARCH) {
        if (ev->key == TB_KEY_ESC) {
            g_tui.searchQuery[0] = '\0';
            g_tui.searchQueryLen = 0;
            g_tui.view = TUI_VIEW_BROWSE;
        } else if (ev->key == TB_KEY_ENTER) {
            g_tui.view = TUI_VIEW_BROWSE;
            g_tui.focus = TUI_FOCUS_APPS;
            tuiSnapAppToVisible();
        } else if (ev->key == TB_KEY_BACKSPACE ||
                   ev->key == TB_KEY_BACKSPACE2) {
            if (g_tui.searchQueryLen > 0) {
                g_tui.searchQuery[--g_tui.searchQueryLen] = '\0';
                tuiSnapAppToVisible();
            }
        } else if (ev->ch >= 32 && ev->ch <= 126) {
            if (g_tui.searchQueryLen < (int)sizeof(g_tui.searchQuery) - 1) {
                g_tui.searchQuery[g_tui.searchQueryLen++] = (char)ev->ch;
                g_tui.searchQuery[g_tui.searchQueryLen] = '\0';
                tuiSnapAppToVisible();
            }
        }
        return;
    }

    // ── 部署视图（DEPLOY） ──
    if (g_tui.view == TUI_VIEW_DEPLOY) {
        if (g_tui.isDeploying) return;  // 引擎执行中，拦截所有输入
        // 部署完毕，任意键返回浏览
        g_tui.view = TUI_VIEW_BROWSE;
        g_tui.focus = TUI_FOCUS_CATEGORIES;
        return;
    }

    // ── 输入弹窗（TUI_VIEW_INPUT） ──
    if (g_tui.view == TUI_VIEW_INPUT) {
        if (ev->key == TB_KEY_ESC) {
            // 取消
            g_tui.inputBuffer[0] = '\0';
            g_tui.inputBufferLen = 0;
            g_tui.view = TUI_VIEW_BROWSE;
        } else if (ev->key == TB_KEY_ENTER) {
            // 确认输入
            if (g_tui.inputBufferLen > 0) {
                if (g_tui.focus == TUI_FOCUS_CATEGORIES)
                    addCategory(g_db, g_tui.inputBuffer);
                else if (g_tui.focus == TUI_FOCUS_APPS && g_tui.selectedCategoryId > 0)
                    addApp(g_db, g_tui.inputBuffer, g_tui.selectedCategoryId, 0, "");
            }
            g_tui.inputBuffer[0] = '\0';
            g_tui.inputBufferLen = 0;
            tuiRefreshData(g_db);
            g_tui.view = TUI_VIEW_BROWSE;
        } else if (ev->key == TB_KEY_BACKSPACE ||
                   ev->key == TB_KEY_BACKSPACE2) {
            if (g_tui.inputBufferLen > 0)
                g_tui.inputBuffer[--g_tui.inputBufferLen] = '\0';
        } else if (ev->ch >= 32 && ev->ch <= 126) {
            if (g_tui.inputBufferLen < (int)sizeof(g_tui.inputBuffer) - 1) {
                g_tui.inputBuffer[g_tui.inputBufferLen++] = (char)ev->ch;
                g_tui.inputBuffer[g_tui.inputBufferLen] = '\0';
            }
        }
        return;
    }

    // ── 确认弹窗（TUI_VIEW_CONFIRM） ──
    if (g_tui.view == TUI_VIEW_CONFIRM) {
        if (ev->ch == 'y' || ev->ch == 'Y' || ev->key == TB_KEY_ENTER) {
            if (g_tui.focus == TUI_FOCUS_CATEGORIES)
                removeCategory(g_db, g_tui.contextId);
            else if (g_tui.focus == TUI_FOCUS_APPS)
                removeApp(g_db, g_tui.contextId);
            g_tui.contextId = 0;
            tuiRefreshData(g_db);
            g_tui.view = TUI_VIEW_BROWSE;
        } else if (ev->ch == 'n' || ev->ch == 'N' || ev->key == TB_KEY_ESC) {
            g_tui.contextId = 0;
            g_tui.view = TUI_VIEW_BROWSE;
        }
        return;
    }

    // ── 浏览模式（BROWSE） ──
    switch (ev->key) {
    case TB_KEY_TAB:
        // 切换焦点：Categories → Apps → Configs → Categories
        if (g_tui.focus == TUI_FOCUS_CATEGORIES)
            g_tui.focus = TUI_FOCUS_APPS;
        else if (g_tui.focus == TUI_FOCUS_APPS)
            g_tui.focus = TUI_FOCUS_CONFIGS;
        else
            g_tui.focus = TUI_FOCUS_CATEGORIES;
        break;

    case TB_KEY_ARROW_DOWN:
        if (g_tui.focus == TUI_FOCUS_CATEGORIES && g_tui.categoryCount > 0) {
            g_tui.selectedCategoryIndex++;
            if (g_tui.selectedCategoryIndex >= g_tui.categoryCount)
                g_tui.selectedCategoryIndex = 0;
            g_tui.selectedCategoryId =
                g_tui.categories[g_tui.selectedCategoryIndex].id;
            tuiReloadApps();
        } else if (g_tui.focus == TUI_FOCUS_APPS && g_tui.appCount > 0) {
            g_tui.selectedAppIndex = tuiDeltaAppIndex(1);
        }
        break;

    case TB_KEY_ARROW_UP:
        if (g_tui.focus == TUI_FOCUS_CATEGORIES && g_tui.categoryCount > 0) {
            g_tui.selectedCategoryIndex--;
            if (g_tui.selectedCategoryIndex < 0)
                g_tui.selectedCategoryIndex = g_tui.categoryCount - 1;
            g_tui.selectedCategoryId =
                g_tui.categories[g_tui.selectedCategoryIndex].id;
            tuiReloadApps();
        } else if (g_tui.focus == TUI_FOCUS_APPS && g_tui.appCount > 0) {
            g_tui.selectedAppIndex = tuiDeltaAppIndex(-1);
        }
        break;

    case TB_KEY_ESC:
        g_tui.view = TUI_VIEW_CONFIRM_QUIT;
        g_tui.focus = TUI_FOCUS_DIALOG;
        strcpy(g_tui.dialogTitle, "Exit");
        strcpy(g_tui.dialogMessage,
               "Detected local config changes. Backup before exit?");
        g_tui.dialogResult = -1;
        break;

    case TB_KEY_CTRL_X:
        if (g_tui.focus != TUI_FOCUS_DIALOG) {
            g_tui.view = TUI_VIEW_DEPLOY;
            runDeployEngine(g_db);
        }
        break;

    default:
        if (ev->ch == 'q' || ev->ch == 'Q') {
            g_tui.view = TUI_VIEW_CONFIRM_QUIT;
            g_tui.focus = TUI_FOCUS_DIALOG;
            strcpy(g_tui.dialogTitle, "Exit");
            strcpy(g_tui.dialogMessage,
                   "Detected local config changes. Backup before exit?");
            g_tui.dialogResult = -1;
        } else if (ev->ch == 'a' || ev->ch == 'A') {
            // 新增：根据焦点进入输入弹窗
            if (g_tui.focus == TUI_FOCUS_CATEGORIES) {
                g_tui.inputBuffer[0] = '\0';
                g_tui.inputBufferLen = 0;
                g_tui.view = TUI_VIEW_INPUT;
            } else if (g_tui.focus == TUI_FOCUS_APPS) {
                if (g_tui.selectedCategoryId > 0) {
                    g_tui.inputBuffer[0] = '\0';
                    g_tui.inputBufferLen = 0;
                    g_tui.view = TUI_VIEW_INPUT;
                }
            }
        } else if (ev->ch == 'd' || ev->ch == 'D') {
            // 删除：根据焦点进入确认弹窗
            if (g_tui.focus == TUI_FOCUS_CATEGORIES &&
                g_tui.selectedCategoryIndex >= 0) {
                g_tui.contextId = g_tui.categories[g_tui.selectedCategoryIndex].id;
                g_tui.view = TUI_VIEW_CONFIRM;
            } else if (g_tui.focus == TUI_FOCUS_APPS &&
                       g_tui.selectedAppIndex >= 0) {
                g_tui.contextId = g_tui.apps[g_tui.selectedAppIndex].id;
                g_tui.view = TUI_VIEW_CONFIRM;
            }
        } else if (ev->ch == '/') {
            g_tui.view = TUI_VIEW_SEARCH;
            g_tui.searchQuery[0] = '\0';
            g_tui.searchQueryLen = 0;
        }
        break;
    }

    if (g_tui.focus == TUI_FOCUS_APPS &&
        g_tui.selectedAppIndex >= 0 && g_tui.selectedAppIndex < g_tui.appCount)
    {
        if (ev->ch == ' ' || ev->key == TB_KEY_ENTER)
            g_tui.apps[g_tui.selectedAppIndex].isChecked ^= 1;
    }
}

// ──────────────────────────────────────────────────────────────
// tuiLoadCategories — 从 SQLite 读取分类及当前平台的软件计数
// ──────────────────────────────────────────────────────────────

static int tuiLoadCategories(struct sqlite3 *db) {
    const char *sql =
        "SELECT c.id, c.name,"
        "  (SELECT COUNT(*) FROM apps a"
        "   JOIN app_platforms ap ON ap.app_id = a.id"
        "   WHERE a.category_id = c.id AND ap.platform = ?1)"
        "FROM categories c ORDER BY c.id;";

    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK)
        return 0;

    sqlite3_bind_text(stmt, 1, g_tui.currentPlatform, -1, SQLITE_STATIC);

    // 先数行数
    int count = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) count++;
    sqlite3_reset(stmt);
    // 重新 bind（reset 会清 bindings）
    sqlite3_bind_text(stmt, 1, g_tui.currentPlatform, -1, SQLITE_STATIC);

    if (count == 0) { sqlite3_finalize(stmt); return 1; }

    g_tui.categories = malloc(sizeof(TuiCategory) * count);
    if (!g_tui.categories) { sqlite3_finalize(stmt); return 0; }

    int i = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW && i < count) {
        g_tui.categories[i].id       = sqlite3_column_int(stmt, 0);
        snprintf(g_tui.categories[i].name, sizeof(g_tui.categories[i].name),
                 "%s", (const char *)sqlite3_column_text(stmt, 1));
        g_tui.categories[i].appCount = sqlite3_column_int(stmt, 2);
        i++;
    }
    g_tui.categoryCount = i;

    sqlite3_finalize(stmt);
    return 1;
}

// ──────────────────────────────────────────────────────────────
// tuiLoadApps — 从 SQLite 读取当前平台全部软件
// ──────────────────────────────────────────────────────────────

static int tuiLoadApps(struct sqlite3 *db, int categoryId) {
    const char *sql =
        "SELECT a.id, a.name, a.category_id, a.priority, a.description,"
        "       ap.id, ap.platform, ap.is_manual,"
        "       COALESCE(ap.download_url, ''), COALESCE(ap.installer_path, ''),"
        "       (SELECT COUNT(*) FROM app_configs ac"
        "         WHERE ac.app_platform_id = ap.id)"
        "FROM apps a"
        " JOIN app_platforms ap ON ap.app_id = a.id"
        " WHERE ap.platform = ?1"
        "   AND (?2 = -1 OR a.category_id = ?2)"
        " ORDER BY a.priority ASC;";

    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK)
        return 0;

    sqlite3_bind_text(stmt, 1, g_tui.currentPlatform, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, categoryId);

    // 数行数
    int count = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) count++;
    sqlite3_reset(stmt);
    sqlite3_bind_text(stmt, 1, g_tui.currentPlatform, -1, SQLITE_STATIC);

    if (count == 0) { sqlite3_finalize(stmt); return 1; }

    g_tui.apps = malloc(sizeof(TuiApp) * count);
    if (!g_tui.apps) { sqlite3_finalize(stmt); return 0; }

    int i = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW && i < count) {
        TuiApp *app = &g_tui.apps[i];

        app->id           = sqlite3_column_int(stmt, 0);
        app->categoryId   = sqlite3_column_int(stmt, 2);
        app->priority     = sqlite3_column_int(stmt, 3);
        app->platformId   = sqlite3_column_int(stmt, 5);
        app->isManual     = sqlite3_column_int(stmt, 7);
        app->configCount  = sqlite3_column_int(stmt, 10);
        app->isChecked    = 0;

        snprintf(app->name,         sizeof(app->name),         "%s",
                 (const char *)sqlite3_column_text(stmt, 1));
        {
            const unsigned char *d = sqlite3_column_text(stmt, 4);
            snprintf(app->description, sizeof(app->description), "%s",
                     d ? (const char *)d : "");
        }
        snprintf(app->platform,     sizeof(app->platform),     "%s",
                 (const char *)sqlite3_column_text(stmt, 6));
        snprintf(app->downloadUrl,  sizeof(app->downloadUrl),  "%s",
                 (const char *)sqlite3_column_text(stmt, 8));
        snprintf(app->installerPath, sizeof(app->installerPath), "%s",
                 (const char *)sqlite3_column_text(stmt, 9));

        i++;
    }
    g_tui.appCount = i;

    sqlite3_finalize(stmt);
    return 1;
}

// ──────────────────────────────────────────────────────────────
// tuiReloadApps — 按当前分类重新加载软件列表
// ──────────────────────────────────────────────────────────────

static void tuiReloadApps(void) {
    free(g_tui.apps); g_tui.apps = NULL; g_tui.appCount = 0;
    if (g_db) {
        int catId = (g_tui.selectedCategoryIndex >= 0)
                      ? g_tui.categories[g_tui.selectedCategoryIndex].id : -1;
        tuiLoadApps(g_db, catId);
    }
    g_tui.selectedAppIndex = (g_tui.appCount > 0) ? 0 : -1;
    g_tui.selectedConfigIndex = -1;
}

// ──────────────────────────────────────────────────────────────
// tuiRefreshData — 从 SQLite 读取完整数据替换 g_tui 缓存
// ──────────────────────────────────────────────────────────────

static int tuiRefreshData(struct sqlite3 *db) {
    // 释放旧缓存（若 tuiInitState 已清零指针，free(NULL) 安全）
    free(g_tui.categories); g_tui.categories = NULL; g_tui.categoryCount = 0;
    free(g_tui.apps);       g_tui.apps = NULL;       g_tui.appCount = 0;
    free(g_tui.configs);    g_tui.configs = NULL;    g_tui.configCount = 0;

    if (!tuiLoadCategories(db)) return 0;

    // 设置初始选中
    g_tui.selectedCategoryIndex = (g_tui.categoryCount > 0) ? 0 : -1;
    g_tui.selectedCategoryId    = (g_tui.categoryCount > 0)
                                    ? g_tui.categories[0].id : -1;

    // 加载当前分类下的软件
    tuiReloadApps();

    return 1;
}

// ──────────────────────────────────────────────────────────────
// 一键装机引擎（Controller）
// ──────────────────────────────────────────────────────────────

// 向环形日志缓冲区追加一行
static void deployLogPush(const char *line) {
    int idx = g_tui.deployLogCount % 24;
    strncpy(g_tui.deployLogs[idx], line, sizeof(g_tui.deployLogs[idx]) - 1);
    g_tui.deployLogs[idx][sizeof(g_tui.deployLogs[idx]) - 1] = '\0';
    g_tui.deployLogCount++;
}

// 阻塞式部署：遍历所有勾选软件，逐条执行 debployAppConfig
static void runDeployEngine(sqlite3 *db) {
    g_tui.isDeploying = 1;
    memset(g_tui.deployLogs, 0, sizeof(g_tui.deployLogs));
    g_tui.deployLogCount = 0;
    // 数一下勾选了多少个
    int checked = 0;
    for (int i = 0; i < g_tui.appCount; i++)
        if (g_tui.apps[i].isChecked) checked++;

    if (checked == 0) {
        deployLogPush("[WARN] No apps checked — nothing to deploy.");
        g_tui.isDeploying = 0;
        deployLogPush("[FINISH] Press any key to return.");
        renderTuiScene();
        return;
    }

    char line[128];
    for (int i = 0; i < g_tui.appCount; i++) {
        if (!g_tui.apps[i].isChecked) continue;

        snprintf(line, sizeof(line), "[START] %s...", g_tui.apps[i].name);
        deployLogPush(line);
        renderTuiScene();

        int ok = deployAppConfig(db, g_tui.apps[i].platformId);

        snprintf(line, sizeof(line), ok
            ? "[DONE] %s deployed successfully!"
            : "[FAIL] %s deployment failed — check logs above.",
            g_tui.apps[i].name);
        deployLogPush(line);
        renderTuiScene();
    }

    deployLogPush("[FINISH] All done. Press any key to return.");
    g_tui.isDeploying = 0;
    renderTuiScene();
}

// ──────────────────────────────────────────────────────────────
// runTuiLoop — 主循环
// ──────────────────────────────────────────────────────────────

int runTuiLoop(struct sqlite3 *db) {
    if (tb_init() != 0) {
        fprintf(stderr, "tb_init failed\n");
        return 1;
    }

    tuiInitState();
    g_db = db;
    g_tui.screenW = tb_width();
    g_tui.screenH = tb_height();

    if (!tuiRefreshData(db)) {
        fprintf(stderr, "tuiRefreshData failed — no data loaded\n");
    }

    // ── 主事件循环 ──
    int running = 1;
    struct tb_event ev;

    while (running) {
        renderTuiScene();

        if (tb_poll_event(&ev) != 0)
            continue;

        if (ev.type == TB_EVENT_KEY) {
            handleKeyEvent(&ev);

            // 对话框确认退出
            if (g_tui.dialogResult == 1 && g_tui.view == TUI_VIEW_CONFIRM_QUIT)
                running = 0;
        }
        // RESIZE 事件：无需额外处理，下次 render 自动适应
    }

    tuiFreeState();
    tb_shutdown();
    return 0;
}
