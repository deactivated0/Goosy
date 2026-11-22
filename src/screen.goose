// Goot — Terminal Screen Grid, Alternate Buffer, Bounded Scrollback,
// Selection Modes (Char, Block, Word, Quote, Line), OSC 7 CWD & OSC 133 Prompts

namespace screen;

import src.colors;
import src.utf8;

let ATTR_BOLD: u16      = 1;
let ATTR_ITALIC: u16    = 2;
let ATTR_UNDERLINE: u16 = 4;
let ATTR_BLINK: u16     = 8;
let ATTR_INVERSE: u16   = 16;
let ATTR_STRIKE: u16    = 32;
let ATTR_DIM: u16       = 64;
let ATTR_HIDDEN: u16    = 128;

let CELL_WIDE_CONT: u8  = 1;

let SEL_CHAR: i32  = 0;
let SEL_BLOCK: i32 = 1;
let SEL_WORD: i32  = 2;
let SEL_QUOTE: i32 = 3;
let SEL_LINE: i32  = 4;

let MAX_COLS: i32 = 512;
let MAX_ROWS: i32 = 256;

struct Cell {
    cp: u32 = 0x20,
    combining: u32 = 0,
    fg: u32 = 0xEAEAEA,
    bg: u32 = 0x080808,
    attrs: u16 = 0,
    width: u8 = 1,
    flags: u8 = 0,
}

root struct Screen {
    cols: i32 = 80,
    rows: i32 = 24,
    cx: i32 = 0,
    cy: i32 = 0,
    saved_cx: i32 = 0,
    saved_cy: i32 = 0,
    alt_saved_cx: i32 = 0,
    alt_saved_cy: i32 = 0,
    wrap_pending: bool = false,

    scroll_top: i32 = 0,
    scroll_bottom: i32 = 23,

    cur_fg: u32 = 0xEAEAEA,
    cur_bg: u32 = 0x080808,
    cur_attrs: u16 = 0,
    default_fg: u32 = 0xEAEAEA,
    default_bg: u32 = 0x080808,

    alt_screen: bool = false,
    cursor_visible: bool = true,
    cursor_style: i32 = 0,
    app_cursor: bool = false,
    bracketed_paste: bool = false,
    autowrap: bool = true,
    origin_mode: bool = false,
    mouse_mode: i32 = 0,
    mouse_sgr: bool = false,

    full_redraw_frames: i32 = 2,
    title_dirty: bool = false,
    title: u8[..256] = "Goot",
    cwd: u8[..512] = "",

    sb_cap_lines: i32 = 4000,
    sb_cols: i32 = 80,
    sb_head: i32 = 0,
    sb_count: i32 = 0,
    view_offset: i32 = 0,

    // Selection state (screen viewport coordinates)
    sel_active: bool = false,
    sel_mode: i32 = 0, // SEL_CHAR, SEL_BLOCK, SEL_WORD, SEL_QUOTE, SEL_LINE
    sel_x0: i32 = 0,
    sel_y0: i32 = 0,
    sel_x1: i32 = 0,
    sel_y1: i32 = 0,

    // Independently stored resizable arrays owned by root struct
    cells: Cell[>..] = [],
    alt_cells: Cell[>..] = [],
    sb_cells: Cell[>..] = [],
    dirty_rows: u8[>..] = [],
    tab_stops: u8[>..] = [],
    prompt_rows: u8[>..] = [],
    sb_prompt_rows: u8[>..] = [],
    reply_buf: u8[>..] = [],
}

fn blank_cell(s: Screen&) -> Cell {
    return Cell {
        cp: 0x20, combining: 0, fg: s.default_fg, bg: s.default_bg,
        attrs: 0, width: 1, flags: 0,
    };
}

fn erase_cell(s: Screen&) -> Cell {
    return Cell {
        cp: 0x20, combining: 0, fg: s.cur_fg, bg: s.cur_bg,
        attrs: 0, width: 1, flags: 0,
    };
}

