// Goosy — Damage-Tracked Software Renderer Driver in Goose
// Renders terminal cells, cursor with glyph inversion, selection highlights,
// URL jump labels, search/unicode overlay bars, and native Wayland CSD title bar.

namespace renderer;

import src.colors;
import src.config;
import src.ffi;
import src.modes;
import src.screen;

let BTN_NONE: i32  = 0;
let BTN_MIN: i32   = 1;
let BTN_MAX: i32   = 2;
let BTN_CLOSE: i32 = 3;

fn draw_overlay_bar(top_y: i32, pad_px: i32, row: i32, cols: i32, cell_w: i32, cell_h: i32,
                    prefix: const u8[:], body: const u8[:], fg: u32, bg: u32) {
    let py = top_y + pad_px + row * cell_h;
    var col: i32 = 0;
    for i in 0..prefix.len {
        if col >= cols { break; }
        ffi::render_cell(pad_px + col * cell_w, py, cell_w, cell_h, prefix[i], fg, bg, screen::ATTR_BOLD, 1);
        col += 1;
    }
    for i in 0..body.len {
        if col >= cols { break; }
        ffi::render_cell(pad_px + col * cell_w, py, cell_w, cell_h, body[i], fg, bg, 0, 1);
        col += 1;
    }
    while col < cols {
        ffi::render_cell(pad_px + col * cell_w, py, cell_w, cell_h, 0x20, fg, bg, 0, 1);
        col += 1;
    }
}

fn draw_url_labels(m: modes::ModeState&, top_y: i32, pad_px: i32, cols: i32, cell_w: i32, cell_h: i32) {
    let lbl_fg: u32 = 0x080808;
    let lbl_bg: u32 = 0xFFD787;
    for i in 0..m.url_count {
        let u = m.urls[i];
        let py = top_y + pad_px + u.row * cell_h;
        var col = u.col_start;
        for k in 0..u.label.len {
            if col >= cols { break; }
            ffi::render_cell(pad_px + col * cell_w, py, cell_w, cell_h,
                             u.label[k], lbl_fg, lbl_bg, screen::ATTR_BOLD, 1);
            col += 1;
        }
        if m.url_show_text && col < cols {
            ffi::render_cell(pad_px + col * cell_w, py, cell_w, cell_h, ':', lbl_fg, lbl_bg, 0, 1);
            col += 1;
            for k in 0..u.url.len {
                if col >= cols { break; }
                ffi::render_cell(pad_px + col * cell_w, py, cell_w, cell_h, u.url[k], lbl_fg, lbl_bg, 0, 1);
                col += 1;
            }
        }
    }
}

