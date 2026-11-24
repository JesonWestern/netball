// src/config.cpp —— 配置持久化 + 开机自启（T3 / AC-9）
//
// 配置文件：%APPDATA%\NetBall\config.ini（不存在则递归建目录并写默认值）
// 自启：HKCU\Software\Microsoft\Windows\CurrentVersion\Run，"NetBall" = "<exe 全路径>"
//       —— 写 HKCU 不需要管理员权限；关闭时删除该值。
//
// 所有读入值都做合法性校验，非法值回落默认（不截断到边界，避免把损坏数据“洗白”）。

#include "config.h"
#include "layout_model.h"

#include <shlobj.h>
#include <cwchar>
#include <cwctype>
#include <cstdlib>
#include <string>

namespace {

const wchar_t* kSection   = L"NetBall";
const wchar_t* kRunSubKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";

const wchar_t* kKeyShape     = L"shape";
const wchar_t* kKeySize      = L"size";
const wchar_t* kKeyOpacity   = L"opacity";
const wchar_t* kKeyBgColor   = L"bgcolor";
const wchar_t* kKeyTextAuto  = L"textauto";
const wchar_t* kKeyTextColor = L"textcolor";
const wchar_t* kKeyShowNet   = L"shownet";
const wchar_t* kKeyShowMem   = L"showmem";
const wchar_t* kKeyRefresh   = L"refreshms";
const wchar_t* kKeyTopmost   = L"alwaysontop";
const wchar_t* kKeyAutoStart = L"autostart";
const wchar_t* kKeyPosX      = L"posx";
const wchar_t* kKeyPosY      = L"posy";
const wchar_t* kKeySavedW    = L"savedw";
const wchar_t* kKeySavedH    = L"savedh";

// v1.1.0：任务栏显示（v1.2.0 起 tbShowX 仅用于迁移与降级兼容）
const wchar_t* kKeyShowBall    = L"showball";
const wchar_t* kKeyTaskbarShow = L"taskbarshow";
const wchar_t* kKeyTbDown      = L"tbshowdown";
const wchar_t* kKeyTbUp        = L"tbshowup";
const wchar_t* kKeyTbMem       = L"tbshowmem";

// v1.2.0：排版 / 字体 / 尺寸 / 材质 / 数据源
const wchar_t* kKeyTbLayout     = L"tblayout";
const wchar_t* kKeyBallLayout   = L"balllayout";
const wchar_t* kKeyTbFont       = L"tbfont";
const wchar_t* kKeyTbLinePct    = L"tblinepct";
const wchar_t* kKeyTbItemGap    = L"tbitemgap";
const wchar_t* kKeyBallFont     = L"ballfont";
const wchar_t* kKeyBallLinePct  = L"balllinepct";
const wchar_t* kKeyBallItemGap  = L"ballitemgap";
const wchar_t* kKeyBallW        = L"ballw";
const wchar_t* kKeyBallH        = L"ballh";
const wchar_t* kKeyTbMat        = L"tbmat";
const wchar_t* kKeyBallMat      = L"ballmat";
const wchar_t* kKeyTbTint       = L"tbtint";
const wchar_t* kKeyTbTintAlpha  = L"tbtintalpha";
const wchar_t* kKeyBallTint     = L"balltint";
const wchar_t* kKeyBallTintAlpha= L"balltintalpha";
const wchar_t* kKeyMatBlur      = L"matblur";
const wchar_t* kKeyMatCorner    = L"matcorner";
const wchar_t* kKeyMatBorder    = L"matborder";
const wchar_t* kKeyMatShadow    = L"matshadow";
const wchar_t* kKeyTempSrc      = L"tempsrc";

// v1.4.0：分表面外观 / 新材质枚举（cfgver=2）
const wchar_t* kKeyCfgVer        = L"cfgver";
const wchar_t* kKeyTbMatV2       = L"tbmatv2";
const wchar_t* kKeyBallMatV2     = L"ballmatv2";
const wchar_t* kKeyTbShape       = L"tbshape";
const wchar_t* kKeyBallShape     = L"ballshape";
const wchar_t* kKeyTbBlur        = L"tbblur";
const wchar_t* kKeyBallBlur      = L"ballblur";
const wchar_t* kKeyTbCorner      = L"tbcorner";
const wchar_t* kKeyBallCorner    = L"ballcorner";
const wchar_t* kKeyTbBorder      = L"tbborder";
const wchar_t* kKeyBallBorder    = L"ballborder";
const wchar_t* kKeyTbSheen       = L"tbsheen";
const wchar_t* kKeyBallSheen     = L"ballsheen";
const wchar_t* kKeyTbTextAuto    = L"tbtextauto";
const wchar_t* kKeyBallTextAuto  = L"balltextauto";
const wchar_t* kKeyTbTextColor   = L"tbtextcolor";
const wchar_t* kKeyBallTextColor = L"balltextcolor";
const wchar_t* kKeyTbSysAcrylic  = L"tbsysacrylic";
const wchar_t* kKeyBallSysAcrylic= L"ballsysacrylic";

// v1.2 材质值：0 纯色 1 亚克力 2 毛玻璃 3 无底板
// v1.4 材质值：0 纯色 1 亚克力 2 玻璃 3 液态玻璃 4 无底板
int MapLegacyMat(int v)
{
    switch (v) {
    case 0: return 0;
    case 1: return 1;
    case 2: return 2;
    case 3: return 4;
    default: return 1;
    }
}

// 写回旧键时把新枚举压回 v1.2 能理解的三档（液态玻璃按玻璃、无底板按 3）
int LegacyMatForSave(int v)
{
    switch (v) {
    case 0: return 0;
    case 1: return 1;
    case 2: return 2;
    case 3: return 2;
    case 4: return 3;
    default: return 0;
    }
}

// ---------------------------------------------------------------- 路径与目录

// 逐级创建目录（含已存在的情况），失败静默。
void EnsureDirRecursive(const std::wstring& path)
{
    std::wstring cur;
    cur.reserve(path.size());
    for (size_t i = 0; i < path.size(); ++i) {
        cur.push_back(path[i]);
        if (path[i] == L'\\' || path[i] == L'/') {
            if (cur.size() > 3) {            // 跳过 "C:\" 这类根
                CreateDirectoryW(cur.c_str(), NULL);
            }
        }
    }
    CreateDirectoryW(path.c_str(), NULL);
}

std::wstring BuildConfigDir()
{
    wchar_t buf[MAX_PATH] = {0};
    bool ok = false;

    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, SHGFP_TYPE_CURRENT, buf))) {
        ok = (buf[0] != 0);
    }
    if (!ok) {
        DWORD n = GetEnvironmentVariableW(L"APPDATA", buf, MAX_PATH);
        ok = (n > 0 && n < MAX_PATH && buf[0] != 0);
    }
    if (!ok) {
        wchar_t tmp[MAX_PATH] = {0};
        if (GetTempPathW(MAX_PATH, tmp) > 0) {
            lstrcpynW(buf, tmp, MAX_PATH);
            ok = true;
        }
    }
    if (!ok) {
        return std::wstring(L".\\NetBall");
    }

    std::wstring dir(buf);
    while (!dir.empty() && (dir[dir.size() - 1] == L'\\' || dir[dir.size() - 1] == L'/')) {
        dir.erase(dir.size() - 1);
    }
    dir += L"\\NetBall";
    EnsureDirRecursive(dir);
    return dir;
}