fn mark_row_dirty(s: Screen&, row: i32) {
    if row >= 0 && row < s.rows && (row as i64) < s.dirty_rows.len {
        s.dirty_rows[row] = 2;
    }
}

fn mark_all_dirty(s: Screen&) {
    s.full_redraw_frames = 2;
    for r in 0..s.rows {
        if (r as i64) < s.dirty_rows.len { s.dirty_rows[r] = 2; }
    }
}

fn clear_dirty(s: Screen&) {
    if s.full_redraw_frames > 0 { s.full_redraw_frames -= 1; }
    for r in 0..s.rows {
        if (r as i64) < s.dirty_rows.len && s.dirty_rows[r] > 0 {
            s.dirty_rows[r] -= 1;
        }
    }
}

fn reset_tabs(s: Screen&) {
    s.tab_stops.clear();
    for c in 0..s.cols {
        let stop: u8 = if (c % 8) == 0 && c > 0 { 1 } else { 0 };
        s.tab_stops.push(stop);
    }
}

fn init(s: Screen&, cols: i32, rows: i32, fg: u32, bg: u32, sb_lines: i32, cursor_style: i32) {
    let c = if cols < 2 { 2 } else if cols > MAX_COLS { MAX_COLS } else { cols };
    let r = if rows < 2 { 2 } else if rows > MAX_ROWS { MAX_ROWS } else { rows };
    s.cols = c; s.rows = r;
    s.cx = 0; s.cy = 0; s.saved_cx = 0; s.saved_cy = 0;
    s.wrap_pending = false;
    s.scroll_top = 0; s.scroll_bottom = r - 1;
    s.default_fg = fg; s.default_bg = bg;
    s.cur_fg = fg; s.cur_bg = bg; s.cur_attrs = 0;
    s.cursor_style = cursor_style;
    s.cursor_visible = true;
    s.alt_screen = false;
    s.app_cursor = false;
    s.bracketed_paste = false;
    s.autowrap = true;
    s.origin_mode = false;
    s.mouse_mode = 0;
    s.mouse_sgr = false;

    let total = (c as i64) * (r as i64);
    let b = blank_cell(s);
    s.cells.clear();
    s.alt_cells.clear();
    for _ in 0..total {
        s.cells.push(b);
        s.alt_cells.push(b);
    }

    s.dirty_rows.clear();
    s.prompt_rows.clear();
    for _ in 0..r {
        s.dirty_rows.push(2);
        s.prompt_rows.push(0);
    }
    reset_tabs(s);

    let cap = if sb_lines < 0 { 0 } else if sb_lines > 100000 { 100000 } else { sb_lines };
    s.sb_cap_lines = cap;
    s.sb_cols = c;
    s.sb_head = 0;
    s.sb_count = 0;
    s.view_offset = 0;
    s.sb_cells.clear();
    s.sb_prompt_rows.clear();
    if cap > 0 {
        let sb_total = (cap as i64) * (c as i64);
        for _ in 0..sb_total { s.sb_cells.push(b); }
        for _ in 0..cap { s.sb_prompt_rows.push(0); }
    }
    s.full_redraw_frames = 2;
}

fn clamp_cursor(s: Screen&) {
    if s.cx < 0 { s.cx = 0; }
    if s.cx >= s.cols { s.cx = s.cols - 1; }
    if s.cy < 0 { s.cy = 0; }
    if s.cy >= s.rows { s.cy = s.rows - 1; }
}

fn set_cursor(s: Screen&, x: i32, y: i32) {
    mark_row_dirty(s, s.cy);
    s.cx = x; s.cy = y;
    s.wrap_pending = false;
    clamp_cursor(s);
    mark_row_dirty(s, s.cy);
}

fn mark_prompt_row(s: Screen&) {
    if !s.alt_screen && s.cy >= 0 && (s.cy as i64) < s.prompt_rows.len {
        s.prompt_rows[s.cy] = 1;
    }
}

