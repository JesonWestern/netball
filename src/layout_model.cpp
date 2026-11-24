// src/layout_model.cpp —— 排版模型实现（v1.2.0）
//
// 本文件只做「数据 → 文本」与 spec 编解码，不碰 GDI+/窗口，方便单测与复用。
// 绘制相关（测量、行高、最小球体尺寸）在 text_render.cpp。
#include "layout_model.h"
#include "render.h"

#include <cwchar>
#include <cwctype>

namespace {

struct ItemDef {
    const wchar_t* token;
    const wchar_t* label;
};

// 顺序必须与 layout_model::ItemId 一一对应
const ItemDef kItems[layout_model::ITEM_COUNT] = {
    { L"down",     L"下行速度" },
    { L"up",       L"上行速度" },
    { L"mem",      L"内存占用" },
    { L"cpuload",  L"CPU 负载" },
    { L"cputemp",  L"CPU 温度" },
    { L"gpu0load", L"GPU0 负载" },
    { L"gpu0temp", L"GPU0 温度" },
    { L"gpu1load", L"GPU1 负载" },
    { L"gpu1temp", L"GPU1 温度" },
    { L"gpu2load", L"GPU2 负载" },
    { L"gpu2temp", L"GPU2 温度" },
    { L"gpu3load", L"GPU3 负载" },
    { L"gpu3temp", L"GPU3 温度" },
    // v1.4.2 追加（顺序必须与 ItemId 尾部一致）
    { L"cpupwr",   L"CPU 功率" },
    { L"gpu0pwr",  L"GPU0 功率" },
    { L"gpu1pwr",  L"GPU1 功率" },
    { L"gpu2pwr",  L"GPU2 功率" },
    { L"gpu3pwr",  L"GPU3 功率" },
};

bool TokenEqual(const wchar_t* a, const wchar_t* b)
{
    if (!a || !b) return false;
    while (*a && *b) {
        wchar_t ca = *a;
        wchar_t cb = *b;
        if (ca >= L'A' && ca <= L'Z') ca = (wchar_t)(ca - L'A' + L'a');
        if (cb >= L'A' && cb <= L'Z') cb = (wchar_t)(cb - L'A' + L'a');
        if (ca != cb) return false;
        ++a;
        ++b;
    }
    return *a == 0 && *b == 0;
}

void AddText(wchar_t* out, int cch, int* pos, const wchar_t* s)
{
    if (!out || !s || !pos) return;
    while (*s && *pos < cch - 1) {
        out[(*pos)++] = *s++;
    }
    out[*pos] = 0;
}

void AddInt(wchar_t* out, int cch, int* pos, int v)
{
    wchar_t buf[24];
    _snwprintf(buf, 23, L"%d", v);
    buf[23] = 0;
    AddText(out, cch, pos, buf);
}

// 指标无效时统一用 "--"，绝不输出 0 冒充真实值
void AddInvalid(wchar_t* out, int cch, int* pos)
{
    AddText(out, cch, pos, L"--");
}

// 毫瓦 → "16.5"（固定 1 位小数，四舍五入到 0.1 W）。
// 固定小数位是为了避免宽度在 9.9W / 10.0W 之间跳变导致排版抖动。
void AddWatts(wchar_t* out, int cch, int* pos, int milliwatts)
{
    if (milliwatts < 0) milliwatts = 0;
    const int deciW = (milliwatts + 50) / 100;      // 0.1 W 为单位，四舍五入
    wchar_t buf[24];
    _snwprintf(buf, 23, L"%d.%d", deciW / 10, deciW % 10);
    buf[23] = 0;
    AddText(out, cch, pos, buf);
}

bool GpuPrefix(int index, const Metrics* m, wchar_t* out, int cch)
{
    if (index < 0) index = 0;
    const int count = m ? m->gpuCount : 0;
    // 只有一张卡且就是 0 号时不带序号，省宽度
    if (count <= 1 && index == 0) {
        lstrcpynW(out, L"GPU ", cch);
        return true;
    }
    _snwprintf(out, (size_t)cch - 1, L"GPU%d ", index);
    out[cch - 1] = 0;
    return true;
}

} // namespace

// ============================================================================
// 注册表
// ============================================================================