std::wstring BuildConfigPath()
{
    return BuildConfigDir() + L"\\config.ini";
}

// ---------------------------------------------------------------- ini 读写

bool ReadIniString(const std::wstring& path, const wchar_t* key, std::wstring* out)
{
    wchar_t buf[512];
    buf[0] = 0;
    DWORD n = GetPrivateProfileStringW(kSection, key, L"", buf,
                                       (DWORD)(sizeof(buf) / sizeof(buf[0])), path.c_str());
    if (n == 0) {
        return false;
    }
    out->assign(buf, n);
    while (!out->empty() && iswspace((*out)[out->size() - 1])) {
        out->erase(out->size() - 1);
    }
    return !out->empty();
}

// 严格整数解析：整串必须是十进制整数（允许首尾空白）。
bool ReadIniInt(const std::wstring& path, const wchar_t* key, int* out)
{
    std::wstring s;
    if (!ReadIniString(path, key, &s)) {
        return false;
    }
    const wchar_t* p = s.c_str();
    wchar_t* end = NULL;
    long v = wcstol(p, &end, 10);
    if (end == p) {
        return false;
    }
    while (*end != 0 && iswspace(*end)) {
        ++end;
    }
    if (*end != 0) {
        return false;
    }
    *out = (int)v;
    return true;
}

