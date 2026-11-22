// Goot — Lightweight goot.ini / goot.toml Configuration Parser in Goose
// Loads ~/.config/goot/goot.ini (with fallback to goot.toml), zero heap allocations.

namespace config;

import src.colors;
import src.ffi;

let CURSOR_BLOCK     = 0;
let CURSOR_UNDERLINE = 1;
let CURSOR_BEAM      = 2;

let DECO_AUTO   = 0;
let DECO_SERVER = 1;
let DECO_CLIENT = 2;
let DECO_NONE   = 3;

struct Config {
    font_family: u8[..128] = "monospace",
    font_size: i32 = 11,
    line_spacing: i32 = 0,
    padding: i32 = 8,
    initial_cols: i32 = 110,
    initial_rows: i32 = 32,
    title: u8[..128] = "Goot",
    cursor_style: i32 = 0,
    scrollback_lines: i32 = 4000,
    allow_osc52: bool = false,
    touch_long_press_ms: i32 = 400,
    url_label_letters: u8[..64] = "sadfjklewcmpgh",
    title_bar: bool = true,
    title_bar_height: i32 = 28,
    decorations: i32 = 0, // 0=auto, 1=server, 2=client, 3=none
    palette: colors::Palette = colors::Palette { .. },
}

fn default_config() -> Config {
    return Config { .. };
}

fn is_ws(c: u8) -> bool {
    return c == ' ' || c == '\t' || c == '\r';
}

fn trim_view(s: const u8[:]) -> const u8[:] {
    var a: i64 = 0;
    var b: i64 = s.len;
    while a < b && is_ws(s[a]) { a += 1; }
    while b > a && is_ws(s[b - 1]) { b -= 1; }
    return s[a..b];
}

fn strip_quotes(s: const u8[:]) -> const u8[:] {
    if s.len >= 2 && ((s[0] == '"' && s[s.len - 1] == '"') ||
                      (s[0] == '\'' && s[s.len - 1] == '\'')) {
        return s[1..(s.len - 1)];
    }
    return s;
}

fn eq_str(a: const u8[:], b: const u8[:]) -> bool {
    if a.len != b.len { return false; }
    for i in 0..a.len {
        if a[i] != b[i] { return false; }
    }
    return true;
}

fn parse_i32(s: const u8[:]) -> i32, bool {
    if s.len == 0 { return 0, false; }
    var neg = false;
    var i: i64 = 0;
    if s[0] == '-' { neg = true; i = 1; }
    if i >= s.len { return 0, false; }
    var val: i64 = 0;
    while i < s.len {
        let c = s[i];
        if c < '0' || c > '9' { return 0, false; }
        val = val * 10 + ((c - '0') as i64);
        if val > 1000000 { return 0, false; }
        i += 1;
    }
    if neg { val = -val; }
    return val as i32, true;
}

fn copy_128(dst: u8[..128]&, src: const u8[:]) {
    dst.clear();
    let lim = if src.len > 127 { 127 } else { src.len };
    for i in 0..lim { dst.push(src[i]); }
}

fn copy_64(dst: u8[..64]&, src: const u8[:]) {
    dst.clear();
    let lim = if src.len > 63 { 63 } else { src.len };
    for i in 0..lim { dst.push(src[i]); }
}

