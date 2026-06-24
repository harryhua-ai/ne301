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
| `Custom/Services/AI/ai_service.{c,h}` | **新增订阅者注册表**(详见 §2.3):`ai_service_register_subscriber(fn)` + 在 draw 回调路径中分发结果给订阅者(+30 行) |
| `Custom/Core/core_init.c` 或 `service_init.c` | 调用 `people_counting_init()`(+2 行) |
| `Custom/Core/System/json_config_mgr.{c,h}` | 把 `people_counting_config_t` 作为 `aicam_global_config_t` 的嵌套成员新增,随主配置一起序列化(+80 行) |
| `Custom/Common/Inc/aicam_types.h` | **新增** `aicam_scenario_t` 枚举类型(项目里目前不存在场景枚举),包含 `AICAM_SCENARIO_NONE=0` / `AICAM_SCENARIO_PEOPLE_COUNTING=1`,后续场景在此扩展。挂在 `work_mode_config_t` 或全局状态中(具体落点在实现计划中定)。约 +10 行 |
| `Custom/Services/Web/web_api.c` | 注册 `/api/people-counting/*` 端点(+5 行) |
| `Custom/Core/Video/ai_draw_service.{c,h}` | 新增 `ai_draw_count_line()` / `ai_draw_count_text()`(+60 行)。注:画图代码在 `Core/Video/ai_draw_service.*`,不是 `Hal/ai_draw.*` |
| `Custom/Services/Webhook/webhook_service.{c,h}` | **新增 `webhook_service_push_json(url, json, len)`**——现有 `push_capture()` 是图像抓拍专用,不支持任意 JSON(+40 行) |
| `Appli/Makefile` 或 `Custom/Makefile` | 新增 .c 加入编译列表 |

### 2.3 AI 结果订阅者机制(新增基础设施)

现有 `ai_service.c` 没有 observer 基础设施,结果仅在 `ai_service_draw_callback()` 内部消费。需要新增一个轻量订阅者注册表:

```c
// ai_service.h 新增
typedef void (*ai_result_subscriber_t)(const nn_result_t *result, uint32_t timestamp_ms);
aicam_result_t ai_service_register_subscriber(ai_result_subscriber_t fn);

// ai_service.c 新增
#define AI_MAX_SUBSCRIBERS 4
static ai_result_subscriber_t g_subscribers[AI_MAX_SUBSCRIBERS];

aicam_result_t ai_service_register_subscriber(ai_result_subscriber_t fn) {
    for (int i = 0; i < AI_MAX_SUBSCRIBERS; ++i) {
        if (g_subscribers[i] == NULL) { g_subscribers[i] = fn; return AICAM_OK; }
    }
    return AICAM_ERROR_FULL;   // 现有错误码,见 aicam_types.h
}

// 注入点:在 ai_service_draw_callback() 内部,调用 ai_service_get_nn_result()
// 拿到栈局部的 nn_result_t 之后,在画出框/推送流之前调用。
// timestamp_ms 来源:osKernelGetTickCount()(CMSIS-RTOS2 API,毫秒单调时钟)。
// 不依赖 frame_id 推导时间,避免与 AI 推理节拍耦合。
static void notify_subscribers(const nn_result_t *r) {
    uint32_t ts = osKernelGetTickCount();
    for (int i = 0; i < AI_MAX_SUBSCRIBERS; ++i) {
        if (g_subscribers[i]) g_subscribers[i](r, ts);
    }
}
```

**线程模型**:`ai_service_draw_callback()` 运行在**摄像头管线上下文**(非 AI 推理线程)。订阅者回调在此上下文同步执行。客流 task 的回调必须:
- 非阻塞(算法预算 < 0.5ms)
- 不持有跨线程锁超过微秒级
- 配置访问用双缓冲原子指针(读快照无锁),仅窗口末上报时短持有互斥锁

客流 task **不开新线程**——算法在 AI 回调上下文同步跑,窗口定时器用 `osTimerPeriodic`(默认 5min)在定时器服务任务上下文回调。两个上下文之间通过 `pending_records` 队列传递,队列操作用 `osMutex` 短锁保护。

## 3. 核心算法

### 3.1 质心追踪(Tracker)

每帧流程:

