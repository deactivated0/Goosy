// Goot — VT100/VT220/ECMA-48 State-Machine Escape Sequence Parser
// Supports OSC 0/2 (title), OSC 7 (working directory), OSC 133;A (prompt marks),
// and disables OSC 52 clipboard writes by default.

namespace parser;

import src.colors;
import src.ffi;
import src.screen;
import src.utf8;

let ST_GROUND     = 0;
let ST_ESC        = 1;
let ST_ESC_INT    = 2;
let ST_CSI_ENTRY  = 3;
let ST_CSI_PARAM  = 4;
let ST_CSI_IGNORE = 5;
let ST_OSC_STRING = 6;
let ST_DCS_IGNORE = 7;
let ST_DCS_DATA   = 8;
let ST_APC_DATA   = 9;

let MAX_PARAMS = 16;
let MAX_OSC    = 512;

root struct Parser {
    state: i32 = 0,
    utf8_dec: utf8::Decoder = utf8::Decoder { .. },
    params: i32[16] = [0; 16],
    is_subparam: bool[16] = [false; 16],
    param_count: i32 = 0,
    cur_param: i32 = 0,
    has_param: bool = false,
    next_is_sub: bool = false,
    private_prefix: u8 = 0,
    intermediate: u8 = 0,
    osc_esc_seen: bool = false,
    esc_seen: bool = false,
    osc_buf: u8[..512] = [],
    dcs_buf: u8[>..] = [],
    apc_buf: u8[>..] = [],
    allow_osc52: bool = false,
}

fn init(p: Parser&, allow_osc52: bool) {
    p.state = 0;
    p.param_count = 0;
    p.cur_param = 0;
    p.has_param = false;
    p.next_is_sub = false;
    p.private_prefix = 0;
    p.intermediate = 0;
    p.osc_esc_seen = false;
    p.esc_seen = false;
    p.osc_buf.clear();
    p.dcs_buf.clear();
    p.apc_buf.clear();
    p.allow_osc52 = allow_osc52;
    for i in 0..MAX_PARAMS { p.params[i] = 0; p.is_subparam[i] = false; }
}

fn clear_csi(p: Parser&) {
    for i in 0..MAX_PARAMS { p.params[i] = 0; p.is_subparam[i] = false; }
    p.param_count = 0;
    p.cur_param = 0;
    p.has_param = false;
    p.next_is_sub = false;
    p.private_prefix = 0;
    p.intermediate = 0;
}

fn param_or(p: Parser&, idx: i32, def: i32) -> i32 {
    if idx < 0 || idx >= p.param_count { return def; }
    let v = p.params[idx];
    return if v == 0 { def } else { v };
}

