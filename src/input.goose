// Goosy — Keyboard Escape Translation, Foot-Compatible Shortcuts & Mouse Reporting

namespace input;

import src.ffi;
import src.screen;

// Common XKB keysyms (xkbcommon-keysyms.h)
let XKB_BackSpace: i32    = 0xFF08;
let XKB_Tab: i32          = 0xFF09;
let XKB_Return: i32       = 0xFF0D;
let XKB_Escape: i32       = 0xFF1B;
let XKB_Delete: i32       = 0xFFFF;
let XKB_Home: i32         = 0xFF50;
let XKB_Left: i32         = 0xFF51;
let XKB_Up: i32           = 0xFF52;
let XKB_Right: i32        = 0xFF53;
let XKB_Down: i32         = 0xFF54;
let XKB_Page_Up: i32      = 0xFF55;
let XKB_Page_Down: i32    = 0xFF56;
let XKB_End: i32          = 0xFF57;
let XKB_Insert: i32       = 0xFF63;
let XKB_KP_Enter: i32     = 0xFF8D;
let XKB_KP_Home: i32      = 0xFF95;
let XKB_KP_Left: i32      = 0xFF96;
let XKB_KP_Up: i32        = 0xFF97;
let XKB_KP_Right: i32     = 0xFF98;
let XKB_KP_Down: i32      = 0xFF99;
let XKB_KP_Page_Up: i32   = 0xFF9A;
let XKB_KP_Page_Down: i32 = 0xFF9B;
let XKB_KP_End: i32       = 0xFF9C;
let XKB_KP_Insert: i32    = 0xFF9E;
let XKB_KP_Delete: i32    = 0xFF9F;
let XKB_KP_Multiply: i32  = 0xFFAA;
let XKB_KP_Add: i32       = 0xFFAB;
let XKB_KP_Separator: i32 = 0xFFAC;
let XKB_KP_Subtract: i32  = 0xFFAD;
let XKB_KP_Decimal: i32   = 0xFFAE;
let XKB_KP_Divide: i32    = 0xFFAF;
let XKB_KP_0: i32         = 0xFFB0;
let XKB_KP_9: i32         = 0xFFB9;
let XKB_ISO_Left_Tab: i32 = 0xFE20;
let XKB_F1: i32           = 0xFFBE;
let XKB_F2: i32           = 0xFFBF;
let XKB_F3: i32           = 0xFFC0;
let XKB_F4: i32           = 0xFFC1;
let XKB_F5: i32           = 0xFFC2;
let XKB_F6: i32           = 0xFFC3;
let XKB_F7: i32           = 0xFFC4;
let XKB_F8: i32           = 0xFFC5;
let XKB_F9: i32           = 0xFFC6;
let XKB_F10: i32          = 0xFFC7;
let XKB_F11: i32          = 0xFFC8;
let XKB_F12: i32          = 0xFFC9;
let XKB_XF86Copy: i32     = 0x1008FF57;
let XKB_XF86Paste: i32    = 0x1008FF6D;

// Normal-mode actions
let ACT_NONE: i32           = 0;
let ACT_COPY: i32           = 1;
let ACT_PASTE_CLIP: i32     = 2;
let ACT_PASTE_PRIMARY: i32  = 3;
let ACT_FONT_INC: i32       = 4;
let ACT_FONT_DEC: i32       = 5;
let ACT_FONT_RST: i32       = 6;
let ACT_SCROLL_UP: i32      = 7;
let ACT_SCROLL_DN: i32      = 8;
let ACT_SEARCH_START: i32   = 9;
let ACT_SPAWN_TERM: i32     = 10;
let ACT_URL_MODE: i32       = 11;
let ACT_UNICODE_MODE: i32   = 12;
let ACT_PROMPT_PREV: i32    = 13;
let ACT_PROMPT_NEXT: i32    = 14;
let ACT_COPY_LAST_OUTPUT: i32 = 15;

