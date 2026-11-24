// src/sensors.cpp —— CPU/GPU 传感器后台线程实现（v1.2.0）
//
// 【为什么全部走后台线程】
//   WMI 查询是跨进程 RPC，一次 10~200 ms；PDH 首次建查询也要几十毫秒。这些放在
//   UI 线程的采样定时器里会造成肉眼可见的卡顿（尤其是拖动悬浮球时）。本文件把
//   全部慢调用收进一个线程，UI 只读快照。
//
// 【各数据源与取舍】
//   GPU 负载：PDH 通配计数器 "\GPU Engine(*)\Utilization Percentage"。
//     实例名形如 pid_1234_luid_0x00000000_0x0000ABCD_phys_0_eng_0_engtype_3D，
//     按 luid 归组求和（同一块卡的所有进程、所有引擎相加），再钳到 0..100。
//     用 PdhAddEnglishCounterW 是为了在非英文系统上也能用英文路径（动态取地址，
//     取不到再退回本地化 PdhAddCounterW）。
//   GPU 温度：NVIDIA 走 NVML（nvml.dll 随驱动安装，LoadLibrary 即可，不需要管理员）；
//     AMD 走 ADL2（atiadlxx.dll，Overdrive6 → Overdrive5 依次尝试）；
//     Intel 核显没有公开温度接口，只能显示 "--"。
//   CPU 温度：WMI ROOT\WMI:MSAcpi_ThermalZoneTemperature（十分之一开尔文），多个
//     热区取最大值；很多台式机没有该热区，读不到就显示 "--"（不伪造数值）。
//   外部 LibreHardwareMonitor：若用户自己开着 LHM 且启用了 WMI 提供程序，可从
//     ROOT\LibreHardwareMonitor 的 Sensor 类读到 CPU/GPU 温度，按 Identifier
//     （/gpu-nvidia/0/temperature/0）精确对应显卡。
//
// 【多 GPU 映射的已知限制】
//   DXGI 只给 LUID + 厂商/设备 ID，不提供 PCI 位置；NVML/ADL 只按自己的枚举序。
//   因此采用「同厂商内按枚举顺序一一对应」：单卡 100% 准确，多卡同型号也准确，
//   多卡不同型号且驱动枚举顺序不一致时温度可能互换（文档已记录）。
#include "sensors.h"
#include "sensors_hwinfo.h"
#include "layout_model.h"

#include <dxgi.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <wbemidl.h>
#include <oleauto.h>

#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <cwchar>
#include <vector>

namespace {

// ---------------------------------------------------------------- 共享快照

SRWLOCK   g_lock  = SRWLOCK_INIT;
Metrics   g_snap  = {};          // 只使用 CPU/GPU 字段
LONG      g_tempSrc = 0;         // AppConfig::tempSrc
LONG      g_gpuCount = 0;        // 原子读
LONG      g_needRescan = 1;      // 原子标志：枚举显卡 / 重建传感器上下文

// 只有排版里真的出现温度项目时才去碰 WMI/NVML：
// WMI 会加载 COM/WBEM 组件，实测常驻 +30MB，纯属浪费；
// 默认配置（只显示网速/负载）因此完全不付这笔开销。
LONG      g_wantCpuTemp = 0;
LONG      g_wantGpuTemp = 0;
LONG      g_wantGpuLoad = 0;     // 排版里没有 GPU 项目时连 PDH 都不建
LONG      g_wantCpuPwr  = 0;     // v1.4.2：没有 CPU 功率项时不建 Energy Meter 查询
LONG      g_wantGpuPwr  = 0;     // v1.4.2：没有 GPU 功率项时不调 NVML 功率接口

bool SpecHasCpuTemp(const wchar_t* spec)
{
    layout_model::LayoutBlock b;
    layout_model::parse(spec, NETBALL_MAX_ITEMS, &b);
    return layout_model::contains(b, layout_model::ITEM_CPUTEMP);
}

bool SpecHasGpuTemp(const wchar_t* spec)
{
    layout_model::LayoutBlock b;
    layout_model::parse(spec, NETBALL_MAX_ITEMS, &b);
    for (int i = 0; i < b.count; ++i) {
        int ix = 0;
        bool isTemp = false;
        if (layout_model::itemGpuIndex(b.item[i], &ix, &isTemp) && isTemp) return true;
    }
    return false;
}

bool SpecHasGpuItem(const wchar_t* spec)
{
    layout_model::LayoutBlock b;
    layout_model::parse(spec, NETBALL_MAX_ITEMS, &b);
    for (int i = 0; i < b.count; ++i) {
        int ix = 0;
        bool isTemp = false;
        if (layout_model::itemGpuIndex(b.item[i], &ix, &isTemp)) return true;
    }
    return false;
}

// v1.4.2：CPU 功率项
bool SpecHasCpuPwr(const wchar_t* spec)
{
    layout_model::LayoutBlock b;
    layout_model::parse(spec, NETBALL_MAX_ITEMS, &b);
    return layout_model::contains(b, layout_model::ITEM_CPUPWR);
}

// v1.4.2：任一 GPU 功率项
bool SpecHasGpuPwr(const wchar_t* spec)
{
    layout_model::LayoutBlock b;
    layout_model::parse(spec, NETBALL_MAX_ITEMS, &b);
    for (int i = 0; i < b.count; ++i) {
        int ix = 0;
        if (layout_model::itemGpuPowerIndex(b.item[i], &ix)) return true;
    }
    return false;
}

HANDLE    g_thread   = nullptr;
HANDLE    g_wake     = nullptr;
volatile LONG g_stop = 0;
volatile LONG g_stopping = 0;    // stop() 已进入：之后禁止再启动线程
DWORD     g_threadId = 0;

// ---------------------------------------------------------------- GPU 枚举

struct GpuAdapter {
    wchar_t name[64];
    UINT    vendorId;
    UINT    deviceId;
    LUID    luid;
    int     nvmlIndex;   // -1 = 未匹配
    int     adlIndex;    // -1 = 未匹配
};

GpuAdapter g_gpus[NETBALL_MAX_GPU];
int        g_enumCount = 0;

void CopyDescName(const wchar_t* src, wchar_t* dst, int cch)
{
    if (!dst || cch <= 0) return;
    if (!src) { dst[0] = 0; return; }
    lstrcpynW(dst, src, cch);
    dst[cch - 1] = 0;
}

void EnumerateGpus()
{
    GpuAdapter list[NETBALL_MAX_GPU];
    int count = 0;

    IDXGIFactory1* factory = nullptr;
    HRESULT hr = CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&factory);
    if (SUCCEEDED(hr) && factory) {
        for (UINT i = 0; count < NETBALL_MAX_GPU; ++i) {
            IDXGIAdapter1* adapter = nullptr;
            if (factory->EnumAdapters1(i, &adapter) != S_OK || !adapter) break;

            DXGI_ADAPTER_DESC1 desc;
            ZeroMemory(&desc, sizeof(desc));
            if (SUCCEEDED(adapter->GetDesc1(&desc))) {
                if ((desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) == 0) {
                    // 混合显卡机器上 EnumAdapters1 可能对同一块卡返回多行（渲染/显示分离），
                    // 按 LUID 去重；LUID 相同的一定是同一块物理适配器。
                    bool dup = false;
                    for (int k = 0; k < count; ++k) {
                        if (list[k].luid.HighPart == desc.AdapterLuid.HighPart &&
                            list[k].luid.LowPart  == desc.AdapterLuid.LowPart) {
                            dup = true;
                            break;
                        }
                    }
                    if (!dup) {
                        GpuAdapter& g = list[count];
                        ZeroMemory(&g, sizeof(g));
                        CopyDescName(desc.Description, g.name, 64);
                        g.vendorId   = desc.VendorId;
                        g.deviceId   = desc.DeviceId;
                        g.luid       = desc.AdapterLuid;
                        g.nvmlIndex  = -1;
                        g.adlIndex   = -1;
                        ++count;
                    }
                }
            }
            adapter->Release();
        }
        factory->Release();
    }

    // 同厂商内按枚举顺序分配 NVML / ADL 索引（已知限制见文件头）
    int nvmlNext = 0;
    for (int i = 0; i < count; ++i) {
        if (list[i].vendorId == 0x10DE) list[i].nvmlIndex = nvmlNext++;
    }

