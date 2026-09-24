#include <array>
#include <algorithm>
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
constexpr auto kReadingCard = Color::Rgb(225, 239, 255);
constexpr auto kErrorCard = Color::Rgb(255, 235, 233);
constexpr auto kWhite = Color::Rgb(255, 255, 255);
constexpr char kHex[] = "0123456789ABCDEF";
constexpr unsigned kDefaultReadBytes = 0x0098U;
constexpr unsigned kFourKilobytes = 4096U;
constexpr unsigned kFullDeviceBytes = 32768U;
constexpr unsigned kDataBytesPerRow = 12U;
constexpr unsigned kDataRowsPerPage = 8U;
constexpr unsigned kDataBytesPerPage = kDataBytesPerRow * kDataRowsPerPage;
constexpr uint8_t kDs1977FamilyCode = 0x37U;
constexpr uint8_t kDs1991FamilyCode = 0x02U;
constexpr unsigned kDs1991DataBytes = 144U;
constexpr unsigned kDs1991PageBytes = 48U;
constexpr unsigned kFactoryImageBytes = 4096U;
constexpr unsigned kFullWritePages = 64U;

void Hex(char* output, const uint8_t* input, unsigned count) {
    for (unsigned i = 0; i < count; ++i) {
        output[i * 2] = kHex[input[i] >> 4];
        output[i * 2 + 1] = kHex[input[i] & 15U];
    }
    output[count * 2] = '\0';
}

template <uint32_t Capacity>
void AppendHexByte(FixedString<Capacity>& output, uint8_t value) {
    const char bytes[]{kHex[value >> 4], kHex[value & 15U], '\0'};
    output.Append(bytes);
}

template <uint32_t Capacity>
void AppendHexWord(FixedString<Capacity>& output, uint16_t value) {
    AppendHexByte(output, static_cast<uint8_t>(value >> 8));
    AppendHexByte(output, static_cast<uint8_t>(value));
}

