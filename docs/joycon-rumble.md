# Joy-Con 游戏震动

本功能把 UU Windows 回传的普通双马达震动转换为 Joy-Con 的低频和高频振动。默认开启，仅在串流中的 Xbox 手柄直连模式生效；打开本地菜单、切回键鼠、失去焦点、手柄断开、会话断开或正常退出时停止。

## 使用和排查

1. 在 Switch 系统设置中打开手柄振动，使用有振动能力的 Joy-Con 或 Pro 手柄。Switch Lite 内置控制器没有振动马达，需要外接支持的手柄。
2. 在客户端首页按住 L 和 R，再按 Y，执行本地自检：左侧振动约 350 ms，间隔后右侧振动约 350 ms，然后自动停止。单 Joy-Con 的两段都在同一设备输出。自检无需电脑连接，需能正常进入应用首页。
3. 连接电脑，长按减号切到 Xbox 手柄直连，进入支持 XInput 震动并启用了震动的游戏。
4. 本地自检有效而游戏无震动时，检查 `connection.log`：`rumble-feedback-received` 表示本次 CONTROL 通道至少收到过一条有效且处于启用状态的反馈；`rumble-output-applied` 表示本地输出调用成功。两者都不是物理马达移动的证明。
5. `rumble-output-unavailable` 或 `rumble-output-failed` 表示设备初始化或发送失败。为避免失败重试风暴，发送失败后需断开并重新连接手柄（让客户端观察到设备变化）或重启程序。

0.3.76 起可直接在 Switch 界面的「外设 → 手柄震动」或「外设 → 手柄设备 → 手柄震动」开关。串流中长按加号键打开菜单即可进入，关闭后停止游戏震动及首页自检，无需断开连接或重启；重新开启只接受新的反馈，不重放旧强度。该开关为本机全局设置，不按游戏自动切换。

开关保存到 `config.ini` 的 `rumble=0/1`，重启后沿用，默认开启；保存失败时本次选择仍生效，界面会提示重启后可能恢复旧设置。也可手动编辑配置后重启应用。开关不改变按键输入、震动强度配置或反馈记录开关；禁用期间的反馈不会进入已接受反馈日志。

## 协议和映射

只处理现有 CONTROL 通道中的文本或二进制 UTF-8 JSON，例如：

```json
{"action":"gpd_vibration","index":0,"left_motor":180,"right_motor":90}
```

最多 512 字节；必须是上述四个字段组成的平坦对象，不接受重复字段、额外字段、空字符、错误类型、非整数、超出 0–255 的数值或非零手柄编号。其他控制消息继续交给原有处理逻辑。

`left_motor` 是低频强度，`right_motor` 是高频强度；它们不是物理左右 Joy-Con。两侧均应用同一组低频和高频强度，频率固定为 160 Hz 和 320 Hz。不复现 Xbox 波形，也不提供游戏专用 HD Rumble 波形。

## 输出调节（0.3.75）

默认总强度 75%，低频系数 100%，高频系数 80%。目标振幅为 `原始值 / 255 * 总强度 / 100 * 对应频段系数 / 100`，因此原始 255/255 对应低频 0.75、高频 0.60。非零强度变化采用线性限速，默认满幅变化约 20 ms；任一路目标为零时立即将该路清零，不增加停止拖尾。启停、换会话和换设备时清空过渡状态。首页自检仍用原有固定测试振幅，不参与游戏手感调节。

以下配置在 `sdmc:/switch/UURemote-ns/config.ini` 设置，重启应用生效；已有配置值会保留：

```ini
rumble=1
rumble_strength=75
rumble_low_gain=100
rumble_high_gain=80
rumble_smooth_ms=20
rumble_trace=1
```

三项强度范围均为 0–100%，过渡范围为 0–100 ms；`rumble_smooth_ms=0` 关闭过渡。要对照旧强度映射，可将三项强度都设为 100、过渡设为 0。不会通过延长停止来人为保留短震；5 ms 输出检查仍可能合并同一轮检查前到达的反馈。

## 强度与间隔记录

`rumble_trace=1` 默认启用本轮测试记录；设为 0 可关闭。文件为 `sdmc:/switch/UURemote-ns/rumble.log`，独立于连接日志，约 1 MiB 后轮转为 `.1`、`.2`。每份带构建版本。