fn push_scrollback_row(s: Screen&, row: i32) {
    if s.alt_screen || s.sb_cap_lines <= 0 || row < 0 || row >= s.rows { return; }
    let dst_line = (s.sb_head + s.sb_count) % s.sb_cap_lines;
    if s.sb_count == s.sb_cap_lines {
        s.sb_head = (s.sb_head + 1) % s.sb_cap_lines;
    } else {
        s.sb_count += 1;
    }

    if (dst_line as i64) < s.sb_prompt_rows.len && (row as i64) < s.prompt_rows.len {
        s.sb_prompt_rows[dst_line] = s.prompt_rows[row];
    }

    let copy_cols = if s.cols < s.sb_cols { s.cols } else { s.sb_cols };
    let b = blank_cell(s);
    let src_base = (row as i64) * (s.cols as i64);
    let dst_base = (dst_line as i64) * (s.sb_cols as i64);
    for c in 0..s.sb_cols {
        if c < copy_cols {
            s.sb_cells[dst_base + (c as i64)] = s.cells[src_base + (c as i64)];
        } else {
            s.sb_cells[dst_base + (c as i64)] = b;
        }
    }
}

fn scroll_up(s: Screen&, top: i32, bottom: i32, n: i32) {
    if n <= 0 || top < 0 || bottom >= s.rows || top > bottom { return; }
    let count = if n > (bottom - top + 1) { bottom - top + 1 } else { n };

    if top == 0 && bottom == s.rows - 1 && !s.alt_screen {
        for i in 0..count { push_scrollback_row(s, i); }
    }

    for r in top..(bottom - count + 1) {
        let dst = (r as i64) * (s.cols as i64);
        let src = ((r + count) as i64) * (s.cols as i64);
        for c in 0..s.cols {
            s.cells[dst + (c as i64)] = s.cells[src + (c as i64)];
        }
        if (r as i64) < s.prompt_rows.len && ((r + count) as i64) < s.prompt_rows.len {
            s.prompt_rows[r] = s.prompt_rows[r + count];
        }
        mark_row_dirty(s, r);
    }

    let ec = erase_cell(s);
    for r in (bottom - count + 1)..(bottom + 1) {
        let base = (r as i64) * (s.cols as i64);
        for c in 0..s.cols { s.cells[base + (c as i64)] = ec; }
        if (r as i64) < s.prompt_rows.len { s.prompt_rows[r] = 0; }
        mark_row_dirty(s, r);
    }
}

fn scroll_down(s: Screen&, top: i32, bottom: i32, n: i32) {
    if n <= 0 || top < 0 || bottom >= s.rows || top > bottom { return; }
    let count = if n > (bottom - top + 1) { bottom - top + 1 } else { n };

    var r = bottom;
    while r >= top + count {
        let dst = (r as i64) * (s.cols as i64);
        let src = ((r - count) as i64) * (s.cols as i64);
        for c in 0..s.cols { s.cells[dst + (c as i64)] = s.cells[src + (c as i64)]; }
        if (r as i64) < s.prompt_rows.len && ((r - count) as i64) < s.prompt_rows.len {
            s.prompt_rows[r] = s.prompt_rows[r - count];
        }
        mark_row_dirty(s, r);
        r -= 1;
    }

    let ec = erase_cell(s);
    for r2 in top..(top + count) {
        let base = (r2 as i64) * (s.cols as i64);
        for c in 0..s.cols { s.cells[base + (c as i64)] = ec; }
        if (r2 as i64) < s.prompt_rows.len { s.prompt_rows[r2] = 0; }
        mark_row_dirty(s, r2);
    }
}

fn linefeed(s: Screen&) {
    s.wrap_pending = false;
    mark_row_dirty(s, s.cy);
    if s.cy == s.scroll_bottom {
        scroll_up(s, s.scroll_top, s.scroll_bottom, 1);
    } else if s.cy < s.rows - 1 {
        s.cy += 1;
    }
    mark_row_dirty(s, s.cy);
}

fn reverse_index(s: Screen&) {
    s.wrap_pending = false;
    mark_row_dirty(s, s.cy);
    if s.cy == s.scroll_top {
        scroll_down(s, s.scroll_top, s.scroll_bottom, 1);
    } else if s.cy > 0 {
        s.cy -= 1;
    }
    mark_row_dirty(s, s.cy);
}

