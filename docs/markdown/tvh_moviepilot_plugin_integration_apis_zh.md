# TVH MoviePilot 插件对接接口

English version: [TVH MoviePilot Plugin Integration APIs](tvh_moviepilot_plugin_integration_apis)

本文记录 `bata` 分支里最近新增或增强、适合 MoviePilot TVH Helper 插件、
插件里的 Telegram 交互入口，以及其他自动化客户端对接的 TVH API 与 Webhook
负载字段。

本文里的“机器人”不是一个单独部署的 Bot 服务，而是 MoviePilot TVH Helper
插件内提供的交互层。Telegram 命令、按钮和通知由 MoviePilot 插件处理，TVH
只负责暴露 API 和 Webhook 事件。

参考实现：

* MoviePilot 插件仓库：
  `https://github.com/qqcomeup/MoviePilot-Plugins`
* TVH Helper 插件目录：
  `https://github.com/qqcomeup/MoviePilot-Plugins/tree/main/plugins.v2/tvhhelper`
* TVH Helper 插件 README：
  `https://github.com/qqcomeup/MoviePilot-Plugins/blob/main/plugins.v2/tvhhelper/README.md`

TVH 不内置、不硬编码这个插件。这里贴出插件地址，只是作为 MoviePilot 侧后续维护和
二次开发的参考实现。

本文示例均已脱敏。请把 `<tvh-host>`、`<user>`、`<password>`、`<token>`、
`<moviepilot-webhook-url>` 替换为实际部署值。不要把真实账号、密码、公开域名、
内网 IP、回调密钥或 API 密钥提交到文档。

## 范围

下面的接口按 MoviePilot 插件常见使用方式整理：

* 首页状态面板
* Webhook 目标管理与测试
* 播放、DVR、DVB、服务异常通知
* DVR 录制任务管理
* EPG 节目指南查询和交互式预约录制

部分接口是 Tvheadend 原有接口。本文只在它们和最近 `bata` MoviePilot
插件/Webhook 改动有关，或建议插件配合新能力一起使用时列出。

## 服务状态与健康信息

### `GET /api/serverinfo`

最近 `bata` 分支增强了 `serverinfo`，可以直接作为机器人 `/tvh` 首页状态数据源。

重要字段：

| 字段 | 机器人用途 |
| ---- | ---------- |
| `sw_version` | 展示 Tvheadend 版本。 |
| `start_time` | 展示服务启动时间。 |
| `current_time` | 计算服务端本地状态时间。 |
| `uptime` | 不解析日志也能展示运行时长。 |
| `system.cpu_percent` | 展示 CPU 使用率。首次请求可能没有该值，因为它需要两次采样计算。 |
| `system.memory_total` | 展示总内存。 |
| `system.memory_available` | 展示可用内存。 |
| `system.memory_used` | 展示已用内存。 |
| `system.memory_used_percent` | 展示内存使用百分比。 |
| `network.rx_bytes` / `network.tx_bytes` | 展示累计网络流量。 |
| `network.rx_bps` / `network.tx_bps` | 展示实时网速。首次请求可能没有该值，因为它需要两次采样计算。 |
| `recording_storage.path` | 当前容器或宿主机使用的录制目录。 |
| `recording_storage.total` | 录制目录总空间。 |
| `recording_storage.available` | 录制目录可用空间。 |
| `recording_storage.used_percent` | 录制目录已用百分比。 |

机器人用途：

* `/tvh` 首页摘要。
* 创建 DVR 录制任务前做健康检查。
* 录制前做空间不足提醒。
* TVH 重启后轮询恢复状态。

脱敏示例：

```text
GET http://<tvh-host>/api/serverinfo
```

### `POST /api/server/restart`

最近 `bata` 新增的机器人触发 TVH 重启接口。

权限：admin。

响应：

```json
{"restart":"requested"}
```

机器人用途：

* 调用前必须加确认页。
* 调用后轮询 `GET /api/serverinfo`，直到接口恢复并且 `start_time` 发生变化。

## 在线播放与下载状态接口

### `GET /api/status/subscriptions`

Tvheadend 原有接口。最近 Webhook 逻辑已经把 DVR 内部订阅和真实用户播放分开过滤，
机器人展示在线用户时也建议使用同样思路。

