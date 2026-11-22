// Goot — Native C Interoperability Bindings
// Maps strictly to native/goot_native.h using verified Goose extern fn ABI.

namespace ffi;

// Event type constants matching native/goot_native.h
let EV_NONE           = 0;
let EV_CLOSE          = 1;
let EV_CONFIGURE      = 2;
let EV_FRAME_DONE     = 3;
let EV_KEY            = 4;
let EV_POINTER_MOTION = 5;
let EV_POINTER_BUTTON = 6;
let EV_POINTER_AXIS   = 7;
let EV_FOCUS          = 8;
let EV_CLIPBOARD_DATA = 9;
let EV_PRIMARY_DATA   = 10;
let EV_TOUCH_DOWN     = 11;
let EV_TOUCH_UP       = 12;
let EV_TOUCH_MOTION   = 13;

// Modifier bitmasks
let MOD_SHIFT = 1;
let MOD_CTRL  = 2;
let MOD_ALT   = 4;
let MOD_SUPER = 8;

// Poll bitmasks
let POLL_ERR     = -1;
let POLL_TIMEOUT = 0;
let POLL_WAYLAND = 1;
let POLL_PTY     = 2;
let POLL_SIGNAL  = 4;
let POLL_PTY_HUP = 8;
let POLL_TIMER   = 16;

// POSIX PTY & safe process helpers
extern fn pty_spawn(shell_override: const u8[:], term_env: const u8[:],
                    cols: i32, rows: i32, width_px: i32, height_px: i32) -> i32 = "goot_pty_spawn";
extern fn pty_fd() -> i32 = "goot_pty_fd";
extern fn pty_child_pid() -> i32 = "goot_pty_child_pid";
extern fn pty_read(buf: u8[:]) -> i64 = "goot_pty_read";
extern fn pty_write(data: const u8[:]) -> i64 = "goot_pty_write";
extern fn pty_resize(cols: i32, rows: i32, width_px: i32, height_px: i32) -> i32 = "goot_pty_resize";
extern fn pty_check_child() -> i32 = "goot_pty_check_child";
extern fn pty_exit_code() -> i32 = "goot_pty_exit_code";
extern fn pty_close() = "goot_pty_close";
extern fn spawn_self(cwd: const u8[:]) -> i32 = "goot_spawn_self";
extern fn validate_url(url: const u8[:]) -> i32 = "goot_validate_url";
extern fn open_url(url: const u8[:]) -> i32 = "goot_open_url";

// Linux epoll + signalfd
extern fn poll_init(wl_fd: i32, pty_fd: i32) -> i32 = "goot_poll_init";
extern fn poll_wait(timeout_ms: i32) -> i32 = "goot_poll_wait";
extern fn poll_terminated() -> i32 = "goot_poll_terminated";
extern fn poll_arm_repeat(delay_ms: i32, interval_ms: i32) = "goot_poll_arm_repeat";
extern fn poll_disarm_repeat() = "goot_poll_disarm_repeat";
extern fn poll_close() = "goot_poll_close";
extern fn time_monotonic_ns() -> i64 = "goot_time_monotonic_ns";

// Native Wayland + xdg-shell + wl_shm + clipboard + primary selection + touch
extern fn wl_init(title: const u8[:], app_id: const u8[:],
                  init_width: i32, init_height: i32) -> i32 = "goot_wl_init";
extern fn wl_fd() -> i32 = "goot_wl_fd";
extern fn wl_dispatch_pending() -> i32 = "goot_wl_dispatch_pending";
extern fn wl_prepare_and_read() -> i32 = "goot_wl_prepare_and_read";
extern fn wl_flush() -> i32 = "goot_wl_flush";
extern fn wl_poll_event() -> i32 = "goot_wl_poll_event";
extern fn wl_ev_a() -> i32 = "goot_wl_ev_a";
extern fn wl_ev_b() -> i32 = "goot_wl_ev_b";
extern fn wl_ev_c() -> i32 = "goot_wl_ev_c";
extern fn wl_ev_utf8(out_buf: u8[:]) -> i64 = "goot_wl_ev_utf8";
extern fn wl_set_title(title: const u8[:]) = "goot_wl_set_title";
extern fn wl_width() -> i32 = "goot_wl_width";
extern fn wl_height() -> i32 = "goot_wl_height";
extern fn wl_scale() -> i32 = "goot_wl_scale";
extern fn wl_frame_ready() -> bool = "goot_wl_frame_ready";
extern fn wl_is_maximized() -> i32 = "goot_wl_is_maximized";
extern fn wl_is_fullscreen() -> i32 = "goot_wl_is_fullscreen";
extern fn wl_is_activated() -> i32 = "goot_wl_is_activated";
extern fn wl_is_csd() -> i32 = "goot_wl_is_csd";
extern fn wl_set_decoration_mode(pref: i32) = "goot_wl_set_decoration_mode";
extern fn wl_toplevel_move() = "goot_wl_toplevel_move";
extern fn wl_toplevel_resize(edges: i32) = "goot_wl_toplevel_resize";
extern fn wl_toplevel_minimize() = "goot_wl_toplevel_minimize";
extern fn wl_toplevel_toggle_maximize() = "goot_wl_toplevel_toggle_maximize";
extern fn wl_handle_timer_repeat() = "goot_wl_handle_timer_repeat";
extern fn wl_set_cursor_shape(shape: i32) = "goot_wl_set_cursor_shape";
extern fn wl_clipboard_set(text: const u8[:]) -> i32 = "goot_wl_clipboard_set";
extern fn wl_clipboard_request() -> i32 = "goot_wl_clipboard_request";
extern fn wl_clipboard_read(out_buf: u8[:]) -> i64 = "goot_wl_clipboard_read";
extern fn wl_primary_set(text: const u8[:]) -> i32 = "goot_wl_primary_set";
extern fn wl_primary_request() -> i32 = "goot_wl_primary_request";
extern fn wl_primary_read(out_buf: u8[:]) -> i64 = "goot_wl_primary_read";
extern fn wl_close() = "goot_wl_close";

