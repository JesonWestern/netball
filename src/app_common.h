// src/app_common.h —— 契约冻结版 v1.2（数据层/渲染层共用，改动需同步 docs）
#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>
#include <cstddef>

#define NETBALL_VERSION_STR   L"1.4.4"
#define NETBALL_APP_NAME      L"NetBall 网速悬浮球"
#define WND_CLASS_BALL        L"NetBallBallWnd"
#define WND_CLASS_SETTINGS    L"NetBallSettingsWnd"
#define WND_CLASS_TASKBAR     L"NetBallTaskbarWnd"
#define WND_CLASS_GLASS       L"NetBallGlassWnd"
#define NETBALL_MUTEX_NAME    L"NetBallSingleInstance"
#define NETBALL_RUN_KEY       L"NetBall"

#define IDI_APP               101
#define IDR_MANIFEST          1
#define TIMER_SAMPLE          1
#define TIMER_TASKBAR         2
#define TIMER_GLASS           3
#define TIMER_LIQUID          4

#define IDM_SETTINGS          1001
#define IDM_TOPMOST           1002
#define IDM_AUTOSTART         1003
#define IDM_RESETPOS          1004
#define IDM_ABOUT             1005
#define IDM_EXIT              1006
#define IDM_TASKBAR           1007
#define IDM_TRAY_SHOW         1010
#define IDM_TRAY_EXIT         1011

// ---------------------------------------------------------------- 排版/指标上限
#define NETBALL_MAX_GPU       4                     // 最多同时展示 4 张 GPU
// v1.4.2：5 基础项 + 1 CPU 功率 + 4 卡 × 3 项（负载/温度/功率）= 18
#define NETBALL_MAX_ITEMS     18                    // 5 基础 + 1 + 4 卡 × 3 项
#define NETBALL_MAX_TB_ROWS   2                     // 任务栏最多两行
#define NETBALL_SPEC_CCH      160                   // 排版 spec 缓冲区（wchar 数）

struct AppConfig {
    int      shape;          // 0=圆(椭圆) 1=圆角方 2=胶囊
    int      size;           // 【v1.1 旧字段，仅用于迁移】48..160
    int      opacity;        // 20..100 (%)  —— 背景/材质的整体不透明度
    COLORREF bgColor;        // 背景色 RGB()
    BOOL     textAuto;       // TRUE=按对比度自动选黑/白
    COLORREF textColor;      // 手动文字色（textAuto=FALSE 时生效）
    BOOL     showNet;        // 【v1.1 旧字段，仅用于迁移】显示网速
    BOOL     showMem;        // 【v1.1 旧字段，仅用于迁移】显示内存
    int      refreshMs;      // 采样周期 500..3000
    BOOL     alwaysOnTop;
    BOOL     autoStart;
    int      posX, posY;     // 虚拟屏幕坐标
    int      savedScreenW, savedScreenH;

    // ---- v1.1.0：任务栏显示 ----
    BOOL     showBall;       // 显示悬浮球
    BOOL     taskbarShow;    // 在任务栏显示
    BOOL     tbShowDown;     // 【仅用于迁移】任务栏显示下行速度
    BOOL     tbShowUp;       // 【仅用于迁移】任务栏显示上行速度
    BOOL     tbShowMem;      // 【仅用于迁移】任务栏显示内存占用

    // ---- v1.2.0：共用排版模型 ----
    // spec 语法：'/' 分行、',' 分项目，例如 L"down,up/cpuload/gpu0temp"
    //   token：down up mem cpuload cputemp gpu0load gpu0temp … gpu3load gpu3temp
    wchar_t  tbLayout[NETBALL_SPEC_CCH];
    wchar_t  ballLayout[NETBALL_SPEC_CCH];

    // ---- v1.2.0：字体排版参数（0 = 自适应）----
    int      tbFontPx;       // 0 或 10..48（DIP）
    int      tbLinePct;      // 100..220（行高 = 字号 × pct / 100）
    int      tbItemGap;      // 0..48（DIP，同一行内项目间距）
    int      ballFontPx;     // 0 或 8..48（DIP）
    int      ballLinePct;    // 100..220
    int      ballItemGap;    // 0..48

    // ---- v1.2.0：悬浮球独立宽高（DIP；下限由排版引擎抬升）----
    int      ballW;
    int      ballH;

    // ---- v1.2.0：材质 ----
    // 0=纯色 1=亚克力 2=玻璃 3=液态玻璃 4=无底板（仅文字 + 描边）
    int      tbMat;
    int      ballMat;
    COLORREF tbTint;
    int      tbTintAlpha;    // 0..100（材质浓度：越高颜色越浓、遮挡越强）
    COLORREF ballTint;
    int      ballTintAlpha;
    // 【v1.2.0 旧字段，仅用于迁移/降级】v1.4.0 起改为分表面字段
    int      matBlur;
    int      matCorner;
    BOOL     matBorder;
    BOOL     matShadow;

