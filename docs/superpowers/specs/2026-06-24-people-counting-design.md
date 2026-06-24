# 客流统计(People Counting)二次开发设计

- **日期**:2026-06-24
- **目标仓库**:`camthink-ai/ne301`(STM32N657 AI 视觉相机固件)
- **状态**:Draft(待 spec review)
- **作者**:二次开发设计

## 1. 背景与目标

### 1.1 场景

NE301 设备**顶装俯视**(吸顶/顶部安装),对一只人员检测模型做推理,统计**虚拟计数线**两侧的进/出客流,按固定时间窗口(默认 5 分钟)汇总,通过 MQTT / Webhook 定期上报。典型应用:门店、展馆、闸机口客流计数。

### 1.2 核心需求

1. 使用现成的 YOLO11n person 检测模型,对画面中的人做检测
2. 跨帧关联同一目标,避免单次穿越被重复计数
3. 顶装俯视场景下,用一条带方向的虚拟计数线判定"进/出"
4. 防抖动:检测框漂移和人在计数线附近来回走动都不应造成重复计数
5. 真实客流语义:来回走的人如实计进+出各 1(而非净人数或去重)
6. 固定窗口(默认 5 分钟)+ 累计计数器,窗口末上报
7. 断网补发(MQTT/Webhook 都断时落盘 LittleFS,重连补发)
8. 以**轨迹数据**为核心上报内容,服务端可重算任何空间统计(热力图、流向图、OD 矩阵等),V2 设备零改动
9. 热力网格作为端侧可选预聚合,默认关闭
10. Web 前端可视化配置(画线、标外侧、参数滑条),实时画面叠加计数

### 1.3 非目标(本期不做)

- 热力图渲染(V2,服务端从轨迹重算)
- 历史趋势图、历史快照查询(V2)
- 实时热力图 WebSocket 推送(V2)
- 多计数线、多 ROI 支持(V2)
- 方向热力(`heat_in`/`heat_out` 分离,V2)
- 跨日强制清零、时区处理(本期窗口与自然日不对齐)

## 2. 总体架构

客流计数是 AI 推理结果的"消费者",通过订阅式钩子接入,独立于推理管线。

```
┌─────────────────────────────────────────────────────────┐
│ Camera → Video Pipeline → AI Service (推理)              │
│                              │                           │
│                              ▼ nn_result_t (OD 检测框)   │
│                    ┌──────────────────────┐              │
│                    │ People Counting Task │ ← 钩子回调  │
│                    │  (Custom/Tasks/)     │              │
│                    │  ┌──────────────┐    │              │
│                    │  │ Tracker      │ 质心追踪 + 防抖 │
│                    │  └──────┬───────┘                │
│                    │  ┌──────▼───────┐                │
│                    │  │ Line Cross   │ 进/出判定      │
│                    │  │ Detector     │                │
│                    │  └──────┬───────┘                │
│                    │  ┌──────▼───────┐                │
│                    │  │ Track Archive│ 轨迹归档(上报)│
│                    │  └──────┬───────┘                │
│                    │  ┌──────▼───────┐                │
│                    │  │ Heat Grid    │ 16x16 预埋     │
│                    │  │ (默认关)     │                │
│                    │  └──────┬───────┘                │
│                    │  ┌──────▼───────┐                │
│                    │  │ Window Stats │ 窗口/累计计数  │
│                    │  └──────┬───────┘                │
│                    └─────────┼──────────┘              │
│              ┌───────────────┼───────────────┐         │
│              ▼               ▼               ▼         │
│         MQTT Service    Webhook Service   LittleFS     │
│         (实时上报)       (实时上报)       (断网补发队列)│
│                              │                         │
│                              ▼                         │
│                        Web API                         │
│        GET/POST /api/people-counting/{config,stats,reset}│
└─────────────────────────────────────────────────────────┘
```

### 2.1 关键设计原则

- **弱钩子订阅**:AI service 暴露 `ai_result_subscribers`,客流 task 注册订阅。未来加别的场景(区域入侵、遗留物)走同一机制,不污染 AI service
- **纯算法与协调层分离**:`pc_tracker.c` / `pc_line_cross.c` 是无副作用纯函数,可在 PC 上单元测试;`people_counting.c` 是薄协调层,调用基础设施
- **配置原子热加载**:配置读写加互斥锁,双缓冲指针原子替换,帧处理永远读完整快照
- **轨迹数据为核心**:服务端从原始轨迹重算任何空间统计,V2 设备零改动