```
1. 收集本帧检测点(注意:bbox 的 x,y 是左上角,必须换算中心)
   for each 检测框 d in nn_result_t (type == PP_TYPE_OD):
     if d.class_name == "person" and d.conf >= conf_threshold_permille/1000.0f:
       // od_detect_t 的 x/y/width/height 已经是归一化 [0,1] 浮点(见 pp.h,后处理库输出)
       // 不是像素坐标,无需额外归一化
       center_x = d.x + d.width / 2.0f      // d.x/d.y 是左上角
       center_y = d.y + d.height / 2.0f
       detects[n++] = { center_x, center_y }

2. 贪心匹配现有轨迹(确定性顺序,避免 ID 抖动)
   // 三级排序键保证完全确定性,任何实现下顺序都一致
   sort active_tracks by (last_match_ts DESC, miss_count ASC, track_id ASC)

   for each 轨迹 trk in active_tracks (上述顺序):
     best_d = INF; best_idx = -1
     for each 未匹配检测点 detects[j]:
       dist = euclidean(trk.last_pos, detects[j])
       if dist < best_d: best_d = dist; best_idx = j
     if best_idx >= 0 and best_d < (max_dist_permille / 1000.0f):
       trk.history[trk.history_head] = detects[best_idx]
       trk.history_ts[trk.history_head] = now_ms
       trk.history_head = (trk.history_head + 1) % K   // 环形缓冲
       trk.last_pos = detects[best_idx]
       trk.last_match_ts = now_ms
       trk.age++
       trk.miss_count = 0
       mark detects[best_idx] as matched
     else:
       trk.miss_count++
       if trk.miss_count > max_miss: 标记待删除

3. 未匹配检测点 → 新建轨迹
   for each 未匹配点 p:
     new_track(id=自增, history={p}, history_head=1, age=1, miss_count=0,
               last_side=0, counted_dir=0, last_match_ts=now_ms,
               last_report_ts=now_ms, entered_at_ms=now_ms)

4. 清理待删除轨迹 → 归档(见 §3.4)
```

**轨迹数据结构**:

```c
// 新增类型(项目里 aicam_point_t 是 int16_t 像素坐标,不适合归一化浮点)
typedef struct { float x, y; } pc_point_t;

#define BIT_IN  0x01
#define BIT_OUT 0x02

// K 是编译期上限(数组尺寸),track_history_k 是运行期配置(实际使用槽数)
#define K_MAX 16

typedef struct {
    uint32_t id;                 // 全局自增 ID
    pc_point_t history[K_MAX];   // 归一化坐标环形缓冲(数组按上限开,实际用 track_history_k 个槽)
    uint32_t   history_ts[K_MAX];// 每个历史点的时间戳(ms)
    uint8_t    history_head;     // 环形缓冲写头(下次写入位置)
    uint8_t    history_used;     // 实际使用的槽数(= min(age, track_history_k))
    uint8_t    age;              // 已存活帧数
    uint8_t    miss_count;       // 连续未匹配帧数
    int8_t     last_side;        // 上次所在侧:-1 / +1 / 0=未初始化
    uint8_t    counted_dir;      // 位图:BIT_IN / BIT_OUT
    uint32_t   entered_at_ms;    // 首次出现时间
    uint32_t   last_match_ts;    // 最近一次匹配时间(用于贪心排序)
    uint32_t   last_report_ts;   // 已上报到的时间点(用于跨窗口续接)
    uint32_t   segment_id;       // 下一个待生成段的 ID(从 0 递增)
    pc_point_t last_pos;         // 最近位置(快速访问,免得读环形缓冲)
} track_t;

#define PC_MAX_TRACKS 64   // 静态数组上限,内存 64×~200B ≈ 13KB(含 K_MAX=16 的环形缓冲)
```

**K 的运行期约束**:环形缓冲写入时,`history_head` 在 `[0, track_history_k)` 范围内自增取模,**不是** `[0, K_MAX)`。`track_history_k` 在配置校验时被 clamp 到 `[4, K_MAX]`。这样配置项 `track_history_k` 真正可调,而数组尺寸固定。

> 注:`pc_line_cross.c` 会修改 `trk->counted_dir` / `trk->last_side`,**不是数学意义上的纯函数**;它是**无 I/O 副作用的状态算法模块**(无文件/网络/内存分配,只读改入参轨迹)。单测时通过对比入参轨迹前后状态验证。

### 3.2 进出判定(Line Cross Detector)

