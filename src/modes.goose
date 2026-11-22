// Goot — Interactive Modes in Goose:
//   1. Scrollback Search Mode (Ctrl+Shift+R, Ctrl+R/S, Ctrl+W/Shift+W, Paste, Return, Esc/Ctrl+G)
//   2. URL Jump-Label Mode (Ctrl+Shift+O, 't' toggle URL in label, Esc/Ctrl+C/G/D)
//   3. Unicode Input Mode (Ctrl+Shift+U, hex entry, Return/Space commit, Esc/Ctrl+C/G)

namespace modes;

import src.ffi;
import src.screen;
import src.utf8;

let MODE_NORMAL: i32  = 0;
let MODE_SEARCH: i32  = 1;
let MODE_URL: i32     = 2;
let MODE_UNICODE: i32 = 3;

let MAX_URLS: i32 = 64;

struct UrlItem {
    row: i32 = 0,
    col_start: i32 = 0,
    col_end: i32 = 0,
    label: u8[..4] = "",
    url: u8[..256] = "",
}

struct ModeState {
    kind: i32 = 0,

    // Scrollback search state
    search_query: u8[..128] = "",
    search_saved_offset: i32 = 0,
    search_found: bool = false,
    search_abs_line: i32 = -1,
    search_col: i32 = -1,
    search_len: i32 = 0,

    // URL mode state
    url_count: i32 = 0,
    url_show_text: bool = false,
    url_typed: u8[..4] = "",
    urls: UrlItem[64] = [UrlItem { .. }; 64],

    // Unicode input mode state
    uni_hex: u8[..8] = "",
}

fn new_state() -> ModeState {
    return ModeState { .. };
}

fn ascii_lower(c: u32) -> u32 {
    if c >= 'A' && c <= 'Z' { return c + 32; }
    return c;
}

// -------------------------------------------------------------------------
// 1. Scrollback Search Engine
// -------------------------------------------------------------------------
fn enter_search(m: ModeState&, s: screen::Screen&) {
    m.kind = MODE_SEARCH;
    m.search_query.clear();
    m.search_saved_offset = s.view_offset;
    m.search_found = false;
    m.search_abs_line = screen::total_history_lines(s) - 1;
    m.search_col = s.cols;
    m.search_len = 0;
    screen::mark_all_dirty(s);
}

fn cancel_search(m: ModeState&, s: screen::Screen&) {
    m.kind = MODE_NORMAL;
    s.view_offset = m.search_saved_offset;
    s.sel_active = false;
    screen::mark_all_dirty(s);
}

fn match_at(s: screen::Screen&, abs_line: i32, col: i32, q: const u8[:]) -> bool {
    if q.len == 0 || col < 0 || col + (q.len as i32) > s.cols { return false; }
    for i in 0..q.len {
        let cell = screen::history_cell(s, abs_line, col + (i as i32));
        if ascii_lower(cell.cp) != ascii_lower(q[i] as u32) {
            return false;
        }
    }
    return true;
}

fn apply_search_highlight(m: ModeState&, s: screen::Screen&) {
    if !m.search_found || m.search_len <= 0 {
        s.sel_active = false;
        screen::mark_all_dirty(s);
        return;
    }
    // Bring matched abs_line into view
    let top_line = s.sb_count - s.view_offset;
    if m.search_abs_line < top_line || m.search_abs_line >= top_line + s.rows {
        var target_off = s.sb_count - m.search_abs_line + (s.rows / 2);
        if target_off < 0 { target_off = 0; }
        if target_off > s.sb_count { target_off = s.sb_count; }
        s.view_offset = target_off;
    }
    let vis_row = m.search_abs_line - (s.sb_count - s.view_offset);
    if vis_row >= 0 && vis_row < s.rows {
        s.sel_active = true;
        s.sel_mode = screen::SEL_CHAR;
        s.sel_x0 = m.search_col;
        s.sel_y0 = vis_row;
        s.sel_x1 = m.search_col + m.search_len - 1;
        s.sel_y1 = vis_row;
    }
    screen::mark_all_dirty(s);
}

