// src/metrics.h —— 数据层：网速/内存/CPU 负载 + 传感器快照合并（T3）
// 实现见 metrics.cpp；对外契约在 app_common.h 中冻结，本头文件仅做包含与说明。
#pragma once

#include "app_common.h"

// namespace metrics 的声明位于 app_common.h：
//   void init();              建立首帧基准（网速计数器快照 + CPU 时间基准），并启动 sensors 线程
//   void sample(Metrics* m);  被动采样一次，不阻塞调用方，线程安全
//   void shutdown();          停止 sensors 线程并清空内部状态
//
// 口径说明：
//   - 网速：GetIfTable2 全接口 InOctets/OutOctets 累加，仅统计 OperStatus=Up 且
//     类型不为 loopback(24)/tunnel(131)、且不是分层过滤伪接口的网卡；
//     两次采样差 / 时间差 = 字节/秒。
//   - 内存：GlobalMemoryStatusEx。
//   - CPU 负载：GetSystemTimes 差分，busy = (kernel - idle) + user，kernel 已包含 idle。
//     首帧没有基准，输出 -1（渲染成 "--"）。
//   - CPU 温度 / GPU 负载 / GPU 温度：由 sensors 后台线程采集，本层只做快照合并；
//     无效值语义为 -1 / FALSE，渲染层统一显示 "--"。