bool ReadIniBool(const std::wstring& path, const wchar_t* key, BOOL def, BOOL* out)
{
    int v = 0;
    if (ReadIniInt(path, key, &v) && (v == 0 || v == 1)) {
        *out = (v != 0) ? TRUE : FALSE;
        return true;
    }
    *out = def;
    return false;
}

// 区间整数：缺失 / 非整数 / 越界一律回落默认（与既有「不截断」风格一致）
int ReadIniIntRange(const std::wstring& path, const wchar_t* key, int lo, int hi, int def)
{
    int v = 0;
    if (ReadIniInt(path, key, &v) && v >= lo && v <= hi) {
        return v;
    }
    return def;
}

// #RRGGBB <-> COLORREF
bool ParseHexColor(const std::wstring& s, COLORREF* out)
{
    if (s.size() != 7 || s[0] != L'#') {
        return false;
    }
    unsigned long v = 0;
    for (size_t i = 1; i < 7; ++i) {
        if (!iswxdigit(s[i])) {
            return false;
        }
        int d;
        wchar_t ch = s[i];
        if (ch >= L'0' && ch <= L'9')      d = (int)(ch - L'0');
        else if (ch >= L'a' && ch <= L'f') d = (int)(ch - L'a') + 10;
        else                               d = (int)(ch - L'A') + 10;
        v = (v << 4) | (unsigned long)d;
    }
    *out = RGB((int)((v >> 16) & 0xFF), (int)((v >> 8) & 0xFF), (int)(v & 0xFF));
    return true;
}

std::wstring FormatHexColor(COLORREF c)
{
    wchar_t buf[16];
    wsprintfW(buf, L"#%02X%02X%02X",
              (unsigned)GetRValue(c), (unsigned)GetGValue(c), (unsigned)GetBValue(c));
    return std::wstring(buf);
}

// ---------------------------------------------------------------- 注册表自启

bool ReadRunValue(std::wstring* out)
{
    HKEY hk = NULL;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunSubKey, 0, KEY_QUERY_VALUE, &hk) != ERROR_SUCCESS) {
        return false;
    }

    wchar_t buf[2 * MAX_PATH];
    DWORD cb   = (DWORD)sizeof(buf);
    DWORD type = 0;
    LONG rc = RegQueryValueExW(hk, NETBALL_RUN_KEY, NULL, &type, (LPBYTE)buf, &cb);
    RegCloseKey(hk);

    if (rc != ERROR_SUCCESS) {
        return false;
    }
    if (type != REG_SZ && type != REG_EXPAND_SZ) {
        return false;
    }

    size_t chars = (size_t)(cb / sizeof(wchar_t));
    if (chars > 0 && buf[chars - 1] == 0) {
        --chars;                      // 去掉结尾 NUL
    }
    if (chars == 0) {
        return false;
    }
    out->assign(buf, chars);
    return true;
}

} // namespace

// ================================================================ namespace cfg

std::wstring cfg::configPath()
{
    // 函数内 static：C++11 保证线程安全初始化，只解析一次路径
    static const std::wstring path = BuildConfigPath();
    return path;
}

void cfg::resetToDefault(AppConfig* c)
{
    if (c == NULL) {
        return;
    }
    c->shape         = 0;
    c->size          = 88;
    c->opacity       = 85;
    c->bgColor       = RGB(24, 28, 36);
    c->textAuto      = TRUE;
    c->textColor     = RGB(255, 255, 255);
    c->showNet       = TRUE;
    c->showMem       = TRUE;
    c->refreshMs     = 1000;
    c->alwaysOnTop   = TRUE;
    c->autoStart     = FALSE;
    c->posX          = -1;           // -1 = 未设置，由调用方放到主屏右下角
    c->posY          = -1;
    c->savedScreenW  = 0;
    c->savedScreenH  = 0;

    // v1.1.0：默认保留悬浮球，同时开启任务栏显示（升级后立即可见）
    c->showBall    = TRUE;
    c->taskbarShow = TRUE;
    c->tbShowDown  = TRUE;
    c->tbShowUp    = TRUE;
    c->tbShowMem   = FALSE;

    // v1.2.0：排版模型 / 字体 / 尺寸 / 材质 / 数据源（默认值集中在 layout_model 一处）
    layout_model::defaults(layout_model::SURF_TASKBAR, c);
    layout_model::defaults(layout_model::SURF_BALL, c);
}

