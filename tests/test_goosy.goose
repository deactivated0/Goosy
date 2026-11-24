// Goosy — Pure Goose Terminal Engine Unit Test Suite
// Covers UTF-8, SGR, scrollback, alt screen, OSC 7/133/52, Search/URL/Unicode modes,
// multi-click/block/quote/extend selection, Foot-compatible shortcuts, and goosy.ini parsing.

import src.colors;
import src.config;
import src.ffi;
import src.input;
import src.modes;
import src.parser;
import src.screen;
import src.utf8;

fn test_utf8_decoding() {
    var d = utf8::Decoder { .. };
    let cp1, ok1 = utf8::feed(d, 'A');
    assert(ok1 && cp1 == 0x41);

    let _, r0 = utf8::feed(d, 0xC3);
    assert(!r0);
    let cp2, ok2 = utf8::feed(d, 0xA9);
    assert(ok2 && cp2 == 0x00E9);

    let _, _ = utf8::feed(d, 0xE4);
    let _, _ = utf8::feed(d, 0xB8);
    let cp3, ok3 = utf8::feed(d, 0xAD);
    assert(ok3 && cp3 == 0x4E2D && utf8::cell_width(cp3) == 2);

    assert(utf8::cell_width(0x0301) == 0 && utf8::is_combining(0x0301));
    let bad1, bok1 = utf8::feed(d, 0xC0);
    assert(bok1 && bad1 == utf8::REPLACEMENT_CHAR);
}

fn test_osc7_and_osc133_prompt_jumps() {
    var pal = colors::default_palette();
    var scr = screen::Screen { .. };
    screen::init(scr, 40, 4, pal.fg, pal.bg, 20, 0);
    var par = parser::Parser { .. };

    // OSC 7 CWD with percent-encoding
    parser::feed_slice(par, scr, pal, "\x1b]7;file://artix/home/user/my%20dir\x07");
    assert(scr.cwd.len == 17);

    // Emit prompts via OSC 133;A and push into scrollback
    parser::feed_slice(par, scr, pal, "\x1b]133;A\x07$ first\r\nout1\r\nout2\r\nout3\r\n\x1b]133;A\x07$ second\r\nout4\r\nout5");
    assert(screen::jump_prev_prompt(scr));
    assert(scr.view_offset > 0);
    assert(screen::jump_next_prompt(scr));
}

fn test_search_url_and_unicode_modes() {
    var pal = colors::default_palette();
    var scr = screen::Screen { .. };
    screen::init(scr, 50, 5, pal.fg, pal.bg, 20, 0);
    var par = parser::Parser { .. };
    var ms  = modes::new_state();

    parser::feed_slice(par, scr, pal, "visit https://aardappel.github.io/goose/ now\r\nhello world foo_bar");

    // URL mode detection & 't' toggle
    modes::enter_url_mode(ms, scr, "sadf");
    assert(ms.kind == modes::MODE_URL && ms.url_count == 1);
    assert(ms.urls[0].label[0] == 's');
    let hit = modes::url_feed_char(ms, 's');
    assert(hit == 0);

    // Scrollback search + Ctrl+W word extension
    modes::enter_search(ms, scr);
    modes::search_append_text(ms, scr, "hel");
    assert(ms.search_found && ms.search_col == 0);
    modes::search_extend_word(ms, scr, false);
    assert(ms.search_query.len == 5); // "hello"
    modes::cancel_search(ms, scr);
    assert(ms.kind == modes::MODE_NORMAL);

    // Unicode input mode: U+03BB ('λ')
    modes::enter_unicode_mode(ms, scr);
    ms.uni_hex.push('0'); ms.uni_hex.push('3'); ms.uni_hex.push('b'); ms.uni_hex.push('b');
    var uout: u8[>..] = [];
    assert(modes::unicode_commit(ms, uout) && uout.len == 2 && uout[0] == 0xCE && uout[1] == 0xBB);
}