机器人用途：

* 当前直播播放列表。
* 识别正在录制产生的 DVR 内部订阅。
* 展示码率、客户端、服务、错误计数等信息。

常见字段会随订阅类型变化，通常包括：

* 订阅 ID
* 用户名
* 客户端
* 服务
* 频道或标题
* 输入/输出码率
* 错误计数

### `GET /api/status/connections`

Tvheadend 原有接口。建议配合 subscriptions 使用，用于识别录制文件下载或只有 HTTP
连接、没有订阅的场景。

机器人用途：

* 当前录制文件下载列表。
* 区分直播播放和录制文件下载。
* 需要时关闭 HTTP 连接。

### `POST /api/connections/cancel`

状态 API 模块里的原有接口。

权限：admin。

请求示例：

```text
POST /api/connections/cancel
id=<connection-id>
```

```text
POST /api/connections/cancel
id=all
```

机器人用途：

* 关闭单个在线用户连接。
* 紧急关闭全部连接。

## Webhook 目标管理

### `GET /api/webhook/targets/grid`

最近 `bata` 新增接口。以 WebUI/机器人可编辑行的形式返回 `webhook_targets`。

权限：admin。

机器人用途：

* 展示已配置的 Webhook 目标。
* 确认 MoviePilot 播放、DVR、异常通知目标是否启用。
* 构建“Webhook 健康检查”页面。

### `POST /api/webhook/targets/save`

最近 `bata` 新增接口。把 Webhook 目标行保存回 `config.webhook_targets`。

权限：admin。

目标字段：

| 字段 | 用途 |
| ---- | ---- |
| `name` | 人类可读的目标名称。 |
| `enabled` | 启用或禁用目标。 |
| `url` | 接收端 URL。文档中必须使用脱敏占位符。 |
| `token` | 作为 `X-Tvh-Token` 发送。不要在日志或界面明文展示。 |
| `hmac_secret` | 可选 HMAC 签名密钥。不要在日志或界面明文展示。 |
| `events` | 事件过滤列表，例如 `playback.*` 或 `dvr.*`。 |
| `ssl_verify` | 是否校验 HTTPS 证书。 |
| `timeout` | HTTP 超时时间，单位秒。 |
| `retry_count` | 临时投递失败时的重试次数。 |
| `retry_interval` | 重试间隔，单位秒。 |
| `template` | 可选模板选择器或接收端标签。 |

脱敏目标示例：

```json
[
  {
    "name": "moviepilot-playback",
    "enabled": true,
    "url": "<moviepilot-webhook-url>",
    "token": "<shared-secret>",
    "hmac_secret": "<hmac-secret>",
    "events": ["system.webhooktest", "playback.*"],
    "ssl_verify": true,
    "timeout": 10,
    "retry_count": 2,
    "retry_interval": 3
  },
  {
    "name": "moviepilot-dvr",
    "enabled": true,
    "url": "<moviepilot-webhook-url>",
    "token": "<shared-secret>",
    "events": ["dvr.*", "service.error", "dvb.error"],
    "ssl_verify": true,
    "timeout": 10,
    "retry_count": 2,
    "retry_interval": 3
  }
]
```

机器人用途：

* 启用或禁用一个 MoviePilot 目标。
* 把播放通知和 DVR/异常通知拆成不同目标。
* 不通过 SSH 或手工编辑 JSON，也能测试目标配置。

### `GET /api/webhook/test`

最近 `bata` 新增接口。向队列写入一个 `system.webhooktest` 测试事件。

权限：admin。

成功响应：

```json
{"success":true,"message":"Webhook test queued"}
```

机器人用途：

* “测试 Webhook”按钮。
* 修改 URL、token 或 HMAC secret 后验证 MoviePilot 接收端。
* 不要对该事件做去重；重复测试是正常行为。

## Webhook 事件与负载

最近 `bata` 新增原生 TVH Webhook 投递。Webhook 异步执行，因此接收端延迟不会阻塞
播放和 DVR 主流程。

支持的事件名：