fn apply_sgr(p: Parser&, s: screen::Screen&, pal: colors::Palette&) {
    if p.param_count == 0 {
        s.cur_fg = pal.fg; s.cur_bg = pal.bg; s.cur_attrs = 0;
        s.cur_ul_style = screen::UL_NONE; s.cur_ul_color = 0;
        return;
    }

    var i: i32 = 0;
    while i < p.param_count {
        let code = p.params[i];
        match code {
            0 => {
                s.cur_fg = pal.fg; s.cur_bg = pal.bg; s.cur_attrs = 0;
                s.cur_ul_style = screen::UL_NONE; s.cur_ul_color = 0;
            },
            1 => { s.cur_attrs = s.cur_attrs | screen::ATTR_BOLD; },
            2 => { s.cur_attrs = s.cur_attrs | screen::ATTR_DIM; },
            3 => { s.cur_attrs = s.cur_attrs | screen::ATTR_ITALIC; },
            4 => {
                if i + 1 < p.param_count && p.is_subparam[i + 1] && (p.params[i + 1] >= 0 && p.params[i + 1] <= 5) {
                    let sub = p.params[i + 1];
                    if sub == 0 {
                        s.cur_attrs = s.cur_attrs & ~screen::ATTR_UNDERLINE;
                        s.cur_ul_style = screen::UL_NONE;
                    } else {
                        s.cur_attrs = s.cur_attrs | screen::ATTR_UNDERLINE;
                        s.cur_ul_style = sub as! u8;
                    }
                    i += 1;
                } else {
                    s.cur_attrs = s.cur_attrs | screen::ATTR_UNDERLINE;
                    s.cur_ul_style = screen::UL_SINGLE;
                }
            },
            5, 6 => { s.cur_attrs = s.cur_attrs | screen::ATTR_BLINK; },
            7 => { s.cur_attrs = s.cur_attrs | screen::ATTR_INVERSE; },
            8 => { s.cur_attrs = s.cur_attrs | screen::ATTR_HIDDEN; },
            9 => { s.cur_attrs = s.cur_attrs | screen::ATTR_STRIKE; },
            22 => { s.cur_attrs = s.cur_attrs & ~(screen::ATTR_BOLD | screen::ATTR_DIM); },
            23 => { s.cur_attrs = s.cur_attrs & ~screen::ATTR_ITALIC; },
            24 => {
                s.cur_attrs = s.cur_attrs & ~screen::ATTR_UNDERLINE;
                s.cur_ul_style = screen::UL_NONE;
                s.cur_ul_color = 0;
            },
            25 => { s.cur_attrs = s.cur_attrs & ~screen::ATTR_BLINK; },
            27 => { s.cur_attrs = s.cur_attrs & ~screen::ATTR_INVERSE; },
            28 => { s.cur_attrs = s.cur_attrs & ~screen::ATTR_HIDDEN; },
            29 => { s.cur_attrs = s.cur_attrs & ~screen::ATTR_STRIKE; },
            30..38 => { s.cur_fg = pal.ansi[code - 30]; },
            39 => { s.cur_fg = pal.fg; },
            40..48 => { s.cur_bg = pal.ansi[code - 40]; },
            49 => { s.cur_bg = pal.bg; },
            58 => {
                if i + 2 < p.param_count && p.params[i + 1] == 5 {
                    s.cur_ul_color = colors::resolve_256(pal, p.params[i + 2]);
                    i += 2;
                } else if i + 4 < p.param_count && p.params[i + 1] == 2 {
                    if i + 5 < p.param_count && p.is_subparam[i + 1] && p.is_subparam[i + 2] {
                        s.cur_ul_color = colors::pack_rgb(p.params[i + 3] as u32, p.params[i + 4] as u32, p.params[i + 5] as u32);
                        i += 5;
                    } else {
                        s.cur_ul_color = colors::pack_rgb(p.params[i + 2] as u32, p.params[i + 3] as u32, p.params[i + 4] as u32);
                        i += 4;
                    }
                }
            },
            59 => {
                s.cur_ul_color = 0;
            },
            90..98 => { s.cur_fg = pal.ansi[code - 90 + 8]; },
            100..108 => { s.cur_bg = pal.ansi[code - 100 + 8]; },
            38, 48 => {
                let is_fg = (code == 38);
                if i + 2 < p.param_count && p.params[i + 1] == 5 {
                    let rgb = colors::resolve_256(pal, p.params[i + 2]);
                    if is_fg { s.cur_fg = rgb; } else { s.cur_bg = rgb; }
                    i += 2;
                } else if i + 4 < p.param_count && p.params[i + 1] == 2 {
                    if i + 5 < p.param_count && p.is_subparam[i + 1] && p.is_subparam[i + 2] {
                        let rgb = colors::pack_rgb(p.params[i + 3] as u32, p.params[i + 4] as u32, p.params[i + 5] as u32);
                        if is_fg { s.cur_fg = rgb; } else { s.cur_bg = rgb; }
                        i += 5;
                    } else {
                        let rgb = colors::pack_rgb(p.params[i + 2] as u32, p.params[i + 3] as u32, p.params[i + 4] as u32);
                        if is_fg { s.cur_fg = rgb; } else { s.cur_bg = rgb; }
                        i += 4;
                    }
                }
            },
            _ => {},
        }
        i += 1;
    }
}

