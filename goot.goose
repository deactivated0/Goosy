// Goot — Native Wayland Terminal Emulator written in Goose
// Event-driven architecture: blocks on epoll when idle, batches PTY reads,
// supports Foot-compatible Normal, Scrollback Search, URL, and Unicode modes,
// multi-click/block/extend mouse selection, primary selection, and touchscreen gestures.

import src.colors;
import src.config;
import src.ffi;
import src.input;
import src.modes;
import src.parser;
import src.renderer;
import src.screen;

fn calc_grid(win_w: i32, win_h: i32, scale: i32, pad_px: i32, cell_w: i32, cell_h: i32, title_h: i32) -> i32, i32 {
    let eff_w = win_w * scale - 2 * pad_px * scale;
    let eff_h = win_h * scale - title_h - 2 * pad_px * scale;
    var cols = if cell_w > 0 { eff_w / cell_w } else { 80 };
    var rows = if cell_h > 0 { eff_h / cell_h } else { 24 };
    if cols < 2 { cols = 2; }
    if rows < 2 { rows = 2; }
    if cols > screen::MAX_COLS { cols = screen::MAX_COLS; }
    if rows > screen::MAX_ROWS { rows = screen::MAX_ROWS; }
    return cols, rows;
}

fn px_to_cell(px: i32, py: i32, scale: i32, pad_px: i32, cell_w: i32, cell_h: i32, cols: i32, rows: i32, title_h: i32) -> i32, i32 {
    var c = if cell_w > 0 { (px * scale - pad_px * scale) / cell_w } else { 0 };
    var r = if cell_h > 0 { (py * scale - title_h - pad_px * scale) / cell_h } else { 0 };
    if c < 0 { c = 0; }
    if c >= cols { c = cols - 1; }
    if r < 0 { r = 0; }
    if r >= rows { r = rows - 1; }
    return c, r;
}

fn hit_test_title_bar(px: i32, py: i32, win_w: i32, title_h: i32, scale: i32) -> i32 {
    let y = py * scale;
    if y < 0 || y >= title_h { return renderer::BTN_NONE; }
    let x = px * scale;
    let btn_w = 38 * scale;
    if x >= win_w - btn_w {
        return renderer::BTN_CLOSE;
    } else if x >= win_w - 2 * btn_w {
        return renderer::BTN_MAX;
    } else if x >= win_w - 3 * btn_w {
        return renderer::BTN_MIN;
    }
    return renderer::BTN_NONE;
}

let RESIZE_NONE: i32         = 0;
let RESIZE_TOP: i32          = 1;
let RESIZE_BOTTOM: i32       = 2;
let RESIZE_LEFT: i32         = 4;
let RESIZE_TOP_LEFT: i32     = 5;
let RESIZE_BOTTOM_LEFT: i32  = 6;
let RESIZE_RIGHT: i32        = 8;
let RESIZE_TOP_RIGHT: i32    = 9;
let RESIZE_BOTTOM_RIGHT: i32 = 10;

fn hit_test_resize(px: i32, py: i32, w: i32, h: i32, margin: i32) -> i32 {
    let top = (py < margin);
    let bot = (py >= h - margin);
    let left = (px < margin);
    let right = (px >= w - margin);

    if top && left { return RESIZE_TOP_LEFT; }
    if top && right { return RESIZE_TOP_RIGHT; }
    if bot && left { return RESIZE_BOTTOM_LEFT; }
    if bot && right { return RESIZE_BOTTOM_RIGHT; }
    if top { return RESIZE_TOP; }
    if bot { return RESIZE_BOTTOM; }
    if left { return RESIZE_LEFT; }
    if right { return RESIZE_RIGHT; }
    return RESIZE_NONE;
}

