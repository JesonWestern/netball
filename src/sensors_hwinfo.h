// src/sensors_hwinfo.h —— HWiNFO 共享内存温度源（v1.3.0）
//
// HWiNFO（免费版即可）在「设置 → 安全 / Safety」里勾选 Shared Memory Support 后，
// 会创建全局共享内存 Global\HWiNFO_SENS_SM2。本模块只读映射，不写、不需要驱动，
// 也不需要管理员权限。
//
// 约定：
//   · 只在 sensors 后台线程调用；失败 30 s 退避，不会阻塞 UI。
//   · 返回 false 表示共享内存不可用或没有可用读数，调用方应回退到其他来源。
//   · 温度单位固定为摄氏度；HWiNFO 的华氏/其他单位会被忽略。
#pragma once

#include "app_common.h"

namespace sensors_hwinfo {

struct Snapshot {
    bool opened;                                // 共享内存可访问
    bool cpuValid;
    int  cpuTempC;
    int  gpuCount;                              // 0..NETBALL_MAX_GPU
    bool gpuValid[NETBALL_MAX_GPU];
    int  gpuTempC[NETBALL_MAX_GPU];
};

// 读取一次当前快照。成功（至少一个温度有效）返回 true。
bool read(Snapshot* out);

// 释放共享内存映射（进程退出时调用）。
void shutdown();

} // namespace sensors_hwinfo