fn put_codepoint(s: Screen&, cp: u32) {
    if s.view_offset > 0 {
        s.view_offset = 0;
        mark_all_dirty(s);
    }

    let w = utf8::cell_width(cp);
    if w == 0 {
        if utf8::is_combining(cp) {
            let prev_x = if s.wrap_pending { s.cx } else if s.cx > 0 { s.cx - 1 } else { 0 };
            let idx = (s.cy as i64) * (s.cols as i64) + (prev_x as i64);
            if idx >= 0 && idx < s.cells.len {
                s.cells[idx].combining = cp;
                mark_row_dirty(s, s.cy);
            }
        }
        return;
    }

    if s.wrap_pending {
        s.wrap_pending = false;
        if s.autowrap { s.cx = 0; linefeed(s); }
    }

    if w == 2 && s.cx == s.cols - 1 && s.autowrap {
        let pad_idx = (s.cy as i64) * (s.cols as i64) + (s.cx as i64);
        s.cells[pad_idx] = erase_cell(s);
        mark_row_dirty(s, s.cy);
        s.cx = 0;
        linefeed(s);
    }

    let idx = (s.cy as i64) * (s.cols as i64) + (s.cx as i64);
    s.cells[idx] = Cell {
        cp: cp, combining: 0, fg: s.cur_fg, bg: s.cur_bg,
        attrs: s.cur_attrs, width: w as! u8, flags: 0,
    };

    if w == 2 && s.cx + 1 < s.cols {
        s.cells[idx + 1] = Cell {
            cp: 0, combining: 0, fg: s.cur_fg, bg: s.cur_bg,
            attrs: s.cur_attrs, width: 0, flags: CELL_WIDE_CONT,
        };
    }

    mark_row_dirty(s, s.cy);

    if s.cx + w >= s.cols {
        s.cx = s.cols - 1;
        if s.autowrap { s.wrap_pending = true; }
    } else {
        s.cx += w;
    }
}

fn erase_display(s: Screen&, mode: i32) {
    s.wrap_pending = false;
    let ec = erase_cell(s);
    if mode == 0 {
        let start = (s.cy as i64) * (s.cols as i64) + (s.cx as i64);
        for i in start..s.cells.len { s.cells[i] = ec; }
        for r in s.cy..s.rows { mark_row_dirty(s, r); }
    } else if mode == 1 {
        let end = (s.cy as i64) * (s.cols as i64) + (s.cx as i64) + 1;
        for i in 0..end { if i < s.cells.len { s.cells[i] = ec; } }
        for r in 0..(s.cy + 1) { mark_row_dirty(s, r); }
    } else if mode == 2 || mode == 3 {
        for i in 0..s.cells.len { s.cells[i] = ec; }
        for r in 0..s.rows { if (r as i64) < s.prompt_rows.len { s.prompt_rows[r] = 0; } }
        if mode == 3 {
            s.sb_head = 0; s.sb_count = 0; s.view_offset = 0;
        }
        mark_all_dirty(s);
    }
}

fn erase_line(s: Screen&, mode: i32) {
    s.wrap_pending = false;
    let ec = erase_cell(s);
    let base = (s.cy as i64) * (s.cols as i64);
    if mode == 0 {
        for c in s.cx..s.cols { s.cells[base + (c as i64)] = ec; }
    } else if mode == 1 {
        for c in 0..(s.cx + 1) { s.cells[base + (c as i64)] = ec; }
    } else if mode == 2 {
        for c in 0..s.cols { s.cells[base + (c as i64)] = ec; }
    }
    mark_row_dirty(s, s.cy);
}

fn insert_lines(s: Screen&, n: i32) {
    if s.cy >= s.scroll_top && s.cy <= s.scroll_bottom { scroll_down(s, s.cy, s.scroll_bottom, n); }
}