fn draw_title_bar(s: screen::Screen&, cfg: config::Config&, focused: bool,
                  hover_btn: i32, pressed_btn: i32, scale: i32,
                  win_w: i32, title_h: i32, cell_w: i32, cell_h: i32) {
    let bar_bg: u32 = 0x000000;
    let border_col: u32 = 0x1A1A1A;

    // 1. Title bar background and bottom subtle border
    ffi::render_fill_rect(0, 0, win_w, title_h, bar_bg);
    ffi::render_fill_rect(0, title_h - 1, win_w, 1, border_col);

    let btn_w = 38 * scale;
    let btn_max_x = win_w - 3 * btn_w;

    // 2. Window title icon & text
    let icon_sz = 16 * scale;
    let icon_x = 10 * scale;
    let icon_y = if title_h > icon_sz { (title_h - icon_sz) / 2 } else { 0 };

    if ffi::render_title_icon(icon_x, icon_y, icon_sz) == 0 {
        // Fallback: draw terminal prompt icon '>_'
        let icon_col: u32 = if focused { 0x4EC9B0 } else { 0x555555 };
        ffi::render_fill_rect(icon_x, icon_y + 1, 2, 2, icon_col);
        ffi::render_fill_rect(icon_x + 2, icon_y + 3, 2, 2, icon_col);
        ffi::render_fill_rect(icon_x + 4, icon_y + 5, 2, 2, icon_col);
        ffi::render_fill_rect(icon_x + 2, icon_y + 7, 2, 2, icon_col);
        ffi::render_fill_rect(icon_x, icon_y + 9, 2, 2, icon_col);
        ffi::render_fill_rect(icon_x + 7, icon_y + 9, 5 * scale, 2, icon_col);
    }

    let title_bytes: const u8[:] = if s.title.len > 0 { s.title } else { cfg.title };
    let title_fg: u32 = if focused { 0xEAEAEA } else { 0x666666 };
    let ty = if title_h > cell_h { (title_h - cell_h) / 2 } else { 0 };

    // Center title text in the middle of the title bar
    let title_len = title_bytes.len as! i32;
    let total_title_w = title_len * cell_w;
    var tx = (win_w - total_title_w) / 2;
    let min_tx = icon_x + icon_sz + 8 * scale;
    let max_tx = btn_max_x - 8 * scale;
    if tx < min_tx { tx = min_tx; }

    for i in 0..title_bytes.len {
        if tx + cell_w >= max_tx { break; }
        ffi::render_cell(tx, ty, cell_w, cell_h, title_bytes[i], title_fg, bar_bg, 0, 1);
        tx += cell_w;
    }

    // 3. Minimize button
    let min_x = win_w - 3 * btn_w;
    if hover_btn == BTN_MIN {
        let min_bg: u32 = if pressed_btn == BTN_MIN { 0x282828 } else { 0x1A1A1A };
        ffi::render_fill_rect(min_x, 0, btn_w, title_h - 1, min_bg);
    }
    let icon_w = 10 * scale;
    let icon_fg: u32 = if focused { 0xEAEAEA } else { 0x777777 };
    let min_ix = min_x + (btn_w - icon_w) / 2;
    let min_iy = title_h / 2 + 2 * scale;
    ffi::render_fill_rect(min_ix, min_iy, icon_w, if scale > 1 { 2 } else { 1 }, icon_fg);

    // 4. Maximize / Restore button
    let max_x = win_w - 2 * btn_w;
    if hover_btn == BTN_MAX {
        let max_bg: u32 = if pressed_btn == BTN_MAX { 0x282828 } else { 0x1A1A1A };
        ffi::render_fill_rect(max_x, 0, btn_w, title_h - 1, max_bg);
    }
    let max_sz = 10 * scale;
    let max_ix = max_x + (btn_w - max_sz) / 2;
    let max_iy = (title_h - max_sz) / 2;
    let is_max = (ffi::wl_is_maximized() != 0);

    if is_max {
        // Restore: two overlapping squares
        let off = 2 * scale;
        let sub_sz = max_sz - off;
        // Background small square outline
        ffi::render_fill_rect(max_ix + off, max_iy, sub_sz, 1, icon_fg);
        ffi::render_fill_rect(max_ix + off, max_iy, 1, sub_sz, icon_fg);
        ffi::render_fill_rect(max_ix + max_sz - 1, max_iy, 1, sub_sz, icon_fg);
        ffi::render_fill_rect(max_ix + off, max_iy + sub_sz - 1, sub_sz, 1, icon_fg);
        // Foreground square outline
        ffi::render_fill_rect(max_ix, max_iy + off, sub_sz, 1, icon_fg);
        ffi::render_fill_rect(max_ix, max_iy + off, 1, sub_sz, icon_fg);
        ffi::render_fill_rect(max_ix + sub_sz - 1, max_iy + off, 1, sub_sz, icon_fg);
        ffi::render_fill_rect(max_ix, max_iy + max_sz - 1, sub_sz, 1, icon_fg);
    } else {
        // Maximize: single outline square
        ffi::render_fill_rect(max_ix, max_iy, max_sz, 1, icon_fg);
        ffi::render_fill_rect(max_ix, max_iy + max_sz - 1, max_sz, 1, icon_fg);
        ffi::render_fill_rect(max_ix, max_iy, 1, max_sz, icon_fg);
        ffi::render_fill_rect(max_ix + max_sz - 1, max_iy, 1, max_sz, icon_fg);
    }

    // 5. Close button
    let cls_x = win_w - btn_w;
    if hover_btn == BTN_CLOSE {
        let cls_bg: u32 = if pressed_btn == BTN_CLOSE { 0xBF0F1D } else { 0xE81123 };
        ffi::render_fill_rect(cls_x, 0, btn_w, title_h - 1, cls_bg);
    }
    let cls_fg: u32 = if hover_btn == BTN_CLOSE { 0xFFFFFF } else if focused { 0xEAEAEA } else { 0x888888 };
    let cls_sz = 10 * scale;
    let cls_ix = cls_x + (btn_w - cls_sz) / 2;
    let cls_iy = (title_h - cls_sz) / 2;

    for d in 0..cls_sz {
        ffi::render_fill_rect(cls_ix + d, cls_iy + d, 1, 1, cls_fg);
        ffi::render_fill_rect(cls_ix + cls_sz - 1 - d, cls_iy + d, 1, 1, cls_fg);
    }
}

