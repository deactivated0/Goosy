// Goosy — Terminal Screen Grid, Alternate Buffer, Bounded Scrollback,
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

let UL_NONE: u8   = 0;
let UL_SINGLE: u8 = 1;
let UL_DOUBLE: u8 = 2;
let UL_CURLY: u8  = 3;
let UL_DOTTED: u8 = 4;
let UL_DASHED: u8 = 5;

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
    bg: u32 = 0x000000,
    attrs: u16 = 0,
    width: u8 = 1,
    flags: u8 = 0,
    ul_style: u8 = 0,
    ul_color: u32 = 0,
    link_id: u32 = 0,
}

root struct Screen {
    cols: i32 = 80,
    rows: i32 = 24,
    grid_offset: i32 = 0,
    cx: i32 = 0,
    cy: i32 = 0,
    saved_cx: i32 = 0,
    saved_cy: i32 = 0,
    saved_fg: u32 = 0xEAEAEA,
    saved_bg: u32 = 0x000000,
    saved_attrs: u16 = 0,
    saved_ul_style: u8 = 0,
    saved_ul_color: u32 = 0,
    saved_autowrap: bool = true,
    saved_origin_mode: bool = false,

    alt_saved_cx: i32 = 0,
    alt_saved_cy: i32 = 0,
    alt_saved_fg: u32 = 0xEAEAEA,
    alt_saved_bg: u32 = 0x000000,
    alt_saved_attrs: u16 = 0,
    alt_saved_ul_style: u8 = 0,
    alt_saved_ul_color: u32 = 0,
    alt_saved_autowrap: bool = true,
    alt_saved_origin_mode: bool = false,
    wrap_pending: bool = false,

    scroll_top: i32 = 0,
    scroll_bottom: i32 = 23,

    cur_fg: u32 = 0xEAEAEA,
    cur_bg: u32 = 0x000000,
    cur_attrs: u16 = 0,
    cur_ul_style: u8 = 0,
    cur_ul_color: u32 = 0,
    cur_link_id: u32 = 0,
    default_fg: u32 = 0xEAEAEA,
    default_bg: u32 = 0x000000,

    alt_screen: bool = false,
    cursor_visible: bool = true,
    cursor_style: i32 = 0,
    app_cursor: bool = false,
    bracketed_paste: bool = false,
    autowrap: bool = true,
    origin_mode: bool = false,
    mouse_mode: i32 = 0,
    mouse_sgr: bool = false,
    kitty_keyboard_flags: i32 = 0,
    kitty_keyboard_stack: i32[..16] = [],

    full_redraw_frames: i32 = 2,
    title_dirty: bool = false,
    title: u8[..256] = "Goosy",
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
    wrapped_rows: u8[>..] = [],
    sb_wrapped_rows: u8[>..] = [],
    hyperlinks: u8[>..] = [],
    hyperlink_offsets: i32[>..] = [],
    hyperlink_lengths: i32[>..] = [],
    reply_buf: u8[>..] = [],
}

fn add_hyperlink(s: Screen&, url: const u8[:]) -> u32 {
    if url.len == 0 { return 0; }
    let id = (s.hyperlink_offsets.len as u32) + 1;
    let off = s.hyperlinks.len as i32;
    s.hyperlink_offsets.push(off);
    s.hyperlink_lengths.push(url.len as i32);
    for i in 0..url.len { s.hyperlinks.push(url[i]); }
    return id;
}

fn get_hyperlink(s: Screen&, id: u32, out: u8[>..]&) -> bool {
    out.clear();
    if id == 0 { return false; }
    let idx = (id - 1) as i64;
    if idx < 0 || idx >= s.hyperlink_offsets.len { return false; }
    let off = s.hyperlink_offsets[idx] as i64;
    let len = s.hyperlink_lengths[idx] as i64;
    if off < 0 || off + len > s.hyperlinks.len { return false; }
    for i in 0..len { out.push(s.hyperlinks[off + i]); }
    return true;
}

extern fn cells_scroll_up(cells: Cell[:], cols: i32, top: i32, bottom: i32, count: i32) = "goosy_cells_scroll_up";
extern fn cells_scroll_down(cells: Cell[:], cols: i32, top: i32, bottom: i32, count: i32) = "goosy_cells_scroll_down";
extern fn screen_scroll_up(cells: Cell[:], cols: i32, top: i32, bottom: i32, count: i32,
                           prompt_rows: u8[:], wrapped_rows: u8[:], dirty_rows: u8[:],
                           fg: u32, bg: u32) = "goosy_screen_scroll_up";
extern fn screen_push_scrollback(sb_cells: Cell[:], sb_cols: i32, dst_line: i32,
                                 cells: Cell[:], cols: i32, row: i32,
                                 sb_prompt: u8[:], sb_wrapped: u8[:],
                                 prompt_rows: const u8[:], wrapped_rows: const u8[:]) = "goosy_push_scrollback";
extern fn cells_copy(dst: Cell[:], dst_offset: i64, src: Cell[:], src_offset: i64, count: i64) = "goosy_cells_copy";
extern fn cells_fill(cells: Cell[:], start_idx: i64, count: i64, fg: u32, bg: u32) = "goosy_cells_fill";
extern fn cells_normalize_grid(cells: Cell[:], cols: i32, rows: i32, offset: i32,
                               prompt_rows: u8[:], wrapped_rows: u8[:]) = "goosy_cells_normalize_grid";

fn push_kitty_keyboard(s: Screen&, flags: i32) {
    if s.kitty_keyboard_stack.len < 16 {
        s.kitty_keyboard_stack.push(s.kitty_keyboard_flags);
    }
    s.kitty_keyboard_flags = flags;
}

fn pop_kitty_keyboard(s: Screen&, count: i32) {
    var n = if count <= 0 { 1 } else { count };
    while n > 0 && s.kitty_keyboard_stack.len > 0 {
        s.kitty_keyboard_flags = s.kitty_keyboard_stack.pop();
        n -= 1;
    }
    if s.kitty_keyboard_stack.len == 0 && n > 0 {
        s.kitty_keyboard_flags = 0;
    }
}

fn phys_row(s: Screen&, r: i32) -> i32 {
    if s.rows <= 0 { return 0; }
    let pr = (s.grid_offset + r) % s.rows;
    return if pr < 0 { pr + s.rows } else { pr };
}