// FreeType + Fontconfig + HarfBuzz
extern fn font_init(family: const u8[:], size_pt: i32, dpi: i32, line_spacing_px: i32) -> i32 = "goot_font_init";
extern fn font_resize(size_pt: i32, dpi: i32) -> i32 = "goot_font_resize";
extern fn font_cell_width() -> i32 = "goot_font_cell_width";
extern fn font_cell_height() -> i32 = "goot_font_cell_height";
extern fn font_baseline() -> i32 = "goot_font_baseline";
extern fn font_is_rtl(cp: u32) -> i32 = "goot_font_is_rtl";
extern fn font_close() = "goot_font_close";

// Pixman software renderer & Graphics API
extern fn render_begin(bg_rgb: u32) -> i32 = "goot_render_begin";
extern fn render_fill_rect(x: i32, y: i32, w: i32, h: i32, rgb: u32) = "goot_render_fill_rect";
extern fn render_cell(px: i32, py: i32, cell_w: i32, cell_h: i32,
                      codepoint: u32, fg_rgb: u32, bg_rgb: u32,
                      attr_flags: u32, span_cols: i32) = "goot_render_cell";
extern fn render_cell_ext(px: i32, py: i32, cell_w: i32, cell_h: i32,
                          codepoint: u32, fg_rgb: u32, bg_rgb: u32,
                          attr_flags: u32, span_cols: i32,
                          ul_style: u8, ul_color: u32) = "goot_render_cell_ext";
extern fn render_cursor(px: i32, py: i32, cell_w: i32, cell_h: i32,
                        color_rgb: u32, style: i32) = "goot_render_cursor";
extern fn render_damage(x: i32, y: i32, w: i32, h: i32) = "goot_render_damage";
extern fn render_images(pad_px: i32, title_h: i32, view_top_abs_row: i32,
                        rows: i32, cols: i32, cell_w: i32, cell_h: i32) = "goot_render_images";
extern fn render_end(request_frame_cb: bool) = "goot_render_end";

// Sixel & Kitty Graphics Protocols
extern fn sixel_decode(data: const u8[:], col: i32, abs_row: i32, cell_w: i32, cell_h: i32) -> i32 = "goot_sixel_decode";
extern fn kitty_graphics(data: const u8[:], col: i32, abs_row: i32, cell_w: i32, cell_h: i32) -> i32 = "goot_kitty_graphics";
extern fn image_scroll(evicted_lines: i32) = "goot_image_scroll";
extern fn image_clear() = "goot_image_clear";

// Environment, State & Config File Helpers
extern fn env_get(name: const u8[:], out_buf: u8[:]) -> i64 = "goot_env_get";
extern fn file_read(path: const u8[:], out_buf: u8[:]) -> i64 = "goot_file_read";
extern fn state_load_cols() -> i32 = "goot_state_load_cols";
extern fn state_load_rows() -> i32 = "goot_state_load_rows";
extern fn state_save_geometry(cols: i32, rows: i32) = "goot_state_save_geometry";
extern fn poll_detach_pty() = "goot_poll_detach_pty";
extern fn cmdline_cols() -> i32 = "goot_cmdline_cols";
extern fn cmdline_rows() -> i32 = "goot_cmdline_rows";
extern fn cmdline_has_custom_size() -> i32 = "goot_cmdline_has_custom_size";
extern fn cmdline_hold() -> i32 = "goot_cmdline_hold";