计数线由两点 `L1, L2` 定义,外侧由用户在 Web 上点击的参考点 `outside` 决定。后端据此算出**指向内侧的法向量** `n`。

```
线方向向量:  dir = normalize(L2 - L1)
法向量候选:  n_candidate = (-dir.y, dir.x)
定向:        若 dot(outside - L1, n_candidate) > 0:
               n = -n_candidate          // outside 在 n_candidate 正向 → 内侧在反向
             else:
               n = n_candidate            // outside 在反向 → 内侧在正向

每帧,对每个轨迹(age >= K_CONFIRM=5):
  now_idx  = (history_head - 1 + K) % K                       // 最新写入点
  prev_idx = (history_head - K_CONFIRM + K) % K               // K_CONFIRM 帧前
  p_now  = history[now_idx]
  p_prev = history[prev_idx]

  side_now = sign(dot(p_now  - L1, n))      // -1 / 0 / +1
  side_prev = sign(dot(p_prev - L1, n))

  // 首次到达 K_CONFIRM 时,last_side 必须初始化为首个有效(非零)side,
  // 避免人一开始就站线上导致永远不计数
  if trk.last_side == 0 and side_now != 0:
    trk.last_side = side_now
    side_prev = side_now   // 本帧不视为穿越,等下一帧位移

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

### 3.3 防抖机制(澄清:无空间滞回,纯时序确认 + 状态位图)

注意:本设计**不使用空间滞回带**(那种"必须深入线后侧 D 像素才触发"的迟滞)。原因是顶装俯视下人的运动近线性,空间滞回反而引入配置复杂度。防抖完全靠三层时序/状态机制:

1. **`K_CONFIRM=5` 帧确认**——刚出现的噪点框(检测闪烁 1-2 帧)不参与进出判定
2. **历史首末点判方向**——`p_prev` 取 `K_CONFIRM` 帧前的点,`p_now` 取最新点,跨多帧的**整体位移**判定方向。单帧抖动(检测框左右漂 1-2 像素)不会改变跨 5 帧的整体位移符号
3. **`counted_dir` 位图 + 反向穿越才解锁**——同方向不会重复计;真实来回走的人(明确跨越后停留至少 K_CONFIRM 帧再反向)会计进+出各 1(符合 §1.2 真实客流语义)

**重要澄清**:这套机制对"在线附近反复横跳小于 K_CONFIRM 帧间隔"的情况**天然不计数**(因为 `p_prev` 和 `p_now` 之间跨越的多帧整体看,可能根本没真正穿过,或穿过后没在反向稳定 K_CONFIRM 帧又穿回来,位图锁定生效)。但对"真正来回走 10 次"的人会计 10 进 + 10 出,这是设计目标。

**反抖动失败模式**(单测必须覆盖):
- 目标在某一侧稳定 K_CONFIRM 帧后,在原侧小幅(归一化 < 0.05)来回 20 次 → 不计数
- 目标真正穿到对侧,稳定 K_CONFIRM 帧后穿回 → in+1 out+1(不重复)

### 3.4 轨迹归档与跨窗口续接(关键逻辑)

**核心原则**:轨迹**生命周期跨越多个窗口**时不删除、不重发,而是**分段(segment)上报**。每段共享同一个 `track_id`,服务端按 `track_id` 拼接。

**两种归档触发时机**:

1. **轨迹自然消失**(连续 `MAX_MISS` 帧未匹配):生成一个"终结段",`seg_end_type=DEPARTED`,从 `last_report_ts` 到 `now` 截取点,归档后从 `active_tracks` 删除
2. **窗口末仍在画面**:轨迹**不删除**,只生成一个"跨窗段快照",`seg_end_type=CROSSING`,从 `last_report_ts` 到窗口末时间截取点。轨迹留在 `active_tracks`,更新 `trk.last_report_ts = window_end_ts`,下个窗口继续追加

```c
typedef enum { SEG_DEPARTED, SEG_CROSSING } segment_end_t;