void cfg::load(AppConfig* c)
{
    if (c == NULL) {
        return;
    }

    resetToDefault(c);

    const std::wstring path = configPath();

    if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
        save(c);                     // 首次运行：落盘默认值
        return;
    }

    // 关键字段缺失 —— 文件损坏或不是我们的文件，重建
    if (!ReadIniInt(path, kKeySize, &(c->size))) {
        resetToDefault(c);
        save(c);
        return;
    }
    // 上面已把 size 读进 c，范围校验在下面统一做
    AppConfig d;
    resetToDefault(&d);
    if (c->size < 48 || c->size > 160) {
        c->size = d.size;
    }

    int v = 0;

    if (ReadIniInt(path, kKeyShape, &v) && v >= 0 && v <= 2) {
        c->shape = v;
    } else {
        c->shape = d.shape;
    }

    if (ReadIniInt(path, kKeyOpacity, &v) && v >= 20 && v <= 100) {
        c->opacity = v;
    } else {
        c->opacity = d.opacity;
    }

    std::wstring s;
    if (ReadIniString(path, kKeyBgColor, &s)) {
        COLORREF cr;
        if (ParseHexColor(s, &cr)) {
            c->bgColor = cr;
        }
    }

    ReadIniBool(path, kKeyTextAuto, d.textAuto, &c->textAuto);

    if (ReadIniString(path, kKeyTextColor, &s)) {
        COLORREF cr;
        if (ParseHexColor(s, &cr)) {
            c->textColor = cr;
        }
    }

    ReadIniBool(path, kKeyShowNet, d.showNet, &c->showNet);
    ReadIniBool(path, kKeyShowMem, d.showMem, &c->showMem);

    if (ReadIniInt(path, kKeyRefresh, &v) && v >= 500 && v <= 3000) {
        c->refreshMs = v;
    } else {
        c->refreshMs = d.refreshMs;
    }

    ReadIniBool(path, kKeyTopmost, d.alwaysOnTop, &c->alwaysOnTop);

    // v1.1.0 新键：旧版 ini 没有这些键 → ReadIniBool 回落到默认值，旧配置不丢失
    ReadIniBool(path, kKeyShowBall,    d.showBall,    &c->showBall);
    ReadIniBool(path, kKeyTaskbarShow, d.taskbarShow, &c->taskbarShow);
    ReadIniBool(path, kKeyTbDown,      d.tbShowDown,  &c->tbShowDown);
    ReadIniBool(path, kKeyTbUp,        d.tbShowUp,    &c->tbShowUp);
    ReadIniBool(path, kKeyTbMem,       d.tbShowMem,   &c->tbShowMem);

    // ---------------------------------------------------------------- v1.2.0
    // 【迁移】旧版本没有 tblayout / balllayout：
    //   v1.1 的任务栏把三个开关拼成一行（中间是三空格）→ 迁移成「单行、项目按原顺序」；
    //   v1.1 的悬浮球是「网速两行 + 内存一行」→ 迁移成 down/up/mem 三行。
    std::wstring spec;
    if (ReadIniString(path, kKeyTbLayout, &spec)) {
        lstrcpynW(c->tbLayout, spec.c_str(), NETBALL_SPEC_CCH);
    } else {
        wchar_t legacy[NETBALL_SPEC_CCH] = {0};
        int pos = 0;
        struct { BOOL on; const wchar_t* tok; } legacyItems[3] = {
            { c->tbShowDown, L"down" },
            { c->tbShowUp,   L"up"   },
            { c->tbShowMem,  L"mem"  },
        };
        for (int i = 0; i < 3; ++i) {
            if (!legacyItems[i].on) continue;
            if (pos > 0) { legacy[pos++] = L','; legacy[pos] = 0; }
            lstrcpynW(legacy + pos, legacyItems[i].tok, NETBALL_SPEC_CCH - pos);
            pos = (int)wcslen(legacy);
        }
        if (pos > 0) lstrcpynW(c->tbLayout, legacy, NETBALL_SPEC_CCH);
        // pos == 0 时保留默认值（down,up），避免把「三项全关」误判成空面板
    }

    if (ReadIniString(path, kKeyBallLayout, &spec)) {
        lstrcpynW(c->ballLayout, spec.c_str(), NETBALL_SPEC_CCH);
    } else {
        wchar_t legacy[NETBALL_SPEC_CCH] = L"";
        if (c->showNet) lstrcpynW(legacy, L"down/up", NETBALL_SPEC_CCH);
        if (c->showMem) {
            if (legacy[0]) lstrcatW(legacy, L"/mem");
            else           lstrcpynW(legacy, L"mem", NETBALL_SPEC_CCH);
        }
        if (legacy[0]) lstrcpynW(c->ballLayout, legacy, NETBALL_SPEC_CCH);
    }

    c->tbFontPx      = ReadIniIntRange(path, kKeyTbFont,      0, 48,  d.tbFontPx);
    if (c->tbFontPx != 0 && c->tbFontPx < 10) c->tbFontPx = d.tbFontPx;
    c->tbLinePct     = ReadIniIntRange(path, kKeyTbLinePct, 100, 220, d.tbLinePct);
    c->tbItemGap     = ReadIniIntRange(path, kKeyTbItemGap,   0, 48,  d.tbItemGap);
    c->ballFontPx    = ReadIniIntRange(path, kKeyBallFont,    0, 48,  d.ballFontPx);
    if (c->ballFontPx != 0 && c->ballFontPx < 8) c->ballFontPx = d.ballFontPx;
    c->ballLinePct   = ReadIniIntRange(path, kKeyBallLinePct, 100, 220, d.ballLinePct);
    c->ballItemGap   = ReadIniIntRange(path, kKeyBallItemGap,   0, 48,  d.ballItemGap);

    // 悬浮球宽高：缺失时由 v1.1 的 size + shape 迁移（胶囊宽 = 高 × 3/2）
    {
        const int migratedH = c->size;
        const int migratedW = (c->shape == 2) ? (c->size * 3 / 2) : c->size;
        c->ballW = ReadIniIntRange(path, kKeyBallW, 40, 400, migratedW);
        c->ballH = ReadIniIntRange(path, kKeyBallH, 40, 400, migratedH);
    }

    // ---- v1.2 旧材质键（0 纯色 / 1 亚克力 / 2 毛玻璃 / 3 无底板）----
    const int legacyTbMat   = ReadIniIntRange(path, kKeyTbMat,   0, 3, LegacyMatForSave(d.tbMat));
    const int legacyBallMat = ReadIniIntRange(path, kKeyBallMat, 0, 3, LegacyMatForSave(d.ballMat));
    c->tbTintAlpha   = ReadIniIntRange(path, kKeyTbTintAlpha,   0, 100, d.tbTintAlpha);
    c->ballTintAlpha = ReadIniIntRange(path, kKeyBallTintAlpha, 0, 100, d.ballTintAlpha);
    c->matBlur       = ReadIniIntRange(path, kKeyMatBlur,   0, 40, d.matBlur);
    c->matCorner     = ReadIniIntRange(path, kKeyMatCorner, 0, 40, d.matCorner);
    c->tempSrc       = ReadIniIntRange(path, kKeyTempSrc,   0, 4,  d.tempSrc);

    ReadIniBool(path, kKeyMatBorder, d.matBorder, &c->matBorder);
    ReadIniBool(path, kKeyMatShadow, d.matShadow, &c->matShadow);

    if (ReadIniString(path, kKeyTbTint, &spec)) {
        COLORREF cr;
        if (ParseHexColor(spec, &cr)) c->tbTint = cr;
    }
    if (ReadIniString(path, kKeyBallTint, &spec)) {
        COLORREF cr;
        if (ParseHexColor(spec, &cr)) c->ballTint = cr;
    }

    // ---- v1.4.0：cfgver / 分表面外观 ----
    int cfgver = 1;
    ReadIniInt(path, kKeyCfgVer, &cfgver);

    if (cfgver < 2) {
        // 旧版：全局 shape / bgcolor / opacity / textauto / textcolor / mat* 迁移到两个表面
        c->tbMat   = MapLegacyMat(legacyTbMat);
        c->ballMat = MapLegacyMat(legacyBallMat);

        c->tbShape  = 1;                    // 任务栏旧版固定圆角方
        c->ballShape = c->shape;            // 旧 shape 只作用于悬浮球

        c->tbBlur    = c->matBlur;
        c->ballBlur  = c->matBlur;
        c->tbCorner  = c->matCorner;
        c->ballCorner = c->matCorner;
        c->tbBorder  = c->matBorder;
        c->ballBorder = c->matBorder;
        c->tbSheen   = c->matShadow;
        c->ballSheen = c->matShadow;

        c->tbTextAuto   = c->textAuto;
        c->ballTextAuto = c->textAuto;
        c->tbTextColor   = c->textColor;
        c->ballTextColor = c->textColor;

        c->tbSysAcrylic   = FALSE;
        c->ballSysAcrylic = FALSE;

        // 旧悬浮球纯色使用 bgcolor/opacity；合并进 ballTint/ballTintAlpha
        if (c->ballMat == 0 || c->ballMat == 4) {
            c->ballTint      = c->bgColor;
            c->ballTintAlpha = c->opacity;
        }
    } else {
        c->tbMat   = ReadIniIntRange(path, kKeyTbMatV2,   0, 4, MapLegacyMat(legacyTbMat));
        c->ballMat = ReadIniIntRange(path, kKeyBallMatV2, 0, 4, MapLegacyMat(legacyBallMat));

        c->tbShape   = ReadIniIntRange(path, kKeyTbShape,   1, 2, d.tbShape);
        c->ballShape = ReadIniIntRange(path, kKeyBallShape, 0, 2, c->shape);

        c->tbBlur    = ReadIniIntRange(path, kKeyTbBlur,   0, 40, c->matBlur);
        c->ballBlur  = ReadIniIntRange(path, kKeyBallBlur, 0, 40, c->matBlur);
        c->tbCorner  = ReadIniIntRange(path, kKeyTbCorner,   0, 40, c->matCorner);
        c->ballCorner = ReadIniIntRange(path, kKeyBallCorner, 0, 40, c->matCorner);

        ReadIniBool(path, kKeyTbBorder,   c->matBorder, &c->tbBorder);
        ReadIniBool(path, kKeyBallBorder, c->matBorder, &c->ballBorder);
        ReadIniBool(path, kKeyTbSheen,    c->matShadow, &c->tbSheen);
        ReadIniBool(path, kKeyBallSheen,  c->matShadow, &c->ballSheen);

        ReadIniBool(path, kKeyTbTextAuto,   c->textAuto, &c->tbTextAuto);
        ReadIniBool(path, kKeyBallTextAuto, c->textAuto, &c->ballTextAuto);
        ReadIniBool(path, kKeyTbSysAcrylic,   d.tbSysAcrylic,   &c->tbSysAcrylic);
        ReadIniBool(path, kKeyBallSysAcrylic, d.ballSysAcrylic, &c->ballSysAcrylic);

        std::wstring tc;
        if (ReadIniString(path, kKeyTbTextColor, &tc)) {
            COLORREF cr;
            if (ParseHexColor(tc, &cr)) c->tbTextColor = cr;
        } else {
            c->tbTextColor = c->textColor;
        }
        if (ReadIniString(path, kKeyBallTextColor, &tc)) {
            COLORREF cr;
            if (ParseHexColor(tc, &cr)) c->ballTextColor = cr;
        } else {
            c->ballTextColor = c->textColor;
        }
    }

    // 坐标：本层**只做语法校验**（能否解析成十进制 int），不做语义合法性判断。
    // 语义（是否落在虚拟屏内、是否还有 >=32px 露出）统一交给 ball_window 的
    // IsPositionUsable 一处判断——全工程只保留这一套坐标语义规则，不要出现
    // load / 窗口层 / applyConfig 各判各的第三、第四套。
    // 【哨兵语义】只有「posX == -1 且 posY == -1」才表示未设置（由调用方放到默认位置）；
    // 单独的 -1 是合法虚拟屏坐标（副屏在主屏左侧时会出现，例如 posX=-1, posY=300）。
    // 负数同样合法（虚拟屏坐标可以为负，例如副屏上的 posx=-500）。
    if (!ReadIniInt(path, kKeyPosX, &c->posX)) {
        c->posX = -1;                 // 缺失 / 非整数 → 未设置
    }
    if (!ReadIniInt(path, kKeyPosY, &c->posY)) {
        c->posY = -1;
    }

    if (ReadIniInt(path, kKeySavedW, &v) && v >= 0 && v <= 65535) {
        c->savedScreenW = v;
    } else {
        c->savedScreenW = 0;
    }
    if (ReadIniInt(path, kKeySavedH, &v) && v >= 0 && v <= 65535) {
        c->savedScreenH = v;
    } else {
        c->savedScreenH = 0;
    }

    // 自启状态以注册表为准（ini 里的值只是快照，注册表才是事实）
    c->autoStart = cfg::getAutoStart() ? TRUE : FALSE;
}

