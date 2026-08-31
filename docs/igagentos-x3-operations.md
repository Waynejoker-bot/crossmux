# iGAgentOS X3 使用、配置与无线刷机手册

本文是 Xteink X3 上 iGAgentOS 与 iGrowth AirPage 的交付手册，供使用者、开发同学和 AI Agent 共同执行。这里的步骤不需要拆机、不需要拔 SD 卡，也不需要读卡器。

## 1. 适用版本与资源

- 设备：Xteink X3；固件与 X4 共用 ESP32-C3/X3-X4 镜像，但本发布以 X3 为验收目标。
- 系统：iGAgentOS X3 v0.1.2，基于 CrossMux 1.5.8。
- iGAgentOS 固件源码：<https://github.com/Waynejoker-bot/crossmux>
- 上游 CrossMux：<https://github.com/0x1abin/crossmux>
- iGrowth 源码：<https://github.com/igrowth-opc/igrowth>
- 本版 Release：<https://github.com/Waynejoker-bot/crossmux/releases/tag/igagentos-x3-v0.1.2>
- 完整安装包：<https://github.com/Waynejoker-bot/crossmux/releases/download/igagentos-x3-v0.1.2/iGAgentOS-X3-v0.1.2-20260831.zip>
- 标准刷机文件：<https://github.com/Waynejoker-bot/crossmux/releases/download/igagentos-x3-v0.1.2/firmware.bin>

下载完整包后先核对 `SHA256SUMS.txt`。macOS/Linux 在包目录运行：

```bash
shasum -a 256 -c SHA256SUMS.txt
```

只有全部显示 `OK` 才继续。`firmware.bin` 与 `iGAgentOS-X3-v0.1.2.bin` 内容相同；前者是标准刷机和恢复约定名，后者便于归档和区分版本。

## 2. 第一次无线刷机

### 2.1 把刷机包通过网络传到设备

1. 给设备充足电量，启动原系统。
2. 在设备主页打开“文件传输”。
3. 选择“加入网络”，连接 2.4 GHz 的可信 Wi-Fi；电脑与设备必须在同一局域网。
4. 设备会显示 IP 地址和二维码，例如 `http://192.168.1.102/`；优先使用屏幕上的 IP，`http://crosspoint.local/` 可作备选。
5. 在电脑浏览器打开该地址，进入“文件管理”，停留在 SD 卡根目录 `/`。
6. 上传 Release 里的 `firmware.bin`。根目录已有同名文件时会直接覆盖。

不用浏览器也可以上传：

```bash
curl -f -F "file=@/绝对路径/firmware.bin" "http://设备屏幕显示的IP/upload?path=/"
```

收到 `File uploaded successfully: firmware.bin` 后，才算文件传输成功。还可以通过文件列表接口核对：

```bash
curl -fsS "http://设备屏幕显示的IP/api/files?path=/"
```

设备的文件传输服务没有登录验证，只能在可信私有网络使用；上传结束后应退出“文件传输”。

### 2.2 在设备上执行更新

1. 从“文件传输”返回主页，进入“设置”。
2. 选择“SD 卡固件更新”。
3. 选择根目录中的 `firmware.bin` 并确认。
4. 写入期间不要断电、不要拔卡、不要按复位键。
5. 完成后设备自动重启，并直接进入 AirPage。
6. 打开“设置 → 关于”，确认固件名称为 `iGAgentOS`、版本为 `0.1.2`。

正常设置更新可以选择任意有效的 `.bin` 文件名；`firmware.bin` 是我们每个发布包固定提供的标准文件名，也是按键恢复刷机使用的约定名。

### 2.3 回滚与恢复

每个正式包同时带一个已知可启动的 CrossMux 回滚文件。正常情况下仍使用“设置 → SD 卡固件更新”选择该回滚 `.bin`。

如果新固件无法进入菜单，把回滚文件改名为 `firmware.bin` 放到 SD 卡根目录，然后按设备既有恢复手势启动（按住上键并按电源键）。恢复路径仍会校验 ESP32-C3 芯片、X3/X4 板型、镜像完整性和 OTA 分区容量。没有任何软件测试能代替最后一次实机确认，因此首刷必须保留回滚文件并保持供电稳定。

## 3. 线上模式：连接 iGrowth

线上模式固定连接 `https://igrowth.cc`，不需要在 SD 卡写服务地址。

