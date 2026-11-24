// src/layout_model.h —— 排版模型（v1.2.0）
//
// 唯一真源：任务栏面板与悬浮球共用同一套「项目 + 行」模型。
//
// spec 语法（存 ini 的单个字符串，同时编码启用集合 / 行归属 / 行内顺序）：
//     '/' 分行，',' 分项目
//     例：L"down,up/cpuload/gpu0temp"  → 行1: 下行 上行；行2: CPU 负载；行3: GPU0 温度
//
// 约束（normalize 强制保证）：
//   · 行内顺序 = 项目在 spec 中出现的先后；
//   · 行号连续（空行被删除并压实），每行至少 1 个项目；
//   · 行数 ≤ 项目数；
//   · 任务栏最多 NETBALL_MAX_TB_ROWS 行，超出部分并入最后一行；
//   · 未知 / 重复 token 丢弃。
#pragma once

#include "app_common.h"

namespace layout_model {

enum Surface {
    SURF_TASKBAR = 0,
    SURF_BALL    = 1
};

// 项目 ID（顺序即设置窗里的分组顺序，不要随意重排：spec token 与它无关，但 UI 依赖）
enum ItemId {
    ITEM_DOWN = 0,      // ↓ 下行速度
    ITEM_UP,            // ↑ 上行速度
    ITEM_MEM,           // 内存占用
    ITEM_CPULOAD,       // CPU 负载率
    ITEM_CPUTEMP,       // CPU 温度
    ITEM_GPU0LOAD,      // GPU0 负载率
    ITEM_GPU0TEMP,      // GPU0 温度
    ITEM_GPU1LOAD,
    ITEM_GPU1TEMP,
    ITEM_GPU2LOAD,
    ITEM_GPU2TEMP,
    ITEM_GPU3LOAD,
    ITEM_GPU3TEMP,
    // v1.4.2 追加：既有 ID 一律不动，新项全部排在末尾，
    // 避免影响任何按 ID 记忆的旧配置或设置窗命令号。
    ITEM_CPUPWR,        // CPU 封装功率
    ITEM_GPU0PWR,       // GPU0 功率
    ITEM_GPU1PWR,
    ITEM_GPU2PWR,
    ITEM_GPU3PWR,
    ITEM_COUNT
};

struct LayoutBlock {
    int count;                                // 有效项目数 0..NETBALL_MAX_ITEMS
    int rows;                                 // 行数 0..maxRows（count==0 时为 0）
    int item[NETBALL_MAX_ITEMS];              // 项目 ID，按显示顺序
    int rowOf[NETBALL_MAX_ITEMS];             // 每项所属行（0 基，连续）
};

// 注册表
const wchar_t* itemToken(int id);             // L"down" … L"gpu3pwr"
const wchar_t* itemLabel(int id);             // L"下行速度" … L"GPU3 功率"
int            itemFromToken(const wchar_t* token);
bool           itemGpuIndex(int id, int* outIndex, bool* outIsTemp);   // 只管 负载/温度
bool           itemGpuPowerIndex(int id, int* outIndex);               // v1.4.2：只管 功率
int            itemCount();

// 表面能力
int  maxRows(Surface s);                      // 任务栏=2，悬浮球=NETBALL_MAX_ITEMS

// spec <-> LayoutBlock
bool parse(const wchar_t* spec, int maxRowCount, LayoutBlock* out);
void format(const LayoutBlock& b, wchar_t* out, int cch);
void normalize(LayoutBlock* b, int maxRowCount);
bool contains(const LayoutBlock& b, int id);

// 单个项目的显示文本（指标无效时输出 "xxx --"，不伪造数值）
void itemText(int id, const AppConfig* c, const Metrics* m, wchar_t* out, int cch);

// 默认值 / 取值范围校验（同时负责 spec 归一化）
void defaults(Surface s, AppConfig* c);
void clampConfig(AppConfig* c, Surface s);

} // namespace layout_model