void cfg::save(const AppConfig* c)
{
    if (c == NULL) {
        return;
    }

    const std::wstring path = configPath();
    wchar_t num[32];

    struct IntField {
        const wchar_t* key;
        int            val;
    };
    const IntField ints[] = {
        // v1.1 旧键：始终写回，保证降级可用
        { kKeyShape,     c->ballShape   },
        { kKeySize,      c->size        },
        { kKeyOpacity,   c->ballTintAlpha },
        { kKeyTextAuto,  c->ballTextAuto ? 1 : 0 },
        { kKeyShowNet,   c->showNet   ? 1 : 0 },
        { kKeyShowMem,   c->showMem   ? 1 : 0 },
        { kKeyRefresh,   c->refreshMs   },
        { kKeyTopmost,   c->alwaysOnTop ? 1 : 0 },
        { kKeyAutoStart, c->autoStart ? 1 : 0 },
        { kKeyPosX,      c->posX        },
        { kKeyPosY,      c->posY        },
        { kKeySavedW,    c->savedScreenW },
        { kKeySavedH,    c->savedScreenH },
        { kKeyShowBall,    c->showBall    ? 1 : 0 },
        { kKeyTaskbarShow, c->taskbarShow ? 1 : 0 },
        // v1.1 旧键：由 v1.2 排版派生写回，保证降级到 v1.1 仍有合理表现
        { kKeyTbDown,      c->tbShowDown  ? 1 : 0 },
        { kKeyTbUp,        c->tbShowUp    ? 1 : 0 },
        { kKeyTbMem,       c->tbShowMem   ? 1 : 0 },

        // v1.2.0 旧键（降级兼容：液态玻璃按玻璃、无底板按 3）
        { kKeyTbMat,          LegacyMatForSave(c->tbMat) },
        { kKeyBallMat,        LegacyMatForSave(c->ballMat) },
        { kKeyMatBlur,        c->ballBlur      },
        { kKeyMatCorner,      c->ballCorner    },
        { kKeyMatBorder,      c->ballBorder ? 1 : 0 },
        { kKeyMatShadow,      c->ballSheen ? 1 : 0 },

        // v1.4.0 正式键
        { kKeyCfgVer,        2                    },
        { kKeyTbMatV2,       c->tbMat             },
        { kKeyBallMatV2,     c->ballMat           },
        { kKeyTbShape,       c->tbShape           },
        { kKeyBallShape,     c->ballShape         },
        { kKeyTbBlur,        c->tbBlur            },
        { kKeyBallBlur,      c->ballBlur          },
        { kKeyTbCorner,      c->tbCorner          },
        { kKeyBallCorner,    c->ballCorner        },
        { kKeyTbBorder,      c->tbBorder ? 1 : 0  },
        { kKeyBallBorder,    c->ballBorder ? 1 : 0 },
        { kKeyTbSheen,       c->tbSheen ? 1 : 0   },
        { kKeyBallSheen,     c->ballSheen ? 1 : 0 },
        { kKeyTbTextAuto,    c->tbTextAuto ? 1 : 0 },
        { kKeyBallTextAuto,  c->ballTextAuto ? 1 : 0 },
        { kKeyTbSysAcrylic,  c->tbSysAcrylic ? 1 : 0 },
        { kKeyBallSysAcrylic,c->ballSysAcrylic ? 1 : 0 },

        // v1.2.0 排版 / 尺寸 / 数据源
        { kKeyTbFont,         c->tbFontPx      },
        { kKeyTbLinePct,      c->tbLinePct     },
        { kKeyTbItemGap,      c->tbItemGap     },
        { kKeyBallFont,       c->ballFontPx    },
        { kKeyBallLinePct,    c->ballLinePct   },
        { kKeyBallItemGap,    c->ballItemGap   },
        { kKeyBallW,          c->ballW         },
        { kKeyBallH,          c->ballH         },
        { kKeyTbTintAlpha,    c->tbTintAlpha   },
        { kKeyBallTintAlpha,  c->ballTintAlpha },
        { kKeyTempSrc,        c->tempSrc       },
    };

    for (size_t i = 0; i < sizeof(ints) / sizeof(ints[0]); ++i) {
        wsprintfW(num, L"%d", ints[i].val);
        WritePrivateProfileStringW(kSection, ints[i].key, num, path.c_str());
    }

    // 排版 spec（写前归一化，避免把非法字符串落盘）
    {
        wchar_t buf[NETBALL_SPEC_CCH];
        layout_model::LayoutBlock tb;
        layout_model::parse(c->tbLayout, NETBALL_MAX_TB_ROWS, &tb);
        layout_model::format(tb, buf, NETBALL_SPEC_CCH);
        WritePrivateProfileStringW(kSection, kKeyTbLayout, buf, path.c_str());

        layout_model::LayoutBlock ball;
        layout_model::parse(c->ballLayout, NETBALL_MAX_ITEMS, &ball);
        layout_model::format(ball, buf, NETBALL_SPEC_CCH);
        WritePrivateProfileStringW(kSection, kKeyBallLayout, buf, path.c_str());

        // 旧键派生：与 v1.1 语义对齐（任务栏三项是否出现在排版里）
        const int legacy[3] = {
            layout_model::contains(tb, layout_model::ITEM_DOWN) ? 1 : 0,
            layout_model::contains(tb, layout_model::ITEM_UP)   ? 1 : 0,
            layout_model::contains(tb, layout_model::ITEM_MEM)  ? 1 : 0,
        };
        const wchar_t* legacyKeys[3] = { kKeyTbDown, kKeyTbUp, kKeyTbMem };
        for (int i = 0; i < 3; ++i) {
            wsprintfW(num, L"%d", legacy[i]);
            WritePrivateProfileStringW(kSection, legacyKeys[i], num, path.c_str());
        }

        const int ballLegacy[2] = {
            (layout_model::contains(ball, layout_model::ITEM_DOWN) ||
             layout_model::contains(ball, layout_model::ITEM_UP)) ? 1 : 0,
            layout_model::contains(ball, layout_model::ITEM_MEM) ? 1 : 0,
        };
        const wchar_t* ballLegacyKeys[2] = { kKeyShowNet, kKeyShowMem };
        for (int i = 0; i < 2; ++i) {
            wsprintfW(num, L"%d", ballLegacy[i]);
            WritePrivateProfileStringW(kSection, ballLegacyKeys[i], num, path.c_str());
        }
    }

    // 旧键取悬浮球值，保证降级后外观基本一致
    std::wstring bg = FormatHexColor(c->ballTint);
    WritePrivateProfileStringW(kSection, kKeyBgColor, bg.c_str(), path.c_str());

    std::wstring tc = FormatHexColor(c->ballTextColor);
    WritePrivateProfileStringW(kSection, kKeyTextColor, tc.c_str(), path.c_str());

    std::wstring tbt = FormatHexColor(c->tbTint);
    WritePrivateProfileStringW(kSection, kKeyTbTint, tbt.c_str(), path.c_str());

    std::wstring bat = FormatHexColor(c->ballTint);
    WritePrivateProfileStringW(kSection, kKeyBallTint, bat.c_str(), path.c_str());

    std::wstring tbtc = FormatHexColor(c->tbTextColor);
    WritePrivateProfileStringW(kSection, kKeyTbTextColor, tbtc.c_str(), path.c_str());

    std::wstring batc = FormatHexColor(c->ballTextColor);
    WritePrivateProfileStringW(kSection, kKeyBallTextColor, batc.c_str(), path.c_str());

    // 让注册表与 ini 保持一致（幂等；写 HKCU 无需提权）
    if ((c->autoStart ? true : false) != cfg::getAutoStart()) {
        cfg::setAutoStart(c->autoStart != FALSE);
    }
}

