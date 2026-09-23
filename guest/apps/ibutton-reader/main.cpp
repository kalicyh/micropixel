#include <array>
#include <cstddef>
#include <cstdint>

#include "factory_catalog.generated.hpp"
#include "ibutton-reader_strings.hpp"
#include "key_algorithms.hpp"
#include "sdk/micropixel.hpp"
#include "sdk/scene.hpp"

using namespace micropixel;
using namespace micropixel::literals;
namespace {
using Strings = ibutton_reader_strings::Catalog;
using StringId = ibutton_reader_strings::Id;
using Password = std::array<uint8_t, 8>;
using Rom = std::array<uint8_t, 8>;
constexpr auto kInk = Color::Rgb(28, 31, 36);
constexpr auto kSecondary = Color::Rgb(111, 119, 130);
constexpr auto kBlue = Color::Rgb(0, 122, 255);
constexpr auto kGreen = Color::Rgb(52, 199, 89);
constexpr auto kGray = Color::Rgb(205, 210, 218);
constexpr auto kPale = Color::Rgb(241, 243, 247);
constexpr auto kWhite = Color::Rgb(255, 255, 255);
constexpr char kHex[] = "0123456789ABCDEF";

void Hex(char* output, const uint8_t* input, unsigned count) {
    for (unsigned i = 0; i < count; ++i) {
        output[i * 2] = kHex[input[i] >> 4];
        output[i * 2 + 1] = kHex[input[i] & 15U];
    }
    output[count * 2] = '\0';
}

enum class Screen { kHome, kPassword, kData, kPicker, kConfirm };
enum class Job { kIdle, kAuthAlpha, kAuthBeta, kAuthManual, kMatch, kWrite };
enum class KeyMode { kAlpha, kBeta, kManual };
}  // namespace