| 事件 | 触发条件 | 机器人动作 |
| ---- | -------- | ---------- |
| `system.webhooktest` | 手动测试 Webhook。 | 显示测试结果。 |
| `playback.start` | 用户开始播放。 | 发送开始播放通知。 |
| `playback.stop` | 用户停止播放。 | 发送停止播放通知，接收端可补充时长。 |
| `dvr.start` | DVR 录制开始。 | 发送开始录制通知。 |
| `dvr.complete` | DVR 录制完成，或完成时带状态字段。 | 发送录制完成通知，并由机器人检查文件大小/时长。 |
| `dvr.error` | DVR 录制失败或进入错误状态。 | 发送录制失败通知，并提供重试/重录动作。 |
| `service.error` | 服务进入异常流状态。 | 发送服务异常告警。 |
| `dvb.error` | DVB 输入或 mux 进入异常流状态。 | 发送调谐器/mux 异常告警。 |

通用 Webhook 字段：

| 字段 | 含义 |
| ---- | ---- |
| `source` | 固定来源标记，当前为 `tvheadend`。 |
| `event` | 事件名。 |
| `event_id` | 事件 ID，供接收端去重使用。 |
| `timestamp` | 事件创建时间。 |
| `server.name` | Tvheadend 服务名称。 |
| `server.version` | 生成事件的 Tvheadend 版本。 |

播放负载新增字段：

| 字段 | 含义 |
| ---- | ---- |
| `subscription_id` | TVH 订阅 ID。 |
| `started` | 订阅开始时间。 |
| `user` | TVH 用户名。 |
| `ip` | TVH 看到的客户端主机/IP。 |
| `client` | 客户端 User-Agent 或应用名。 |
| `title` | 订阅标题。 |
| `channel` | 可用时为频道名。 |
| `service` | 适配器/服务显示名。 |
| `errors` | 订阅错误计数。 |
| `input_kbps` / `output_kbps` | 输入和输出码率。 |
| `channel_uuid` | 频道 UUID。 |
| `channel_icon` | 经过 TVH 图片处理后的频道图标 URL。 |
| `program_event_id` | 当前 EPG 事件 ID。 |
| `program_start` / `program_stop` | 当前节目时间范围。 |
| `program_title` | 当前节目标题。 |
| `program_subtitle` | 当前节目副标题。 |
| `program_summary` | 当前节目摘要。 |
| `program_description` | 当前节目简介。 |
| `program_image` | 可用时为当前节目图片 URL。 |

机器人接收端建议：

* 通知图片优先使用 `channel_icon`。
* 没有频道图标时再回退到 `program_image`。
* TVH 提供多语言文本时优先使用中文 EPG 字段。
* 不要依赖临时 connection id 判断稳定播放身份；必要时组合用户、频道/标题、
  客户端和来源字段。

DVR 负载新增字段：

| 字段 | 含义 |
| ---- | ---- |
| `dvr_uuid` | DVR 条目 UUID。 |
| `sched_state` | 排程状态。 |
| `recording_state` | 录制状态。 |
| `errors` | DVR 错误计数。 |
| `data_errors` | 广播/数据错误计数。 |
| `last_error` | 最近一次流错误码。 |
| `last_error_text` | 最近一次流错误文本。 |
| `start` / `stop` | 计划录制时间范围。 |
| `title` | 录制标题。 |
| `subtitle` | 录制副标题。 |
| `channel` | 频道名称。 |
| `filename` | 已知时为录制文件路径。 |
| `user` | DVR 所属用户。 |

机器人接收端建议：

* 少量 `data_errors` 应视作信号质量信息，不要自动判定为失败。
* 判定“需重录”时，应结合 `recording_state`、`sched_state`、`last_error`、
  文件大小和视频时长。
* 除非部署明确允许，不要把完整文件系统路径发到公开群。

异常负载新增字段：

| 事件 | 重要字段 |
| ---- | -------- |
| `service.error` | `service_uuid`, `service`, `adapter`, `status_flags`, `status_text` |
| `dvb.error` | `input_uuid`, `mux_uuid`, `input`, `mux`, `status_flags`, `status_text` |

## 机器人工作流可用的 DVR API

这些接口不全是新接口，但最近的机器人功能依赖它们。