fn cursor_for_resize_edge(edge: i32) -> i32 {
    match edge {
        RESIZE_TOP, RESIZE_BOTTOM => 3, // ns-resize
        RESIZE_LEFT, RESIZE_RIGHT => 4, // ew-resize
        RESIZE_TOP_LEFT, RESIZE_BOTTOM_RIGHT => 5, // nwse-resize
        RESIZE_BOTTOM_LEFT, RESIZE_TOP_RIGHT => 6, // nesw-resize
        _ => 1,
    }
}

fn adjust_font(scr: screen::Screen&, cfg: config::Config&, font_size: i32&, base_size: i32, delta: i32) {
    if delta == 0 {
        font_size = base_size;
    } else {
        let next = font_size + delta;
        if next < 6 || next > 72 { return; }
        font_size = next;
    }
    let sc = ffi::wl_scale();
    if ffi::font_resize(font_size, 96 * sc) == 0 {
        let is_csd = cfg.title_bar && (ffi::wl_is_csd() != 0);
        let title_h = if is_csd { cfg.title_bar_height * sc } else { 0 };
        let nc, nr = calc_grid(ffi::wl_width(), ffi::wl_height(), sc,
                               cfg.padding, ffi::font_cell_width(), ffi::font_cell_height(), title_h);
        screen::resize(scr, nc, nr);
        let content_h = ffi::wl_height() * sc - title_h;
        ffi::pty_resize(nc, nr, ffi::wl_width() * sc, if content_h > 0 { content_h } else { 0 });
        screen::mark_all_dirty(scr);
    }
}