typedef struct {
    uint32_t       track_id;
    uint32_t       segment_id;          // 同一 track_id 下递增,从 0 开始
    uint32_t       entered_at_ms;       // 轨迹首次出现(整个生命周期不变)
    uint32_t       seg_start_ms;        // 本段起始(= 该 track 的上次 last_report_ts)
    uint32_t       seg_end_ms;          // 本段结束
    segment_end_t  seg_end_type;        // DEPARTED=轨迹消失;CROSSING=窗口跨越
    uint8_t        events;              // 本段期间触发的进/出事件位图(BIT_IN/BIT_OUT)
    uint8_t        nb_points;
    pc_point_t     points[];            // 软数组,[seg_start_ms, seg_end_ms] 区间下采样后
} track_record_t;
```

**`events` 位图 → JSON 字符串数组映射**(序列化契约):

| 位图值 | JSON `events` 数组 |
|------|------|
| `0x00` | `[]` |
| `BIT_IN` (0x01) | `["line_cross_in"]` |
| `BIT_OUT` (0x02) | `["line_cross_out"]` |
| `BIT_IN \| BIT_OUT` (0x03) | `["line_cross_in", "line_cross_out"]` |

字符串常量固定为小写下划线形式,不可改名。

**下采样基于时间戳(不依赖固定 fps,§7.1 已说明推理可能掉帧)**:

```c
resample_to_record(trk, from_ms, to_ms):
  duration_s = (to_ms - from_ms) / 1000.0f
  if duration_s < 1.0:   interval = 0       // 全量(短轨迹本来就少)
  elif duration_s < 5.0: interval = 333     // ~3Hz
  else:                  interval = 1000    // 1Hz

  next_ts = from_ms
  for i in trk.history (按 ts 升序遍历环形缓冲):
    if history_ts[i] <= from_ms: continue
    if history_ts[i] >  to_ms: break
    if interval == 0 or history_ts[i] >= next_ts:
      append (history[i], history_ts[i]) to rec.points
      next_ts = history_ts[i] + interval
```

**关键约束**:
- `track_id` 全局唯一自增,跨窗口不变
- 每个 `track_id` 的 `segment_id` 从 0 递增,服务端按 `(track_id, segment_id)` 排序拼接还原完整轨迹
- 一条轨迹整个生命周期可能产生多段(每跨越一个窗口 +1 段,再加最终一段)
- **跨窗段只含上次上报后新增的点**,不会重发已上报的点

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
| `conf_threshold_permille` | 250 (=0.25) | 检测置信度阈值(千分位整数,与现有 `ai_debug_config_t.confidence_threshold` 用 0-100 不同——这里用 0-1000 给 YOLOv11 更细粒度,边界处换算 `conf_threshold_permille/1000.0f` 与 float conf 比较) |
| `max_dist_permille` | 150 (=0.15) | 归一化匹配距离上限,边界处换算 `max_dist_permille/1000.0f` |
| `track_history_k` | 8 | 轨迹环形缓冲长度(代码里 `#define K track_history_k`) |
| `max_miss` | 5 | 连续丢失多少帧删轨迹 |
| `k_confirm` | 5 | 多少帧才参与进出判定 / 热力累积 |
| `heat_grid_w/h` | 16 | 热力网格分辨率(本期固定) |

> **统一约定**:所有浮点参数用 `*_permille`(0-1000 整数)存,代码里使用时统一除以 1000.0f。这避免 JSON 浮点精度差异和现有 `confidence_threshold` 风格不一致问题。

### 3.7 性能预算

每帧 ~10 个目标、~20 条轨迹:
- 匹配 O(10×20) = 200 次距离计算
- 进出判定 O(20) 次点积
- 热力累积 O(10) 次自增
- **预估 < 0.5ms @ Cortex-M55**

## 4. 数据结构与持久化

### 4.1 配置结构(集成进现有 `aicam_global_config_t`)

**集成方式**:作为 `aicam_global_config_t` 的**嵌套成员**,跟随主配置文件 `/config/aicam_config.json` 一起序列化/反序列化。**不另开 NVS key**,与现有所有模块(MQTT、Webhook、AI、System)的配置存储方式一致。