enum class Screen { kHome, kPassword, kData, kPicker, kConfirm, kFullReadPrompt, kDs1991AuthChoice };
enum class Job { kIdle, kAuthAlpha, kAuthBeta, kAuthManual, kAuthDs1991Alpha, kAuthDs1991Beta, kRead, kMatch, kWrite };
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
    auto full_read_prompt_view = scene.CreateContainer().value();
    auto ds1991_auth_view = scene.CreateContainer().value();
    password_view.SetVisible(false);
    data_view.SetVisible(false);
    picker_view.SetVisible(false);
    confirm_view.SetVisible(false);
    full_read_prompt_view.SetVisible(false);
    ds1991_auth_view.SetVisible(false);

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

    // Home is a status card. Its surface changes color while the device is being read.
    label(home, 40, 28, strings.Get(StringId::kAppTitle), kInk, SystemFont::kLarge);
    auto home_card = button(home, {24, 112, 672, 454}, strings.Get(StringId::kConnectionChecking), kGray, kInk, 34U,
                            SystemFont::kLarge);
    home_card.SetEnabled(false);
    auto device_name = label(home, 48, 514, strings.Get(StringId::kIdNotScanned), kSecondary,
                             SystemFont::kMedium);
    auto home_detail = label(home, 64, 405, strings.Get(StringId::kPasswordHint), kSecondary, SystemFont::kMedium);
    home_detail.SetPosition({530, 514});
    home_detail.SetCentered(true);
    home_detail.SetFont(SystemFont::kSmall);
    auto full_read_home_button = button(home, {40, 610, 640, 72}, strings.Get(StringId::kButtonFullRead), kWhite, kBlue,
                                        22U, SystemFont::kMedium);
    full_read_home_button.SetEnabled(false);

    // Manual key entry is only shown if neither built-in algorithm authenticates.
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
    auto key_cancel = button(password_view, {40, 635, 640, 44}, strings.Get(StringId::kButtonBackArrow), kPale,
                             kBlue, 16U, SystemFont::kSmall);

    // Data view: identity, optional similarity match, and the selected 96-byte display page.
    auto data_back = button(data_view, {12, 24, 160, 54}, strings.Get(StringId::kButtonBackArrow), kPale, kBlue, 18U);
    auto data_title = data_view.CreateLabel({360, 30}, "—", kInk, SystemFont::kLarge, true).value();
    label(data_view, 40, 444, strings.Get(StringId::kMatchCaption), kSecondary, SystemFont::kSmall);
    auto match_name = label(data_view, 40, 468, "—", kInk, SystemFont::kLarge);
    std::array<LabelNode, kDataRowsPerPage> data_rows;
    for (unsigned i = 0; i < data_rows.size(); ++i)
        data_rows[i] = label(data_view, 40, 120 + static_cast<int>(i) * 42, " ", kInk, SystemFont::kLarge);
    auto data_page = data_view.CreateLabel({625, 456}, "1 / 64", kSecondary, SystemFont::kSmall, true).value();
    auto data_previous = button(data_view, {390, 488, 130, 46}, strings.Get(StringId::kButtonPrevious), kPale, kBlue,
                                16U, SystemFont::kSmall);
    auto data_next = button(data_view, {540, 488, 140, 46}, strings.Get(StringId::kButtonNext), kPale, kBlue, 16U,
                            SystemFont::kSmall);
    auto data_status = label(data_view, 40, 540, strings.Get(StringId::kStatusReading), kSecondary,
                             SystemFont::kSmall);
    auto match_button = button(data_view, {40, 610, 300, 72}, strings.Get(StringId::kButtonMatchImage), kWhite, kBlue,
                               22U, SystemFont::kMedium);
    auto data_choose = button(data_view, {360, 610, 320, 72}, strings.Get(StringId::kButtonChooseData), kBlue, kWhite,
                              22U, SystemFont::kMedium);

    // The catalog remains a separate page with large tap targets.
    auto picker_back = button(picker_view, {12, 24, 160, 54}, strings.Get(StringId::kButtonBackArrow), kPale, kBlue,
                              18U);
    (void)picker_view.CreateLabel({360, 30}, strings.Get(StringId::kPickerTitle), kInk, SystemFont::kLarge, true);
    auto picker_hint = label(picker_view, 40, 96, strings.Get(StringId::kMatchTapHint), kSecondary,
                             SystemFont::kSmall);
    std::array<ui::TextButton, 5> dataset_buttons;
    for (unsigned i = 0; i < dataset_buttons.size(); ++i)
        dataset_buttons[i] = button(picker_view, {40, 138 + static_cast<int>(i) * 72, 640, 60}, "-", kWhite, kInk,
                                    18U, SystemFont::kMedium);
    auto picker_page = label(picker_view, 40, 602, "1 / 1", kSecondary, SystemFont::kSmall);
    auto page_previous = button(picker_view, {390, 638, 130, 54}, strings.Get(StringId::kButtonPrevious), kWhite,
                                kBlue, 18U, SystemFont::kSmall);
    auto page_next = button(picker_view, {540, 638, 140, 54}, strings.Get(StringId::kButtonNext), kWhite, kBlue, 18U,
                            SystemFont::kSmall);

    auto confirm_back = button(confirm_view, {32, 24, 160, 54}, strings.Get(StringId::kButtonBackArrow), kPale,
                               kBlue, 18U);
    label(confirm_view, 40, 132, strings.Get(StringId::kConfirmTitle), kInk, SystemFont::kLarge);
    auto confirm_name = label(confirm_view, 40, 226, strings.Get(StringId::kFactoryNoData), kBlue, SystemFont::kLarge);
    label(confirm_view, 40, 310, strings.Get(StringId::kConfirmNote), kSecondary);
    auto confirm_status = label(confirm_view, 40, 390, " ", kSecondary);
    auto confirm_full_write = button(confirm_view, {40, 570, 640, 76}, strings.Get(StringId::kFactoryFullWrite),
                                     kBlue, kWhite, 24U, SystemFont::kLarge);

    (void)full_read_prompt_view.CreateShape({0, 0, 720, 720}, kInk, 128U);
    (void)full_read_prompt_view.CreateRoundedRect({52, 218, 616, 284}, {.fill = kWhite, .radius = 30U});
    label(full_read_prompt_view, 88, 252, strings.Get(StringId::kFullReadConfirmTitle), kInk, SystemFont::kLarge);
    label(full_read_prompt_view, 88, 310, strings.Get(StringId::kFullReadConfirmNote), kSecondary);
    auto full_read_4kb = button(full_read_prompt_view, {72, 408, 176, 62}, strings.Get(StringId::kButtonRead4kb),
                                kWhite, kBlue, 18U);
    auto full_read_all = button(full_read_prompt_view, {272, 408, 176, 62}, strings.Get(StringId::kButtonReadAll),
                                kBlue, kWhite, 18U);
    auto full_read_cancel = button(full_read_prompt_view, {472, 408, 176, 62}, strings.Get(StringId::kButtonCancel),
                                   kPale, kBlue, 18U);

    (void)ds1991_auth_view.CreateShape({0, 0, 720, 720}, kInk, 128U);
    (void)ds1991_auth_view.CreateRoundedRect({54, 165, 612, 420}, {.fill = kWhite, .radius = 30U});
    label(ds1991_auth_view, 90, 202, strings.Get(StringId::kAuthChoiceTitle), kInk, SystemFont::kLarge);
    label(ds1991_auth_view, 90, 264, strings.Get(StringId::kWarningDs1991), kSecondary, SystemFont::kSmall);
    auto auth_alpha = button(ds1991_auth_view, {76, 354, 176, 76}, strings.Get(StringId::kButtonAlgorithmAlpha),
                             kWhite, kBlue, 20U, SystemFont::kMedium);
    auto auth_beta = button(ds1991_auth_view, {272, 354, 176, 76}, strings.Get(StringId::kButtonAlgorithmBeta),
                            kWhite, kBlue, 20U, SystemFont::kMedium);
    auto auth_manual = button(ds1991_auth_view, {468, 354, 176, 76}, strings.Get(StringId::kButtonPasswordManual),
                              kBlue, kWhite, 20U, SystemFont::kMedium);
    auto auth_cancel = button(ds1991_auth_view, {250, 480, 220, 60}, strings.Get(StringId::kButtonCancel),
                              kPale, kBlue, 18U);

    Screen screen = Screen::kHome;
    Job job = Job::kIdle;
    Rom rom{};
    Password manual_password{};
    Password active_password{};
    static std::array<uint8_t, kFullDeviceBytes> device_data{};
    std::size_t dataset_index = 0, picker_page_index = 0;
    FixedString<112> match_summary;
    int last_written_dataset = -1;
    int matched_dataset = -1;
    unsigned page_offset = 0, read_limit = kDefaultReadBytes, loaded_bytes = 0;
    unsigned display_page = 0, write_page = 0, cursor = 0;
    unsigned match_index = 0, best_match_bytes = 0;
    bool editing = false, device_present = false, authenticated = false;
    auto device_capacity = [&] { return rom[0] == kDs1991FamilyCode ? kDs1991DataBytes : kFullDeviceBytes; };
    auto quick_read_bytes = [&] { return std::min(kDefaultReadBytes, device_capacity()); };
    auto show_home_hint = [&](StringId id) {
        home_detail.SetPosition({530, 514});
        home_detail.SetCentered(true);
        home_detail.SetFont(SystemFont::kSmall);
        home_detail.SetText(strings.Get(id));
    };

    auto set_screen = [&](Screen next) {
        if (screen == Screen::kPicker && next != Screen::kPicker) last_written_dataset = -1;
        screen = next;
        home.SetVisible(next == Screen::kHome || next == Screen::kFullReadPrompt ||
                        next == Screen::kDs1991AuthChoice);
        password_view.SetVisible(next == Screen::kPassword);
        data_view.SetVisible(next == Screen::kData);
        picker_view.SetVisible(next == Screen::kPicker);
        confirm_view.SetVisible(next == Screen::kConfirm);
        full_read_prompt_view.SetVisible(next == Screen::kFullReadPrompt);
        ds1991_auth_view.SetVisible(next == Screen::kDs1991AuthChoice);
    };
    auto update_key = [&] {
        char text[17];
        Hex(text, manual_password.data(), manual_password.size());
        password_label.SetText(text);
        FixedString<48> position;
        position.Append(strings.Get(StringId::kPasswordCursor));
        position.AppendUint(cursor + 1U);
        cursor_label.SetText(position.c_str());
    };
    auto update_device_labels = [&] {
        char id[17];
        Hex(id, rom.data(), rom.size());
        FixedString<64> identity;
        identity.Append(strings.Get(StringId::kIdPrefix));
        identity.Append(id);
        device_name.SetText(identity.c_str());
        data_title.SetText(id);
    };
    auto update_data_rows = [&] {
        for (unsigned row = 0; row < data_rows.size(); ++row) {
            const auto offset = static_cast<uint16_t>(display_page * kDataBytesPerPage + row * kDataBytesPerRow);
            if (offset >= loaded_bytes) {
                data_rows[row].SetText(" ");
                continue;
            }
            FixedString<64> line;
            AppendHexWord(line, offset);
            line.Append("  ");
            const auto row_bytes = std::min(kDataBytesPerRow, loaded_bytes - offset);
            for (unsigned column = 0; column < row_bytes; ++column) {
                AppendHexByte(line, device_data[offset + column]);
                if (column + 1U != row_bytes) line.Append(" ");
            }
            data_rows[row].SetText(line.c_str());
        }
        FixedString<24> page;
        const unsigned page_count = (loaded_bytes + kDataBytesPerPage - 1U) / kDataBytesPerPage;
        page.AppendUint(display_page + 1U);
        page.Append(" / ");
        page.AppendUint(page_count == 0U ? 1U : page_count);
        data_page.SetText(page.c_str());
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
            const bool recently_written = static_cast<int>(index) == last_written_dataset;
            const auto color = recently_written ? kGreen : kWhite;
            const auto text_color = recently_written ? kWhite : kInk;
            FixedString<112> title;
            if (recently_written) title.Append("✓   ");
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
        picker_hint.SetText(matched_dataset >= 0 ? match_summary.c_str() : strings.Get(StringId::kMatchTapHint));
    };
    auto start_read = [&](unsigned target_bytes, bool first_page_cached = false, bool resume_existing = false) {
        if (resume_existing && loaded_bytes >= target_bytes) return false;
        job = Job::kRead;
        home_card.SetEnabled(false);
        full_read_home_button.SetEnabled(false);
        read_limit = target_bytes;
        if (first_page_cached) {
            page_offset = loaded_bytes;
        } else if (resume_existing) {
            page_offset = loaded_bytes;
        } else {
            loaded_bytes = 0;
            page_offset = 0;
        }
        matched_dataset = -1;
        match_name.SetText("—");
        (void)home_card.SetStyle({.background = kReadingCard, .text = kBlue, .feedback = kGray,
                                  .font = SystemFont::kLarge, .corner_radius = 34U});
        const auto reading_label = target_bytes == quick_read_bytes() ? StringId::kStatusQuickReading
                                   : target_bytes == kFourKilobytes ? StringId::kStatusReading4kb
                                                                    : StringId::kStatusReadingFull;
        (void)home_card.SetText(" ");
        home_detail.SetPosition({530, 514});
        home_detail.SetCentered(true);
        home_detail.SetFont(SystemFont::kMedium);
        home_detail.SetText(strings.Get(reading_label));
        data_status.SetText(strings.Get(reading_label));
        return true;
    };
    auto show_manual_key = [&] {
        job = Job::kIdle;
        manual_password.fill(0xFF);
        cursor = 0;
        password_hint.SetText(strings.Get(StringId::kManualKeyHint));
        update_key();
        set_screen(Screen::kPassword);
    };
    auto show_ds1991_auth_choice = [&] {
        job = Job::kIdle;
        set_screen(Screen::kDs1991AuthChoice);
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
                    loaded_bytes = 0;
                    (void)home_card.SetStyle({.background = kGray, .text = kSecondary, .feedback = kPale,
                                              .font = SystemFont::kLarge, .corner_radius = 34U});
                    (void)home_card.SetText(strings.Get(StringId::kConnectionDisconnected));
                    home_card.SetEnabled(false);
                    full_read_home_button.SetEnabled(false);
                    full_read_home_button.SetVisible(true);
                    show_home_hint(StringId::kPasswordHint);
                    if (screen == Screen::kData || screen == Screen::kPicker || screen == Screen::kConfirm ||
                        screen == Screen::kPassword || screen == Screen::kDs1991AuthChoice)
                        set_screen(Screen::kHome);
                }
            } else if (!device_present || scan->rom != rom) {
                device_present = true;
                rom = scan->rom;
                authenticated = false;
                loaded_bytes = 0;
                update_device_labels();
                full_read_home_button.SetVisible(rom[0] != kDs1991FamilyCode);
                (void)home_card.SetStyle({.background = kReadingCard, .text = kBlue, .feedback = kGray,
                                          .font = SystemFont::kLarge, .corner_radius = 34U});
                (void)home_card.SetText(strings.Get(StringId::kStatusReading));
                home_card.SetEnabled(false);
                if (rom[0] == kDs1977FamilyCode) {
                    (void)data_choose.SetText(strings.Get(StringId::kButtonChooseData));
                    active_password = ibutton_reader::Alpha(rom);
                    job = Job::kAuthAlpha;
                    home_detail.SetText(strings.Get(StringId::kStatusTryingA));
                } else if (rom[0] == kDs1991FamilyCode) {
                    (void)home_card.SetText(strings.Get(StringId::kConnectionConnected));
                    show_home_hint(StringId::kPasswordHint);
                    full_read_home_button.SetVisible(false);
                    home_card.SetEnabled(true);
                    (void)data_choose.SetText(strings.Get(StringId::kButtonChooseData));
                    data_choose.SetEnabled(false);
                    show_ds1991_auth_choice();
                } else {
                    (void)data_choose.SetText(strings.Get(StringId::kButtonChooseData));
                    (void)home_card.SetStyle({.background = kGray, .text = kSecondary, .feedback = kPale,
                                              .font = SystemFont::kLarge, .corner_radius = 34U});
                    (void)home_card.SetText(strings.Get(StringId::kConnectionConnected));
                    home_card.SetEnabled(false);
                    show_home_hint(StringId::kStatusUnsupported);
                }
            }
        }
        if (event.TimerFrom(work_timer) && job != Job::kIdle) {
            dirty = true;
            if (job == Job::kAuthAlpha || job == Job::kAuthBeta || job == Job::kAuthManual ||
                job == Job::kAuthDs1991Alpha || job == Job::kAuthDs1991Beta) {
                const unsigned auth_bytes = rom[0] == kDs1991FamilyCode ? kDs1991PageBytes : 64U;
                const auto result = app.ibutton().Read(rom, 0, static_cast<uint16_t>(auth_bytes), active_password);
                if (result && result->status == IButtonStatus::kOk) {
                    authenticated = true;
                    for (unsigned i = 0; i < auth_bytes; ++i) device_data[i] = result->data[i];
                    loaded_bytes = auth_bytes;
                    if (rom[0] == kDs1991FamilyCode) {
                        data_status.SetText(strings.Get(StringId::kWarningDs1991));
                        (void)data_choose.SetText(strings.Get(StringId::kButtonChooseData));
                    }
                    data_choose.SetEnabled(true);
                    start_read(quick_read_bytes(), true);
                    set_screen(Screen::kHome);
                } else if (rom[0] == kDs1991FamilyCode) {
                    job = Job::kIdle;
                    password_hint.SetText(strings.Get(StringId::kStatusReadFailed));
                    show_ds1991_auth_choice();
                } else if (job == Job::kAuthAlpha) {
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
            } else if (job == Job::kRead) {
                const unsigned device_page_bytes = rom[0] == kDs1991FamilyCode ? kDs1991PageBytes : 64U;
                const unsigned chunk = std::min({64U - page_offset % 64U,
                                                 device_page_bytes - page_offset % device_page_bytes,
                                                 read_limit - page_offset});
                const auto result = app.ibutton().Read(rom, static_cast<uint16_t>(page_offset),
                                                      static_cast<uint16_t>(chunk), active_password);
                if (!result || result->status != IButtonStatus::kOk) {
                    job = Job::kIdle;
                    (void)home_card.SetStyle({.background = kErrorCard, .text = kInk, .feedback = kGray,
                                              .font = SystemFont::kLarge, .corner_radius = 34U});
                    (void)home_card.SetText(strings.Get(StringId::kStatusReadFailed));
                    home_card.SetEnabled(authenticated);
                    home_detail.SetText(strings.Get(StringId::kStatusReadFailed));
                    data_status.SetText(strings.Get(StringId::kStatusReadFailed));
                    if (loaded_bytes != 0U) {
                        display_page = 0;
                        update_data_rows();
                        full_read_home_button.SetEnabled(rom[0] != kDs1991FamilyCode &&
                                                         loaded_bytes < device_capacity());
                        set_screen(Screen::kData);
                    }
                } else {
                    for (unsigned i = 0; i < chunk; ++i) device_data[page_offset + i] = result->data[i];
                    page_offset += chunk;
                    loaded_bytes = page_offset;
                    FixedString<48> progress;
                    const auto progress_label = read_limit == quick_read_bytes() ? StringId::kStatusQuickReading
                                                : read_limit == kFourKilobytes ? StringId::kStatusReading4kb
                                                                              : StringId::kStatusReadingFull;
                    progress.Append(strings.Get(progress_label));
                    progress.Append(" ");
                    progress.AppendUint((page_offset + 63U) / 64U);
                    progress.Append(" / ");
                    progress.AppendUint((read_limit + 63U) / 64U);
                    home_detail.SetText(progress.c_str());
                    if (page_offset >= read_limit) {
                        job = Job::kIdle;
                        display_page = 0;
                        update_data_rows();
                        (void)home_card.SetStyle({.background = kWhite, .text = kGreen, .feedback = kGray,
                                                  .font = SystemFont::kTitle, .corner_radius = 34U});
                        (void)home_card.SetText(strings.Get(StringId::kStatusQuickReadPrompt));
                        home_card.SetEnabled(authenticated);
                        home_detail.SetText(" ");
                        home_detail.SetPosition({64, 405});
                        home_detail.SetCentered(false);
                        home_detail.SetFont(SystemFont::kMedium);
                        data_status.SetText(" ");
                        match_button.SetEnabled(loaded_bytes != 0U);
                        full_read_home_button.SetVisible(rom[0] != kDs1991FamilyCode);
                        full_read_home_button.SetEnabled(rom[0] != kDs1991FamilyCode &&
                                                         loaded_bytes < device_capacity());
                        data_choose.SetEnabled(authenticated);
                        set_screen(Screen::kData);
                    } else {
                        data_status.SetText(progress.c_str());
                    }
                }
            } else if (job == Job::kMatch) {
                const auto& dataset = ibutton_reader::kFactoryCatalog[match_index];
                unsigned identical = 0;
                const auto compare_bytes = std::min<std::size_t>(loaded_bytes, kFactoryImageBytes);
                for (std::size_t i = 0; i < compare_bytes; ++i)
                    if (device_data[i] == dataset.bytes[i]) ++identical;
                if (identical > best_match_bytes || match_index == 0U) {
                    best_match_bytes = identical;
                    matched_dataset = static_cast<int>(match_index);
                }
                ++match_index;
                if (match_index >= ibutton_reader::kFactoryCatalog.size()) {
                    job = Job::kIdle;
                    const auto compare_bytes = std::min<std::size_t>(loaded_bytes, kFactoryImageBytes);
                    const unsigned percent = compare_bytes == 0U ? 0U : best_match_bytes * 100U / compare_bytes;
                    match_summary.Clear();
                    match_summary.Append(ibutton_reader::kFactoryCatalog[matched_dataset].name);
                    match_summary.Append(" · ");
                    match_summary.AppendUint(percent);
                    match_summary.Append("%");
                    match_name.SetText(match_summary.c_str());
                    picker_hint.SetText(match_summary.c_str());
                    data_status.SetText(" ");
                } else {
                    FixedString<48> progress;
                    progress.Append(strings.Get(StringId::kStatusMatching));
                    progress.Append(" ");
                    progress.AppendUint(match_index);
                    progress.Append(" / ");
                    progress.AppendUint(ibutton_reader::kFactoryCatalog.size());
                    data_status.SetText(progress.c_str());
                }
            } else if (job == Job::kWrite) {
                const auto& dataset = ibutton_reader::kFactoryCatalog[dataset_index];
                std::array<uint8_t, 64> page{};
                for (unsigned i = 0; i < page.size(); ++i) page[i] = dataset.bytes[write_page * 64U + i];
                const auto result = app.ibutton().Write(rom, static_cast<uint16_t>(write_page * 64U), page, active_password);
                if (!result || result->status != IButtonStatus::kOk) {
                    job = Job::kIdle;
                    confirm_status.SetText(strings.Get(StringId::kFactoryFailed));
                } else if (++write_page >= kFullWritePages) {
                    job = Job::kIdle;
                    last_written_dataset = static_cast<int>(dataset_index);
                    set_screen(Screen::kPicker);
                    update_dataset_list();
                    picker_hint.SetText(strings.Get(StringId::kFactoryComplete));
                    app.log().Info("iButton Reader: factory write and readback complete");
                } else {
                    FixedString<48> progress;
                    progress.Append(strings.Get(StringId::kFactoryProgress));
                    progress.AppendUint(write_page);
                    progress.Append(" / ");
                    progress.AppendUint(kFullWritePages);
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
                    job = Job::kAuthManual;
                    password_hint.SetText(strings.Get(StringId::kStatusReading));
                    set_screen(Screen::kHome);
                }
                if (key_cancel.OnTouch(*touch).clicked) {
                    job = Job::kIdle;
                    if (device_present && rom[0] == kDs1991FamilyCode) {
                        (void)home_card.SetText(strings.Get(StringId::kConnectionConnected));
                        home_card.SetEnabled(true);
                        show_home_hint(StringId::kPasswordHint);
                    }
                    set_screen(Screen::kHome);
                }
                update_key();
            } else if (screen == Screen::kDs1991AuthChoice) {
                if (auth_alpha.OnTouch(*touch).clicked && device_present) {
                    active_password = ibutton_reader::Alpha(rom);
                    job = Job::kAuthDs1991Alpha;
                    home_detail.SetText(strings.Get(StringId::kStatusTryingA));
                    set_screen(Screen::kHome);
                } else if (auth_beta.OnTouch(*touch).clicked && device_present) {
                    active_password = ibutton_reader::Beta(rom);
                    job = Job::kAuthDs1991Beta;
                    home_detail.SetText(strings.Get(StringId::kStatusTryingB));
                    set_screen(Screen::kHome);
                } else if (auth_manual.OnTouch(*touch).clicked && device_present) {
                    show_manual_key();
                } else if (auth_cancel.OnTouch(*touch).clicked) {
                    if (device_present && rom[0] == kDs1991FamilyCode) {
                        (void)home_card.SetText(strings.Get(StringId::kConnectionConnected));
                        home_card.SetEnabled(true);
                    }
                    set_screen(Screen::kHome);
                }
            } else if (screen == Screen::kData && job == Job::kIdle && loaded_bytes != 0U) {
                if (data_back.OnTouch(*touch).clicked) set_screen(Screen::kHome);
                if (data_previous.OnTouch(*touch).clicked && display_page > 0U) {
                    --display_page;
                    update_data_rows();
                }
                const unsigned page_count = (loaded_bytes + kDataBytesPerPage - 1U) / kDataBytesPerPage;
                if (data_next.OnTouch(*touch).clicked && display_page + 1U < page_count) {
                    ++display_page;
                    update_data_rows();
                }
                if (match_button.OnTouch(*touch).clicked) {
                    matched_dataset = -1;
                    match_index = 0;
                    best_match_bytes = 0;
                    job = Job::kMatch;
                    match_name.SetText("…");
                    data_status.SetText(strings.Get(StringId::kStatusMatching));
                }
                if (data_choose.OnTouch(*touch).clicked) {
                    // Authentication is selected from the home card each time for DS1991.
                    // Keep the data picker independent so its button always opens the catalog.
                    picker_page_index = 0;
                    update_dataset_list();
                    set_screen(Screen::kPicker);
                }
            } else if (screen == Screen::kHome) {
                if (home_card.OnTouch(*touch).clicked && job == Job::kIdle && device_present) {
                    if (rom[0] == kDs1991FamilyCode) show_ds1991_auth_choice();
                    else if (authenticated) (void)start_read(quick_read_bytes());
                }
                if (full_read_home_button.OnTouch(*touch).clicked && job == Job::kIdle && authenticated &&
                    rom[0] != kDs1991FamilyCode && loaded_bytes < device_capacity())
                    set_screen(Screen::kFullReadPrompt);
            } else if (screen == Screen::kFullReadPrompt) {
                if (full_read_4kb.OnTouch(*touch).clicked) {
                    if (start_read(kFourKilobytes, false, true)) set_screen(Screen::kHome);
                    else set_screen(Screen::kData);
                }
                if (full_read_all.OnTouch(*touch).clicked) {
                    if (start_read(kFullDeviceBytes, false, true)) set_screen(Screen::kHome);
                    else set_screen(Screen::kData);
                }
                if (full_read_cancel.OnTouch(*touch).clicked) set_screen(Screen::kHome);
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
                if (page_previous.OnTouch(*touch).clicked && picker_page_index > 0U) {
                    --picker_page_index;
                    last_written_dataset = -1;
                    update_dataset_list();
                }
                if (page_next.OnTouch(*touch).clicked && picker_page_index + 1U < page_count) {
                    ++picker_page_index;
                    last_written_dataset = -1;
                    update_dataset_list();
                }
            } else if (screen == Screen::kConfirm) {
                if (confirm_back.OnTouch(*touch).clicked && job == Job::kIdle) {
                    set_screen(Screen::kPicker);
                    update_dataset_list();
                }
                if (confirm_full_write.OnTouch(*touch).clicked && job == Job::kIdle && device_present && authenticated) {
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