    // ---- v1.4.0：分表面外观（任务栏 / 悬浮球各自独立）----
    int      tbShape;        // 1=圆角方 2=胶囊
    int      ballShape;      // 0=圆 1=圆角方 2=胶囊
    int      tbBlur;         // 0..40（DIP，材质模糊强度）
    int      ballBlur;
    int      tbCorner;       // 0..40（DIP，圆角）
    int      ballCorner;
    BOOL     tbBorder;       // 1px 高光描边
    BOOL     ballBorder;
    BOOL     tbSheen;        // 顶部高光 + 内阴影
    BOOL     ballSheen;
    BOOL     tbTextAuto;     // 按材质对比度自动选黑/白
    BOOL     ballTextAuto;
    COLORREF tbTextColor;    // tbTextAuto=FALSE 时生效
    COLORREF ballTextColor;
    BOOL     tbSysAcrylic;   // 仅亚克力有效：使用系统 DWM 实时模糊（实验性）
    BOOL     ballSysAcrylic;

    // ---- v1.2.0：温度数据源 ----
    // 0=自动 1=仅系统接口 2=仅外部 LibreHardwareMonitor 3=关闭
    int      tempSrc;
};

struct Metrics {
    double    upBps;
    double    downBps;
    int       memPercent;
    ULONGLONG memUsedBytes;
    ULONGLONG memTotalBytes;
    bool      netValid;

    // ---- v1.2.0：CPU / GPU（-1 / FALSE 表示本周期无效）----
    int       cpuPercent;                            // -1 无效
    int       cpuTempC;
    BOOL      cpuTempValid;
    int       gpuCount;                              // 0..NETBALL_MAX_GPU
    int       gpuLoad[NETBALL_MAX_GPU];              // -1 无效
    int       gpuTempC[NETBALL_MAX_GPU];
    BOOL      gpuTempValid[NETBALL_MAX_GPU];
    wchar_t   gpuName[NETBALL_MAX_GPU][64];          // 适配器名（可为空）

    // ---- v1.4.2：功率（毫瓦；-1 / FALSE 表示本周期无效）----
    // 统一用 mW 整数：跨线程快照不涉及浮点比较误差，渲染时再换算成 W。
    int       cpuPwrMw;                              // -1 无效；CPU 封装功率（APU 上含核显）
    BOOL      cpuPwrValid;
    int       gpuPwrMw[NETBALL_MAX_GPU];             // -1 无效
    BOOL      gpuPwrValid[NETBALL_MAX_GPU];
};

namespace cfg {
    void         resetToDefault(AppConfig* c);
    std::wstring configPath();
    void         load(AppConfig* c);
    void         save(const AppConfig* c);
    bool         getAutoStart();
    void         setAutoStart(bool on);
}

namespace metrics {
    void init();
    void sample(Metrics* m);
    void shutdown();
}

namespace render {
    COLORREF autoTextColor(COLORREF bg, int opacityPercent);
    double   contrastRatio(COLORREF a, COLORREF b);
    void     formatSpeed(double bps, wchar_t* out, size_t cch);
    void     drawBall(HDC memDc, int w, int h, const AppConfig* c, const Metrics* m);
}

namespace settings_ui {
    typedef void (*ApplyFn)(const AppConfig* c, void* user);
    // 预览回调：设置窗口里任意控件变化时调用（16ms 合并），宿主只做轻量重绘，
    // 不写盘、不重启采样定时器、不重新采样。
    typedef void (*PreviewFn)(const AppConfig* c, void* user);
    void open(HWND owner, HINSTANCE hInst, const AppConfig* cur,
              PreviewFn onPreview, ApplyFn onApply, void* user);
    bool isOpen();
}

namespace tray {
    bool add(HWND ball, HINSTANCE hInst);
    void remove();
    void updateTip(const Metrics* m);
    // v1.4.3：宿主可用多行文本直接覆盖悬停提示（全部指标），仍受 shell 的 128 字符上限约束。
    void updateTipText(const wchar_t* text);
    // 刷新菜单勾选态（ball/taskbar 当前是否显示），由 main.cpp 在状态变化后调用
    void setMenuState(bool showBall, bool taskbarShow);
}

namespace ballwin {
    typedef void (*CommandFn)(int cmdId);
    bool create(HINSTANCE hInst, const AppConfig* c, CommandFn onCommand);
    void destroy();
    void applyConfig(const AppConfig* c);
    void setMetrics(const Metrics* m);
    void refreshMaterial();   // v1.4.1：500ms 实时背景（不隐藏窗口）
    void animateLiquid();     // v1.4.3：液态玻璃流光逐帧推进（与采样解耦）
    HWND hwnd();
}

namespace taskbar {
    // 任务栏透明文字覆盖窗（v1.1.0，v1.2.0 起带材质底板与双行排版）。
    // 生命周期与主消息循环同属 main.cpp。
    bool create(HINSTANCE hInst, const AppConfig* c);
    void destroy();
    void applyConfig(const AppConfig* c);
    void setMetrics(const Metrics* m);
    void refreshMaterial();   // v1.4.1：500ms 实时背景（不隐藏窗口）
    void animateLiquid();     // v1.4.3：液态玻璃流光逐帧推进（与采样解耦）
    void sync();       // 重新定位 / 可见性 / 主题同步，由 UI 线程定时器驱动
    HWND hwnd();
}