### 2.2 接入点(对现有代码改动最小化)

| 文件 | 改动 |
|------|------|
| `Custom/Services/AI/ai_service.c` | 推理完成后调用订阅者钩子(+5 行) |
| `Custom/Core/core_init.c` 或 `service_init.c` | 调用 `people_counting_init()`(+2 行) |
| `Custom/Core/System/json_config_mgr.{c,h}` | 新增 `people_counting_config_t` + get/set + JSON 序列化(+80 行) |
| `Custom/Common/Inc/aicam_types.h` | 加 `SCENARIO_PEOPLE_COUNTING` 场景枚举值(+1 行) |
| `Custom/Services/Web/web_api.c` | 注册 `/api/people-counting/*` 端点(+5 行) |
| `Custom/Hal/ai_draw.c` 或 `ai_draw_service.c` | 新增 `ai_draw_count_line()` / `ai_draw_count_text()`(+60 行) |
| `Appli/Makefile` 或 `Custom/Makefile` | 新增 .c 加入编译列表 |

## 3. 核心算法

### 3.1 质心追踪(Tracker)

每帧流程:

```
1. 收集本帧检测点
   for each 检测框 d in nn_result_t:
     if d.class == "person" and d.conf >= conf_threshold:
       p_center = (d.x, d.y)              // 归一化中心点

2. 贪心匹配现有轨迹
   for each 轨迹 trk in active_tracks:
     在未匹配检测点中找最近的 (距离 < MAX_DIST=0.15)
     if 匹配成功:
       trk.history.append(p_center)       // 环形缓冲,保留最近 K=8 帧
       trk.age++
       trk.miss_count = 0
     else:
       trk.miss_count++
       if trk.miss_count > MAX_MISS=5: 标记待删除

3. 未匹配检测点 → 新建轨迹
   for each 未匹配点 p:
     new_track(id=自增, history=[p], age=1, miss_count=0,
               last_side=0, counted_dir=0)

4. 清理待删除轨迹 → 归档为上报记录(见 §3.4)
```

**轨迹数据结构**:

```c
typedef struct {
    uint32_t id;                 // 全局自增 ID
    point_t  history[K];         // 最近 K=8 帧归一化坐标(环形缓冲)
    uint32_t history_ts[K];      // 每个历史点的时间戳(ms)
    uint8_t  history_head;       // 环形缓冲写头
    uint8_t  age;                // 已存活帧数
    uint8_t  miss_count;         // 连续未匹配帧数
    int8_t   last_side;          // 上次所在侧:-1 / +1 / 0=未知
    uint8_t  counted_dir;        // 位图:bit0=进已计,bit1=出已计
    uint32_t entered_at_ms;      // 首次出现时间
} track_t;
```

### 3.2 进出判定(Line Cross Detector)

计数线由两点 `L1, L2` 定义,外侧由用户在 Web 上点击的参考点 `outside` 决定。后端据此算出**指向内侧的法向量** `n`。

```
线方向向量:  dir = normalize(L2 - L1)
法向量候选:  n_candidate = (-dir.y, dir.x)
定向:        若 dot(outside - L1, n_candidate) > 0:
               n = -n_candidate          // outside 在 n_candidate 正向 → 内侧在反向
             else:
               n = n_candidate            // outside 在反向 → 内侧在正向

每帧,对每个稳定轨迹(age >= K_CONFIRM=5):
  p_now  = history 最新点
  p_prev = history[head - K_CONFIRM + 1]   // K_CONFIRM 帧前的点

  side_now  = sign(dot(p_now  - L1, n))
  side_prev = sign(dot(p_prev - L1, n))

  if side_now != 0 and side_prev != 0 and side_now != side_prev:
    disp = p_now - p_prev
    if dot(disp, n) > 0 and not (counted_dir & BIT_IN):
      counter.window_in++;  counter.total_in++
      counted_dir |= BIT_IN; counted_dir &= ~BIT_OUT
      record_event(track, "line_cross_in")
    elif dot(disp, n) < 0 and not (counted_dir & BIT_OUT):
      counter.window_out++; counter.total_out++
      counted_dir |= BIT_OUT; counted_dir &= ~BIT_IN
      record_event(track, "line_cross_out")
```