fn test_mouse_selection_modes() {
    var pal = colors::default_palette();
    var scr = screen::Screen { .. };
    screen::init(scr, 40, 4, pal.fg, pal.bg, 10, 0);
    var par = parser::Parser { .. };
    parser::feed_slice(par, scr, pal, "say \"hello world\" today\r\n123456");

    // Double-click word selection
    screen::select_word(scr, 6, 0, false);
    var out: u8[>..] = [];
    screen::extract_selection(scr, out);
    assert(out.len == 5); // "hello"

    // Triple-click quote selection
    screen::select_quote_or_line(scr, 6, 0);
    screen::extract_selection(scr, out);
    assert(out.len == 11); // "hello world"

    // Quad-click full line
    screen::select_line(scr, 1);
    screen::extract_selection(scr, out);
    assert(out.len == 6); // "123456"

    // Block selection
    scr.sel_active = true;
    scr.sel_mode = screen::SEL_BLOCK;
    scr.sel_x0 = 0; scr.sel_y0 = 0;
    scr.sel_x1 = 2; scr.sel_y1 = 1;
    assert(screen::is_selected(scr, 1, 1));
    assert(!screen::is_selected(scr, 3, 0));

    // Single line left-to-right selection from (5, 2) to (15, 2)
    scr.sel_mode = screen::SEL_CHAR;
    scr.sel_x0 = 5; scr.sel_y0 = 2;
    scr.sel_x1 = 15; scr.sel_y1 = 2;
    assert(screen::is_selected(scr, 5, 2));
    assert(screen::is_selected(scr, 10, 2));
    assert(screen::is_selected(scr, 15, 2));
    assert(!screen::is_selected(scr, 4, 2));
    assert(!screen::is_selected(scr, 16, 2));
    assert(!screen::is_selected(scr, 10, 1));

    // Single line right-to-left selection from (15, 2) to (5, 2)
    scr.sel_x0 = 15; scr.sel_y0 = 2;
    scr.sel_x1 = 5; scr.sel_y1 = 2;
    assert(screen::is_selected(scr, 10, 2));
    assert(screen::is_selected(scr, 5, 2));
    assert(screen::is_selected(scr, 15, 2));
    assert(!screen::is_selected(scr, 4, 2));
    assert(!screen::is_selected(scr, 16, 2));

    // Multi-line top-to-bottom selection from (10, 2) to (5, 4)
    scr.sel_x0 = 10; scr.sel_y0 = 2;
    scr.sel_x1 = 5; scr.sel_y1 = 4;
    assert(!screen::is_selected(scr, 9, 2));
    assert(screen::is_selected(scr, 10, 2));
    assert(screen::is_selected(scr, 39, 2));
    assert(screen::is_selected(scr, 0, 3));
    assert(screen::is_selected(scr, 39, 3));
    assert(screen::is_selected(scr, 5, 4));
    assert(!screen::is_selected(scr, 6, 4));

    // Multi-line bottom-to-top selection from (5, 4) to (10, 2)
    scr.sel_x0 = 5; scr.sel_y0 = 4;
    scr.sel_x1 = 10; scr.sel_y1 = 2;
    assert(!screen::is_selected(scr, 9, 2));
    assert(screen::is_selected(scr, 10, 2));
    assert(screen::is_selected(scr, 39, 2));
    assert(screen::is_selected(scr, 0, 3));
    assert(screen::is_selected(scr, 5, 4));
    assert(!screen::is_selected(scr, 6, 4));

    // Selection must never alter viewport scroll offset
    assert(scr.view_offset == 0);
}

fn test_shortcuts_and_goosy_ini() {
    assert(input::check_shortcut('C', ffi::MOD_CTRL | ffi::MOD_SHIFT) == input::ACT_COPY);
    assert(input::check_shortcut(input::XKB_XF86Copy, 0) == input::ACT_COPY);
    assert(input::check_shortcut(input::XKB_XF86Paste, 0) == input::ACT_PASTE_CLIP);
    assert(input::check_shortcut(input::XKB_Insert, ffi::MOD_SHIFT) == input::ACT_PASTE_PRIMARY);
    assert(input::check_shortcut('R', ffi::MOD_CTRL | ffi::MOD_SHIFT) == input::ACT_SEARCH_START);
    assert(input::check_shortcut('=', ffi::MOD_CTRL) == input::ACT_FONT_INC);
    assert(input::check_shortcut('+', ffi::MOD_CTRL) == input::ACT_FONT_INC);
    assert(input::check_shortcut('-', ffi::MOD_CTRL) == input::ACT_FONT_DEC);
    assert(input::check_shortcut('0', ffi::MOD_CTRL) == input::ACT_FONT_RST);
    assert(input::check_shortcut('N', ffi::MOD_CTRL | ffi::MOD_SHIFT) == input::ACT_SPAWN_TERM);
    assert(input::check_shortcut('O', ffi::MOD_CTRL | ffi::MOD_SHIFT) == input::ACT_URL_MODE);
    assert(input::check_shortcut('U', ffi::MOD_CTRL | ffi::MOD_SHIFT) == input::ACT_UNICODE_MODE);
    assert(input::check_shortcut('Z', ffi::MOD_CTRL | ffi::MOD_SHIFT) == input::ACT_PROMPT_PREV);
    assert(input::check_shortcut('X', ffi::MOD_CTRL | ffi::MOD_SHIFT) == input::ACT_PROMPT_NEXT);

    var cfg = config::default_config();
    config::parse_text(cfg, "[touch]\nlong-press-delay = 550\n[url]\nlabel-letters = asdf\n[colors]\nbackground = #1a1b26\nforeground = #c0caf5 # inline comment\nregular0 = #15161e\n");
    assert(cfg.touch_long_press_ms == 550);
    assert(cfg.url_label_letters.len == 4);
    assert(cfg.palette.bg == 0x1a1b26);
    assert(cfg.palette.fg == 0xc0caf5);
    assert(cfg.palette.ansi[0] == 0x15161e);
}

