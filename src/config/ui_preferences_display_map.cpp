#include "stdafx.h"

#pragma warning(push, 0)
#include "foobar2000/SDK/coreDarkMode.h"
#include "foobar2000/helpers/atl-misc.h"
#include "resource.h"
#pragma warning(pop)

#include "config/config_auto.h"
#include "logging.h"
#include "preferences.h"
#include "ui_hooks.h"

// clang-format off: GUIDs should be one line
static const GUID GUID_PREFERENCES_PAGE_DISPLAY_MAP = { 0x5c1e7a44, 0x9b02, 0x4f38, { 0x8a, 0x61, 0xd3, 0x27, 0x46, 0x0e, 0xb9, 0x5c } };

static const GUID GUID_CFG_MAP_ENABLED = { 0x1d7b3f28, 0x64a9, 0x4c1e, { 0xb3, 0x50, 0x92, 0x8f, 0x1a, 0xc7, 0x65, 0x04 } };
static const GUID GUID_CFG_MAP_SHOW_TRANSLIT = { 0x8e4c2051, 0xf6b3, 0x4a7d, { 0x9d, 0x12, 0x6a, 0x38, 0xc4, 0xe5, 0x71, 0x2b } };
static const GUID GUID_CFG_MAP_PALETTE_FROM_BUNDLE = { 0x3a9f6c07, 0x2e58, 0x4b91, { 0x87, 0x0d, 0x5c, 0xb2, 0x19, 0x74, 0xe8, 0xaf } };
static const GUID GUID_CFG_MAP_HOVER_SPOTLIGHT = { 0x61b2e95d, 0x07c4, 0x4d8a, { 0xa5, 0x3f, 0xe1, 0x68, 0x2b, 0x90, 0x47, 0x16 } };
static const GUID GUID_CFG_MAP_PAST_BLEND = { 0xc47a08e3, 0x5916, 0x4f2b, { 0xbc, 0x84, 0x37, 0xd5, 0x60, 0x0a, 0x9e, 0x71 } };
static const GUID GUID_CFG_MAP_INDENT_PX = { 0x9a05d1f8, 0xb643, 0x4e27, { 0x93, 0x6c, 0x20, 0xaf, 0x58, 0xd1, 0x3b, 0x82 } };
static const GUID GUID_CFG_MAP_HIDDEN_COLUMNS = { 0x7d3e6b14, 0x20a5, 0x4869, { 0xb0, 0x71, 0xc8, 0x45, 0x9e, 0x3d, 0xf6, 0x0b } };
// clang-format on

static cfg_auto_bool cfg_map_enabled(GUID_CFG_MAP_ENABLED, IDC_MAP_ENABLED, true);
static cfg_auto_bool cfg_map_show_translit(GUID_CFG_MAP_SHOW_TRANSLIT, IDC_MAP_SHOW_TRANSLIT, true);
static cfg_auto_bool cfg_map_palette_from_bundle(GUID_CFG_MAP_PALETTE_FROM_BUNDLE,
                                                 IDC_MAP_PALETTE_FROM_BUNDLE,
                                                 true);
static cfg_auto_bool cfg_map_hover_spotlight(GUID_CFG_MAP_HOVER_SPOTLIGHT, IDC_MAP_HOVER_SPOTLIGHT, true);
// NOTE: cfg_auto_ranged_int drives a trackbar control, so both of these are sliders in the
//       dialog resource. Increments keep the numbers round without making the slider
//       unusable (a 1-px-per-notch indent slider would be 200 notches wide).
static cfg_auto_ranged_int cfg_map_past_blend(GUID_CFG_MAP_PAST_BLEND, IDC_MAP_PAST_BLEND, 0, 100, 5, 35);
static cfg_auto_ranged_int cfg_map_indent_px(GUID_CFG_MAP_INDENT_PX, IDC_MAP_INDENT, 0, 80, 2, 0);

// Language keys never to draw, comma-separated (e.g. "tr,de"). A checklist would be
// friendlier but it would have to be rebuilt from whichever map happens to be loaded, and a
// preference that silently forgets a language the user hid for a different song is worse
// than a text field that keeps every language on purpose.
static cfg_auto_string cfg_map_hidden_columns(GUID_CFG_MAP_HIDDEN_COLUMNS, IDC_MAP_HIDDEN_COLUMNS, "");

static cfg_auto_property* g_map_auto_properties[] = {
    &cfg_map_enabled,
    &cfg_map_show_translit,
    &cfg_map_palette_from_bundle,
    &cfg_map_hover_spotlight,
    &cfg_map_past_blend,
    &cfg_map_indent_px,
    &cfg_map_hidden_columns,
};

bool preferences::display::map_enabled()
{
    return cfg_map_enabled.get_value();
}

bool preferences::display::map_show_transliteration()
{
    return cfg_map_show_translit.get_value();
}

bool preferences::display::map_palette_from_bundle()
{
    return cfg_map_palette_from_bundle.get_value();
}

