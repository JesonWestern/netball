// src/metrics.cpp —— 网速与内存采样（T3 / AC-3 AC-4）
//
// 【网速口径（已由 Leader 批准并写入 DECISIONS，勿改回字面口径）】
//   统计范围 = OperStatus == IfOperStatusUp
//            ∧ IfType ∉ { IF_TYPE_SOFTWARE_LOOPBACK(24), IF_TYPE_TUNNEL(131) }
//            ∧ InterfaceAndOperStatusFlags.FilterInterface == 0
//   取这些接口的 InOctets / OutOctets（ULONG64）累加，与上一次快照相减，除以两次采样
//   之间由 QueryPerformanceCounter 测得的真实时间差，得到字节/秒。
//   计数器回绕 / 被重置（本次总量小于上次）时，该区间速度置 0 并重建基准，绝不输出可疑值。
//
// 【为什么必须跳过 FilterInterface —— 实测依据，勿删，T10 审计依赖此注释】
//   Windows 的 GetIfTable2 会为同一块物理网卡按 NDIS 过滤层各返回一行，例如本机：
//     Realtek PCIe GbE Family Controller                            （真实网卡，hw=1 flt=0）
//     Realtek ...-WFP Native MAC Layer LightWeight Filter-0000      （hw=0 flt=1）
//     Realtek ...-QoS Packet Scheduler-0000                         （hw=0 flt=1）
//     Realtek ...-WFP 802.3 MAC Layer LightWeight Filter-0000       （hw=0 flt=1）
//   这些行的 InOctets / OutOctets 与父网卡【完全相同】，是同一份流量的副本，不是新增流量。
//   实测（同一 3 秒窗口）：上述 4 行 inDelta 均 = 136455、outDelta 均 = 192302；
//   Wi-Fi 侧（MediaTek Wi-Fi 6 MT7921）为 5 行同值。
//   若按字面口径全量累加：all-up in=546264 / out=769580，而 hardware-only 仅
//   in=136529 / out=192364 —— 约 4 倍放大；与 verifier 独立脚本测得的
//   inflation = 4.00×（21 行合计 19,822,517,840 vs 去重 7 行 4,951,641,908）一致。
//   去重后显示值才与 curl 实下字节数/耗时吻合（20,000,000 字节，采样合计 ≈20.7MB）。
//   注：只排除 Windows 分层过滤伪接口，不排除 VMware / WAN Miniport 等虚拟适配器。
//
// 时间基准：QueryPerformanceCounter（高精度），失败时退化为 GetTickCount64。
// 线程安全：std::mutex 保护内部状态；API 调用（GetIfTable2 / GlobalMemoryStatusEx）
//           全部在锁外完成，不让任何调用方长时间等锁。
// 不启动任何线程：纯被动由调用方定时调用 sample()。
//
// 链接注意：需要 -liphlpapi（GetIfTable2 / FreeMibTable）。

#include "metrics.h"
#include "sensors.h"

// 注意包含顺序：mingw-w64 的 netioapi.h 只有在 _WS2IPDEF_ 已定义时才会声明
// MIB_IF_ROW2 / MIB_IF_TABLE2 / GetIfTable2，因此必须先引入 ws2ipdef.h。
// （app_common.h 已带 WIN32_LEAN_AND_MEAN，windows.h 不会拉入 winsock.h）
#include <winsock2.h>
#include <ws2ipdef.h>
#include <iphlpapi.h>
#include <netioapi.h>

#include <mutex>

