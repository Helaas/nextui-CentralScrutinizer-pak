#include "cs_app.h"
#include "cs_server.h"
#include "cs_ui.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#if defined(CS_ENABLE_APOSTROPHE_UI)
#include "apostrophe.h"
#include "apostrophe_widgets.h"
#include "../third_party/qrcodegen.h"

#define CS_UI_QR_MAX_VERSION 12
#endif

void cs_ui_model_make_active(
    cs_ui_model *model, const char *ip, int port, const char *code, int trusted_count, int terminal_enabled) {
    if (!model) {
        return;
    }

    memset(model, 0, sizeof(*model));
    if (ip) {
        snprintf(model->ip, sizeof(model->ip), "%s", ip);
    }
    if (code) {
        snprintf(model->code, sizeof(model->code), "%s", code);
    }
    model->port = port;
    model->trusted_browser_count = trusted_count;
    model->terminal_enabled = terminal_enabled ? 1 : 0;
    snprintf(model->status_message,
             sizeof(model->status_message),
             "%s",
             trusted_count > 0
                 ? "Trusted clients are remembered. Pairing codes refresh after each use. Press Y to add another client."
                 : "Open the URL in a browser or press Y for QR pairing. Pairing codes are single-use and refresh automatically.");
}

void cs_ui_model_make_offline(cs_ui_model *model) {
    if (!model) {
        return;
    }

    memset(model, 0, sizeof(*model));
    model->is_offline = 1;
    snprintf(model->status_message, sizeof(model->status_message), "%s", "Connect Wi-Fi in NextUI first.");
}

#if defined(CS_ENABLE_APOSTROPHE_UI)

int cs_ui_init(void) {
    ap_config cfg = {0};

    cfg.window_title = "Central Scrutinizer";
    cfg.font_path = AP_PLATFORM_IS_DEVICE ? NULL : "third_party/apostrophe/res/font.ttf";
    cfg.log_path = ap_resolve_log_path("central-scrutinizer");
    cfg.is_nextui = AP_PLATFORM_IS_DEVICE;
    cfg.cpu_speed = AP_CPU_SPEED_MENU;

    return ap_init(&cfg) == AP_OK ? 0 : -1;
}

void cs_ui_shutdown(void) {
    ap_quit();
}

static int cs_ui_show_offline_message(const cs_ui_model *model) {
    ap_footer_item footer[] = {
        {.button = AP_BTN_B, .label = "Exit"},
        {.button = AP_BTN_A, .label = "Retry", .is_confirm = true},
    };
    ap_message_opts opts = {
        .message = model ? model->status_message : "Connect Wi-Fi in NextUI first.",
        .footer = footer,
        .footer_count = (int) (sizeof(footer) / sizeof(footer[0])),
    };
    ap_confirm_result result = {0};

    (void) ap_confirmation(&opts, &result);
    return result.confirmed ? CS_UI_ACTION_REFRESH : CS_UI_ACTION_EXIT;
}

static void cs_ui_show_settings_error(const char *message) {
    ap_footer_item footer[] = {
        {.button = AP_BTN_A, .label = "OK", .is_confirm = true},
    };
    ap_message_opts opts = {
        .message = message ? message : "Could not save setting.",
        .footer = footer,
        .footer_count = (int) (sizeof(footer) / sizeof(footer[0])),
    };
    ap_confirm_result result = {0};

    (void) ap_confirmation(&opts, &result);
}

void cs_ui_show_error(const char *message) {
    cs_ui_show_settings_error(message);
}

static int cs_ui_scale_px(int screen_w, int base) {
    int scaled;

    if (base <= 0) {
        return 0;
    }

    scaled = (screen_w * base) / 320;
    return scaled > 0 ? scaled : 1;
}

static void cs_ui_draw_info_pair(TTF_Font *key_font,
                                 TTF_Font *value_font,
                                 const ap_theme *theme,
                                 int x,
                                 int width,
                                 int value_gap,
                                 int row_gap,
                                 int *cursor_y,
                                 const char *key,
                                 const char *value) {
    int key_h;
    int value_h;

    if (!key_font || !value_font || !theme || !cursor_y || !key || !value) {
        return;
    }

    key_h = TTF_FontHeight(key_font);
    value_h = ap_measure_wrapped_text_height(value_font, value, width);
    ap_draw_text(key_font, key, x, *cursor_y, theme->accent);
    ap_draw_text_wrapped(value_font, value, x, *cursor_y + key_h + value_gap, width, theme->text, AP_ALIGN_LEFT);
    *cursor_y += key_h + value_h + row_gap;
}