fn test_fastfetch_alignment_and_box_drawing() {
    var pal = colors::default_palette();
    var scr = screen::Screen { .. };
    screen::init(scr, 120, 30, pal.fg, pal.bg, 100, 0);
    var par = parser::Parser { .. };

    // Fastfetch line with Artix logo on left (col 0..43) and sysinfo on right (col 44..),
    // testing ITU colon truecolor syntax, bold SGR, and box-drawing horizontal lines (U+2500).
    let sample = "                    '                       \x1b[38:2::0;150;255mArtix Linux\x1b[0m\r\n                   'o'                      \x1b[1mSYSTEM\x1b[0m ────────────────────────────────────────────────────────\r\n";
    parser::feed_slice(par, scr, pal, sample);

    // Row 0 checks:
    let c_quote = screen::visible_cell(scr, 20, 0);
    assert(c_quote.cp == '\'');
    let c_a = screen::visible_cell(scr, 44, 0);
    assert(c_a.cp == 'A');
    assert(c_a.fg == 0x0096FF); // 0, 150, 255 RGB

    // Row 1 checks:
    let c_o = screen::visible_cell(scr, 20, 1);
    assert(c_o.cp == 'o');
    let c_sys = screen::visible_cell(scr, 44, 1);
    assert(c_sys.cp == 'S');
    let c_dash = screen::visible_cell(scr, 51, 1);
    assert(c_dash.cp == 0x2500);

    // Row 2 should be empty (no unwanted wrapping occurred)
    let c_r2 = screen::visible_cell(scr, 0, 2);
    assert(c_r2.cp == 0 || c_r2.cp == 0x20);
}

fn test_underlines_hyperlinks_and_reflow() {
    var pal = colors::default_palette();
    var scr = screen::Screen { .. };
    screen::init(scr, 30, 4, pal.fg, pal.bg, 20, 0);
    var par = parser::Parser { .. };

    // Test Underline styles: curly (4:3), double (4:2), dotted (4:4), dashed (4:5)
    // and custom underline truecolor (58;2;255;0;128)
    parser::feed_slice(par, scr, pal, "\x1b[4:3m\x1b[58;2;255;0;128mCurly\x1b[59m\x1b[24m\r\n");
    let c0 = screen::visible_cell(scr, 0, 0);
    assert(c0.ul_style == screen::UL_CURLY);
    assert(c0.ul_color == 0xFF0080);

    // Test OSC 8 hyperlinks
    parser::feed_slice(par, scr, pal, "\x1b]8;;https://artixlinux.org\x1b\\Link\x1b]8;;\x1b\\\r\n");
    let c_link = screen::visible_cell(scr, 0, 1);
    assert(c_link.link_id > 0);
    var link_out: u8[>..] = [];
    assert(screen::get_hyperlink(scr, c_link.link_id, link_out));
    assert(link_out.len == 22);

    // Test Scrollback Reflow: narrow to wide and wide to narrow
    var reflow_scr = screen::Screen { .. };
    screen::init(reflow_scr, 10, 4, pal.fg, pal.bg, 20, 0);
    var reflow_par = parser::Parser { .. };
    // "1234567890abcdef" on 10-col screen soft-wraps "abcdef" to row 1
    parser::feed_slice(reflow_par, reflow_scr, pal, "1234567890abcdef\r\n");
    assert(screen::visible_cell(reflow_scr, 0, 1).cp == 'a');
    assert(reflow_scr.wrapped_rows[0] == 1);

    // Now resize wider to 20 cols: logical line should unwrap and fit on row 0
    screen::resize(reflow_scr, 20, 4);
    assert(screen::visible_cell(reflow_scr, 10, 0).cp == 'a');
    assert(reflow_scr.wrapped_rows[0] == 0);
}