fn delete_lines(s: Screen&, n: i32) {
    if s.cy >= s.scroll_top && s.cy <= s.scroll_bottom { scroll_up(s, s.cy, s.scroll_bottom, n); }
}

fn insert_chars(s: Screen&, n: i32) {
    if n <= 0 { return; }
    let count = if n > (s.cols - s.cx) { s.cols - s.cx } else { n };
    let base = (s.cy as i64) * (s.cols as i64);
    var c = s.cols - 1;
    while c >= s.cx + count {
        s.cells[base + (c as i64)] = s.cells[base + ((c - count) as i64)];
        c -= 1;
    }
    let ec = erase_cell(s);
    for c2 in s.cx..(s.cx + count) { s.cells[base + (c2 as i64)] = ec; }
    mark_row_dirty(s, s.cy);
}

fn delete_chars(s: Screen&, n: i32) {
    if n <= 0 { return; }
    let count = if n > (s.cols - s.cx) { s.cols - s.cx } else { n };
    let base = (s.cy as i64) * (s.cols as i64);
    for c in s.cx..(s.cols - count) {
        s.cells[base + (c as i64)] = s.cells[base + ((c + count) as i64)];
    }
    let ec = erase_cell(s);
    for c2 in (s.cols - count)..s.cols { s.cells[base + (c2 as i64)] = ec; }
    mark_row_dirty(s, s.cy);
}

fn erase_chars(s: Screen&, n: i32) {
    if n <= 0 { return; }
    let count = if n > (s.cols - s.cx) { s.cols - s.cx } else { n };
    let base = (s.cy as i64) * (s.cols as i64);
    let ec = erase_cell(s);
    for c in s.cx..(s.cx + count) { s.cells[base + (c as i64)] = ec; }
    mark_row_dirty(s, s.cy);
}

fn tab_forward(s: Screen&) {
    s.wrap_pending = false;
    var c = s.cx + 1;
    while c < s.cols - 1 {
        if (c as i64) < s.tab_stops.len && s.tab_stops[c] != 0 { break; }
        c += 1;
    }
    if c >= s.cols { c = s.cols - 1; }
    s.cx = c;
    mark_row_dirty(s, s.cy);
}

fn swap_buffers(s: Screen&) {
    let n = if s.cells.len < s.alt_cells.len { s.cells.len } else { s.alt_cells.len };
    for i in 0..n {
        let tmp = s.cells[i]; s.cells[i] = s.alt_cells[i]; s.alt_cells[i] = tmp;
    }
}

fn enter_alt_screen(s: Screen&, save_cursor: bool, clear_alt: bool) {
    if s.alt_screen { return; }
    if save_cursor { s.alt_saved_cx = s.cx; s.alt_saved_cy = s.cy; }
    swap_buffers(s);
    s.alt_screen = true;
    s.view_offset = 0;
    s.scroll_top = 0; s.scroll_bottom = s.rows - 1;
    if clear_alt {
        let b = blank_cell(s);
        for i in 0..s.cells.len { s.cells[i] = b; }
        s.cx = 0; s.cy = 0;
    }
    mark_all_dirty(s);
}

fn leave_alt_screen(s: Screen&, restore_cursor: bool) {
    if !s.alt_screen { return; }
    swap_buffers(s);
    s.alt_screen = false;
    s.scroll_top = 0; s.scroll_bottom = s.rows - 1;
    if restore_cursor { s.cx = s.alt_saved_cx; s.cy = s.alt_saved_cy; clamp_cursor(s); }
    mark_all_dirty(s);
}