1. 确保设备已联网，打开 AirPage；首次没有缓存时会显示 AirPage 设备二维码。
2. 在 iGrowth 网页登录目标账号，进入“渠道中心 → 电子墨水屏”。
3. 上传/识别设备二维码截图，或粘贴二维码里的设备 URL，完成绑定并发送测试页。
4. 在设备打开“AirPage 设置 → iGrowth 按键”，选择“线上”。
5. 设备会显示 8 位配对码。
6. 回到 iGrowth 的墨水屏连接面板，选择“启用四键通信”，输入配对码并确认。
7. 设备显示配对成功后，发送一条新的墨水屏内容做闭环测试。

配对密钥按服务环境分开保存；从“开发”切到“线上”时，需要使用线上环境自己的配对码完成一次配对，不会复用开发环境密钥。

## 4. 开发模式：连接本机 iGrowth

设备不能访问电脑自己的 `localhost`。开发地址必须是设备能从同一局域网访问的 HTTP 地址，格式只能是 `http://<私网 IPv4 或 .local 主机名>:<非特权端口>`，不能带路径、参数、账号或密码。

### 4.1 启动本地服务

iGrowth 标准本地端口为 Web `3001`、Message Station `2048`、Agent Station `2047`。AirPage 设备直接访问 Message Station 的 `/msapi/airpage/device/*` 路由，因此开发地址通常使用开发机局域网 IP 加 `2048`。

在 iGrowth 仓库按工程说明启动本地服务，并确认 AirPage 功能已启用：

```bash
cd backend
AIRPAGE_CHANNEL_ENABLED=true docker compose up -d --build
curl -fsS http://127.0.0.1:2048/health
```

如团队为隔离实例显式映射了其他端口，例如 `2148:2048`，设备就使用映射后的 `2148`。不要因为示例而固定使用 2148；应以当前 Compose 的实际端口为准：

```bash
docker compose ps
```

查开发机局域网地址时，macOS 常用：

```bash
ipconfig getifaddr en0
```

假设返回 `192.168.1.20`，标准开发地址就是 `http://192.168.1.20:2048`。先从同一 Wi-Fi 下的另一台设备访问该地址的 `/health`；如果访问不到，检查 macOS 防火墙、访客网络/AP 隔离和端口绑定。

### 4.2 无线写入开发地址

在电脑创建一个纯文本文件 `igrowth-development.txt`，内容只有一行：

```text
http://192.168.1.20:2048
```

不要使用示例 IP，要替换成当前开发机的局域网 IP 和实际端口。然后让设备进入“文件传输 → 加入网络”，通过网页上传到 SD 卡根目录。也可以直接运行：

```bash
curl -f -F "file=@/绝对路径/igrowth-development.txt" "http://设备屏幕显示的IP/upload?path=/"
```

上传成功后退出文件传输，进入“AirPage 设置 → iGrowth 按键”，切换到“开发”。设备应显示当前开发地址；再获取 8 位配对码，到本地 iGrowth 网页的墨水屏面板完成配对。

开发环境和线上环境使用各自独立的凭据、缓存和待发送队列。切换服务环境不会把开发操作误发到线上。

## 5. 四键双向通信如何工作

四个逻辑按键不是固定的“稍后 / 继续 / 解释 / 下一步”。每次 iGrowth 生成或续写墨水屏内容时，模型同时为当前回复生成四个可能继续发散的选项；服务端把四段文案与该次投递、图片哈希、页码和原 Session 冻结并签名，设备只显示这四段已验证文案。

四个槽位的稳定协议如下：

| 设备逻辑键 | 稳定 action ID | 用户看到的文案 |
|---|---|---|
| Back | `dismiss` | 模型为本轮生成的选项 A |
| Confirm | `continue` | 模型为本轮生成的选项 B |
| Left | `explain` | 模型为本轮生成的选项 C |
| Right | `next` | 模型为本轮生成的选项 D |

按下任一键后，服务端用固定槽位找到本轮冻结文案，并把该文案写成原 iGrowth Session 里的下一条用户消息，显示为“墨水屏 · <选项文案>”。模型在同一个 Session 继续回复，回复卡片再推回同一台墨水屏。因此：

- 固件只负责显示和回传，不自行编造选项。
- action ID 固定是为了协议兼容，按键文案每一轮都可以不同。
- 当前内容与四个选项必须来自同一次签名 manifest，不能跨轮混用。
- 短按四个键分别提交 A/B/C/D；长按逻辑 Back 约 1 秒退出当前卡片，回到 AirPage 二维码页，不提交 A。