fn apply_dec_private_mode(s: screen::Screen&, mode: i32, enable: bool) {
    match mode {
        1 => { s.app_cursor = enable; },
        6 => { s.origin_mode = enable; screen::set_cursor(s, 0, if enable { s.scroll_top } else { 0 }); },
        7 => { s.autowrap = enable; if !enable { s.wrap_pending = false; } },
        25 => { s.cursor_visible = enable; screen::mark_row_dirty(s, s.cy); },
        47, 1047 => {
            if enable { screen::enter_alt_screen(s, false, false); }
            else { screen::leave_alt_screen(s, false); }
        },
        1049 => {
            if enable { screen::enter_alt_screen(s, true, true); }
            else { screen::leave_alt_screen(s, true); }
        },
        1000, 1002, 1003 => { s.mouse_mode = if enable { mode } else { 0 }; },
        1006 => { s.mouse_sgr = enable; },
        2004 => { s.bracketed_paste = enable; },
        _ => {},
    }
}

fn push_reply_int(out: u8[>..]&, v: i32) {
    var n = if v < 0 { 0 } else { v };
    var digits: u8[..12] = [];
    if n == 0 { out.push('0'); return; }
    while n > 0 {
        digits.push(('0' as i32 + (n % 10)) as! u8);
        n = n / 10;
    }
    var k: i64 = digits.len - 1;
    while k >= 0 { out.push(digits[k]); k -= 1; }
}

