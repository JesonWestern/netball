// src/sensors.h —— CPU/GPU 传感器后台线程（v1.2.0）
//
// 设计约束（改前先读）：
//   1. UI 线程绝不直接调用 PDH / NVML / ADL / WMI：WMI 一次 RPC 可能几十到几百毫秒，
//      放在采样定时器里会卡住消息循环。全部工作在本模块的后台线程完成。
//   2. 线程只写自己的快照（SRWLOCK 保护），metrics::sample() 每周期把快照合并进
//      Metrics，UI 侧看到的永远是「上一次成功采集的值」。
//   3. 任何一步失败都只让对应字段变成 -1 / FALSE（渲染成 "--"），绝不抛出、不崩溃、
//      不影响网速与内存的既有采样。
//   4. 温度数据源可选：0 自动（系统接口优先，失败后尝试外部 LHM）、1 仅系统接口、
//      2 仅外部 LibreHardwareMonitor、3 关闭（不采集温度）。
//      【v1.4.2】"系统接口"= PDH \Thermal Zone Information（免管理员），
//      PDH 不可用时才退回 ACPI WMI。原因：非提权进程查询
//      MSAcpi_ThermalZoneTemperature 会在 IEnumWbemClassObject::Next 收到
//      0x80041003（WBEM_E_ACCESS_DENIED），旧路径在 asInvoker 下永远读不到。
//      该值语义是"主板/EC 定义的系统热区"，不是核心结温（Tctl/Tdie）。
//   5. 采样频率与实现代价：
//        GPU 负载  PDH 通配计数器 + 实例名解析，约 0.5~1 ms，每 1 s
//        GPU 温度  NVML（NVIDIA，随驱动，无需管理员）/ ADL（AMD，尽力），每 3 s
//        CPU 温度  PDH 热区（v1.4.2 起的首选，<0.1 ms）每 5 s；WMI 仅在 PDH 失败时兜底
//        CPU 功率  PDH Energy Meter（RAPL），每 1 s（v1.4.2）
//        GPU 功率  NVML nvmlDeviceGetPowerUsage，随 GPU 温度同周期 3 s（v1.4.2）
#pragma once

#include "app_common.h"

namespace sensors {

// 启动后台线程（c 可为空，此时使用内置默认：自动数据源）。可重复调用，第二次为空操作。
bool start(const AppConfig* c);

// 停止并 join（限时 2 s），必须在进程退出前调用
void stop();

// 配置变化（温度数据源）时调用；线程内只读取缓存值，不阻塞调用方
void setConfig(const AppConfig* c);

// 把当前快照写入 m 的 CPU/GPU 字段（其余字段不动）。线程安全。
void get(Metrics* out);

// 显示设置变化 / WM_DISPLAYCHANGE 后重新枚举显卡
void rescanGpus();

// 当前枚举到的 GPU 数量（0..NETBALL_MAX_GPU）
int gpuCount();

} // namespace sensors
