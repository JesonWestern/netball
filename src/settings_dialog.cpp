// src/settings_dialog.cpp —— 设置窗口（v1.4.0 三页版）
//
// 页签：任务栏 / 悬浮球 / 数据。
//   · 任务栏、悬浮球两页各自拥有完整的形状、颜色、浓度、文字色、材质、模糊、
//     圆角、描边、高光、系统亚克力开关；互不影响。
//   · 数据页只放刷新频率、温度数据源、GPU 列表与重新探测。
//
// 维护须知（沿用 v1.3.1 的坑）：
//   1. 尺寸/字体全部走 DpiOf()；WM_DPICHANGED 必须重建字体 → Relayout → RefreshAll。
//   2. 客户区尺寸用 GetClientRect 差值校正，AdjustWindowRectEx 只按 96dpi 估边框。
//   3. WM_ERASEBKGND 返回 1 + WM_PAINT 双缓冲，避免拖动/换色闪烁。
//   4. 控件一次性创建，Reg(hwnd,page,getRect) 登记；getRect 是 std::function，
//      按“当前 Geo”返回像素矩形，所以窗口缩放/换 DPI 不需要重建控件。
//   5. BS_OWNERDRAW 按钮不参与系统主题绘制，状态变化后手动 InvalidateRect。
//   6. 列表是 LBS_OWNERDRAWFIXED，配色走 WM_DRAWITEM；不要改回默认绘制。
//   7. 排版真实数据是 g.surf[].spec；保存时才 format() 回 cfg.*Layout 字符串。
//   8. State 含 std::function，禁止 ZeroMemory(&g)，一律 g = State()。
#include "settings_dialog.h"
#include "glass.h"
#include "layout_model.h"
#include "render.h"
#include "sensors.h"
#include "surface_shape.h"
#include "text_render.h"

#include <commctrl.h>
#include <commdlg.h>
#include <dwmapi.h>
#include <windowsx.h>
#include <objidl.h>
#include <gdiplus.h>

#include <cstdio>
#include <cwchar>
#include <functional>
#include <string>

#ifndef WM_DPICHANGED
#define WM_DPICHANGED 0x02E0
#endif

using namespace Gdiplus;

namespace {

// ----------------------------------------------------------------- 配色
const COLORREF kDlgBg    = RGB(32, 34, 40);
const COLORREF kPanelBg  = RGB(43, 46, 55);
const COLORREF kLine     = RGB(56, 60, 70);
const COLORREF kText     = RGB(230, 233, 240);
const COLORREF kTextDim  = RGB(145, 150, 163);
const COLORREF kAccent   = RGB(58, 126, 235);
const COLORREF kAccentPr = RGB(40, 100, 200);
const COLORREF kBtnFace  = RGB(52, 56, 66);
const COLORREF kBtnFaceP = RGB(40, 44, 52);
const COLORREF kSwatchBd = RGB(70, 74, 84);

// ----------------------------------------------------------------- 页 / 控件 ID
enum {
    PAGE_TB   = 0,
    PAGE_BALL = 1,
    PAGE_DATA = 2
};

enum {
    IDC_TAB0 = 3001, IDC_TAB1, IDC_TAB2,

    // ---- 任务栏页 3100..3199 ----
    IDC_TB_SHOW = 3100,
    IDC_TB_LIST, IDC_TB_ADD, IDC_TB_DEL, IDC_TB_UP, IDC_TB_DOWN, IDC_TB_ROWMINUS, IDC_TB_ROWPLUS,
    IDC_TB_FONT_TB, IDC_TB_FONT_VAL,
    IDC_TB_LINE_TB, IDC_TB_LINE_VAL,
    IDC_TB_GAP_TB, IDC_TB_GAP_VAL,
    IDC_TB_SHAPE0, IDC_TB_SHAPE1,
    IDC_TB_COLOR, IDC_TB_CONC_TB, IDC_TB_CONC_VAL,
    IDC_TB_MAT0, IDC_TB_MAT1, IDC_TB_MAT2, IDC_TB_MAT3, IDC_TB_MAT4,
    IDC_TB_BLUR_TB, IDC_TB_BLUR_VAL,
    IDC_TB_CORNER_TB, IDC_TB_CORNER_VAL,
    IDC_TB_AUTOTEXT, IDC_TB_TEXTCOLOR,
    IDC_TB_BORDER, IDC_TB_SHEEN,
    IDC_TB_SYSACRYLIC,

    // ---- 悬浮球页 3200..3299 ----
    IDC_BALL_SHOW = 3200,
    IDC_BALL_LIST, IDC_BALL_ADD, IDC_BALL_DEL, IDC_BALL_UP, IDC_BALL_DOWN, IDC_BALL_ROWMINUS, IDC_BALL_ROWPLUS,
    IDC_BALL_FONT_TB, IDC_BALL_FONT_VAL,
    IDC_BALL_LINE_TB, IDC_BALL_LINE_VAL,
    IDC_BALL_GAP_TB, IDC_BALL_GAP_VAL,
    IDC_BALL_W_TB, IDC_BALL_W_VAL, IDC_BALL_H_TB, IDC_BALL_H_VAL,
    IDC_BALL_SHAPE0, IDC_BALL_SHAPE1, IDC_BALL_SHAPE2,
    IDC_BALL_COLOR, IDC_BALL_CONC_TB, IDC_BALL_CONC_VAL,
    IDC_BALL_MAT0, IDC_BALL_MAT1, IDC_BALL_MAT2, IDC_BALL_MAT3, IDC_BALL_MAT4,
    IDC_BALL_BLUR_TB, IDC_BALL_BLUR_VAL,
    IDC_BALL_CORNER_TB, IDC_BALL_CORNER_VAL,
    IDC_BALL_AUTOTEXT, IDC_BALL_TEXTCOLOR,
    IDC_BALL_BORDER, IDC_BALL_SHEEN,
    IDC_BALL_SYSACRYLIC,

    // ---- 数据页 3300..3399 ----
    IDC_REFRESH = 3300,
    IDC_TEMPSRC0, IDC_TEMPSRC1, IDC_TEMPSRC2, IDC_TEMPSRC3, IDC_TEMPSRC4,
    IDC_GPUINFO, IDC_RESCAN,

    // ---- 标签 3400..3499 ----
    IDC_LBL_TB_LAYOUT = 3400,
    IDC_LBL_TB_FONT, IDC_LBL_TB_LINE, IDC_LBL_TB_GAP,
    IDC_LBL_TB_SHAPE, IDC_LBL_TB_COLOR, IDC_LBL_TB_CONC, IDC_LBL_TB_MAT,
    IDC_LBL_TB_BLUR, IDC_LBL_TB_CORNER,
    IDC_LBL_BALL_LAYOUT,
    IDC_LBL_BALL_FONT, IDC_LBL_BALL_LINE, IDC_LBL_BALL_GAP,
    IDC_LBL_BALL_W, IDC_LBL_BALL_H, IDC_LBL_BALL_SHAPE,
    IDC_LBL_BALL_COLOR, IDC_LBL_BALL_CONC, IDC_LBL_BALL_MAT,
    IDC_LBL_BALL_BLUR, IDC_LBL_BALL_CORNER,
    IDC_LBL_REFRESH, IDC_LBL_TEMPSRC,

