// Goot — Pure Goose Terminal Engine Unit Test Suite
// Covers UTF-8, SGR, scrollback, alt screen, OSC 7/133/52, Search/URL/Unicode modes,
// multi-click/block/quote/extend selection, Foot-compatible shortcuts, and goot.ini parsing.

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
    var par = parser::new_parser(false);

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
    var par = parser::new_parser(false);
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
    var par = parser::new_parser(false);
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
}

fn test_shortcuts_and_goot_ini() {
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
    var par = parser::new_parser(false);

    // Fastfetch line with Artix logo on left (col 0..43) and sysinfo on right (col 44..),
    // testing ITU colon truecolor syntax, bold SGR, and box-drawing horizontal lines (U+2500).
    let sample = "                    '                       \x1b[38:2::0;150;255mArtix Linux\x1b[0m\r\n                   'o'                      \x1b[1mSYSTEM\x1b[0m ────────────────────────────────────────────────────────\r\n";
    parser::feed_slice(par, scr, pal, sample);

    // Row 0 checks:
    let c_quote = screen::visible_cell(scr, 20, 0);
    assert(c_quote.cp == '\'' as u32);
    let c_a = screen::visible_cell(scr, 44, 0);
    assert(c_a.cp == 'A' as u32);
    assert(c_a.fg == 0x0096FF); // 0, 150, 255 RGB

    // Row 1 checks:
    let c_o = screen::visible_cell(scr, 20, 1);
    assert(c_o.cp == 'o' as u32);
    let c_sys = screen::visible_cell(scr, 44, 1);
    assert(c_sys.cp == 'S' as u32);
    let c_dash = screen::visible_cell(scr, 51, 1);
    assert(c_dash.cp == 0x2500);

    // Row 2 should be empty (no unwanted wrapping occurred)
    let c_r2 = screen::visible_cell(scr, 0, 2);
    assert(c_r2.cp == 0 || c_r2.cp == 0x20);
}

fn main() {
    test_utf8_decoding();
    test_osc7_and_osc133_prompt_jumps();
    test_search_url_and_unicode_modes();
    test_mouse_selection_modes();
    test_shortcuts_and_goot_ini();
    test_fastfetch_alignment_and_box_drawing();
    print("goot: all Goose unit tests passed");
}
