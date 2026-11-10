# #193 B3 预审修正轮（A comment 6758150393）

Worktree: `.worktree/lean-193-p1b/.worktree/lean-193-b3`（branch `lean/193-b3`，自 `bba5baf2c` fast-forward 起）
范围（A 预审裁定 PRE-REVIEW CHANGES NEEDED 的最小修正边界）：

1. 缺陷 1：worker `case 'open'` 未清上一 socket 的 `lastValidFrameAt` → 重连后旧帧时间武装 5s steady 窗口，绕开新连接 10s join 宽限。
2. 缺陷 2：`case 'close'`（含 `Connection replaced`）只 dispatch `wv_close`，未清 `wsOpenAt`/标识 socket 已退出 → `isSocketLive` 误判在线 → 旧末帧 5s 触发 hardRestart → 同 IP 替换连接被再次挤掉（自激循环）。
3. 两个负例状态转移测试（A 指定）。
4. 交付身份校正（仅补证据）：容量两口径分列、SRAM_POOL baseline/final/delta、APP 616 与 Web 1.5.0.8 双包身份配对。

隔离约束：无设备/无 flash/无 OTA/无正式 PR；唯一推送 = 对既有远端 `pre-review/193-final-candidate` 的一次 fast-forward。