int layout_model::itemCount()
{
    return (int)ITEM_COUNT;
}

const wchar_t* layout_model::itemToken(int id)
{
    if (id < 0 || id >= (int)ITEM_COUNT) return L"";
    return kItems[id].token;
}

const wchar_t* layout_model::itemLabel(int id)
{
    if (id < 0 || id >= (int)ITEM_COUNT) return L"";
    return kItems[id].label;
}

int layout_model::itemFromToken(const wchar_t* token)
{
    if (!token || !*token) return -1;
    for (int i = 0; i < (int)ITEM_COUNT; ++i) {
        if (TokenEqual(token, kItems[i].token)) return i;
    }
    return -1;
}

bool layout_model::itemGpuIndex(int id, int* outIndex, bool* outIsTemp)
{
    switch (id) {
    case ITEM_GPU0LOAD: if (outIndex) *outIndex = 0; if (outIsTemp) *outIsTemp = false; return true;
    case ITEM_GPU0TEMP: if (outIndex) *outIndex = 0; if (outIsTemp) *outIsTemp = true;  return true;
    case ITEM_GPU1LOAD: if (outIndex) *outIndex = 1; if (outIsTemp) *outIsTemp = false; return true;
    case ITEM_GPU1TEMP: if (outIndex) *outIndex = 1; if (outIsTemp) *outIsTemp = true;  return true;
    case ITEM_GPU2LOAD: if (outIndex) *outIndex = 2; if (outIsTemp) *outIsTemp = false; return true;
    case ITEM_GPU2TEMP: if (outIndex) *outIndex = 2; if (outIsTemp) *outIsTemp = true;  return true;
    case ITEM_GPU3LOAD: if (outIndex) *outIndex = 3; if (outIsTemp) *outIsTemp = false; return true;
    case ITEM_GPU3TEMP: if (outIndex) *outIndex = 3; if (outIsTemp) *outIsTemp = true;  return true;
    default: break;
    }
    return false;
}

// v1.4.2：功率项目单独判，避免动既有的 itemGpuIndex 语义
bool layout_model::itemGpuPowerIndex(int id, int* outIndex)
{
    switch (id) {
    case ITEM_GPU0PWR: if (outIndex) *outIndex = 0; return true;
    case ITEM_GPU1PWR: if (outIndex) *outIndex = 1; return true;
    case ITEM_GPU2PWR: if (outIndex) *outIndex = 2; return true;
    case ITEM_GPU3PWR: if (outIndex) *outIndex = 3; return true;
    default: break;
    }
    return false;
}

int layout_model::maxRows(Surface s)
{
    return (s == SURF_TASKBAR) ? NETBALL_MAX_TB_ROWS : NETBALL_MAX_ITEMS;
}

// ============================================================================
// spec 编解码
// ============================================================================

bool layout_model::contains(const LayoutBlock& b, int id)
{
    for (int i = 0; i < b.count; ++i) {
        if (b.item[i] == id) return true;
    }
    return false;
}

void layout_model::normalize(LayoutBlock* b, int maxRowCount)
{
    if (!b) return;

    if (maxRowCount < 1) maxRowCount = 1;
    if (maxRowCount > NETBALL_MAX_ITEMS) maxRowCount = NETBALL_MAX_ITEMS;

    if (b->count <= 0) {
        b->count = 0;
        b->rows  = 0;
        return;
    }
    if (b->count > NETBALL_MAX_ITEMS) b->count = NETBALL_MAX_ITEMS;

    // 1) 去重（保留首次出现，顺序不变）
    int n = 0;
    for (int i = 0; i < b->count; ++i) {
        const int id = b->item[i];
        if (id < 0 || id >= ITEM_COUNT) continue;
        bool dup = false;
        for (int j = 0; j < n; ++j) {
            if (b->item[j] == id) { dup = true; break; }
        }
        if (dup) continue;
        b->item[n]  = id;
        b->rowOf[n] = b->rowOf[i];
        ++n;
    }
    b->count = n;
    if (b->count == 0) { b->rows = 0; return; }

    // 2) 行号夹取（任务栏超过上限的项目并入最后一行，不丢项目）
    for (int i = 0; i < b->count; ++i) {
        if (b->rowOf[i] < 0) b->rowOf[i] = 0;
        if (b->rowOf[i] > maxRowCount - 1) b->rowOf[i] = maxRowCount - 1;
    }

    // 3) 行号压实：把实际用到的行按升序重映射到 0..k-1（空行随之消失）
    int used[NETBALL_MAX_ITEMS];
    int usedCount = 0;
    for (int i = 0; i < b->count; ++i) {
        bool found = false;
        for (int j = 0; j < usedCount; ++j) {
            if (used[j] == b->rowOf[i]) { found = true; break; }
        }
        if (!found) used[usedCount++] = b->rowOf[i];
    }
    for (int i = 1; i < usedCount; ++i) {
        const int v = used[i];
        int j = i - 1;
        while (j >= 0 && used[j] > v) { used[j + 1] = used[j]; --j; }
        used[j + 1] = v;
    }
    for (int i = 0; i < b->count; ++i) {
        for (int j = 0; j < usedCount; ++j) {
            if (used[j] == b->rowOf[i]) { b->rowOf[i] = j; break; }
        }
    }
    b->rows = usedCount;
}