fn test_agy_escape_sequences_and_state() {
    var pal = colors::default_palette();
    var scr = screen::Screen { .. };
    screen::init(scr, 80, 24, pal.fg, pal.bg, 20, 0);
    var par = parser::Parser { .. };

    // 1. CSI > 4;2m (modifyOtherKeys) must NOT trigger underline
    parser::feed_slice(par, scr, pal, "\x1b[>4;2mHello\x1b[m");
    let c0 = screen::visible_cell(scr, 0, 0);
    assert((c0.attrs & screen::ATTR_UNDERLINE) == 0);
    assert(c0.ul_style == screen::UL_NONE);

    // 2. CSI > 5 u and CSI ? u must NOT jump cursor
    scr.saved_cx = 50; scr.saved_cy = 20;
    scr.cx = 5; scr.cy = 2;
    parser::feed_slice(par, scr, pal, "\x1b[>5u\x1b[?u");
    assert(scr.cx == 5 && scr.cy == 2); // Not jumped to 50, 20

    // 3. OSC 11 background query reply
    scr.reply_buf.clear();
    parser::feed_slice(par, scr, pal, "\x1b]11;?\x07");
    assert(scr.reply_buf.len > 0);

    // 4. Window size report CSI 14 t
    scr.reply_buf.clear();
    parser::feed_slice(par, scr, pal, "\x1b[14t");
    assert(scr.reply_buf.len > 0);

    // 5. Persistent geometry save & load
    ffi::state_save_geometry(115, 38);
    assert(ffi::state_load_cols() == 115);
    assert(ffi::state_load_rows() == 38);

    // 5b. CLI argument inspection helpers
    let _ = ffi::cmdline_cols();
    let _ = ffi::cmdline_rows();
    let _ = ffi::cmdline_has_custom_size();
    let _ = ffi::cmdline_hold();

    // 6. Compound SGR with 38;2;...;48;2;... (such as agy banner)
    parser::feed_slice(par, scr, pal, "\x1b[38;2;242;146;46;48;2;246;145;46mX\x1b[m");
    let c_comp = screen::visible_cell(scr, 5, 2);
    assert(c_comp.fg == 0xF2922E);
    assert(c_comp.bg == 0xF6912E);
}