- `rumble-rx`：每条通过当前会话和本地启用检查的反馈，含原始 `low/high`（0–255）、本地单调时间 `t_ms`、本地序号 `seq`、启停代数 `epoch`、与上一条已接受反馈的接收间隔 `dt_ms`。每次重新启用后的首条间隔为 0；这是客户端接收间隔，不是游戏生成时间或网络延迟。
- `replaced=1`：该反馈替换了尚未被输出线程读取的状态，可用来排查短震合并；也可能只是重复强度，不能单独证明漏震。
- `rumble-out`：成功提交给 HID 的低/高频振幅 `out_low/out_high`（0–1）、本轮目标原始强度、输出时间、物理侧掩码及 `mode=remote/test`。调用成功不等于物理马达已达到对应振幅。
- `rumble-stop-attempt`：设备切换、发送失败或退出清理时尝试清零，不表示 HID 停止调用一定成功。
- `rumble-rx-dropped` 和 `# dropped_entries_total`：分别标记接收记录环和异步写入队列的丢失；出现时日志不完整。

网络回调只往固定 512 条环形缓冲写入数值记录，不写 SD 卡。输出线程先更新 HID，再提交异步日志；异步写入线程不可用时丢弃日志，不退回同步写盘。未启用时的反馈、非法消息及旧会话消息不进入该日志。日志不含原始消息、认证信息或按键输入。

## 生命周期和线程

网络回调只解析并更新会话私有的最新状态，既不调用 HID，也不把高频震动消息放入 Client 事件队列。会话 generation 不匹配或本地未启用时丢弃。每次启停、换手柄和换会话都会清空状态，暂停后不重放。

一个独立线程每约 5 ms 检查状态，独占初始化、发送、停止等 libnx 振动调用。只在输出参数变化时发送；正常 0/0 反馈在下一次检查时直接清零，不做平滑衰减。电脑可能只发送状态变化，因此不会仅因长时间没有新反馈而切断持续震动。

主循环每帧续约；超过 500 ms 未更新时，输出线程清空状态并停止振动。这是本地 UI 卡顿保护，不是网络断开检测时限。网络停止仍依赖 WebRTC 的断开回调。积压超过 500 ms 的未消费非零反馈会被丢弃，停止反馈仍被执行。

libnx 此接口没有 SDL 那样的有限时长参数。独立线程保护主循环停顿，不能保证整个进程被强制终止、操作系统挂起或设备服务故障后仍能执行停止。休眠、HOME 切换和强制退出的物理表现需要真机验证。

## GitHub 资料

- UU 协议证据：[VallierDev/uu-remote-linux 原生手柄文档](https://github.com/VallierDev/uu-remote-linux/blob/main/docs/deckpad-native-gamepad.md)，阅读时文档 blob 为 `09dce599f4110bbcea609f6c57410374d3e6de47`。文档记录 UU Windows 4.45.0.4318 静态检查及一组 Windows/Steam Deck 用户反馈，不能推定所有 UU 版本兼容。
- libnx 官方示例：[switchbrew/switch-examples hid/vibration](https://github.com/switchbrew/switch-examples/blob/master/hid/vibration/source/main.c)，阅读时 blob 为 `8e2a581cac0cc1bdecc12aeacc7b000bcacc34fd`。参考设备句柄、掌机与双 Joy-Con 选择、停止参数；本实现独立编写。
- API 定义：[switchbrew/libnx hid.h](https://github.com/switchbrew/libnx/blob/master/nx/include/switch/services/hid.h) 与 [pad.h](https://github.com/switchbrew/libnx/blob/master/nx/include/switch/runtime/pad.h)。

## 验证范围

`rumble_test` 覆盖协议拒绝、最新状态替换、会话隔离、启停、主循环超时、持续震动和并发访问。`rumble_switch_test` 通过模拟 HID 调用验证振幅映射、左右自检、设备切换、输出失败后的停止、主循环停顿和析构停止；模拟头只用于主机测试，不进入 Switch 产品构建。

在准备好原项目依赖后，可通过 `UU_CORE_ONLY=ON` 构建并运行 `ctest -R gamepad_rumble --output-on-failure`。普通电脑上的测试不能替代 Switch NRO 构建与真机测试。

真机验收仍须覆盖：掌机/拆卸双 Joy-Con/Pro/单 Joy-Con，游戏强弱反馈与 0/0 停止，菜单、键鼠切换、HOME、软键盘、断线重连、休眠和退出。需记录电脑 UU 版本及手柄连接形态。本实现尚未宣称通过这些验收。