int main() {
    Application app;
    const auto strings = ibutton_reader_strings::ForLocale(app.localization().CurrentLocale());
    app.renderer().ConfigureDisplay({.logical_size = {720, 720}, .scale_mode = DisplayScaleMode::kAspectFit}).value();
    auto scene = app.renderer().CreateScene(kPale).value();
    auto home = scene.CreateContainer().value();
    auto password_view = scene.CreateContainer().value();
    auto data_view = scene.CreateContainer().value();
    auto picker_view = scene.CreateContainer().value();
    auto confirm_view = scene.CreateContainer().value();
    password_view.SetVisible(false);
    data_view.SetVisible(false);
    picker_view.SetVisible(false);
    confirm_view.SetVisible(false);
    (void)home.CreateRoundedRect({24, 112, 672, 454}, {.fill = kWhite, .radius = 34U});
    (void)password_view.CreateRoundedRect({24, 20, 672, 660}, {.fill = kWhite, .radius = 30U});
    (void)data_view.CreateRoundedRect({24, 96, 672, 468}, {.fill = kWhite, .radius = 30U});
    (void)confirm_view.CreateRoundedRect({24, 96, 672, 448}, {.fill = kWhite, .radius = 30U});

    auto label = [&](Container& parent, int x, int y, const char* text, Color color = kInk,
                     SystemFont font = SystemFont::kMedium) {
        return parent.CreateLabel({x, y}, text, color, font).value();
    };
    auto button = [&](Container& parent, Rect bounds, const char* text, Color background = kWhite,
                      Color foreground = kInk, uint32_t radius = 22U, SystemFont font = SystemFont::kMedium) {
        return parent.CreateTextButton({.bounds = bounds,
                                        .text = text,
                                        .style = {.background = background,
                                                  .text = foreground,
                                                  .feedback = kGray,
                                                  .font = font,
                                                  .corner_radius = radius}});
    };

    // Home: one unmistakable device affordance and a single path forward.
    label(home, 40, 28, strings.Get(StringId::kAppTitle), kInk, SystemFont::kLarge);
    auto home_status = label(home, 40, 88, strings.Get(StringId::kConnectionChecking), kSecondary);
    auto device_button = button(home, {235, 160, 250, 250}, strings.Get(StringId::kConnectionDisconnected), kGray,
                                kWhite, 125U, SystemFont::kLarge);
    auto device_name = label(home, 40, 438, strings.Get(StringId::kIdNotScanned), kSecondary);
    auto home_detail = label(home, 40, 478, strings.Get(StringId::kPasswordHint), kSecondary, SystemFont::kSmall);
    auto choose_button = button(home, {40, 610, 640, 72}, strings.Get(StringId::kButtonChooseData), kBlue, kWhite, 24U,
                                SystemFont::kLarge);
    choose_button.SetEnabled(false);

    // Manual password entry is only shown if neither built-in algorithm works.
    label(password_view, 40, 30, strings.Get(StringId::kPasswordTitle), kInk, SystemFont::kLarge);
    auto password_hint = label(password_view, 40, 92, strings.Get(StringId::kPasswordUsage), kSecondary);
    auto password_label = label(password_view, 40, 150, "FFFFFFFFFFFFFFFF", kInk, SystemFont::kLarge);
    auto cursor_label = label(password_view, 40, 200, strings.Get(StringId::kPasswordRamNote), kSecondary,
                              SystemFont::kSmall);
    std::array<ui::TextButton, 16> keys;
    for (unsigned i = 0; i < keys.size(); ++i) {
        char digit[]{kHex[i], '\0'};
        keys[i] = button(password_view, {40 + static_cast<int>(i % 4) * 160,
                                         270 + static_cast<int>(i / 4) * 66, 140, 54},
                         digit, kWhite, kInk, 16U, SystemFont::kLarge);
    }
    auto key_back = button(password_view, {40, 555, 300, 64}, strings.Get(StringId::kButtonPreviousDigit));
    auto key_done = button(password_view, {380, 555, 300, 64}, strings.Get(StringId::kButtonDone), kBlue, kWhite);
    auto key_cancel = button(password_view, {40, 635, 640, 44}, strings.Get(StringId::kFactoryBack), kPale, kSecondary,
                             16U, SystemFont::kSmall);

    // Data view contains only the identity, image match and next action.
    auto data_back = button(data_view, {32, 24, 140, 54}, strings.Get(StringId::kFactoryBack), kPale, kBlue, 18U);
    label(data_view, 40, 116, strings.Get(StringId::kDataTitle), kInk, SystemFont::kLarge);
    auto data_device = label(data_view, 40, 180, strings.Get(StringId::kIdNotScanned), kSecondary);
    label(data_view, 40, 272, strings.Get(StringId::kMatchCaption), kSecondary);
    auto match_name = label(data_view, 40, 325, strings.Get(StringId::kFactoryNoData), kInk, SystemFont::kLarge);
    auto data_status = label(data_view, 40, 405, strings.Get(StringId::kStatusReading), kSecondary);
    auto data_choose = button(data_view, {40, 610, 640, 72}, strings.Get(StringId::kButtonChooseData), kBlue, kWhite,
                              24U, SystemFont::kLarge);

    // Large, uncluttered image list on its own screen.
    auto picker_back = button(picker_view, {32, 24, 140, 54}, strings.Get(StringId::kFactoryBack), kPale, kBlue, 18U);
    label(picker_view, 40, 108, strings.Get(StringId::kPickerTitle), kInk, SystemFont::kLarge);
    auto picker_hint = label(picker_view, 40, 158, strings.Get(StringId::kMatchCaption), kSecondary,
                             SystemFont::kSmall);
    std::array<ui::TextButton, 5> dataset_buttons;
    for (unsigned i = 0; i < dataset_buttons.size(); ++i)
        dataset_buttons[i] = button(picker_view, {40, 206 + static_cast<int>(i) * 72, 640, 60}, "-", kWhite, kInk,
                                    18U, SystemFont::kMedium);
    auto picker_page = label(picker_view, 40, 584, "1 / 1", kSecondary, SystemFont::kSmall);
    auto page_previous = button(picker_view, {390, 570, 130, 54}, strings.Get(StringId::kButtonPrevious), kWhite, kBlue,
                                18U, SystemFont::kSmall);
    auto page_next = button(picker_view, {540, 570, 140, 54}, strings.Get(StringId::kButtonNext), kWhite, kBlue,
                            18U, SystemFont::kSmall);

    auto confirm_back = button(confirm_view, {32, 24, 140, 54}, strings.Get(StringId::kFactoryBack), kPale, kBlue, 18U);
    label(confirm_view, 40, 132, strings.Get(StringId::kConfirmTitle), kInk, SystemFont::kLarge);
    auto confirm_name = label(confirm_view, 40, 226, strings.Get(StringId::kFactoryNoData), kBlue, SystemFont::kLarge);
    label(confirm_view, 40, 310, strings.Get(StringId::kConfirmNote), kSecondary);
    auto confirm_status = label(confirm_view, 40, 390, " ", kSecondary);
    auto confirm_write = button(confirm_view, {40, 570, 640, 76}, strings.Get(StringId::kFactoryConfirm), kBlue, kWhite,
                                24U, SystemFont::kLarge);

    Screen screen = Screen::kHome;
    Job job = Job::kIdle;
    KeyMode key_mode = KeyMode::kManual;
    Rom rom{};
    Password manual_password{};
    Password active_password{};
    std::array<uint8_t, 4096> device_data{};
    std::array<bool, 128> written{};
    std::size_t dataset_index = 0, picker_page_index = 0;
    unsigned page_offset = 0, write_page = 0, cursor = 0;
    bool editing = false, device_present = false, authenticated = false, matching_complete = false;
    int matched_dataset = -1;

    auto set_screen = [&](Screen next) {
        screen = next;
        home.SetVisible(next == Screen::kHome);
        password_view.SetVisible(next == Screen::kPassword);
        data_view.SetVisible(next == Screen::kData);
        picker_view.SetVisible(next == Screen::kPicker);
        confirm_view.SetVisible(next == Screen::kConfirm);
    };
    auto update_key = [&] {
        char text[17];
        Hex(text, manual_password.data(), manual_password.size());
        password_label.SetText(text);
        FixedString<48> pos;
        pos.Append(strings.Get(StringId::kPasswordCursor));
        pos.AppendUint(cursor + 1U);
        cursor_label.SetText(pos.c_str());
    };
    auto start_matching = [&] {
        job = Job::kMatch;
        page_offset = 0;
        matching_complete = false;
        matched_dataset = -1;
        home_status.SetText(strings.Get(StringId::kStatusMatching));
        home_detail.SetText(strings.Get(StringId::kStatusMatching));
        data_status.SetText(strings.Get(StringId::kStatusMatching));
    };
    auto update_dataset_list = [&] {
        constexpr std::size_t per_page = 5;
        const auto count = ibutton_reader::kFactoryCatalog.size();
        const auto total_pages = (count + per_page - 1U) / per_page;
        for (std::size_t row = 0; row < dataset_buttons.size(); ++row) {
            const auto index = picker_page_index * per_page + row;
            if (index >= count) {
                dataset_buttons[row].SetVisible(false);
                continue;
            }
            dataset_buttons[row].SetVisible(true);
            const auto color = written[index] ? kGreen : (static_cast<int>(index) == matched_dataset ? kBlue : kWhite);
            const auto text_color = written[index] || static_cast<int>(index) == matched_dataset ? kWhite : kInk;
            FixedString<112> title;
            if (written[index]) title.Append("✓   ");
            else if (static_cast<int>(index) == matched_dataset) title.Append("●   ");
            title.Append(ibutton_reader::kFactoryCatalog[index].name);
            (void)dataset_buttons[row].SetText(title.c_str());
            (void)dataset_buttons[row].SetStyle({.background = color, .text = text_color, .feedback = kGray,
                                                  .font = SystemFont::kMedium, .corner_radius = 18U});
        }
        FixedString<32> page;
        page.AppendUint(picker_page_index + 1U);
        page.Append(" / ");
        page.AppendUint(total_pages == 0 ? 1U : total_pages);
        picker_page.SetText(page.c_str());
        picker_hint.SetText(matched_dataset >= 0 ? ibutton_reader::kFactoryCatalog[matched_dataset].name
                                                 : strings.Get(StringId::kMatchUnknown));
    };
    auto update_device_labels = [&] {
        char id[17];
        Hex(id, rom.data(), rom.size());
        FixedString<64> identity;
        identity.Append(strings.Get(StringId::kIdPrefix));
        identity.Append(id);
        device_name.SetText(identity.c_str());
        data_device.SetText(identity.c_str());
    };
    auto show_manual_key = [&] {
        job = Job::kIdle;
        key_mode = KeyMode::kManual;
        manual_password.fill(0xFF);
        cursor = 0;
        password_hint.SetText(strings.Get(StringId::kManualKeyHint));
        update_key();
        set_screen(Screen::kPassword);
    };
    auto present = [&] {
        const auto result = app.renderer().Present(scene);
        if (!result) app.log().Error("iButton Reader: rendering failed");
    };

    auto work_timer = app.timers().Every(30_ms).value();
    auto scan_timer = app.timers().Every(1_s).value();
    present();
    app.Run([&](const Event& event) {
        bool dirty = false;
        if (event.TimerFrom(scan_timer) && !editing && job == Job::kIdle) {
            const auto scan = app.ibutton().Scan();
            dirty = true;
            if (!scan || scan->status != IButtonStatus::kOk) {
                if (device_present) {
                    device_present = false;
                    authenticated = false;
                    matching_complete = false;
                    home_status.SetText(strings.Get(StringId::kConnectionDisconnected));
                    home_status.SetColor(kSecondary);
                    (void)device_button.SetStyle({.background = kGray, .text = kWhite, .feedback = kPale,
                                                  .font = SystemFont::kLarge, .corner_radius = 125U});
                    (void)device_button.SetText(strings.Get(StringId::kConnectionDisconnected));
                    home_detail.SetText(strings.Get(StringId::kPasswordHint));
                    choose_button.SetEnabled(false);
                    if (screen == Screen::kData || screen == Screen::kPicker || screen == Screen::kConfirm)
                        set_screen(Screen::kHome);
                }
            } else if (!device_present || scan->rom != rom) {
                device_present = true;
                rom = scan->rom;
                authenticated = false;
                matching_complete = false;
                update_device_labels();
                home_status.SetText(strings.Get(StringId::kConnectionConnected));
                home_status.SetColor(kGreen);
                (void)device_button.SetStyle({.background = kBlue, .text = kWhite, .feedback = kPale,
                                              .font = SystemFont::kLarge, .corner_radius = 125U});
                (void)device_button.SetText(strings.Get(StringId::kStatusReading));
                if (rom[0] == 0x37) {
                    key_mode = KeyMode::kAlpha;
                    active_password = ibutton_reader::Alpha(rom);
                    job = Job::kAuthAlpha;
                    home_detail.SetText(strings.Get(StringId::kStatusTryingA));
                } else {
                    home_detail.SetText(strings.Get(StringId::kStatusUnsupported));
                    (void)device_button.SetStyle({.background = kGray, .text = kWhite, .feedback = kPale,
                                                  .font = SystemFont::kLarge, .corner_radius = 125U});
                }
            }
        }
        if (event.TimerFrom(work_timer) && job != Job::kIdle) {
            dirty = true;
            if (job == Job::kAuthAlpha || job == Job::kAuthBeta || job == Job::kAuthManual) {
                const auto result = app.ibutton().Read(rom, 0, 64, active_password);
                if (result && result->status == IButtonStatus::kOk) {
                    authenticated = true;
                    start_matching();
                    set_screen(Screen::kHome);
                } else if (job == Job::kAuthAlpha) {
                    key_mode = KeyMode::kBeta;
                    active_password = ibutton_reader::Beta(rom);
                    job = Job::kAuthBeta;
                    home_detail.SetText(strings.Get(StringId::kStatusTryingB));
                } else if (job == Job::kAuthBeta) {
                    home_detail.SetText(strings.Get(StringId::kStatusManualKey));
                    show_manual_key();
                } else {
                    job = Job::kIdle;
                    password_hint.SetText(strings.Get(StringId::kStatusKeyRejected));
                    set_screen(Screen::kPassword);
                }
            } else if (job == Job::kMatch) {
                const auto result = app.ibutton().Read(rom, static_cast<uint16_t>(page_offset), 64, active_password);
                if (!result || result->status != IButtonStatus::kOk) {
                    job = Job::kIdle;
                    matching_complete = false;
                    home_detail.SetText(strings.Get(StringId::kStatusMatchFailed));
                    data_status.SetText(strings.Get(StringId::kStatusMatchFailed));
                } else {
                    for (unsigned i = 0; i < 64; ++i) device_data[page_offset + i] = result->data[i];
                    page_offset += 64;
                    if (page_offset >= device_data.size()) {
                        job = Job::kIdle;
                        matching_complete = true;
                        matched_dataset = -1;
                        for (std::size_t i = 0; i < ibutton_reader::kFactoryCatalog.size(); ++i) {
                            bool equal = true;
                            for (std::size_t j = 0; j < device_data.size(); ++j)
                                if (device_data[j] != ibutton_reader::kFactoryCatalog[i].bytes[j]) {
                                    equal = false;
                                    break;
                                }
                            if (equal) { matched_dataset = static_cast<int>(i); break; }
                        }
                        const char* matched = matched_dataset >= 0 ? ibutton_reader::kFactoryCatalog[matched_dataset].name
                                                                    : strings.Get(StringId::kMatchUnknown);
                        match_name.SetText(matched);
                        data_status.SetText(strings.Get(StringId::kConnectionConnected));
                        home_detail.SetText(matched_dataset >= 0 ? strings.Get(StringId::kMatchFound)
                                                                 : strings.Get(StringId::kMatchUnknown));
                        (void)device_button.SetText(strings.Get(StringId::kStatusReady));
                        choose_button.SetEnabled(true);
                        update_dataset_list();
                        set_screen(Screen::kData);
                    } else {
                        FixedString<48> progress;
                        progress.Append(strings.Get(StringId::kStatusMatching));
                        progress.Append(" ");
                        progress.AppendUint(page_offset / 64U);
                        progress.Append(" / 64");
                        home_detail.SetText(progress.c_str());
                        data_status.SetText(progress.c_str());
                    }
                }
            } else if (job == Job::kWrite) {
                const auto& dataset = ibutton_reader::kFactoryCatalog[dataset_index];
                std::array<uint8_t, 64> page{};
                for (unsigned i = 0; i < page.size(); ++i) page[i] = dataset.bytes[write_page * 64U + i];
                const auto result = app.ibutton().Write(rom, static_cast<uint16_t>(write_page * 64U), page, active_password);
                if (!result || result->status != IButtonStatus::kOk) {
                    job = Job::kIdle;
                    confirm_status.SetText(strings.Get(StringId::kFactoryFailed));
                } else if (++write_page >= 64U) {
                    job = Job::kIdle;
                    written[dataset_index] = true;
                    confirm_status.SetText(strings.Get(StringId::kFactoryComplete));
                    data_status.SetText(strings.Get(StringId::kFactoryComplete));
                    update_dataset_list();
                    picker_hint.SetText(strings.Get(StringId::kFactoryComplete));
                    set_screen(Screen::kPicker);
                    app.log().Info("iButton Reader: factory write and readback complete");
                } else {
                    FixedString<48> progress;
                    progress.Append(strings.Get(StringId::kFactoryProgress));
                    progress.AppendUint(write_page);
                    progress.Append(strings.Get(StringId::kFactoryOf));
                    confirm_status.SetText(progress.c_str());
                }
            }
        }
        if (const auto* touch = event.touch()) {
            dirty = true;
            if (screen == Screen::kPassword) {
                for (unsigned i = 0; i < keys.size(); ++i)
                    if (keys[i].OnTouch(*touch).clicked) {
                        auto& byte = manual_password[cursor / 2U];
                        byte = cursor % 2U == 0 ? static_cast<uint8_t>((byte & 0x0FU) | (i << 4U))
                                                : static_cast<uint8_t>((byte & 0xF0U) | i);
                        cursor = (cursor + 1U) % 16U;
                    }
                if (key_back.OnTouch(*touch).clicked) cursor = (cursor + 15U) % 16U;
                if (key_done.OnTouch(*touch).clicked && device_present) {
                    active_password = manual_password;
                    key_mode = KeyMode::kManual;
                    job = Job::kAuthManual;
                    password_hint.SetText(strings.Get(StringId::kStatusReading));
                    set_screen(Screen::kHome);
                }
                if (key_cancel.OnTouch(*touch).clicked) set_screen(Screen::kHome);
                update_key();
            } else if (screen == Screen::kHome) {
                if (device_button.OnTouch(*touch).clicked && job == Job::kIdle) {
                    const auto scan = app.ibutton().Scan();
                    if (scan && scan->status == IButtonStatus::kOk && scan->rom[0] == 0x37) {
                        rom = scan->rom;
                        device_present = true;
                        update_device_labels();
                        key_mode = KeyMode::kAlpha;
                        active_password = ibutton_reader::Alpha(rom);
                        job = Job::kAuthAlpha;
                        home_detail.SetText(strings.Get(StringId::kStatusTryingA));
                    }
                }
                if (choose_button.OnTouch(*touch).clicked && authenticated && matching_complete) {
                    picker_page_index = 0;
                    update_dataset_list();
                    set_screen(Screen::kPicker);
                }
            } else if (screen == Screen::kData) {
                if (data_back.OnTouch(*touch).clicked) set_screen(Screen::kHome);
                if (data_choose.OnTouch(*touch).clicked && authenticated && matching_complete) {
                    picker_page_index = 0;
                    update_dataset_list();
                    set_screen(Screen::kPicker);
                }
            } else if (screen == Screen::kPicker && job == Job::kIdle) {
                if (picker_back.OnTouch(*touch).clicked) set_screen(Screen::kData);
                for (std::size_t i = 0; i < dataset_buttons.size(); ++i) {
                    const auto index = picker_page_index * dataset_buttons.size() + i;
                    if (index < ibutton_reader::kFactoryCatalog.size() && dataset_buttons[i].OnTouch(*touch).clicked) {
                        dataset_index = index;
                        confirm_name.SetText(ibutton_reader::kFactoryCatalog[index].name);
                        confirm_status.SetText(" ");
                        set_screen(Screen::kConfirm);
                    }
                }
                const auto page_count = (ibutton_reader::kFactoryCatalog.size() + dataset_buttons.size() - 1U) /
                                        dataset_buttons.size();
                if (page_previous.OnTouch(*touch).clicked && picker_page_index > 0) {
                    --picker_page_index;
                    update_dataset_list();
                }
                if (page_next.OnTouch(*touch).clicked && picker_page_index + 1U < page_count) {
                    ++picker_page_index;
                    update_dataset_list();
                }
            } else if (screen == Screen::kConfirm) {
                if (confirm_back.OnTouch(*touch).clicked && job == Job::kIdle) set_screen(Screen::kPicker);
                if (confirm_write.OnTouch(*touch).clicked && job == Job::kIdle && device_present && authenticated) {
                    // Refresh identity before the destructive operation.
                    const auto scan = app.ibutton().Scan();
                    if (scan && scan->status == IButtonStatus::kOk && scan->rom == rom) {
                        write_page = 0;
                        job = Job::kWrite;
                        confirm_status.SetText(strings.Get(StringId::kStatusWriting));
                    } else {
                        confirm_status.SetText(strings.Get(StringId::kFactoryTargetRequired));
                    }
                }
            }
        }
        if (dirty) present();
    });
    return 0;
}
