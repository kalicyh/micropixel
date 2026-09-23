# iButton Reader

Metalio-Claw4 的只读 App，适配 esp-1wire `DS1977_1991` 中的 DS2484、DS1977 和
DS1991 读取协议。点击“读取”显示当前 ROM ID 和第一页十六进制数据；通过“上一页 / 下一页”
读取其他地址。每次翻页都会重新检查 ROM，设备更换或同时连接多个设备时拒绝读取。

## 接线

| Metalio-Claw4 | DS2484 |
| --- | --- |
| 任一开放 GPIO | SDA（App 自动探测） |
| 另一开放 GPIO | SCL（App 自动探测） |
| 3V3 | VCC、SLPZ |
| GND | GND |

DS2484 的 1WIRE 接 iButton DQ，外壳接地；地址为 `0x18`，I²C 为 400 kHz。
按 DS2484 模块要求配置总线上拉，SLPZ 不得悬空。GP15/17 不能同时由其他功能占用。
这不是与另一块 ESP32 的 UART 连接，也不是将 iButton DQ 直接接到 GP17。

## 使用

1. 安装包含 iButton 服务的本分支 Host 固件和此 App Bundle。
2. 点击“设置访问密码”，输入 16 位十六进制值，按线序表示 8 字节密码。
   DS1977 使用密码 1；DS1991 的三个 SubKey 分别使用密码 1、2、3。
   键盘输入替换当前位并移动光标，“前一位”用于修正。
3. 接触单个 iButton，点击“读取”。ID 以 ROM 原始线序显示，包含 family 和 CRC8。
4. DS1977 每页 64 字节，共 512 页（包含末尾控制/密码页）；DS1991 每页 48 字节，共 3 页。
   页面读取是按需执行的，不是全芯片快照。失败后不显示上次读取的数据。

初始密码为全 FF，仅为输入初值，不保证适合设备。密码只保存在 App 本次运行的内存中，不持久化，
不记录到日志。DS1977 读取保留 3000 µs 强上拉并校验页面 CRC16。
DS1991 没有数据 CRC，错误密码可能产生伪数据，App 会明确提示。
此版本只提供识别和读取，不提供数据写入、密码修改或 ROM 写入。

## 构建

先按[构建指南](../../../docs/development/flashing.zh-CN.md)配置 ESP-IDF 6.1、WASI SDK 33 和匹配 WAMRC：

```bash
bash tools/p4.sh build-host
python3 tools/micropixel build guest/apps/ibutton-reader --profile release --output-dir build/ibutton-reader
bash tools/tests/test_firmware_host.sh --ibutton-only
bash tools/tests/test_firmware_host.sh
bash tools/check_firmware_style.sh --format-only
```

仅 Metalio-Claw4 提供硬件实现；其他板返回不支持。固件构建不包含自动烧录。
协议单元测试使用模拟总线，不能替代真实 DS2484 的接触、供电、强上拉与密码验证。