bool layout_model::parse(const wchar_t* spec, int maxRowCount, LayoutBlock* out)
{
    if (!out) return false;

    LayoutBlock b;
    b.count = 0;
    b.rows  = 0;
    for (int i = 0; i < NETBALL_MAX_ITEMS; ++i) {
        b.item[i]  = -1;
        b.rowOf[i] = 0;
    }

    if (!spec || !*spec) {
        *out = b;
        return false;
    }

    int row = 0;
    const wchar_t* p = spec;
    while (*p && b.count < NETBALL_MAX_ITEMS) {
        if (*p == L',') { ++p; continue; }
        if (*p == L'/') { ++row; ++p; continue; }
        if (iswspace(*p)) { ++p; continue; }

        const wchar_t* s = p;
        while (*p && *p != L',' && *p != L'/') ++p;
        const size_t len = (size_t)(p - s);
        if (len > 0 && len < 24) {
            wchar_t tok[24];
            for (size_t i = 0; i < len; ++i) tok[i] = s[i];
            tok[len] = 0;
            // 去掉尾部空白
            size_t tail = len;
            while (tail > 0 && iswspace(tok[tail - 1])) tok[--tail] = 0;

            const int id = itemFromToken(tok);
            if (id >= 0 && !contains(b, id)) {
                b.item[b.count]  = id;
                b.rowOf[b.count] = row;
                ++b.count;
            }
        }
    }

    normalize(&b, maxRowCount);
    *out = b;
    return b.count > 0;
}

void layout_model::format(const LayoutBlock& b, wchar_t* out, int cch)
{
    if (!out || cch <= 0) return;
    out[0] = 0;

    int pos = 0;
    for (int r = 0; r < b.rows; ++r) {
        if (r > 0) AddText(out, cch, &pos, L"/");
        bool first = true;
        for (int i = 0; i < b.count; ++i) {
            if (b.rowOf[i] != r) continue;
            if (!first) AddText(out, cch, &pos, L",");
            AddText(out, cch, &pos, itemToken(b.item[i]));
            first = false;
        }
    }
    out[cch - 1] = 0;
}

// ============================================================================
// 项目文本
// ============================================================================