```c
// Custom/Core/System/json_config_mgr.h

// 新增结构
typedef struct {
    aicam_bool_t enable;                // 总开关,默认 false

    // 几何配置(归一化 0-1,以 *_permille 存储避免浮点精度问题)
    uint16_t line_x1_permille, line_y1_permille;       // 端点 1
    uint16_t line_x2_permille, line_y2_permille;       // 端点 2
    uint16_t outside_x_permille, outside_y_permille;   // "外侧"参考点

    uint16_t conf_threshold_permille;   // 默认 250 (=0.25)
    uint16_t max_dist_permille;         // 默认 150 (=0.15)

    uint8_t  track_history_k;           // 默认 8
    uint8_t  max_miss;                  // 默认 5
    uint8_t  k_confirm;                 // 默认 5

    uint16_t window_minutes;            // 默认 5

    aicam_bool_t mqtt_report_enable;    // 默认 true
    aicam_bool_t webhook_report_enable; // 默认 false
    aicam_bool_t tracks_report_enable;  // 默认 true(轨迹数据,核心)
    aicam_bool_t heat_grid_enable;      // 默认 false

    uint16_t backlog_capacity;          // 默认 24
} people_counting_config_t;

// 嵌入到主配置
typedef struct {
    // ... 现有成员 ...
    people_counting_config_t people_counting;   // 新增
} aicam_global_config_t;
```

访问方式:
```c
// 现有 json_config_get_config() 是输出参数风格:
//   aicam_result_t json_config_get_config(aicam_global_config_t *out);
// 每帧拷贝 ~8KB 主配置太贵。需要在 json_config_mgr 新增 RO 快照接口:
const aicam_global_config_t* json_config_get_config_ro(void);   // 返回内部 const 指针,内部用 rwlock 或 seqlock 保证读不阻塞写

// 帧处理读(只读,无拷贝)
const people_counting_config_t* cfg = &json_config_get_config_ro()->people_counting;

// 写(Web API 用)——使用现有 copy-based API,因为写入要完整序列化
aicam_global_config_t mutated;
json_config_get_config(&mutated);            // 拷贝出当前完整配置
mutated.people_counting.enable = AICAM_TRUE;
json_config_set_config(&mutated);            // 触发 JSON 序列化到 LittleFS + 原子切换 RO 指针
```

**新增 API**:`json_config_get_config_ro()` 需要在 `json_config_mgr.c` 实现。内部存储改为 `static aicam_global_config_t g_config_ro` + `osRwlock` 或 `uint32_t g_config_seq`(seqlock 风格,读侧重试如果序号在读完前后不一致)。这个改动是客流模块的硬依赖,需要在实现计划里单列任务。