// Default Normal-mode shortcuts matching Foot/goosy.ini
fn check_shortcut(keysym: i32, mods: i32) -> i32 {
    let ctrl  = (mods & ffi::MOD_CTRL) != 0;
    let shift = (mods & ffi::MOD_SHIFT) != 0;
    let alt   = (mods & ffi::MOD_ALT) != 0;

    if keysym == XKB_XF86Copy  { return ACT_COPY; }
    if keysym == XKB_XF86Paste { return ACT_PASTE_CLIP; }

    if ctrl && shift && !alt {
        if keysym == 'C' || keysym == 'c' { return ACT_COPY; }
        if keysym == 'V' || keysym == 'v' { return ACT_PASTE_CLIP; }
        if keysym == 'R' || keysym == 'r' { return ACT_SEARCH_START; }
        if keysym == 'N' || keysym == 'n' { return ACT_SPAWN_TERM; }
        if keysym == 'O' || keysym == 'o' { return ACT_URL_MODE; }
        if keysym == 'U' || keysym == 'u' { return ACT_UNICODE_MODE; }
        if keysym == 'Z' || keysym == 'z' { return ACT_PROMPT_PREV; }
        if keysym == 'X' || keysym == 'x' { return ACT_PROMPT_NEXT; }
        if keysym == 'A' || keysym == 'a' { return ACT_COPY_LAST_OUTPUT; }
        if keysym == '+' || keysym == '=' { return ACT_FONT_INC; }
    }
    if ctrl && !alt {
        if keysym == '+' || keysym == '=' { return ACT_FONT_INC; }
        if keysym == '-' || keysym == '_' { return ACT_FONT_DEC; }
        if keysym == '0'                  { return ACT_FONT_RST; }
    }
    if shift && !ctrl && !alt {
        if keysym == XKB_Page_Up   { return ACT_SCROLL_UP; }
        if keysym == XKB_Page_Down { return ACT_SCROLL_DN; }
        if keysym == XKB_Insert    { return ACT_PASTE_PRIMARY; }
    }
    return ACT_NONE;
}

fn xterm_mod_param(mods: i32) -> i32 {
    var m: i32 = 1;
    if (mods & ffi::MOD_SHIFT) != 0 { m += 1; }
    if (mods & ffi::MOD_ALT) != 0   { m += 2; }
    if (mods & ffi::MOD_CTRL) != 0  { m += 4; }
    return m;
}

fn push_csi_arrow(out: u8[>..]&, letter: u8, mods: i32, app_cursor: bool, kitty_flags: i32) {
    let mp = xterm_mod_param(mods);
    if mp > 1 {
        out.append("\x1b[1;");
        out.push(('0' + mp) as! u8);
        out.push(letter);
    } else if app_cursor && (kitty_flags & 1) == 0 {
        out.append("\x1bO");
        out.push(letter);
    } else {
        out.append("\x1b[");
        out.push(letter);
    }
}

fn push_tilde_key(out: u8[>..]&, num: const u8[:], mods: i32) {
    let mp = xterm_mod_param(mods);
    out.append("\x1b[");
    out.append(num);
    if mp > 1 {
        out.push(';');
        out.push(('0' + mp) as! u8);
    }
    out.push('~');
}