断网时按键事件最多缓存 8 条；AirPage 回到前台并恢复 Wi-Fi 后按顺序补发。服务端“已接收/已入队”只证明请求到达服务端，不等于实体屏幕已刷新，最后仍要看设备画面确认。

## 6. 开机、休眠和回到 AirPage

- 正常启动、深度休眠后的硬件唤醒、以及首次验证通过的 OTA 启动都会直接进入 AirPage。
- 有有效缓存时重开当前图片；没有缓存时显示配对/上传二维码。
- 墨水屏深度休眠时 Wi-Fi 和 MQTT 不在线，服务器不能远程唤醒。先用设备原有硬件唤醒手势唤醒，再等待 AirPage 恢复连接。
- 在一张带四键选项的卡片上，长按逻辑 Back 约 1 秒回到 AirPage 二维码页；机身复位键只用于故障恢复，不作为日常返回键。
- 自动刷新要求 AirPage 在前台、Wi-Fi 在线且实时模式开启。

## 7. 端到端验收清单

1. “设置 → 关于”显示 `iGAgentOS 0.1.2`。
2. AirPage 选择正确的“线上”或“开发”环境，页面显示的地址符合预期。
3. iGrowth 墨水屏面板显示同一设备已绑定且四键通信已启用。
4. 从某个普通用户会话推送一条墨水屏内容，实机显示本轮四个动态选项。
5. 记录按键时刻并按任一选项；网页端应在原 Session 看到“墨水屏 · <选项文案>”。
6. 模型继续回复后，同一设备收到下一张卡片和新一轮四个选项。
7. 分别记录“设备按下 → 服务端收到”和“服务端完成回复 → 实机刷新”的时间，避免把模型生成时间混进按键上行延时。
8. 断开 Wi-Fi 按一次，确认显示已排队；恢复网络后确认只补发一次。
9. 长按逻辑 Back 约 1 秒，确认回到 AirPage 二维码页且没有误提交选项 A。

## 8. 常见故障

### 文件上传成功，但“SD 卡固件更新”找不到

确认文件上传到了根目录 `/`，扩展名为 `.bin`，并重新进入更新页面。浏览器上传同名文件会覆盖旧文件。

### 选择“开发”后报地址无效

检查 `igrowth-development.txt` 是否位于 SD 卡根目录、只有一行、以 `http://` 开头、使用私网 IPv4 或 `.local` 主机名、带 1024–65535 端口且不含任何路径或参数。

### 设备显示“已发送到 iGrowth”，网页端没有消息

先确认查看的是完成绑定时的原 Session，而不是另一条同名会话；再检查本轮 manifest、配对环境、设备时钟、Message Station 日志和离线 outbox。设备反馈代表事件已提交或已进入本地队列，不代表 Agent 已完成回复。

### 四个选项仍是固定文案或互相重叠

固定文案说明设备仍在旧固件或服务端投递不含动态 label；先核对“关于”页版本和 Release 校验值。文字重叠应同时核对服务端是否只下发短标签，以及设备 footer 是否只渲染一层按键提示。v0.1.2 的固件从签名 manifest 读取每轮动态 label，不再使用固件内置的四句固定文案。

### 重刷会不会丢失绑定

正常 OTA 更新不会格式化 SD 卡，设备 ID、环境配置和配对状态都会保留。更换/格式化 SD 卡，或删除 `/.crosspoint/airpage_device_id` 与对应环境状态目录，会生成新身份并要求重新绑定。

## 9. 发布新版本的约定

每个正式包必须有唯一 SemVer，不能用同一版本号覆盖不同二进制。发布人必须：

1. 在 `platformio.ini` 更新 `[igagentos] version`。
2. 从干净、已提交的 `main` 运行 `pio run -e gh_release`。
3. 用 `scripts/package_igagentos_x3.py` 生成版本目录和 manifest，`sourceDirty` 必须为 `false`。
4. 带上 `firmware.bin`、版本化 `.bin`、回滚 `.bin`、manifest、`SHA256SUMS.txt` 和刷机说明。
5. 将源码 `main` 非强制推送到远端，并创建与版本唯一对应的 GitHub Release；禁止替换同一版本已有二进制。
6. 从 Release 重新下载并复算 SHA-256，再通知设备用户刷机。

任何开发者或 AI 在执行推送前，都要先确认设备 IP、目标版本、文件 SHA-256 和可信局域网；只能覆盖明确的目标文件，不能删除或格式化用户 SD 卡。