fn dispatch_csi(p: Parser&, s: screen::Screen&, pal: colors::Palette&, final_b: u8) {
    if p.has_param && p.param_count < MAX_PARAMS {
        p.params[p.param_count] = p.cur_param;
        p.is_subparam[p.param_count] = p.next_is_sub;
        p.param_count += 1;
    }

    if p.private_prefix == '?' {
        if final_b == 'h' || final_b == 'l' {
            let enable = (final_b == 'h');
            for i in 0..p.param_count { apply_dec_private_mode(s, p.params[i], enable); }
        } else if final_b == 'u' {
            s.reply_buf.append("\x1b[?0u");
        } else if p.intermediate == '$' && final_b == 'p' {
            if p.param_count > 0 {
                s.reply_buf.append("\x1b[?");
                push_reply_int(s.reply_buf, p.params[0]);
                s.reply_buf.append(";0$y");
            }
        }
        return;
    }

    if p.private_prefix == '>' {
        if final_b == 'c' {
            s.reply_buf.append("\x1b[>0;10;0c");
        }
        return;
    }

    if p.private_prefix != 0 {
        return;
    }

    if p.intermediate == ' ' && final_b == 'q' {
        let st = param_or(p, 0, 1);
        match st {
            0, 1, 2 => { s.cursor_style = 0; },
            3, 4 => { s.cursor_style = 1; },
            5, 6 => { s.cursor_style = 2; },
            _ => {},
        }
        screen::mark_row_dirty(s, s.cy);
        return;
    }

    if p.intermediate != 0 {
        return;
    }

    match final_b {
        'A' => { screen::set_cursor(s, s.cx, s.cy - param_or(p, 0, 1)); },
        'B' => { screen::set_cursor(s, s.cx, s.cy + param_or(p, 0, 1)); },
        'C' => { screen::set_cursor(s, s.cx + param_or(p, 0, 1), s.cy); },
        'D' => { screen::set_cursor(s, s.cx - param_or(p, 0, 1), s.cy); },
        'E' => { screen::set_cursor(s, 0, s.cy + param_or(p, 0, 1)); },
        'F' => { screen::set_cursor(s, 0, s.cy - param_or(p, 0, 1)); },
        'G', '`' => { screen::set_cursor(s, param_or(p, 0, 1) - 1, s.cy); },
        'd' => { screen::set_cursor(s, s.cx, param_or(p, 0, 1) - 1); },
        'H', 'f' => {
            let row = param_or(p, 0, 1) - 1;
            let col = param_or(p, 1, 1) - 1;
            let y = if s.origin_mode { s.scroll_top + row } else { row };
            screen::set_cursor(s, col, y);
        },
        'J' => { screen::erase_display(s, if p.param_count > 0 { p.params[0] } else { 0 }); },
        'K' => { screen::erase_line(s, if p.param_count > 0 { p.params[0] } else { 0 }); },
        'L' => { screen::insert_lines(s, param_or(p, 0, 1)); },
        'M' => { screen::delete_lines(s, param_or(p, 0, 1)); },
        '@' => { screen::insert_chars(s, param_or(p, 0, 1)); },
        'P' => { screen::delete_chars(s, param_or(p, 0, 1)); },
        'X' => { screen::erase_chars(s, param_or(p, 0, 1)); },
        'S' => { screen::scroll_up(s, s.scroll_top, s.scroll_bottom, param_or(p, 0, 1)); },
        'T' => { screen::scroll_down(s, s.scroll_top, s.scroll_bottom, param_or(p, 0, 1)); },
        'm' => { apply_sgr(p, s, pal); },
        'r' => {
            let top = param_or(p, 0, 1) - 1;
            let bot = param_or(p, 1, s.rows) - 1;
            if top >= 0 && bot < s.rows && top < bot {
                s.scroll_top = top; s.scroll_bottom = bot;
                screen::set_cursor(s, 0, if s.origin_mode { top } else { 0 });
            }
        },
        's' => { s.saved_cx = s.cx; s.saved_cy = s.cy; },
        'u' => { screen::set_cursor(s, s.saved_cx, s.saved_cy); },
        'n' => {
            if p.param_count > 0 && p.params[0] == 6 {
                s.reply_buf.append("\x1b[");
                push_reply_int(s.reply_buf, s.cy + 1);
                s.reply_buf.push(';');
                push_reply_int(s.reply_buf, s.cx + 1);
                s.reply_buf.push('R');
            } else if p.param_count > 0 && p.params[0] == 5 {
                s.reply_buf.append("\x1b[0n");
            }
        },
        'c' => { if p.private_prefix == 0 { s.reply_buf.append("\x1b[?62;22c"); } },
        't' => {
            if p.param_count > 0 && p.params[0] == 14 {
                let cw = ffi::font_cell_width();
                let ch = ffi::font_cell_height();
                s.reply_buf.append("\x1b[4;");
                push_reply_int(s.reply_buf, s.rows * ch);
                s.reply_buf.push(';');
                push_reply_int(s.reply_buf, s.cols * cw);
                s.reply_buf.push('t');
            } else if p.param_count > 0 && p.params[0] == 18 {
                s.reply_buf.append("\x1b[8;");
                push_reply_int(s.reply_buf, s.rows);
                s.reply_buf.push(';');
                push_reply_int(s.reply_buf, s.cols);
                s.reply_buf.push('t');
            }
        },
        _ => {},
    }
}

fn hex_val(c: u8) -> i32 {
    if c >= '0' && c <= '9' { return (c - '0') as i32; }
    if c >= 'a' && c <= 'f' { return (c - 'a') as i32 + 10; }
    if c >= 'A' && c <= 'F' { return (c - 'A') as i32 + 10; }
    return -1;
}

// Parse OSC 7 URI: "file://<hostname>/path/to/dir" -> decode %XX into s.cwd
fn parse_osc7_cwd(s: screen::Screen&, uri: const u8[:]) {
    if uri.len < 8 { return; }
    if uri[0] != 'f' || uri[1] != 'i' || uri[2] != 'l' || uri[3] != 'e' ||
       uri[4] != ':' || uri[5] != '/' || uri[6] != '/' {
        return;
    }
    // Find the '/' that begins the path after hostname
    var slash: i64 = 7;
    while slash < uri.len && uri[slash] != '/' { slash += 1; }
    if slash >= uri.len { return; }

    s.cwd.clear();
    var i = slash;
    while i < uri.len && s.cwd.len < 511 {
        if uri[i] == '%' && i + 2 < uri.len {
            let hi = hex_val(uri[i + 1]);
            let lo = hex_val(uri[i + 2]);
            if hi >= 0 && lo >= 0 {
                let dec = ((hi << 4) | lo) as! u8;
                if dec != 0 { s.cwd.push(dec); }
                i += 3;
                continue;
            }
        }
        s.cwd.push(uri[i]);
        i += 1;
    }
}