fn test_rendering_glitches_and_unicode_width() {
    var pal = colors::default_palette();
    var scr = screen::Screen { .. };
    screen::init(scr, 80, 24, pal.fg, pal.bg, 20, 0);
    var par = parser::Parser { .. };

    // 1. Emoji / Symbol cell widths matching agy / Bubble Tea
    assert(utf8::cell_width(0x2728) == 2); // ✨
    assert(utf8::cell_width(0x26A1) == 2); // ⚡
    assert(utf8::cell_width(0x23F3) == 2); // ⏳
    assert(utf8::cell_width(0x2705) == 2); // ✅
    assert(utf8::cell_width(0x274C) == 2); // ❌
    assert(utf8::cell_width(0x2B50) == 2); // ⭐

    // 2. Cursor save/restore (ESC 7 / ESC 8) preserves SGR attributes and colors
    scr.cur_fg = 0x112233;
    scr.cur_bg = 0x445566;
    scr.cur_attrs = screen::ATTR_BOLD;
    parser::feed_slice(par, scr, pal, "\x1b7\x1b[38;2;255;0;0;48;2;0;255;0;1mTest\x1b8");
    assert(scr.cur_fg == 0x112233);
    assert(scr.cur_bg == 0x445566);
    assert(scr.cur_attrs == screen::ATTR_BOLD);

    // 3. Wide character boundary integrity
    // Writing 2-column emoji ✨ takes col 0 and 1
    parser::feed_slice(par, scr, pal, "\x1b[1;1H\xE2\x9C\xA8AB");
    assert(screen::visible_cell(scr, 0, 0).width == 2);
    assert(screen::visible_cell(scr, 1, 0).flags == screen::CELL_WIDE_CONT);
    assert(screen::visible_cell(scr, 2, 0).cp == 'A');
    assert(screen::visible_cell(scr, 3, 0).cp == 'B');

    // Overwriting col 0 with 1-width 'X' must clear continuation cell at col 1
    parser::feed_slice(par, scr, pal, "\x1b[1;1HX");
    let c0 = screen::visible_cell(scr, 0, 0);
    let c1 = screen::visible_cell(scr, 1, 0);
    assert(c0.cp == 'X' && c0.width == 1 && c0.flags == 0);
    assert(c1.cp == ' ' && c1.flags == 0);

    // Overwriting continuation cell (col 1) must clear left half at col 0
    parser::feed_slice(par, scr, pal, "\x1b[1;1H\xE2\x9C\xA8");
    parser::feed_slice(par, scr, pal, "\x1b[1;2HY");
    let c0_b = screen::visible_cell(scr, 0, 0);
    let c1_b = screen::visible_cell(scr, 1, 0);
    assert(c0_b.cp == ' ' && c0_b.width == 1);
    assert(c1_b.cp == 'Y' && c1_b.width == 1);

    // 4. Origin mode (DECOM) positioning and clamping
    parser::feed_slice(par, scr, pal, "\x1b[5;15r\x1b[?6h\x1b[1;1H");
    assert(scr.cy == 4); // Line 1 in origin mode maps to row 4 (0-based)
    screen::clamp_cursor(scr);
    assert(scr.cy == 4);

    // 5. ECMA-48 Repeat character (REP: CSI b)
    parser::feed_slice(par, scr, pal, "\x1b[?6l\x1b[1;24r\x1b[2;1HZ\x1b[4b");
    for col in 0..5 {
        let cz = screen::visible_cell(scr, col as i32, 1);
        assert(cz.cp == 'Z');
    }
    assert(scr.cx == 5);

    // 6. ECMA-48 HPR (CSI a) and VPR (CSI e)
    parser::feed_slice(par, scr, pal, "\x1b[1;1H\x1b[3a");
    assert(scr.cx == 3 && scr.cy == 0);
    parser::feed_slice(par, scr, pal, "\x1b[2e");
    assert(scr.cx == 3 && scr.cy == 2);

    // 7. ECMA-48 VPA (CSI d) respecting origin mode
    parser::feed_slice(par, scr, pal, "\x1b[?6l\x1b[10d");
    assert(scr.cy == 9);
    parser::feed_slice(par, scr, pal, "\x1b[5;15r\x1b[?6h\x1b[3d");
    assert(scr.cy == 6);
    parser::feed_slice(par, scr, pal, "\x1b[?6l\x1b[1;24r");
}

fn test_crlf_paste_and_resize_preservation() {
    var out: u8[>..] = [];
    input::format_paste(out, "hello\r\nworld\nfoo\rbar", false);
    // Should be "hello\rworld\rfoo\rbar" with no double \r\r (len = 19)
    assert(out.len == 19);
    assert(out[5] == '\r' && out[6] == 'w');
    assert(out[11] == '\r' && out[12] == 'f');
    assert(out[15] == '\r' && out[16] == 'b');

    var pal = colors::default_palette();
    var scr = screen::Screen { .. };
    screen::init(scr, 40, 4, pal.fg, pal.bg, 10, 0);
    scr.cy = 1;
    screen::mark_prompt_row(scr);
    assert(scr.prompt_rows[1] == 1);

    // Height-only resize: row 1 prompt mark must be preserved
    screen::resize(scr, 40, 8);
    assert(scr.prompt_rows.len == 8);
    assert(scr.prompt_rows[1] == 1);
}