fn apply_kv(cfg: Config&, section: const u8[:], key: const u8[:], raw_val: const u8[:]) {
    let val = strip_quotes(trim_view(raw_val));
    if eq_str(section, "font") || eq_str(section, "main") || section.len == 0 {
        if (eq_str(key, "family") || eq_str(key, "font")) && val.len > 0 {
            copy_128(cfg.font_family, val);
        } else if eq_str(key, "size") {
            let n, ok = parse_i32(val);
            if ok && n >= 6 && n <= 72 { cfg.font_size = n; }
        } else if eq_str(key, "line_spacing") || eq_str(key, "line-spacing") {
            let n, ok = parse_i32(val);
            if ok && n >= 0 && n <= 32 { cfg.line_spacing = n; }
        } else if eq_str(key, "pad") || eq_str(key, "padding") {
            let n, ok = parse_i32(val);
            if ok && n >= 0 && n <= 128 { cfg.padding = n; }
        } else if eq_str(key, "title") && val.len > 0 {
            copy_128(cfg.title, val);
        }
    }

    if eq_str(section, "window") {
        if eq_str(key, "padding") || eq_str(key, "pad") {
            let n, ok = parse_i32(val);
            if ok && n >= 0 && n <= 128 { cfg.padding = n; }
        } else if eq_str(key, "initial_cols") || eq_str(key, "initial-cols") {
            let n, ok = parse_i32(val);
            if ok && n >= 20 && n <= 500 { cfg.initial_cols = n; }
        } else if eq_str(key, "initial_rows") || eq_str(key, "initial-rows") {
            let n, ok = parse_i32(val);
            if ok && n >= 5 && n <= 200 { cfg.initial_rows = n; }
        } else if eq_str(key, "title") && val.len > 0 {
            copy_128(cfg.title, val);
        } else if eq_str(key, "decorations") {
            if eq_str(val, "auto") { cfg.decorations = DECO_AUTO; }
            else if eq_str(val, "server") || eq_str(val, "ssd") { cfg.decorations = DECO_SERVER; }
            else if eq_str(val, "client") || eq_str(val, "csd") { cfg.decorations = DECO_CLIENT; }
            else if eq_str(val, "none") { cfg.decorations = DECO_NONE; }
        } else if eq_str(key, "title-bar") || eq_str(key, "title_bar") {
            cfg.title_bar = eq_str(val, "true") || eq_str(val, "yes") || eq_str(val, "1");
        } else if eq_str(key, "title-bar-height") || eq_str(key, "title_bar_height") {
            let n, ok = parse_i32(val);
            if ok && n >= 16 && n <= 64 { cfg.title_bar_height = n; }
        }
    } else if eq_str(section, "cursor") {
        if eq_str(key, "style") {
            if eq_str(val, "block") { cfg.cursor_style = CURSOR_BLOCK; }
            else if eq_str(val, "underline") { cfg.cursor_style = CURSOR_UNDERLINE; }
            else if eq_str(val, "beam") { cfg.cursor_style = CURSOR_BEAM; }
        }
    } else if eq_str(section, "scrollback") {
        if eq_str(key, "lines") {
            let n, ok = parse_i32(val);
            if ok && n >= 0 && n <= 100000 { cfg.scrollback_lines = n; }
        }
    } else if eq_str(section, "touch") {
        if eq_str(key, "long-press-delay") || eq_str(key, "long_press_delay") {
            let n, ok = parse_i32(val);
            if ok && n >= 100 && n <= 3000 { cfg.touch_long_press_ms = n; }
        }
    } else if eq_str(section, "url") {
        if (eq_str(key, "label-letters") || eq_str(key, "label_letters")) && val.len >= 4 {
            copy_64(cfg.url_label_letters, val);
        }
    } else if eq_str(section, "security") {
        if eq_str(key, "allow_osc52") || eq_str(key, "allow-osc52") {
            cfg.allow_osc52 = eq_str(val, "true") || eq_str(val, "yes");
        }
    } else if eq_str(section, "colors") {
        let rgb, ok = colors::parse_hex(val);
        guard ok else { return; }
        if eq_str(key, "background") { cfg.palette.bg = rgb; }
        else if eq_str(key, "foreground") { cfg.palette.fg = rgb; }
        else if eq_str(key, "cursor") { cfg.palette.cursor = rgb; }
        else if eq_str(key, "selection_bg") || eq_str(key, "selection-background") { cfg.palette.selection_bg = rgb; }
        else if eq_str(key, "selection_fg") || eq_str(key, "selection-foreground") { cfg.palette.selection_fg = rgb; }
        else if key.len >= 6 && eq_str(key[..5], "color") {
            let idx, iok = parse_i32(key[5..]);
            if iok && idx >= 0 && idx < 16 { cfg.palette.ansi[idx] = rgb; }
        } else if key.len >= 8 && eq_str(key[..7], "regular") {
            let idx, iok = parse_i32(key[7..]);
            if iok && idx >= 0 && idx < 8 { cfg.palette.ansi[idx] = rgb; }
        } else if key.len >= 7 && eq_str(key[..6], "bright") {
            let idx, iok = parse_i32(key[6..]);
            if iok && idx >= 0 && idx < 8 { cfg.palette.ansi[idx + 8] = rgb; }
        }
    }
}