### 3.3 防抖三重保障

回应"来回走动不应反复触发"的需求,用三层组合:

1. **`K_CONFIRM=5` 帧确认**——刚出现的噪点框不参与进出判定
2. **历史首末点判方向**——用 `K_CONFIRM` 帧前的点 vs 当前点,跨多帧的整体位移,单帧抖动不影响
3. **`counted_dir` 位图 + 反向穿越才解锁**——同方向不会重复计;真实来回走的人会计进+出各 1(符合 §1.2 真实客流语义)

### 3.4 轨迹归档(用于上报)

轨迹消失时(连续 `MAX_MISS` 帧未匹配)或窗口末仍在画面,触发归档:

```
archive_track(track_t *trk):
  // 按 3Hz 下采样历史点(每 333ms 取一个)
  samples = resample(trk.history, trk.history_ts, target_hz=3)

  record = {
    track_id:      trk.id,
    entered_at_ms: trk.entered_at_ms,
    left_at_ms:    now,
    event_flags:   trk.counted_dir,    // 进/出事件
    points:        samples
  }
  push to window.pending_records[]
```

下采样策略:
- 轨迹存活 < 1 秒:全量上报(本来就少)
- 1 ~ 5 秒:3Hz 重采样
- \> 5 秒:1Hz 重采样(避免长停留轨迹点过多)

### 3.5 热力网格(端侧预聚合,默认关)

仅在 `heat_grid_enable=true` 时累积与上报。

```c
#define HEAT_GRID_W 16
#define HEAT_GRID_H 16
uint32_t heat[HEAT_GRID_W * HEAT_GRID_H];   // 1 KB

每帧,对每个稳定轨迹(age >= K_CONFIRM):
  p = 当前帧归一化中心点
  gx = (uint8_t)(p.x * HEAT_GRID_W);   if gx >= W: gx = W-1
  gy = (uint8_t)(p.y * HEAT_GRID_H);   if gy >= H: gy = H-1
  heat[gy * HEAT_GRID_W + gx]++
// 窗口末清零,跟 in/out 同步
```

**统计语义**:每帧累加 = 停留帧数(15fps 下 1 帧 = 67ms),符合热力图"停留越久越红"的直觉。

**只统计稳定轨迹**:刚出现的噪点框(age < K_CONFIRM)不进热力,避免误检污染。

### 3.6 算法参数默认值(全部 Web 可配)

| 参数 | 默认 | 含义 |
|------|------|------|
| `conf_threshold` | 0.25 | 检测置信度阈值 |
| `max_dist_permille` | 150 (=0.15) | 归一化匹配距离上限 |
| `track_history_k` | 8 | 轨迹环形缓冲长度 |
| `max_miss` | 5 | 连续丢失多少帧删轨迹 |
| `k_confirm` | 5 | 多少帧才参与进出判定 / 热力累积 |
| `heat_grid_w/h` | 16 | 热力网格分辨率(本期固定) |

### 3.7 性能预算

每帧 ~10 个目标、~20 条轨迹:
- 匹配 O(10×20) = 200 次距离计算
- 进出判定 O(20) 次点积
- 热力累积 O(10) 次自增
- **预估 < 0.5ms @ Cortex-M55**

## 4. 数据结构与持久化

### 4.1 配置结构(NVS 持久化)

```c
// Custom/Core/System/json_config_mgr.h 新增
typedef struct {
    aicam_bool_t enable;                // 总开关,默认 false

    // 几何配置(全部归一化 0-1)
    float line_x1, line_y1;             // 计数线端点 1
    float line_x2, line_y2;             // 计数线端点 2
    float outside_x, outside_y;         // 用户点击的"外侧"参考点

    // 模型与阈值
    float conf_threshold;               // 默认 0.25

    // 算法参数(整数化避免 JSON 浮点精度问题)
    uint16_t max_dist_permille;         // MAX_DIST × 1000,默认 150
    uint8_t  track_history_k;           // 默认 8
    uint8_t  max_miss;                  // 默认 5
    uint8_t  k_confirm;                 // 默认 5

    // 时间窗口
    uint16_t window_minutes;            // 默认 5

    // 上报通道开关
    aicam_bool_t mqtt_report_enable;    // 默认 true
    aicam_bool_t webhook_report_enable; // 默认 false
    aicam_bool_t tracks_report_enable;  // 默认 true(轨迹数据,核心)
    aicam_bool_t heat_grid_enable;      // 默认 false(端侧预聚合,V2 才打开)

    // 断网补发队列容量
    uint16_t backlog_capacity;          // 默认 24
} people_counting_config_t;
```

