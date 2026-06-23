#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// termbox2 单头文件库：恰好这一个 .c 编译其实现在
#define TB_IMPL
#include "tui.h"
#include "termbox2.h"

// ──────────────────────────────────────────────────────────────
// 全局单例
// ──────────────────────────────────────────────────────────────
TuiState g_tui;

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

    for (int i = 0; i < g_tui.appCount; i++) {
        int y = 2 + i;
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

    // 平台标签
    tb_printf(0, SY, TB_BLACK | TB_BOLD, TB_CYAN, "[%s]",
              g_tui.currentPlatform[0] ? g_tui.currentPlatform : "ALL");

    // 快捷键提示
    const char *hints;
    if (g_tui.focus == TUI_FOCUS_DIALOG)
        hints = "Y=Yes  N=No  ESC=Cancel";
    else
        hints = "\xe2\x86\x91\xe2\x86\x93 Navigate  Tab Switch  Space/Enter Check  a Add  d Delete  / Search  q/ESC Quit";

    tb_printf(10, SY, TB_BLACK, TB_WHITE, "%s", hints);
}

// ──────────────────────────────────────────────────────────────
// drawDeployMode — 一键装机全屏界面（预留）
// ──────────────────────────────────────────────────────────────

static void drawDeployMode(void) {
    int W = g_tui.screenW;
    int H = g_tui.screenH;
    uintattr_t bg = TB_DEFAULT;
    uintattr_t fg = TB_GREEN | TB_BOLD;

    // 清屏（用空格填满）
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
            putChar(x, y, ' ', TB_DEFAULT, TB_DEFAULT);

    // 居中矩形
    int boxW = 50;
    int boxH = 7;
    int bx = (W - boxW) / 2;
    int by = (H - boxH) / 2;

    // 边框
    putChar(bx,     by,     '+', fg, bg);
    putChar(bx+boxW-1, by,     '+', fg, bg);
    putChar(bx,     by+boxH-1, '+', fg, bg);
    putChar(bx+boxW-1, by+boxH-1, '+', fg, bg);
    drawHLine(bx+1, bx+boxW-1, by,        fg, bg);
    drawHLine(bx+1, bx+boxW-1, by+boxH-1, fg, bg);
    for (int y = by+1; y < by+boxH-1; y++) {
        putChar(bx,        y, '|', fg, bg);
        putChar(bx+boxW-1, y, '|', fg, bg);
    }

    // 中央文字
    int cx = bx + boxW / 2;
    int cy = by + boxH / 2;
    tb_printf(cx - 14, cy - 1, TB_CYAN | TB_BOLD, bg,
              "[Engine Reserved] Deploying...");
    tb_printf(cx - 8,  cy + 1, TB_DEFAULT, bg,
              "Press ESC to cancel.");
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
        } else if (g_tui.focus == TUI_FOCUS_APPS && g_tui.appCount > 0) {
            g_tui.selectedAppIndex++;
            if (g_tui.selectedAppIndex >= g_tui.appCount)
                g_tui.selectedAppIndex = 0;
        }
        break;

    case TB_KEY_ARROW_UP:
        if (g_tui.focus == TUI_FOCUS_CATEGORIES && g_tui.categoryCount > 0) {
            g_tui.selectedCategoryIndex--;
            if (g_tui.selectedCategoryIndex < 0)
                g_tui.selectedCategoryIndex = g_tui.categoryCount - 1;
        } else if (g_tui.focus == TUI_FOCUS_APPS && g_tui.appCount > 0) {
            g_tui.selectedAppIndex--;
            if (g_tui.selectedAppIndex < 0)
                g_tui.selectedAppIndex = g_tui.appCount - 1;
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

    default:
        if (ev->ch == 'q' || ev->ch == 'Q') {
            g_tui.view = TUI_VIEW_CONFIRM_QUIT;
            g_tui.focus = TUI_FOCUS_DIALOG;
            strcpy(g_tui.dialogTitle, "Exit");
            strcpy(g_tui.dialogMessage,
                   "Detected local config changes. Backup before exit?");
            g_tui.dialogResult = -1;
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
// runTuiLoop — 主循环
// ──────────────────────────────────────────────────────────────

int runTuiLoop(void) {
    if (tb_init() != 0) {
        fprintf(stderr, "tb_init failed\n");
        return 1;
    }

    tuiInitState();
    g_tui.screenW = tb_width();
    g_tui.screenH = tb_height();

    // ── 硬编码演示数据（3 个分类 + 3 个软件） ──
    // 分类
    g_tui.categoryCount = 3;
    g_tui.categories = malloc(sizeof(TuiCategory) * 3);
    g_tui.categories[0] = (TuiCategory){ .id = 1, .name = "Development Tools", .appCount = 2 };
    g_tui.categories[1] = (TuiCategory){ .id = 2, .name = "Communication",     .appCount = 0 };
    g_tui.categories[2] = (TuiCategory){ .id = 3, .name = "Utilities",         .appCount = 1 };
    g_tui.selectedCategoryIndex = 0;
    g_tui.selectedCategoryId    = 1;

    // 软件
    g_tui.appCount = 3;
    g_tui.apps = malloc(sizeof(TuiApp) * 3);
    g_tui.apps[0] = (TuiApp){
        .id = 101, .categoryId = 1, .name = "Visual Studio Code",
        .description = "Code editor", .priority = 1,
        .platformId = 1001, .isManual = 0, .configCount = 3,
        .isChecked = 1,
    };
    strcpy(g_tui.apps[0].platform, "macOS");
    strcpy(g_tui.apps[0].downloadUrl, "https://code.visualstudio.com/download");
    strcpy(g_tui.apps[0].installerPath, "");

    g_tui.apps[1] = (TuiApp){
        .id = 102, .categoryId = 1, .name = "Firefox",
        .description = "Web browser", .priority = 2,
        .platformId = 1002, .isManual = 1, .configCount = 1,
        .isChecked = 1,
    };
    strcpy(g_tui.apps[1].platform, "macOS");
    strcpy(g_tui.apps[1].downloadUrl, "https://www.mozilla.org/firefox/");
    strcpy(g_tui.apps[1].installerPath, "Firefox.dmg");

    g_tui.apps[2] = (TuiApp){
        .id = 103, .categoryId = 3, .name = "iTerm2",
        .description = "Terminal emulator", .priority = 3,
        .platformId = 1003, .isManual = 0, .configCount = 0,
        .isChecked = 0,
    };
    strcpy(g_tui.apps[2].platform, "macOS");
    strcpy(g_tui.apps[2].downloadUrl, "https://iterm2.com/downloads.html");
    strcpy(g_tui.apps[2].installerPath, "");

    g_tui.selectedAppIndex = 0;

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
