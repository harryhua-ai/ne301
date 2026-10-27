# AC5 独立 App 站点方案（第二 IP）——设计与离线证据

> **注：方案 A（同 IP 双端口、origin 级隔离）已于 2026-10-26 由 User/A 正式采纳（#48 AC5 合同修订），本文件所述第二 IP 方案转为备选归档。现行实现见 origin_scheme_harness.c 与 PR 正文。**

## 方案

管理入口保持现状（设备 IP₁ 或设备主机名，端口 80/443）；App 站点 = **同一物理网口的第二个 IP 地址 IP₂**，同一 Web 监听器（bind 0.0.0.0）按 Host 分类分流：

| 入口 | Origin | Host 分类 | 可得内容 |
|---|---|---|---|
| 管理（按 IP） | http://IP₁ | ADMIN | 管理 UI/API；`/appsite/` 403（PR #153） |
| 管理（按主机名） | http://<hostname> | ADMIN | 同上 |
| App 站点 | http://IP₂ | APP_SITE | 仅 `/appsite/g<gen>/`（ACTIVE/世代/SHA 门）；`/api/v1/*` 403 |
| 其他 Host | — | UNKNOWN | 403 fail-closed |

`app_site_origin = http://IP₂`（canonical，无默认端口后缀——真实 `sidecarProjectionServable` 要求 `new URL(o).origin === o`）。启动时序：持久化配置读出 IP₂ → `app_bridge_app_site_origin_set()` 先于 Web 服务可用；未配置 = null → UI fail-closed（现状语义保留）。

## 为什么是第二 IP（矩阵依据，真实 `isCrossSite`/`sidecarProjectionServable`）

- 管理 IP₁ + App IP₂：siteOf(IP) 全串比较 → cross-site ✓
- 管理 hostname + App IP₂：名称 vs IP → cross-site ✓（**唯一同时满足两种管理入口方式的方案**）
- 端口方案：siteOf 不看端口 → 同 Site ✗（A 终裁已排除）
- 子域 app.<hostname>：last-2-labels → 与管理 hostname 同 Site ✗（主机名入口陷阱）
- mDNS 方案：`LWIP_MDNS_RESPONDER=0` 且不能假定单网卡双名/目标浏览器 mDNS 可靠 → 不采

## 离线证据（本分支）

1. **真实前端模块 vitest 4/4**（`Web/src/test/appManagement/siteSchemeMatrix.test.ts`，import 真实 `sidecarProjectionServable`/`isCrossSite`）：双 IP 正例（含 :8080 端口形态）、hostname×IP₂ 正例、端口/子域/同源/null 陷阱、inactive/appid/abi/非 canonical origin fail-closed
2. **真实浏览器端到端**（`tests/web_host_policy/testbed/site_scheme_harness.c` 双 IP + 签名中继，无 hosts 映射）：管理页 http://127.0.0.1:8080 从 API 取 projection（ACTIVE + app_site_origin=http://192.168.110.8:8080）→ 真实 `siteOf` 判定 PASS → sandbox iframe 从 **192.168.110.8** 加载 App HTML 成功（iframeLoaded=true）；App 页面对管理 API 的读/写探测全拦（count=0，reject 日志 verdict=3）；管理 IP 直接导航 /appsite 403
3. **HTTP 路由矩阵**（curl）：admin /admin.html 200、admin /appsite 403、App-IP /appsite 200、App-IP /api 403、projection 200、未知 Host 403、无 Origin POST 403

## 待 A/User 决定（未自行实施）

- IP₂ 的分配方式（DHCP 保留地址 / 管理员配置 / 相邻地址）与子网约束、冲突处理
- lwIP 双地址实现（第二 netif / 别名）——设备网络配置变更，需单独授权后实机证明
- HTTPS/证书（当前方案 HTTP；如需 HTTPS 属新证书决定）
- 真机浏览器解析/ARP/交换机行为 = real-device UNKNOWN，随 B4 v2 授权包 RUN 时取证