> **注**:用 `permille`(千分位整数)存浮点参数,避免 JSON 浮点精度 / 跨平台问题——嵌入式配置常见做法。配置 JSON 里写 `"max_dist_permille": 150`。

### 4.2 运行时状态(内存)

```c
typedef struct {
    // 当前窗口(窗口末清零)
    uint32_t window_in;
    uint32_t window_out;
    uint32_t window_start_ts;           // 窗口起始时间戳(ms)
    uint32_t heat[16 * 16];             // 1KB,热力网格

    // 累计计数器(持久化到 NVS,每窗口末同步)
    uint32_t total_in;
    uint32_t total_out;

    // 设备元信息
    uint32_t boot_id;                   // 本次开机标识
    uint32_t last_report_ts;            // 上次成功上报时间
    uint32_t dropped_windows;           // 补发队列满导致丢弃的窗口数

    // 待上报轨迹(本窗口内已归档)
    track_record_t *pending_records;
    uint16_t pending_count;
} people_counting_stats_t;
```

### 4.3 上报 JSON 结构

窗口末生成,补发队列里也存同样结构:

```json
{
  "type": "people_counting",
  "device_id": "ne301-xxxxxx",
  "boot_id": 1719216000,
  "window": {
    "start": 1719216000,
    "end":   1719216300,
    "duration_min": 5,
    "in":    12,
    "out":   9
  },
  "total": {
    "in":    1284,
    "out":   1267
  },
  "tracks": [
    {
      "id": 1234,
      "entered_at": 1719216012.3,
      "left_at":    1719216015.8,
      "events":     ["line_cross_in"],
      "points":     [[0.12,0.45,1719216012.3],
                     [0.18,0.47,1719216012.6],
                     [0.25,0.50,1719216012.9]]
    }
  ],
  "heat_grid": null,
  "dropped_windows": 0,
  "line": {
    "x1": 0.2, "y1": 0.5, "x2": 0.8, "y2": 0.5
  },
  "model": "yolo11n_256_quant_pc_uf_od_coco-person-st",
  "conf_threshold": 0.25
}
```

字段说明:
- `window.in/out`:本窗口进出计数,窗口末清零
- `total.in/out`:从开机累计,NVS 持久化
- `tracks[]`:本窗口产生的所有轨迹(3Hz 下采样)。`tracks_report_enable=false` 时为空数组
- `heat_grid`:`heat_grid_enable=true` 时为 `{width:16, height:16, data:[256 个 uint32]}`,否则 `null`
- `dropped_windows`:补发队列满丢弃的窗口计数(服务端可见数据缺口)
- `line` / `model` / `conf_threshold`:配置回显,服务端可校验

### 4.4 持久化策略

| 数据 | 存储位置 | 时机 |
|------|------|------|
| `people_counting_config_t` | NVS (`json_config_mgr`) | Web 保存时写;开机时读 |
| `total_in / total_out` | NVS(独立 key) | 每窗口末写一次(5min,Flash 磨损可忽略) |
| 补发队列 | LittleFS 文件 `/pc_backlog/{seq}.json` | MQTT+Webhook 都断时落盘;成功上报后删除 |
| `boot_id` | 内存(开机时取 RTC,失败用单调计数器) | 不持久化 |
| 轨迹、窗口计数、热力网格 | 内存 | 不持久化,重启清零 |

### 4.5 上报通道

