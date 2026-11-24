// src/sensors_hwinfo.cpp —— HWiNFO 共享内存温度源实现（v1.3.0）
//
// 协议来自 HWiNFO 官方公开的 HWiNFO_SENSORS_SHARED_MEM2 布局：
//   header（签名 'HWiS' + 版本 + 两个区段偏移/元素大小/数量）
//   → 传感器数组（每个 264 B）
//   → 读数数组（每个 312 B）
// 这里只做只读解析，所有长度都按 header 里的 dwSizeOfXxxElement 步进，
// 因此 HWiNFO 以后追加字段也能兼容。
#include "sensors_hwinfo.h"

#include <windows.h>
#include <cstring>

namespace {

const DWORD kSignature = 0x53695748;      // 'HWiS'（小端）
const ULONGLONG kRetryMs = 30000;         // 共享内存不可用时 30 s 退避

#pragma pack(push, 8)
struct SmHeader {
    DWORD   dwSignature;
    DWORD   dwVersion;
    DWORD   dwRevision;
    __int64 poll_time;
    DWORD   dwOffsetOfSensorSection;
    DWORD   dwSizeOfSensorElement;
    DWORD   dwNumSensorElements;
    DWORD   dwOffsetOfReadingSection;
    DWORD   dwSizeOfReadingElement;
    DWORD   dwNumReadingElements;
};

struct SmSensor {
    DWORD dwSensorID;
    DWORD dwSensorInst;
    char  szSensorNameOrig[128];
    char  szSensorNameUser[128];
};

struct SmReading {
    DWORD  dwSensorIndex;
    DWORD  dwReadingID;
    char   szLabelOrig[128];
    char   szLabelUser[128];
    char   szUnit[16];
    double Value;
    double ValueMin;
    double ValueMax;
    double ValueAvg;
};
#pragma pack(pop)

HANDLE      g_map       = nullptr;
const BYTE* g_base      = nullptr;
SIZE_T      g_mapped    = 0;
ULONGLONG   g_lastTry   = 0;
ULONGLONG   g_lastOpen  = 0;

bool ContainsNoCase(const char* hay, const char* needle)
{
    if (!hay || !needle || !*needle) return false;
    const size_t n = strlen(needle);
    for (const char* p = hay; *p; ++p) {
        size_t i = 0;
        while (i < n && p[i]) {
            char a = p[i];
            char b = needle[i];
            if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
            if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
            if (a != b) break;
            ++i;
        }
        if (i == n) return true;
    }
    return false;
}

// 只接受摄氏度（ANSI 下 °C 至少包含 'C'，同时排除华氏 F）
bool IsCelsiusUnit(const char* unit)
{
    if (!unit || !*unit) return false;
    if (!ContainsNoCase(unit, "C")) return false;
    if (ContainsNoCase(unit, "F")) return false;
    return true;
}

void CloseMapping()
{
    if (g_base) { UnmapViewOfFile(g_base); g_base = nullptr; }
    if (g_map)  { CloseHandle(g_map);      g_map  = nullptr; }
    g_mapped = 0;
}

bool EnsureOpen()
{
    const ULONGLONG now = GetTickCount64();

    if (g_base) {
        // HWiNFO 重启会销毁旧 section：映射还在但数据不再更新（温度冻结）。
        // 每 30 s 重开一次，HWiNFO 不在时自然退化为不可用。
        if (now - g_lastOpen < kRetryMs) return true;
        CloseMapping();
    }

    if (g_lastTry && now - g_lastTry < kRetryMs) return false;
    g_lastTry = now;

    g_map = OpenFileMappingW(FILE_MAP_READ, FALSE, L"Global\\HWiNFO_SENS_SM2");
    if (!g_map) g_map = OpenFileMappingW(FILE_MAP_READ, FALSE, L"HWiNFO_SENS_SM2");
    if (!g_map) return false;

    g_base = (const BYTE*)MapViewOfFile(g_map, FILE_MAP_READ, 0, 0, 0);
    if (!g_base) { CloseMapping(); return false; }

    // 共享内存的 header 来自外部进程：必须按“实际映射长度”校验区段，否则损坏的
    // offset/count 会让下面的遍历直接越界读。
    MEMORY_BASIC_INFORMATION mbi;
    ZeroMemory(&mbi, sizeof(mbi));
    g_mapped = (VirtualQuery(g_base, &mbi, sizeof(mbi)) > 0) ? mbi.RegionSize : 0;

    const SmHeader* h = (const SmHeader*)g_base;
    const SIZE_T sensorEnd  = (SIZE_T)h->dwOffsetOfSensorSection +
                              (SIZE_T)h->dwNumSensorElements * h->dwSizeOfSensorElement;
    const SIZE_T readingEnd = (SIZE_T)h->dwOffsetOfReadingSection +
                              (SIZE_T)h->dwNumReadingElements * h->dwSizeOfReadingElement;
    const bool sane =
        g_mapped > sizeof(SmHeader) &&
        h->dwSignature == kSignature &&
        h->dwSizeOfSensorElement  >= sizeof(SmSensor) &&
        h->dwSizeOfReadingElement >= sizeof(SmReading) &&
        h->dwNumSensorElements  <= 512 &&
        h->dwNumReadingElements <= 16384 &&
        sensorEnd  <= g_mapped &&
        readingEnd <= g_mapped;
    if (!sane) { CloseMapping(); return false; }

    g_lastOpen = now;
    return true;
}

} // namespace