namespace {

// 超过 100 Gbps 视为计数器异常（重置/回绕未按常规表现），直接丢弃该区间。
const double kMaxPlausibleBps = 100.0 * 1000.0 * 1000.0 * 1000.0;

struct IfTotals {
    ULONG64 in;
    ULONG64 out;
    bool    ok;
    IfTotals() : in(0), out(0), ok(false) {}
};

std::mutex g_mtx;
bool       g_haveBaseline = false;
LONGLONG   g_lastTicks    = 0;
LONGLONG   g_lastFreq     = 0;
ULONG64    g_lastIn       = 0;
ULONG64    g_lastOut      = 0;

// ---- v1.2.0 CPU 负载（GetSystemTimes 差分）----
// kernel 时间已包含 idle，所以 busy = (kernel - idle) + user。
// 首次采样没有基准，输出 -1（渲染成 "--"），不伪造 0%。
bool     g_haveCpuBase = false;
ULONGLONG g_prevIdle   = 0;
ULONGLONG g_prevKernel = 0;
ULONGLONG g_prevUser   = 0;

ULONGLONG FileTimeToU64(const FILETIME& ft)
{
    ULARGE_INTEGER u;
    u.LowPart  = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    return u.QuadPart;
}

int SampleCpuPercent()
{
    FILETIME idleFt, kernelFt, userFt;
    if (!GetSystemTimes(&idleFt, &kernelFt, &userFt)) return -1;

    const ULONGLONG idle   = FileTimeToU64(idleFt);
    const ULONGLONG kernel = FileTimeToU64(kernelFt);
    const ULONGLONG user   = FileTimeToU64(userFt);

    int percent = -1;
    if (g_haveCpuBase && idle >= g_prevIdle && kernel >= g_prevKernel && user >= g_prevUser) {
        const ULONGLONG idleD   = idle   - g_prevIdle;
        const ULONGLONG kernelD = kernel - g_prevKernel;
        const ULONGLONG userD   = user   - g_prevUser;
        const ULONGLONG total   = kernelD + userD;      // kernel 已含 idle
        if (total > 0) {
            const ULONGLONG busy = (kernelD > idleD ? kernelD - idleD : 0) + userD;
            int v = (int)((busy * 100ULL + total / 2) / total);
            if (v < 0) v = 0;
            if (v > 100) v = 100;
            percent = v;
        }
    }

    g_prevIdle   = idle;
    g_prevKernel = kernel;
    g_prevUser   = user;
    g_haveCpuBase = true;
    return percent;
}

void QueryTicks(LONGLONG* ticks, LONGLONG* freq)
{
    LARGE_INTEGER f;
    LARGE_INTEGER c;
    if (QueryPerformanceFrequency(&f) && f.QuadPart > 0 && QueryPerformanceCounter(&c)) {
        *ticks = c.QuadPart;
        *freq  = f.QuadPart;
        return;
    }
    *ticks = (LONGLONG)GetTickCount64();
    *freq  = 1000;
}

// 汇总所有“真实在用”网卡的 64 位字节计数器。
IfTotals SumIfOctets()
{
    IfTotals t;

    PMIB_IF_TABLE2 table = NULL;
    if (GetIfTable2(&table) != NO_ERROR || table == NULL) {
        return t;
    }

    for (ULONG i = 0; i < table->NumEntries; ++i) {
        const MIB_IF_ROW2& r = table->Table[i];

        if ((UINT)r.OperStatus != (UINT)IfOperStatusUp) {
            continue;
        }
        if ((UINT)r.Type == (UINT)IF_TYPE_SOFTWARE_LOOPBACK) { // 24
            continue;
        }
        if ((UINT)r.Type == (UINT)IF_TYPE_TUNNEL) {            // 131
            continue;
        }
        // Windows 的 WFP / QoS / Native-*-Filter 等分层过滤接口会镜像同一块物理网卡的
        // 计数器（本机实测：Realtek 网卡连同 3 个过滤层共 4 行，增量完全相同）。
        // 不过滤会把真实速度放大 4~5 倍，因此跳过 FilterInterface。
        if (r.InterfaceAndOperStatusFlags.FilterInterface) {
            continue;
        }

        t.in  += (ULONG64)r.InOctets;
        t.out += (ULONG64)r.OutOctets;
    }

    FreeMibTable(table);
    t.ok = true;
    return t;
}

void FillMemoryInfo(Metrics* m)
{
    MEMORYSTATUSEX ms;
    ms.dwLength = sizeof(ms);
    if (!GlobalMemoryStatusEx(&ms)) {
        return;
    }
    m->memPercent    = (int)ms.dwMemoryLoad;
    m->memTotalBytes = (ULONGLONG)ms.ullTotalPhys;
    m->memUsedBytes  = (ULONGLONG)ms.ullTotalPhys - (ULONGLONG)ms.ullAvailPhys;
}

} // namespace

