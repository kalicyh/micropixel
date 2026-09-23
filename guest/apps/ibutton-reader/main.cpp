#include <array>

#include "sdk/micropixel.hpp"
#include "sdk/scene.hpp"

using namespace micropixel;
using namespace micropixel::literals;
namespace {
constexpr auto kText = Color::Rgb(222, 232, 244);
constexpr auto kAccent = Color::Rgb(26, 106, 132);
constexpr char kHex[] = "0123456789ABCDEF";
const char* StatusText(IButtonStatus status) {
    switch (status) {
        case IButtonStatus::kOk:
            return "读取完成";
        case IButtonStatus::kNoDevice:
            return "未检测到设备或设备已更换，请重新读取";
        case IButtonStatus::kMultipleDevices:
            return "请只连接一个 iButton";
        case IButtonStatus::kUnsupported:
            return "仅支持 DS1977 / DS1991";
        case IButtonStatus::kBusError:
            return "总线错误：检查接线、DS2484 和引脚占用";
        case IButtonStatus::kCrcError:
            return "CRC 错误：检查密码或设备接触";
        default:
            return "读取范围无效";
    }
}
void Hex(char* target, const uint8_t* source, unsigned length) {
    for (unsigned i = 0; i < length; ++i) {
        target[i * 2] = kHex[source[i] >> 4];
        target[i * 2 + 1] = kHex[source[i] & 15];
    }
    target[length * 2] = 0;
}

template <uint32_t Capacity>
void AppendHexByte(FixedString<Capacity>& text, uint8_t value) {
    const char bytes[]{kHex[value >> 4], kHex[value & 15U], '\0'};
    text.Append(bytes);
}

template <uint32_t Capacity>
void AppendHexWord(FixedString<Capacity>& text, uint16_t value) {
    AppendHexByte(text, static_cast<uint8_t>(value >> 8));
    AppendHexByte(text, static_cast<uint8_t>(value));
}
}  // namespace
int main() {
    Application app;
    app.renderer().ConfigureDisplay({.logical_size = {720, 720}, .scale_mode = DisplayScaleMode::kAspectFit}).value();
    auto scene = app.renderer().CreateScene(Color::Rgb(12, 20, 30)).value();
    auto main_view = scene.CreateContainer().value();
    auto password_view = scene.CreateContainer().value();
    password_view.SetVisible(false);
    auto label = [&](Container& parent, int y, const char* text, SystemFont font = SystemFont::kMedium) {
        return parent.CreateLabel({24, y}, text, kText, font).value();
    };
    auto button = [&](Container& parent, Rect bounds, const char* text) {
        return parent.CreateTextButton(
            {.bounds = bounds, .text = text, .style = {.background = kAccent, .font = SystemFont::kMedium}});
    };
    label(main_view, 20, "iButton · DS1977 / DS1991", SystemFont::kLarge);
    auto bus_label = label(main_view, 70, "DS2484 · 自动轮询 GPIO · 0x18", SystemFont::kSmall);
    auto id_label = label(main_view, 108, "ID：待读取");
    auto page_label = label(main_view, 151, "点击读取以识别设备并读取第一页");
    auto status_label = label(main_view, 192, "先设置设备的 8 字节访问密码", SystemFont::kSmall);
    std::array<LabelNode, 8> rows;
    for (unsigned i = 0; i < rows.size(); ++i) rows[i] = label(main_view, 240 + i * 34, " ", SystemFont::kSmall);
    auto warning = label(main_view, 522, " ", SystemFont::kSmall);
    auto read = button(main_view, {24, 575, 210, 60}, "读取");
    auto previous = button(main_view, {254, 575, 210, 60}, "上一页");
    auto next = button(main_view, {484, 575, 210, 60}, "下一页");
    auto edit = button(main_view, {24, 648, 670, 54}, "设置访问密码");
    label(password_view, 22, "访问密码（16 位十六进制）", SystemFont::kLarge);
    label(password_view, 76, "DS1977 使用密码 1；DS1991 分别使用密码 1 / 2 / 3", SystemFont::kSmall);
    auto password_label = label(password_view, 128, " ", SystemFont::kLarge);
    auto cursor_label = label(password_view, 180, " ", SystemFont::kSmall);
    std::array<ui::TextButton, 3> password_tabs;
    constexpr const char* kPasswordTabLabels[]{"密码 1", "密码 2", "密码 3"};
    for (unsigned i = 0; i < 3; ++i)
        password_tabs[i] = button(password_view, {24 + static_cast<int>(i) * 230, 220, 210, 52}, kPasswordTabLabels[i]);
    std::array<ui::TextButton, 16> keys;
    for (unsigned i = 0; i < 16; ++i) {
        char text[]{kHex[i], 0};
        keys[i] = button(password_view,
                         {24 + static_cast<int>(i % 4) * 170, 290 + static_cast<int>(i / 4) * 65, 155, 55}, text);
    }
    auto backspace = button(password_view, {24, 565, 320, 55}, "前一位");
    auto done = button(password_view, {374, 565, 320, 55}, "完成");
    label(password_view, 652, "仅保存在本次运行内存中；默认全 FF 不保证正确", SystemFont::kSmall);
    std::array<std::array<uint8_t, 8>, 3> passwords;
    for (auto& password : passwords) password.fill(0xFF);
    unsigned password_index = 0, cursor = 0;
    bool editing = false, selected = false, pending_scan = false, pending_read = false;
    uint16_t offset = 0, stride = 64, limit = 32768;
    std::array<uint8_t, 8> rom{};
    auto clear_data = [&] {
        for (auto& row : rows) row.SetText(" ");
    };
    auto update_password = [&] {
        char value[17];
        Hex(value, passwords[password_index].data(), 8);
        password_label.SetText(value);
        FixedString<64> text;
        text.Append("密码 ");
        text.AppendUint(password_index + 1);
        text.Append(" · 下一次输入替换第 ");
        text.AppendUint(cursor + 1);
        text.Append(" 位");
        cursor_label.SetText(text.c_str());
    };
    auto queue_read = [&](bool scan) {
        clear_data();
        pending_scan = scan;
        pending_read = !scan;
        status_label.SetText("正在读取…");
        warning.SetText(" ");
    };
    auto present = [&] { app.renderer().Present(scene).value(); };
    auto timer = app.timers().Every(30_ms).value();
    present();
    app.Run([&](const Event& event) {
        bool dirty = false;
        if (event.TimerFrom(timer) && (pending_scan || pending_read)) {
            dirty = true;
            if (pending_scan) {
                pending_scan = false;
                selected = false;
                id_label.SetText("ID：未识别");
                page_label.SetText(" ");
                auto result = app.ibutton().Scan();
                if (!result)
                    status_label.SetText("Host 未提供服务或服务调用失败");
                else if (result->status != IButtonStatus::kOk)
                    status_label.SetText(StatusText(result->status));
                else {
                    FixedString<64> bus;
                    bus.Append("DS2484 · SDA GP");
                    bus.AppendUint(result->sda_line);
                    bus.Append(" / SCL GP");
                    bus.AppendUint(result->scl_line);
                    bus.Append(" · 0x18");
                    bus_label.SetText(bus.c_str());
                    rom = result->rom;
                    char id[17];
                    Hex(id, rom.data(), 8);
                    FixedString<64> text;
                    text.Append("ID: ");
                    text.Append(id);
                    text.Append(" · ");
                    text.Append(rom[0] == 0x37 ? "DS1977" : rom[0] == 0x02 ? "DS1991" : "未知型号");
                    id_label.SetText(text.c_str());
                    selected = rom[0] == 0x37 || rom[0] == 0x02;
                    if (selected) {
                        stride = rom[0] == 0x37 ? 64 : 48;
                        limit = rom[0] == 0x37 ? 32768 : 144;
                        offset = 0;
                        pending_read = true;
                    } else
                        status_label.SetText(StatusText(IButtonStatus::kUnsupported));
                }
            } else {
                pending_read = false;
                auto result = app.ibutton().Read(rom, offset, stride, passwords[rom[0] == 0x02 ? offset / 48 : 0]);
                FixedString<96> heading;
                heading.Append("地址 ");
                AppendHexWord(heading, offset);
                heading.Append("–");
                AppendHexWord(heading, offset + stride - 1);
                heading.Append(" · ");
                heading.AppendUint(offset / stride + 1);
                heading.Append(" / ");
                heading.AppendUint(limit / stride);
                heading.Append(" 页");
                page_label.SetText(heading.c_str());
                if (!result)
                    status_label.SetText("服务调用失败，请重试");
                else {
                    status_label.SetText(StatusText(result->status));
                    if (result->status == IButtonStatus::kOk) {
                        for (unsigned row = 0; row < stride / 8; ++row) {
                            FixedString<96> text;
                            auto* data = result->data.data() + row * 8;
                            AppendHexWord(text, offset + row * 8);
                            text.Append("   ");
                            for (unsigned column = 0; column < 8; ++column) {
                                AppendHexByte(text, data[column]);
                                text.Append(column == 3 ? "   " : column == 7 ? "" : " ");
                            }
                            rows[row].SetText(text.c_str());
                        }
                        warning.SetText(rom[0] == 0x02 ? "DS1991 无数据 CRC：错误密码可能返回伪数据"
                                                       : "DS1977 页面 CRC16 校验通过");
                    }
                }
            }
        }
        if (const auto* touch = event.touch()) {
            dirty = true;
            if (editing) {
                for (unsigned i = 0; i < 3; ++i)
                    if (password_tabs[i].OnTouch(*touch).clicked) {
                        password_index = i;
                        cursor = 0;
                    }
                for (unsigned i = 0; i < 16; ++i)
                    if (keys[i].OnTouch(*touch).clicked) {
                        auto& byte = passwords[password_index][cursor / 2];
                        byte = cursor % 2 == 0 ? (byte & 15U) | (i << 4) : (byte & 0xF0U) | i;
                        cursor = (cursor + 1) % 16;
                    }
                if (backspace.OnTouch(*touch).clicked) cursor = (cursor + 15) % 16;
                if (done.OnTouch(*touch).clicked) {
                    editing = false;
                    password_view.SetVisible(false);
                    main_view.SetVisible(true);
                }
                update_password();
            } else if (!pending_scan && !pending_read) {
                if (read.OnTouch(*touch).clicked) queue_read(true);
                if (previous.OnTouch(*touch).clicked && selected && offset >= stride) {
                    offset -= stride;
                    queue_read(false);
                }
                if (next.OnTouch(*touch).clicked && selected && offset + stride < limit) {
                    offset += stride;
                    queue_read(false);
                }
                if (edit.OnTouch(*touch).clicked) {
                    clear_data();
                    status_label.SetText("密码已进入编辑，完成后请重新读取");
                    editing = true;
                    update_password();
                    main_view.SetVisible(false);
                    password_view.SetVisible(true);
                }
            }
        }
        if (dirty) present();
    });
    return 0;
}