    IDC_RESET, IDC_CANCEL, IDC_SAVE
};

const int kRefreshSteps[4] = { 500, 1000, 2000, 3000 };

const COLORREF kPresets[8] = {
    RGB(38, 42, 52), RGB(0, 0, 0), RGB(255, 255, 255), RGB(20, 75, 200),
    RGB(34, 160, 90), RGB(235, 140, 30), RGB(110, 50, 190), RGB(180, 25, 35)
};

const wchar_t* kMatNames[5] = { L"纯色", L"亚克力", L"玻璃", L"液态玻璃", L"无底板" };
const wchar_t* kTempSrcNames[5] = { L"自动", L"仅内置", L"仅 LHM", L"关闭", L"HWiNFO" };

// ----------------------------------------------------------------- 小工具
static int ClampInt(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
static int SD(int dpi, int v) { return MulDiv(v, dpi, 96); }

static RECT Rc(int dpi, int l, int t, int r, int b)
{
    RECT x;
    x.left = SD(dpi, l); x.top = SD(dpi, t);
    x.right = SD(dpi, r); x.bottom = SD(dpi, b);
    return x;
}

static int DpiOf(HWND hwnd)
{
    (void)hwnd;
    HDC dc = GetDC(nullptr);
    int dpi = dc ? GetDeviceCaps(dc, LOGPIXELSX) : 96;
    if (dc) ReleaseDC(nullptr, dc);
    if (dpi < 48 || dpi > 480) dpi = 96;
    return dpi;
}

static COLORREF Blend(COLORREF under, COLORREF over, int percent)
{
    int p = ClampInt(percent, 0, 100);
    int r = (GetRValue(under) * (100 - p) + GetRValue(over) * p) / 100;
    int g = (GetGValue(under) * (100 - p) + GetGValue(over) * p) / 100;
    int b = (GetBValue(under) * (100 - p) + GetBValue(over) * p) / 100;
    return RGB(r, g, b);
}

static HFONT MakeFont(int dpi, int pt, bool bold)
{
    LOGFONTW lf;
    ZeroMemory(&lf, sizeof(lf));
    lf.lfHeight = -MulDiv(pt, dpi, 72);
    lf.lfWeight = bold ? FW_SEMIBOLD : FW_NORMAL;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfOutPrecision = OUT_TT_PRECIS;
    lf.lfQuality = CLEARTYPE_QUALITY;
    lstrcpynW(lf.lfFaceName, L"Segoe UI", 9);
    return CreateFontIndirectW(&lf);
}

static void FillRounded(HDC dc, const RECT& r, int radius, COLORREF fill, COLORREF border)
{
    HBRUSH br = CreateSolidBrush(fill);
    HPEN   pen = CreatePen(PS_SOLID, 1, border);
    HGDIOBJ ob = SelectObject(dc, br);
    HGDIOBJ op = SelectObject(dc, pen);
    RoundRect(dc, r.left, r.top, r.right, r.bottom, radius * 2, radius * 2);
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(br);
    DeleteObject(pen);
}

// ----------------------------------------------------------------- 布局
struct SurfaceGeo {
    RECT showChk, layoutLbl, list, btn[6];
    RECT fontLbl, fontTb, fontVal;
    RECT lineLbl, lineTb, lineVal;
    RECT gapLbl, gapTb, gapVal;
    RECT wLbl, wTb, wVal, hLbl, hTb, hVal;
    RECT shapeLbl, shapeBtn[3];
    RECT colorLbl, colorBtn, concLbl, concTb, concVal;
    RECT matLbl, matBtn[5];
    RECT blurLbl, blurTb, blurVal;
    RECT cornerLbl, cornerTb, cornerVal;
    RECT autoChk, textColorBtn, borderChk, sheenChk;
    RECT sysAcrylicChk;
    RECT hint;
};

struct Geo {
    RECT tab[3];
    SurfaceGeo surf[2];

    RECT refreshLbl, refreshCombo;
    RECT tempSrcLbl, tempSrcBtn[5];
    RECT gpuInfo, rescanBtn, dataHint;

    RECT btnReset, btnCancel, btnSave;
    int  clientW, clientH;
};

static SurfaceGeo ComputeSurfaceGeo(int dpi, int y0, int listH,
                                    bool hasSize, int shapeCount,
                                    int hintBottom, int R)
{
    SurfaceGeo s;
    ZeroMemory(&s, sizeof(s));

    int y = y0;
    s.showChk = Rc(dpi, 12, y, 300, y + 26);                 y += 30;
    s.layoutLbl = Rc(dpi, 12, y, R, y + 18);                 y += 22;
    s.list = Rc(dpi, 12, y, R, y + listH);                   y += listH + 4;
    for (int i = 0; i < 6; ++i) {
        s.btn[i] = Rc(dpi, 12 + i * 82, y, 12 + i * 82 + 76, y + 26);
    }
    y += 30;

    s.fontLbl = Rc(dpi, 12, y + 4, 60, y + 24);
    s.fontTb  = Rc(dpi, 64, y, 390, y + 28);
    s.fontVal = Rc(dpi, 394, y + 4, R, y + 24);              y += 30;

    s.lineLbl = Rc(dpi, 12, y + 4, 60, y + 24);
    s.lineTb  = Rc(dpi, 64, y, 390, y + 28);
    s.lineVal = Rc(dpi, 394, y + 4, R, y + 24);              y += 30;

    s.gapLbl = Rc(dpi, 12, y + 4, 60, y + 24);
    s.gapTb  = Rc(dpi, 64, y, 390, y + 28);
    s.gapVal = Rc(dpi, 394, y + 4, R, y + 24);               y += 30;

    if (hasSize) {
        s.wLbl = Rc(dpi, 12, y + 4, 44, y + 24);
        s.wTb  = Rc(dpi, 48, y, 234, y + 28);
        s.wVal = Rc(dpi, 238, y + 4, 292, y + 24);
        s.hLbl = Rc(dpi, 298, y + 4, 330, y + 24);
        s.hTb  = Rc(dpi, 334, y, 470, y + 28);
        s.hVal = Rc(dpi, 474, y + 4, R, y + 24);
        y += 30;
    }

    s.shapeLbl = Rc(dpi, 12, y + 4, 60, y + 24);
    for (int i = 0; i < shapeCount; ++i) {
        s.shapeBtn[i] = Rc(dpi, 64 + i * 86, y, 64 + i * 86 + 80, y + 28);
    }
    y += 32;

    s.colorLbl = Rc(dpi, 12, y + 4, 52, y + 24);
    s.colorBtn = Rc(dpi, 64, y, 176, y + 28);
    s.concLbl  = Rc(dpi, 180, y + 4, 212, y + 24);
    s.concTb   = Rc(dpi, 214, y, 470, y + 28);
    s.concVal  = Rc(dpi, 474, y + 4, R, y + 24);             y += 32;

    s.matLbl = Rc(dpi, 12, y + 6, 56, y + 28);
    for (int i = 0; i < 5; ++i) {
        s.matBtn[i] = Rc(dpi, 64 + i * 88, y, 64 + i * 88 + 84, y + 30);
    }
    y += 34;

    s.blurLbl = Rc(dpi, 12, y + 4, 52, y + 24);
    s.blurTb  = Rc(dpi, 64, y, 280, y + 28);
    s.blurVal = Rc(dpi, 284, y + 4, 326, y + 24);
    s.cornerLbl = Rc(dpi, 330, y + 4, 380, y + 24);
    s.cornerTb  = Rc(dpi, 384, y, 470, y + 28);
    s.cornerVal = Rc(dpi, 474, y + 4, R, y + 24);            y += 32;

    s.autoChk     = Rc(dpi, 12, y, 150, y + 26);
    s.textColorBtn = Rc(dpi, 156, y, 286, y + 26);
    s.borderChk   = Rc(dpi, 292, y, 382, y + 26);
    s.sheenChk    = Rc(dpi, 388, y, 478, y + 26);            y += 30;

    s.sysAcrylicChk = Rc(dpi, 12, y, 360, y + 26);           y += 30;

    s.hint = Rc(dpi, 12, y, R, hintBottom);
    if (s.hint.bottom < s.hint.top) s.hint.bottom = s.hint.top;
    return s;
}

static Geo ComputeGeo(int dpi, int clientWpx, int clientHpx)
{
    Geo g;
    ZeroMemory(&g, sizeof(g));
    if (dpi < 48) dpi = 96;

    int cw = MulDiv(clientWpx, 96, dpi);
    int ch = MulDiv(clientHpx, 96, dpi);
    if (cw < 520) cw = 520;
    if (ch < 600) ch = 600;

    const int extraH = (ch > 680) ? (ch - 680) : 0;
    const int R      = 508 + (cw - 520);
    const int botY   = ch - 46;

    for (int i = 0; i < 3; ++i) {
        g.tab[i] = Rc(dpi, 12 + i * 166, 8, 12 + i * 166 + 160, 42);
    }

    const int listH = 90 + extraH / 2;
    g.surf[0] = ComputeSurfaceGeo(dpi, 48, listH, false, 2, botY - 14, R);
    g.surf[1] = ComputeSurfaceGeo(dpi, 48, listH, true,  3, botY - 14, R);

    // 数据页
    g.refreshLbl   = Rc(dpi, 12, 54, 100, 78);
    g.refreshCombo = Rc(dpi, 108, 48, 240, 80);
    g.tempSrcLbl   = Rc(dpi, 12, 96, 112, 120);
    for (int i = 0; i < 5; ++i) {
        g.tempSrcBtn[i] = Rc(dpi, 116 + i * 78, 90, 116 + i * 78 + 72, 122);
    }
    const int gpuTop    = 140;
    const int gpuBottom = botY - 54;
    g.gpuInfo   = Rc(dpi, 12, gpuTop, R, gpuBottom);
    g.rescanBtn = Rc(dpi, 12, gpuBottom + 8, 140, gpuBottom + 36);
    g.dataHint  = Rc(dpi, 12, gpuBottom + 44, R, botY - 14);

    g.btnReset  = Rc(dpi, 12, botY, 124, botY + 32);
    g.btnCancel = Rc(dpi, 268, botY, 380, botY + 32);
    g.btnSave   = Rc(dpi, R - 112, botY, R, botY + 32);

    g.clientW = SD(dpi, cw);
    g.clientH = SD(dpi, ch);
    return g;
}

// ----------------------------------------------------------------- 状态
using GetRectFn = std::function<RECT(const Geo&)>;

struct Entry {
    HWND     hwnd;
    int      page;
    GetRectFn get;
};

struct SurfaceCtrls {
    HWND showChk, layoutLbl, list;
    HWND btn[6];
    HWND fontLbl, fontTb, fontVal;
    HWND lineLbl, lineTb, lineVal;
    HWND gapLbl, gapTb, gapVal;
    HWND wLbl, wTb, wVal, hLbl, hTb, hVal;
    HWND shapeLbl, shape[3];
    HWND colorLbl, colorBtn, concLbl, concTb, concVal;
    HWND matLbl, mat[5];
    HWND blurLbl, blurTb, blurVal;
    HWND cornerLbl, cornerTb, cornerVal;
    HWND autoChk, textColorBtn, borderChk, sheenChk, sysAcrylicChk;
};

struct SurfaceState {
    layout_model::LayoutBlock spec;
    int sel;
};

struct State {
    HWND      hwnd, owner;
    HINSTANCE inst;
    AppConfig cfg, defaults, origin;
    int       dpi, page;
    Geo       geo;

    SurfaceState surf[2];
    SurfaceCtrls ui[2];

    Entry ctrls[192];
    int   ctrlCount;

    HWND tab[3];
    HWND refresh, gpuInfo, rescanBtn;
    HWND tempSrc[5];
    HWND btnReset, btnCancel, btnSave;

    HFONT font, fontBold;
    settings_ui::PreviewFn onPreview;
    settings_ui::ApplyFn   onApply;
    void* user;
};

static State g;
static int g_pendingDpi = 0;

// ----------------------------------------------------------------- 页面判定
static int SurfaceOfId(int id)
{
    if (id >= 3100 && id < 3200) return 0;
    if (id >= 3200 && id < 3300) return 1;
    return -1;
}

static int& MatOf(AppConfig& c, int s)       { return s == 0 ? c.tbMat : c.ballMat; }
static int& ShapeOf(AppConfig& c, int s)     { return s == 0 ? c.tbShape : c.ballShape; }
static int& BlurOf(AppConfig& c, int s)      { return s == 0 ? c.tbBlur : c.ballBlur; }
static int& CornerOf(AppConfig& c, int s)    { return s == 0 ? c.tbCorner : c.ballCorner; }
static int& ConcOf(AppConfig& c, int s)      { return s == 0 ? c.tbTintAlpha : c.ballTintAlpha; }
static COLORREF& TintOf(AppConfig& c, int s) { return s == 0 ? c.tbTint : c.ballTint; }
static BOOL& BorderOf(AppConfig& c, int s)   { return s == 0 ? c.tbBorder : c.ballBorder; }
static BOOL& SheenOf(AppConfig& c, int s)    { return s == 0 ? c.tbSheen : c.ballSheen; }
static BOOL& TextAutoOf(AppConfig& c, int s) { return s == 0 ? c.tbTextAuto : c.ballTextAuto; }
static COLORREF& TextColorOf(AppConfig& c, int s) { return s == 0 ? c.tbTextColor : c.ballTextColor; }
static BOOL& SysAcrylicOf(AppConfig& c, int s) { return s == 0 ? c.tbSysAcrylic : c.ballSysAcrylic; }

static int& FontOf(AppConfig& c, int s)  { return s == 0 ? c.tbFontPx : c.ballFontPx; }
static int& LineOf(AppConfig& c, int s)  { return s == 0 ? c.tbLinePct : c.ballLinePct; }
static int& GapOf(AppConfig& c, int s)   { return s == 0 ? c.tbItemGap : c.ballItemGap; }

static void SchedulePreview()
{
    if (g.hwnd && g.onPreview) SetTimer(g.hwnd, 43, 16, NULL);
}

static void CancelAndClose()
{
    HWND h = g.hwnd;
    if (h) KillTimer(h, 43);
    if (g.onPreview) g.onPreview(&g.origin, g.user);
    if (h && IsWindow(h)) DestroyWindow(h);
}

// ----------------------------------------------------------------- 控件登记
static void Reg(HWND h, int page, GetRectFn get)
{
    if (!h) return;
    if (g.ctrlCount >= (int)(sizeof(g.ctrls) / sizeof(g.ctrls[0]))) return;
    Entry& e = g.ctrls[g.ctrlCount++];
    e.hwnd = h;
    e.page = page;
    e.get  = std::move(get);
}

static void ApplyPage()
{
    for (int i = 0; i < g.ctrlCount; ++i) {
        const Entry& e = g.ctrls[i];
        if (!e.hwnd || !IsWindow(e.hwnd)) continue;
        const bool visible = (e.page < 0) || (e.page == g.page);
        if (!visible) {
            ShowWindow(e.hwnd, SW_HIDE);
            continue;
        }
        if (e.get) {
            const RECT rc = e.get(g.geo);
            SetWindowPos(e.hwnd, NULL, rc.left, rc.top,
                         rc.right - rc.left, rc.bottom - rc.top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
        }
        ShowWindow(e.hwnd, SW_SHOW);
    }
    for (int i = 0; i < 3; ++i) {
        if (g.tab[i]) InvalidateRect(g.tab[i], NULL, TRUE);
    }
    InvalidateRect(g.hwnd, NULL, TRUE);
}

// ----------------------------------------------------------------- 排版列表
static void SpecToList(const layout_model::LayoutBlock& b, HWND list, int* io)
{
    if (!list) return;
    SendMessageW(list, LB_RESETCONTENT, 0, 0);
    for (int i = 0; i < b.count; ++i) {
        wchar_t line[160];
        _snwprintf(line, 159, L"  行%d  \u00B7  %ls", b.rowOf[i] + 1,
                   layout_model::itemLabel(b.item[i]));
        line[159] = 0;
        SendMessageW(list, LB_ADDSTRING, 0, (LPARAM)line);
    }
    if (b.count > 0) {
        int s = (io && *io >= 0) ? *io : 0;
        if (s >= b.count) s = b.count - 1;
        SendMessageW(list, LB_SETCURSEL, (WPARAM)s, 0);
        if (io) *io = s;
    } else if (io) {
        *io = -1;
    }
}

static int ListSel(HWND list)
{
    if (!list) return -1;
    const int s = (int)SendMessageW(list, LB_GETCURSEL, 0, 0);
    return (s == LB_ERR) ? -1 : s;
}

static void SyncSpecToCfg()
{
    layout_model::format(g.surf[0].spec, g.cfg.tbLayout, NETBALL_SPEC_CCH);
    layout_model::format(g.surf[1].spec, g.cfg.ballLayout, NETBALL_SPEC_CCH);
}

static void CommitSpec(int page, layout_model::LayoutBlock block, int sel)
{
    const int s = (page == PAGE_TB) ? 0 : 1;
    if (s == 0) {
        layout_model::normalize(&block, NETBALL_MAX_TB_ROWS);
    } else {
        layout_model::normalize(&block, NETBALL_MAX_ITEMS);
    }
    g.surf[s].spec = block;
    g.surf[s].sel  = sel;
    SpecToList(g.surf[s].spec, g.ui[s].list, &g.surf[s].sel);
    SyncSpecToCfg();
    SchedulePreview();
}

static layout_model::LayoutBlock WorkingSpec(int page)
{
    return (page == PAGE_TB) ? g.surf[0].spec : g.surf[1].spec;
}

static int& WorkingSel(int page)
{
    return (page == PAGE_TB) ? g.surf[0].sel : g.surf[1].sel;
}

static void UpdateListButtons();

// 列表操作：op 1=删除 2=上移 3=下移 4=行-1 5=行+1
static void ListOp(int page, int op)
{
    layout_model::LayoutBlock b = WorkingSpec(page);
    int sel = WorkingSel(page);
    if (sel < 0 || sel >= b.count) return;

    if (op == 1) {
        if (b.count <= 1) return;
        for (int i = sel; i + 1 < b.count; ++i) {
            b.item[i]  = b.item[i + 1];
            b.rowOf[i] = b.rowOf[i + 1];
        }
        --b.count;
        if (sel >= b.count) sel = b.count - 1;
    } else if (op == 2 && sel > 0) {
        const int ti = b.item[sel];  b.item[sel]  = b.item[sel - 1];  b.item[sel - 1]  = ti;
        const int tr = b.rowOf[sel]; b.rowOf[sel] = b.rowOf[sel - 1]; b.rowOf[sel - 1] = tr;
        --sel;
    } else if (op == 3 && sel + 1 < b.count) {
        const int ti = b.item[sel];  b.item[sel]  = b.item[sel + 1];  b.item[sel + 1]  = ti;
        const int tr = b.rowOf[sel]; b.rowOf[sel] = b.rowOf[sel + 1]; b.rowOf[sel + 1] = tr;
        ++sel;
    } else if (op == 4) {
        if (b.rowOf[sel] > 0) --b.rowOf[sel];
    } else if (op == 5) {
        const int maxRows = layout_model::maxRows(page == PAGE_TB ? layout_model::SURF_TASKBAR
                                                                 : layout_model::SURF_BALL);
        if (b.rowOf[sel] + 1 < maxRows) ++b.rowOf[sel];
    }

    CommitSpec(page, b, sel);
    UpdateListButtons();
    InvalidateRect(g.hwnd, NULL, FALSE);
}

static void UpdateListButtons()
{
    if (g.ui[0].btn[1]) EnableWindow(g.ui[0].btn[1], g.surf[0].spec.count > 1 ? TRUE : FALSE);
    if (g.ui[1].btn[1]) EnableWindow(g.ui[1].btn[1], g.surf[1].spec.count > 1 ? TRUE : FALSE);
}

static void ShowAddMenu(int page)
{
    layout_model::LayoutBlock b = WorkingSpec(page);
    const int maxRows = layout_model::maxRows(page == PAGE_TB ? layout_model::SURF_TASKBAR
                                                              : layout_model::SURF_BALL);
    if (b.count >= NETBALL_MAX_ITEMS) return;

    HMENU menu = CreatePopupMenu();
    if (!menu) return;

    int n = 0;
    for (int i = 0; i < layout_model::itemCount(); ++i) {
        if (layout_model::contains(b, i)) continue;
        AppendMenuW(menu, MF_STRING, 100 + i, layout_model::itemLabel(i));
        ++n;
    }
    if (n == 0) {
        DestroyMenu(menu);
        return;
    }

    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(g.hwnd);
    const int cmd = (int)TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                                        pt.x, pt.y, 0, g.hwnd, nullptr);
    DestroyMenu(menu);
    PostMessageW(g.hwnd, WM_NULL, 0, 0);

    if (cmd < 100) return;
    const int idx = cmd - 100;
    if (idx < 0 || idx >= layout_model::itemCount()) return;

    b.item[b.count] = idx;
    const int sel = WorkingSel(page);
    int row = (sel >= 0 && sel < b.count) ? b.rowOf[sel] : (b.rows > 0 ? b.rows - 1 : 0);
    if (row < 0) row = 0;
    if (row >= maxRows) row = maxRows - 1;
    b.rowOf[b.count] = row;
    ++b.count;

    CommitSpec(page, b, b.count - 1);
    UpdateListButtons();
    InvalidateRect(g.hwnd, NULL, FALSE);
}

// ----------------------------------------------------------------- 球体 / 字号
static int BallFontUpperDip()
{
    const int w = MulDiv(g.cfg.ballW, g.dpi, 96);
    const int h = MulDiv(g.cfg.ballH, g.dpi, 96);
    return render::maxFontForBall(&g.cfg, nullptr, g.dpi, w, h);
}

// v1.4.1：球体尺寸由宽高滑块/形状决定，字号只做「当前球体能装下」的上限收敛。
// 不再在字号变化时把球体改成最小尺寸，避免胶囊被压成方形、也避免运行期尺寸跳变。
static void ClampBallFontAndSize()
{
    if (g.cfg.ballFontPx > 0) {
        const int maxDip = BallFontUpperDip();
        if (g.cfg.ballFontPx > maxDip) g.cfg.ballFontPx = maxDip;
        if (g.cfg.ballFontPx > 48) g.cfg.ballFontPx = 48;
        if (g.cfg.ballFontPx < 8)  g.cfg.ballFontPx = 8;
    }

    if (g.cfg.ballW < 40)  g.cfg.ballW = 40;
    if (g.cfg.ballH < 40)  g.cfg.ballH = 40;
    if (g.cfg.ballW > 400) g.cfg.ballW = 400;
    if (g.cfg.ballH > 400) g.cfg.ballH = 400;

    // 方形胶囊在像素上就是圆；发现宽高相等时补成 3:2。
    if (g.cfg.ballShape == 2 && g.cfg.ballW == g.cfg.ballH) {
        if (g.cfg.ballH > 266) g.cfg.ballH = 266;
        g.cfg.ballW = g.cfg.ballH * 3 / 2;
        if (g.cfg.ballW > 400) g.cfg.ballW = 400;
    }
}

// ----------------------------------------------------------------- 值显示
static void UpdateValueLabels()
{
    wchar_t buf[48];

    for (int s = 0; s < 2; ++s) {
        SurfaceCtrls& ui = g.ui[s];
        const bool isBall = (s == 1);

        if (FontOf(g.cfg, s) == 0) {
            lstrcpynW(buf, L"自适应", 48);
        } else if (isBall) {
            const int maxDip = BallFontUpperDip();
            if (g.cfg.ballFontPx >= maxDip)
                _snwprintf(buf, 47, L"%d px · 上限", g.cfg.ballFontPx);
            else
                _snwprintf(buf, 47, L"%d px", g.cfg.ballFontPx);
        } else {
            _snwprintf(buf, 47, L"%d px", g.cfg.tbFontPx);
        }
        buf[47] = 0;
        if (ui.fontVal) SetWindowTextW(ui.fontVal, buf);

        _snwprintf(buf, 47, L"%d %%", LineOf(g.cfg, s)); buf[47] = 0;
        if (ui.lineVal) SetWindowTextW(ui.lineVal, buf);

        _snwprintf(buf, 47, L"%d px", GapOf(g.cfg, s)); buf[47] = 0;
        if (ui.gapVal) SetWindowTextW(ui.gapVal, buf);

        if (isBall) {
            _snwprintf(buf, 47, L"%d px", g.cfg.ballW); buf[47] = 0;
            if (ui.wVal) SetWindowTextW(ui.wVal, buf);

            _snwprintf(buf, 47, L"%d px", g.cfg.ballH); buf[47] = 0;
            if (ui.hVal) SetWindowTextW(ui.hVal, buf);
        }

        _snwprintf(buf, 47, L"%d %%", ConcOf(g.cfg, s)); buf[47] = 0;
        if (ui.concVal) SetWindowTextW(ui.concVal, buf);

        _snwprintf(buf, 47, L"%d px", BlurOf(g.cfg, s)); buf[47] = 0;
        if (ui.blurVal) SetWindowTextW(ui.blurVal, buf);

        _snwprintf(buf, 47, L"%d px", CornerOf(g.cfg, s)); buf[47] = 0;
        if (ui.cornerVal) SetWindowTextW(ui.cornerVal, buf);
    }
}

static void FillGpuInfo()
{
    if (!g.gpuInfo) return;

    Metrics m;
    ZeroMemory(&m, sizeof(m));
    sensors::get(&m);

    std::wstring txt;
    wchar_t line[256];
    _snwprintf(line, 255, L"检测到 %d 张显卡", m.gpuCount);
    line[255] = 0;
    txt += line;
    txt += L"\r\n";

    for (int i = 0; i < m.gpuCount && i < NETBALL_MAX_GPU; ++i) {
        const wchar_t* name = m.gpuName[i][0] ? m.gpuName[i] : L"未知显卡";
        _snwprintf(line, 255, L"  GPU%d  %ls", i, name);
        line[255] = 0;
        txt += line;
        txt += L"\r\n";
    }
    if (m.gpuCount == 0) txt += L"  （枚举失败或没有独立显卡）\r\n";

    txt += L"\r\n数据源：";
    switch (g.cfg.tempSrc) {
    case 0: txt += L"自动"; break;
    case 1: txt += L"仅内置"; break;
    case 2: txt += L"仅 LHM"; break;
    case 4: txt += L"HWiNFO"; break;
    default: txt += L"已关闭"; break;
    }
    txt += glass::acrylicAvailable()
        ? L"    系统实时亚克力：可用（实验性）"
        : L"    系统实时亚克力：不可用（自动回退自绘）";

    SetWindowTextW(g.gpuInfo, txt.c_str());
    SendMessageW(g.gpuInfo, EM_SETSEL, 0, 0);
    SendMessageW(g.gpuInfo, EM_SCROLLCARET, 0, 0);
}

// ----------------------------------------------------------------- 整窗绘制
static const wchar_t* kTbHintText =
    L"面板尺寸由内容自动决定；胶囊会按端部圆弧自动留出内边距。"
    L"调整颜色/材质/模糊时任务栏面板会实时变化。";
static const wchar_t* kBallHintText =
    L"球体尺寸固定；字号与球体互相制约。亚克力/玻璃的模糊来自当前桌面背景，"
    L"拖动位置或更换壁纸后会自动重算。";
static const wchar_t* kDataHintText =
    L"温度读不到时显示 \"--\"，不会伪造数值。HWiNFO 需在「设置 → Safety」中开启 "
    L"Shared Memory Support。系统实时亚克力仅亚克力材质可用，Win11 非激活窗口可能退化为纯色。";

static void PaintAll(HDC dc, const RECT& rc)
{
    HBRUSH bg = CreateSolidBrush(kDlgBg);
    FillRect(dc, &rc, bg);
    DeleteObject(bg);

    SetBkMode(dc, TRANSPARENT);

    const wchar_t* hint = nullptr;
    RECT hintRc = {0, 0, 0, 0};
    if (g.page == PAGE_TB)      { hint = kTbHintText;  hintRc = g.geo.surf[0].hint; }
    else if (g.page == PAGE_BALL){ hint = kBallHintText; hintRc = g.geo.surf[1].hint; }
    else                         { hint = kDataHintText; hintRc = g.geo.dataHint; }

    // 600 DIP 最小高度下悬浮球页只剩几像素说明区，画出来会被底栏切成半行字，
    // 不如直接省略；正常/放大窗口仍然完整显示。
    if (hint && (hintRc.bottom - hintRc.top) >= SD(g.dpi, 18)) {
        HGDIOBJ of = SelectObject(dc, g.font);
        SetTextColor(dc, kTextDim);
        DrawTextW(dc, hint, -1, &hintRc, DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX);
        SelectObject(dc, of);
    }

    if (g.page == PAGE_DATA) {
        const RECT& gr = g.geo.gpuInfo;
        HPEN pen = CreatePen(PS_SOLID, 1, kSwatchBd);
        HGDIOBJ op = SelectObject(dc, pen);
        HGDIOBJ ob = SelectObject(dc, GetStockObject(NULL_BRUSH));
        RoundRect(dc, gr.left, gr.top, gr.right, gr.bottom,
                  SD(g.dpi, 8), SD(g.dpi, 8));
        SelectObject(dc, ob);
        SelectObject(dc, op);
        DeleteObject(pen);
    }

    const int y = g.geo.btnReset.top - SD(g.dpi, 14);
    HPEN pen = CreatePen(PS_SOLID, 1, kLine);
    HGDIOBJ op = SelectObject(dc, pen);
    MoveToEx(dc, g.geo.btnReset.left, y, NULL);
    LineTo(dc, g.geo.btnSave.right, y);
    SelectObject(dc, op);
    DeleteObject(pen);
}

// ----------------------------------------------------------------- 自绘按钮
static void DrawCheckGlyph(HDC dc, const RECT& box, COLORREF fg)
{
    HPEN pen = CreatePen(PS_SOLID, 2, fg);
    HGDIOBJ op = SelectObject(dc, pen);
    int w = box.right - box.left;
    int hh = box.bottom - box.top;
    POINT p[3];
    p[0].x = box.left + w * 22 / 100;
    p[0].y = box.top + hh * 52 / 100;
    p[1].x = box.left + w * 43 / 100;
    p[1].y = box.top + hh * 74 / 100;
    p[2].x = box.left + w * 79 / 100;
    p[2].y = box.top + hh * 27 / 100;
    Polyline(dc, p, 3);
    SelectObject(dc, op);
    DeleteObject(pen);
}

static void DrawOwnButton(const DRAWITEMSTRUCT* dis)
{
    HDC dc = dis->hDC;
    RECT r = dis->rcItem;
    const int id = (int)dis->CtlID;
    const int dpi = g.dpi;
    const bool disabled = (dis->itemState & ODS_DISABLED) != 0;
    const bool pressed  = (dis->itemState & ODS_SELECTED) != 0;

    wchar_t text[128];
    text[0] = 0;
    GetWindowTextW(dis->hwndItem, text, 128);

    HBRUSH bg = CreateSolidBrush(kDlgBg);
    FillRect(dc, &r, bg);
    DeleteObject(bg);

    SetBkMode(dc, TRANSPARENT);
    SelectObject(dc, g.font);

    // ---- 页签 ----
    if (id >= IDC_TAB0 && id <= IDC_TAB2) {
        const int idx = id - IDC_TAB0;
        const bool sel = (idx == g.page);
        RECT rr = r;
        if (sel) {
            FillRounded(dc, rr, SD(dpi, 6), kBtnFace, kBtnFace);
        } else if (pressed) {
            FillRounded(dc, rr, SD(dpi, 6), kBtnFaceP, kBtnFaceP);
        }
        SelectObject(dc, sel ? g.fontBold : g.font);
        SetTextColor(dc, sel ? kText : kTextDim);
        RECT tr = rr;
        DrawTextW(dc, text, -1, &tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        if (sel) {
            RECT under;
            under.left   = rr.left + SD(dpi, 10);
            under.right  = rr.right - SD(dpi, 10);
            under.bottom = rr.bottom - 1;
            under.top    = under.bottom - SD(dpi, 2);
            HBRUSH b = CreateSolidBrush(kAccent);
            FillRect(dc, &under, b);
            DeleteObject(b);
        }
        return;
    }

    // ---- 形状单选 ----
    const bool isTbShape   = (id == IDC_TB_SHAPE0 || id == IDC_TB_SHAPE1);
    const bool isBallShape = (id >= IDC_BALL_SHAPE0 && id <= IDC_BALL_SHAPE2);
    if (isTbShape || isBallShape) {
        const int s = isTbShape ? 0 : 1;
        const int idx = isTbShape ? (id - IDC_TB_SHAPE0) : (id - IDC_BALL_SHAPE0);
        const int shape = ShapeOf(g.cfg, s);
        const bool sel = isTbShape ? (shape == idx + 1) : (shape == idx);

        const int d = SD(dpi, 14);
        RECT cir;
        cir.left = r.left + SD(dpi, 1);
        cir.right = cir.left + d;
        cir.top = (r.top + r.bottom) / 2 - d / 2;
        cir.bottom = cir.top + d;

        HBRUSH brBg = CreateSolidBrush(kPanelBg);
        HPEN pen = CreatePen(PS_SOLID, 1, sel ? kAccent : kSwatchBd);
        HGDIOBJ ob = SelectObject(dc, brBg);
        HGDIOBJ op = SelectObject(dc, pen);
        Ellipse(dc, cir.left, cir.top, cir.right, cir.bottom);
        SelectObject(dc, ob);
        SelectObject(dc, op);
        DeleteObject(brBg);
        DeleteObject(pen);

        if (sel) {
            RECT dot = cir;
            InflateRect(&dot, -SD(dpi, 4), -SD(dpi, 4));
            HBRUSH b2 = CreateSolidBrush(kAccent);
            HPEN p2 = CreatePen(PS_SOLID, 1, kAccent);
            HGDIOBJ o2 = SelectObject(dc, b2);
            HGDIOBJ o3 = SelectObject(dc, p2);
            Ellipse(dc, dot.left, dot.top, dot.right, dot.bottom);
            SelectObject(dc, o2);
            SelectObject(dc, o3);
            DeleteObject(b2);
            DeleteObject(p2);
        }

        SetTextColor(dc, sel ? kText : kTextDim);
        RECT tr = r;
        tr.left = cir.right + SD(dpi, 6);
        DrawTextW(dc, text, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        return;
    }

    // ---- 复选 ----
    const bool isCheck =
        (id == IDC_TB_SHOW || id == IDC_BALL_SHOW ||
         id == IDC_TB_AUTOTEXT || id == IDC_BALL_AUTOTEXT ||
         id == IDC_TB_BORDER || id == IDC_BALL_BORDER ||
         id == IDC_TB_SHEEN || id == IDC_BALL_SHEEN ||
         id == IDC_TB_SYSACRYLIC || id == IDC_BALL_SYSACRYLIC);
    if (isCheck) {
        bool checked = false;
        switch (id) {
        case IDC_TB_SHOW:       checked = g.cfg.taskbarShow != FALSE; break;
        case IDC_BALL_SHOW:     checked = g.cfg.showBall != FALSE; break;
        case IDC_TB_AUTOTEXT:   checked = g.cfg.tbTextAuto != FALSE; break;
        case IDC_BALL_AUTOTEXT: checked = g.cfg.ballTextAuto != FALSE; break;
        case IDC_TB_BORDER:     checked = g.cfg.tbBorder != FALSE; break;
        case IDC_BALL_BORDER:   checked = g.cfg.ballBorder != FALSE; break;
        case IDC_TB_SHEEN:      checked = g.cfg.tbSheen != FALSE; break;
        case IDC_BALL_SHEEN:    checked = g.cfg.ballSheen != FALSE; break;
        case IDC_TB_SYSACRYLIC: checked = g.cfg.tbSysAcrylic != FALSE; break;
        default:                checked = g.cfg.ballSysAcrylic != FALSE; break;
        }

        const int d = SD(dpi, 15);
        RECT box;
        box.left = r.left + SD(dpi, 1);
        box.right = box.left + d;
        box.top = (r.top + r.bottom) / 2 - d / 2;
        box.bottom = box.top + d;
        FillRounded(dc, box, SD(dpi, 3), checked ? kAccent : kPanelBg,
                    checked ? kAccent : kSwatchBd);
        if (checked) {
            RECT gb = box;
            InflateRect(&gb, -SD(dpi, 1), -SD(dpi, 1));
            DrawCheckGlyph(dc, gb, RGB(255, 255, 255));
        }
        SetTextColor(dc, disabled ? kTextDim : (checked ? kText : kTextDim));
        RECT tr = r;
        tr.left = box.right + SD(dpi, 8);
        DrawTextW(dc, text, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        return;
    }

    // ---- 颜色按钮 ----
    const bool isColor = (id == IDC_TB_COLOR || id == IDC_BALL_COLOR ||
                          id == IDC_TB_TEXTCOLOR || id == IDC_BALL_TEXTCOLOR);
    if (isColor) {
        COLORREF c = RGB(255, 255, 255);
        if (id == IDC_TB_COLOR)        c = g.cfg.tbTint;
        if (id == IDC_BALL_COLOR)      c = g.cfg.ballTint;
        if (id == IDC_TB_TEXTCOLOR)    c = g.cfg.tbTextColor;
        if (id == IDC_BALL_TEXTCOLOR)  c = g.cfg.ballTextColor;

        RECT br2 = r;
        InflateRect(&br2, -1, -1);
        FillRounded(dc, br2, SD(dpi, 6), kBtnFace, kSwatchBd);
        RECT sw = br2;
        sw.left += SD(dpi, 6);
        sw.right = sw.left + SD(dpi, 18);
        sw.top += SD(dpi, 6);
        sw.bottom -= SD(dpi, 6);
        if (sw.bottom > sw.top && sw.right > sw.left) {
            FillRounded(dc, sw, SD(dpi, 3), c, kSwatchBd);
        }
        SetTextColor(dc, disabled ? kTextDim : kText);
        RECT tr = br2;
        tr.left = sw.right + SD(dpi, 6);
        DrawTextW(dc, text, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        return;
    }

    // ---- 材质 / 温度来源卡片 ----
    const int sMat = SurfaceOfId(id);
    const bool isMatCard = (sMat >= 0 && id >= (sMat == 0 ? IDC_TB_MAT0 : IDC_BALL_MAT0) &&
                            id <= (sMat == 0 ? IDC_TB_MAT4 : IDC_BALL_MAT4));
    const bool isTempCard = (id >= IDC_TEMPSRC0 && id <= IDC_TEMPSRC4);
    if (isMatCard || isTempCard) {
        const int idx = isTempCard ? (id - IDC_TEMPSRC0)
                                   : (id - (sMat == 0 ? IDC_TB_MAT0 : IDC_BALL_MAT0));
        const int sel = isTempCard ? ClampInt(g.cfg.tempSrc, 0, 4)
                                   : ClampInt(MatOf(g.cfg, sMat), 0, 4);
        const bool on = (idx == sel);

        RECT rr = r;
        InflateRect(&rr, -1, -1);
        FillRounded(dc, rr, SD(dpi, 6),
                    on ? Blend(kPanelBg, kAccent, 28) : kPanelBg,
                    on ? kAccent : kSwatchBd);
        if (on) {
            RECT hl = rr;
            InflateRect(&hl, -SD(dpi, 1), -SD(dpi, 1));
            hl.bottom = hl.top + SD(dpi, 2);
            HBRUSH hb = CreateSolidBrush(Blend(kAccent, RGB(255, 255, 255), 40));
            FillRect(dc, &hl, hb);
            DeleteObject(hb);
        }
        SelectObject(dc, on ? g.fontBold : g.font);
        SetTextColor(dc, disabled ? kTextDim : (on ? kText : kTextDim));
        DrawTextW(dc, text, -1, &rr, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        return;
    }

    // ---- 普通按钮 ----
    COLORREF face = kBtnFace;
    COLORREF tcol = kText;
    if (id == IDC_SAVE) {
        face = pressed ? kAccentPr : kAccent;
        tcol = RGB(255, 255, 255);
    } else {
        face = pressed ? kBtnFaceP : kBtnFace;
        tcol = kText;
    }
    if (disabled) {
        face = kBtnFaceP;
        tcol = kTextDim;
    }
    RECT br2 = r;
    InflateRect(&br2, -1, -1);
    FillRounded(dc, br2, SD(dpi, 6), face, pressed ? face : kSwatchBd);

    SetTextColor(dc, tcol);
    SelectObject(dc, (id == IDC_SAVE) ? g.fontBold : g.font);
    DrawTextW(dc, text, -1, &br2, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    if (dis->itemState & ODS_FOCUS) {
        RECT f = br2;
        InflateRect(&f, -SD(dpi, 3), -SD(dpi, 3));
        DrawFocusRect(dc, &f);
    }
}

// ----------------------------------------------------------------- 建控件
static HWND MakeLabel(HWND parent, const wchar_t* txt, int id, int page,
                      GetRectFn get, DWORD extraStyle)
{
    RECT rc = get(g.geo);
    HWND h = CreateWindowExW(0, L"STATIC", txt,
                             WS_CHILD | WS_VISIBLE | extraStyle,
                             rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top,
                             parent, (HMENU)(INT_PTR)id, g.inst, NULL);
    if (h) SendMessageW(h, WM_SETFONT, (WPARAM)g.font, TRUE);
    Reg(h, page, std::move(get));
    return h;
}

static HWND MakeButton(HWND parent, const wchar_t* txt, int id, int page, GetRectFn get)
{
    RECT rc = get(g.geo);
    HWND h = CreateWindowExW(0, L"BUTTON", txt,
                             WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                             rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top,
                             parent, (HMENU)(INT_PTR)id, g.inst, NULL);
    if (h) SendMessageW(h, WM_SETFONT, (WPARAM)g.font, TRUE);
    Reg(h, page, std::move(get));
    return h;
}

static HWND MakeTrackbar(HWND parent, int id, int lo, int hi, int line, int page, GetRectFn get)
{
    RECT rc = get(g.geo);
    HWND h = CreateWindowExW(0, TRACKBAR_CLASSW, L"",
                             WS_CHILD | WS_VISIBLE | WS_TABSTOP | TBS_HORZ | TBS_NOTICKS,
                             rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top,
                             parent, (HMENU)(INT_PTR)id, g.inst, NULL);
    SendMessageW(h, TBM_SETRANGE, TRUE, MAKELPARAM(lo, hi));
    SendMessageW(h, TBM_SETLINESIZE, 0, line);
    SendMessageW(h, TBM_SETPAGESIZE, 0, (LPARAM)(line * 4));
    Reg(h, page, std::move(get));
    return h;
}

static HWND MakeCombo(HWND parent, int id, const wchar_t* const* items, int count,
                      int page, GetRectFn get)
{
    RECT rc = get(g.geo);
    HWND h = CreateWindowExW(0, L"COMBOBOX", L"",
                             WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST,
                             rc.left, rc.top, rc.right - rc.left,
                             (rc.bottom - rc.top) + SD(g.dpi, 130),
                             parent, (HMENU)(INT_PTR)id, g.inst, NULL);
    for (int i = 0; i < count; ++i) SendMessageW(h, CB_ADDSTRING, 0, (LPARAM)items[i]);
    SendMessageW(h, WM_SETFONT, (WPARAM)g.font, TRUE);
    Reg(h, page, std::move(get));
    return h;
}

static HWND MakeList(HWND parent, int id, int page, GetRectFn get)
{
    RECT rc = get(g.geo);
    HWND h = CreateWindowExW(0, L"LISTBOX", L"",
                             WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL |
                             LBS_NOTIFY | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS,
                             rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top,
                             parent, (HMENU)(INT_PTR)id, g.inst, NULL);
    SendMessageW(h, WM_SETFONT, (WPARAM)g.font, TRUE);
    SendMessageW(h, LB_SETITEMHEIGHT, 0, (LPARAM)SD(g.dpi, 26));
    Reg(h, page, std::move(get));
    return h;
}

static HWND MakeEdit(HWND parent, int id, int page, GetRectFn get)
{
    RECT rc = get(g.geo);
    HWND h = CreateWindowExW(0, L"EDIT", L"",
                             WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL |
                             ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | ES_NOHIDESEL,
                             rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top,
                             parent, (HMENU)(INT_PTR)id, g.inst, NULL);
    SendMessageW(h, WM_SETFONT, (WPARAM)g.font, TRUE);
    SendMessageW(h, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN,
                 MAKELPARAM(SD(g.dpi, 8), SD(g.dpi, 8)));
    Reg(h, page, std::move(get));
    return h;
}

// 控件矩形来自 g.geo；宏只在 CreateControls 内使用
#define RG(field)      [surface](const Geo& geo) { return geo.surf[surface].field; }
#define RGI(field, i)  [surface, i](const Geo& geo) { return geo.surf[surface].field[i]; }

static void CreateOneSurface(HWND parent, int surface, bool isBall, int shapeCount)
{
    SurfaceCtrls& ui = g.ui[surface];
    const int page = surface;

    ui.showChk = MakeButton(parent, isBall ? L"显示悬浮球" : L"在任务栏显示",
                            isBall ? IDC_BALL_SHOW : IDC_TB_SHOW, page, RG(showChk));

    ui.layoutLbl = MakeLabel(parent,
        isBall ? L"项目 / 行（行数只受项目数量限制）"
               : L"项目 / 行（列表从上到下即显示顺序）",
        isBall ? IDC_LBL_BALL_LAYOUT : IDC_LBL_TB_LAYOUT, page,
        RG(layoutLbl), SS_LEFT | SS_CENTERIMAGE);

    ui.list = MakeList(parent, isBall ? IDC_BALL_LIST : IDC_TB_LIST, page, RG(list));

    static const wchar_t* btnNames[6] = { L"添加\u2026", L"移除", L"上移", L"下移", L"行 \u2190", L"行 \u2192" };
    for (int i = 0; i < 6; ++i) {
        const int id = (isBall ? IDC_BALL_ADD : IDC_TB_ADD) + i;
        ui.btn[i] = MakeButton(parent, btnNames[i], id, page, RGI(btn, i));
    }

    ui.fontLbl = MakeLabel(parent, L"字号", isBall ? IDC_LBL_BALL_FONT : IDC_LBL_TB_FONT,
                           page, RG(fontLbl), SS_LEFT | SS_CENTERIMAGE);
    ui.fontTb = MakeTrackbar(parent, isBall ? IDC_BALL_FONT_TB : IDC_TB_FONT_TB,
                             0, 48, 1, page, RG(fontTb));
    ui.fontVal = MakeLabel(parent, L"", isBall ? IDC_BALL_FONT_VAL : IDC_TB_FONT_VAL,
                           page, RG(fontVal), SS_RIGHT | SS_CENTERIMAGE);

    ui.lineLbl = MakeLabel(parent, L"行间距", isBall ? IDC_LBL_BALL_LINE : IDC_LBL_TB_LINE,
                           page, RG(lineLbl), SS_LEFT | SS_CENTERIMAGE);
    ui.lineTb = MakeTrackbar(parent, isBall ? IDC_BALL_LINE_TB : IDC_TB_LINE_TB,
                             100, 200, 2, page, RG(lineTb));
    ui.lineVal = MakeLabel(parent, L"", isBall ? IDC_BALL_LINE_VAL : IDC_TB_LINE_VAL,
                           page, RG(lineVal), SS_RIGHT | SS_CENTERIMAGE);

    ui.gapLbl = MakeLabel(parent, L"项目间距", isBall ? IDC_LBL_BALL_GAP : IDC_LBL_TB_GAP,
                          page, RG(gapLbl), SS_LEFT | SS_CENTERIMAGE);
    ui.gapTb = MakeTrackbar(parent, isBall ? IDC_BALL_GAP_TB : IDC_TB_GAP_TB,
                            0, 48, 1, page, RG(gapTb));
    ui.gapVal = MakeLabel(parent, L"", isBall ? IDC_BALL_GAP_VAL : IDC_TB_GAP_VAL,
                          page, RG(gapVal), SS_RIGHT | SS_CENTERIMAGE);

    if (isBall) {
        ui.wLbl = MakeLabel(parent, L"宽", IDC_LBL_BALL_W, page, RG(wLbl),
                            SS_LEFT | SS_CENTERIMAGE);
        ui.wTb = MakeTrackbar(parent, IDC_BALL_W_TB, 40, 400, 2, page, RG(wTb));
        ui.wVal = MakeLabel(parent, L"", IDC_BALL_W_VAL, page, RG(wVal),
                            SS_RIGHT | SS_CENTERIMAGE);

        ui.hLbl = MakeLabel(parent, L"高", IDC_LBL_BALL_H, page, RG(hLbl),
                            SS_LEFT | SS_CENTERIMAGE);
        ui.hTb = MakeTrackbar(parent, IDC_BALL_H_TB, 40, 400, 2, page, RG(hTb));
        ui.hVal = MakeLabel(parent, L"", IDC_BALL_H_VAL, page, RG(hVal),
                            SS_RIGHT | SS_CENTERIMAGE);
    }

    ui.shapeLbl = MakeLabel(parent, L"形状", isBall ? IDC_LBL_BALL_SHAPE : IDC_LBL_TB_SHAPE,
                            page, RG(shapeLbl), SS_LEFT | SS_CENTERIMAGE);

    static const wchar_t* shapeNames[3] = { L"圆", L"圆角方", L"胶囊" };
    for (int i = 0; i < shapeCount; ++i) {
        const int id = isBall ? (IDC_BALL_SHAPE0 + i) : (IDC_TB_SHAPE0 + i);
        const wchar_t* name = isBall ? shapeNames[i] : (i == 0 ? L"圆角方" : L"胶囊");
        ui.shape[i] = MakeButton(parent, name, id, page, RGI(shapeBtn, i));
    }

    ui.colorLbl = MakeLabel(parent, L"颜色", isBall ? IDC_LBL_BALL_COLOR : IDC_LBL_TB_COLOR,
                            page, RG(colorLbl), SS_LEFT | SS_CENTERIMAGE);
    ui.colorBtn = MakeButton(parent, L"选择\u2026", isBall ? IDC_BALL_COLOR : IDC_TB_COLOR,
                             page, RG(colorBtn));
    ui.concLbl = MakeLabel(parent, L"浓度", isBall ? IDC_LBL_BALL_CONC : IDC_LBL_TB_CONC,
                           page, RG(concLbl), SS_LEFT | SS_CENTERIMAGE);
    ui.concTb = MakeTrackbar(parent, isBall ? IDC_BALL_CONC_TB : IDC_TB_CONC_TB,
                             0, 100, 5, page, RG(concTb));
    ui.concVal = MakeLabel(parent, L"", isBall ? IDC_BALL_CONC_VAL : IDC_TB_CONC_VAL,
                           page, RG(concVal), SS_RIGHT | SS_CENTERIMAGE);

    ui.matLbl = MakeLabel(parent, L"材质", isBall ? IDC_LBL_BALL_MAT : IDC_LBL_TB_MAT,
                          page, RG(matLbl), SS_LEFT | SS_CENTERIMAGE);
    for (int i = 0; i < 5; ++i) {
        const int id = (isBall ? IDC_BALL_MAT0 : IDC_TB_MAT0) + i;
        ui.mat[i] = MakeButton(parent, kMatNames[i], id, page, RGI(matBtn, i));
    }

    ui.blurLbl = MakeLabel(parent, L"模糊", isBall ? IDC_LBL_BALL_BLUR : IDC_LBL_TB_BLUR,
                           page, RG(blurLbl), SS_LEFT | SS_CENTERIMAGE);
    ui.blurTb = MakeTrackbar(parent, isBall ? IDC_BALL_BLUR_TB : IDC_TB_BLUR_TB,
                             0, 40, 1, page, RG(blurTb));
    ui.blurVal = MakeLabel(parent, L"", isBall ? IDC_BALL_BLUR_VAL : IDC_TB_BLUR_VAL,
                           page, RG(blurVal), SS_RIGHT | SS_CENTERIMAGE);

    ui.cornerLbl = MakeLabel(parent, L"圆角", isBall ? IDC_LBL_BALL_CORNER : IDC_LBL_TB_CORNER,
                             page, RG(cornerLbl), SS_LEFT | SS_CENTERIMAGE);
    ui.cornerTb = MakeTrackbar(parent, isBall ? IDC_BALL_CORNER_TB : IDC_TB_CORNER_TB,
                               0, 40, 1, page, RG(cornerTb));
    ui.cornerVal = MakeLabel(parent, L"", isBall ? IDC_BALL_CORNER_VAL : IDC_TB_CORNER_VAL,
                             page, RG(cornerVal), SS_RIGHT | SS_CENTERIMAGE);

    ui.autoChk = MakeButton(parent, L"自动对比",
                            isBall ? IDC_BALL_AUTOTEXT : IDC_TB_AUTOTEXT,
                            page, RG(autoChk));
    ui.textColorBtn = MakeButton(parent, L"文字颜色",
                                 isBall ? IDC_BALL_TEXTCOLOR : IDC_TB_TEXTCOLOR,
                                 page, RG(textColorBtn));
    ui.borderChk = MakeButton(parent, L"描边",
                              isBall ? IDC_BALL_BORDER : IDC_TB_BORDER,
                              page, RG(borderChk));
    ui.sheenChk = MakeButton(parent, L"高光",
                             isBall ? IDC_BALL_SHEEN : IDC_TB_SHEEN,
                             page, RG(sheenChk));
    ui.sysAcrylicChk = MakeButton(parent, L"系统实时亚克力（实验性）",
                                  isBall ? IDC_BALL_SYSACRYLIC : IDC_TB_SYSACRYLIC,
                                  page, RG(sysAcrylicChk));
}

#undef RG
#undef RGI

static void CreateControls(HWND parent)
{
    static const wchar_t* tabNames[3] = { L"任务栏", L"悬浮球", L"数据" };
    for (int i = 0; i < 3; ++i) {
        const int idx = i;
        g.tab[i] = MakeButton(parent, tabNames[i], IDC_TAB0 + i, -1,
                              [idx](const Geo& geo) { return geo.tab[idx]; });
    }

    CreateOneSurface(parent, 0, false, 2);
    CreateOneSurface(parent, 1, true,  3);

    // ---- 数据页 ----
    {
        auto getRefreshLbl = [](const Geo& geo) { return geo.refreshLbl; };
        auto getRefresh    = [](const Geo& geo) { return geo.refreshCombo; };
        auto getTempLbl    = [](const Geo& geo) { return geo.tempSrcLbl; };

        MakeLabel(parent, L"刷新频率", IDC_LBL_REFRESH, PAGE_DATA,
                  getRefreshLbl, SS_LEFT | SS_CENTERIMAGE);

        static const wchar_t* refItems[4] = { L"500 ms", L"1000 ms", L"2000 ms", L"3000 ms" };
        g.refresh = MakeCombo(parent, IDC_REFRESH, refItems, 4, PAGE_DATA, getRefresh);

        MakeLabel(parent, L"温度数据源", IDC_LBL_TEMPSRC, PAGE_DATA,
                  getTempLbl, SS_LEFT | SS_CENTERIMAGE);
        for (int i = 0; i < 5; ++i) {
            const int idx = i;
            g.tempSrc[i] = MakeButton(parent, kTempSrcNames[i], IDC_TEMPSRC0 + i, PAGE_DATA,
                                      [idx](const Geo& geo) { return geo.tempSrcBtn[idx]; });
        }

        g.gpuInfo = MakeEdit(parent, IDC_GPUINFO, PAGE_DATA,
                             [](const Geo& geo) {
                                 RECT r = geo.gpuInfo;
                                 InflateRect(&r, -SD(g.dpi, 1), -SD(g.dpi, 1));
                                 return r;
                             });

        g.rescanBtn = MakeButton(parent, L"重新探测", IDC_RESCAN, PAGE_DATA,
                                 [](const Geo& geo) { return geo.rescanBtn; });
    }

    g.btnReset  = MakeButton(parent, L"恢复默认", IDC_RESET, -1,
                             [](const Geo& geo) { return geo.btnReset; });
    g.btnCancel = MakeButton(parent, L"取消", IDC_CANCEL, -1,
                             [](const Geo& geo) { return geo.btnCancel; });
    g.btnSave   = MakeButton(parent, L"保存", IDC_SAVE, -1,
                             [](const Geo& geo) { return geo.btnSave; });
}

// ----------------------------------------------------------------- 颜色选择
static bool PickColor(HWND owner, COLORREF* io)
{
    if (!io) return false;
    typedef BOOL (WINAPI* PFN_ChooseColorW)(LPCHOOSECOLORW);
    static PFN_ChooseColorW fn = NULL;
    static bool tried = false;
    if (!tried) {
        tried = true;
        HMODULE h = LoadLibraryW(L"comdlg32.dll");
        if (h) fn = (PFN_ChooseColorW)(void*)GetProcAddress(h, "ChooseColorW");
    }
    if (!fn) return false;

    static COLORREF customs[16] = {
        kPresets[0], kPresets[1], kPresets[2], kPresets[3],
        kPresets[4], kPresets[5], kPresets[6], kPresets[7]
    };
    CHOOSECOLORW cc;
    ZeroMemory(&cc, sizeof(cc));
    cc.lStructSize = sizeof(cc);
    cc.hwndOwner = owner;
    cc.rgbResult = *io;
    cc.lpCustColors = customs;
    cc.Flags = CC_FULLOPEN | CC_RGBINIT;
    if (fn(&cc)) {
        *io = cc.rgbResult;
        return true;
    }
    return false;
}

// ----------------------------------------------------------------- 同步控件
static void RefreshAll()
{
    if (!g.hwnd || !IsWindow(g.hwnd)) return;

    for (int s = 0; s < 2; ++s) {
        SurfaceCtrls& ui = g.ui[s];
        const int mat = ClampInt(MatOf(g.cfg, s), 0, 4);

        SendMessageW(ui.fontTb, TBM_SETPOS, TRUE, ClampInt(FontOf(g.cfg, s), 0, 48));
        SendMessageW(ui.lineTb, TBM_SETPOS, TRUE, ClampInt(LineOf(g.cfg, s), 100, 220));
        SendMessageW(ui.gapTb,  TBM_SETPOS, TRUE, ClampInt(GapOf(g.cfg, s), 0, 48));
        SendMessageW(ui.concTb, TBM_SETPOS, TRUE, ClampInt(ConcOf(g.cfg, s), 0, 100));
        SendMessageW(ui.blurTb, TBM_SETPOS, TRUE, ClampInt(BlurOf(g.cfg, s), 0, 40));
        SendMessageW(ui.cornerTb, TBM_SETPOS, TRUE, ClampInt(CornerOf(g.cfg, s), 0, 40));

        if (s == 1) {
            SendMessageW(ui.wTb, TBM_SETPOS, TRUE, ClampInt(g.cfg.ballW, 40, 400));
            SendMessageW(ui.hTb, TBM_SETPOS, TRUE, ClampInt(g.cfg.ballH, 40, 400));
        }

        for (int i = 0; i < 5; ++i) {
            if (ui.mat[i]) InvalidateRect(ui.mat[i], NULL, TRUE);
        }
        for (int i = 0; i < 3; ++i) {
            if (ui.shape[i]) InvalidateRect(ui.shape[i], NULL, TRUE);
        }
        InvalidateRect(ui.autoChk, NULL, TRUE);
        InvalidateRect(ui.textColorBtn, NULL, TRUE);
        InvalidateRect(ui.borderChk, NULL, TRUE);
        InvalidateRect(ui.sheenChk, NULL, TRUE);
        InvalidateRect(ui.sysAcrylicChk, NULL, TRUE);

        EnableWindow(ui.textColorBtn, TextAutoOf(g.cfg, s) ? FALSE : TRUE);
        EnableWindow(ui.blurTb, (mat == 0 || mat == 4) ? FALSE : TRUE);
        EnableWindow(ui.concTb, (mat == 4) ? FALSE : TRUE);
        EnableWindow(ui.sysAcrylicChk,
                     (mat == 1 && glass::acrylicAvailable()) ? TRUE : FALSE);
    }

    int idx = 1;
    for (int i = 0; i < 4; ++i) {
        if (g.cfg.refreshMs == kRefreshSteps[i]) { idx = i; break; }
        if (g.cfg.refreshMs > kRefreshSteps[i]) idx = i;
    }
    if (g.refresh) SendMessageW(g.refresh, CB_SETCURSEL, (WPARAM)idx, 0);

    for (int i = 0; i < 5; ++i) {
        if (g.tempSrc[i]) InvalidateRect(g.tempSrc[i], NULL, TRUE);
    }

    UpdateListButtons();
    UpdateValueLabels();
    FillGpuInfo();
    SpecToList(g.surf[0].spec, g.ui[0].list, &g.surf[0].sel);
    SpecToList(g.surf[1].spec, g.ui[1].list, &g.surf[1].sel);

    InvalidateRect(g.hwnd, NULL, FALSE);
    HWND all[] = { g.ui[0].showChk, g.ui[1].showChk,
                   g.ui[0].btn[0], g.ui[0].btn[1], g.ui[0].btn[2], g.ui[0].btn[3], g.ui[0].btn[4], g.ui[0].btn[5],
                   g.ui[1].btn[0], g.ui[1].btn[1], g.ui[1].btn[2], g.ui[1].btn[3], g.ui[1].btn[4], g.ui[1].btn[5],
                   g.btnReset, g.btnCancel, g.btnSave };
    for (size_t i = 0; i < sizeof(all) / sizeof(all[0]); ++i) {
        if (all[i]) InvalidateRect(all[i], NULL, TRUE);
    }
}

static void Relayout()
{
    RECT rc;
    if (!g.hwnd || !GetClientRect(g.hwnd, &rc)) return;
    g.geo = ComputeGeo(g.dpi, (int)rc.right, (int)rc.bottom);
    ApplyPage();
    RefreshAll();
}

// ----------------------------------------------------------------- 保存 / 重置
static void ApplyAndClose()
{
    if (g.hwnd) KillTimer(g.hwnd, 43);
    AppConfig c = g.cfg;
    layout_model::clampConfig(&c, layout_model::SURF_TASKBAR);
    layout_model::clampConfig(&c, layout_model::SURF_BALL);

    int best = kRefreshSteps[0];
    for (int i = 0; i < 4; ++i) {
        int d = c.refreshMs - kRefreshSteps[i];
        if (d < 0) d = -d;
        int bd = c.refreshMs - best;
        if (bd < 0) bd = -bd;
        if (d < bd) best = kRefreshSteps[i];
    }
    c.refreshMs = best;

    SyncSpecToCfg();
    lstrcpynW(c.tbLayout, g.cfg.tbLayout, NETBALL_SPEC_CCH);
    lstrcpynW(c.ballLayout, g.cfg.ballLayout, NETBALL_SPEC_CCH);
    g.cfg = c;

    if (g.onApply) g.onApply(&c, g.user);
    DestroyWindow(g.hwnd);
}

static void ResetToDefaults()
{
    g.cfg = g.defaults;
    g.surf[0].sel = 0;
    g.surf[1].sel = 0;
    layout_model::parse(g.cfg.tbLayout, NETBALL_MAX_TB_ROWS, &g.surf[0].spec);
    layout_model::parse(g.cfg.ballLayout, NETBALL_MAX_ITEMS, &g.surf[1].spec);
    RefreshAll();
    SchedulePreview();
}

// ----------------------------------------------------------------- 窗口过程
static LRESULT CALLBACK SettingsProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_CREATE: {
        g.hwnd = hwnd;
        g.dpi = g_pendingDpi ? g_pendingDpi : DpiOf(g.owner ? g.owner : hwnd);
        g.geo = ComputeGeo(g.dpi, SD(g.dpi, 520), SD(g.dpi, 680));

        {
            RECT wr = {0, 0, SD(g.dpi, 520), SD(g.dpi, 680)};
            AdjustWindowRectEx(&wr, (DWORD)GetWindowLongPtrW(hwnd, GWL_STYLE), FALSE,
                               (DWORD)GetWindowLongPtrW(hwnd, GWL_EXSTYLE));
            SetWindowPos(hwnd, NULL, 0, 0, wr.right - wr.left, wr.bottom - wr.top,
                         SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        }

        g.font     = MakeFont(g.dpi, 9, false);
        g.fontBold = MakeFont(g.dpi, 9, true);

        HICON ico = (HICON)LoadImageW(g.inst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON,
                                      0, 0, LR_DEFAULTSIZE | LR_SHARED);
        if (ico) {
            SendMessageW(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)ico);
            SendMessageW(hwnd, WM_SETICON, ICON_BIG, (LPARAM)ico);
        }

        CreateControls(hwnd);
        ApplyPage();
        RefreshAll();
        return 0;
    }
    case WM_GETMINMAXINFO: {
        MINMAXINFO* mmi = (MINMAXINFO*)lParam;
        const int dpi = g.dpi ? g.dpi : 96;
        RECT r = {0, 0, SD(dpi, 520), SD(dpi, 600)};
        AdjustWindowRectEx(&r, (DWORD)GetWindowLongPtrW(hwnd, GWL_STYLE), FALSE,
                           (DWORD)GetWindowLongPtrW(hwnd, GWL_EXSTYLE));
        if (mmi) {
            mmi->ptMinTrackSize.x = r.right - r.left;
            mmi->ptMinTrackSize.y = r.bottom - r.top;
        }
        return 0;
    }
    case WM_SIZE:
        if (wParam == SIZE_MINIMIZED) break;
        if (g.hwnd == hwnd && g.ctrlCount > 0) Relayout();
        InvalidateRect(hwnd, NULL, TRUE);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);
        if (rc.right > 0 && rc.bottom > 0) {
            HDC mem = CreateCompatibleDC(dc);
            HBITMAP bmp = CreateCompatibleBitmap(dc, rc.right, rc.bottom);
            HGDIOBJ old = SelectObject(mem, bmp);
            PaintAll(mem, rc);
            BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
            SelectObject(mem, old);
            DeleteObject(bmp);
            DeleteDC(mem);
        }
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_CTLCOLORSTATIC: {
        HDC dc = (HDC)wParam;
        const bool isGpu = ((HWND)lParam == g.gpuInfo);
        SetTextColor(dc, kText);
        SetBkColor(dc, isGpu ? kPanelBg : kDlgBg);
        static HBRUSH brDlg   = CreateSolidBrush(kDlgBg);
        static HBRUSH brPanel = CreateSolidBrush(kPanelBg);
        return (LRESULT)(isGpu ? brPanel : brDlg);
    }
    case WM_CTLCOLOREDIT: {
        HDC dc = (HDC)wParam;
        SetTextColor(dc, kText);
        SetBkColor(dc, kPanelBg);
        static HBRUSH brEdit = CreateSolidBrush(kPanelBg);
        return (LRESULT)brEdit;
    }
    case WM_CTLCOLORLISTBOX: {
        HDC dc = (HDC)wParam;
        SetTextColor(dc, kText);
        SetBkColor(dc, kPanelBg);
        static HBRUSH br = CreateSolidBrush(kPanelBg);
        return (LRESULT)br;
    }
    case WM_DRAWITEM: {
        const DRAWITEMSTRUCT* dis = (const DRAWITEMSTRUCT*)lParam;
        if (dis && dis->CtlType == ODT_BUTTON) {
            DrawOwnButton(dis);
            return TRUE;
        }
        if (dis && dis->CtlType == ODT_LISTBOX) {
            const bool sel = (dis->itemState & ODS_SELECTED) != 0;
            RECT r = dis->rcItem;
            HBRUSH bg = CreateSolidBrush(sel ? Blend(kPanelBg, kAccent, 26) : kPanelBg);
            FillRect(dis->hDC, &r, bg);
            DeleteObject(bg);
            if (sel) {
                RECT bar = r;
                bar.right = bar.left + SD(g.dpi, 2);
                HBRUSH ab = CreateSolidBrush(kAccent);
                FillRect(dis->hDC, &bar, ab);
                DeleteObject(ab);
            }
            wchar_t buf[192] = {0};
            if (dis->itemID != (UINT)-1) {
                SendMessageW(dis->hwndItem, LB_GETTEXT, (WPARAM)dis->itemID, (LPARAM)buf);
            }
            SetBkMode(dis->hDC, TRANSPARENT);
            SetTextColor(dis->hDC, sel ? kText : RGB(205, 210, 220));
            SelectObject(dis->hDC, g.font);
            r.left += SD(g.dpi, 8);
            DrawTextW(dis->hDC, buf, -1, &r, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            if (dis->itemState & ODS_FOCUS) DrawFocusRect(dis->hDC, &dis->rcItem);
            return TRUE;
        }
        break;
    }
    case WM_HSCROLL: {
        HWND ctl = (HWND)lParam;
        if (!ctl) break;
        const int id = GetDlgCtrlID(ctl);
        const int v = (int)SendMessageW(ctl, TBM_GETPOS, 0, 0);

        switch (id) {
        case IDC_TB_FONT_TB: g.cfg.tbFontPx = (v <= 0) ? 0 : ClampInt(v, 10, 48); break;
        case IDC_TB_LINE_TB: g.cfg.tbLinePct = ClampInt(v, 100, 200); break;
        case IDC_TB_GAP_TB:  g.cfg.tbItemGap = ClampInt(v, 0, 48); break;

        case IDC_BALL_FONT_TB:
            g.cfg.ballFontPx = (v <= 0) ? 0 : ClampInt(v, 8, 48);
            ClampBallFontAndSize();
            break;
        case IDC_BALL_LINE_TB: g.cfg.ballLinePct = ClampInt(v, 100, 200); break;
        case IDC_BALL_GAP_TB:  g.cfg.ballItemGap = ClampInt(v, 0, 48); break;
        case IDC_BALL_W_TB:
            g.cfg.ballW = ClampInt(v, 40, 400);
            ClampBallFontAndSize();
            break;
        case IDC_BALL_H_TB:
            g.cfg.ballH = ClampInt(v, 40, 400);
            ClampBallFontAndSize();
            break;

        case IDC_TB_CONC_TB:   g.cfg.tbTintAlpha = ClampInt(v, 0, 100); break;
        case IDC_BALL_CONC_TB: g.cfg.ballTintAlpha = ClampInt(v, 0, 100); break;
        case IDC_TB_BLUR_TB:   g.cfg.tbBlur = ClampInt(v, 0, 40); break;
        case IDC_BALL_BLUR_TB: g.cfg.ballBlur = ClampInt(v, 0, 40); break;
        case IDC_TB_CORNER_TB: g.cfg.tbCorner = ClampInt(v, 0, 40); break;
        case IDC_BALL_CORNER_TB: g.cfg.ballCorner = ClampInt(v, 0, 40); break;
        default: break;
        }

        SendMessageW(g.ui[1].fontTb, TBM_SETPOS, TRUE, g.cfg.ballFontPx);
        SendMessageW(g.ui[1].wTb, TBM_SETPOS, TRUE, g.cfg.ballW);
        SendMessageW(g.ui[1].hTb, TBM_SETPOS, TRUE, g.cfg.ballH);
        UpdateValueLabels();
        SchedulePreview();
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }
    case WM_COMMAND: {
        const int id = LOWORD(wParam);
        const int code = HIWORD(wParam);

        if (id == IDC_TAB0 || id == IDC_TAB1 || id == IDC_TAB2) {
            if (code == BN_CLICKED) {
                g.page = id - IDC_TAB0;
                ApplyPage();
                RefreshAll();
            }
            return 0;
        }

        if (id == IDC_TB_SHOW || id == IDC_BALL_SHOW) {
            if (code == BN_CLICKED) {
                BOOL& v = (id == IDC_TB_SHOW) ? g.cfg.taskbarShow : g.cfg.showBall;
                v = v ? FALSE : TRUE;
                SchedulePreview();
                InvalidateRect((id == IDC_TB_SHOW) ? g.ui[0].showChk : g.ui[1].showChk, NULL, TRUE);
            }
            return 0;
        }

        if (id == IDC_TB_SHAPE0 || id == IDC_TB_SHAPE1 ||
            (id >= IDC_BALL_SHAPE0 && id <= IDC_BALL_SHAPE2)) {
            if (code == BN_CLICKED) {
                if (id == IDC_TB_SHAPE0 || id == IDC_TB_SHAPE1) {
                    g.cfg.tbShape = (id == IDC_TB_SHAPE0) ? 1 : 2;
                    for (int i = 0; i < 2; ++i) InvalidateRect(g.ui[0].shape[i], NULL, TRUE);
                } else {
                    const int newShape = id - IDC_BALL_SHAPE0;
                    g.cfg.ballShape = newShape;
                    // 胶囊必须比高度宽；方形胶囊在像素上是圆。
                    if (newShape == 2) {
                        if (g.cfg.ballH > 266) g.cfg.ballH = 266;
                        const int wantW = g.cfg.ballH * 3 / 2;
                        if (g.cfg.ballW < wantW) g.cfg.ballW = wantW;
                        if (g.cfg.ballW > 400) g.cfg.ballW = 400;
                    }
                    ClampBallFontAndSize();
                    SendMessageW(g.ui[1].wTb, TBM_SETPOS, TRUE, g.cfg.ballW);
                    SendMessageW(g.ui[1].hTb, TBM_SETPOS, TRUE, g.cfg.ballH);
                    SendMessageW(g.ui[1].fontTb, TBM_SETPOS, TRUE, g.cfg.ballFontPx);
                    UpdateValueLabels();
                    for (int i = 0; i < 3; ++i) InvalidateRect(g.ui[1].shape[i], NULL, TRUE);
                }
                SchedulePreview();
            }
            return 0;
        }

        if (id == IDC_TB_AUTOTEXT || id == IDC_BALL_AUTOTEXT) {
            if (code == BN_CLICKED) {
                const int s = (id == IDC_TB_AUTOTEXT) ? 0 : 1;
                BOOL& v = TextAutoOf(g.cfg, s);
                v = v ? FALSE : TRUE;
                EnableWindow(g.ui[s].textColorBtn, v ? FALSE : TRUE);
                SchedulePreview();
                InvalidateRect(g.ui[s].autoChk, NULL, TRUE);
                InvalidateRect(g.ui[s].textColorBtn, NULL, TRUE);
            }
            return 0;
        }

        if (id == IDC_TB_BORDER || id == IDC_BALL_BORDER ||
            id == IDC_TB_SHEEN || id == IDC_BALL_SHEEN) {
            if (code == BN_CLICKED) {
                const bool isTb = (id == IDC_TB_BORDER || id == IDC_TB_SHEEN);
                const int s = isTb ? 0 : 1;
                if (id == IDC_TB_BORDER || id == IDC_BALL_BORDER) {
                    BOOL& v = BorderOf(g.cfg, s);
                    v = v ? FALSE : TRUE;
                } else {
                    BOOL& v = SheenOf(g.cfg, s);
                    v = v ? FALSE : TRUE;
                }
                SchedulePreview();
                InvalidateRect((id == IDC_TB_BORDER || id == IDC_BALL_BORDER)
                                   ? g.ui[s].borderChk : g.ui[s].sheenChk, NULL, TRUE);
            }
            return 0;
        }

        if (id == IDC_TB_SYSACRYLIC || id == IDC_BALL_SYSACRYLIC) {
            if (code == BN_CLICKED) {
                const int s = (id == IDC_TB_SYSACRYLIC) ? 0 : 1;
                BOOL& v = SysAcrylicOf(g.cfg, s);
                v = v ? FALSE : TRUE;
                SchedulePreview();
                InvalidateRect(g.ui[s].sysAcrylicChk, NULL, TRUE);
            }
            return 0;
        }

        if (id == IDC_TB_COLOR || id == IDC_BALL_COLOR ||
            id == IDC_TB_TEXTCOLOR || id == IDC_BALL_TEXTCOLOR) {
            if (code == BN_CLICKED) {
                const bool isTb = (id == IDC_TB_COLOR || id == IDC_TB_TEXTCOLOR);
                const int s = isTb ? 0 : 1;
                COLORREF& target = (id == IDC_TB_COLOR || id == IDC_BALL_COLOR)
                                       ? TintOf(g.cfg, s) : TextColorOf(g.cfg, s);
                COLORREF c = target;
                if (PickColor(hwnd, &c)) {
                    target = c;
                    SchedulePreview();
                    InvalidateRect((id == IDC_TB_COLOR || id == IDC_BALL_COLOR)
                                       ? g.ui[s].colorBtn : g.ui[s].textColorBtn, NULL, TRUE);
                    InvalidateRect(hwnd, NULL, FALSE);
                }
            }
            return 0;
        }

        // 材质卡
        {
            const int s = (id >= IDC_TB_MAT0 && id <= IDC_TB_MAT4) ? 0
                        : (id >= IDC_BALL_MAT0 && id <= IDC_BALL_MAT4) ? 1 : -1;
            if (s >= 0 && code == BN_CLICKED) {
                const int base = (s == 0) ? IDC_TB_MAT0 : IDC_BALL_MAT0;
                MatOf(g.cfg, s) = id - base;
                SchedulePreview();
                RefreshAll();
                return 0;
            }
        }

        if (id == IDC_REFRESH) {
            if (code == CBN_SELCHANGE) {
                const int sel = (int)SendMessageW(g.refresh, CB_GETCURSEL, 0, 0);
                if (sel >= 0 && sel < 4) g.cfg.refreshMs = kRefreshSteps[sel];
                SchedulePreview();
            }
            return 0;
        }

        if (id >= IDC_TEMPSRC0 && id <= IDC_TEMPSRC4) {
            if (code == BN_CLICKED) {
                g.cfg.tempSrc = id - IDC_TEMPSRC0;
                SchedulePreview();
                FillGpuInfo();
                RefreshAll();
            }
            return 0;
        }

        if (id == IDC_RESCAN) {
            if (code == BN_CLICKED) {
                sensors::rescanGpus();
                SetTimer(hwnd, 42, 800, NULL);
                SetWindowTextW(g.gpuInfo, L"正在重新枚举显卡…");
            }
            return 0;
        }

        switch (id) {
        case IDC_TB_ADD:      if (code == BN_CLICKED) ShowAddMenu(PAGE_TB); return 0;
        case IDC_TB_DEL:      if (code == BN_CLICKED) ListOp(PAGE_TB, 1); return 0;
        case IDC_TB_UP:       if (code == BN_CLICKED) ListOp(PAGE_TB, 2); return 0;
        case IDC_TB_DOWN:     if (code == BN_CLICKED) ListOp(PAGE_TB, 3); return 0;
        case IDC_TB_ROWMINUS: if (code == BN_CLICKED) ListOp(PAGE_TB, 4); return 0;
        case IDC_TB_ROWPLUS:  if (code == BN_CLICKED) ListOp(PAGE_TB, 5); return 0;

        case IDC_BALL_ADD:      if (code == BN_CLICKED) ShowAddMenu(PAGE_BALL); return 0;
        case IDC_BALL_DEL:      if (code == BN_CLICKED) ListOp(PAGE_BALL, 1); return 0;
        case IDC_BALL_UP:       if (code == BN_CLICKED) ListOp(PAGE_BALL, 2); return 0;
        case IDC_BALL_DOWN:     if (code == BN_CLICKED) ListOp(PAGE_BALL, 3); return 0;
        case IDC_BALL_ROWMINUS: if (code == BN_CLICKED) ListOp(PAGE_BALL, 4); return 0;
        case IDC_BALL_ROWPLUS:  if (code == BN_CLICKED) ListOp(PAGE_BALL, 5); return 0;

        case IDC_TB_LIST:
            if (code == LBN_SELCHANGE) g.surf[0].sel = ListSel(g.ui[0].list);
            return 0;
        case IDC_BALL_LIST:
            if (code == LBN_SELCHANGE) g.surf[1].sel = ListSel(g.ui[1].list);
            return 0;

        case IDC_RESET:  if (code == BN_CLICKED) ResetToDefaults(); return 0;
        case IDC_CANCEL: if (code == BN_CLICKED) CancelAndClose(); return 0;
        case IDC_SAVE:   if (code == BN_CLICKED) ApplyAndClose(); return 0;
        default: break;
        }
        break;
    }
    case WM_TIMER:
        if (wParam == 42) {
            KillTimer(hwnd, 42);
            FillGpuInfo();
            return 0;
        }
        if (wParam == 43) {
            KillTimer(hwnd, 43);
            if (g.onPreview) g.onPreview(&g.cfg, g.user);
            return 0;
        }
        break;
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) {
            CancelAndClose();
            return 0;
        }
        break;
    case WM_DPICHANGED: {
        const int oldDpi = g.dpi ? g.dpi : 96;
        g.dpi = (int)HIWORD(wParam);
        g_pendingDpi = g.dpi;
        RECT* pr = (RECT*)lParam;
        if (g.font)     DeleteObject(g.font);
        if (g.fontBold) DeleteObject(g.fontBold);
        g.font     = MakeFont(g.dpi, 9, false);
        g.fontBold = MakeFont(g.dpi, 9, true);

        RECT cur = {0, 0, 0, 0};
        GetClientRect(hwnd, &cur);
        int dipW = MulDiv((int)cur.right, 96, oldDpi);
        int dipH = MulDiv((int)cur.bottom, 96, oldDpi);
        if (dipW < 520) dipW = 520;
        if (dipH < 680) dipH = 680;

        RECT wr = {0, 0, SD(g.dpi, dipW), SD(g.dpi, dipH)};
        AdjustWindowRectEx(&wr, (DWORD)GetWindowLongPtrW(hwnd, GWL_STYLE), FALSE,
                           (DWORD)GetWindowLongPtrW(hwnd, GWL_EXSTYLE));
        const int px = pr ? pr->left : 0;
        const int py = pr ? pr->top : 0;
        SetWindowPos(hwnd, NULL, px, py, wr.right - wr.left, wr.bottom - wr.top,
                     SWP_NOZORDER | SWP_NOACTIVATE);

        for (int i = 0; i < g.ctrlCount; ++i) {
            if (g.ctrls[i].hwnd && IsWindow(g.ctrls[i].hwnd)) {
                SendMessageW(g.ctrls[i].hwnd, WM_SETFONT, (WPARAM)g.font, TRUE);
            }
        }
        SendMessageW(g.ui[0].list, LB_SETITEMHEIGHT, 0, (LPARAM)SD(g.dpi, 26));
        SendMessageW(g.ui[1].list, LB_SETITEMHEIGHT, 0, (LPARAM)SD(g.dpi, 26));
        Relayout();
        return 0;
    }
    case WM_CLOSE:
        CancelAndClose();
        return 0;
    case WM_DESTROY: {
        HWND h = g.hwnd;
        if (h) KillTimer(h, 43);
        if (g.owner && IsWindow(g.owner)) {
            EnableWindow(g.owner, TRUE);
            SetForegroundWindow(g.owner);
        }
        if (g.font)     { DeleteObject(g.font);     g.font = NULL; }
        if (g.fontBold) { DeleteObject(g.fontBold); g.fontBold = NULL; }

        g = State();
        return 0;
    }
    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static void ApplyDarkTitleBar(HWND hwnd)
{
    if (!hwnd) return;
    BOOL dark = TRUE;
    // 20 = DWMWA_USE_IMMERSIVE_DARK_MODE (Win10 2004+/Win11), 19 = 旧版
    if (FAILED(DwmSetWindowAttribute(hwnd, 20, &dark, sizeof(dark)))) {
        DwmSetWindowAttribute(hwnd, 19, &dark, sizeof(dark));
    }
}

static bool EnsureClass(HINSTANCE inst)
{
    WNDCLASSEXW wc;
    ZeroMemory(&wc, sizeof(wc));
    if (GetClassInfoExW(inst, WND_CLASS_SETTINGS, &wc)) return true;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.style = 0;
    wc.lpfnWndProc = SettingsProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    wc.lpszClassName = WND_CLASS_SETTINGS;
    return RegisterClassExW(&wc) != 0;
}

} // namespace

// ============================================================================
// 契约实现：settings_ui
// ============================================================================

void settings_ui::open(HWND owner, HINSTANCE hInst, const AppConfig* cur,
                       settings_ui::PreviewFn onPreview,
                       settings_ui::ApplyFn onApply, void* user)
{
    if (g.hwnd && IsWindow(g.hwnd)) {
        ShowWindow(g.hwnd, SW_SHOWNORMAL);
        SetForegroundWindow(g.hwnd);
        return;
    }

    INITCOMMONCONTROLSEX icc;
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_BAR_CLASSES | ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES;
    InitCommonControlsEx(&icc);

    g = State();
    g.owner = owner;
    g.inst = hInst ? hInst : (HINSTANCE)GetModuleHandleW(NULL);
    g.onApply = onApply;
    g.onPreview = onPreview;
    g.user = user;
    g.page = PAGE_TB;
    g.surf[0].sel = 0;
    g.surf[1].sel = 0;

    cfg::resetToDefault(&g.defaults);
    g.cfg = cur ? *cur : g.defaults;
    layout_model::clampConfig(&g.cfg, layout_model::SURF_TASKBAR);
    layout_model::clampConfig(&g.cfg, layout_model::SURF_BALL);
    layout_model::parse(g.cfg.tbLayout, NETBALL_MAX_TB_ROWS, &g.surf[0].spec);
    layout_model::parse(g.cfg.ballLayout, NETBALL_MAX_ITEMS, &g.surf[1].spec);
    g.origin = g.cfg;

    if (!EnsureClass(g.inst)) return;

    int dpi = DpiOf(owner);
    if (dpi < 48 || dpi > 480) dpi = 96;
    g_pendingDpi = dpi;
    Geo geo = ComputeGeo(dpi, SD(dpi, 520), SD(dpi, 680));

    DWORD style = WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN | WS_THICKFRAME | WS_MAXIMIZEBOX;
    RECT wr;
    ZeroMemory(&wr, sizeof(wr));
    wr.right = geo.clientW;
    wr.bottom = geo.clientH;
    AdjustWindowRectEx(&wr, style, FALSE, 0);
    int w = wr.right - wr.left;
    int h = wr.bottom - wr.top;

    std::wstring title = std::wstring(NETBALL_APP_NAME) + L" - 设置";

    HWND hwnd = CreateWindowExW(0, WND_CLASS_SETTINGS, title.c_str(), style,
                                0, 0, w, h, owner, NULL, g.inst, NULL);
    if (!hwnd) {
        g = State();
        return;
    }

    ApplyDarkTitleBar(hwnd);

    RECT cr;
    GetClientRect(hwnd, &cr);
    RECT wr2;
    GetWindowRect(hwnd, &wr2);
    const int wantW = SD(dpi, 520);
    const int wantH = SD(dpi, 680);
    int dw = wantW - (cr.right - cr.left);
    int dh = wantH - (cr.bottom - cr.top);
    if (dw != 0 || dh != 0) {
        SetWindowPos(hwnd, NULL, 0, 0,
                     (wr2.right - wr2.left) + dw, (wr2.bottom - wr2.top) + dh,
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }

    // 优先放在悬浮球左侧/右侧，避免把球完全盖住
    RECT ref;
    ZeroMemory(&ref, sizeof(ref));
    if (owner && IsWindow(owner)) {
        GetWindowRect(owner, &ref);
    } else {
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &ref, 0);
    }

    GetWindowRect(hwnd, &wr2);
    const int ww = wr2.right - wr2.left;
    int wh = wr2.bottom - wr2.top;
    const int gap = SD(dpi, 12);

    MONITORINFO mi;
    ZeroMemory(&mi, sizeof(mi));
    mi.cbSize = sizeof(mi);
    HMONITOR mon = MonitorFromRect(&ref, MONITOR_DEFAULTTONEAREST);
    RECT wa = ref;
    if (mon && GetMonitorInfoW(mon, &mi)) wa = mi.rcWork;

    // 175% DPI 的 1080p 工作区只有 ~678 DIP：680 DIP 设计稿会顶出屏幕，
    // 这里按工作区高度收一次，WM_SIZE 会让页面按 600 DIP 下限重排。
    if (wa.bottom > wa.top && wh > (wa.bottom - wa.top)) {
        SetWindowPos(hwnd, NULL, 0, 0, ww, wa.bottom - wa.top,
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        GetWindowRect(hwnd, &wr2);
        wh = wr2.bottom - wr2.top;
    }

    int x = ref.left - ww - gap;
    if (x < wa.left) x = ref.right + gap;
    if (x + ww > wa.right) x = wa.left + ((wa.right - wa.left) - ww) / 2;
    int y = ref.top + ((ref.bottom - ref.top) - wh) / 2;

    if (x < wa.left) x = wa.left;
    if (x + ww > wa.right) x = wa.right - ww;
    if (y < wa.top + 8) y = wa.top + 8;
    if (y + wh > wa.bottom) y = wa.bottom - wh;
    SetWindowPos(hwnd, NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);

    if (owner && IsWindow(owner) && IsWindowEnabled(owner)) {
        EnableWindow(owner, FALSE);
    }

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
    SetForegroundWindow(hwnd);
    SetFocus(g.btnSave);
}

bool settings_ui::isOpen()
{
    return g.hwnd != NULL && IsWindow(g.hwnd) != FALSE;
}