void metrics::init()
{
    LONGLONG ticks = 0;
    LONGLONG freq  = 0;
    QueryTicks(&ticks, &freq);

    IfTotals t = SumIfOctets();

    {
        std::lock_guard<std::mutex> lk(g_mtx);
        g_lastTicks    = ticks;
        g_lastFreq     = freq;
        g_lastIn       = t.in;
        g_lastOut      = t.out;
        g_haveBaseline = t.ok;   // 取表失败则留待首次 sample() 重建基准
    }

    SampleCpuPercent();          // 建立 CPU 基准（本次返回值无效，不输出）
    sensors::start(NULL);        // 后台线程：GPU 负载 / 温度（慢调用不进 UI 线程）
}

void metrics::sample(Metrics* m)
{
    if (m == NULL) {
        return;
    }

    *m = Metrics();   // 全 0 初始化
    // 无效值语义：-1 / FALSE，渲染层统一输出 "--"（0 是合法读数，不能用来表示无效）
    m->cpuPercent = -1;
    for (int i = 0; i < NETBALL_MAX_GPU; ++i) m->gpuLoad[i] = -1;

    LONGLONG ticks = 0;
    LONGLONG freq  = 0;
    QueryTicks(&ticks, &freq);

    // 锁外完成所有可能耗时的系统调用
    IfTotals cur = SumIfOctets();
    FillMemoryInfo(m);
    m->cpuPercent = SampleCpuPercent();
    sensors::get(m);              // 合并后台线程的 CPU 温度 / GPU 快照（只读锁，微秒级）

    std::lock_guard<std::mutex> lk(g_mtx);

    double dtSec = 0.0;
    bool   reset = false;

    if (g_haveBaseline && cur.ok && g_lastFreq > 0) {
        dtSec = (double)(ticks - g_lastTicks) / (double)g_lastFreq;
        if (dtSec > 0.0 && (cur.in < g_lastIn || cur.out < g_lastOut)) {
            reset = true;    // 计数器回绕 / 网卡增减导致的总量下降
        }
    }

    if (dtSec > 0.0 && !reset && cur.ok) {
        // InOctets  = 网卡收到的字节 = 下行；OutOctets = 网卡发出的字节 = 上行
        double down = (double)(cur.in  - g_lastIn)  / dtSec;
        double up   = (double)(cur.out - g_lastOut) / dtSec;

        if (down >= 0.0 && up >= 0.0 &&
            down <= kMaxPlausibleBps && up <= kMaxPlausibleBps) {
            m->downBps = down;
            m->upBps   = up;
            m->netValid = true;
        }
        // 越界值保持 0 且 netValid=false（不伪造数值）
    }

    if (cur.ok) {
        g_lastTicks    = ticks;
        g_lastFreq     = freq;
        g_lastIn       = cur.in;
        g_lastOut      = cur.out;
        g_haveBaseline = true;
    }
    // cur.ok == false 时保留旧基准，下一次成功采样仍可算出覆盖更长时间区间的平均速度
}

void metrics::shutdown()
{
    sensors::stop();              // 先停后台线程（限时 join），再清基准
    std::lock_guard<std::mutex> lk(g_mtx);
    g_haveBaseline = false;
    g_lastTicks    = 0;
    g_lastFreq     = 0;
    g_lastIn       = 0;
    g_lastOut      = 0;
}