void layout_model::itemText(int id, const AppConfig* c, const Metrics* m, wchar_t* out, int cch)
{
    if (!out || cch <= 0) return;
    out[0] = 0;
    (void)c;   // 预留：单位/精度目前是固定文案，不随配置变化

    int pos = 0;

    switch (id) {
    case ITEM_DOWN: {
        AddText(out, cch, &pos, L"\u2193 ");              // ↓
        wchar_t buf[48] = L"--";
        if (m && m->netValid) render::formatSpeed(m->downBps, buf, 48);
        AddText(out, cch, &pos, buf);
        break;
    }
    case ITEM_UP: {
        AddText(out, cch, &pos, L"\u2191 ");              // ↑
        wchar_t buf[48] = L"--";
        if (m && m->netValid) render::formatSpeed(m->upBps, buf, 48);
        AddText(out, cch, &pos, buf);
        break;
    }
    case ITEM_MEM: {
        AddText(out, cch, &pos, L"\u5185\u5b58 ");        // 内存
        if (m && m->memTotalBytes > 0) {
            int v = m->memPercent;
            if (v < 0) v = 0;
            if (v > 100) v = 100;
            AddInt(out, cch, &pos, v);
            AddText(out, cch, &pos, L"%");
        } else {
            AddInvalid(out, cch, &pos);
        }
        break;
    }
    case ITEM_CPULOAD: {
        AddText(out, cch, &pos, L"CPU ");
        if (m && m->cpuPercent >= 0) {
            AddInt(out, cch, &pos, m->cpuPercent);
            AddText(out, cch, &pos, L"%");
        } else {
            AddInvalid(out, cch, &pos);
        }
        break;
    }
    case ITEM_CPUTEMP: {
        AddText(out, cch, &pos, L"CPU ");
        if (m && m->cpuTempValid) {
            AddInt(out, cch, &pos, m->cpuTempC);
            AddText(out, cch, &pos, L"\u00B0C");          // °C
        } else {
            AddInvalid(out, cch, &pos);
        }
        break;
    }
    case ITEM_CPUPWR: {
        // v1.4.2：CPU 封装功率。文案与温度同为 "CPU " 前缀，靠单位区分。
        AddText(out, cch, &pos, L"CPU ");
        if (m && m->cpuPwrValid) {
            AddWatts(out, cch, &pos, m->cpuPwrMw);
            AddText(out, cch, &pos, L"W");
        } else {
            AddInvalid(out, cch, &pos);
        }
        break;
    }
    default: {
        int gpuIx = 0;

        // v1.4.2：GPU 功率先判（它不是 load/temp，走不到 itemGpuIndex）
        if (itemGpuPowerIndex(id, &gpuIx)) {
            wchar_t prefix[24];
            GpuPrefix(gpuIx, m, prefix, 24);
            AddText(out, cch, &pos, prefix);
            const bool have = (m && gpuIx >= 0 && gpuIx < m->gpuCount && gpuIx < NETBALL_MAX_GPU);
            if (have && m->gpuPwrValid[gpuIx]) {
                AddWatts(out, cch, &pos, m->gpuPwrMw[gpuIx]);
                AddText(out, cch, &pos, L"W");
            } else {
                AddInvalid(out, cch, &pos);
            }
            break;
        }

        bool isTemp = false;
        if (!itemGpuIndex(id, &gpuIx, &isTemp)) {
            out[0] = 0;
            break;
        }
        wchar_t prefix[24];
        GpuPrefix(gpuIx, m, prefix, 24);
        AddText(out, cch, &pos, prefix);

        const bool haveGpu = (m && gpuIx >= 0 && gpuIx < m->gpuCount && gpuIx < NETBALL_MAX_GPU);
        if (!isTemp) {
            if (haveGpu && m->gpuLoad[gpuIx] >= 0) {
                AddInt(out, cch, &pos, m->gpuLoad[gpuIx]);
                AddText(out, cch, &pos, L"%");
            } else {
                AddInvalid(out, cch, &pos);
            }
        } else {
            if (haveGpu && m->gpuTempValid[gpuIx]) {
                AddInt(out, cch, &pos, m->gpuTempC[gpuIx]);
                AddText(out, cch, &pos, L"\u00B0C");
            } else {
                AddInvalid(out, cch, &pos);
            }
        }
        break;
    }
    }

    out[cch - 1] = 0;
}

// ============================================================================
// 默认值 / 校验
// ============================================================================