fn parse_text(cfg: Config&, text: const u8[:]) {
    var section: const u8[:] = text[..0];
    var pos: i64 = 0;

    while pos < text.len {
        var end = pos;
        while end < text.len && text[end] != '\n' { end += 1; }
        let raw_line = text[pos..end];
        pos = if end < text.len { end + 1 } else { end };

        var in_quote = false;
        var clean_end = raw_line.len;
        var first_non_ws: i64 = -1;
        for i in 0..raw_line.len {
            let c = raw_line[i];
            if !is_ws(c) && first_non_ws < 0 {
                first_non_ws = i;
            }
            if c == '"' {
                in_quote = !in_quote;
            } else if !in_quote {
                if (c == '#' || c == ';') && i == first_non_ws {
                    clean_end = i;
                    break;
                } else if c == ';' && i > 0 && is_ws(raw_line[i - 1]) {
                    clean_end = i;
                    break;
                } else if c == '#' && i > 0 && is_ws(raw_line[i - 1]) {
                    var is_hex = false;
                    if i + 1 < raw_line.len {
                        let nc = raw_line[i + 1];
                        if colors::hex_nibble(nc) >= 0 {
                            is_hex = true;
                        }
                    }
                    if !is_hex {
                        clean_end = i;
                        break;
                    }
                }
            }
        }

        let line = trim_view(raw_line[..clean_end]);
        if line.len == 0 { continue; }

        if line[0] == '[' && line[line.len - 1] == ']' {
            section = trim_view(line[1..(line.len - 1)]);
            continue;
        }

        var eq: i64 = -1;
        for i in 0..line.len {
            if line[i] == '=' { eq = i; break; }
        }
        if eq > 0 {
            let key = trim_view(line[..eq]);
            let val = trim_view(line[(eq + 1)..]);
            apply_kv(cfg, section, key, val);
        }
    }
}

fn try_load_path(cfg: Config&, path: const u8[:]) -> bool {
    var file_buf: u8[32768] = [0; 32768];
    let n = ffi::file_read(path, file_buf);
    if n > 0 {
        parse_text(cfg, file_buf[..n]);
        return true;
    }
    return false;
}

// Load ~/.config/goot/goot.ini (or fallback goot.toml).
fn load_user_config() -> Config {
    var cfg = default_config();
    var env_buf: u8[512] = [0; 512];
    var base: u8[>..] = [];

    let xdg_len = ffi::env_get("XDG_CONFIG_HOME", env_buf);
    if xdg_len > 0 {
        base.append(env_buf[..xdg_len]);
        base.append("/goot/");
    } else {
        let home_len = ffi::env_get("HOME", env_buf);
        if home_len <= 0 { return cfg; }
        base.append(env_buf[..home_len]);
        base.append("/.config/goot/");
    }

    var p_ini: u8[>..] = [];
    p_ini.append(base);
    p_ini.append("goot.ini");
    if try_load_path(cfg, p_ini) { return cfg; }

    var p_toml: u8[>..] = [];
    p_toml.append(base);
    p_toml.append("goot.toml");
    let _ = try_load_path(cfg, p_toml);
    return cfg;
}