fn resize(s: Screen&, new_cols: i32, new_rows: i32) {
    let nc = if new_cols < 2 { 2 } else if new_cols > MAX_COLS { MAX_COLS } else { new_cols };
    let nr = if new_rows < 2 { 2 } else if new_rows > MAX_ROWS { MAX_ROWS } else { new_rows };
    if nc == s.cols && nr == s.rows { return; }

    var next_cells: Cell[>..] = [];
    let b = blank_cell(s);
    let total = (nc as i64) * (nr as i64);
    for _ in 0..total { next_cells.push(b); }

    let min_r = if nr < s.rows { nr } else { s.rows };
    let min_c = if nc < s.cols { nc } else { s.cols };
    for r in 0..min_r {
        let src = (r as i64) * (s.cols as i64);
        let dst = (r as i64) * (nc as i64);
        for c in 0..min_c { next_cells[dst + (c as i64)] = s.cells[src + (c as i64)]; }
    }

    s.cells.clear();
    for i in 0..total { s.cells.push(next_cells[i]); }
    s.alt_cells.clear();
    for _ in 0..total { s.alt_cells.push(b); }

    s.cols = nc; s.rows = nr;
    s.scroll_top = 0; s.scroll_bottom = nr - 1;
    s.wrap_pending = false;
    clamp_cursor(s);

    s.dirty_rows.clear();
    s.prompt_rows.clear();
    for _ in 0..nr { s.dirty_rows.push(2); s.prompt_rows.push(0); }
    reset_tabs(s);
    s.sel_active = false;
    s.full_redraw_frames = 2;
}

fn scroll_viewport(s: Screen&, delta: i32) {
    if s.alt_screen { return; }
    var next = s.view_offset + delta;
    if next < 0 { next = 0; }
    if next > s.sb_count { next = s.sb_count; }
    if next != s.view_offset {
        s.view_offset = next;
        mark_all_dirty(s);
    }
}

// Unified absolute history indexing: abs_line in 0 .. (sb_count + rows - 1)
fn total_history_lines(s: Screen&) -> i32 {
    return if s.alt_screen { s.rows } else { s.sb_count + s.rows };
}

fn history_cell(s: Screen&, abs_line: i32, col: i32) -> Cell {
    if col < 0 || col >= s.cols { return blank_cell(s); }
    if s.alt_screen {
        if abs_line < 0 || abs_line >= s.rows { return blank_cell(s); }
        return s.cells[(abs_line as i64) * (s.cols as i64) + (col as i64)];
    }
    if abs_line < 0 || abs_line >= s.sb_count + s.rows { return blank_cell(s); }
    if abs_line < s.sb_count {
        if col >= s.sb_cols { return blank_cell(s); }
        let line_idx = (s.sb_head + abs_line) % s.sb_cap_lines;
        return s.sb_cells[(line_idx as i64) * (s.sb_cols as i64) + (col as i64)];
    }
    let live_row = abs_line - s.sb_count;
    return s.cells[(live_row as i64) * (s.cols as i64) + (col as i64)];
}

fn history_is_prompt(s: Screen&, abs_line: i32) -> bool {
    if s.alt_screen || abs_line < 0 || abs_line >= s.sb_count + s.rows { return false; }
    if abs_line < s.sb_count {
        let line_idx = (s.sb_head + abs_line) % s.sb_cap_lines;
        return (line_idx as i64) < s.sb_prompt_rows.len && s.sb_prompt_rows[line_idx] != 0;
    }
    let live_row = abs_line - s.sb_count;
    return (live_row as i64) < s.prompt_rows.len && s.prompt_rows[live_row] != 0;
}

// Jump to previous currently-not-visible prompt (Ctrl+Shift+Z, requires OSC 133;A)
fn jump_prev_prompt(s: Screen&) -> bool {
    if s.alt_screen { return false; }
    let top_visible = s.sb_count - s.view_offset;
    var line = top_visible - 1;
    while line >= 0 {
        if history_is_prompt(s, line) {
            s.view_offset = s.sb_count - line;
            if s.view_offset > s.sb_count { s.view_offset = s.sb_count; }
            mark_all_dirty(s);
            return true;
        }
        line -= 1;
    }
    return false;
}

// Jump to next prompt (Ctrl+Shift+X, requires OSC 133;A)
fn jump_next_prompt(s: Screen&) -> bool {
    if s.alt_screen { return false; }
    let top_visible = s.sb_count - s.view_offset;
    let total = s.sb_count + s.rows;
    var line = top_visible + 1;
    while line < total {
        if history_is_prompt(s, line) {
            var next_off = s.sb_count - line;
            if next_off < 0 { next_off = 0; }
            s.view_offset = next_off;
            mark_all_dirty(s);
            return true;
        }
        line += 1;
    }
    return false;
}