    SRWLOCK* lock = &g_lock;
    AcquireSRWLockExclusive(lock);
    for (int i = 0; i < count; ++i) g_gpus[i] = list[i];
    for (int i = count; i < NETBALL_MAX_GPU; ++i) ZeroMemory(&g_gpus[i], sizeof(g_gpus[i]));
    g_enumCount = count;
    InterlockedExchange(&g_gpuCount, (LONG)count);
    ReleaseSRWLockExclusive(lock);

    OutputDebugStringW(L"[NetBall] GPU enumerate done\n");
}

// ---------------------------------------------------------------- PDH：GPU 负载

typedef PDH_STATUS (WINAPI* PFN_PdhOpenQueryW)(LPCWSTR, DWORD_PTR, PDH_HQUERY*);
typedef PDH_STATUS (WINAPI* PFN_PdhAddEnglishCounterW)(PDH_HQUERY, LPCWSTR, DWORD_PTR, PDH_HCOUNTER*);
typedef PDH_STATUS (WINAPI* PFN_PdhAddCounterW)(PDH_HQUERY, LPCWSTR, DWORD_PTR, PDH_HCOUNTER*);
typedef PDH_STATUS (WINAPI* PFN_PdhCollectQueryData)(PDH_HQUERY);
typedef PDH_STATUS (WINAPI* PFN_PdhGetFormattedCounterArrayW)(PDH_HCOUNTER, DWORD, LPDWORD,
                                                              LPDWORD, PPDH_FMT_COUNTERVALUE_ITEM_W);
typedef PDH_STATUS (WINAPI* PFN_PdhCloseQuery)(PDH_HQUERY);

HMODULE g_pdhDll = nullptr;
PFN_PdhOpenQueryW                 pPdhOpenQuery = nullptr;
PFN_PdhAddEnglishCounterW         pPdhAddEnglish = nullptr;
PFN_PdhAddCounterW                pPdhAddCounter = nullptr;
PFN_PdhCollectQueryData           pPdhCollect = nullptr;
PFN_PdhGetFormattedCounterArrayW  pPdhGetArray = nullptr;
PFN_PdhCloseQuery                 pPdhCloseQuery = nullptr;

PDH_HQUERY   g_query = nullptr;
PDH_HCOUNTER g_gpuCounter = nullptr;
std::vector<BYTE> g_pdhBuf;

// ---- v1.4.2：热区温度查询（\\Thermal Zone Information\\*）----
// 走 PDH 而不是 WMI：同一份 ACPI 热区数据，但不受 root\\WMI 类 ACL 限制，
// 非管理员进程也能读（实测 MSAcpi_ThermalZoneTemperature 返回 0x80041003）。
PDH_HQUERY   g_tzQuery  = nullptr;
PDH_HCOUNTER g_tzHp     = nullptr;   // High Precision Temperature，分度开尔文
PDH_HCOUNTER g_tzPlain  = nullptr;   // Temperature，整数开尔文（回退）
std::vector<BYTE> g_tzBuf;

// ---- v1.4.2：CPU 封装功率查询（\\Energy Meter\\Power）----
// Windows 11 的 Energy Meter Interface（RAPL）。实例名形如 rapl_package0_pkg，
// 单位毫瓦；_total 恒为 0，必须按实例名匹配，不能用合计值。
PDH_HQUERY   g_pwrQuery = nullptr;
PDH_HCOUNTER g_pwrCounter = nullptr;
std::vector<BYTE> g_pwrBuf;

void ReleasePdhDll()
{
    if (g_pdhDll) {
        FreeLibrary(g_pdhDll);
        g_pdhDll = nullptr;
    }
    // 模块卸载后函数指针必须一起失效，否则下次重试会调到已卸载的代码
    pPdhOpenQuery  = nullptr;
    pPdhAddEnglish = nullptr;
    pPdhAddCounter = nullptr;
    pPdhCollect    = nullptr;
    pPdhGetArray   = nullptr;
    pPdhCloseQuery = nullptr;
}

// v1.4.2：把「加载 pdh.dll」与「建具体查询」拆开。
// 原来两件事耦合在 InitPdh 里，任一查询失败就 ReleasePdhDll()，会把别的查询一起带崩；
// 现在模块只加载一次，三条查询（GPU 负载 / 热区温度 / 能量计功率）各自独立失败。
bool EnsurePdhDll()
{
    if (g_pdhDll) return true;

    // 只在未加载时加载：每秒重试一次，重复 LoadLibrary 会让引用计数只增不减
    ReleasePdhDll();
    g_pdhDll = LoadLibraryW(L"pdh.dll");
    if (!g_pdhDll) return false;

    pPdhOpenQuery   = (PFN_PdhOpenQueryW)(void*)GetProcAddress(g_pdhDll, "PdhOpenQueryW");
    pPdhAddEnglish  = (PFN_PdhAddEnglishCounterW)(void*)GetProcAddress(g_pdhDll, "PdhAddEnglishCounterW");
    pPdhAddCounter  = (PFN_PdhAddCounterW)(void*)GetProcAddress(g_pdhDll, "PdhAddCounterW");
    pPdhCollect     = (PFN_PdhCollectQueryData)(void*)GetProcAddress(g_pdhDll, "PdhCollectQueryData");
    pPdhGetArray    = (PFN_PdhGetFormattedCounterArrayW)(void*)GetProcAddress(g_pdhDll, "PdhGetFormattedCounterArrayW");
    pPdhCloseQuery  = (PFN_PdhCloseQuery)(void*)GetProcAddress(g_pdhDll, "PdhCloseQuery");

    if (!pPdhOpenQuery || !pPdhCollect || !pPdhGetArray || !pPdhCloseQuery ||
        (!pPdhAddEnglish && !pPdhAddCounter)) {
        ReleasePdhDll();
        return false;
    }
    return true;
}

bool InitPdh()
{
    if (g_query) return true;
    if (!EnsurePdhDll()) return false;
    if (pPdhOpenQuery(nullptr, 0, &g_query) != ERROR_SUCCESS) {
        g_query = nullptr;
        return false;
    }

    const wchar_t* path = L"\\GPU Engine(*)\\Utilization Percentage";
    PDH_STATUS st = (PDH_STATUS)-1;
    if (pPdhAddEnglish) st = pPdhAddEnglish(g_query, path, 0, &g_gpuCounter);
    if (st != ERROR_SUCCESS) {
        g_gpuCounter = nullptr;
        if (pPdhAddCounter) st = pPdhAddCounter(g_query, path, 0, &g_gpuCounter);
    }
    if (st != ERROR_SUCCESS || !g_gpuCounter) {
        pPdhCloseQuery(g_query);
        g_query = nullptr;
        g_gpuCounter = nullptr;
        return false;          // 不释放模块：温度/功率查询还要用它
    }

    pPdhCollect(g_query);   // 第一次是建立基准，值无效，忽略
    return true;
}

void ShutdownPdh()
{
    if (g_query && pPdhCloseQuery) pPdhCloseQuery(g_query);
    g_query = nullptr;
    g_gpuCounter = nullptr;
    g_pdhBuf.clear();

    if (g_tzQuery && pPdhCloseQuery) pPdhCloseQuery(g_tzQuery);
    g_tzQuery = nullptr;
    g_tzHp = nullptr;
    g_tzPlain = nullptr;
    g_tzBuf.clear();

    if (g_pwrQuery && pPdhCloseQuery) pPdhCloseQuery(g_pwrQuery);
    g_pwrQuery = nullptr;
    g_pwrCounter = nullptr;
    g_pwrBuf.clear();

    ReleasePdhDll();
}

// ---------------------------------------------------------------- PDH 通用取值