fn hex_nibble(v: u32) -> u8 {
    let nib = (v & 0xF) as u8;
    return if nib < 10 { '0' + nib } else { 'a' + (nib - 10) };
}

fn push_reply_hex2(out: u8[>..]&, v: u32) {
    let byte_val = v & 0xFF;
    let hi = hex_nibble(byte_val >> 4);
    let lo = hex_nibble(byte_val & 0xF);
    out.push(hi); out.push(lo); out.push(hi); out.push(lo);
}

fn dispatch_osc(p: Parser&, s: screen::Screen&) {
    if p.osc_buf.len < 2 { return; }
    var semi: i64 = -1;
    for i in 0..p.osc_buf.len {
        if p.osc_buf[i] == ';' { semi = i; break; }
    }
    if semi <= 0 { return; }

    var cmd: i32 = 0;
    for i in 0..semi {
        let c = p.osc_buf[i];
        if c < '0' || c > '9' { return; }
        cmd = cmd * 10 + ((c - '0') as i32);
        if cmd > 10000 { return; }
    }

    let payload = p.osc_buf[(semi + 1)..];
    if cmd == 0 || cmd == 2 {
        s.title.clear();
        for i in 0..payload.len {
            let b = payload[i];
            if b >= 0x20 && b != 0x7F && s.title.len < 255 { s.title.push(b); }
        }
        s.title_dirty = true;
    } else if cmd == 7 {
        parse_osc7_cwd(s, payload);
    } else if cmd == 8 {
        // OSC 8: Hyperlinks: \x1b]8;params;url\x1b\
        var semi2: i64 = -1;
        for i in 0..payload.len {
            if payload[i] == ';' { semi2 = i; break; }
        }
        if semi2 >= 0 {
            let url = payload[(semi2 + 1)..];
            if url.len == 0 {
                s.cur_link_id = 0;
            } else {
                s.cur_link_id = screen::add_hyperlink(s, url);
            }
        }
    } else if cmd == 10 {
        if payload.len >= 1 && payload[0] == '?' {
            s.reply_buf.append("\x1b]10;rgb:");
            push_reply_hex2(s.reply_buf, (s.default_fg >> 16) & 0xFF);
            s.reply_buf.push('/');
            push_reply_hex2(s.reply_buf, (s.default_fg >> 8) & 0xFF);
            s.reply_buf.push('/');
            push_reply_hex2(s.reply_buf, s.default_fg & 0xFF);
            s.reply_buf.append("\x1b\\");
        }
    } else if cmd == 11 {
        if payload.len >= 1 && payload[0] == '?' {
            s.reply_buf.append("\x1b]11;rgb:");
            push_reply_hex2(s.reply_buf, (s.default_bg >> 16) & 0xFF);
            s.reply_buf.push('/');
            push_reply_hex2(s.reply_buf, (s.default_bg >> 8) & 0xFF);
            s.reply_buf.push('/');
            push_reply_hex2(s.reply_buf, s.default_bg & 0xFF);
            s.reply_buf.append("\x1b\\");
        }
    } else if cmd == 133 {
        // Shell integration: OSC 133;A marks prompt start for Ctrl+Shift+Z/X
        if payload.len >= 1 && payload[0] == 'A' {
            screen::mark_prompt_row(s);
        }
    } else if cmd == 52 && !p.allow_osc52 {
        return;
    }
}

fn handle_c0(s: screen::Screen&, b: u8) {
    match b {
        0x07 => {},
        0x08 => { s.wrap_pending = false; if s.cx > 0 { s.cx -= 1; screen::mark_row_dirty(s, s.cy); } },
        0x09 => { screen::tab_forward(s); },
        0x0A, 0x0B, 0x0C => { screen::linefeed(s); },
        0x0D => { s.wrap_pending = false; s.cx = 0; screen::mark_row_dirty(s, s.cy); },
        _ => {},
    }
}

