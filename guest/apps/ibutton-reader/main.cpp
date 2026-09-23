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
            return "Read complete";
        case IButtonStatus::kNoDevice:
            return "No device or device changed; scan again";
        case IButtonStatus::kMultipleDevices:
            return "Connect one iButton only";
        case IButtonStatus::kUnsupported:
            return "Only DS1977 / DS1991 supported";
        case IButtonStatus::kBusError:
            return "Bus error: check wiring, DS2484 and GPIO";
        case IButtonStatus::kCrcError:
            return "CRC error: check password and contact";
        default:
            return "Invalid read range";
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
    label(main_view, 20, "iButton | DS1977 / DS1991", SystemFont::kLarge);
    auto id_label = label(main_view, 70, "ID: Not scanned");
    auto page_label = label(main_view, 113, "Tap Read to identify and read page 1");
    auto status_label = label(main_view, 154, "Set the 8-byte access password first", SystemFont::kSmall);
    std::array<LabelNode, 8> rows;
    for (unsigned i = 0; i < rows.size(); ++i) rows[i] = label(main_view, 202 + i * 34, " ", SystemFont::kSmall);
    auto warning = label(main_view, 484, " ", SystemFont::kSmall);
    auto read = button(main_view, {24, 575, 210, 60}, "Read");
    auto previous = button(main_view, {254, 575, 210, 60}, "Previous");
    auto next = button(main_view, {484, 575, 210, 60}, "Next");
    auto edit = button(main_view, {24, 648, 670, 54}, "Set Access Password");
    label(password_view, 22, "Access Password (16 hex digits)", SystemFont::kLarge);
    label(password_view, 76, "DS1977: password 1 | DS1991: passwords 1 / 2 / 3", SystemFont::kSmall);
    auto password_label = label(password_view, 128, " ", SystemFont::kLarge);
    auto cursor_label = label(password_view, 180, " ", SystemFont::kSmall);
    std::array<ui::TextButton, 3> password_tabs;
    constexpr const char* kPasswordTabLabels[]{"Password 1", "Password 2", "Password 3"};
    for (unsigned i = 0; i < 3; ++i)
        password_tabs[i] = button(password_view, {24 + static_cast<int>(i) * 230, 220, 210, 52}, kPasswordTabLabels[i]);
    std::array<ui::TextButton, 16> keys;
    for (unsigned i = 0; i < 16; ++i) {
        char text[]{kHex[i], 0};
        keys[i] = button(password_view,
                         {24 + static_cast<int>(i % 4) * 170, 290 + static_cast<int>(i / 4) * 65, 155, 55}, text);
    }
    auto backspace = button(password_view, {24, 565, 320, 55}, "Previous Digit");
    auto done = button(password_view, {374, 565, 320, 55}, "Done");
    label(password_view, 652, "RAM only; default FF may not be correct", SystemFont::kSmall);
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
        text.Append("Password ");
        text.AppendUint(password_index + 1);
        text.Append(" | next digit replaces #");
        text.AppendUint(cursor + 1);
        cursor_label.SetText(text.c_str());
    };
    auto queue_read = [&](bool scan) {
        clear_data();
        pending_scan = scan;
        pending_read = !scan;
        status_label.SetText("Reading...");
        warning.SetText(" ");
    };
    auto present = [&] {
        const auto result = app.renderer().Present(scene);
        if (!result) app.log().Error("iButton Reader: scene rendering failed");
        return static_cast<bool>(result);
    };
    auto timer = app.timers().Every(30_ms).value();
    present();
    app.Run([&](const Event& event) {
        bool dirty = false;
        if (event.TimerFrom(timer) && (pending_scan || pending_read)) {
            dirty = true;
            if (pending_scan) {
                pending_scan = false;
                selected = false;
                id_label.SetText("ID: Not identified");
                page_label.SetText(" ");
                auto result = app.ibutton().Scan();
                if (!result)
                    status_label.SetText("Host service unavailable or call failed");
                else if (result->status != IButtonStatus::kOk)
                    status_label.SetText(StatusText(result->status));
                else {
                    rom = result->rom;
                    char id[17];
                    Hex(id, rom.data(), 8);
                    FixedString<64> text;
                    text.Append("ID: ");
                    text.Append(id);
                    text.Append(" | ");
                    text.Append(rom[0] == 0x37 ? "DS1977" : rom[0] == 0x02 ? "DS1991" : "Unknown device");
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
                heading.Append("Address ");
                AppendHexWord(heading, offset);
                heading.Append("-");
                AppendHexWord(heading, offset + stride - 1);
                heading.Append(" | ");
                heading.AppendUint(offset / stride + 1);
                heading.Append(" / ");
                heading.AppendUint(limit / stride);
                heading.Append(" page");
                page_label.SetText(heading.c_str());
                if (!result)
                    status_label.SetText("Service call failed; try again");
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
                        warning.SetText(rom[0] == 0x02 ? "DS1991 has no data CRC; a wrong password may return false data"
                                                       : "DS1977 page CRC16 verified");
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
                    status_label.SetText("Password changed; tap Read to scan again");
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
