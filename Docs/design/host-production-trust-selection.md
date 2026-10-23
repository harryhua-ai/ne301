# NE301 Host 生产发行者信任选择

本记录中的 App 签名与发行者信任链已由 #136 退役。当前 Host 格式和安装策略见 [NEAPP05 无签名单文件格式](./app-unsigned-single-file-format.md)。

## 状态

此说明定义 Host 编译期 issuer trust profile。当前没有经指定来源确认的生产 publisher ID、DER SPKI 或 SPKI 指纹；因此本仓库只提供 production `unprovisioned` fail-closed 选择和每次测试运行生成的 synthetic fixture。Synthetic identity 不是产品身份。

## 构建选择

根构建的 `APP_TRUST_PROFILE` 默认为 `test`。`test` 固定使用现有 `ne301-exp-pub` 测试锚，并在 profile 元数据中标记为非生产。`production` 不会读取或回退到该锚。

Production 构建可通过 `APP_TRUST_PROVISION_HEADER=/absolute/path/to/issuer_config.h` 指定外部配置头。该头必须定义：

- `APP_TRUST_PRODUCTION_PUBLISHER_ID`：小写 publisher identifier。
- `APP_TRUST_PRODUCTION_SPKI_DER`：P-256 SubjectPublicKeyInfo DER 字节数组初值。
- `APP_TRUST_PRODUCTION_SPKI_SHA256`：上述 DER 的 32 字节 SHA-256 摘要初值。
- `APP_TRUST_PRODUCTION_SPKI_SHA256_HEX`：同一摘要的 64 位小写十六进制文本；运行时会与摘要字节及 DER 实际摘要交叉校验。
- `APP_TRUST_PRODUCTION_PROVENANCE`：publisher/SPKI 指定来源标识。
- `APP_TRUST_PRODUCTION_PROVENANCE_CLASS`：正式固件必须设为 `APP_TRUST_PROVENANCE_CLASS_PRODUCTION`。合成 fixture 使用 `APP_TRUST_PROVENANCE_CLASS_SYNTHETIC`，仅供 Host 测试；Appli 固件构建会拒绝该类别。

生产参数通过普通构建配置注入；不得提交生产私钥或 secrets。Production profile 缺少配置时锚点状态为 `unprovisioned`，信任验证关闭。配置不完整时也不加载 test 锚；格式、publisher、SPKI P-256 曲线、digest、provenance 或 provenance class 校验失败时状态为 `malformed`。Production 明确拒绝 `ne301-exp-pub`；固件编译期也会拒绝该 publisher、synthetic provenance class 和 `SYNTHETIC_TEST_ONLY` 来源标识。

Appli 构建会比较 profile、配置头路径及配置头校验和。切换 profile 或更换注入头后，信任锚对象会自动重编译并重新链接，无需手动清理构建目录。

## 信任与运行时

信任根只来自编译期锚。Package/native/UI/Pair 内容不提供可写 trust root。Production 仍使用 #124 的 native、NEUI、Pair 三签名校验、session/run gate 和单调序号 floor；本功能不增加第二套安装状态或持久化信任根。

现有只读 admission 状态响应呈现 profile、publisher、SPKI SHA-256、provenance 与状态，用于核对固件身份。缺少真实 production SPKI 时，响应只应显示 `production` / `unprovisioned`，不能把 synthetic fixture 标成生产身份。

## 测试

`tests/app_installer` 每次生成独立的 synthetic P-256 issuer，并在 `build/synthetic` 构造真正经 ECDSA 签名的 native、NEUI 与完整 Pair fixture。该 fixture 走已存在的 #124 gate 测试覆盖安装、显式运行、Pair floor、重放、同序不同内容冲突和降级。其他负例覆盖 test issuer、publisher/SPKI 混配、格式损坏与包内替换信任根。synthetic 私钥只在 fixture 生成进程内存中存在，不写入文件；`make clean` 清除生成的公开配置与测试包。

Synthetic 私钥仅供测试构造，测试结束后清理；它不代表生产密钥托管、签名授权、设备验收或正式发布。