fn feed_byte(p: Parser&, s: screen::Screen&, pal: colors::Palette&, b: u8) {
    if b == 0x18 || b == 0x1A {
        p.state = ST_GROUND;
        utf8::reset(p.utf8_dec);
        return;
    }

    if p.state == ST_GROUND {
        if b == 0x1B { utf8::reset(p.utf8_dec); p.state = ST_ESC; return; }
        if b < 0x20 { handle_c0(s, b); return; }
        if b == 0x7F { return; }
        let cp, ready = utf8::feed(p.utf8_dec, b);
        if ready { screen::put_codepoint(s, cp); }
        return;
    }

    if p.state == ST_ESC {
        match b {
            '[' => { clear_csi(p); p.state = ST_CSI_ENTRY; },
            ']' => { p.osc_buf.clear(); p.osc_esc_seen = false; p.state = ST_OSC_STRING; },
            'P' => { p.dcs_buf.clear(); p.esc_seen = false; p.state = ST_DCS_DATA; },
            '_' => { p.apc_buf.clear(); p.esc_seen = false; p.state = ST_APC_DATA; },
            '^' => { p.state = ST_DCS_IGNORE; },
            '7' => { s.saved_cx = s.cx; s.saved_cy = s.cy; p.state = ST_GROUND; },
            '8' => { screen::set_cursor(s, s.saved_cx, s.saved_cy); p.state = ST_GROUND; },
            'D' => { screen::linefeed(s); p.state = ST_GROUND; },
            'E' => { s.cx = 0; screen::linefeed(s); p.state = ST_GROUND; },
            'M' => { screen::reverse_index(s); p.state = ST_GROUND; },
            'H' => { if (s.cx as i64) < s.tab_stops.len { s.tab_stops[s.cx] = 1; } p.state = ST_GROUND; },
            'c' => { screen::init(s, s.cols, s.rows, pal.fg, pal.bg, s.sb_cap_lines, s.cursor_style); ffi::image_clear(); p.state = ST_GROUND; },
            '(', ')', '*', '+', '#', '%' => { p.state = ST_ESC_INT; },
            _ => { p.state = ST_GROUND; },
        }
        return;
    }

    if p.state == ST_ESC_INT {
        if b >= 0x30 && b <= 0x7E { p.state = ST_GROUND; }
        return;
    }

    if p.state == ST_CSI_ENTRY || p.state == ST_CSI_PARAM {
        if b < 0x20 { handle_c0(s, b); return; }
        if p.state == ST_CSI_ENTRY && (b == '?' || b == '>' || b == '=' || b == '<') {
            p.private_prefix = b; p.state = ST_CSI_PARAM; return;
        }
        if b >= '0' && b <= '9' {
            p.state = ST_CSI_PARAM; p.has_param = true;
            if p.cur_param < 100000 { p.cur_param = p.cur_param * 10 + ((b - '0') as i32); }
            return;
        }
        if b == ';' || b == ':' {
            p.state = ST_CSI_PARAM;
            if p.param_count < MAX_PARAMS {
                p.params[p.param_count] = p.cur_param;
                p.is_subparam[p.param_count] = p.next_is_sub;
                p.param_count += 1;
            }
            p.cur_param = 0;
            p.has_param = false;
            p.next_is_sub = (b == ':');
            return;
        }
        if b >= 0x20 && b <= 0x2F { p.intermediate = b; p.state = ST_CSI_PARAM; return; }
        if b >= 0x40 && b <= 0x7E { dispatch_csi(p, s, pal, b); p.state = ST_GROUND; return; }
        p.state = ST_CSI_IGNORE;
        return;
    }

    if p.state == ST_CSI_IGNORE {
        if b >= 0x40 && b <= 0x7E { p.state = ST_GROUND; }
        return;
    }

    if p.state == ST_OSC_STRING {
        if b == 0x07 { dispatch_osc(p, s); p.state = ST_GROUND; return; }
        if p.osc_esc_seen {
            p.osc_esc_seen = false;
            if b == '\\' { dispatch_osc(p, s); p.state = ST_GROUND; return; }
        }
        if b == 0x1B { p.osc_esc_seen = true; return; }
        if p.osc_buf.len < (MAX_OSC - 1) as i64 { p.osc_buf.push(b); }
        return;
    }

    if p.state == ST_DCS_IGNORE {
        if b == 0x1B || b == 0x9C || b == 0x07 { p.state = ST_GROUND; }
        return;
    }

    if p.state == ST_DCS_DATA {
        if b == 0x07 {
            if p.dcs_buf.len > 0 && p.dcs_buf[0] == 'q' {
                let cw = ffi::font_cell_width();
                let ch = ffi::font_cell_height();
                let abs_r = if s.alt_screen { s.cy } else { s.sb_count + s.cy };
                let rows_cov = ffi::sixel_decode(p.dcs_buf, s.cx, abs_r, cw, ch);
                if rows_cov > 0 {
                    for _ in 0..rows_cov { screen::linefeed(s); }
                }
            } else if p.dcs_buf.len >= 2 && p.dcs_buf[0] == '+' && p.dcs_buf[1] == 'q' {
                s.reply_buf.append("\x1bP0+r\x1b\\");
            }
            p.state = ST_GROUND;
            return;
        }
        if p.esc_seen {
            p.esc_seen = false;
            if b == '\\' {
                if p.dcs_buf.len > 0 && p.dcs_buf[0] == 'q' {
                    let cw = ffi::font_cell_width();
                    let ch = ffi::font_cell_height();
                    let abs_r = if s.alt_screen { s.cy } else { s.sb_count + s.cy };
                    let rows_cov = ffi::sixel_decode(p.dcs_buf, s.cx, abs_r, cw, ch);
                    if rows_cov > 0 {
                        for _ in 0..rows_cov { screen::linefeed(s); }
                    }
                } else if p.dcs_buf.len >= 2 && p.dcs_buf[0] == '+' && p.dcs_buf[1] == 'q' {
                    s.reply_buf.append("\x1bP0+r\x1b\\");
                }
                p.state = ST_GROUND;
                return;
            }
        }
        if b == 0x1B { p.esc_seen = true; return; }
        if p.dcs_buf.len < 1048576 { p.dcs_buf.push(b); }
        return;
    }

    if p.state == ST_APC_DATA {
        if b == 0x07 {
            let cw = ffi::font_cell_width();
            let ch = ffi::font_cell_height();
            let abs_r = if s.alt_screen { s.cy } else { s.sb_count + s.cy };
            if p.apc_buf.len > 1 && p.apc_buf[0] == 'G' {
                let rows_cov = ffi::kitty_graphics(p.apc_buf[1..], s.cx, abs_r, cw, ch);
                if rows_cov > 0 {
                    for _ in 0..rows_cov { screen::linefeed(s); }
                }
            }
            p.state = ST_GROUND;
            return;
        }
        if p.esc_seen {
            p.esc_seen = false;
            if b == '\\' {
                let cw = ffi::font_cell_width();
                let ch = ffi::font_cell_height();
                let abs_r = if s.alt_screen { s.cy } else { s.sb_count + s.cy };
                if p.apc_buf.len > 1 && p.apc_buf[0] == 'G' {
                    let rows_cov = ffi::kitty_graphics(p.apc_buf[1..], s.cx, abs_r, cw, ch);
                    if rows_cov > 0 {
                        for _ in 0..rows_cov { screen::linefeed(s); }
                    }
                }
                p.state = ST_GROUND;
                return;
            }
        }
        if b == 0x1B { p.esc_seen = true; return; }
        if p.apc_buf.len < 1048576 { p.apc_buf.push(b); }
        return;
    }
}

fn feed_slice(p: Parser&, s: screen::Screen&, pal: colors::Palette&, data: const u8[:]) {
    for i in 0..data.len { feed_byte(p, s, pal, data[i]); }
}
