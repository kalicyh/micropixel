# iButton Reader

Metalio-Claw4 的 DS2484 / DS1977 工位 App。支持连接状态检测、手动或 Alpha/Beta 派生密码读取，
以及选择工厂数据镜像、分 64 字节页写入并逐页回读校验。DS1991 保留读取功能。
界面会跟随设备当前系统语言，支持英语、简体中文、繁体中文、日语和韩语；切换系统语言后重新打开 App 即可生效。

## 接线

| Metalio-Claw4 | DS2484 |
| --- | --- |
| GP17 | SDA |
| GP15 | SCL |
| 3V3 | VCC、SLPZ |
| GND | GND |

DS2484 的 1WIRE 接 iButton DQ，外壳接地；地址为 `0x18`，I²C 为 400 kHz。
按 DS2484 模块要求配置总线上拉，SLPZ 不得悬空。DS2484 I²C 地址固定为 `0x18`；GP15/17 不能同时由其他功能占用。
这不是与另一块 ESP32 的 UART 连接，也不是将 iButton DQ 直接接到 GP17。

## 使用

1. 安装包含 iButton 服务的本分支 Host 固件和此 App Bundle。
2. 点击“设置访问密码”，输入 16 位十六进制值，按线序表示 8 字节密码。
   DS1977 使用密码 1；DS1991 的三个 SubKey 分别使用密码 1、2、3。
   键盘输入替换当前位并移动光标，“前一位”用于修正。
3. 接触单个 iButton，点击“读取”。ID 以 ROM 原始线序显示，包含 family 和 CRC8。
4. DS1977 每页 64 字节，共 512 页（包含末尾控制/密码页）；DS1991 每页 48 字节，共 3 页。
   页面读取是按需执行的，不是全芯片快照。失败后不显示上次读取的数据。
5. 工厂烧录时打开“工厂烧录”，选择数据镜像和密码来源，先检查目标 ROM，再二次确认写入。
   每次只写入单颗 DS1977，目标 ROM 在每页写入前复核；写入通过 Scratchpad CRC、设备 ACK 和回读比对。

Alpha/Beta 规则按 DS1977 ROM 生成读取/写入密码。手动密码只保存在 App 本次运行的内存中，不持久化，
不记录到日志。DS1977 读取保留 3000 µs 强上拉；写入使用 25 ms 强上拉并逐页验证。
DS1991 没有数据 CRC，错误密码可能产生伪数据，App 会明确提示。

## 本机工厂数据目录

数据目录不会提交到 Git，也不要把包含工厂数据的 Bundle 发布到公开商店。打包脚本从本机目录读取 `.bin`：
要求每个文件恰好为 16 字节填充头部（`00`、`55` 或 `FF`）加 4096 字节镜像；不符合格式时会停止打包。

```bash
python3 guest/apps/ibutton-reader/tools/package_factory_catalog.py \
  "/Users/kalicyh/Downloads/DS1977FD数据" \
  --output-dir build/ibutton-reader-factory
```

生成的本地 Bundle 内含所选目录的全部镜像；只安装到受控工厂工位。

## 构建

先按[构建指南](../../../docs/development/flashing.zh-CN.md)配置 ESP-IDF 6.1、WASI SDK 33 和匹配 WAMRC：

```bash
bash tools/p4.sh build-host
python3 guest/apps/ibutton-reader/tools/package_factory_catalog.py "/path/to/DS1977FD数据"
bash tools/tests/test_firmware_host.sh --ibutton-only
bash tools/tests/test_firmware_host.sh
bash tools/check_firmware_style.sh --format-only
```

仅 Metalio-Claw4 提供硬件实现；其他板返回不支持。固件构建不包含自动烧录。
真实烧录前先用工厂测试样片确认设备密码和镜像内容。协议构建不替代真实 DS2484 的接触、供电和强上拉验证。