fn draw_frame(s: screen::Screen&, cfg: config::Config&, m: modes::ModeState&,
              focused: bool, hover_btn: i32, pressed_btn: i32) {
    let scale  = ffi::wl_scale();
    let is_csd = cfg.title_bar && (ffi::wl_is_csd() != 0);
    let title_h = if is_csd { cfg.title_bar_height * scale } else { 0 };

    let is_gpu = ffi::renderer_get_active_type() != ffi::RENDERER_PIXMAN;
    let is_full = is_gpu || (s.full_redraw_frames > 0);
    var any_dirty = (s.full_redraw_frames > 0) || s.title_dirty || (m.kind != modes::MODE_NORMAL);
    if !any_dirty {
        for r in 0..s.rows {
            if r < s.dirty_rows.len && s.dirty_rows[r] > 0 {
                any_dirty = true;
                break;
            }
        }
    }
    guard any_dirty else { return; }
    guard ffi::wl_frame_ready() else { return; }
    guard ffi::render_begin(cfg.palette.bg) == 0 else { return; }

    let pad_px = cfg.padding * scale;
    let cell_w = ffi::font_cell_width();
    let cell_h = ffi::font_cell_height();
    let win_w  = ffi::wl_width() * scale;
    let win_h  = ffi::wl_height() * scale;

    if is_full {
        ffi::render_fill_rect(0, title_h, win_w, win_h - title_h, cfg.palette.bg);
    }

    if is_csd && (is_full || s.title_dirty) {
        draw_title_bar(s, cfg, focused, hover_btn, pressed_btn, scale, win_w, title_h, cell_w, cell_h);
        s.title_dirty = false;
    }

    let is_live = (s.view_offset == 0 || s.alt_screen);
    let sel_active = s.sel_active;

    // PASS 1: Fill cell backgrounds for all dirty rows in batched horizontal runs
    for row in 0..s.rows {
        let row_dirty = is_full || (row < s.dirty_rows.len && s.dirty_rows[row] > 0) ||
                        (m.kind != modes::MODE_NORMAL && row == s.rows - 1);
        if !row_dirty { continue; }

        let py = title_h + pad_px + row * cell_h;
        let phys_r = if is_live { screen::phys_row(s, row) } else { 0 };
        let row_base = (phys_r as i64) * s.cols;

        var col: i32 = 0;
        while col < s.cols {
            let cell = if is_live { s.cells[row_base + col] } else { screen::visible_cell(s, col, row) };
            var bg = cell.bg;
            if (cell.attrs & screen::ATTR_INVERSE) != 0 { bg = cell.fg; }
            if (cell.attrs & screen::ATTR_HIDDEN) != 0  { bg = cell.bg; }
            if sel_active && screen::is_selected(s, col, row) { bg = cfg.palette.selection_bg; }

            let start_col = col;
            col += 1;
            while col < s.cols {
                let next_cell = if is_live { s.cells[row_base + col] } else { screen::visible_cell(s, col, row) };
                var next_bg = next_cell.bg;
                if (next_cell.attrs & screen::ATTR_INVERSE) != 0 { next_bg = next_cell.fg; }
                if (next_cell.attrs & screen::ATTR_HIDDEN) != 0  { next_bg = next_cell.bg; }
                if sel_active && screen::is_selected(s, col, row) { next_bg = cfg.palette.selection_bg; }
                if next_bg != bg { break; }
                col += 1;
            }

            let bx = pad_px + start_col * cell_w;
            let bw = (col - start_col) * cell_w;
            ffi::render_fill_rect(bx, py, bw, cell_h, bg);
        }
    }

    // PASS 2: Render glyphs, procedural shapes, and underlines on top of clean backgrounds
    for row in 0..s.rows {
        let row_dirty = is_full || (row < s.dirty_rows.len && s.dirty_rows[row] > 0) ||
                        (m.kind != modes::MODE_NORMAL && row == s.rows - 1);
        if !row_dirty { continue; }

        let py = title_h + pad_px + row * cell_h;
        let phys_r = if is_live { screen::phys_row(s, row) } else { 0 };
        let row_base = (phys_r as i64) * s.cols;
        for col in 0..s.cols {
            let cell = if is_live { s.cells[row_base + col] } else { screen::visible_cell(s, col, row) };
            if cell.flags == screen::CELL_WIDE_CONT { continue; }

            var fg = cell.fg;
            var bg = cell.bg;
            if (cell.attrs & screen::ATTR_INVERSE) != 0 { let tmp = fg; fg = bg; bg = tmp; }
            if (cell.attrs & screen::ATTR_DIM) != 0     { fg = colors::dim_rgb(fg); }
            if (cell.attrs & screen::ATTR_HIDDEN) != 0  { fg = bg; }
            if sel_active && screen::is_selected(s, col, row) {
                fg = cfg.palette.selection_fg;
                bg = cfg.palette.selection_bg;
            }

            let px = pad_px + col * cell_w;
            let span: i32 = if cell.width == 2 { 2 } else { 1 };
            ffi::render_cell_ext(px, py, cell_w, cell_h, cell.cp, cell.combining, fg, 0xFFFFFFFF, cell.attrs, span, cell.ul_style, cell.ul_color);
        }
    }

    // Render active inline images (Sixel and Kitty Graphics)
    let view_top_abs = if s.alt_screen { 0 } else { s.sb_count - s.view_offset };
    ffi::render_images(pad_px, title_h, view_top_abs, s.rows, s.cols, cell_w, cell_h);

    if m.kind == modes::MODE_URL {
        draw_url_labels(m, title_h, pad_px, s.cols, cell_w, cell_h);
    } else if m.kind == modes::MODE_SEARCH && s.rows > 0 {
        let bar_bg: u32 = if m.search_found || m.search_query.len == 0 { 0x1E2A38 } else { 0x4A1E1E };
        draw_overlay_bar(title_h, pad_px, s.rows - 1, s.cols, cell_w, cell_h, "search: ", m.search_query, 0xEAEAEA, bar_bg);
    } else if m.kind == modes::MODE_UNICODE && s.rows > 0 {
        draw_overlay_bar(title_h, pad_px, s.rows - 1, s.cols, cell_w, cell_h, "U+", m.uni_hex, 0xEAEAEA, 0x1E2A38);
    }

    if s.cursor_visible && s.view_offset == 0 && m.kind == modes::MODE_NORMAL {
        let cx_px = pad_px + s.cx * cell_w;
        let cy_px = title_h + pad_px + s.cy * cell_h;
        let style = if focused { s.cursor_style } else { config::CURSOR_UNDERLINE };
        if style == config::CURSOR_BLOCK {
            let cell = screen::visible_cell(s, s.cx, s.cy);
            let cp = if cell.cp > 0x20 { cell.cp } else { 0 };
            ffi::render_cell(cx_px, cy_px, cell_w, cell_h, cp, cfg.palette.bg, cfg.palette.cursor, 0, 1);
        } else {
            ffi::render_cursor(cx_px, cy_px, cell_w, cell_h, cfg.palette.cursor, style);
        }
    }

    ffi::render_end(true);
    screen::clear_dirty(s);
}
