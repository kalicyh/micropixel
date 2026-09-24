# iButton Reader

Metalio-Claw4 的 DS2484 / DS1977 / DS1991 工位 App。自动检测设备，尝试 Alpha/Beta 密钥，失败时允许手动输入。
认证后默认读取并显示 `0x0000–0x0097`，数据每行显示 8 字节；“完整读取”会继续读取至 DS1977 末尾 `0x7FFF`。
用户可手动触发本地镜像相似度匹配，点按数据行可逐行修改并写入校验，也可选择镜像并逐页写入校验。
设备应用大厅封面由 `assets/launch.jpg` 提供，并随 Bundle 一起安装。
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
2. 接触单个 DS1977。认证成功后会自动读取 `0x0000–0x0097`（地址 `0x0090` 的一行也包含在内）；默认不会匹配镜像。
3. 点“完整读取”会从 `0x0098` 继续读到 `0x7FFF`，然后可按 64 字节分页查看全部 32 KiB。每行地址递增 `0x0008`，例如第二行是 `0008`。
4. 点“匹配镜像”后，App 对已读取范围与本地镜像按相同字节比例评分，只显示相似度最高的一项。
5. 在 DS1977 或 DS1991 数据页点按一行可进入十六进制编辑器；保存时只更新该 8 字节行并回读校验。DS1991 按所属 SubKey 写入，不会跨越 48 字节边界；DS1977 此功能限于前 4 KB 写入区域。
6. 点“选择数据”打开镜像列表并选择镜像。确认页只提供“完整写入”，写入全部 4 KB，以保留尾部辅助数据。
   每次只写入单颗 DS1977，目标 ROM 在每页写入前复核；写入通过 Scratchpad CRC、设备 ACK 和回读比对。成功后列表短暂标绿，切换页面或列表分页后清除标记。
7. Alpha/Beta 自动认证失败时输入 16 位十六进制值（8 字节密钥）；密钥只保存在本次 App 运行内存中。

Alpha/Beta 规则按 ROM 生成读取/写入密码。DS1977 写入通过 Scratchpad CRC、设备 ACK 和回读比对；DS1991 需要正确的 SubKey 密码，按行写入后回读比对。DS1991 无数据 CRC，密码错误可能返回伪数据。

## 本机工厂数据目录

数据目录不会提交到 Git，也不要把包含工厂数据的 Bundle 发布到公开商店。打包脚本从本机目录读取 `.bin`：
要求每个文件恰好为 16 字节填充头部（`00`、`55` 或 `FF`）加 4096 字节镜像；不符合格式时会停止打包。

```bash
python3 guest/apps/ibutton-reader/tools/package_factory_catalog.py \
  "/Users/kalicyh/Downloads/DS1977FD数据" \
  --output-dir build/ibutton-reader-factory
```

目录检查发现镜像的主要数据位于 `0x0000–0x012F`（前 5 个 64 字节页），另有 `0x07D0` 与 `0x0FF1–0x0FFF` 的变化字节。完整写入覆盖全部 4 KB，以保留主数据与辅助数据。

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
真实写入前先用工厂测试样片确认设备密码和数据内容。协议构建不替代真实 DS2484 接触、供电和器件写入验证。