fn normalize_grid(s: Screen&) {
    if s.grid_offset == 0 { return; }
    cells_normalize_grid(s.cells[..], s.cols, s.rows, s.grid_offset,
                         s.prompt_rows[..], s.wrapped_rows[..]);
    s.grid_offset = 0;
}

fn blank_cell(s: Screen&) -> Cell {
    return Cell {
        cp: 0x20, combining: 0, fg: s.default_fg, bg: s.default_bg,
        attrs: 0, width: 1, flags: 0,
        ul_style: 0, ul_color: 0, link_id: 0,
    };
}

fn erase_cell(s: Screen&) -> Cell {
    return Cell {
        cp: 0x20, combining: 0, fg: s.cur_fg, bg: s.cur_bg,
        attrs: 0, width: 1, flags: 0,
        ul_style: 0, ul_color: 0, link_id: 0,
    };
}

fn mark_row_dirty(s: Screen&, row: i32) {
    if row >= 0 && row < s.rows && row < s.dirty_rows.len {
        s.dirty_rows[row] = 2;
    }
}

fn mark_all_dirty(s: Screen&) {
    s.full_redraw_frames = 2;
    for r in 0..s.rows {
        if r < s.dirty_rows.len { s.dirty_rows[r] = 2; }
    }
}

fn clear_dirty(s: Screen&) {
    if s.full_redraw_frames > 0 { s.full_redraw_frames -= 1; }
    for r in 0..s.rows {
        if r < s.dirty_rows.len && s.dirty_rows[r] > 0 {
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
    s.cx = 0; s.cy = 0;
    s.saved_cx = 0; s.saved_cy = 0;
    s.saved_fg = fg; s.saved_bg = bg; s.saved_attrs = 0;
    s.saved_ul_style = 0; s.saved_ul_color = 0;
    s.saved_autowrap = true; s.saved_origin_mode = false;
    s.alt_saved_cx = 0; s.alt_saved_cy = 0;
    s.alt_saved_fg = fg; s.alt_saved_bg = bg; s.alt_saved_attrs = 0;
    s.alt_saved_ul_style = 0; s.alt_saved_ul_color = 0;
    s.alt_saved_autowrap = true; s.alt_saved_origin_mode = false;
    s.wrap_pending = false;
    s.scroll_top = 0; s.scroll_bottom = r - 1;
    s.default_fg = fg; s.default_bg = bg;
    s.cur_fg = fg; s.cur_bg = bg; s.cur_attrs = 0;
    s.cur_ul_style = 0; s.cur_ul_color = 0; s.cur_link_id = 0;
    s.cursor_style = cursor_style;
    s.cursor_visible = true;
    s.alt_screen = false;
    s.app_cursor = false;
    s.bracketed_paste = false;
    s.autowrap = true;
    s.origin_mode = false;
    s.mouse_mode = 0;
    s.mouse_sgr = false;
    s.kitty_keyboard_flags = 0;
    s.kitty_keyboard_stack.clear();

    let total = (c as i64) * r;
    let b = blank_cell(s);
    s.cells.clear();
    s.alt_cells.clear();
    for _ in 0..total {
        s.cells.push(b);
        s.alt_cells.push(b);
    }

    s.dirty_rows.clear();
    s.prompt_rows.clear();
    s.wrapped_rows.clear();
    for _ in 0..r {
        s.dirty_rows.push(2);
        s.prompt_rows.push(0);
        s.wrapped_rows.push(0);
    }
    reset_tabs(s);

    s.hyperlinks.clear();
    s.hyperlink_offsets.clear();
    s.hyperlink_lengths.clear();

    let cap = if sb_lines < 0 { 0 } else if sb_lines > 100000 { 100000 } else { sb_lines };
    s.sb_cap_lines = cap;
    s.sb_cols = c;
    s.sb_head = 0;
    s.sb_count = 0;
    s.view_offset = 0;
    s.sb_cells.clear();
    s.sb_prompt_rows.clear();
    s.sb_wrapped_rows.clear();
    s.grid_offset = 0;
    // On-demand allocation: avoid allocating up to 100,000 blank rows at startup.
    // Rows are grown dynamically in push_scrollback_row, saving ~20 MB idle RSS and cutting startup time.
    s.full_redraw_frames = 2;
}

fn clamp_cursor(s: Screen&) {
    if s.cx < 0 { s.cx = 0; }
    if s.cx >= s.cols { s.cx = s.cols - 1; }
    let min_y = if s.origin_mode { s.scroll_top } else { 0 };
    let max_y = if s.origin_mode { s.scroll_bottom } else { s.rows - 1 };
    if s.cy < min_y { s.cy = min_y; }
    if s.cy > max_y { s.cy = max_y; }
}

fn save_cursor(s: Screen&) {
    s.saved_cx = s.cx;
    s.saved_cy = s.cy;
    s.saved_fg = s.cur_fg;
    s.saved_bg = s.cur_bg;
    s.saved_attrs = s.cur_attrs;
    s.saved_ul_style = s.cur_ul_style;
    s.saved_ul_color = s.cur_ul_color;
    s.saved_autowrap = s.autowrap;
    s.saved_origin_mode = s.origin_mode;
}

fn restore_cursor(s: Screen&) {
    s.cx = s.saved_cx;
    s.cy = s.saved_cy;
    s.cur_fg = s.saved_fg;
    s.cur_bg = s.saved_bg;
    s.cur_attrs = s.saved_attrs;
    s.cur_ul_style = s.saved_ul_style;
    s.cur_ul_color = s.saved_ul_color;
    s.autowrap = s.saved_autowrap;
    s.origin_mode = s.saved_origin_mode;
    s.wrap_pending = false;
    clamp_cursor(s);
}

fn clean_wide_boundary(s: Screen&, base: i64, col: i32) {
    if col < 0 || col >= s.cols { return; }
    let idx = base + col;
    if idx < 0 || idx >= s.cells.len { return; }
    let cell = s.cells[idx];
    if cell.flags == CELL_WIDE_CONT {
        if col > 0 {
            let left_idx = base + (col - 1);
            if left_idx >= 0 && left_idx < s.cells.len {
                s.cells[left_idx] = erase_cell(s);
            }
        }
    } else if cell.width == 2 {
        if col + 1 < s.cols {
            let right_idx = base + (col + 1);
            if right_idx >= 0 && right_idx < s.cells.len {
                s.cells[right_idx] = erase_cell(s);
            }
        }
    }
}

fn set_cursor(s: Screen&, x: i32, y: i32) {
    mark_row_dirty(s, s.cy);
    s.cx = x; s.cy = y;
    s.wrap_pending = false;
    clamp_cursor(s);
    mark_row_dirty(s, s.cy);
}

fn mark_prompt_row(s: Screen&) {
    if !s.alt_screen && s.cy >= 0 && s.cy < s.rows {
        let pr = phys_row(s, s.cy);
        if pr < s.prompt_rows.len {
            s.prompt_rows[pr] = 1;
        }
    }
}

fn push_scrollback_row(s: Screen&, row: i32) {
    if s.alt_screen || s.sb_cap_lines <= 0 || row < 0 || row >= s.rows { return; }
    var dst_line = s.sb_head + s.sb_count;
    if dst_line >= s.sb_cap_lines { dst_line -= s.sb_cap_lines; }
    if s.sb_count == s.sb_cap_lines {
        s.sb_head += 1;
        if s.sb_head >= s.sb_cap_lines { s.sb_head = 0; }
        ffi::image_scroll(1);
        if s.sel_active {
            s.sel_y0 -= 1;
            s.sel_y1 -= 1;
            if s.sel_y0 < 0 && s.sel_y1 < 0 {
                s.sel_active = false;
            }
        }
    } else {
        s.sb_count += 1;
    }

    let dst_base = (dst_line as i64) * s.sb_cols;
    let b = blank_cell(s);
    let target = dst_base + s.sb_cols;
    if s.sb_cells.len < target {
        var new_cap = s.sb_cells.len + (512 as i64) * s.sb_cols;
        let max_cap = (s.sb_cap_lines as i64) * s.sb_cols;
        if new_cap > max_cap { new_cap = max_cap; }
        if new_cap < target { new_cap = target; }
        while s.sb_cells.len < new_cap {
            s.sb_cells.push(b);
        }
    }

    while s.sb_prompt_rows.len <= dst_line { s.sb_prompt_rows.push(0); }
    while s.sb_wrapped_rows.len <= dst_line { s.sb_wrapped_rows.push(0); }

    screen_push_scrollback(s.sb_cells[..], s.sb_cols, dst_line,
                           s.cells[..], s.cols, phys_row(s, row),
                           s.sb_prompt_rows[..], s.sb_wrapped_rows[..],
                           s.prompt_rows[..], s.wrapped_rows[..]);
}

fn scroll_up(s: Screen&, top: i32, bottom: i32, n: i32) {
    if n <= 0 || top < 0 || bottom >= s.rows || top > bottom { return; }
    let count = if n > (bottom - top + 1) { bottom - top + 1 } else { n };

    if top == 0 && bottom == s.rows - 1 {
        if !s.alt_screen {
            for i in 0..count { push_scrollback_row(s, i); }
        }
        s.grid_offset = (s.grid_offset + count) % s.rows;
        let ec = erase_cell(s);
        for i in 0..count {
            let log_r = s.rows - count + i;
            let pr = phys_row(s, log_r);
            let base = (pr as i64) * s.cols;
            cells_fill(s.cells[..], base, s.cols, ec.fg, ec.bg);
            if pr < s.prompt_rows.len { s.prompt_rows[pr] = 0; }
            if pr < s.wrapped_rows.len { s.wrapped_rows[pr] = 0; }
        }
        s.full_redraw_frames = 2;
        for r in 0..s.rows {
            if r < s.dirty_rows.len { s.dirty_rows[r] = 2; }
        }
        if s.alt_screen && s.sel_active {
            s.sel_y0 -= count;
            s.sel_y1 -= count;
            if s.sel_y0 < 0 && s.sel_y1 < 0 {
                s.sel_active = false;
            }
        }
        return;
    }

    normalize_grid(s);
    s.full_redraw_frames = 2;
    let ec = erase_cell(s);
    screen_scroll_up(s.cells[..], s.cols, top, bottom, count,
                     s.prompt_rows[..], s.wrapped_rows[..], s.dirty_rows[..],
                     ec.fg, ec.bg);
}

fn scroll_down(s: Screen&, top: i32, bottom: i32, n: i32) {
    if n <= 0 || top < 0 || bottom >= s.rows || top > bottom { return; }
    let count = if n > (bottom - top + 1) { bottom - top + 1 } else { n };

    if top == 0 && bottom == s.rows - 1 {
        s.grid_offset = (s.grid_offset - count) % s.rows;
        if s.grid_offset < 0 { s.grid_offset += s.rows; }
        let ec = erase_cell(s);
        for i in 0..count {
            let pr = phys_row(s, i);
            let base = (pr as i64) * s.cols;
            cells_fill(s.cells[..], base, s.cols, ec.fg, ec.bg);
            if pr < s.prompt_rows.len { s.prompt_rows[pr] = 0; }
            if pr < s.wrapped_rows.len { s.wrapped_rows[pr] = 0; }
        }
        s.full_redraw_frames = 2;
        for r in 0..s.rows {
            if r < s.dirty_rows.len { s.dirty_rows[r] = 2; }
        }
        if s.alt_screen && s.sel_active {
            s.sel_y0 += count;
            s.sel_y1 += count;
            if s.sel_y0 >= s.rows && s.sel_y1 >= s.rows {
                s.sel_active = false;
            }
        }
        return;
    }

    normalize_grid(s);
    s.full_redraw_frames = 2;
    let row_count = bottom - count + 1 - top;
    cells_scroll_down(s.cells[..], s.cols, top, bottom, count);
    var r = row_count - 1;
    while r >= 0 {
        let dst_r = top + count + r;
        let src_r = top + r;
        if dst_r < s.prompt_rows.len && src_r < s.prompt_rows.len {
            s.prompt_rows[dst_r] = s.prompt_rows[src_r];
        }
        if dst_r < s.wrapped_rows.len && src_r < s.wrapped_rows.len {
            s.wrapped_rows[dst_r] = s.wrapped_rows[src_r];
        }
        r -= 1;
    }
    for r in top..(bottom + 1) {
        if r < s.dirty_rows.len { s.dirty_rows[r] = 2; }
    }

    let ec = erase_cell(s);
    let clear_start = (top as i64) * s.cols;
    let clear_total = (count as i64) * s.cols;
    cells_fill(s.cells[..], clear_start, clear_total, ec.fg, ec.bg);
    for r in top..(top + count) {
        if r < s.prompt_rows.len { s.prompt_rows[r] = 0; }
        if r < s.wrapped_rows.len { s.wrapped_rows[r] = 0; }
    }
}

fn linefeed(s: Screen&) {
    s.wrap_pending = false;
    if s.cy == s.scroll_bottom {
        scroll_up(s, s.scroll_top, s.scroll_bottom, 1);
    } else {
        mark_row_dirty(s, s.cy);
        if s.cy < s.rows - 1 {
            s.cy += 1;
        }
        mark_row_dirty(s, s.cy);
    }
}

fn reverse_index(s: Screen&) {
    s.wrap_pending = false;
    if s.cy == s.scroll_top {
        scroll_down(s, s.scroll_top, s.scroll_bottom, 1);
    } else {
        mark_row_dirty(s, s.cy);
        if s.cy > 0 {
            s.cy -= 1;
        }
        mark_row_dirty(s, s.cy);
    }
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
            let pr = phys_row(s, s.cy);
            let idx = (pr as i64) * s.cols + prev_x;
            if idx >= 0 && idx < s.cells.len {
                s.cells[idx].combining = cp;
                mark_row_dirty(s, s.cy);
            }
        }
        return;
    }

    if s.wrap_pending {
        s.wrap_pending = false;
        if s.autowrap {
            let pr = phys_row(s, s.cy);
            if pr >= 0 && pr < s.wrapped_rows.len {
                s.wrapped_rows[pr] = 1;
            }
            s.cx = 0; linefeed(s);
        }
    }

    if w == 2 && s.cx == s.cols - 1 && s.autowrap {
        let pr = phys_row(s, s.cy);
        let pad_idx = (pr as i64) * s.cols + s.cx;
        s.cells[pad_idx] = erase_cell(s);
        mark_row_dirty(s, s.cy);
        if pr >= 0 && pr < s.wrapped_rows.len {
            s.wrapped_rows[pr] = 1;
        }
        s.cx = 0;
        linefeed(s);
    }

    let pr = phys_row(s, s.cy);
    let base = (pr as i64) * s.cols;
    clean_wide_boundary(s, base, s.cx);
    if w == 2 {
        clean_wide_boundary(s, base, s.cx + 1);
    }
    let idx = base + s.cx;
    s.cells[idx] = Cell {
        cp: cp, combining: 0, fg: s.cur_fg, bg: s.cur_bg,
        attrs: s.cur_attrs, width: w as! u8, flags: 0,
        ul_style: s.cur_ul_style, ul_color: s.cur_ul_color, link_id: s.cur_link_id,
    };

    if w == 2 && s.cx + 1 < s.cols {
        s.cells[idx + 1] = Cell {
            cp: 0, combining: 0, fg: s.cur_fg, bg: s.cur_bg,
            attrs: s.cur_attrs, width: 0, flags: CELL_WIDE_CONT,
            ul_style: s.cur_ul_style, ul_color: s.cur_ul_color, link_id: s.cur_link_id,
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
        let cur_pr = phys_row(s, s.cy);
        let cur_base = (cur_pr as i64) * s.cols;
        clean_wide_boundary(s, cur_base, s.cx);
        for c in s.cx..s.cols { s.cells[cur_base + c] = ec; }
        for r in (s.cy + 1)..s.rows {
            let pr = phys_row(s, r);
            let b = (pr as i64) * s.cols;
            for c in 0..s.cols { s.cells[b + c] = ec; }
        }
        for r in s.cy..s.rows { mark_row_dirty(s, r); }
    } else if mode == 1 {
        for r in 0..s.cy {
            let pr = phys_row(s, r);
            let b = (pr as i64) * s.cols;
            for c in 0..s.cols { s.cells[b + c] = ec; }
        }
        let cur_pr = phys_row(s, s.cy);
        let cur_base = (cur_pr as i64) * s.cols;
        clean_wide_boundary(s, cur_base, s.cx);
        for c in 0..(s.cx + 1) { if c < s.cols { s.cells[cur_base + c] = ec; } }
        for r in 0..(s.cy + 1) { mark_row_dirty(s, r); }
    } else if mode == 2 || mode == 3 {
        for i in 0..s.cells.len { s.cells[i] = ec; }
        for r in 0..s.rows { if r < s.prompt_rows.len { s.prompt_rows[r] = 0; } }
        s.grid_offset = 0;
        if mode == 3 {
            s.sb_head = 0; s.sb_count = 0; s.view_offset = 0;
        }
        s.full_redraw_frames = 2;
        mark_all_dirty(s);
    }
}

fn erase_line(s: Screen&, mode: i32) {
    s.wrap_pending = false;
    let ec = erase_cell(s);
    let base = (phys_row(s, s.cy) as i64) * s.cols;
    if mode == 0 {
        clean_wide_boundary(s, base, s.cx);
        for c in s.cx..s.cols { s.cells[base + c] = ec; }
    } else if mode == 1 {
        clean_wide_boundary(s, base, s.cx);
        for c in 0..(s.cx + 1) { s.cells[base + c] = ec; }
    } else if mode == 2 {
        for c in 0..s.cols { s.cells[base + c] = ec; }
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
    let base = (phys_row(s, s.cy) as i64) * s.cols;
    clean_wide_boundary(s, base, s.cx);
    clean_wide_boundary(s, base, s.cols - 1);
    var c = s.cols - 1;
    while c >= s.cx + count {
        s.cells[base + c] = s.cells[base + (c - count)];
        c -= 1;
    }
    let ec = erase_cell(s);
    for c2 in s.cx..(s.cx + count) { s.cells[base + c2] = ec; }
    mark_row_dirty(s, s.cy);
}

fn delete_chars(s: Screen&, n: i32) {
    if n <= 0 { return; }
    let count = if n > (s.cols - s.cx) { s.cols - s.cx } else { n };
    let base = (phys_row(s, s.cy) as i64) * s.cols;
    clean_wide_boundary(s, base, s.cx);
    clean_wide_boundary(s, base, s.cols - count);
    for c in s.cx..(s.cols - count) {
        s.cells[base + c] = s.cells[base + (c + count)];
    }
    let ec = erase_cell(s);
    for c2 in (s.cols - count)..s.cols { s.cells[base + c2] = ec; }
    mark_row_dirty(s, s.cy);
}

fn erase_chars(s: Screen&, n: i32) {
    if n <= 0 { return; }
    let count = if n > (s.cols - s.cx) { s.cols - s.cx } else { n };
    let base = (phys_row(s, s.cy) as i64) * s.cols;
    clean_wide_boundary(s, base, s.cx);
    clean_wide_boundary(s, base, s.cx + count - 1);
    let ec = erase_cell(s);
    for c in s.cx..(s.cx + count) { s.cells[base + c] = ec; }
    mark_row_dirty(s, s.cy);
}

fn tab_forward(s: Screen&) {
    s.wrap_pending = false;
    var c = s.cx + 1;
    while c < s.cols - 1 {
        if c < s.tab_stops.len && s.tab_stops[c] != 0 { break; }
        c += 1;
    }
    if c >= s.cols { c = s.cols - 1; }
    s.cx = c;
    mark_row_dirty(s, s.cy);
}

fn swap_buffers(s: Screen&) {
    normalize_grid(s);
    let n = if s.cells.len < s.alt_cells.len { s.cells.len } else { s.alt_cells.len };
    for i in 0..n {
        let tmp = s.cells[i]; s.cells[i] = s.alt_cells[i]; s.alt_cells[i] = tmp;
    }
}

fn enter_alt_screen(s: Screen&, save_cursor: bool, clear_alt: bool) {
    if s.alt_screen { return; }
    if save_cursor {
        s.alt_saved_cx = s.cx;
        s.alt_saved_cy = s.cy;
        s.alt_saved_fg = s.cur_fg;
        s.alt_saved_bg = s.cur_bg;
        s.alt_saved_attrs = s.cur_attrs;
        s.alt_saved_ul_style = s.cur_ul_style;
        s.alt_saved_ul_color = s.cur_ul_color;
        s.alt_saved_autowrap = s.autowrap;
        s.alt_saved_origin_mode = s.origin_mode;
    }
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
    s.mouse_mode = 0;
    s.mouse_sgr = false;
    s.kitty_keyboard_flags = 0;
    s.kitty_keyboard_stack.clear();
    s.scroll_top = 0; s.scroll_bottom = s.rows - 1;
    if restore_cursor {
        s.cx = s.alt_saved_cx;
        s.cy = s.alt_saved_cy;
        s.cur_fg = s.alt_saved_fg;
        s.cur_bg = s.alt_saved_bg;
        s.cur_attrs = s.alt_saved_attrs;
        s.cur_ul_style = s.alt_saved_ul_style;
        s.cur_ul_color = s.alt_saved_ul_color;
        s.autowrap = s.alt_saved_autowrap;
        s.origin_mode = s.alt_saved_origin_mode;
        s.wrap_pending = false;
        clamp_cursor(s);
    }
    mark_all_dirty(s);
}

fn resize(s: Screen&, new_cols: i32, new_rows: i32) {
    let nc = if new_cols < 2 { 2 } else if new_cols > MAX_COLS { MAX_COLS } else { new_cols };
    let nr = if new_rows < 2 { 2 } else if new_rows > MAX_ROWS { MAX_ROWS } else { new_rows };
    if nc == s.cols && nr == s.rows { return; }
    normalize_grid(s);

    let b = blank_cell(s);

    if s.alt_screen {
        let total = (nc as i64) * nr;
        var next_cells: Cell[>..] = [];
        for _ in 0..total { next_cells.push(b); }
        let min_r = if nr < s.rows { nr } else { s.rows };
        let min_c = if nc < s.cols { nc } else { s.cols };
        for r in 0..min_r {
            let src = (r as i64) * s.cols;
            let dst = (r as i64) * nc;
            for c in 0..min_c { next_cells[dst + c] = s.cells[src + c]; }
        }
        s.cells.clear();
        for i in 0..total { s.cells.push(next_cells[i]); }

        var next_alt: Cell[>..] = [];
        for _ in 0..total { next_alt.push(b); }
        for r in 0..min_r {
            let src = (r as i64) * s.cols;
            let dst = (r as i64) * nc;
            for c in 0..min_c {
                if src + c < s.alt_cells.len {
                    next_alt[dst + c] = s.alt_cells[src + c];
                }
            }
        }
        s.alt_cells.clear();
        for i in 0..total { s.alt_cells.push(next_alt[i]); }

        s.cols = nc; s.rows = nr;
        s.scroll_top = 0; s.scroll_bottom = nr - 1;
        clamp_cursor(s);
        reset_tabs(s);
        s.prompt_rows.clear();
        s.wrapped_rows.clear();
        s.dirty_rows.clear();
        for _ in 0..nr {
            s.prompt_rows.push(0);
            s.wrapped_rows.push(0);
            s.dirty_rows.push(2);
        }
        s.sel_active = false;
        s.full_redraw_frames = 2;
        return;
    }

    // Normal screen reflow:
    // 1. Determine active content boundary on the live screen.
    // Any trailing blank rows strictly below max(cursor_y, last_non_space_row)
    // are empty padding and must NOT be collected into logical lines or pushed into scrollback!
    var max_live_row = s.cy;
    if max_live_row < 0 { max_live_row = 0; }
    if max_live_row >= s.rows { max_live_row = s.rows - 1; }
    for r in (max_live_row + 1)..s.rows {
        let pr = phys_row(s, r);
        let base = (pr as i64) * s.cols;
        var has_content = false;
        for c in 0..s.cols {
            let cell = s.cells[base + c];
            if cell.cp != 0x20 || cell.attrs != 0 || cell.combining != 0 || cell.link_id != 0 {
                has_content = true;
                break;
            }
        }
        if has_content {
            max_live_row = r;
        }
    }

    let live_lines_count = max_live_row + 1;
    let total_src_lines = s.sb_count + live_lines_count;
    let cursor_abs_r = s.sb_count + s.cy;

    var logical_lines: Cell[>..] = [];
    var line_offsets: i32[>..] = [];
    var line_lengths: i32[>..] = [];
    var line_prompts: u8[>..] = [];

    var cursor_line_idx: i32 = -1;
    var cursor_char_offset: i32 = -1;

    var line_start: i64 = 0;
    var cur_line_len: i32 = 0;
    var cur_line_prompt: u8 = 0;

    for abs_r in 0..total_src_lines {
        let is_sb = abs_r < s.sb_count;
        let is_wrapped = if is_sb {
            let lidx = (s.sb_head + abs_r) % s.sb_cap_lines;
            if lidx < s.sb_wrapped_rows.len { s.sb_wrapped_rows[lidx] } else { 0 }
        } else {
            let lr = abs_r - s.sb_count;
            if lr < s.wrapped_rows.len { s.wrapped_rows[lr] } else { 0 }
        };
        let is_prompt = if is_sb {
            let lidx = (s.sb_head + abs_r) % s.sb_cap_lines;
            if lidx < s.sb_prompt_rows.len { s.sb_prompt_rows[lidx] } else { 0 }
        } else {
            let lr = abs_r - s.sb_count;
            if lr < s.prompt_rows.len { s.prompt_rows[lr] } else { 0 }
        };
        if cur_line_len == 0 && is_prompt != 0 { cur_line_prompt = 1; }

        let cols_in_row = if is_sb { s.sb_cols } else { s.cols };
        var row_end: i32 = cols_in_row;
        if is_wrapped == 0 {
            while row_end > 0 {
                let cell = history_cell(s, abs_r, row_end - 1);
                if cell.cp != 0x20 || cell.attrs != 0 || cell.combining != 0 || cell.link_id != 0 {
                    break;
                }
                row_end -= 1;
            }
        }

        // If cursor is on this physical line, don't trim past the cursor's column!
        if abs_r == cursor_abs_r {
            let cur_col = if s.cx < 0 { 0 } else if s.cx >= cols_in_row { cols_in_row - 1 } else { s.cx };
            let offset_col = if s.wrap_pending && cur_col == cols_in_row - 1 { cols_in_row } else { cur_col };
            if row_end < offset_col { row_end = offset_col; }
            cursor_line_idx = line_lengths.len as i32;
            cursor_char_offset = cur_line_len + offset_col;
        }

        for c in 0..row_end {
            logical_lines.push(history_cell(s, abs_r, c));
            cur_line_len += 1;
        }

        if is_wrapped == 0 || abs_r == total_src_lines - 1 {
            line_offsets.push(line_start as i32);
            line_lengths.push(cur_line_len);
            line_prompts.push(cur_line_prompt);
            line_start = logical_lines.len;
            cur_line_len = 0;
            cur_line_prompt = 0;
        }
    }

    // 2. Reflow logical lines into new column width nc
    var reflowed_rows: Cell[>..] = [];
    var reflowed_wrapped: u8[>..] = [];
    var reflowed_prompts: u8[>..] = [];
    var cursor_reflowed_row: i32 = -1;
    var cursor_reflowed_col: i32 = 0;

    for l in 0..line_lengths.len {
        let off = line_offsets[l] as i64;
        let len = line_lengths[l];
        let pmt = line_prompts[l];
        let line_start_row = reflowed_wrapped.len as i32;

        if l == cursor_line_idx {
            let sub = if cursor_char_offset >= 0 { cursor_char_offset / nc } else { 0 };
            cursor_reflowed_row = line_start_row + sub;
            cursor_reflowed_col = if cursor_char_offset >= 0 { cursor_char_offset % nc } else { 0 };
        }

        if len == 0 {
            for _ in 0..nc { reflowed_rows.push(b); }
            reflowed_wrapped.push(0);
            reflowed_prompts.push(pmt);
            continue;
        }

        var rem = len;
        var src_idx = off;
        var first_subrow = true;
        while rem > 0 {
            let take = if rem < nc { rem } else { nc };
            for c in 0..nc {
                if c < take {
                    reflowed_rows.push(logical_lines[src_idx]);
                    src_idx += 1;
                } else {
                    reflowed_rows.push(b);
                }
            }
            rem -= take;
            reflowed_wrapped.push(if rem > 0 { 1 } else { 0 });
            reflowed_prompts.push(if first_subrow { pmt } else { 0 });
            first_subrow = false;
        }
    }

    if cursor_reflowed_row < 0 {
        cursor_reflowed_row = 0;
        cursor_reflowed_col = 0;
    }
    let max_reflow_idx = if reflowed_wrapped.len > 0 { (reflowed_wrapped.len as i32) - 1 } else { 0 };
    if cursor_reflowed_row > max_reflow_idx {
        cursor_reflowed_row = max_reflow_idx;
        cursor_reflowed_col = nc - 1;
    }
    if cursor_reflowed_col >= nc { cursor_reflowed_col = nc - 1; }
    if cursor_reflowed_col < 0 { cursor_reflowed_col = 0; }

    // 3. Anchoring viewport and scrollback:
    let total_reflowed = reflowed_wrapped.len as i32;
    var live_start_row: i32 = 0;
    if total_reflowed > nr {
        live_start_row = total_reflowed - nr;
        if cursor_reflowed_row < live_start_row {
            live_start_row = cursor_reflowed_row;
        }
    }
    let sb_count = live_start_row;

    s.cols = nc; s.rows = nr;
    s.sb_cols = nc;
    s.sb_head = 0;
    s.sb_count = if sb_count > s.sb_cap_lines { s.sb_cap_lines } else { sb_count };

    s.cells.clear();
    s.wrapped_rows.clear();
    s.prompt_rows.clear();
    s.dirty_rows.clear();
    for _ in 0..nr {
        s.wrapped_rows.push(0);
        s.prompt_rows.push(0);
        s.dirty_rows.push(2);
    }
    for _ in 0..((nc as i64) * nr) { s.cells.push(b); }
    s.alt_cells.clear();
    for _ in 0..((nc as i64) * nr) { s.alt_cells.push(b); }

    let live_rows_to_copy = if total_reflowed - live_start_row < nr { total_reflowed - live_start_row } else { nr };
    for r in 0..live_rows_to_copy {
        let src_r = live_start_row + r;
        let src_base = (src_r as i64) * nc;
        let dst_base = (r as i64) * nc;
        for c in 0..nc {
            s.cells[dst_base + c] = reflowed_rows[src_base + c];
        }
        s.wrapped_rows[r] = reflowed_wrapped[src_r];
        s.prompt_rows[r] = reflowed_prompts[src_r];
    }

    s.sb_cells.clear();
    s.sb_prompt_rows.clear();
    s.sb_wrapped_rows.clear();
    s.sb_cols = nc;
    s.sb_head = 0;
    s.sb_count = 0;
    if s.sb_cap_lines > 0 && sb_count > 0 {
        let sb_take_start = if sb_count > s.sb_cap_lines { sb_count - s.sb_cap_lines } else { 0 };
        let sb_take_count = sb_count - sb_take_start;
        for i in 0..sb_take_count {
            let src_r = sb_take_start + i;
            let src_base = (src_r as i64) * nc;
            let dst_base = (i as i64) * nc;
            while s.sb_cells.len < dst_base + nc { s.sb_cells.push(b); }
            for c in 0..nc {
                s.sb_cells[dst_base + c] = reflowed_rows[src_base + c];
            }
            s.sb_wrapped_rows.push(reflowed_wrapped[src_r]);
            s.sb_prompt_rows.push(reflowed_prompts[src_r]);
        }
        s.sb_count = sb_take_count;
    }

    s.cy = cursor_reflowed_row - live_start_row;
    s.cx = cursor_reflowed_col;
    clamp_cursor(s);
    if s.cx > 0 && s.cells[(s.cy as i64) * nc + s.cx].flags == CELL_WIDE_CONT {
        s.cx -= 1;
    }

    if s.view_offset > s.sb_count {
        s.view_offset = s.sb_count;
    }
    s.scroll_top = 0; s.scroll_bottom = nr - 1;
    s.wrap_pending = false;
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
        let pr = phys_row(s, abs_line);
        return s.cells[(pr as i64) * s.cols + col];
    }
    if abs_line < 0 || abs_line >= s.sb_count + s.rows { return blank_cell(s); }
    if abs_line < s.sb_count {
        if col >= s.sb_cols { return blank_cell(s); }
        let line_idx = (s.sb_head + abs_line) % s.sb_cap_lines;
        let idx = (line_idx as i64) * s.sb_cols + col;
        return if idx >= 0 && idx < s.sb_cells.len { s.sb_cells[idx] } else { blank_cell(s) };
    }
    let live_row = abs_line - s.sb_count;
    let pr = phys_row(s, live_row);
    return s.cells[(pr as i64) * s.cols + col];
}

fn history_is_prompt(s: Screen&, abs_line: i32) -> bool {
    if s.alt_screen || abs_line < 0 || abs_line >= s.sb_count + s.rows { return false; }
    if abs_line < s.sb_count {
        let line_idx = (s.sb_head + abs_line) % s.sb_cap_lines;
        return line_idx < s.sb_prompt_rows.len && s.sb_prompt_rows[line_idx] != 0;
    }
    let live_row = abs_line - s.sb_count;
    let pr = phys_row(s, live_row);
    return pr < s.prompt_rows.len && s.prompt_rows[pr] != 0;
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

// Select the output of the most recent command between previous prompt and current line
fn select_last_command_output(s: Screen&) -> bool {
    if s.alt_screen { return false; }
    let top_visible = s.sb_count - s.view_offset;
    var prompt_line: i32 = -1;
    var line: i32 = top_visible + s.cy;
    if history_is_prompt(s, line) {
        line -= 1;
    }
    while line >= 0 {
        if history_is_prompt(s, line) {
            prompt_line = line;
            break;
        }
        line -= 1;
    }
    if prompt_line < 0 { return false; }
    let start_line: i32 = prompt_line + 1;
    var end_line: i32 = top_visible + s.cy;
    if history_is_prompt(s, end_line) {
        end_line -= 1;
    }
    if start_line > end_line { return false; }

    s.sel_active = true;
    s.sel_mode = SEL_LINE;
    s.sel_x0 = 0;
    s.sel_y0 = start_line;
    s.sel_x1 = s.cols - 1;
    s.sel_y1 = end_line;
    if s.sel_y1 < s.sel_y0 {
        s.sel_y1 = s.sel_y0;
    }
    mark_all_dirty(s);
    return true;
}

fn visible_row_to_abs(s: Screen&, row: i32) -> i32 {
    return if s.alt_screen { row } else { s.sb_count - s.view_offset + row };
}

fn abs_to_visible_row(s: Screen&, abs_line: i32) -> i32 {
    return if s.alt_screen { abs_line } else { abs_line - (s.sb_count - s.view_offset) };
}

fn visible_cell(s: Screen&, col: i32, row: i32) -> Cell {
    if col < 0 || col >= s.cols || row < 0 || row >= s.rows { return blank_cell(s); }
    if s.view_offset == 0 || s.alt_screen {
        let pr = phys_row(s, row);
        return s.cells[(pr as i64) * s.cols + col];
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
    let abs_y = visible_row_to_abs(s, row);
    let cur = history_cell(s, abs_y, col);
    let sep = is_word_sep(cur.cp, whitespace_only);
    var c0 = col;
    while c0 > 0 && is_word_sep(history_cell(s, abs_y, c0 - 1).cp, whitespace_only) == sep {
        c0 -= 1;
    }
    var c1 = col;
    while c1 < s.cols - 1 && is_word_sep(history_cell(s, abs_y, c1 + 1).cp, whitespace_only) == sep {
        c1 += 1;
    }
    s.sel_active = true;
    s.sel_mode = SEL_WORD;
    s.sel_x0 = c0; s.sel_y0 = abs_y;
    s.sel_x1 = c1; s.sel_y1 = abs_y;
    mark_all_dirty(s);
}

fn select_line(s: Screen&, row: i32) {
    let abs_y = visible_row_to_abs(s, row);
    s.sel_active = true;
    s.sel_mode = SEL_LINE;
    s.sel_x0 = 0; s.sel_y0 = abs_y;
    s.sel_x1 = s.cols - 1; s.sel_y1 = abs_y;
    mark_all_dirty(s);
}

// Triple-click: select inside enclosing quotes (", ', `), or entire row if not inside quotes.
fn select_quote_or_line(s: Screen&, col: i32, row: i32) {
    let abs_y = visible_row_to_abs(s, row);
    var left_q: i32 = -1;
    var qchar: u32 = 0;
    var c = col;
    while c >= 0 {
        let cp = history_cell(s, abs_y, c).cp;
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
            if history_cell(s, abs_y, c2).cp == qchar {
                right_q = c2;
                break;
            }
            c2 += 1;
        }
        if right_q > left_q + 1 {
            s.sel_active = true;
            s.sel_mode = SEL_QUOTE;
            s.sel_x0 = left_q + 1; s.sel_y0 = abs_y;
            s.sel_x1 = right_q - 1; s.sel_y1 = abs_y;
            mark_all_dirty(s);
            return;
        }
    }
    select_line(s, row);
}

// Right-click / Ctrl+Right-click: extend current selection
fn extend_selection(s: Screen&, col: i32, row: i32, force_char: bool) {
    let abs_y = visible_row_to_abs(s, row);
    if !s.sel_active {
        s.sel_active = true;
        s.sel_mode = SEL_CHAR;
        s.sel_x0 = col; s.sel_y0 = abs_y;
        s.sel_x1 = col; s.sel_y1 = abs_y;
        mark_all_dirty(s);
        return;
    }
    if force_char {
        s.sel_mode = SEL_CHAR;
    }
    if s.sel_mode == SEL_LINE && !force_char {
        if abs_y < s.sel_y0 { s.sel_y0 = abs_y; s.sel_x0 = 0; }
        else { s.sel_y1 = abs_y; s.sel_x1 = s.cols - 1; }
    } else {
        s.sel_x1 = col;
        s.sel_y1 = abs_y;
    }
    mark_all_dirty(s);
}

fn is_selected(s: Screen&, col: i32, row: i32) -> bool {
    if !s.sel_active { return false; }
    let abs_row = visible_row_to_abs(s, row);
    if s.sel_mode == SEL_BLOCK {
        let min_x = if s.sel_x0 < s.sel_x1 { s.sel_x0 } else { s.sel_x1 };
        let max_x = if s.sel_x0 > s.sel_x1 { s.sel_x0 } else { s.sel_x1 };
        let min_y = if s.sel_y0 < s.sel_y1 { s.sel_y0 } else { s.sel_y1 };
        let max_y = if s.sel_y0 > s.sel_y1 { s.sel_y0 } else { s.sel_y1 };
        return col >= min_x && col <= max_x && abs_row >= min_y && abs_row <= max_y;
    }
    var y0 = s.sel_y0; var x0 = s.sel_x0;
    var y1 = s.sel_y1; var x1 = s.sel_x1;
    if y0 > y1 || (y0 == y1 && x0 > x1) {
        let ty = y0; y0 = y1; y1 = ty;
        let tx = x0; x0 = x1; x1 = tx;
    }
    if abs_row < y0 || abs_row > y1 { return false; }
    if abs_row == y0 && col < x0 { return false; }
    if abs_row == y1 && col > x1 { return false; }
    return true;
}

fn extract_selection(s: Screen&, out: u8[>..]&) {
    out.clear();
    if !s.sel_active { return; }

    var y0 = s.sel_y0; var x0 = s.sel_x0;
    var y1 = s.sel_y1; var x1 = s.sel_x1;
    if y0 > y1 || (y0 == y1 && x0 > x1) {
        let ty = y0; y0 = y1; y1 = ty;
        let tx = x0; x0 = x1; x1 = tx;
    }
    let total_lines = total_history_lines(s);
    if y0 < 0 { y0 = 0; }
    if y1 >= total_lines { y1 = total_lines - 1; }
    if y0 > y1 { return; }

    if s.sel_mode == SEL_BLOCK {
        let min_x = if x0 < x1 { x0 } else { x1 };
        let max_x = if x0 > x1 { x0 } else { x1 };
        let min_y = if y0 < y1 { y0 } else { y1 };
        let max_y = if y0 > y1 { y0 } else { y1 };
        for r in min_y..(max_y + 1) {
            var last_ns = min_x - 1;
            for c in min_x..(max_x + 1) {
                let cell = history_cell(s, r, c);
                if cell.flags == 0 && cell.cp != 0x20 && cell.cp != 0 { last_ns = c; }
            }
            if last_ns >= min_x {
                for c in min_x..(last_ns + 1) {
                    let cell = history_cell(s, r, c);
                    if cell.flags == CELL_WIDE_CONT { continue; }
                    utf8::encode_to(out, if cell.cp == 0 { 0x20 } else { cell.cp });
                }
            }
            if r < max_y { out.push('\n'); }
        }
        return;
    }

    for r in y0..(y1 + 1) {
        let c_start = if r == y0 { x0 } else { 0 };
        let c_end   = if r == y1 { x1 } else { s.cols - 1 };
        var last_nonspace = c_start - 1;
        for c in c_start..(c_end + 1) {
            let cell = history_cell(s, r, c);
            if cell.flags == 0 && cell.cp != 0x20 && cell.cp != 0 { last_nonspace = c; }
        }
        if last_nonspace >= c_start {
            for c in c_start..(last_nonspace + 1) {
                let cell = history_cell(s, r, c);
                if cell.flags == CELL_WIDE_CONT { continue; }
                utf8::encode_to(out, if cell.cp == 0 { 0x20 } else { cell.cp });
                if cell.combining != 0 { utf8::encode_to(out, cell.combining); }
            }
        }
        if r < y1 {
            let is_wrapped = if s.alt_screen {
                false
            } else if r < s.sb_count {
                let lidx = (s.sb_head + r) % s.sb_cap_lines;
                lidx < s.sb_wrapped_rows.len && s.sb_wrapped_rows[lidx] != 0
            } else {
                let lr = r - s.sb_count;
                let pr = phys_row(s, lr);
                pr < s.wrapped_rows.len && s.wrapped_rows[pr] != 0
            };
            if !is_wrapped {
                out.push('\n');
            }
        }
    }
}