- **MQTT**:topic `device/{device_id}/people-count`(独立主题,跟现有 `data`/`status` 分开),走 `mqtt_service_publish_data_json()`
- **Webhook**:HTTP POST 同一 JSON 到用户配置 URL,复用 `webhook_service`
- **双发各自可开关**:`mqtt_report_enable` / `webhook_report_enable` 独立
- **断网补发独立队列**:MQTT 和 Webhook 各自一个 LittleFS 队列,互不阻塞

## 5. 模块拆分与接口

### 5.1 新增文件

```
Custom/
├── Tasks/
│   ├── Inc/
│   │   └── people_counting.h              # 对外接口
│   └── Src/
│       ├── people_counting.c              # 主逻辑(初始化、帧回调、窗口定时器)
│       ├── pc_tracker.c                   # 质心追踪(纯算法,无副作用)
│       ├── pc_line_cross.c                # 进出判定(纯算法)
│       ├── pc_backlog.c                   # LittleFS 补发队列封装
│       └── test/                          # 单元测试(PC 上 gcc + unity)
│           ├── test_pc_tracker.c
│           └── test_pc_line_cross.c
├── Services/Web/api/
│   └── api_people_counting_module.c       # REST 端点
└── Web/src/pages/people-counting/         # 前端页面
    ├── index.tsx
    ├── LineCanvas.tsx
    ├── ConfigPanel.tsx
    ├── StatsPanel.tsx
    └── api.ts
```

### 5.2 对外接口

```c
// Custom/Tasks/Inc/people_counting.h

// 开机调用一次(在 service_init.c)
aicam_result_t people_counting_init(void);

// AI service 回调钩子:每帧推理结果进来
// 实现内部判断 enable,关闭时直接 return
void people_counting_on_ai_result(const nn_result_t *result, uint32_t timestamp_ms);

// Web API 查询当前统计(GET /api/people-counting/stats 用)
const people_counting_stats_t* people_counting_get_stats(void);

// Web API 重置累计计数器(POST /api/people-counting/reset 用)
void people_counting_reset_totals(void);
```

### 5.3 纯算法模块接口(可单测)

```c
// pc_tracker.h
typedef struct { /* 内部状态 */ } tracker_t;
tracker_t* tracker_create(const pc_config_t *cfg);
void       tracker_destroy(tracker_t *t);

// 输入本帧检测点,输出更新后的轨迹列表 + 归档的已完成轨迹
void tracker_update(tracker_t *t,
                    const point_t *detects, uint8_t n_detects,
                    uint32_t now_ms,
                    track_view_t *out_active,        // 当前活跃轨迹(只读视图)
                    track_record_t **out_archived,   // 归档的已完成轨迹(调用方释放)
                    uint16_t *out_n_archived);

// pc_line_cross.h
typedef struct { /* 线段 + 法向量,预计算 */ } line_cross_t;
line_cross_t* line_cross_create(float x1, float y1, float x2, float y2,
                                 float outside_x, float outside_y);

// 输入轨迹,判定是否发生穿越,更新轨迹 counted_dir,返回事件
cross_event_t line_cross_check(line_cross_t *lc, track_t *trk);
// cross_event_t: CROSS_NONE / CROSS_IN / CROSS_OUT
```

### 5.4 模块依赖

```
api_people_counting_module.c ──┐
                                ├─► people_counting.c ──► pc_tracker.c    (纯算法,可单测)
                                │           │           pc_line_cross.c   (纯算法,可单测)
                                │           │
ai_service.c ──── on_result ────┘           ├─► json_config_mgr (读配置)
                                            ├─► mqtt_service      (发布)
                                            ├─► webhook_service   (推送)
                                            ├─► pc_backlog.c ──► LittleFS
                                            └─► ai_draw_service   (画框/线/计数)
```

## 6. Web 前端

### 6.1 页面布局