// 取一个计数器数组里的最大值（多实例时用）。成功返回 true。
bool ReadCounterMaxDouble(PDH_HCOUNTER counter, std::vector<BYTE>& buf, double* outMax)
{
    if (!counter || !outMax || !pPdhGetArray) return false;

    DWORD size = 0;
    DWORD items = 0;
    PDH_STATUS st = pPdhGetArray(counter, PDH_FMT_DOUBLE, &size, &items, nullptr);
    if (st != (PDH_STATUS)PDH_MORE_DATA || size == 0) return false;

    if (buf.size() < size) buf.resize(size);
    st = pPdhGetArray(counter, PDH_FMT_DOUBLE, &size, &items,
                      (PPDH_FMT_COUNTERVALUE_ITEM_W)(void*)buf.data());
    if (st != ERROR_SUCCESS) return false;

    const PDH_FMT_COUNTERVALUE_ITEM_W* arr =
        (const PDH_FMT_COUNTERVALUE_ITEM_W*)(const void*)buf.data();

    bool found = false;
    double best = 0.0;
    for (DWORD i = 0; i < items; ++i) {
        if (arr[i].FmtValue.CStatus != PDH_CSTATUS_VALID_DATA &&
            arr[i].FmtValue.CStatus != PDH_CSTATUS_NEW_DATA) continue;
        const double v = arr[i].FmtValue.doubleValue;
        if (!(v > 0.0)) continue;
        if (!found || v > best) { best = v; found = true; }
    }
    if (!found) return false;
    *outMax = best;
    return true;
}

bool StartsWithNoCase(const wchar_t* s, const wchar_t* prefix)
{
    if (!s || !prefix) return false;
    while (*prefix) {
        wchar_t a = *s;
        wchar_t b = *prefix;
        if (a >= L'A' && a <= L'Z') a = (wchar_t)(a - L'A' + L'a');
        if (b >= L'A' && b <= L'Z') b = (wchar_t)(b - L'A' + L'a');
        if (!a || a != b) return false;
        ++s; ++prefix;
    }
    return true;
}

bool EndsWithNoCase(const wchar_t* s, const wchar_t* suffix)
{
    if (!s || !suffix) return false;
    const size_t ls = wcslen(s);
    const size_t lf = wcslen(suffix);
    if (lf > ls) return false;
    const wchar_t* p = s + (ls - lf);
    for (size_t i = 0; i < lf; ++i) {
        wchar_t a = p[i];
        wchar_t b = suffix[i];
        if (a >= L'A' && a <= L'Z') a = (wchar_t)(a - L'A' + L'a');
        if (b >= L'A' && b <= L'Z') b = (wchar_t)(b - L'A' + L'a');
        if (a != b) return false;
    }
    return true;
}

// ---------------------------------------------------------------- 热区温度

bool EnsureTzQuery()
{
    if (g_tzQuery) return true;
    if (!EnsurePdhDll()) return false;
    if (pPdhOpenQuery(nullptr, 0, &g_tzQuery) != ERROR_SUCCESS) {
        g_tzQuery = nullptr;
        return false;
    }

    const wchar_t* kHp    = L"\\Thermal Zone Information(*)\\High Precision Temperature";
    const wchar_t* kPlain = L"\\Thermal Zone Information(*)\\Temperature";

    PDH_STATUS s1 = (PDH_STATUS)-1;
    PDH_STATUS s2 = (PDH_STATUS)-1;
    if (pPdhAddEnglish) {
        s1 = pPdhAddEnglish(g_tzQuery, kHp, 0, &g_tzHp);
        s2 = pPdhAddEnglish(g_tzQuery, kPlain, 0, &g_tzPlain);
    }
    if (s1 != ERROR_SUCCESS && pPdhAddCounter) {
        g_tzHp = nullptr;
        s1 = pPdhAddCounter(g_tzQuery, kHp, 0, &g_tzHp);
    }
    if (s2 != ERROR_SUCCESS && pPdhAddCounter) {
        g_tzPlain = nullptr;
        s2 = pPdhAddCounter(g_tzQuery, kPlain, 0, &g_tzPlain);
    }
    if (!g_tzHp && !g_tzPlain) {
        pPdhCloseQuery(g_tzQuery);
        g_tzQuery = nullptr;
        return false;
    }

    pPdhCollect(g_tzQuery);   // 第一次建立基准，值无效
    return true;
}