fn test_kitty_keyboard_protocol() {
    var pal = colors::default_palette();
    var scr = screen::Screen { .. };
    screen::init(scr, 40, 4, pal.fg, pal.bg, 10, 0);
    var par = parser::Parser { .. };

    // Query flags: initial 0
    scr.reply_buf.clear();
    parser::feed_slice(par, scr, pal, "\x1b[?u");
    assert(scr.reply_buf.len == 5); // "\x1b[?0u"
    assert(scr.reply_buf[0] == 0x1B && scr.reply_buf[1] == '[' && scr.reply_buf[2] == '?' && scr.reply_buf[3] == '0' && scr.reply_buf[4] == 'u');

    // Set flags to 3
    parser::feed_slice(par, scr, pal, "\x1b[=3u");
    assert(scr.kitty_keyboard_flags == 3);

    // Query flags again: now 3
    scr.reply_buf.clear();
    parser::feed_slice(par, scr, pal, "\x1b[?u");
    assert(scr.reply_buf.len == 5);
    assert(scr.reply_buf[3] == '3');

    // Test key translation in standard mode (flags == 0)
    var key_out: u8[>..] = [];
    input::translate_key(key_out, input::XKB_Return, 0, [], false, 0);
    assert(key_out.len == 1 && key_out[0] == 0x0D);
    input::translate_key(key_out, input::XKB_KP_Enter, 0, [], false, 0);
    assert(key_out.len == 1 && key_out[0] == 0x0D);
    input::translate_key(key_out, input::XKB_Up, 0, [], false, 0);
    assert(key_out.len == 3 && key_out[0] == 0x1B && key_out[1] == '[' && key_out[2] == 'A');
    input::translate_key(key_out, input::XKB_ISO_Left_Tab, 0, [], false, 0);
    assert(key_out.len == 3 && key_out[0] == 0x1B && key_out[1] == '[' && key_out[2] == 'Z');

    // Test key translation in Kitty Keyboard mode (flags & 1 != 0)
    // Unmodified keys MUST NOT emit \x1b[...u to prevent typing literal 'u' in shells and REPLs
    input::translate_key(key_out, input::XKB_Return, 0, [], false, 1);
    assert(key_out.len == 1 && key_out[0] == 0x0D);
    input::translate_key(key_out, input::XKB_KP_Enter, 0, [], false, 1);
    assert(key_out.len == 1 && key_out[0] == 0x0D);
    input::translate_key(key_out, input::XKB_Escape, 0, [], false, 1);
    assert(key_out.len == 1 && key_out[0] == 0x1B);
    input::translate_key(key_out, input::XKB_Tab, 0, [], false, 1);
    assert(key_out.len == 1 && key_out[0] == 0x09);
    input::translate_key(key_out, input::XKB_BackSpace, 0, [], false, 1);
    assert(key_out.len == 1 && key_out[0] == 0x7F);

    // Modified keys under Kitty Keyboard disambiguation mode (flags & 1 != 0) emit CSI ... u
    input::translate_key(key_out, input::XKB_Return, ffi::MOD_SHIFT, [], false, 1);
    assert(key_out.len == 7 && key_out[0] == 0x1B && key_out[1] == '[' && key_out[2] == '1' && key_out[3] == '3' && key_out[4] == ';' && key_out[5] == '2' && key_out[6] == 'u');
    input::translate_key(key_out, input::XKB_Return, ffi::MOD_CTRL, [], false, 1);
    assert(key_out.len == 7 && key_out[0] == 0x1B && key_out[1] == '[' && key_out[2] == '1' && key_out[3] == '3' && key_out[4] == ';' && key_out[5] == '5' && key_out[6] == 'u');
    input::translate_key(key_out, input::XKB_Tab, ffi::MOD_SHIFT, [], false, 1);
    assert(key_out.len == 6 && key_out[0] == 0x1B && key_out[1] == '[' && key_out[2] == '9' && key_out[3] == ';' && key_out[4] == '2' && key_out[5] == 'u');
    input::translate_key(key_out, input::XKB_BackSpace, ffi::MOD_CTRL, [], false, 1);
    assert(key_out.len == 8 && key_out[0] == 0x1B && key_out[1] == '[' && key_out[2] == '1' && key_out[3] == '2' && key_out[4] == '7' && key_out[5] == ';' && key_out[6] == '5' && key_out[7] == 'u');
    input::translate_key(key_out, input::XKB_Up, 0, [], true, 1);
    assert(key_out.len == 3 && key_out[0] == 0x1B && key_out[1] == '[' && key_out[2] == 'A');

    // Test Kitty Keyboard push, pop, clear mode (mode 3), and DECSTR reset
    parser::feed_slice(par, scr, pal, "\x1b[>1u"); // push 1
    assert(scr.kitty_keyboard_flags == 1);
    assert(scr.kitty_keyboard_stack.len == 1);
    parser::feed_slice(par, scr, pal, "\x1b[<1u"); // pop 1
    assert(scr.kitty_keyboard_flags == 3);
    assert(scr.kitty_keyboard_stack.len == 0);
    parser::feed_slice(par, scr, pal, "\x1b[=3;3u"); // clear flags 3
    assert(scr.kitty_keyboard_flags == 0);
    parser::feed_slice(par, scr, pal, "\x1b[=1u"); // set flag 1
    assert(scr.kitty_keyboard_flags == 1);
    parser::feed_slice(par, scr, pal, "\x1b[!p"); // DECSTR soft reset
    assert(scr.kitty_keyboard_flags == 0);
}