void layout_model::defaults(Surface s, AppConfig* c)
{
    if (!c) return;

    if (s == SURF_TASKBAR) {
        lstrcpynW(c->tbLayout, L"down,up", NETBALL_SPEC_CCH);
        c->tbFontPx    = 0;         // 自适应任务栏高度
        c->tbLinePct   = 118;
        c->tbItemGap   = 8;
        c->tbMat       = 1;         // 亚克力
        c->tbTint      = RGB(24, 27, 34);
        c->tbTintAlpha = 55;
        c->tbShape     = 1;         // 圆角方
        c->tbBlur      = 16;
        c->tbCorner    = 12;
        c->tbBorder    = TRUE;
        c->tbSheen     = TRUE;
        c->tbTextAuto  = TRUE;
        c->tbTextColor = RGB(255, 255, 255);
        c->tbSysAcrylic = FALSE;
    } else {
        lstrcpynW(c->ballLayout, L"down/up/mem", NETBALL_SPEC_CCH);
        c->ballFontPx    = 0;       // 自适应球体
        c->ballLinePct   = 118;
        c->ballItemGap   = 6;
        c->ballMat       = 0;       // 纯色（与 v1.1 一致）
        c->ballTint      = RGB(24, 28, 36);
        c->ballTintAlpha = 85;
        c->ballW         = 88;
        c->ballH         = 88;
        c->ballShape     = 0;       // 圆
        c->ballBlur      = 16;
        c->ballCorner    = 12;
        c->ballBorder    = TRUE;
        c->ballSheen     = TRUE;
        c->ballTextAuto  = TRUE;
        c->ballTextColor = RGB(255, 255, 255);
        c->ballSysAcrylic = FALSE;
    }

    // 共享（v1.2 旧字段，用于迁移/降级）
    c->matBlur   = 16;
    c->matCorner = 12;
    c->matBorder = TRUE;
    c->matShadow = TRUE;
    c->tempSrc   = 0;
}

// 空 spec 回落：配置被清空/损坏时按表面恢复默认排版，避免表面整块空白
static void ResetLayoutSpec(AppConfig* c, layout_model::Surface s)
{
    if (s == layout_model::SURF_TASKBAR) lstrcpynW(c->tbLayout, L"down,up", NETBALL_SPEC_CCH);
    else                                 lstrcpynW(c->ballLayout, L"down/up/mem", NETBALL_SPEC_CCH);
}