static int cs_ui_measure_info_pair_height(TTF_Font *key_font,
                                          TTF_Font *value_font,
                                          int width,
                                          int value_gap,
                                          int row_gap,
                                          const char *value) {
    if (!key_font || !value_font || !value || width <= 0) {
        return 0;
    }

    return TTF_FontHeight(key_font) + ap_measure_wrapped_text_height(value_font, value, width) + row_gap;
}

static int cs_ui_run_pair_qr_screen(cs_app *app, const cs_ui_model *model) {
    ap_footer_item footer[] = {
        {.button = AP_BTN_B, .label = "Back"},
        {.button = AP_BTN_A, .label = "Refresh", .is_confirm = true},
    };
    uint8_t temp[qrcodegen_BUFFER_LEN_FOR_VERSION(CS_UI_QR_MAX_VERSION)];
    uint8_t qrcode[qrcodegen_BUFFER_LEN_FOR_VERSION(CS_UI_QR_MAX_VERSION)];
    char token[64];
    char url[256];
    char ttl_text[96];
    int initial_trusted_count;
    int regenerate = 1;

    if (!app || !model) {
        return -1;
    }

    initial_trusted_count = model->trusted_browser_count;
    snprintf(ttl_text,
             sizeof(ttl_text),
             "Valid for %d minutes or until it is used.",
             (cs_server_get_qr_pair_token_ttl_seconds() + 59) / 60);

    for (;;) {
        ap_input_event ev;
        ap_theme *theme;
        TTF_Font *hint_font;
        SDL_Rect content_rect;
        int screen_w;
        int ttl_h;
        int qr_modules;
        int side_pad;
        int top_pad;
        int bottom_pad;
        int border;
        int gap;
        int warning_y;
        int qr_available_h;
        int max_qr_px;
        int module_px;
        int qr_px;
        int qr_x;
        int qr_y;
        int quiet_zone = 4;
        int title_gap;
        ap_color white = {255, 255, 255, 255};
        ap_color black = {0, 0, 0, 255};

        if (regenerate) {
            if (cs_server_issue_qr_pair_token(token, sizeof(token)) != 0
                || snprintf(url, sizeof(url), "http://%s:%d%s?token=%s", model->ip, model->port, CS_SERVER_QR_PAIR_PATH, token)
                       >= (int) sizeof(url)
                || !qrcodegen_encodeText(url,
                                         temp,
                                         qrcode,
                                         qrcodegen_Ecc_MEDIUM,
                                         qrcodegen_VERSION_MIN,
                                         CS_UI_QR_MAX_VERSION,
                                         qrcodegen_Mask_AUTO,
                                         true)) {
                cs_ui_show_settings_error("Could not generate a pairing QR code.");
                return -1;
            }
            regenerate = 0;
        }

        if (!regenerate
            && (cs_server_get_trusted_count() != initial_trusted_count || !cs_server_qr_pair_token_is_active(token))) {
            return CS_UI_ACTION_REFRESH;
        }

        while (ap_poll_input(&ev)) {
            if (!ev.pressed) {
                continue;
            }

            switch (ev.button) {
                case AP_BTN_B:
                    return -1;
                case AP_BTN_A:
                    regenerate = 1;
                    break;
                default:
                    break;
            }
        }

        theme = ap_get_theme();
        hint_font = ap_get_font(AP_FONT_MICRO);
        if (!theme || !hint_font) {
            return -1;
        }

        screen_w = ap_get_screen_width();
        content_rect = ap_get_content_rect(true, true, false);
        side_pad = cs_ui_scale_px(screen_w, 14);
        top_pad = cs_ui_scale_px(screen_w, 12);
        bottom_pad = cs_ui_scale_px(screen_w, 10);
        border = cs_ui_scale_px(screen_w, 6);
        gap = cs_ui_scale_px(screen_w, 10);
        title_gap = cs_ui_scale_px(screen_w, 2);
        ttl_h = ap_measure_wrapped_text_height(hint_font, ttl_text, screen_w - (side_pad * 2));
        qr_modules = qrcodegen_getSize(qrcode) + (quiet_zone * 2);
        qr_available_h = content_rect.h - top_pad - bottom_pad - ttl_h - gap;
        max_qr_px = screen_w - (side_pad * 2);
        if (qr_available_h < max_qr_px) {
            max_qr_px = qr_available_h;
        }
        if (max_qr_px < qr_modules) {
            cs_ui_show_settings_error("The QR code is too large to render on this screen.");
            return -1;
        }

        module_px = max_qr_px / qr_modules;
        qr_px = module_px * qr_modules;
        qr_x = (screen_w - qr_px) / 2;
        qr_y = content_rect.y + top_pad + ((qr_available_h - qr_px) / 2);
        if (qr_y < content_rect.y + top_pad) {
            qr_y = content_rect.y + top_pad;
        }
        warning_y = content_rect.y + content_rect.h - bottom_pad - ttl_h;

        ap_draw_background();
        ap_draw_screen_title_centered("Pair by QR", NULL);
        ap_draw_rect(qr_x - border, qr_y - border, qr_px + (border * 2), qr_px + (border * 2), theme->accent);
        ap_draw_rect(qr_x, qr_y, qr_px, qr_px, white);

        for (int y = 0; y < qrcodegen_getSize(qrcode); ++y) {
            for (int x = 0; x < qrcodegen_getSize(qrcode); ++x) {
                if (qrcodegen_getModule(qrcode, x, y)) {
                    ap_draw_rect(qr_x + (x + quiet_zone) * module_px,
                                 qr_y + (y + quiet_zone) * module_px,
                                 module_px,
                                 module_px,
                                 black);
                }
            }
        }

        ap_draw_text_wrapped(hint_font,
                             ttl_text,
                             side_pad,
                             warning_y + title_gap,
                             screen_w - (side_pad * 2),
                             theme->hint,
                             AP_ALIGN_CENTER);
        ap_draw_footer(footer, (int) (sizeof(footer) / sizeof(footer[0])));
        ap_present();
    }
}