bool cfg::getAutoStart()
{
    std::wstring val;
    return ReadRunValue(&val);
}

void cfg::setAutoStart(bool on)
{
    if (!on) {
        HKEY hk = NULL;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunSubKey, 0, KEY_SET_VALUE, &hk) != ERROR_SUCCESS) {
            return;
        }
        RegDeleteValueW(hk, NETBALL_RUN_KEY);   // 不存在时返回 ERROR_FILE_NOT_FOUND，忽略
        RegCloseKey(hk);
        return;
    }

    wchar_t exe[2 * MAX_PATH];
    exe[0] = 0;
    DWORD n = GetModuleFileNameW(NULL, exe, (DWORD)(sizeof(exe) / sizeof(exe[0])));
    if (n == 0 || n >= (DWORD)(sizeof(exe) / sizeof(exe[0]))) {
        return;
    }

    std::wstring val = L"\"";
    val.append(exe, n);
    val += L"\"";

    HKEY hk = NULL;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRunSubKey, 0, NULL, 0,
                        KEY_SET_VALUE, NULL, &hk, NULL) != ERROR_SUCCESS) {
        return;
    }
    RegSetValueExW(hk, NETBALL_RUN_KEY, 0, REG_SZ,
                   (const BYTE*)val.c_str(),
                   (DWORD)((val.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(hk);
}