fn main() {
    var cfg = config::load_user_config();
    var font_size = cfg.font_size;
    let base_font_size = cfg.font_size;

    guard ffi::font_init(cfg.font_family, font_size, 96, cfg.line_spacing) == 0 else {
        print("goot: failed to initialize font engine");
        exit(1);
    }
    defer ffi::font_close();

    var init_cols = cfg.initial_cols;
    var init_rows = cfg.initial_rows;
    if cfg.remember_window_size {
        let sc = ffi::state_load_cols();
        let sr = ffi::state_load_rows();
        if sc >= 20 && sr >= 5 {
            init_cols = sc;
            init_rows = sr;
        }
    }

    let init_cw = ffi::font_cell_width();
    let init_ch = ffi::font_cell_height();
    let init_title_h = if cfg.title_bar { cfg.title_bar_height } else { 0 };
    let init_w  = init_cols * init_cw + 2 * cfg.padding;
    let init_h  = init_rows * init_ch + 2 * cfg.padding + init_title_h;

    guard ffi::wl_init(cfg.title, "goot", init_w, init_h) == 0 else {
        print("goot: failed to initialize Wayland display (is WAYLAND_DISPLAY set?)");
        exit(1);
    }
    defer ffi::wl_close();

    ffi::wl_set_decoration_mode(cfg.decorations);

    var scr = screen::Screen { .. };
    screen::init(scr, init_cols, init_rows,
                 cfg.palette.fg, cfg.palette.bg,
                 cfg.scrollback_lines, cfg.cursor_style);
    var par = parser::Parser { allow_osc52: cfg.allow_osc52, .. };
    var mstate = modes::new_state();

    guard ffi::pty_spawn("", "xterm-256color", scr.cols, scr.rows, init_cols * init_cw, init_rows * init_ch) == 0 else {
        print("goot: failed to spawn PTY shell");
        exit(1);
    }
    defer ffi::pty_close();

    guard ffi::poll_init(ffi::wl_fd(), ffi::pty_fd()) == 0 else {
        print("goot: failed to initialize epoll loop");
        exit(1);
    }
    defer ffi::poll_close();

    var pty_buf: u8[16384]  = [0; 16384];
    var key_utf8: u8[16]    = [0; 16];
    var clip_buf: u8[65536] = [0; 65536];
    var key_out: u8[>..]    = [];
    var paste_out: u8[>..]  = [];
    var sel_out: u8[>..]    = [];
    var mouse_out: u8[>..]  = [];
    var uni_out: u8[>..]    = [];

    var running       = true;
    var focused       = true;
    var drag_left     = false;
    var drag_right    = false;
    var right_force_c = false;
    var ptr_x: i32    = 0;
    var ptr_y: i32    = 0;

    var hover_btn: i32           = renderer::BTN_NONE;
    var pressed_btn: i32         = renderer::BTN_NONE;
    var last_title_click_ns: i64 = 0;

    // Multi-click tracking (single / double / triple / quad)
    var last_click_ns: i64 = 0;
    var last_click_c: i32  = -1;
    var last_click_r: i32  = -1;
    var click_count: i32   = 0;

    // Touchscreen gesture state
    var touch_active      = false;
    var touch_selecting   = false;
    var touch_down_ns: i64 = 0;
    var touch_start_x: i32 = 0;
    var touch_start_y: i32 = 0;
    var touch_last_y: i32  = 0;

    renderer::draw_frame(scr, cfg, mstate, focused, hover_btn, pressed_btn);

    while running {
        if ffi::wl_dispatch_pending() < 0 || ffi::wl_flush() < 0 { break; }

        let ready = ffi::poll_wait(-1);
        if ready == ffi::POLL_ERR { break; }

        if (ready & ffi::POLL_WAYLAND) != 0 {
            if ffi::wl_prepare_and_read() < 0 { break; }
        }
        if (ready & ffi::POLL_TIMER) != 0 {
            ffi::wl_handle_timer_repeat();
        }

        loop {
            let ev = ffi::wl_poll_event();
            if ev == ffi::EV_NONE { break; }

            if ev == ffi::EV_CLOSE {
                running = false;
                break;
            } else if ev == ffi::EV_CONFIGURE {
                let w = ffi::wl_ev_a();
                let h = ffi::wl_ev_b();
                let sc = ffi::wl_ev_c();
                let is_csd = cfg.title_bar && (ffi::wl_is_csd() != 0);
                let title_h = if is_csd { cfg.title_bar_height * sc } else { 0 };
                let nc, nr = calc_grid(w, h, sc, cfg.padding, ffi::font_cell_width(), ffi::font_cell_height(), title_h);
                if nc != scr.cols || nr != scr.rows {
                    screen::resize(scr, nc, nr);
                    let content_h = h * sc - title_h;
                    ffi::pty_resize(nc, nr, w * sc, if content_h > 0 { content_h } else { 0 });
                    if cfg.remember_window_size && ffi::wl_is_maximized() == 0 && ffi::wl_is_fullscreen() == 0 {
                        ffi::state_save_geometry(nc, nr);
                    }
                } else {
                    screen::mark_all_dirty(scr);
                }
            } else if ev == ffi::EV_FOCUS {
                focused = (ffi::wl_ev_a() != 0);
                scr.title_dirty = true;
                screen::mark_row_dirty(scr, scr.cy);
            } else if ev == ffi::EV_KEY {
                let sym   = ffi::wl_ev_a();
                let mods  = ffi::wl_ev_b();
                let ulen  = ffi::wl_ev_utf8(key_utf8);
                let ctrl  = (mods & ffi::MOD_CTRL) != 0;
                let shift = (mods & ffi::MOD_SHIFT) != 0;

                if mstate.kind == modes::MODE_SEARCH {
                    if sym == input::XKB_Escape || (ctrl && (sym == 'g' || sym == 'G')) {
                        modes::cancel_search(mstate, scr);
                    } else if sym == input::XKB_Return {
                        screen::extract_selection(scr, sel_out);
                        if sel_out.len > 0 { ffi::wl_primary_set(sel_out); }
                        mstate.kind = modes::MODE_NORMAL;
                        screen::mark_all_dirty(scr);
                    } else if ctrl && (sym == 'r' || sym == 'R') {
                        let _ = modes::search_step(mstate, scr, -1, true);
                    } else if ctrl && (sym == 's' || sym == 'S') {
                        let _ = modes::search_step(mstate, scr, 1, true);
                    } else if ctrl && (sym == 'w' || sym == 'W') {
                        modes::search_extend_word(mstate, scr, shift);
                    } else if sym == input::XKB_XF86Paste || (ctrl && (sym == 'v' || sym == 'V' || sym == 'y' || sym == 'Y')) {
                        ffi::wl_clipboard_request();
                    } else if shift && sym == input::XKB_Insert {
                        ffi::wl_primary_request();
                    } else if sym == input::XKB_BackSpace {
                        if mstate.search_query.len > 0 {
                            mstate.search_query.pop();
                            let _ = modes::search_step(mstate, scr, -1, false);
                        }
                    } else if !ctrl && ulen > 0 {
                        modes::search_append_text(mstate, scr, key_utf8[..ulen]);
                    }
                } else if mstate.kind == modes::MODE_URL {
                    if sym == input::XKB_Escape || (ctrl && (sym == 'c' || sym == 'C' || sym == 'g' || sym == 'G' || sym == 'd' || sym == 'D')) {
                        mstate.kind = modes::MODE_NORMAL;
                        screen::mark_all_dirty(scr);
                    } else if !ctrl && (sym == 't' || sym == 'T') {
                        mstate.url_show_text = !mstate.url_show_text;
                        screen::mark_all_dirty(scr);
                    } else if !ctrl && ulen == 1 {
                        let hit = modes::url_feed_char(mstate, key_utf8[0]);
                        if hit >= 0 && hit < mstate.url_count {
                            let _ = ffi::open_url(mstate.urls[hit].url);
                            mstate.kind = modes::MODE_NORMAL;
                        }
                        screen::mark_all_dirty(scr);
                    }
                } else if mstate.kind == modes::MODE_UNICODE {
                    if sym == input::XKB_Escape || (ctrl && (sym == 'c' || sym == 'C' || sym == 'g' || sym == 'G')) {
                        mstate.kind = modes::MODE_NORMAL;
                        screen::mark_all_dirty(scr);
                    } else if sym == input::XKB_Return || sym == ' ' {
                        if modes::unicode_commit(mstate, uni_out) && uni_out.len > 0 {
                            ffi::pty_write(uni_out);
                        }
                        mstate.kind = modes::MODE_NORMAL;
                        screen::mark_all_dirty(scr);
                    } else if sym == input::XKB_BackSpace {
                        if mstate.uni_hex.len > 0 { mstate.uni_hex.pop(); }
                        screen::mark_all_dirty(scr);
                    } else if !ctrl && ulen == 1 && modes::is_hex_digit(key_utf8[0]) && mstate.uni_hex.len < 6 {
                        mstate.uni_hex.push(key_utf8[0]);
                        screen::mark_all_dirty(scr);
                    }
                } else {
                    // Normal mode
                    let act = input::check_shortcut(sym, mods);
                    if act == input::ACT_COPY {
                        screen::extract_selection(scr, sel_out);
                        if sel_out.len > 0 { ffi::wl_clipboard_set(sel_out); }
                    } else if act == input::ACT_PASTE_CLIP {
                        ffi::wl_clipboard_request();
                    } else if act == input::ACT_PASTE_PRIMARY {
                        ffi::wl_primary_request();
                    } else if act == input::ACT_FONT_INC {
                        adjust_font(scr, cfg, font_size, base_font_size, 1);
                    } else if act == input::ACT_FONT_DEC {
                        adjust_font(scr, cfg, font_size, base_font_size, -1);
                    } else if act == input::ACT_FONT_RST {
                        adjust_font(scr, cfg, font_size, base_font_size, 0);
                    } else if act == input::ACT_SCROLL_UP {
                        screen::scroll_viewport(scr, scr.rows / 2);
                    } else if act == input::ACT_SCROLL_DN {
                        screen::scroll_viewport(scr, -(scr.rows / 2));
                    } else if act == input::ACT_SEARCH_START {
                        modes::enter_search(mstate, scr);
                    } else if act == input::ACT_SPAWN_TERM {
                        let _ = ffi::spawn_self(scr.cwd);
                    } else if act == input::ACT_URL_MODE {
                        modes::enter_url_mode(mstate, scr, cfg.url_label_letters);
                    } else if act == input::ACT_UNICODE_MODE {
                        modes::enter_unicode_mode(mstate, scr);
                    } else if act == input::ACT_PROMPT_PREV {
                        let _ = screen::jump_prev_prompt(scr);
                    } else if act == input::ACT_PROMPT_NEXT {
                        let _ = screen::jump_next_prompt(scr);
                    } else {
                        if scr.view_offset > 0 { scr.view_offset = 0; screen::mark_all_dirty(scr); }
                        input::translate_key(key_out, sym, mods, key_utf8[..ulen], scr.app_cursor);
                        if key_out.len > 0 { ffi::pty_write(key_out); }
                    }
                }
            } else if ev == ffi::EV_CLIPBOARD_DATA || ev == ffi::EV_PRIMARY_DATA {
                let n = if ev == ffi::EV_CLIPBOARD_DATA {
                    ffi::wl_clipboard_read(clip_buf)
                } else {
                    ffi::wl_primary_read(clip_buf)
                };
                if n > 0 {
                    if mstate.kind == modes::MODE_SEARCH {
                        modes::search_append_text(mstate, scr, clip_buf[..n]);
                    } else {
                        input::format_paste(paste_out, clip_buf[..n], scr.bracketed_paste);
                        if paste_out.len > 0 { ffi::pty_write(paste_out); }
                    }
                }
            } else if ev == ffi::EV_POINTER_MOTION {
                ptr_x = ffi::wl_ev_a();
                ptr_y = ffi::wl_ev_b();
                let sc = ffi::wl_scale();
                let is_csd = cfg.title_bar && (ffi::wl_is_csd() != 0);
                let title_h = if is_csd { cfg.title_bar_height * sc } else { 0 };
                let win_w = ffi::wl_width() * sc;
                let win_logical_w = ffi::wl_width();
                let win_logical_h = ffi::wl_height();
                let is_resizable = (ffi::wl_is_maximized() == 0) && (ffi::wl_is_fullscreen() == 0);
                let resize_margin: i32 = if cfg.padding > 2 { 2 } else { 1 };
                let resize_edge = if is_resizable { hit_test_resize(ptr_x, ptr_y, win_logical_w, win_logical_h, resize_margin) } else { RESIZE_NONE };

                if drag_left {
                    let c, r = px_to_cell(ptr_x, ptr_y, sc, cfg.padding,
                                          ffi::font_cell_width(), ffi::font_cell_height(), scr.cols, scr.rows, title_h);
                    scr.sel_x1 = c; scr.sel_y1 = r;
                    screen::mark_all_dirty(scr);
                } else if drag_right {
                    let c, r = px_to_cell(ptr_x, ptr_y, sc, cfg.padding,
                                          ffi::font_cell_width(), ffi::font_cell_height(), scr.cols, scr.rows, title_h);
                    screen::extend_selection(scr, c, r, right_force_c);
                } else if resize_edge != RESIZE_NONE {
                    if hover_btn != renderer::BTN_NONE {
                        hover_btn = renderer::BTN_NONE;
                        scr.title_dirty = true;
                    }
                    ffi::wl_set_cursor_shape(cursor_for_resize_edge(resize_edge));
                } else if title_h > 0 && ptr_y * sc < title_h {
                    let next_hover = hit_test_title_bar(ptr_x, ptr_y, win_w, title_h, sc);
                    if hover_btn != next_hover {
                        hover_btn = next_hover;
                        scr.title_dirty = true;
                    }
                    if hover_btn != renderer::BTN_NONE {
                        ffi::wl_set_cursor_shape(2); // pointer
                    } else {
                        ffi::wl_set_cursor_shape(1); // default
                    }
                } else {
                    if hover_btn != renderer::BTN_NONE {
                        hover_btn = renderer::BTN_NONE;
                        scr.title_dirty = true;
                    }
                    let c, r = px_to_cell(ptr_x, ptr_y, sc, cfg.padding,
                                          ffi::font_cell_width(), ffi::font_cell_height(), scr.cols, scr.rows, title_h);
                    let cell = screen::visible_cell(scr, c, r);
                    if cell.link_id > 0 {
                        ffi::wl_set_cursor_shape(2); // pointer for hyperlinks
                    } else {
                        ffi::wl_set_cursor_shape(0); // text
                    }
                }
            } else if ev == ffi::EV_POINTER_BUTTON {
                let btn     = ffi::wl_ev_a();
                let pressed = (ffi::wl_ev_b() != 0);
                let mods    = ffi::wl_ev_c();
                let shift   = (mods & ffi::MOD_SHIFT) != 0;
                let ctrl    = (mods & ffi::MOD_CTRL) != 0;
                let sc      = ffi::wl_scale();
                let is_csd  = cfg.title_bar && (ffi::wl_is_csd() != 0);
                let title_h = if is_csd { cfg.title_bar_height * sc } else { 0 };
                let win_logical_w = ffi::wl_width();
                let win_logical_h = ffi::wl_height();
                let is_resizable = (ffi::wl_is_maximized() == 0) && (ffi::wl_is_fullscreen() == 0);
                let resize_margin: i32 = if cfg.padding > 2 { 2 } else { 1 };
                let resize_edge = if is_resizable { hit_test_resize(ptr_x, ptr_y, win_logical_w, win_logical_h, resize_margin) } else { RESIZE_NONE };

                if resize_edge != RESIZE_NONE && btn == 272 && pressed && !drag_left && !drag_right {
                    ffi::wl_toplevel_resize(resize_edge);
                } else if title_h > 0 && ptr_y * sc < title_h && !drag_left && !drag_right {
                    if btn == 272 { // Left click
                        if pressed {
                            pressed_btn = hover_btn;
                            scr.title_dirty = true;
                            if hover_btn == renderer::BTN_NONE {
                                let now = ffi::time_monotonic_ns();
                                if now - last_title_click_ns < 350000000 {
                                    ffi::wl_toplevel_toggle_maximize();
                                    last_title_click_ns = 0;
                                } else {
                                    last_title_click_ns = now;
                                    ffi::wl_toplevel_move();
                                }
                            }
                        } else {
                            let was_pressed = pressed_btn;
                            pressed_btn = renderer::BTN_NONE;
                            scr.title_dirty = true;
                            if was_pressed == renderer::BTN_CLOSE && hover_btn == renderer::BTN_CLOSE {
                                running = false;
                                break;
                            } else if was_pressed == renderer::BTN_MAX && hover_btn == renderer::BTN_MAX {
                                ffi::wl_toplevel_toggle_maximize();
                            } else if was_pressed == renderer::BTN_MIN && hover_btn == renderer::BTN_MIN {
                                ffi::wl_toplevel_minimize();
                            }
                        }
                    }
                } else {
                    let c, r = px_to_cell(ptr_x, ptr_y, sc, cfg.padding,
                                          ffi::font_cell_width(), ffi::font_cell_height(), scr.cols, scr.rows, title_h);

                    // Client mouse tracking active unless Shift overrides for local selection
                    if scr.mouse_mode != 0 && !shift {
                        let mbtn: i32 = if btn == 272 { 0 } else if btn == 274 { 1 } else { 2 };
                        input::encode_mouse(mouse_out, scr, mbtn, c, r, pressed);
                        if mouse_out.len > 0 { ffi::pty_write(mouse_out); }
                    } else if btn == 272 { // Left click
                        if pressed {
                            let cell = screen::visible_cell(scr, c, r);
                            if ctrl && cell.link_id > 0 {
                                var link_url: u8[>..] = [];
                                if screen::get_hyperlink(scr, cell.link_id, link_url) && link_url.len > 0 {
                                    let _ = ffi::open_url(link_url);
                                }
                            } else {
                                let now = ffi::time_monotonic_ns();
                                if now - last_click_ns < 400000000 && c == last_click_c && r == last_click_r {
                                    click_count += 1;
                                    if click_count > 4 { click_count = 1; }
                                } else {
                                    click_count = 1;
                                }
                                last_click_ns = now; last_click_c = c; last_click_r = r;

                                if click_count == 1 {
                                    drag_left = true;
                                    scr.sel_active = true;
                                    scr.sel_mode = if ctrl { screen::SEL_BLOCK } else { screen::SEL_CHAR };
                                    scr.sel_x0 = c; scr.sel_y0 = r;
                                    scr.sel_x1 = c; scr.sel_y1 = r;
                                    screen::mark_all_dirty(scr);
                                } else if click_count == 2 {
                                    screen::select_word(scr, c, r, ctrl);
                                } else if click_count == 3 {
                                    screen::select_quote_or_line(scr, c, r);
                                } else if click_count == 4 {
                                    screen::select_line(scr, r);
                                }
                            }
                        } else {
                            drag_left = false;
                            if scr.sel_active {
                                if click_count == 1 && scr.sel_x0 == scr.sel_x1 && scr.sel_y0 == scr.sel_y1 {
                                    scr.sel_active = false;
                                    screen::mark_all_dirty(scr);
                                } else {
                                    screen::extract_selection(scr, sel_out);
                                    if sel_out.len > 0 {
                                        ffi::wl_primary_set(sel_out);
                                        ffi::wl_clipboard_set(sel_out);
                                    }
                                }
                            }
                        }
                    } else if btn == 274 { // Middle click: paste from primary selection
                        if pressed { ffi::wl_primary_request(); }
                    } else if btn == 273 { // Right click: extend selection (+ Ctrl forces char-wise)
                        if pressed {
                            drag_right = true;
                            right_force_c = ctrl;
                            screen::extend_selection(scr, c, r, ctrl);
                        } else {
                            drag_right = false;
                            if scr.sel_active {
                                screen::extract_selection(scr, sel_out);
                                if sel_out.len > 0 {
                                    ffi::wl_primary_set(sel_out);
                                    ffi::wl_clipboard_set(sel_out);
                                }
                            }
                        }
                    }
                }
            } else if ev == ffi::EV_POINTER_AXIS {
                let delta = ffi::wl_ev_a();
                let mods  = ffi::wl_ev_b();
                let ctrl  = (mods & ffi::MOD_CTRL) != 0;
                let sc    = ffi::wl_scale();
                let is_csd = cfg.title_bar && (ffi::wl_is_csd() != 0);
                let title_h = if is_csd { cfg.title_bar_height * sc } else { 0 };

                if ctrl {
                    adjust_font(scr, cfg, font_size, base_font_size, if delta < 0 { 1 } else { -1 });
                } else if scr.mouse_mode != 0 {
                    let c, r = px_to_cell(ptr_x, ptr_y, sc, cfg.padding,
                                          ffi::font_cell_width(), ffi::font_cell_height(), scr.cols, scr.rows, title_h);
                    let mbtn: i32 = if delta < 0 { 64 } else { 65 };
                    input::encode_mouse(mouse_out, scr, mbtn, c, r, true);
                    if mouse_out.len > 0 { ffi::pty_write(mouse_out); }
                } else {
                    screen::scroll_viewport(scr, if delta < 0 { 3 } else { -3 });
                }
            } else if ev == ffi::EV_TOUCH_DOWN {
                touch_active    = true;
                touch_selecting = false;
                touch_down_ns   = ffi::time_monotonic_ns();
                touch_start_x   = ffi::wl_ev_a();
                touch_start_y   = ffi::wl_ev_b();
                touch_last_y    = touch_start_y;
            } else if ev == ffi::EV_TOUCH_MOTION {
                if touch_active {
                    let tx = ffi::wl_ev_a();
                    let ty = ffi::wl_ev_b();
                    let sc = ffi::wl_scale();
                    let is_csd = cfg.title_bar && (ffi::wl_is_csd() != 0);
                    let title_h = if is_csd { cfg.title_bar_height * sc } else { 0 };
                    let held_ms = (ffi::time_monotonic_ns() - touch_down_ns) / 1000000;
                    if !touch_selecting && held_ms >= (cfg.touch_long_press_ms as i64) {
                        touch_selecting = true;
                        let c0, r0 = px_to_cell(touch_start_x, touch_start_y, sc, cfg.padding,
                                                ffi::font_cell_width(), ffi::font_cell_height(), scr.cols, scr.rows, title_h);
                        scr.sel_active = true;
                        scr.sel_mode = screen::SEL_CHAR;
                        scr.sel_x0 = c0; scr.sel_y0 = r0;
                    }
                    if touch_selecting {
                        let c1, r1 = px_to_cell(tx, ty, sc, cfg.padding,
                                                ffi::font_cell_width(), ffi::font_cell_height(), scr.cols, scr.rows, title_h);
                        scr.sel_x1 = c1; scr.sel_y1 = r1;
                        screen::mark_all_dirty(scr);
                    } else {
                        let dy = ty - touch_last_y;
                        let ch = ffi::font_cell_height();
                        if ch > 0 && (dy >= ch || dy <= -ch) {
                            screen::scroll_viewport(scr, dy / ch);
                            touch_last_y = ty;
                        }
                    }
                }
            } else if ev == ffi::EV_TOUCH_UP {
                if touch_active {
                    let sc = ffi::wl_scale();
                    let is_csd = cfg.title_bar && (ffi::wl_is_csd() != 0);
                    let title_h = if is_csd { cfg.title_bar_height * sc } else { 0 };

                    if touch_selecting && scr.sel_active {
                        screen::extract_selection(scr, sel_out);
                        if sel_out.len > 0 { ffi::wl_primary_set(sel_out); }
                    } else if scr.mouse_mode != 0 {
                        // Tap emulates left click when client wants mouse events
                        let c, r = px_to_cell(touch_start_x, touch_start_y, sc, cfg.padding,
                                              ffi::font_cell_width(), ffi::font_cell_height(), scr.cols, scr.rows, title_h);
                        input::encode_mouse(mouse_out, scr, 0, c, r, true);
                        if mouse_out.len > 0 { ffi::pty_write(mouse_out); }
                        input::encode_mouse(mouse_out, scr, 0, c, r, false);
                        if mouse_out.len > 0 { ffi::pty_write(mouse_out); }
                    }
                    touch_active = false;
                    touch_selecting = false;
                }
            }
        }

        if (ready & ffi::POLL_PTY) != 0 {
            var batch: i32 = 0;
            while batch < 4 {
                let n = ffi::pty_read(pty_buf);
                if n > 0 {
                    parser::feed_slice(par, scr, cfg.palette, pty_buf[..n]);
                    batch += 1;
                } else if n == 0 {
                    running = false;
                    break;
                } else {
                    break;
                }
            }
            if scr.reply_buf.len > 0 {
                ffi::pty_write(scr.reply_buf);
                scr.reply_buf.clear();
            }
        }

        if (ready & (ffi::POLL_PTY_HUP | ffi::POLL_SIGNAL)) != 0 {
            if ffi::poll_terminated() != 0 || ffi::pty_check_child() != 0 { running = false; }
        }

        if scr.title_dirty {
            ffi::wl_set_title(scr.title);
        }

        if running {
            renderer::draw_frame(scr, cfg, mstate, focused, hover_btn, pressed_btn);
            scr.title_dirty = false;
        }
    }

    if cfg.remember_window_size && ffi::wl_is_maximized() == 0 && ffi::wl_is_fullscreen() == 0 {
        ffi::state_save_geometry(scr.cols, scr.rows);
    }

    ffi::pty_close();
    ffi::poll_close();
    ffi::wl_close();
    ffi::font_close();
}