bool preferences::display::map_hover_spotlight()
{
    return cfg_map_hover_spotlight.get_value();
}

int preferences::display::map_past_blend_percent()
{
    return cfg_map_past_blend.get_value();
}

int preferences::display::map_indent_px()
{
    return cfg_map_indent_px.get_value();
}

std::tstring preferences::display::map_hidden_columns()
{
    return to_tstring(cfg_map_hidden_columns.get_value());
}

// The preferences page is intentionally small: every one of these toggles only affects
// tracks that have a bundle, so there is nothing here that can surprise someone who does
// not use word maps.
class PreferencesDisplayMap : public CDialogImpl<PreferencesDisplayMap>, public auto_preferences_page_instance
{
public:
    PreferencesDisplayMap(preferences_page_callback::ptr callback)
        : auto_preferences_page_instance(callback, g_map_auto_properties)
    {
    }

    enum
    {
        IDD = IDD_PREFERENCES_DISPLAY_MAP
    };

    void apply() override;
    void reset() override;
    bool has_changed() override;

    BEGIN_MSG_MAP_EX(PreferencesDisplayMap)
    MSG_WM_INITDIALOG(OnInitDialog)
    COMMAND_HANDLER_EX(IDC_MAP_ENABLED, BN_CLICKED, OnUIChange)
    COMMAND_HANDLER_EX(IDC_MAP_SHOW_TRANSLIT, BN_CLICKED, OnUIChange)
    COMMAND_HANDLER_EX(IDC_MAP_PALETTE_FROM_BUNDLE, BN_CLICKED, OnUIChange)
    COMMAND_HANDLER_EX(IDC_MAP_HOVER_SPOTLIGHT, BN_CLICKED, OnUIChange)
    COMMAND_HANDLER_EX(IDC_MAP_PAST_BLEND, TB_THUMBPOSITION, OnUIChange)
    COMMAND_HANDLER_EX(IDC_MAP_INDENT, TB_THUMBPOSITION, OnUIChange)
    COMMAND_HANDLER_EX(IDC_MAP_HIDDEN_COLUMNS, EN_CHANGE, OnUIChange)
    END_MSG_MAP()

private:
    BOOL OnInitDialog(CWindow, LPARAM);
    void OnUIChange(UINT, int, CWindow);

    void UpdatePastBlendPreview();
    void UpdateIndentPreview();

    fb2k::CCoreDarkModeHooks m_dark;
};

BOOL PreferencesDisplayMap::OnInitDialog(CWindow /*wndFocus*/, LPARAM /*initParam*/)
{
    m_dark.AddDialogWithControls(m_hWnd);
    UpdatePastBlendPreview();
    UpdateIndentPreview();
    return TRUE;
}

void PreferencesDisplayMap::OnUIChange(UINT /*code*/, int /*id*/, CWindow /*ctrl*/)
{
    UpdatePastBlendPreview();
    UpdateIndentPreview();
    OnChanged();
}

void PreferencesDisplayMap::UpdatePastBlendPreview()
{
    const int value = cfg_map_past_blend.get_ui_value();

    const int buffer_len = 32;
    TCHAR buffer[buffer_len];
    _sntprintf_s(buffer, buffer_len, _T("%d %%"), value);
    SetDlgItemText(IDC_MAP_PAST_BLEND_PREVIEW, buffer);
}

void PreferencesDisplayMap::UpdateIndentPreview()
{
    const int value = cfg_map_indent_px.get_ui_value();

    const int buffer_len = 64;
    TCHAR buffer[buffer_len];
    if(value == 0)
    {
        _sntprintf_s(buffer, buffer_len, _T("auto (font-based)"));
    }
    else
    {
        _sntprintf_s(buffer, buffer_len, _T("%d px"), value);
    }
    SetDlgItemText(IDC_MAP_INDENT_PREVIEW, buffer);
}

void PreferencesDisplayMap::apply()
{
    auto_preferences_page_instance::apply();
    // Colours and layout are read from the configuration at paint time, so a repaint is
    // all that is needed for the change to take effect.
    repaint_all_lyric_panels();
}

void PreferencesDisplayMap::reset()
{
    auto_preferences_page_instance::reset();
    UpdatePastBlendPreview();
    UpdateIndentPreview();
}

bool PreferencesDisplayMap::has_changed()
{
    return auto_preferences_page_instance::has_changed();
}

class PreferencesDisplayMapImpl : public preferences_page_impl<PreferencesDisplayMap>
{
public:
    const char* get_name() final
    {
        return "Word map";
    }
    GUID get_guid() final
    {
        return GUID_PREFERENCES_PAGE_DISPLAY_MAP;
    }
    GUID get_parent_guid() final
    {
        return GUID_PREFERENCES_PAGE_ROOT;
    }
};
static preferences_page_factory_t<PreferencesDisplayMapImpl> g_preferences_page_display_map_factory;
