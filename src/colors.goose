// Goot — Color Palette Management (16-color, 256-color xterm, 24-bit True Color)
// Default aesthetic: Foot/Kitty-inspired dark theme (#080808 bg, #EAEAEA fg).

namespace colors;

let DEFAULT_BG: u32           = 0x080808;
let DEFAULT_FG: u32           = 0xEAEAEA;
let DEFAULT_CURSOR: u32       = 0xEAEAEA;
let DEFAULT_SELECTION_BG: u32 = 0x2A3F5F;
let DEFAULT_SELECTION_FG: u32 = 0xFFFFFF;

struct Palette {
    bg: u32 = 0x080808,
    fg: u32 = 0xEAEAEA,
    cursor: u32 = 0xEAEAEA,
    selection_bg: u32 = 0x2A3F5F,
    selection_fg: u32 = 0xFFFFFF,
    ansi: u32[16] = [
        0x181818, 0xD75F5F, 0x87AF87, 0xD7AF87,
        0x5F87AF, 0xAF87AF, 0x5FAFAF, 0xD0D0D0,
        0x585858, 0xFF5F5F, 0xAFDF87, 0xFFD787,
        0x87AFFF, 0xD7AFFF, 0x87DFDF, 0xFFFFFF,
    ],
}

fn default_palette() -> Palette {
    return Palette { .. };
}

fn pack_rgb(r: u32, g: u32, b: u32) -> u32 {
    return ((r & 0xFF) << 16) | ((g & 0xFF) << 8) | (b & 0xFF);
}

// Resolve an xterm 256-color index (0..255) to 24-bit 0xRRGGBB.
fn resolve_256(pal: Palette&, idx: i32) -> u32 {
    if idx < 0 {
        return pal.fg;
    }
    if idx < 16 {
        return pal.ansi[idx];
    }
    if idx < 232 {
        let c = idx - 16;
        let r_i = c / 36;
        let g_i = (c / 6) % 6;
        let b_i = c % 6;
        let r: u32 = if r_i == 0 { 0 } else { (55 + r_i * 40) as u32 };
        let g: u32 = if g_i == 0 { 0 } else { (55 + g_i * 40) as u32 };
        let b: u32 = if b_i == 0 { 0 } else { (55 + b_i * 40) as u32 };
        return pack_rgb(r, g, b);
    }
    if idx < 256 {
        let gray = (8 + (idx - 232) * 10) as u32;
        return pack_rgb(gray, gray, gray);
    }
    return pal.fg;
}

// Dim a 24-bit RGB color by ~35% for SGR 2 (faint/dim).
fn dim_rgb(rgb: u32) -> u32 {
    let r = (((rgb >> 16) & 0xFF) * 2) / 3;
    let g = (((rgb >> 8) & 0xFF) * 2) / 3;
    let b = ((rgb & 0xFF) * 2) / 3;
    return pack_rgb(r, g, b);
}

fn hex_nibble(c: u8) -> i32 {
    if c >= '0' && c <= '9' { return (c - '0') as i32; }
    if c >= 'a' && c <= 'f' { return (c - 'a') as i32 + 10; }
    if c >= 'A' && c <= 'F' { return (c - 'A') as i32 + 10; }
    return -1;
}

// Parse "#RRGGBB" or "RRGGBB" into 24-bit u32.
fn parse_hex(s: const u8[:]) -> u32, bool {
    var start: i64 = 0;
    if s.len == 7 && s[0] == '#' {
        start = 1;
    } else if s.len != 6 {
        return 0, false;
    }
    var acc: u32 = 0;
    for i in start..(start + 6) {
        let nib = hex_nibble(s[i]);
        if nib < 0 {
            return 0, false;
        }
        acc = (acc << 4) | (nib as u32);
    }
    return acc, true;
}
