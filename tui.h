#ifndef TUI_H
#define TUI_H

#include <stddef.h>  // size_t

// sqlite3 前向声明 —— tui.h 作为纯 Model+Controller 接口不需要包含
// sqlite3.h 的完整定义（View 层禁止接触 SQLite 句柄）
struct sqlite3;

/*
 * tui.h — TUI 后端数据模型（Model）
 *
 * 三层架构中的最底层，定义前端（View）开箱即用的"拍平"结构体，
 * 以及前后端之间的"隔离带"—— TuiState 全局状态机。
 *
 * 核心约束：
 *   View（渲染层） 禁止 #include "sqlite/sqlite3.h"
 *   View（渲染层） 禁止读写 sqlite3 句柄
 *   View（渲染层） 只读 g_tui 缓存数据
 *   Controller 层负责：从 SQLite 查询 → 填入 g_tui → 触发重绘
 */

// ============================================================
// 前端展示模型（Flat Display Models）
//
// 后端（Controller）从 SQLite 查询、JOIN、聚合后填入这些结构体。
// 前端（View）只管读结构体画图，不需要知道什么是 sqlite3。
// ============================================================

// 分类展示模型
typedef struct {
    int  id;                      // categories.id
    char name[64];                // categories.name
    int  appCount;                // 该分类下（当前平台）的软件数量
} TuiCategory;

// 软件展示模型：apps + app_platforms 拍平
//
// 每个实例对应"某平台下的一个软件条目"。
// platform / downloadUrl / installerPath 来自 app_platforms，
// configCount 来自 app_configs 的 COUNT 聚合。
typedef struct {
    int  id;                      // apps.id
    int  categoryId;              // apps.category_id
    char name[256];               // apps.name
    char description[512];        // apps.description
    int  priority;                // apps.priority

    // ---- 来自 app_platforms（当前平台的那一条） ----
    int  platformId;              // app_platforms.id
    char platform[16];            // app_platforms.platform
    int  isManual;                // app_platforms.is_manual
    char downloadUrl[512];        // app_platforms.download_url
    char installerPath[512];      // app_platforms.installer_path

    // ---- 聚合 ----
    int  configCount;             // 该平台下配置/脚本数量

    // ---- 交互状态（Controller 填写，View 只读） ----
    int  isChecked;               // 用户勾选了此软件（用于一键装机）
} TuiApp;

// 配置/脚本明细模型
typedef struct {
    int  id;                      // app_configs.id
    char configPath[512];         // app_configs.config_path
    char scriptType[32];          // app_configs.script_type
    char scriptPath[512];         // app_configs.script_path
} TuiAppConfig;

// ============================================================
// TUI 全局状态机
//
// 这是前后端之间的"解耦隔离带"。
//   Controller 写 → g_tui 缓存 → View 读（只读）
// ============================================================

// 焦点区域 —— 键盘事件路由到哪个面板
typedef enum {
    TUI_FOCUS_CATEGORIES,   // 左侧分类列表
    TUI_FOCUS_APPS,         // 右侧软件列表
    TUI_FOCUS_CONFIGS,      // 底部/右侧配置明细
    TUI_FOCUS_DIALOG,       // 弹出对话框（阻断所有面板输入）
} TuiFocus;

// 视图模式
typedef enum {
    TUI_VIEW_BROWSE,        // 浏览模式：分类 → 软件 → 明细
    TUI_VIEW_SEARCH,        // 搜索输入模式（键盘输入搜索关键字）
    TUI_VIEW_DEPLOY,        // 部署执行中（显示进度）
    TUI_VIEW_INPUT,         // 文本输入弹窗（新增分类/软件）
    TUI_VIEW_CONFIRM,       // 操作确认弹窗（删除确认）
    TUI_VIEW_CONFIRM_QUIT,  // 退出确认
} TuiView;

// 全局状态缓存
typedef struct {
    // ======== 元状态（Controller 读写，View 只读） ========
    TuiFocus focus;                      // 当前焦点面板
    TuiView  view;                       // 当前视图模式

    int      selectedCategoryId;         // 选中的分类 DB ID（-1 = 全部）
    int      selectedCategoryIndex;      // categories[] 中选中的下标（-1 = 未选）
    int      selectedAppIndex;           // apps[] 中选中的下标（-1 = 未选）
    int      selectedConfigIndex;        // configs[] 中选中的下标（-1 = 未选）

    // ======== 数据缓存（Controller 从 SQLite 刷新） ========
    TuiCategory *categories;             // 分类列表（动态数组）
    int          categoryCount;

    TuiApp      *apps;                   // 当前分类/全部下的软件列表（动态数组）
    int          appCount;

    TuiAppConfig *configs;               // 当前选中软件的配置明细（动态数组）
    int           configCount;

    // ======== 平台过滤器 ========
    char         currentPlatform[16];    // "macOS" / "Windows" / "Linux" / ""（全部）

    // ======== 屏幕尺寸（Controller 在 Resize 事件中更新） ========
    int          screenW;
    int          screenH;

    // ======== 对话框状态 ========
    char         dialogTitle[64];
    char         dialogMessage[256];
    int          dialogResult;           // -1 = 未决, 0 = 取消, 1 = 确认

    // ======== 搜索过滤 ========
    char         searchQuery[32];        // 当前搜索关键字
    int          searchQueryLen;         // 关键字长度

    // ======== 一键装机引擎状态 ========
    char         deployLogs[24][128];    // 最近 24 行实时日志流水
    int          deployLogCount;         // 已写入日志总行数
    int          isDeploying;            // 引擎运行锁（1=执行中）

    // ======== 输入弹窗状态（TUI_VIEW_INPUT / TUI_VIEW_CONFIRM） ========
    char         inputBuffer[64];        // 弹窗文本输入缓冲区
    int          inputBufferLen;         // 当前输入长度
    int          contextId;              // 待删除/操作的分类或软件 ID
} TuiState;

// 全局单例（由 Controller 初始化，View 只读）
extern TuiState g_tui;

// ---- 生命周期 ----
void tuiInitState(void);
void tuiFreeState(void);

// ---- 渲染入口（纯 View，只读 g_tui） ----
void renderTuiScene(void);

// ---- 主循环（初始化 termbox → 加载数据 → 事件循环 → 清理） ----
int  runTuiLoop(struct sqlite3 *db);

#endif // TUI_H
