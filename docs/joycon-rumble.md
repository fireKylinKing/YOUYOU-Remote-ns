# Joy-Con 游戏震动

本功能把 UU Windows 回传的普通双马达震动转换为 Joy-Con 的低频和高频振动。默认开启，仅在串流中的 Xbox 手柄直连模式生效；打开本地菜单、切回键鼠、失去焦点、手柄断开、会话断开或正常退出时停止。

## 使用和排查

1. 在 Switch 系统设置中打开手柄振动，使用有振动能力的 Joy-Con 或 Pro 手柄。Switch Lite 内置控制器没有振动马达，需要外接支持的手柄。
2. 在客户端首页按住 L 和 R，再按 Y，执行本地自检：左侧振动约 350 ms，间隔后右侧振动约 350 ms，然后自动停止。单 Joy-Con 的两段都在同一设备输出。自检无需电脑连接，需能正常进入应用首页。
3. 连接电脑，长按减号切到 Xbox 手柄直连，进入支持 XInput 震动并启用了震动的游戏。
4. 本地自检有效而游戏无震动时，检查 `connection.log`：`rumble-feedback-received` 表示本次 CONTROL 通道至少收到过一条有效且处于启用状态的反馈；`rumble-output-applied` 表示本地输出调用成功。两者都不是物理马达移动的证明。
5. `rumble-output-unavailable` 或 `rumble-output-failed` 表示设备初始化或发送失败。为避免失败重试风暴，发送失败后需断开并重新连接手柄（让客户端观察到设备变化）或重启程序。

可在 `config.ini` 中设置 `rumble=0` 禁用包括自检在内的震动功能，重新启动生效。默认值为 1。

## 协议和映射

只处理现有 CONTROL 通道中的文本或二进制 UTF-8 JSON，例如：

```json
{"action":"gpd_vibration","index":0,"left_motor":180,"right_motor":90}
```

最多 512 字节；必须是上述四个字段组成的平坦对象，不接受重复字段、额外字段、空字符、错误类型、非整数、超出 0–255 的数值或非零手柄编号。其他控制消息继续交给原有处理逻辑。

`left_motor` 是低频强度，`right_motor` 是高频强度；它们不是物理左右 Joy-Con。两侧均应用同一组低频和高频强度，振幅分别除以 255，频率先采用 libnx 示例中的 160 Hz 和 320 Hz。第一版不复现 Xbox 波形，也不提供游戏专用 HD Rumble 波形。

## 生命周期和线程

网络回调只解析并更新会话私有的最新状态，既不调用 HID，也不把高频震动消息放入 Client 事件队列。会话 generation 不匹配或本地未启用时丢弃。每次启停、换手柄和换会话都会清空状态，暂停后不重放。

一个独立线程每约 10 ms 检查状态，独占初始化、发送、停止等 libnx 振动调用。只在参数变化时发送；正常 0/0 反馈立即成为停止状态。电脑可能只发送状态变化，因此不会仅因长时间没有新反馈而切断持续震动。

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