```
┌─────────────────────────────────────────────────────────┐
│ 实时预览画面 (从现有 WebSocket 视频流拉)                 │
│  ┌─────────────────────────────────────┐                │
│  │        [视频画面]                    │                │
│  │   ●─────────────►                   │ ← 计数线(红)  │
│  │   L1                  L2            │   + 方向箭头    │
│  │            ○ outside                │ ← 外侧标记(蓝)│
│  │   IN: 12    OUT: 9                 │ ← 实时计数叠加  │
│  └─────────────────────────────────────┘                │
│ [画线模式] [清除] [保存]                                │
├─────────────────────────────────────────────────────────┤
│ 配置区                                                  │
│  ☑ 启用客流统计                                         │
│  统计窗口: [5 分钟 ▼]                                  │
│  置信度阈值: ────●──── 0.25                            │
│  ☑ MQTT 上报   ☐ Webhook 上报   ☑ 轨迹数据            │
│  断网补发队列: [24 条]                                  │
│  ☐ 端侧热力网格(默认关)                              │
├─────────────────────────────────────────────────────────┤
│ 统计区                                                  │
│  当前窗口: IN 12 | OUT 9 | 净流入 +3                    │
│  累计:     IN 1284 | OUT 1267                           │
│  最近上报: 2026-06-24 14:30:00 ✓                       │
│  [重置累计计数器]                                       │
└─────────────────────────────────────────────────────────┘
```

### 6.2 画线交互

1. 进入"画线模式"→ 鼠标变十字光标
2. **第一次点击**:定 `L1`(线起点),出现红点
3. **第二次点击**:定 `L2`(线终点),出现红色线段
4. **第三次点击**:在某一侧点击标记为"外侧",该侧出现蓝色半透明区域 + 线上出现指向另一侧的箭头(进方向)
5. 点"清除"重画;点"保存"POST 到设备
6. 坐标全部存**归一化值**(0-1),前端按预览分辨率换算像素

### 6.3 实时叠加(运行态,设备端 ai_draw 画到视频帧)

- 计数线(红)+ 进方向箭头
- 画面左上角文字:`IN: 12  OUT: 9`(当前窗口)
- (可选,默认关)外侧半透明色块

实时计数文字不通过 MQTT 推送——直接画在视频帧上跟着视频流走,零额外延迟。

### 6.4 REST API 端点

| 方法 | 路径 | 功能 |
|------|------|------|
| `GET` | `/api/people-counting/config` | 返回当前配置 JSON |
| `POST` | `/api/people-counting/config` | 保存配置(热加载) |
| `GET` | `/api/people-counting/stats` | 返回当前窗口 + 累计统计 |
| `POST` | `/api/people-counting/reset` | 重置累计计数器(写 NVS) |
| `GET` | `/api/people-counting/backlog` | 查看补发队列状态(条数、最早/最晚时间) |

### 6.5 前端集成

- 加路由到 `Web/src/router`(跟现有页面同级)
- 加菜单项"客流统计"到导航
- 复用现有视频预览组件(WebSocket MJPEG/JPEG 推流)
- 复用现有表单组件库(shadcn/ui,`components.json` 已配)

## 7. 错误处理与边界情况

### 7.1 关键边界

| 边界情况 | 处理策略 |
|------|------|
| **MQTT/Webhook 都断连** | 窗口末 JSON 落 LittleFS 补发队列(两条独立)。重连后按时间序补发,超过 `backlog_capacity=24` 条丢最旧的 |
| **补发队列写满** | 丢弃最旧条目,`dropped_windows++`,下次上报带上(服务端可见缺口) |
| **LittleFS 写失败**(磁盘满) | 日志告警 + LED 闪烁;窗口数据丢失但累计计数器仍递增(NVS 独立) |
| **配置线段退化**(L1≈L2,长度<0.01) | Web 端校验拒绝;设备收到则不启用计数,日志告警 |
| **轨迹跨窗口**(窗口末仍在画面) | 窗口末强制快照活跃轨迹上报;下个窗口该轨迹 ID 不重复上报,只续接 points |
| **目标 ID 溢出**(uint32 自增) | 设备寿命内不可能(15fps×10 年 ≈ 5e9),到顶回绕;服务端按 `boot_id + track_id` 联合去重 |
| **RTC 未同步** | `boot_id` 用单调计数器 fallback;`entered_at/left_at` 用相对窗口起始的毫秒偏移 |
| **模型推理掉帧** | 追踪器按"收到才算一帧"处理,不假设固定 fps;时间戳用实际 `timestamp_ms` |
| **配置热加载竞态** | 配置读写加 `osMutexId_t`;配置指针双缓冲原子替换,帧处理永远读完整快照 |
| **NVS 写累计计数器失败** | 重试 3 次;失败则内存值继续递增,下次窗口末再试;绝不让计数停摆 |