void layout_model::clampConfig(AppConfig* c, Surface s)
{
    if (!c) return;

    // ---- 公共字段（原先散落在 ball_window / taskbar_text / settings 三处）----
    if (c->shape < 0 || c->shape > 2) c->shape = 0;
    if (c->size < 48) c->size = 48;
    if (c->size > 160) c->size = 160;
    if (c->opacity < 20) c->opacity = 20;
    if (c->opacity > 100) c->opacity = 100;
    if (c->refreshMs < 500) c->refreshMs = 500;
    if (c->refreshMs > 3000) c->refreshMs = 3000;

    c->textAuto    = c->textAuto ? TRUE : FALSE;
    c->showNet     = c->showNet ? TRUE : FALSE;
    c->showMem     = c->showMem ? TRUE : FALSE;
    c->alwaysOnTop = c->alwaysOnTop ? TRUE : FALSE;
    c->autoStart   = c->autoStart ? TRUE : FALSE;
    c->showBall    = c->showBall ? TRUE : FALSE;
    c->taskbarShow = c->taskbarShow ? TRUE : FALSE;
    c->tbShowDown  = c->tbShowDown ? TRUE : FALSE;
    c->tbShowUp    = c->tbShowUp ? TRUE : FALSE;
    c->tbShowMem   = c->tbShowMem ? TRUE : FALSE;
    c->matBorder   = c->matBorder ? TRUE : FALSE;
    c->matShadow   = c->matShadow ? TRUE : FALSE;

    if (c->matBlur < 0) c->matBlur = 0;
    if (c->matBlur > 40) c->matBlur = 40;
    if (c->matCorner < 0) c->matCorner = 0;
    if (c->matCorner > 40) c->matCorner = 40;
    if (c->tempSrc < 0 || c->tempSrc > 4) c->tempSrc = 0;

    // ---- v1.4.0：分表面外观（两个表面都要校验，不能只校验当前表面）----
    if (c->tbShape != 1 && c->tbShape != 2) c->tbShape = 1;
    if (c->ballShape < 0 || c->ballShape > 2) c->ballShape = 0;
    if (c->tbBlur < 0) c->tbBlur = 0;
    if (c->tbBlur > 40) c->tbBlur = 40;
    if (c->ballBlur < 0) c->ballBlur = 0;
    if (c->ballBlur > 40) c->ballBlur = 40;
    if (c->tbCorner < 0) c->tbCorner = 0;
    if (c->tbCorner > 40) c->tbCorner = 40;
    if (c->ballCorner < 0) c->ballCorner = 0;
    if (c->ballCorner > 40) c->ballCorner = 40;
    c->tbBorder      = c->tbBorder ? TRUE : FALSE;
    c->ballBorder    = c->ballBorder ? TRUE : FALSE;
    c->tbSheen       = c->tbSheen ? TRUE : FALSE;
    c->ballSheen     = c->ballSheen ? TRUE : FALSE;
    c->tbTextAuto    = c->tbTextAuto ? TRUE : FALSE;
    c->ballTextAuto  = c->ballTextAuto ? TRUE : FALSE;
    c->tbSysAcrylic  = c->tbSysAcrylic ? TRUE : FALSE;
    c->ballSysAcrylic = c->ballSysAcrylic ? TRUE : FALSE;
    if (c->tbTintAlpha < 0) c->tbTintAlpha = 0;
    if (c->tbTintAlpha > 100) c->tbTintAlpha = 100;
    if (c->ballTintAlpha < 0) c->ballTintAlpha = 0;
    if (c->ballTintAlpha > 100) c->ballTintAlpha = 100;

    // ---- 表面相关字段：只校验当前表面 ----
    if (s == SURF_TASKBAR) {
        LayoutBlock b;
        parse(c->tbLayout, NETBALL_MAX_TB_ROWS, &b);
        if (b.count <= 0) {
            ResetLayoutSpec(c, SURF_TASKBAR);
            parse(c->tbLayout, NETBALL_MAX_TB_ROWS, &b);
        }
        format(b, c->tbLayout, NETBALL_SPEC_CCH);

        if (c->tbFontPx != 0) {
            if (c->tbFontPx < 10) c->tbFontPx = 10;
            if (c->tbFontPx > 48) c->tbFontPx = 48;
        }
        if (c->tbLinePct < 100) c->tbLinePct = 100;
        if (c->tbLinePct > 220) c->tbLinePct = 220;
        if (c->tbItemGap < 0) c->tbItemGap = 0;
        if (c->tbItemGap > 48) c->tbItemGap = 48;
        if (c->tbMat < 0 || c->tbMat > 4) c->tbMat = 1;
        if (c->tbTintAlpha < 0) c->tbTintAlpha = 0;
        if (c->tbTintAlpha > 100) c->tbTintAlpha = 100;
    } else {
        LayoutBlock b;
        parse(c->ballLayout, NETBALL_MAX_ITEMS, &b);
        if (b.count <= 0) {
            ResetLayoutSpec(c, SURF_BALL);
            parse(c->ballLayout, NETBALL_MAX_ITEMS, &b);
        }
        format(b, c->ballLayout, NETBALL_SPEC_CCH);

        if (c->ballFontPx != 0) {
            if (c->ballFontPx < 8) c->ballFontPx = 8;
            if (c->ballFontPx > 48) c->ballFontPx = 48;
        }
        if (c->ballLinePct < 100) c->ballLinePct = 100;
        if (c->ballLinePct > 220) c->ballLinePct = 220;
        if (c->ballItemGap < 0) c->ballItemGap = 0;
        if (c->ballItemGap > 48) c->ballItemGap = 48;
        if (c->ballMat < 0 || c->ballMat > 4) c->ballMat = 0;
        if (c->ballTintAlpha < 0) c->ballTintAlpha = 0;
        if (c->ballTintAlpha > 100) c->ballTintAlpha = 100;
        if (c->ballW < 40) c->ballW = 40;
        if (c->ballW > 400) c->ballW = 400;
        if (c->ballH < 40) c->ballH = 40;
        if (c->ballH > 400) c->ballH = 400;

        // 方形胶囊在像素上就是一个圆（半径 = min(w,h)/2）。旧配置/手改 ini
        // 出现 ballShape=2 且宽高相等时，自动补成 3:2 的横向胶囊。
        if (c->ballShape == 2 && c->ballW == c->ballH) {
            if (c->ballH > 266) c->ballH = 266;   // 保证 1.5×h ≤ 400
            c->ballW = c->ballH * 3 / 2;
            if (c->ballW > 400) c->ballW = 400;
        }
    }
}