// Search backward (dir = -1) or forward (dir = +1) through scrollback + live screen
fn search_step(m: ModeState&, s: screen::Screen&, dir: i32, advance: bool) -> bool {
    if m.search_query.len == 0 {
        m.search_found = false;
        apply_search_highlight(m, s);
        return false;
    }
    let total = screen::total_history_lines(s);
    let qlen = m.search_query.len as i32;
    var line = if m.search_abs_line >= 0 && m.search_abs_line < total { m.search_abs_line } else { total - 1 };
    var col = m.search_col;
    if advance {
        col += dir;
    }

    if dir < 0 {
        while line >= 0 {
            var c = if col >= s.cols { s.cols - qlen } else { col };
            while c >= 0 {
                if match_at(s, line, c, m.search_query) {
                    m.search_found = true;
                    m.search_abs_line = line;
                    m.search_col = c;
                    m.search_len = qlen;
                    apply_search_highlight(m, s);
                    return true;
                }
                c -= 1;
            }
            line -= 1;
            col = s.cols;
        }
    } else {
        while line < total {
            var c = if col < 0 { 0 } else { col };
            while c + qlen <= s.cols {
                if match_at(s, line, c, m.search_query) {
                    m.search_found = true;
                    m.search_abs_line = line;
                    m.search_col = c;
                    m.search_len = qlen;
                    apply_search_highlight(m, s);
                    return true;
                }
                c += 1;
            }
            line += 1;
            col = 0;
        }
    }
    m.search_found = false;
    apply_search_highlight(m, s);
    return false;
}

// Ctrl+W / Ctrl+Shift+W in search mode: extend search query to end of word (or next word)
fn search_extend_word(m: ModeState&, s: screen::Screen&, whitespace_only: bool) {
    if !m.search_found || m.search_abs_line < 0 { return; }
    var c = m.search_col + m.search_len;
    if c >= s.cols { return; }

    // If currently on a separator, consume separators first to reach the next word
    if screen::is_word_sep(screen::history_cell(s, m.search_abs_line, c).cp, whitespace_only) {
        while c < s.cols && m.search_query.len < 127 &&
              screen::is_word_sep(screen::history_cell(s, m.search_abs_line, c).cp, whitespace_only) {
            let cp = screen::history_cell(s, m.search_abs_line, c).cp;
            m.search_query.push(if cp >= 0x20 && cp < 0x7F { cp as! u8 } else { ' ' });
            c += 1;
        }
    }
    // Consume word characters up to the next separator
    while c < s.cols && m.search_query.len < 127 &&
          !screen::is_word_sep(screen::history_cell(s, m.search_abs_line, c).cp, whitespace_only) {
        let cp = screen::history_cell(s, m.search_abs_line, c).cp;
        if cp >= 0x20 && cp < 0x7F {
            m.search_query.push(cp as! u8);
        }
        c += 1;
    }
    m.search_len = m.search_query.len as i32;
    apply_search_highlight(m, s);
}

fn search_append_text(m: ModeState&, s: screen::Screen&, text: const u8[:]) {
    for i in 0..text.len {
        let b = text[i];
        if b >= 0x20 && b != 0x7F && m.search_query.len < 127 {
            m.search_query.push(b);
        }
    }
    let _ = search_step(m, s, -1, false);
}

// -------------------------------------------------------------------------
// 2. URL Mode (Ctrl+Shift+O)
// -------------------------------------------------------------------------
fn is_url_char(cp: u32) -> bool {
    if cp <= 0x20 || cp >= 0x7F { return false; }
    if cp == '"' || cp == '\'' || cp == '<' || cp == '>' || cp == '`' ||
       cp == '{' || cp == '}' || cp == '|' || cp == '\\' || cp == '^' {
        return false;
    }
    return true;
}

fn row_starts_with(s: screen::Screen&, row: i32, col: i32, prefix: const u8[:]) -> bool {
    if col + (prefix.len as i32) > s.cols { return false; }
    for i in 0..prefix.len {
        if screen::visible_cell(s, col + (i as i32), row).cp != (prefix[i] as u32) {
            return false;
        }
    }
    return true;
}