fn translate_key(out: u8[>..]&, keysym: i32, mods: i32, utf8_bytes: const u8[:], app_cursor: bool, kitty_flags: i32) {
    out.clear();
    let ctrl  = (mods & ffi::MOD_CTRL) != 0;
    let shift = (mods & ffi::MOD_SHIFT) != 0;
    let alt   = (mods & ffi::MOD_ALT) != 0;

    // Kitty Keyboard Protocol disambiguation (flag 1: DISAMBIGUATE_ESCAPE_CODES)
    if (kitty_flags & 1) != 0 {
        if keysym == XKB_Return || keysym == XKB_KP_Enter {
            let mp = xterm_mod_param(mods);
            out.append("\x1b[13");
            if mp > 1 { out.push(';'); out.push(('0' + mp) as! u8); }
            out.push('u');
            return;
        }
        if keysym == XKB_Escape {
            let mp = xterm_mod_param(mods);
            out.append("\x1b[27");
            if mp > 1 { out.push(';'); out.push(('0' + mp) as! u8); }
            out.push('u');
            return;
        }
        if keysym == XKB_Tab {
            let mp = xterm_mod_param(mods);
            out.append("\x1b[9");
            if mp > 1 { out.push(';'); out.push(('0' + mp) as! u8); }
            out.push('u');
            return;
        }
        if keysym == XKB_ISO_Left_Tab {
            let mp = xterm_mod_param(mods | ffi::MOD_SHIFT);
            out.append("\x1b[9;");
            out.push(('0' + mp) as! u8);
            out.push('u');
            return;
        }
        if keysym == XKB_BackSpace {
            let mp = xterm_mod_param(mods);
            out.append("\x1b[127");
            if mp > 1 { out.push(';'); out.push(('0' + mp) as! u8); }
            out.push('u');
            return;
        }
    }

    match keysym {
        XKB_Return => { if alt { out.push(0x1B); } out.push(0x0D); return; },
        XKB_KP_Enter => { if alt { out.push(0x1B); } out.push(0x0D); return; },
        XKB_BackSpace => { if alt { out.push(0x1B); } out.push(if ctrl { 0x08 } else { 0x7F }); return; },
        XKB_Tab => {
            if shift { out.append("\x1b[Z"); }
            else { out.push(0x09); }
            return;
        },
        XKB_ISO_Left_Tab => { out.append("\x1b[Z"); return; },
        XKB_Escape => { out.push(0x1B); return; },
        XKB_Up        => { push_csi_arrow(out, 'A', mods, app_cursor, kitty_flags); return; },
        XKB_KP_Up     => { push_csi_arrow(out, 'A', mods, app_cursor, kitty_flags); return; },
        XKB_Down      => { push_csi_arrow(out, 'B', mods, app_cursor, kitty_flags); return; },
        XKB_KP_Down   => { push_csi_arrow(out, 'B', mods, app_cursor, kitty_flags); return; },
        XKB_Right     => { push_csi_arrow(out, 'C', mods, app_cursor, kitty_flags); return; },
        XKB_KP_Right  => { push_csi_arrow(out, 'C', mods, app_cursor, kitty_flags); return; },
        XKB_Left      => { push_csi_arrow(out, 'D', mods, app_cursor, kitty_flags); return; },
        XKB_KP_Left   => { push_csi_arrow(out, 'D', mods, app_cursor, kitty_flags); return; },
        XKB_Home      => { push_csi_arrow(out, 'H', mods, app_cursor, kitty_flags); return; },
        XKB_KP_Home   => { push_csi_arrow(out, 'H', mods, app_cursor, kitty_flags); return; },
        XKB_End       => { push_csi_arrow(out, 'F', mods, app_cursor, kitty_flags); return; },
        XKB_KP_End     => { push_csi_arrow(out, 'F', mods, app_cursor, kitty_flags); return; },
        XKB_Insert    => { push_tilde_key(out, "2", mods); return; },
        XKB_KP_Insert => { push_tilde_key(out, "2", mods); return; },
        XKB_Delete    => { push_tilde_key(out, "3", mods); return; },
        XKB_KP_Delete => { push_tilde_key(out, "3", mods); return; },
        XKB_Page_Up   => { push_tilde_key(out, "5", mods); return; },
        XKB_KP_Page_Up => { push_tilde_key(out, "5", mods); return; },
        XKB_Page_Down => { push_tilde_key(out, "6", mods); return; },
        XKB_KP_Page_Down => { push_tilde_key(out, "6", mods); return; },
        XKB_KP_Add       => { if alt { out.push(0x1B); } out.push('+'); return; },
        XKB_KP_Subtract  => { if alt { out.push(0x1B); } out.push('-'); return; },
        XKB_KP_Multiply  => { if alt { out.push(0x1B); } out.push('*'); return; },
        XKB_KP_Divide    => { if alt { out.push(0x1B); } out.push('/'); return; },
        XKB_KP_Separator => { if alt { out.push(0x1B); } out.push(','); return; },
        XKB_KP_Decimal   => { if alt { out.push(0x1B); } out.push('.'); return; },
        XKB_F1  => { out.append("\x1bOP"); return; },
        XKB_F2  => { out.append("\x1bOQ"); return; },
        XKB_F3  => { out.append("\x1bOR"); return; },
        XKB_F4  => { out.append("\x1bOS"); return; },
        XKB_F5  => { push_tilde_key(out, "15", mods); return; },
        XKB_F6  => { push_tilde_key(out, "17", mods); return; },
        XKB_F7  => { push_tilde_key(out, "18", mods); return; },
        XKB_F8  => { push_tilde_key(out, "19", mods); return; },
        XKB_F9  => { push_tilde_key(out, "20", mods); return; },
        XKB_F10 => { push_tilde_key(out, "21", mods); return; },
        XKB_F11 => { push_tilde_key(out, "23", mods); return; },
        XKB_F12 => { push_tilde_key(out, "24", mods); return; },
        _ => {},
    }

    if keysym >= XKB_KP_0 && keysym <= XKB_KP_9 {
        if alt { out.push(0x1B); }
        out.push(('0' + (keysym - XKB_KP_0)) as! u8);
        return;
    }

    if ctrl {
        if keysym >= 'a' && keysym <= 'z' {
            if alt { out.push(0x1B); }
            out.push((keysym - 'a' + 1) as! u8);
            return;
        }
        if keysym >= 'A' && keysym <= 'Z' {
            if alt { out.push(0x1B); }
            out.push((keysym - 'A' + 1) as! u8);
            return;
        }
        if keysym == ' ' || keysym == '@' { out.push(0x00); return; }
        if keysym == '['  { out.push(0x1B); return; }
        if keysym == '\\' { out.push(0x1C); return; }
        if keysym == ']'  { out.push(0x1D); return; }
    }

    if utf8_bytes.len > 0 {
        if alt { out.push(0x1B); }
        out.append(utf8_bytes);
    }
}