// 返回分度开尔文（deci-K）。多个热区取最大值（最接近 CPU 的那一路）。
bool ReadThermalZoneDeciK(int* outDeciK)
{
    if (!outDeciK) return false;
    if (!EnsureTzQuery()) return false;
    if (pPdhCollect(g_tzQuery) != ERROR_SUCCESS) return false;

    double v = 0.0;
    if (g_tzHp && ReadCounterMaxDouble(g_tzHp, g_tzBuf, &v)) {
        const int dk = (int)(v + 0.5);
        if (dk >= 2000 && dk <= 4500) {           // 200.0 K ~ 450.0 K
            *outDeciK = dk;
            return true;
        }
    }
    if (g_tzPlain && ReadCounterMaxDouble(g_tzPlain, g_tzBuf, &v)) {
        const int dk = (int)(v * 10.0 + 0.5);     // 整数 K → 分度 K
        if (dk >= 2000 && dk <= 4500) {
            *outDeciK = dk;
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------- CPU 封装功率

bool EnsurePwrQuery()
{
    if (g_pwrQuery) return true;
    if (!EnsurePdhDll()) return false;
    if (pPdhOpenQuery(nullptr, 0, &g_pwrQuery) != ERROR_SUCCESS) {
        g_pwrQuery = nullptr;
        return false;
    }

    const wchar_t* path = L"\\Energy Meter(*)\\Power";
    PDH_STATUS st = (PDH_STATUS)-1;
    if (pPdhAddEnglish) st = pPdhAddEnglish(g_pwrQuery, path, 0, &g_pwrCounter);
    if (st != ERROR_SUCCESS) {
        g_pwrCounter = nullptr;
        if (pPdhAddCounter) st = pPdhAddCounter(g_pwrQuery, path, 0, &g_pwrCounter);
    }
    if (st != ERROR_SUCCESS || !g_pwrCounter) {
        pPdhCloseQuery(g_pwrQuery);
        g_pwrQuery = nullptr;
        g_pwrCounter = nullptr;
        return false;
    }

    pPdhCollect(g_pwrQuery);
    return true;
}

// 多路 CPU 求和。只认 rapl_*_pkg 实例：_total 实测恒为 0，不能当合计值用。
bool ReadCpuPowerMw(int* outMw)
{
    if (!outMw) return false;
    if (!EnsurePwrQuery()) return false;
    if (pPdhCollect(g_pwrQuery) != ERROR_SUCCESS) return false;

    DWORD size = 0;
    DWORD items = 0;
    PDH_STATUS st = pPdhGetArray(g_pwrCounter, PDH_FMT_DOUBLE, &size, &items, nullptr);
    if (st != (PDH_STATUS)PDH_MORE_DATA || size == 0) return false;

    if (g_pwrBuf.size() < size) g_pwrBuf.resize(size);
    st = pPdhGetArray(g_pwrCounter, PDH_FMT_DOUBLE, &size, &items,
                      (PPDH_FMT_COUNTERVALUE_ITEM_W)(void*)g_pwrBuf.data());
    if (st != ERROR_SUCCESS) return false;

    const PDH_FMT_COUNTERVALUE_ITEM_W* arr =
        (const PDH_FMT_COUNTERVALUE_ITEM_W*)(const void*)g_pwrBuf.data();

    long long sum = 0;
    int matched = 0;
    for (DWORD i = 0; i < items; ++i) {
        if (!arr[i].szName) continue;
        if (!StartsWithNoCase(arr[i].szName, L"rapl_")) continue;
        if (!EndsWithNoCase(arr[i].szName, L"_pkg")) continue;
        if (arr[i].FmtValue.CStatus != PDH_CSTATUS_VALID_DATA &&
            arr[i].FmtValue.CStatus != PDH_CSTATUS_NEW_DATA) continue;

        const double v = arr[i].FmtValue.doubleValue;
        if (!(v >= 0.0) || v > 2000000.0) continue;
        sum += (long long)(v + 0.5);
        ++matched;
    }
    if (matched == 0) return false;
    if (sum > 2000000LL) return false;
    *outMw = (int)sum;
    return true;
}

// 从 PDH 实例名里取 LUID：..._luid_0x00000000_0x0000ABCD_...
bool ParseLuid(const wchar_t* name, LUID* out)
{
    if (!name || !out) return false;
    const wchar_t* p = wcsstr(name, L"_luid_");
    if (!p) return false;
    p += 6;

    wchar_t* end = nullptr;
    const unsigned long hi = wcstoul(p, &end, 16);
    if (!end || *end != L'_') return false;
    const unsigned long lo = wcstoul(end + 1, &end, 16);
    if (!end) return false;

    out->HighPart = (LONG)hi;
    out->LowPart  = (DWORD)lo;
    return true;
}

void CollectGpuLoad()
{
    if (!InterlockedCompareExchange(&g_wantGpuLoad, 0, 0)) return;   // 没有 GPU 项目 → 不建 PDH 查询

    int loads[NETBALL_MAX_GPU];
    for (int i = 0; i < NETBALL_MAX_GPU; ++i) loads[i] = -1;

    if (InitPdh() && g_gpuCounter) {
        if (pPdhCollect(g_query) == ERROR_SUCCESS) {
            DWORD size = 0;
            DWORD items = 0;
            PDH_STATUS st = pPdhGetArray(g_gpuCounter, PDH_FMT_DOUBLE, &size, &items, nullptr);
            if (st == (PDH_STATUS)PDH_MORE_DATA && size > 0) {
                if (g_pdhBuf.size() < size) g_pdhBuf.resize(size);
                st = pPdhGetArray(g_gpuCounter, PDH_FMT_DOUBLE, &size, &items,
                                  (PPDH_FMT_COUNTERVALUE_ITEM_W)(void*)g_pdhBuf.data());
                if (st == ERROR_SUCCESS) {
                    const PDH_FMT_COUNTERVALUE_ITEM_W* arr =
                        (const PDH_FMT_COUNTERVALUE_ITEM_W*)(const void*)g_pdhBuf.data();

                    double sum[NETBALL_MAX_GPU] = {0};
                    bool   seen[NETBALL_MAX_GPU] = {false};

                    for (DWORD i = 0; i < items; ++i) {
                        LUID luid;
                        if (!ParseLuid(arr[i].szName, &luid)) continue;
                        if (arr[i].FmtValue.CStatus != PDH_CSTATUS_VALID_DATA &&
                            arr[i].FmtValue.CStatus != PDH_CSTATUS_NEW_DATA) continue;

                        const double v = arr[i].FmtValue.doubleValue;
                        if (!(v > 0.0)) continue;
                        if (v > 100.0) continue;   // 单实例异常值直接丢弃

                        for (int g = 0; g < NETBALL_MAX_GPU; ++g) {
                            if (g >= g_enumCount) break;
                            if (g_gpus[g].luid.HighPart == luid.HighPart &&
                                g_gpus[g].luid.LowPart  == luid.LowPart) {
                                sum[g] += v;
                                seen[g] = true;
                                break;
                            }
                        }
                    }

                    for (int g = 0; g < g_enumCount && g < NETBALL_MAX_GPU; ++g) {
                        if (!seen[g]) { loads[g] = 0; continue; }
                        double v = sum[g];
                        if (v < 0.0) v = 0.0;
                        if (v > 100.0) v = 100.0;
                        loads[g] = (int)(v + 0.5);
                    }
                }
            }
        }
    }

    AcquireSRWLockExclusive(&g_lock);
    for (int i = 0; i < NETBALL_MAX_GPU; ++i) g_snap.gpuLoad[i] = loads[i];
    ReleaseSRWLockExclusive(&g_lock);
}

// ---------------------------------------------------------------- NVML：NVIDIA 温度

typedef int   nvmlReturn_t;
typedef void* nvmlDevice_t;

typedef nvmlReturn_t (*PFN_nvmlInit)(void);
typedef nvmlReturn_t (*PFN_nvmlShutdown)(void);
typedef nvmlReturn_t (*PFN_nvmlDeviceGetCount)(unsigned*);
typedef nvmlReturn_t (*PFN_nvmlDeviceGetHandleByIndex)(unsigned, nvmlDevice_t*);
typedef nvmlReturn_t (*PFN_nvmlDeviceGetTemperature)(nvmlDevice_t, int, unsigned*);
typedef nvmlReturn_t (*PFN_nvmlDeviceGetPowerUsage)(nvmlDevice_t, unsigned*);      // v1.4.2

const int NVML_SUCCESS = 0;
const int NVML_TEMPERATURE_GPU = 0;

HMODULE g_nvmlDll = nullptr;
PFN_nvmlInit                    pNvmlInit = nullptr;
PFN_nvmlShutdown                pNvmlShutdown = nullptr;
PFN_nvmlDeviceGetCount          pNvmlCount = nullptr;
PFN_nvmlDeviceGetHandleByIndex  pNvmlHandle = nullptr;
PFN_nvmlDeviceGetTemperature    pNvmlTemp = nullptr;
PFN_nvmlDeviceGetPowerUsage     pNvmlPower = nullptr;    // v1.4.2
bool g_nvmlReady = false;
bool g_nvmlProbed = false;

void InitNvml()
{
    if (g_nvmlProbed) return;
    g_nvmlProbed = true;

    g_nvmlDll = LoadLibraryW(L"nvml.dll");
    if (!g_nvmlDll) {
        static const wchar_t kSuffix[] = L"\\NVIDIA Corporation\\NVSMI\\nvml.dll";
        const size_t suffixLen = sizeof(kSuffix) / sizeof(kSuffix[0]) - 1;
        wchar_t alt[MAX_PATH] = {0};
        const DWORD n = GetEnvironmentVariableW(L"ProgramFiles", alt, MAX_PATH);
        if (n > 0 && n < MAX_PATH && wcslen(alt) + suffixLen < MAX_PATH) {
            lstrcatW(alt, kSuffix);
            g_nvmlDll = LoadLibraryW(alt);
        }
    }
    if (!g_nvmlDll) return;

    pNvmlInit     = (PFN_nvmlInit)(void*)GetProcAddress(g_nvmlDll, "nvmlInit_v2");
    if (!pNvmlInit) pNvmlInit = (PFN_nvmlInit)(void*)GetProcAddress(g_nvmlDll, "nvmlInit");
    pNvmlShutdown = (PFN_nvmlShutdown)(void*)GetProcAddress(g_nvmlDll, "nvmlShutdown");
    pNvmlCount    = (PFN_nvmlDeviceGetCount)(void*)GetProcAddress(g_nvmlDll, "nvmlDeviceGetCount_v2");
    if (!pNvmlCount) pNvmlCount = (PFN_nvmlDeviceGetCount)(void*)GetProcAddress(g_nvmlDll, "nvmlDeviceGetCount");
    pNvmlHandle   = (PFN_nvmlDeviceGetHandleByIndex)(void*)GetProcAddress(g_nvmlDll, "nvmlDeviceGetHandleByIndex_v2");
    if (!pNvmlHandle) pNvmlHandle = (PFN_nvmlDeviceGetHandleByIndex)(void*)GetProcAddress(g_nvmlDll, "nvmlDeviceGetHandleByIndex");
    pNvmlTemp     = (PFN_nvmlDeviceGetTemperature)(void*)GetProcAddress(g_nvmlDll, "nvmlDeviceGetTemperature");
    // 功率是可选能力：老驱动没有该导出时只让功率显示 "--"，不影响温度
    pNvmlPower    = (PFN_nvmlDeviceGetPowerUsage)(void*)GetProcAddress(g_nvmlDll, "nvmlDeviceGetPowerUsage");

    if (!pNvmlInit || !pNvmlCount || !pNvmlHandle || !pNvmlTemp) return;
    if (pNvmlInit() != NVML_SUCCESS) return;
    g_nvmlReady = true;
}

void ShutdownNvml()
{
    if (g_nvmlReady && pNvmlShutdown) pNvmlShutdown();
    g_nvmlReady = false;

    if (g_nvmlDll) {
        FreeLibrary(g_nvmlDll);
        g_nvmlDll = nullptr;
        pNvmlInit = nullptr;
        pNvmlShutdown = nullptr;
        pNvmlCount = nullptr;
        pNvmlHandle = nullptr;
        pNvmlTemp = nullptr;
        pNvmlPower = nullptr;
    }
}

bool ReadNvmlTemp(int nvmlIndex, int* outC)
{
    if (!g_nvmlReady || nvmlIndex < 0 || !outC) return false;
    nvmlDevice_t dev = nullptr;
    if (pNvmlHandle((unsigned)nvmlIndex, &dev) != NVML_SUCCESS || !dev) return false;
    unsigned t = 0;
    if (pNvmlTemp(dev, NVML_TEMPERATURE_GPU, &t) != NVML_SUCCESS) return false;
    if (t == 0 || t > 150) return false;          // 0/异常值视为无效
    *outC = (int)t;
    return true;
}

// v1.4.2：GPU 功率（毫瓦）。NVML 返回值本身就是 mW，不再换算。
bool ReadNvmlPowerMw(int nvmlIndex, int* outMw)
{
    if (!g_nvmlReady || nvmlIndex < 0 || !outMw || !pNvmlPower) return false;
    nvmlDevice_t dev = nullptr;
    if (pNvmlHandle((unsigned)nvmlIndex, &dev) != NVML_SUCCESS || !dev) return false;

    unsigned mw = 0;
    if (pNvmlPower(dev, &mw) != NVML_SUCCESS) return false;
    if (mw > 2000000u) return false;               // >2 kW 视为异常
    *outMw = (int)mw;
    return true;
}

// ---------------------------------------------------------------- ADL：AMD 温度

typedef void* ADL_CONTEXT;

struct ADLAdapterInfo {
    int  iSize;
    int  iAdapterIndex;
    char strUDID[256];
    int  iBusNumber;
    int  iDeviceNumber;
    int  iFunctionNumber;
    int  iVendorID;
    char strAdapterName[256];
    char strDisplayName[256];
    int  iPresent;
    int  iExist;
    char strDriverPath[256];
    char strDriverPathExt[256];
    char strPNPString[256];
    int  iOSDisplayIndex;
};
static_assert(sizeof(ADLAdapterInfo) == 1572, "ADLAdapterInfo 必须与 adl_sdk.h 完全一致");

struct ADLTemperature {
    int iSize;
    int iTemperature;    // 千分之一摄氏度
};

typedef void* (__stdcall *PFN_AdlMalloc)(int);
typedef int (*PFN_AdlMainCreate)(PFN_AdlMalloc, int, ADL_CONTEXT*);
typedef int (*PFN_AdlMainDestroy)(ADL_CONTEXT);
typedef int (*PFN_AdlNumberOfAdapters)(ADL_CONTEXT, int*);
typedef int (*PFN_AdlAdapterInfo)(ADL_CONTEXT, ADLAdapterInfo*, int);
typedef int (*PFN_AdlOd6Temp)(ADL_CONTEXT, int, int*);
typedef int (*PFN_AdlOd5Temp)(ADL_CONTEXT, int, int, ADLTemperature*);

HMODULE g_adlDll = nullptr;
ADL_CONTEXT g_adlCtx = nullptr;
PFN_AdlMainDestroy      pAdlDestroy = nullptr;
PFN_AdlNumberOfAdapters pAdlCount = nullptr;
PFN_AdlAdapterInfo      pAdlInfo = nullptr;
PFN_AdlOd6Temp          pAdlOd6Temp = nullptr;
PFN_AdlOd5Temp          pAdlOd5Temp = nullptr;
bool g_adlProbed = false;
bool g_adlReady  = false;

void* __stdcall AdlMallocCb(int size)
{
    return malloc((size_t)(size > 0 ? size : 1));
}

void InitAdl()
{
    if (g_adlProbed) return;
    g_adlProbed = true;

    g_adlDll = LoadLibraryW(L"atiadlxx.dll");
    if (!g_adlDll) g_adlDll = LoadLibraryW(L"atiadlxy.dll");
    if (!g_adlDll) return;

    PFN_AdlMainCreate create =
        (PFN_AdlMainCreate)(void*)GetProcAddress(g_adlDll, "ADL2_Main_Control_Create");
    if (!create) return;

    pAdlDestroy = (PFN_AdlMainDestroy)(void*)GetProcAddress(g_adlDll, "ADL2_Main_Control_Destroy");
    pAdlCount   = (PFN_AdlNumberOfAdapters)(void*)GetProcAddress(g_adlDll, "ADL2_Adapter_NumberOfAdapters_Get");
    pAdlInfo    = (PFN_AdlAdapterInfo)(void*)GetProcAddress(g_adlDll, "ADL2_Adapter_AdapterInfo_Get");
    pAdlOd6Temp = (PFN_AdlOd6Temp)(void*)GetProcAddress(g_adlDll, "ADL2_Overdrive6_Temperature_Get");
    pAdlOd5Temp = (PFN_AdlOd5Temp)(void*)GetProcAddress(g_adlDll, "ADL2_Overdrive5_Temperature_Get");

    if (!pAdlCount || !pAdlInfo) return;
    if (create(AdlMallocCb, 0, &g_adlCtx) != 0 || !g_adlCtx) {
        g_adlCtx = nullptr;
        return;
    }
    g_adlReady = true;

    // 把 ADL 里「在用且是 AMD」的适配器按顺序映射到我们的 DXGI 顺序
    int numbers = 0;
    if (pAdlCount(g_adlCtx, &numbers) != 0 || numbers <= 0 || numbers > 64) return;

    std::vector<ADLAdapterInfo> infos((size_t)numbers);
    for (int i = 0; i < numbers; ++i) {
        ZeroMemory(&infos[(size_t)i], sizeof(ADLAdapterInfo));
        infos[(size_t)i].iSize = (int)sizeof(ADLAdapterInfo);
    }
    if (pAdlInfo(g_adlCtx, infos.data(), (int)(sizeof(ADLAdapterInfo) * (size_t)numbers)) != 0) return;

    // 收集「在用且是 AMD」的 ADL 适配器（按 ADL 枚举序）
    int amdAdl[NETBALL_MAX_GPU];
    int amdAdlCount = 0;
    for (int i = 0; i < numbers && amdAdlCount < NETBALL_MAX_GPU; ++i) {
        if (infos[(size_t)i].iVendorID != 0x1002) continue;
        if (!infos[(size_t)i].iPresent) continue;
        amdAdl[amdAdlCount++] = infos[(size_t)i].iAdapterIndex;
    }

    AcquireSRWLockExclusive(&g_lock);
    int amdNext = 0;
    for (int g = 0; g < g_enumCount; ++g) {
        if (g_gpus[g].vendorId != 0x1002) continue;
        if (amdNext >= amdAdlCount) break;
        g_gpus[g].adlIndex = amdAdl[amdNext++];
    }
    ReleaseSRWLockExclusive(&g_lock);
}

void ShutdownAdl()
{
    if (g_adlCtx && pAdlDestroy) pAdlDestroy(g_adlCtx);
    g_adlCtx = nullptr;
    g_adlReady = false;

    if (g_adlDll) {
        FreeLibrary(g_adlDll);
        g_adlDll = nullptr;
        pAdlDestroy = nullptr;
        pAdlCount = nullptr;
        pAdlInfo = nullptr;
        pAdlOd6Temp = nullptr;
        pAdlOd5Temp = nullptr;
    }
}

bool ReadAdlTemp(int adlIndex, int* outC)
{
    if (!g_adlReady || adlIndex < 0 || !outC) return false;

    if (pAdlOd6Temp) {
        int milli = 0;
        if (pAdlOd6Temp(g_adlCtx, adlIndex, &milli) == 0 && milli > 0 && milli < 150000) {
            *outC = milli / 1000;
            return true;
        }
    }
    if (pAdlOd5Temp) {
        ADLTemperature t;
        t.iSize = (int)sizeof(ADLTemperature);
        t.iTemperature = 0;
        if (pAdlOd5Temp(g_adlCtx, adlIndex, 0, &t) == 0 &&
            t.iTemperature > 0 && t.iTemperature < 150000) {
            *outC = t.iTemperature / 1000;
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------- WMI

typedef bool (*WmiObjectFn)(IWbemClassObject* obj, void* ctx);

bool WmiQuery(const wchar_t* ns, const wchar_t* wql, WmiObjectFn fn, void* ctx)
{
    IWbemLocator* loc = nullptr;
    IWbemServices* svc = nullptr;
    IEnumWbemClassObject* en = nullptr;
    bool ok = false;

    HRESULT hr = CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_IWbemLocator, (void**)&loc);
    if (FAILED(hr) || !loc) return false;

    BSTR bns = SysAllocString(ns);
    hr = loc->ConnectServer(bns, nullptr, nullptr, nullptr, 0, nullptr, nullptr, &svc);
    SysFreeString(bns);

    if (SUCCEEDED(hr) && svc) {
        CoSetProxyBlanket(svc, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr,
                          RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE);

        BSTR lang = SysAllocString(L"WQL");
        BSTR q    = SysAllocString(wql);
        hr = svc->ExecQuery(lang, q,
                            WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
                            nullptr, &en);
        SysFreeString(lang);
        SysFreeString(q);

        if (SUCCEEDED(hr) && en) {
            for (;;) {
                IWbemClassObject* obj = nullptr;
                ULONG ret = 0;
                if (en->Next(WBEM_INFINITE, 1, &obj, &ret) != S_OK || !obj) break;
                if (fn) ok = fn(obj, ctx) || ok;
                obj->Release();
            }
            en->Release();
        }
        svc->Release();
    }
    loc->Release();
    return ok;
}

bool GetPropInt(IWbemClassObject* obj, const wchar_t* name, LONG* out)
{
    if (!obj || !out) return false;
    VARIANT v;
    VariantInit(&v);
    bool ok = false;
    if (SUCCEEDED(obj->Get(name, 0, &v, nullptr, nullptr))) {
        switch (v.vt) {
        case VT_I4: *out = v.lVal;  ok = true; break;
        case VT_UI4: *out = (LONG)v.ulVal; ok = true; break;
        case VT_I2: *out = (LONG)v.iVal;   ok = true; break;
        case VT_UI2: *out = (LONG)v.uiVal; ok = true; break;
        case VT_R8: *out = (LONG)(v.dblVal + 0.5); ok = true; break;
        default: break;
        }
    }
    VariantClear(&v);
    return ok;
}

bool GetPropStr(IWbemClassObject* obj, const wchar_t* name, wchar_t* out, int cch)
{
    if (!obj || !out || cch <= 0) return false;
    out[0] = 0;
    VARIANT v;
    VariantInit(&v);
    bool ok = false;
    if (SUCCEEDED(obj->Get(name, 0, &v, nullptr, nullptr)) && v.vt == VT_BSTR && v.bstrVal) {
        lstrcpynW(out, v.bstrVal, cch);
        out[cch - 1] = 0;
        ok = true;
    }
    VariantClear(&v);
    return ok;
}

bool GetPropDouble(IWbemClassObject* obj, const wchar_t* name, double* out)
{
    if (!obj || !out) return false;
    VARIANT v;
    VariantInit(&v);
    bool ok = false;
    if (SUCCEEDED(obj->Get(name, 0, &v, nullptr, nullptr))) {
        if (v.vt == VT_R8) { *out = v.dblVal; ok = true; }
        else if (v.vt == VT_I4) { *out = (double)v.lVal; ok = true; }
    }
    VariantClear(&v);
    return ok;
}

struct AcpiCtx {
    LONG maxTenthKelvin;
    bool found;
};

bool AcpiObj(IWbemClassObject* obj, void* ctx)
{
    AcpiCtx* c = (AcpiCtx*)ctx;
    LONG t = 0;
    if (!GetPropInt(obj, L"CurrentTemperature", &t)) return false;
    if (t <= 0 || t > 5000) return false;         // 合理区间 0~226.85℃
    if (!c->found || t > c->maxTenthKelvin) {
        c->maxTenthKelvin = t;
        c->found = true;
    }
    return true;
}

bool QueryAcpiCpuTemp(int* outC)
{
    AcpiCtx ctx;
    ctx.maxTenthKelvin = 0;
    ctx.found = false;

    if (!WmiQuery(L"ROOT\\WMI",
                  L"SELECT CurrentTemperature FROM MSAcpi_ThermalZoneTemperature",
                  AcpiObj, &ctx) || !ctx.found) {
        return false;
    }

    const double celsius = (double)ctx.maxTenthKelvin / 10.0 - 273.15;
    if (celsius < -20.0 || celsius > 150.0) return false;
    *outC = (int)(celsius + 0.5);
    return true;
}

struct LhmCtx {
    int  cpuTempC;
    int  cpuPriority;                 // 越小越优先
    bool cpuOk;
    int  gpuTemp[NETBALL_MAX_GPU];
    bool gpuOk[NETBALL_MAX_GPU];
};

int LhmVendorId(const wchar_t* vendor)
{
    if (!vendor) return 0;
    if (lstrcmpiW(vendor, L"nvidia") == 0) return 0x10DE;
    if (lstrcmpiW(vendor, L"ati") == 0 || lstrcmpiW(vendor, L"amd") == 0) return 0x1002;
    if (lstrcmpiW(vendor, L"intel") == 0) return 0x8086;
    return 0;
}

bool LhmObj(IWbemClassObject* obj, void* ctx)
{
    LhmCtx* c = (LhmCtx*)ctx;

    wchar_t type[64] = {0};
    if (!GetPropStr(obj, L"SensorType", type, 64)) return false;
    if (lstrcmpiW(type, L"Temperature") != 0) return false;

    double value = 0.0;
    if (!GetPropDouble(obj, L"Value", &value)) return false;
    if (!(value > -20.0) || value > 150.0) return false;

    wchar_t name[128] = {0};
    GetPropStr(obj, L"Name", name, 128);

    wchar_t ident[160] = {0};
    GetPropStr(obj, L"Identifier", ident, 160);

    // GPU：/gpu-nvidia/0/temperature/0
    if (wcsncmp(ident, L"/gpu-", 5) == 0) {
        const wchar_t* p = ident + 5;
        const wchar_t* dash = wcschr(p, L'/');
        if (!dash) return false;

        wchar_t vendor[16] = {0};
        const size_t vlen = (size_t)(dash - p);
        if (vlen == 0 || vlen >= 16) return false;
        for (size_t i = 0; i < vlen; ++i) vendor[i] = p[i];
        vendor[vlen] = 0;

        const int vid = LhmVendorId(vendor);
        if (vid == 0) return false;

        // vendor 后面的第一段数字是「同厂商第几张卡」
        const wchar_t* num = dash + 1;
        const int vendorIx = (int)wcstol(num, nullptr, 10);

        int seen = 0;
        for (int g = 0; g < g_enumCount && g < NETBALL_MAX_GPU; ++g) {
            if (g_gpus[g].vendorId != (UINT)vid) continue;
            if (seen == vendorIx) {
                c->gpuTemp[g] = (int)(value + 0.5);
                c->gpuOk[g] = true;
                return true;
            }
            ++seen;
        }
        return false;
    }

    // CPU：按名称优先级挑一个
    int prio = 99;
    if (wcsstr(name, L"CPU Package"))      prio = 0;
    else if (wcsstr(name, L"Core Average")) prio = 1;
    else if (wcsstr(name, L"Tctl"))         prio = 2;
    else if (wcsstr(name, L"Core Max"))     prio = 3;
    else if (wcsstr(name, L"CPU"))          prio = 4;
    else return false;

    if (!c->cpuOk || prio < c->cpuPriority) {
        c->cpuTempC = (int)(value + 0.5);
        c->cpuPriority = prio;
        c->cpuOk = true;
    }
    return true;
}

bool QueryLhmTemps(LhmCtx* ctx)
{
    if (!ctx) return false;
    ZeroMemory(ctx, sizeof(*ctx));
    ctx->cpuPriority = 99;
    return WmiQuery(L"ROOT\\LibreHardwareMonitor",
                    L"SELECT Name, SensorType, Value, Identifier FROM Sensor",
                    LhmObj, ctx);
}

// ---------------------------------------------------------------- 采集调度

enum TempSource {
    SRC_AUTO = 0,
    SRC_SYSTEM = 1,
    SRC_LHM = 2,
    SRC_OFF = 3,
    SRC_HWINFO = 4
};

// GPU 温度来源优先级：厂商 3 > HWiNFO 2 > LHM 1 > 无 0。
// worker 采集时写，UI 线程 setConfig→ClearAllTemps 也会重置；统一由 g_lock 保护。
int g_gpuTempPrio[NETBALL_MAX_GPU] = {0};

void PublishCpuTemp(bool valid, int celsius)
{
    AcquireSRWLockExclusive(&g_lock);
    g_snap.cpuTempValid = valid ? TRUE : FALSE;
    g_snap.cpuTempC = valid ? celsius : 0;
    ReleaseSRWLockExclusive(&g_lock);
}

// priority：1=LHM 2=HWiNFO；已有更高优先级来源时不覆盖
void MergeGpuTemps(const int* temps, const bool* valid, int priority)
{
    AcquireSRWLockExclusive(&g_lock);
    for (int i = 0; i < NETBALL_MAX_GPU; ++i) {
        if (!valid[i]) continue;
        if (g_gpuTempPrio[i] > priority) continue;
        g_snap.gpuTempC[i] = temps[i];
        g_snap.gpuTempValid[i] = TRUE;
        g_gpuTempPrio[i] = priority;
    }
    ReleaseSRWLockExclusive(&g_lock);
}

void ClearAllTemps()
{
    AcquireSRWLockExclusive(&g_lock);
    g_snap.cpuTempValid = FALSE;
    g_snap.cpuTempC = 0;
    for (int i = 0; i < NETBALL_MAX_GPU; ++i) {
        g_snap.gpuTempValid[i] = FALSE;
        g_snap.gpuTempC[i] = 0;
        g_gpuTempPrio[i] = 0;
    }
    ReleaseSRWLockExclusive(&g_lock);
}

bool HasNonNvidiaGpu()
{
    for (int g = 0; g < g_enumCount; ++g) {
        if (g_gpus[g].vendorId != 0x10DE) return true;
    }
    return false;
}

// 厂商原生温度（NVML / ADL）：3 s 一次；全部失败则退避 30 s
bool CollectVendorGpuTemps()
{
    if (g_enumCount <= 0) return false;

    // v1.4.2：温度或功率任一被需要，都要初始化厂商库
    const bool wantTemp = InterlockedCompareExchange(&g_wantGpuTemp, 0, 0) != 0;
    const bool wantPwr  = InterlockedCompareExchange(&g_wantGpuPwr, 0, 0) != 0;
    if (!wantTemp && !wantPwr) return true;

    InitNvml();
    InitAdl();

    bool any = false;
    for (int g = 0; g < g_enumCount && g < NETBALL_MAX_GPU; ++g) {
        int t = 0;
        bool ok = false;
        const bool vendorTempSupported =
            (g_gpus[g].vendorId == 0x10DE) || (g_gpus[g].vendorId == 0x1002);
        if (g_gpus[g].vendorId == 0x10DE)      ok = ReadNvmlTemp(g_gpus[g].nvmlIndex, &t);
        else if (g_gpus[g].vendorId == 0x1002) ok = ReadAdlTemp(g_gpus[g].adlIndex, &t);

        if (vendorTempSupported) {
            AcquireSRWLockExclusive(&g_lock);
            // 成功才覆盖，并把来源优先级提到最高；失败只降级优先级，保留已显示的值，
            // 让 HWiNFO / LHM 可以补位，同时避免外部源每 5 s 被清成 "--"。
            if (ok) {
                g_snap.gpuTempValid[g] = TRUE;
                g_snap.gpuTempC[g] = t;
                g_gpuTempPrio[g] = 3;
            } else {
                g_gpuTempPrio[g] = 0;
            }
            ReleaseSRWLockExclusive(&g_lock);
        }

        // v1.4.2：GPU 功率。目前只有 NVML（NVIDIA）有可靠接口；
        // AMD 的 ADL2_Overdrive6_CurrentPower_Get 返回值语义无可靠文档，不做。
        if (wantPwr) {
            int mw = 0;
            const bool pwrOk =
                (g_gpus[g].vendorId == 0x10DE) && ReadNvmlPowerMw(g_gpus[g].nvmlIndex, &mw);
            AcquireSRWLockExclusive(&g_lock);
            g_snap.gpuPwrValid[g] = pwrOk ? TRUE : FALSE;
            g_snap.gpuPwrMw[g]    = pwrOk ? mw : -1;
            ReleaseSRWLockExclusive(&g_lock);
        }

        any = any || ok;
    }
    return any;
}

// 外部温度（5 s 一次；全部失败退避 30 s）：
//   自动 = ACPI → HWiNFO → LHM；仅内置 = ACPI；仅 HWiNFO / 仅 LHM = 对应来源。
bool CollectWmiTemps(int source)
{
    bool ok = false;
    bool cpuOk = false;
    int  cpuC = 0;

    const bool wantCpu = InterlockedCompareExchange(&g_wantCpuTemp, 0, 0) != 0;
    const bool wantGpu = InterlockedCompareExchange(&g_wantGpuTemp, 0, 0) != 0;
    const bool external = (source == SRC_LHM || source == SRC_HWINFO || source == SRC_AUTO);
    if (!wantCpu && !(wantGpu && external)) return true;

    // v1.4.2：系统接口优先走 PDH 热区。
    // 实测 MSAcpi_ThermalZoneTemperature 在非提权进程下 IEnumWbemClassObject::Next
    // 返回 0x80041003（WBEM_E_ACCESS_DENIED），所以旧路径在 asInvoker 下永远读不到。
    // PDH 的 Thermal Zone Information 读的是同一份 ACPI 热区数据，但走性能计数器通道，
    // 不受 WMI 类 ACL 限制；PDH 不可用时才退回 ACPI WMI（提权环境仍有 0.1 K 精度）。
    if ((source == SRC_AUTO || source == SRC_SYSTEM) && wantCpu) {
        int deciK = 0;
        if (ReadThermalZoneDeciK(&deciK)) {
            const double c = (double)deciK / 10.0 - 273.15;
            cpuC = (int)(c + (c >= 0.0 ? 0.5 : -0.5));
            cpuOk = true;
            ok = true;
        } else {
            cpuOk = QueryAcpiCpuTemp(&cpuC);
            ok = cpuOk;
        }
    }

    // HWiNFO 共享内存：读取成本极低，自动模式总是先试（ACPI 已有时只补 GPU）
    bool hwGpuOk = false;
    if (source == SRC_HWINFO || source == SRC_AUTO) {
        sensors_hwinfo::Snapshot hs;
        if (sensors_hwinfo::read(&hs)) {
            if (!cpuOk && hs.cpuValid) {
                cpuC = hs.cpuTempC;
                cpuOk = true;
                ok = true;
            }
            MergeGpuTemps(hs.gpuTempC, hs.gpuValid, 2);
            hwGpuOk = hs.gpuCount > 0;
        }
    }

    // 自动模式下仅在仍缺数据时才走 LHM（WMI RPC 较贵）：
    //   CPU 没拿到，或存在非 NVIDIA 卡且 HWiNFO 没给 GPU 温度。
    const bool wantLhm = (source == SRC_LHM) ||
                         (source == SRC_AUTO &&
                          ((wantCpu && !cpuOk) || (wantGpu && HasNonNvidiaGpu() && !hwGpuOk)));
    if (wantLhm) {
        LhmCtx ctx;
        if (QueryLhmTemps(&ctx)) {
            if (!cpuOk && ctx.cpuOk) {
                cpuC = ctx.cpuTempC;
                cpuOk = true;
            }
            MergeGpuTemps(ctx.gpuTemp, ctx.gpuOk, 1);
            ok = ok || ctx.cpuOk;
        }
    }

    PublishCpuTemp(cpuOk, cpuC);
    return ok;
}

// v1.4.2：CPU 封装功率。返回值只表示"本次采集有没有出错"（用于决定退避），
// 不表示一定有值：没显示功率项时直接跳过，也不清空快照。
bool CollectCpuPower()
{
    if (!InterlockedCompareExchange(&g_wantCpuPwr, 0, 0)) return true;

    int mw = 0;
    const bool ok = ReadCpuPowerMw(&mw);

    AcquireSRWLockExclusive(&g_lock);
    g_snap.cpuPwrValid = ok ? TRUE : FALSE;
    g_snap.cpuPwrMw    = ok ? mw : -1;
    ReleaseSRWLockExclusive(&g_lock);
    return ok;
}

void PublishGpuNames()
{
    AcquireSRWLockExclusive(&g_lock);
    for (int i = 0; i < NETBALL_MAX_GPU; ++i) {
        if (i < g_enumCount) CopyDescName(g_gpus[i].name, g_snap.gpuName[i], 64);
        else g_snap.gpuName[i][0] = 0;
    }
    g_snap.gpuCount = g_enumCount;
    ReleaseSRWLockExclusive(&g_lock);
}

DWORD WINAPI WorkerProc(LPVOID)
{
    g_threadId = GetCurrentThreadId();
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    ULONGLONG nextLoad    = 0;
    ULONGLONG nextGpuTemp = 0;
    ULONGLONG nextWmi     = 0;
    ULONGLONG nextCpuPwr  = 0;      // v1.4.2

    for (;;) {
        if (g_stop) break;

        const ULONGLONG now = GetTickCount64();
        const int source = (int)InterlockedCompareExchange(&g_tempSrc, 0, 0);

        if (InterlockedExchange(&g_needRescan, 0) != 0) {
            EnumerateGpus();
            PublishGpuNames();
            nextLoad    = 0;
            nextGpuTemp = 0;
            nextWmi     = 0;
            nextCpuPwr  = 0;
        }

        if (now >= nextLoad) {
            CollectGpuLoad();
            nextLoad = now + 1000;
        }

        if (source != SRC_OFF && now >= nextGpuTemp) {
            const bool ok = CollectVendorGpuTemps();
            nextGpuTemp = now + (ok ? 3000 : 30000);
        }

        if (source != SRC_OFF && now >= nextWmi) {
            const bool ok = CollectWmiTemps(source);
            nextWmi = now + (ok ? 5000 : 30000);
        }

        // v1.4.2：CPU 功率是独立指标，不吃 tempSrc 的"关闭温度"开关
        if (now >= nextCpuPwr) {
            const bool ok = CollectCpuPower();
            nextCpuPwr = now + (ok ? 1000 : 60000);
        }

        WaitForSingleObject(g_wake, 250);
    }

    ShutdownPdh();
    ShutdownNvml();
    ShutdownAdl();
    sensors_hwinfo::shutdown();
    CoUninitialize();
    return 0;
}

} // namespace

// ============================================================================
// 公开接口
// ============================================================================

bool sensors::start(const AppConfig* c)
{
    if (c) InterlockedExchange(&g_tempSrc, (LONG)c->tempSrc);

    // 退出流程优先：stop() join 超时后 g_thread 仍非空，但线程已不可再用
    if (InterlockedCompareExchange(&g_stopping, 0, 0) != 0) return false;
    if (g_thread) return true;

    g_wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!g_wake) return false;

    InterlockedExchange(&g_stop, 0);
    InterlockedExchange(&g_needRescan, 1);

    // 首帧语义：GPU 负载 -1（无效）而不是 0，避免启动瞬间显示成 0%
    AcquireSRWLockExclusive(&g_lock);
    g_snap.cpuTempValid = FALSE;
    g_snap.cpuTempC     = 0;
    g_snap.cpuPwrValid  = FALSE;
    g_snap.cpuPwrMw     = -1;
    g_snap.gpuCount     = 0;
    for (int i = 0; i < NETBALL_MAX_GPU; ++i) {
        g_snap.gpuLoad[i]      = -1;
        g_snap.gpuTempValid[i] = FALSE;
        g_snap.gpuTempC[i]     = 0;
        g_snap.gpuName[i][0]   = 0;
        g_snap.gpuPwrValid[i]  = FALSE;
        g_snap.gpuPwrMw[i]     = -1;
    }
    ReleaseSRWLockExclusive(&g_lock);

    g_thread = CreateThread(nullptr, 0, WorkerProc, nullptr, 0, nullptr);
    if (!g_thread) {
        CloseHandle(g_wake);
        g_wake = nullptr;
        return false;
    }
    SetThreadPriority(g_thread, THREAD_PRIORITY_BELOW_NORMAL);
    return true;
}

void sensors::stop()
{
    if (!g_thread) return;
    InterlockedExchange(&g_stopping, 1);   // 之后不再允许 start()
    InterlockedExchange(&g_stop, 1);
    if (g_wake) SetEvent(g_wake);

    const DWORD wait = WaitForSingleObject(g_thread, 2000);
    if (wait != WAIT_OBJECT_0) {
        // 线程卡在 WMI/PDH 之类的 RPC 里：不强杀、也不关闭任何句柄。
        // worker 下一轮还要用 g_wake 等待，关掉它会让线程等在一个已关闭（甚至被复用）
        // 的句柄上；把 g_thread 置空还会让 start() 起来第二个线程。宁可留到进程退出回收。
        OutputDebugStringW(L"[NetBall] sensors thread join timeout, handles kept for process exit\n");
        return;
    }

    CloseHandle(g_thread);
    g_thread = nullptr;
    if (g_wake) {
        CloseHandle(g_wake);
        g_wake = nullptr;
    }
    g_threadId = 0;
}

void sensors::setConfig(const AppConfig* c)
{
    if (!c) return;
    const LONG src = (c->tempSrc < 0 || c->tempSrc > 4) ? 0 : (LONG)c->tempSrc;
    const LONG oldSrc = InterlockedExchange(&g_tempSrc, src);

    // 排版里没有温度项 → 不加载 WMI / NVML / ADL（省 ~30MB 常驻与周期性 RPC）
    const LONG wantCpu = (SpecHasCpuTemp(c->tbLayout) || SpecHasCpuTemp(c->ballLayout)) ? 1 : 0;
    const LONG wantGpu = (SpecHasGpuTemp(c->tbLayout) || SpecHasGpuTemp(c->ballLayout)) ? 1 : 0;
    const LONG wantLoad = (SpecHasGpuItem(c->tbLayout) || SpecHasGpuItem(c->ballLayout)) ? 1 : 0;
    // v1.4.2：功率项的门控
    const LONG wantCpuPwr = (SpecHasCpuPwr(c->tbLayout) || SpecHasCpuPwr(c->ballLayout)) ? 1 : 0;
    const LONG wantGpuPwr = (SpecHasGpuPwr(c->tbLayout) || SpecHasGpuPwr(c->ballLayout)) ? 1 : 0;
    const LONG oldCpu  = InterlockedExchange(&g_wantCpuTemp, wantCpu);
    const LONG oldGpu  = InterlockedExchange(&g_wantGpuTemp, wantGpu);
    const LONG oldLoad = InterlockedExchange(&g_wantGpuLoad, wantLoad);
    const LONG oldCpuPwr = InterlockedExchange(&g_wantCpuPwr, wantCpuPwr);
    const LONG oldGpuPwr = InterlockedExchange(&g_wantGpuPwr, wantGpuPwr);
    const LONG old = oldSrc;
    if (old != src || oldCpu != wantCpu || oldGpu != wantGpu || oldLoad != wantLoad ||
        oldCpuPwr != wantCpuPwr || oldGpuPwr != wantGpuPwr) {
        // 换数据源时先清空，避免把上一个源的值当成新源的结果
        ClearAllTemps();
        if (src == SRC_OFF) {
            AcquireSRWLockExclusive(&g_lock);
            for (int i = 0; i < NETBALL_MAX_GPU; ++i) g_snap.gpuLoad[i] = -1;
            ReleaseSRWLockExclusive(&g_lock);
        }
        if (g_wake) SetEvent(g_wake);
    }
}

void sensors::get(Metrics* out)
{
    if (!out) return;
    AcquireSRWLockShared(&g_lock);
    out->cpuTempC     = g_snap.cpuTempC;
    out->cpuTempValid = g_snap.cpuTempValid;
    out->cpuPwrMw     = g_snap.cpuPwrMw;
    out->cpuPwrValid  = g_snap.cpuPwrValid;
    out->gpuCount     = g_snap.gpuCount;
    for (int i = 0; i < NETBALL_MAX_GPU; ++i) {
        out->gpuLoad[i]      = g_snap.gpuLoad[i];
        out->gpuTempC[i]     = g_snap.gpuTempC[i];
        out->gpuTempValid[i] = g_snap.gpuTempValid[i];
        out->gpuPwrMw[i]     = g_snap.gpuPwrMw[i];
        out->gpuPwrValid[i]  = g_snap.gpuPwrValid[i];
        CopyDescName(g_snap.gpuName[i], out->gpuName[i], 64);
    }
    ReleaseSRWLockShared(&g_lock);
}

void sensors::rescanGpus()
{
    InterlockedExchange(&g_needRescan, 1);
    if (g_wake) SetEvent(g_wake);
}

int sensors::gpuCount()
{
    return (int)InterlockedCompareExchange(&g_gpuCount, 0, 0);
}