bool sensors_hwinfo::read(Snapshot* out)
{
    if (!out) return false;
    ZeroMemory(out, sizeof(*out));
    if (!EnsureOpen()) return false;
    out->opened = true;

    const SmHeader*  h        = (const SmHeader*)g_base;
    const SmSensor*  sensors  = (const SmSensor*)(g_base + h->dwOffsetOfSensorSection);
    const SmReading* readings = (const SmReading*)(g_base + h->dwOffsetOfReadingSection);

    bool cpuDone = false;
    int  gpuCount = 0;

    for (DWORD i = 0; i < h->dwNumReadingElements; ++i) {
        const SmReading* r = (const SmReading*)((const BYTE*)readings +
                                                (size_t)i * h->dwSizeOfReadingElement);
        if (r->dwSensorIndex >= h->dwNumSensorElements) continue;

        // 外部共享内存的定长字段不保证 NUL 结尾：复制到本地缓冲并强制截断
        char unit[17];
        memcpy(unit, r->szUnit, 16);
        unit[16] = 0;
        if (!IsCelsiusUnit(unit)) continue;

        const int t = (int)(r->Value + 0.5);
        if (t <= 0 || t > 150) continue;      // 0/异常值视为无效

        const SmSensor* s = (const SmSensor*)((const BYTE*)sensors +
                                              (size_t)r->dwSensorIndex * h->dwSizeOfSensorElement);
        char label[129];
        memcpy(label, (r->szLabelOrig[0] ? r->szLabelOrig : r->szLabelUser), 128);
        label[128] = 0;
        char sname[129];
        memcpy(sname, (s->szSensorNameOrig[0] ? s->szSensorNameOrig : s->szSensorNameUser), 128);
        sname[128] = 0;

        if (!cpuDone && ContainsNoCase(sname, "CPU") &&
            (ContainsNoCase(label, "CPU Package") || ContainsNoCase(label, "Tctl") ||
             ContainsNoCase(label, "Tdie")        || ContainsNoCase(label, "CPU"))) {
            out->cpuValid = true;
            out->cpuTempC = t;
            cpuDone = true;
            continue;
        }

        if (ContainsNoCase(sname, "GPU") && gpuCount < NETBALL_MAX_GPU &&
            (ContainsNoCase(label, "GPU Temperature") || ContainsNoCase(label, "GPU Temp"))) {
            out->gpuValid[gpuCount] = true;
            out->gpuTempC[gpuCount] = t;
            ++gpuCount;
        }
    }

    out->gpuCount = gpuCount;
    return out->cpuValid || gpuCount > 0;
}

void sensors_hwinfo::shutdown()
{
    CloseMapping();
}