static int cs_ui_run_settings_screen(cs_app *app) {
    ap_option terminal_options[] = {
        {.label = "Disabled", .value = "Disabled"},
        {.label = "Enabled", .value = "Enabled"},
    };
    ap_option keep_awake_options[] = {
        {.label = "Disabled", .value = "Disabled"},
        {.label = "Enabled", .value = "Enabled"},
    };
    ap_options_item items[] = {
        {.label = "Terminal", .type = AP_OPT_STANDARD, .options = terminal_options, .option_count = 2, .selected_option = 0},
        {.label = "Keep Awake in Background",
         .type = AP_OPT_STANDARD,
         .options = keep_awake_options,
         .option_count = 2,
         .selected_option = 0},
        {.label = "Revoke Trusted Browsers", .type = AP_OPT_CLICKABLE},
        {.label = "Run in Background", .type = AP_OPT_CLICKABLE},
    };
    ap_footer_item footer[] = {
        {.button = AP_BTN_B, .label = "Back"},
        {.button = AP_BTN_A, .label = "Choose", .is_confirm = true},
    };
    int focused_index = 0;
    int visible_start_index = 0;

    if (!app) {
        return CS_UI_ACTION_EXIT;
    }

    for (;;) {
        ap_options_list_opts opts = {0};
        ap_options_list_result result = {0};
        int rc;

        items[0].selected_option = cs_app_get_terminal_enabled(app) ? 1 : 0;
        items[1].selected_option = cs_app_get_keep_awake_in_background(app) ? 1 : 0;
        opts.title = "Settings";
        opts.items = items;
        opts.item_count = (int) (sizeof(items) / sizeof(items[0]));
        opts.footer = footer;
        opts.footer_count = (int) (sizeof(footer) / sizeof(footer[0]));
        opts.initial_selected_index = focused_index;
        opts.visible_start_index = visible_start_index;
        opts.return_on_option_change = 1;

        rc = ap_options_list(&opts, &result);
        focused_index = result.focused_index;
        visible_start_index = result.visible_start_index;
        if (rc == AP_CANCELLED || result.action == AP_ACTION_BACK) {
            return -1;
        }
        if (result.action == AP_ACTION_OPTION_CHANGED) {
            if (result.focused_index == 0) {
                if (cs_app_set_terminal_enabled(app, items[0].selected_option == 1) != 0) {
                    cs_ui_show_settings_error("Could not save the terminal setting.");
                }
                continue;
            }
            if (result.focused_index == 1) {
                if (cs_app_set_keep_awake_in_background(app, items[1].selected_option == 1) != 0) {
                    cs_ui_show_settings_error("Could not save the background keep-awake setting.");
                }
            }
            continue;
        }
        if (result.action == AP_ACTION_SELECTED) {
            switch (result.focused_index) {
                case 2:
                    return CS_UI_ACTION_REVOKE;
                case 3: {
                    ap_footer_item confirm_footer[] = {
                        {.button = AP_BTN_B, .label = "Cancel"},
                        {.button = AP_BTN_A, .label = "Run", .is_confirm = true},
                    };
                    ap_message_opts confirm_opts = {
                        .message = "Trusted browsers stay connected in background mode.\n\nNew pairing is disabled until you reopen the app. Reopening it stops background mode and brings back pairing and settings. Active terminal sessions may need to reconnect.",
                        .footer = confirm_footer,
                        .footer_count = (int) (sizeof(confirm_footer) / sizeof(confirm_footer[0])),
                    };
                    ap_confirm_result confirm_result = {0};

                    if (!cs_app_can_background(app)) {
                        cs_ui_show_settings_error("Run in Background requires at least one trusted browser.");
                        break;
                    }

                    (void) ap_confirmation(&confirm_opts, &confirm_result);
                    if (confirm_result.confirmed) {
                        return CS_UI_ACTION_BACKGROUND;
                    }
                    break;
                }
                default:
                    break;
            }
        }
    }
}