fn test_command_output_selection_and_combining() {
    var pal = colors::default_palette();
    var scr = screen::Screen { .. };
    screen::init(scr, 40, 10, pal.fg, pal.bg, 20, 0);
    var par = parser::Parser { .. };

    // 1. Combining marks
    // 'e' (U+0065) + combining acute (U+0301 = 0xCC 0x81)
    parser::feed_slice(par, scr, pal, "e\xCC\x81");
    let c = screen::visible_cell(scr, 0, 0);
    assert(c.cp == 'e');
    assert(c.combining == 0x0301);
    assert(c.width == 1);

    // 2. OSC 133 semantic prompt output selection
    parser::feed_slice(par, scr, pal, "\r\n\x1b]133;A\x07$ ls\r\nfile1.txt\r\nfile2.txt\r\n\x1b]133;A\x07$ ");
    let ok = screen::select_last_command_output(scr);
    assert(ok);
    assert(scr.sel_active);
    assert(scr.sel_mode == screen::SEL_LINE);
    var sel_out: u8[>..] = [];
    screen::extract_selection(scr, sel_out);
    assert(sel_out.len > 0);
}

fn test_selection_scroll_and_multiscreen() {
    var pal = colors::default_palette();
    var scr = screen::Screen { .. };
    screen::init(scr, 20, 4, pal.fg, pal.bg, 20, 0);
    var par = parser::Parser { .. };

    // Emit 10 lines of text -> 6 lines in scrollback, 4 lines visible
    parser::feed_slice(par, scr, pal, "line0\r\nline1\r\nline2\r\nline3\r\nline4\r\nline5\r\nline6\r\nline7\r\nline8\r\nline9");
    assert(scr.sb_count == 6);

    // Initial visible screen has line6 (row 0), line7 (row 1), line8 (row 2), line9 (row 3)
    // Select "line7" on visible row 1:
    let abs_r1 = screen::visible_row_to_abs(scr, 1);
    assert(abs_r1 == 7); // line7 is abs_line 7
    scr.sel_active = true;
    scr.sel_mode = screen::SEL_CHAR;
    scr.sel_x0 = 0; scr.sel_y0 = abs_r1;
    scr.sel_x1 = 4; scr.sel_y1 = abs_r1;

    // Verify cell (0, 1) is selected:
    assert(screen::is_selected(scr, 0, 1));
    assert(screen::is_selected(scr, 4, 1));
    assert(!screen::is_selected(scr, 0, 0));

    // Scroll viewport up by 2:
    screen::scroll_viewport(scr, 2);
    assert(scr.view_offset == 2);
    // Now visible row 3 has line7 (row 3 + (6 - 2) = 7)!
    assert(screen::is_selected(scr, 0, 3));
    assert(screen::is_selected(scr, 4, 3));
    assert(!screen::is_selected(scr, 0, 1)); // row 1 is now line5, not selected!

    // Multi-screen selection extraction across scrollback and live screen:
    // Select from line2 (in scrollback) to line8 (in live screen)
    scr.sel_x0 = 0; scr.sel_y0 = 2;
    scr.sel_x1 = 4; scr.sel_y1 = 8;
    var sel_out: u8[>..] = [];
    screen::extract_selection(scr, sel_out);
    assert(sel_out.len > 0);

    // Alt-screen rectangular resize test:
    // Switch to alt screen
    parser::feed_slice(par, scr, pal, "\x1b[?1049h");
    assert(scr.alt_screen);
    parser::feed_slice(par, scr, pal, "\x1b[1;1HTOPLEFT\x1b[4;1HBOTTOM");
    let c_top = screen::visible_cell(scr, 0, 0);
    assert(c_top.cp == 'T');

    // Maximize / resize alt-screen:
    screen::resize(scr, 60, 12);
    assert(scr.cols == 60 && scr.rows == 12);
    let c_top_after = screen::visible_cell(scr, 0, 0);
    assert(c_top_after.cp == 'T'); // text preserved without reflow corruption

    // Exit alt screen
    parser::feed_slice(par, scr, pal, "\x1b[?1049l");
    assert(!scr.alt_screen);
    // Primary scrollback must remain intact
    assert(scr.sb_count == 6);
}