fn visible_cell(s: Screen&, col: i32, row: i32) -> Cell {
    if col < 0 || col >= s.cols || row < 0 || row >= s.rows { return blank_cell(s); }
    if s.view_offset == 0 || s.alt_screen {
        return s.cells[(row as i64) * (s.cols as i64) + (col as i64)];
    }
    let abs_line = s.sb_count - s.view_offset + row;
    return history_cell(s, abs_line, col);
}

// -------------------------------------------------------------------------
// Word, Quote, Row, Block & Extend Selection Helpers
// -------------------------------------------------------------------------
fn is_word_sep(cp: u32, whitespace_only: bool) -> bool {
    if cp == 0 || cp == 0x20 || cp == 0x09 { return true; }
    if whitespace_only { return false; }
    return cp == '.' || cp == ',' || cp == '(' || cp == ')' ||
           cp == '[' || cp == ']' || cp == '{' || cp == '}' ||
           cp == ':' || cp == ';' || cp == '"' || cp == '\'' ||
           cp == '`' || cp == '<' || cp == '>' || cp == '|' ||
           cp == '!' || cp == '?';
}

fn select_word(s: Screen&, col: i32, row: i32, whitespace_only: bool) {
    let cur = visible_cell(s, col, row);
    let sep = is_word_sep(cur.cp, whitespace_only);
    var c0 = col;
    while c0 > 0 && is_word_sep(visible_cell(s, c0 - 1, row).cp, whitespace_only) == sep {
        c0 -= 1;
    }
    var c1 = col;
    while c1 < s.cols - 1 && is_word_sep(visible_cell(s, c1 + 1, row).cp, whitespace_only) == sep {
        c1 += 1;
    }
    s.sel_active = true;
    s.sel_mode = SEL_WORD;
    s.sel_x0 = c0; s.sel_y0 = row;
    s.sel_x1 = c1; s.sel_y1 = row;
    mark_all_dirty(s);
}

fn select_line(s: Screen&, row: i32) {
    s.sel_active = true;
    s.sel_mode = SEL_LINE;
    s.sel_x0 = 0; s.sel_y0 = row;
    s.sel_x1 = s.cols - 1; s.sel_y1 = row;
    mark_all_dirty(s);
}

// Triple-click: select inside enclosing quotes (", ', `), or entire row if not inside quotes.
fn select_quote_or_line(s: Screen&, col: i32, row: i32) {
    var left_q: i32 = -1;
    var qchar: u32 = 0;
    var c = col;
    while c >= 0 {
        let cp = visible_cell(s, c, row).cp;
        if cp == '"' || cp == '\'' || cp == '`' {
            left_q = c;
            qchar = cp;
            break;
        }
        c -= 1;
    }
    if left_q >= 0 {
        var right_q: i32 = -1;
        var c2 = col + 1;
        while c2 < s.cols {
            if visible_cell(s, c2, row).cp == qchar {
                right_q = c2;
                break;
            }
            c2 += 1;
        }
        if right_q > left_q + 1 {
            s.sel_active = true;
            s.sel_mode = SEL_QUOTE;
            s.sel_x0 = left_q + 1; s.sel_y0 = row;
            s.sel_x1 = right_q - 1; s.sel_y1 = row;
            mark_all_dirty(s);
            return;
        }
    }
    select_line(s, row);
}

// Right-click / Ctrl+Right-click: extend current selection
fn extend_selection(s: Screen&, col: i32, row: i32, force_char: bool) {
    if !s.sel_active {
        s.sel_active = true;
        s.sel_mode = SEL_CHAR;
        s.sel_x0 = col; s.sel_y0 = row;
        s.sel_x1 = col; s.sel_y1 = row;
        mark_all_dirty(s);
        return;
    }
    if force_char {
        s.sel_mode = SEL_CHAR;
    }
    if s.sel_mode == SEL_LINE && !force_char {
        if row < s.sel_y0 { s.sel_y0 = row; s.sel_x0 = 0; }
        else { s.sel_y1 = row; s.sel_x1 = s.cols - 1; }
    } else {
        s.sel_x1 = col;
        s.sel_y1 = row;
    }
    mark_all_dirty(s);
}