fn assign_label(dst: u8[..4]&, idx: i32, letters: const u8[:]) {
    dst.clear();
    let n = if letters.len > 0 { letters.len as i32 } else { 1 };
    if idx < n {
        dst.push(letters[idx]);
    } else {
        dst.push(letters[(idx / n) % n]);
        dst.push(letters[idx % n]);
    }
}

fn enter_url_mode(m: ModeState&, s: screen::Screen&, letters: const u8[:]) {
    m.url_count = 0;
    m.url_typed.clear();
    m.url_show_text = false;

    for r in 0..s.rows {
        var c: i32 = 0;
        while c < s.cols && m.url_count < MAX_URLS {
            let is_match = row_starts_with(s, r, c, "https://") ||
                           row_starts_with(s, r, c, "http://") ||
                           row_starts_with(s, r, c, "ftp://") ||
                           row_starts_with(s, r, c, "file://");
            if is_match {
                var end_c = c;
                while end_c < s.cols && is_url_char(screen::visible_cell(s, end_c, r).cp) {
                    end_c += 1;
                }
                // Trim trailing punctuation like '.', ',', ')', ';'
                while end_c > c {
                    let last = screen::visible_cell(s, end_c - 1, r).cp;
                    if last == '.' || last == ',' || last == ')' || last == ']' || last == ';' || last == ':' {
                        end_c -= 1;
                    } else {
                        break;
                    }
                }
                if end_c - c >= 8 {
                    var item = UrlItem { row: r, col_start: c, col_end: end_c - 1, .. };
                    assign_label(item.label, m.url_count, letters);
                    for k in c..end_c {
                        if item.url.len < 255 {
                            item.url.push(screen::visible_cell(s, k, r).cp as! u8);
                        }
                    }
                    m.urls[m.url_count] = item;
                    m.url_count += 1;
                    c = end_c;
                    continue;
                }
            }
            c += 1;
        }
    }

    if m.url_count > 0 {
        m.kind = MODE_URL;
        screen::mark_all_dirty(s);
    }
}

// Feed a character in URL mode; returns matched index >= 0 if a label completed, -1 otherwise.
fn url_feed_char(m: ModeState&, ch: u8) -> i32 {
    if m.url_typed.len < 3 {
        m.url_typed.push(ch);
    }
    var exact: i32 = -1;
    var prefix_matches: i32 = 0;
    for i in 0..m.url_count {
        let lbl = m.urls[i].label;
        if lbl.len >= m.url_typed.len {
            var ok = true;
            for k in 0..m.url_typed.len {
                if lbl[k] != m.url_typed[k] { ok = false; break; }
            }
            if ok {
                prefix_matches += 1;
                if lbl.len == m.url_typed.len { exact = i; }
            }
        }
    }
    if exact >= 0 && prefix_matches == 1 {
        return exact;
    }
    if prefix_matches == 0 {
        m.url_typed.clear();
    }
    return -1;
}

// -------------------------------------------------------------------------
// 3. Unicode Input Mode (Ctrl+Shift+U)
// -------------------------------------------------------------------------
fn enter_unicode_mode(m: ModeState&, s: screen::Screen&) {
    m.kind = MODE_UNICODE;
    m.uni_hex.clear();
    screen::mark_all_dirty(s);
}

fn is_hex_digit(b: u8) -> bool {
    return (b >= '0' && b <= '9') || (b >= 'a' && b <= 'f') || (b >= 'A' && b <= 'F');
}

fn unicode_commit(m: ModeState&, out: u8[>..]&) -> bool {
    out.clear();
    if m.uni_hex.len == 0 { return false; }
    var cp: u32 = 0;
    for i in 0..m.uni_hex.len {
        let c = m.uni_hex[i];
        let v: u32 = if c >= '0' && c <= '9' { (c - '0') as u32 }
                     else if c >= 'a' && c <= 'f' { (c - 'a' + 10) as u32 }
                     else { (c - 'A' + 10) as u32 };
        cp = (cp << 4) | v;
    }
    if cp == 0 || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF) {
        return false;
    }
    utf8::encode_to(out, cp);
    return true;
}