int cs_ui_run_server_screen(cs_app *app, const cs_ui_model *model) {
    char url[128];
    char trusted[32];
    const char *pairing_description;
    const char *terminal_state;
    static int scroll_offset = 0;
    ap_footer_item footer[] = {
        {.button = AP_BTN_B, .label = "Exit"},
        {.button = AP_BTN_Y, .label = "QR"},
        {.button = AP_BTN_A, .label = "Settings", .is_confirm = true},
    };

    if (!app || !model) {
        return CS_UI_ACTION_EXIT;
    }

    if (model->is_offline) {
        return cs_ui_show_offline_message(model);
    }

    snprintf(url, sizeof(url), "http://%s:%d", model->ip, model->port);
    snprintf(trusted, sizeof(trusted), "%d", model->trusted_browser_count);
    pairing_description = model->trusted_browser_count > 0
                              ? "This screen refreshes automatically after a client pairs. Press Y to generate a new QR code for another trusted client."
                              : "Open the URL in a browser and enter the PIN once, or press Y for a QR code. PINs and QR links are single-use.";
    terminal_state = model->terminal_enabled ? "Enabled" : "Disabled";

    for (;;) {
        ap_input_event ev;
        ap_theme *theme;
        TTF_Font *section_font;
        TTF_Font *key_font;
        TTF_Font *value_font;
        SDL_Rect content_rect;
        int margin;
        int width;
        int cursor_y;
        int margin_pad;
        int content_pad;
        int top_pad;
        int bottom_pad;
        int section_gap;
        int info_value_gap;
        int info_row_gap;
        int block_gap;
        int scrollbar_gap;
        int scroll_step;
        int total_content_h;
        int max_scroll;
        int scrollbar_x;
        uint32_t started_at = SDL_GetTicks();

        for (;;) {
            theme = ap_get_theme();
            section_font = ap_get_font(AP_FONT_SMALL);
            key_font = ap_get_font(AP_FONT_TINY);
            value_font = ap_get_font(AP_FONT_TINY);
            if (!theme || !section_font || !key_font || !value_font) {
                return CS_UI_ACTION_EXIT;
            }

            content_rect = ap_get_content_rect(true, true, false);
            margin_pad = cs_ui_scale_px(content_rect.w, 12);
            content_pad = cs_ui_scale_px(content_rect.w, 24);
            top_pad = cs_ui_scale_px(content_rect.w, 8);
            bottom_pad = cs_ui_scale_px(content_rect.w, 8);
            section_gap = cs_ui_scale_px(content_rect.w, 6);
            info_value_gap = cs_ui_scale_px(content_rect.w, 2);
            info_row_gap = cs_ui_scale_px(content_rect.w, 10);
            block_gap = cs_ui_scale_px(content_rect.w, 4);
            scrollbar_gap = cs_ui_scale_px(content_rect.w, 14);
            scroll_step = cs_ui_scale_px(content_rect.w, 40);
            margin = content_rect.x + margin_pad;
            width = content_rect.w - content_pad - scrollbar_gap;
            if (width < 1) {
                width = 1;
            }

            total_content_h = top_pad;
            total_content_h += TTF_FontHeight(section_font) + section_gap;
            total_content_h += cs_ui_measure_info_pair_height(key_font, value_font, width, info_value_gap, info_row_gap, url);
            total_content_h += cs_ui_measure_info_pair_height(
                key_font, value_font, width, info_value_gap, info_row_gap, model->code[0] ? model->code : "----");
            total_content_h += cs_ui_measure_info_pair_height(key_font, value_font, width, info_value_gap, info_row_gap, trusted);
            total_content_h += cs_ui_measure_info_pair_height(key_font, value_font, width, info_value_gap, info_row_gap, terminal_state);
            total_content_h += cs_ui_measure_info_pair_height(
                key_font, value_font, width, info_value_gap, info_row_gap, model->status_message);
            total_content_h += block_gap;
            total_content_h += TTF_FontHeight(section_font) + section_gap;
            total_content_h += ap_measure_wrapped_text_height(value_font, pairing_description, width);
            total_content_h += bottom_pad;
            max_scroll = total_content_h - content_rect.h;
            if (max_scroll < 0) {
                max_scroll = 0;
            }
            if (scroll_offset > max_scroll) {
                scroll_offset = max_scroll;
            }
            scrollbar_x = margin + width + cs_ui_scale_px(content_rect.w, 6);

            while (ap_poll_input(&ev)) {
                if (!ev.pressed) {
                    continue;
                }

                switch (ev.button) {
                    case AP_BTN_A: {
                        int action = cs_ui_run_settings_screen(app);

                        if (action >= 0) {
                            return action;
                        }
                        started_at = SDL_GetTicks();
                        break;
                    }
                    case AP_BTN_Y: {
                        int action = cs_ui_run_pair_qr_screen(app, model);

                        if (action >= 0) {
                            return action;
                        }
                        started_at = SDL_GetTicks();
                        break;
                    }
                    case AP_BTN_B:
                        return CS_UI_ACTION_EXIT;
                    case AP_BTN_UP:
                        scroll_offset -= scroll_step;
                        if (scroll_offset < 0) {
                            scroll_offset = 0;
                        }
                        started_at = SDL_GetTicks();
                        break;
                    case AP_BTN_DOWN:
                        scroll_offset += scroll_step;
                        if (scroll_offset > max_scroll) {
                            scroll_offset = max_scroll;
                        }
                        started_at = SDL_GetTicks();
                        break;
                    default:
                        break;
                }
            }

            if (SDL_GetTicks() - started_at >= 1000u) {
                return CS_UI_ACTION_REFRESH;
            }

            ap_draw_background();
            ap_draw_screen_title_centered("Central Scrutinizer", NULL);
            cursor_y = content_rect.y + top_pad - scroll_offset;
            SDL_RenderSetClipRect(ap_get_renderer(), &content_rect);
            ap_draw_text(section_font, "Server", margin, cursor_y, theme->accent);
            cursor_y += TTF_FontHeight(section_font) + section_gap;
            cs_ui_draw_info_pair(key_font, value_font, theme, margin, width, info_value_gap, info_row_gap, &cursor_y, "URL", url);
            cs_ui_draw_info_pair(
                key_font,
                value_font,
                theme,
                margin,
                width,
                info_value_gap,
                info_row_gap,
                &cursor_y,
                "Code",
                model->code[0] ? model->code : "----");
            cs_ui_draw_info_pair(
                key_font, value_font, theme, margin, width, info_value_gap, info_row_gap, &cursor_y, "Trusted Clients", trusted);
            cs_ui_draw_info_pair(
                key_font, value_font, theme, margin, width, info_value_gap, info_row_gap, &cursor_y, "Terminal", terminal_state);
            cs_ui_draw_info_pair(
                key_font, value_font, theme, margin, width, info_value_gap, info_row_gap, &cursor_y, "Status", model->status_message);
            cursor_y += block_gap;
            ap_draw_text(section_font, "Pairing", margin, cursor_y, theme->accent);
            cursor_y += TTF_FontHeight(section_font) + section_gap;
            ap_draw_text_wrapped(value_font, pairing_description, margin, cursor_y, width, theme->text, AP_ALIGN_LEFT);
            SDL_RenderSetClipRect(ap_get_renderer(), NULL);
            if (max_scroll > 0) {
                ap_draw_scrollbar(scrollbar_x, content_rect.y, content_rect.h, content_rect.h, total_content_h, scroll_offset);
            }
            ap_draw_footer(footer, (int) (sizeof(footer) / sizeof(footer[0])));
            ap_present();
            SDL_Delay(16);
        }
    }
}

#else

int cs_ui_init(void) {
    return 0;
}

void cs_ui_shutdown(void) {
}

void cs_ui_show_error(const char *message) {
    (void) message;
}

int cs_ui_run_server_screen(cs_app *app, const cs_ui_model *model) {
    (void) app;
    (void) model;
    return CS_UI_ACTION_EXIT;
}

#endif