fn format_paste(out: u8[>..]&, raw: const u8[:], bracketed: bool) {
    out.clear();
    if bracketed { out.append("\x1b[200~"); }
    var prev_cr = false;
    for i in 0..raw.len {
        let b = raw[i];
        if b == 0x00 || b == 0x1B { continue; }
        if b == '\r' {
            out.push('\r');
            prev_cr = true;
        } else if b == '\n' {
            if !prev_cr {
                out.push('\r');
            }
            prev_cr = false;
        } else {
            out.push(b);
            prev_cr = false;
        }
    }
    if bracketed { out.append("\x1b[201~"); }
}

fn push_u32_dec(out: u8[>..]&, v: i32) {
    var n = if v < 0 { 0 } else { v };
    if n == 0 { out.push('0'); return; }
    var buf: u8[..12] = [];
    while n > 0 {
        buf.push(('0' + (n % 10)) as! u8);
        n = n / 10;
    }
    var k = buf.len - 1;
    while k >= 0 { out.push(buf[k]); k -= 1; }
}

fn encode_mouse(out: u8[>..]&, s: screen::Screen&, btn: i32, col: i32, row: i32, pressed: bool) {
    out.clear();
    if s.mouse_mode == 0 { return; }
    let x1 = col + 1;
    let y1 = row + 1;
    if s.mouse_sgr {
        out.append("\x1b[<");
        push_u32_dec(out, btn);
        out.push(';');
        push_u32_dec(out, x1);
        out.push(';');
        push_u32_dec(out, y1);
        out.push(if pressed { 'M' } else { 'm' });
    } else if x1 <= 223 && y1 <= 223 {
        let cb = if pressed { btn } else { 3 };
        out.append("\x1b[M");
        out.push((32 + cb) as! u8);
        out.push((32 + x1) as! u8);
        out.push((32 + y1) as! u8);
    }
}