### 7.2 内存预算

| 项 | 预算 |
|------|------|
| 追踪器(50 轨迹 × ~80B) | 4KB |
| 热力网格 16×16 × 4B | 1KB |
| 窗口轨迹归档(50 轨迹 × 9 点 × 12B) | 5.4KB |
| 补发队列(落盘 LittleFS,内存只保留路径) | < 1KB |
| **总 RAM** | **< 12KB** |

### 7.3 降级模式

- **配置禁用**:`enable=false` → 帧回调直接 return,几乎零开销
- **模型加载失败**:启动检测,失败则客流 task 不启动,日志告警,Web 显示"客流统计不可用:模型缺失"
- **AI service 未就绪**:帧回调检查 AI 状态,未就绪直接 return

### 7.4 日志事件

复用现有 `Core/Log/` 机制,新增:
- `PC_TRACKER_INIT` / `PC_CONFIG_LOADED` / `PC_CONFIG_UPDATED`(INFO)
- `PC_WINDOW_REPORTED`(成功,INFO)/ `PC_WINDOW_BACKLOGGED`(落盘,WARN)
- `PC_BACKLOG_DROPPED`(队列满丢弃,WARN)
- `PC_NVS_WRITE_FAILED`(ERROR)
- `PC_LINE_INVALID`(WARN)

## 8. 测试策略

| 层 | 测试方式 |
|------|------|
| `pc_tracker.c` / `pc_line_cross.c`(纯算法) | PC 上 gcc + unity,喂脚本化检测序列。覆盖:直线穿越、抖动、来回走、多目标交叉、轨迹消失/重现 |
| `pc_backlog.c`(LittleFS 封装) | mock 文件层,测 push/pop/满队列丢弃 |
| `people_counting.c`(协调层) | 集成测试在硬件上,人工走几个来回验证 in/out 计数 |
| Web 前端 | 浏览器手动测试画线 + 保存 + 实时叠加 |

### 纯算法单测覆盖用例

1. **直线穿越**:1 个目标沿直线穿过计数线 → 断言 in=1 out=0
2. **抖动不重计**:目标在线两侧小幅(小于滞回阈值)来回 10 次 → 断言 in=0 out=0
3. **真实来回走**:目标穿越后深入内侧,再反向穿越回到外侧 → 断言 in=1 out=1
4. **多目标交叉**:2 个目标同时相反方向穿过 → 断言 in=1 out=1(各计一次)
5. **轨迹消失/重现**:目标消失 3 帧(小于 MAX_MISS)后重现 → 同一 ID 继续;消失 10 帧后重现 → 新 ID
6. **噪点不计数**:目标只出现 2 帧(< K_CONFIRM)就消失 → 不计 in/out,不进热力

## 9. 验收标准

1. 设备顶装,Web 上画线 + 标外侧,保存后**人在画面里走过线 10 次**,设备统计 `in=10 out=10`(允许 ±1 误差)
2. 在线附近来回抖动手(模拟检测框漂移),计数**不重复增加**
3. MQTT broker 收到每 5 分钟一条 JSON,字段完整(`window` / `total` / `tracks`)
4. 拔网线 10 分钟,重新插上后**收到 2 条补发数据**
5. Web 上勾选"轨迹数据"开关后,JSON 的 `tracks[]` 字段出现完整路径
6. 重启设备,`total.in/out` 恢复,`window.in/out` 清零

## 10. V2 扩展位(本期不实现,数据结构已兼容)

服务端从 `tracks[]` 重算,**设备零改动**:
- 热力图(把 points 投到任意分辨率网格)
- 流向图(按 points 方向画箭头)
- OD 矩阵(起点 → 终点统计)
- 停留热点(聚类 points 找密集区)
- 异常路径(孤立点检测)
- 路径分桶(直走 / 徘徊 / 折返分类)

设备端可选增量:
- 端侧热力网格渲染(打开 `heat_grid_enable`,Web 加渲染模块)
- 方向热力(`heat_in[256]` / `heat_out[256]`)
- 历史快照(每窗口 dump 到 LittleFS,Web 查询历史)
- 实时热力图(WebSocket 流式推送降采样网格)
- 多计数线 / 多 ROI 支持