| 接口 | 用途 |
| ---- | ---- |
| `GET /api/dvr/entry/grid` | 全部 DVR 条目。 |
| `GET /api/dvr/entry/grid_upcoming` | 已预约/即将录制。 |
| `GET /api/dvr/entry/grid_finished` | 已完成录制。 |
| `GET /api/dvr/entry/grid_failed` | 失败录制。 |
| `POST /api/dvr/entry/create_by_event` | 根据 EPG 事件 ID 创建 DVR 条目。 |
| `POST /api/dvr/entry/create` | 手动创建 DVR 条目。 |
| `POST /api/dvr/entry/stop` | 优雅停止正在录制的任务。 |
| `POST /api/dvr/entry/cancel` | 取消已预约或正在录制的任务。 |
| `POST /api/dvr/entry/remove` | 删除 DVR 条目和录制文件。`bata` 已修复无 config 条目删除时可能崩溃的问题。 |
| `POST /api/dvr/entry/rerecord/allow` | 允许失败/已完成条目再次录制。 |
| `POST /api/dvr/entry/rerecord/deny` | 禁止重录。 |
| `POST /api/dvr/entry/rerecord/toggle` | 切换重录标记。 |

机器人用途：

* 从 EPG 里选择“录制这个节目”。
* 创建任务时传入条目级 `start_extra` 和 `stop_extra`，实现“提前 X 分钟录制 /
  延后 X 分钟停止”。
* 展示“预约中 / 录制中 / 已完成 / 失败”任务视图。
* 提供“取消”、“停止”、“删除文件”、“允许重录”等按钮。

推荐的 DVR 创建方式：

* 专用 DVR 配置里的全局 pre/post padding 保持 `0`。
* 由机器人在每条任务上显式传入 `start_extra` 和 `stop_extra`。
* 如果部署录制前会预热调谐器，机器人确认页应提示预热时间。

## 交互式预约录制可用的 EPG API

这些是 Tvheadend 原有接口，但对机器人交互非常关键：

| 接口 | 用途 |
| ---- | ---- |
| `GET /api/epg/events/grid` | 列出当前/即将播出的节目。 |
| `GET /api/epg/events/load` | 读取单个节目详情。 |
| `GET /api/epg/events/alternative` | 查找替代播出。 |
| `GET /api/epg/events/related` | 查找相关节目。 |
| `GET /api/epg/content_type/list` | 节目分类列表。 |

机器人用途：

* 频道 -> 节目列表 -> 确认录制。
* DVR 失败后查找重播。
* 展示节目时间、时长、进度、摘要、简介、频道图标和节目图片。

## 安全与日志规则

机器人对接应遵守以下规则：

* 不要在日志、截图或提交文档里打印真实 `token`、`hmac_secret`、API key、
  密码或回调 URL。
* 优先使用 `<moviepilot-webhook-url>` 和 `<shared-secret>` 这类占位符。
* 不要在源码里硬编码部署域名或内网 IP。
* Webhook body 视作运维数据；除非明确启用，不要把完整文件路径转发到公开群。
* 破坏性操作必须使用机器人确认页：
  `server/restart`、`connections/cancel`、`dvr/entry/cancel`、
  `dvr/entry/stop`、`dvr/entry/remove`。

## MoviePilot 插件功能速查

| 插件功能 | 主要 TVH API |
| -------- | ------------ |
| `/tvh` 首页 | `serverinfo`, `status/inputs`, `status/subscriptions`, `status/connections` |
| Webhook 健康检查 | `webhook/test`, `webhook/targets/grid` |
| 配置 MoviePilot 目标 | `webhook/targets/grid`, `webhook/targets/save` |
| 当前播放 | `status/subscriptions`, `status/connections` |
| 关闭在线连接 | `connections/cancel` |
| 频道节目列表 | `epg/events/grid` |
| 节目详情 | `epg/events/load` |
| 预约录制 | `dvr/entry/create_by_event` 或 `dvr/entry/create` |
| 即将录制 | `dvr/entry/grid_upcoming` |
| 已完成录制 | `dvr/entry/grid_finished` |
| 失败录制 | `dvr/entry/grid_failed` |
| 停止正在录制 | `dvr/entry/stop` |
| 取消预约录制 | `dvr/entry/cancel` |
| 删除录制文件 | `dvr/entry/remove` |
| 重试/重录 | `dvr/entry/rerecord/allow` |