**校验范围**:`enable ∈ {0,1}`;`*_permille ∈ [0, 1000]`;`window_minutes ∈ {1,5,15,30,60}`;`track_history_k ∈ [4, 16]`;`k_confirm ∈ [3, track_history_k]`;`backlog_capacity ∈ [6, 96]`;线段长度 `|L2-L1|_permille > 10`(防退化,见 §7.1)。Web API 写入前校验,设备收到非法值拒绝并返回 400。

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
    uint32_t dropped_windows_mqtt;      // MQTT 队列满导致丢弃的窗口数
    uint32_t dropped_windows_webhook;   // Webhook 队列满导致丢弃的窗口数

    // 待上报轨迹(本窗口内已归档)
    track_record_t *pending_records;
    uint16_t pending_count;
} people_counting_stats_t;
```

### 4.3 上报 JSON 结构

窗口末生成,补发队列里也存同样结构。**时间戳统一约定**:`boot_id`、`window.start/end`、`entered_at`、`seg_start/end` 全部用**相对开机单调时钟的毫秒偏移**(uint32),而不是 Unix epoch——因为设备 RTC 可能未同步(§7.1)。字段名保留 `_ms` 后缀明示。

```json
{
  "type": "people_counting",
  "device_id": "ne301-xxxxxx",
  "boot_id": 12345,
  "boot_id_kind": "monotonic",
  "window": {
    "start_ms": 600000,
    "end_ms":   630000,
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
      "track_id": 1234,
      "segment_id": 0,
      "entered_at_ms": 601200,
      "seg_start_ms":  601200,
      "seg_end_ms":    601580,
      "seg_end_type":  "departed",
      "events":        ["line_cross_in"],
      "points":        [[0.12,0.45,601200],
                        [0.18,0.47,601533],
                        [0.25,0.50,601580]]
    }
  ],
  "heat_grid": null,
  "dropped_windows_mqtt":   0,
  "dropped_windows_webhook": 0,
  "line": {
    "x1": 0.2, "y1": 0.5, "x2": 0.8, "y2": 0.5
  },
  "model": "yolo11n_256_quant_pc_uf_od_coco-person-st",
  "conf_threshold_permille": 250
}
```

字段说明:
- `boot_id`:本次开机标识(uint32)。`boot_id_kind` 取值 `"rtc"`(已同步 RTC,值为 Unix epoch 秒)或 `"monotonic"`(开机单调计数器,小整数)。服务端按 `boot_id + track_id + segment_id` 联合去重
- `window.in/out`:本窗口进出计数,窗口末清零
- `total.in/out`:从开机累计,持久化(详见 §4.4)
- `tracks[]`:本窗口产生的所有段(包含自然消失的 DEPARTED 段 + 窗口末仍在画面的 CROSSING 段)。`tracks_report_enable=false` 时为空数组
- `heat_grid`:`heat_grid_enable=true` 时为 `{width:16, height:16, data:[256 个 uint32]}`,否则 `null`。**与 `tracks[]` 相互独立**——可单独开热力不开轨迹,或反之
- `dropped_windows_mqtt` / `dropped_windows_webhook`:**各自通道独立的丢弃计数**,服务端可见每个通道的数据缺口

### 4.4 持久化策略

| 数据 | 存储位置 | 时机 |
|------|------|------|
| `people_counting_config_t` | `/config/aicam_config.json`(随主配置) | Web 保存时写;开机时读 |
| `total_in / total_out` | `/config/pc_totals.json`(独立小文件) | 每窗口末写一次(5min,Flash 磨损可忽略)。**为何不放进主配置**:主配置每次写都要重写整个 ~8KB JSON,而 totals 每 5 分钟变一次,放主配置会反复放大写入量 |
| 补发队列 | `/pc_backlog/{channel}/{seq}.json`,`channel ∈ {mqtt, webhook}` | MQTT/Webhook 任一通道断连时,该窗口数据进**对应通道**队列;该通道重连后按 `seq` 升序补发,补发成功删除该文件 |
| `boot_id` | 内存(开机时优先取 RTC,失败用单调计数器) | 不持久化 |
| 轨迹段、窗口计数、热力网格 | 内存 | 不持久化,重启清零 |

### 4.5 上报通道

- **MQTT**:topic `device/{device_id}/people-count`(独立主题,跟现有 `data`/`status` 分开)。**注意**:`mqtt_service_publish_data_json()` 内部固定写到 `data_report_topic`,**不能用于自定义主题**。需要调用更底层的 `mqtt_service_publish_json(const char* topic, const char* json, int qos, int retain)`(若该函数不存在则需在 mqtt_service 新增 thin wrapper)。已在 §2.2 改动表标注
- **Webhook**:HTTP POST 同一 JSON 到用户配置 URL。**注意**:现有 `webhook_service_push_capture()` 是图像抓拍专用(签名 `jpeg_data + metadata + ai_result`),**不支持任意 JSON**。需要在 webhook_service 新增 `webhook_service_push_json(const char* url, const char* json, size_t len)`(已在 §2.2 改动表标注)
- **双发各自可开关**:`mqtt_report_enable` / `webhook_report_enable` 独立
- **断网补发独立队列**:MQTT 和 Webhook **各自一个 LittleFS 队列**,互不阻塞。任一通道重连时只补发自己队列里的内容;另一通道仍断则其队列继续累积,直到 `backlog_capacity` 满后丢最旧的(各自 `dropped_windows_*` 计数)

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
└── Web/src/pages/people-counting/         # 前端页面(Web/ 是当前激活前端,Frontend/ 是构建产物)
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
// pc_point_t 已在 §3.1 定义

// pc_tracker.h
typedef struct { /* 内部状态:tracks[PC_MAX_TRACKS], next_id, cfg */ } tracker_t;
tracker_t* tracker_create(const people_counting_config_t *cfg);
void       tracker_destroy(tracker_t *t);

// 输入本帧检测点(归一化中心点数组),输出归档段(调用方负责释放)
void tracker_update(tracker_t *t,
                    const pc_point_t *detects, uint8_t n_detects,
                    uint32_t now_ms,
                    track_record_t ***out_records,   // 输出新归档段(DEPARTED)
                    uint16_t *out_n_records);

// 窗口末调用:对所有活跃轨迹生成 CROSSING 段,更新 last_report_ts
void tracker_window_snapshot(tracker_t *t, uint32_t window_end_ms,
                             track_record_t ***out_records,
                             uint16_t *out_n_records);

// pc_line_cross.h
typedef struct { pc_point_t L1, L2, n; } line_cross_t;   // n 为预算法向量(指向内侧)
line_cross_t* line_cross_create(float x1, float y1, float x2, float y2,
                                 float outside_x, float outside_y);
typedef enum { CROSS_NONE, CROSS_IN, CROSS_OUT } cross_event_t;
cross_event_t line_cross_check(line_cross_t *lc, track_t *trk);  // 更新 trk->counted_dir

// pc_backlog.h
typedef enum { BACKLOG_MQTT, BACKLOG_WEBHOOK } backlog_channel_t;
// 枚举值到目录名的固定映射(避免出现 /pc_backlog/0/ 之类不直观路径)
static inline const char* backlog_channel_name(backlog_channel_t ch) {
    return ch == BACKLOG_MQTT ? "mqtt" : "webhook";
}
aicam_result_t backlog_push(backlog_channel_t ch, const char *json, uint16_t capacity);
aicam_result_t backlog_pop (backlog_channel_t ch, char *json_buf, size_t buf_len);  // 弹最旧
uint16_t       backlog_count(backlog_channel_t ch);
// 文件路径示例:/pc_backlog/mqtt/000123.json(8 位十进制 seq,固定宽度便于字典序 = 时间序)
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

**V1 限制**(本期不实现,后续 V2 补):
- 已画好的线**不能拖拽微调**,只能"清除"重画
- 不支持多点折线,只能是单段直线
- 不支持撤销(Undo)

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
| **MQTT 断、Webhook 通(或反之)** | **只把数据放进断连通道的队列**,通的通道正常实时上报。两队列完全独立(§4.5) |
| **MQTT/Webhook 都断连** | 数据同时进两个队列。各自重连后各自补发,互不阻塞 |
| **补发队列写满** | 该通道丢最旧条目,该通道 `dropped_windows_*++`,下次上报带上 |
| **LittleFS 写失败**(磁盘满) | 日志告警 + LED 闪烁;该窗口数据丢失但累计计数器仍递增(`/config/pc_totals.json` 独立) |
| **配置线段退化**(`|L2-L1|_permille ≤ 10`) | Web API 写入校验拒绝(返回 400);若设备内部检测到则禁用计数,日志告警 |
| **轨迹跨窗口**(窗口末仍在画面) | 不删除,生成 `CROSSING` 段上报(只含上次上报后新增点);下窗口续接同 ID(§3.4) |
| **目标 ID 溢出**(uint32 自增) | 设备寿命内不可能(15fps×10 年 ≈ 5e9),到顶回绕;服务端按 `boot_id + track_id + segment_id` 联合去重 |
| **RTC 未同步** | `boot_id_kind="monotonic"` + 单调计数器;时间字段统一用开机毫秒偏移(§4.3) |
| **模型推理掉帧** | 追踪器按"收到才算一帧",不假设固定 fps;`now_ms` 来自实际回调时间戳 |
| **配置热加载竞态** | 读无锁(双缓冲原子指针);写用 `osMutex` 短锁,触发指针原子切换。帧处理永远读完整快照 |
| **NVS 写累计计数器失败** | 重试 3 次;失败则内存值继续递增,下次窗口末再试;绝不让计数停摆 |
| **AI 回调上下文 vs 窗口定时器上下文** | 两个上下文通过 `pending_records` 链表传递,`osMutex` 短锁保护(微秒级),无长临界区 |
| **JSON 序列化缓冲** | 窗口末上报 JSON 可能达 ~10KB(50 段 × 9 点 + 热力网格),需要独立 `pc_json_buf[16*1024]`。**不能复用** `json_config_mgr` 的 buffer |

### 7.2 内存预算

| 项 | 预算 |
|------|------|
| 追踪器 `tracks[PC_MAX_TRACKS=64]` × ~232B(含 `history[K_MAX=16]` 128B + `history_ts[16]` 64B + ~40B 字段) | ~15KB |
| 热力网格 16×16 × 4B | 1KB |
| 窗口段归档 `pending_records`(50 段 × 9 点 × 12B) | 5.4KB |
| JSON 序列化缓冲 `pc_json_buf` | 16KB |
| 补发队列(落盘 LittleFS,内存只保留文件名索引) | < 1KB |
| **总 RAM** | **< 40KB**(STM32N6 32MB 中可忽略) |

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

1. **直线穿越**:1 个目标沿直线穿过计数线,稳定 `K_CONFIRM` 帧后到达对侧 → 断言 in=1 out=0
2. **抖动不重计**:目标稳定在某一侧后,在该侧内(归一化位移 < 0.05)来回 20 次,**从未跨越计数线** → 断言 in=0 out=0
3. **真实来回走**:目标穿越到对侧,稳定 `K_CONFIRM` 帧后,反向穿越回原侧 → 断言 in=1 out=1
4. **快速来回(防抖关键)**:目标穿越后**未稳定 K_CONFIRM 帧**就反向穿回 → 断言 in=0 out=0(历史首末点同侧 + counted_dir 锁定)
5. **多目标交叉**:2 个目标同时相反方向穿过 → 断言 in=1 out=1(各计一次,无 ID swap)
6. **轨迹消失/重现**:目标消失 3 帧(< MAX_MISS)后重现 → 同一 ID 继续;消失 10 帧后重现 → 新 ID
7. **噪点不计数**:目标只出现 2 帧(< K_CONFIRM)就消失 → 不计 in/out,不进热力
8. **跨窗口续接**:模拟同一轨迹跨 2 个窗口(驱动 `tracker_update` + `tracker_window_snapshot`)→ 输出 2 段,`track_id` 相同,`segment_id` 分别 0/1,`seg_end_type` 分别 `CROSSING`/`DEPARTED`,第二段 `seg_start_ms` = 第一段 `seg_end_ms`

## 9. 验收标准

1. 设备顶装,Web 上画线 + 标外侧,保存后**人在画面里走过线 10 次**(每次明确穿过并在对侧停留 ≥ 1 秒),设备统计 `in=10 out=10`(允许 ±1 误差)
2. **防抖验证**:人站在计数线某一侧(归一化距离线 > 0.05)原地小幅移动 30 秒,计数**保持不变**;然后快速来回跨线(每次跨线后停留 < 0.5 秒就跨回)10 次,计数**最多增加 2**(只有真正稳定穿越的才计)
3. MQTT broker 收到每 5 分钟一条 JSON,字段完整(`window` / `total` / `tracks`),`tracks[]` 内每段含 `track_id` / `segment_id` / `points`
4. **补发验证**:拔网线 10 分钟,MQTT broker 在 10 分钟内收到 0 条;重新插上网线后**在 60 秒内收到 2 条补发数据**(`window.start_ms` 对应断网期间)
5. **轨迹开关**:Web 上关闭"轨迹数据"后,JSON 的 `tracks[]` 为空数组;`window.in/out` 仍正常
6. **热力开关**:Web 上单独打开"端侧热力网格"(轨迹保持关),JSON 的 `heat_grid` 出现 256 元素数组,`tracks[]` 仍为空
7. **重启恢复**:重启设备,`total.in/out` 从 `/config/pc_totals.json` 恢复,`window.in/out` 清零,新轨迹 `track_id` 从 0 重开(`boot_id` 变化)
8. **重置累计**:Web 点"重置累计计数器",`total.in/out` 立即归零并持久化

## 10. V2 扩展位(本期不实现,数据结构已兼容)

服务端从 `tracks[]` 重算,**设备端通常零改动**:
- 流向图(按 points 方向画箭头)
- OD 矩阵(起点 → 终点统计)
- 停留热点(聚类 points 找密集区)
- 异常路径(孤立点检测)
- 路径分桶(直走 / 徘徊 / 折返分类)

**热力图特殊说明**(精度权衡):轨迹 points 是 3Hz/1Hz 下采样后的稀疏点。对于"快速通过"的目标,采样点稀疏,网格密度不准;对于"长时间停留"的目标,采样点密集但停留时长被压缩(1Hz 时 30 秒只产生 30 个点)。
- **精确热力图**:仍需打开设备端 `heat_grid_enable`(§3.5 的每帧累加保留真实停留时长)
- **粗略热力图**:仅靠 `tracks[]` 也能算,适合"流量分布"而非"停留分布"的展示
- 因此 `heat_grid` 字段保留为独立可选,V2 不强制移除

设备端可选增量:
- 端侧热力网格渲染(打开 `heat_grid_enable`,Web 加渲染模块)
- 方向热力(`heat_in[256]` / `heat_out[256]`)
- 历史快照(每窗口 dump 到 LittleFS,Web 查询历史)
- 实时热力图(WebSocket 流式推送降采样网格)
- 多计数线 / 多 ROI 支持