fn is_selected(s: Screen&, col: i32, row: i32) -> bool {
    if !s.sel_active { return false; }
    if s.sel_mode == SEL_BLOCK {
        let min_x = if s.sel_x0 < s.sel_x1 { s.sel_x0 } else { s.sel_x1 };
        let max_x = if s.sel_x0 > s.sel_x1 { s.sel_x0 } else { s.sel_x1 };
        let min_y = if s.sel_y0 < s.sel_y1 { s.sel_y0 } else { s.sel_y1 };
        let max_y = if s.sel_y0 > s.sel_y1 { s.sel_y0 } else { s.sel_y1 };
        return col >= min_x && col <= max_x && row >= min_y && row <= max_y;
    }
    var y0 = s.sel_y0; var x0 = s.sel_x0;
    var y1 = s.sel_y1; var x1 = s.sel_x1;
    if y0 > y1 || (y0 == y1 && x0 > x1) {
        let ty = y0; y0 = y1; y1 = ty;
        let tx = x0; x0 = x1; x1 = tx;
    }
    if row < y0 || row > y1 { return false; }
    if row == y0 && col < x0 { return false; }
    if row == y1 && col > x1 { return false; }
    return true;
}

fn extract_selection(s: Screen&, out: u8[>..]&) {
    out.clear();
    if !s.sel_active { return; }

    var y0 = s.sel_y0; var x0 = s.sel_x0;
    var y1 = s.sel_y1; var x1 = s.sel_x1;
    if s.sel_mode == SEL_BLOCK {
        let min_x = if x0 < x1 { x0 } else { x1 };
        let max_x = if x0 > x1 { x0 } else { x1 };
        let min_y = if y0 < y1 { y0 } else { y1 };
        let max_y = if y0 > y1 { y0 } else { y1 };
        for r in min_y..(max_y + 1) {
            var last_ns = min_x - 1;
            for c in min_x..(max_x + 1) {
                let cell = visible_cell(s, c, r);
                if cell.flags == 0 && cell.cp != 0x20 && cell.cp != 0 { last_ns = c; }
            }
            if last_ns >= min_x {
                for c in min_x..(last_ns + 1) {
                    let cell = visible_cell(s, c, r);
                    if cell.flags == CELL_WIDE_CONT { continue; }
                    utf8::encode_to(out, if cell.cp == 0 { 0x20 } else { cell.cp });
                }
            }
            if r < max_y { out.push('\n'); }
        }
        return;
    }

    if y0 > y1 || (y0 == y1 && x0 > x1) {
        let ty = y0; y0 = y1; y1 = ty;
        let tx = x0; x0 = x1; x1 = tx;
    }
    if y0 < 0 { y0 = 0; }
    if y1 >= s.rows { y1 = s.rows - 1; }

    for r in y0..(y1 + 1) {
        let c_start = if r == y0 { x0 } else { 0 };
        let c_end   = if r == y1 { x1 } else { s.cols - 1 };
        var last_nonspace = c_start - 1;
        for c in c_start..(c_end + 1) {
            let cell = visible_cell(s, c, r);
            if cell.flags == 0 && cell.cp != 0x20 && cell.cp != 0 { last_nonspace = c; }
        }
        if last_nonspace >= c_start {
            for c in c_start..(last_nonspace + 1) {
                let cell = visible_cell(s, c, r);
                if cell.flags == CELL_WIDE_CONT { continue; }
                utf8::encode_to(out, if cell.cp == 0 { 0x20 } else { cell.cp });
                if cell.combining != 0 { utf8::encode_to(out, cell.combining); }
            }
        }
        if r < y1 { out.push('\n'); }
    }
}