fn test_maximize_restore_preservation() {
    var pal = colors::default_palette();
    var scr = screen::Screen { .. };
    screen::init(scr, 80, 24, pal.fg, pal.bg, 100, 0);
    var par = parser::Parser { .. };

    // 1. Terminal with 3 lines of output and trailing blank rows
    parser::feed_slice(par, scr, pal, "echo hello\r\nhello\r\n$ ");
    assert(scr.cy == 2 && scr.cx == 2);
    assert(scr.sb_count == 0);
    assert(screen::visible_cell(scr, 0, 0).cp == 'e');
    assert(screen::visible_cell(scr, 0, 1).cp == 'h');
    assert(screen::visible_cell(scr, 0, 2).cp == '$');

    // Maximize window to 160x50:
    screen::resize(scr, 160, 50);
    assert(scr.cols == 160 && scr.rows == 50);
    // Cursor MUST remain on the prompt line ($ )
    assert(scr.cy == 2 && scr.cx == 2);
    // Content must NOT be duplicated or pushed into scrollback
    assert(scr.sb_count == 0);
    assert(screen::visible_cell(scr, 0, 0).cp == 'e');
    assert(screen::visible_cell(scr, 0, 1).cp == 'h');
    assert(screen::visible_cell(scr, 0, 2).cp == '$');
    // Row 3 and beyond must be blank
    assert(screen::visible_cell(scr, 0, 3).cp == 0x20);
    assert(screen::visible_cell(scr, 0, 49).cp == 0x20);

    // Restore window back to 80x24:
    screen::resize(scr, 80, 24);
    assert(scr.cols == 80 && scr.rows == 24);
    assert(scr.cy == 2 && scr.cx == 2);
    assert(scr.sb_count == 0);
    assert(screen::visible_cell(scr, 0, 0).cp == 'e');
    assert(screen::visible_cell(scr, 0, 1).cp == 'h');
    assert(screen::visible_cell(scr, 0, 2).cp == '$');

    // Maximize again: must still be completely stable
    screen::resize(scr, 160, 50);
    assert(scr.cy == 2 && scr.cx == 2);
    assert(scr.sb_count == 0);
    assert(screen::visible_cell(scr, 0, 2).cp == '$');
    screen::resize(scr, 80, 24);
    assert(scr.cy == 2 && scr.cx == 2);
    assert(scr.sb_count == 0);

    // 2. Wrapped command line reflow and cursor tracking
    var wrap_scr = screen::Screen { .. };
    screen::init(wrap_scr, 40, 10, pal.fg, pal.bg, 50, 0);
    var wrap_par = parser::Parser { .. };
    // Type a 60-character line (wraps across 40 cols: 40 on row 0, 20 on row 1)
    parser::feed_slice(wrap_par, wrap_scr, pal, "012345678901234567890123456789012345678901234567890123456789");
    assert(wrap_scr.cy == 1 && wrap_scr.cx == 20);
    assert(wrap_scr.wrapped_rows[0] == 1);

    // Maximize to 80 cols: the 60 characters unwrap to fit on row 0!
    screen::resize(wrap_scr, 80, 20);
    assert(wrap_scr.wrapped_rows[0] == 0);
    assert(wrap_scr.cy == 0 && wrap_scr.cx == 60);

    // Restore to 40 cols: wraps back to 2 rows, cursor at row 1 col 20!
    screen::resize(wrap_scr, 40, 10);
    assert(wrap_scr.wrapped_rows[0] == 1);
    assert(wrap_scr.cy == 1 && wrap_scr.cx == 20);
}

fn main() {
    test_utf8_decoding();
    test_osc7_and_osc133_prompt_jumps();
    test_search_url_and_unicode_modes();
    test_mouse_selection_modes();
    test_shortcuts_and_goosy_ini();
    test_fastfetch_alignment_and_box_drawing();
    test_underlines_hyperlinks_and_reflow();
    test_agy_escape_sequences_and_state();
    test_rendering_glitches_and_unicode_width();
    test_crlf_paste_and_resize_preservation();
    test_kitty_keyboard_protocol();
    test_command_output_selection_and_combining();
    test_selection_scroll_and_multiscreen();
    test_maximize_restore_preservation();
    print("goosy: all Goose unit tests passed");
}
