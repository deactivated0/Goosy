// Goot — Damage-Tracked Software Renderer Driver in Goose
// Renders terminal cells, cursor, selection highlights, URL jump labels,
// and minimal bottom-row overlays for Scrollback Search and Unicode Input modes.

namespace renderer;

import src.colors;
import src.config;
import src.ffi;
import src.modes;
import src.screen;

fn draw_overlay_bar(pad_px: i32, row: i32, cols: i32, cell_w: i32, cell_h: i32,
                    prefix: const u8[:], body: const u8[:], fg: u32, bg: u32) {
    let py = pad_px + row * cell_h;
    var col: i32 = 0;
    for i in 0..prefix.len {
        if col >= cols { break; }
        ffi::render_cell(pad_px + col * cell_w, py, cell_w, cell_h, prefix[i] as u32, fg, bg, screen::ATTR_BOLD as u32, 1);
        col += 1;
    }
    for i in 0..body.len {
        if col >= cols { break; }
        ffi::render_cell(pad_px + col * cell_w, py, cell_w, cell_h, body[i] as u32, fg, bg, 0, 1);
        col += 1;
    }
    while col < cols {
        ffi::render_cell(pad_px + col * cell_w, py, cell_w, cell_h, 0x20, fg, bg, 0, 1);
        col += 1;
    }
}

fn draw_url_labels(m: modes::ModeState&, pad_px: i32, cols: i32, cell_w: i32, cell_h: i32) {
    let lbl_fg: u32 = 0x080808;
    let lbl_bg: u32 = 0xFFD787;
    for i in 0..m.url_count {
        let u = m.urls[i];
        let py = pad_px + u.row * cell_h;
        var col = u.col_start;
        for k in 0..u.label.len {
            if col >= cols { break; }
            ffi::render_cell(pad_px + col * cell_w, py, cell_w, cell_h,
                             u.label[k] as u32, lbl_fg, lbl_bg, screen::ATTR_BOLD as u32, 1);
            col += 1;
        }
        if m.url_show_text && col < cols {
            ffi::render_cell(pad_px + col * cell_w, py, cell_w, cell_h, ':' as u32, lbl_fg, lbl_bg, 0, 1);
            col += 1;
            for k in 0..u.url.len {
                if col >= cols { break; }
                ffi::render_cell(pad_px + col * cell_w, py, cell_w, cell_h, u.url[k] as u32, lbl_fg, lbl_bg, 0, 1);
                col += 1;
            }
        }
    }
}

fn draw_frame(s: screen::Screen&, cfg: config::Config&, m: modes::ModeState&, focused: bool) {
    var any_dirty = s.full_redraw || (m.kind != modes::MODE_NORMAL);
    if !any_dirty {
        for r in 0..s.rows {
            if (r as i64) < s.dirty_rows.len && s.dirty_rows[r] != 0 {
                any_dirty = true;
                break;
            }
        }
    }
    guard any_dirty else { return; }
    guard ffi::wl_frame_ready() else { return; }
    guard ffi::render_begin(cfg.palette.bg) == 0 else { return; }

    let scale  = ffi::wl_scale();
    let pad_px = cfg.padding * scale;
    let cell_w = ffi::font_cell_width();
    let cell_h = ffi::font_cell_height();
    let win_w  = ffi::wl_width() * scale;
    let win_h  = ffi::wl_height() * scale;

    if s.full_redraw {
        ffi::render_fill_rect(0, 0, win_w, win_h, cfg.palette.bg);
    }

    for row in 0..s.rows {
        let row_dirty = s.full_redraw || ((row as i64) < s.dirty_rows.len && s.dirty_rows[row] != 0) ||
                        (m.kind != modes::MODE_NORMAL && row == s.rows - 1);
        if !row_dirty { continue; }

        let py = pad_px + row * cell_h;
        for col in 0..s.cols {
            let cell = screen::visible_cell(s, col, row);
            if cell.flags == screen::CELL_WIDE_CONT { continue; }

            var fg = cell.fg;
            var bg = cell.bg;
            if (cell.attrs & screen::ATTR_INVERSE) != 0 { let tmp = fg; fg = bg; bg = tmp; }
            if (cell.attrs & screen::ATTR_DIM) != 0     { fg = colors::dim_rgb(fg); }
            if (cell.attrs & screen::ATTR_HIDDEN) != 0  { fg = bg; }
            if screen::is_selected(s, col, row) {
                fg = cfg.palette.selection_fg;
                bg = cfg.palette.selection_bg;
            }

            let px = pad_px + col * cell_w;
            let span: i32 = if cell.width == 2 { 2 } else { 1 };
            ffi::render_cell(px, py, cell_w, cell_h, cell.cp, fg, bg, cell.attrs as u32, span);
        }
    }

    if m.kind == modes::MODE_URL {
        draw_url_labels(m, pad_px, s.cols, cell_w, cell_h);
    } else if m.kind == modes::MODE_SEARCH && s.rows > 0 {
        let bar_bg: u32 = if m.search_found || m.search_query.len == 0 { 0x1E2A38 } else { 0x4A1E1E };
        draw_overlay_bar(pad_px, s.rows - 1, s.cols, cell_w, cell_h, "search: ", m.search_query, 0xEAEAEA, bar_bg);
    } else if m.kind == modes::MODE_UNICODE && s.rows > 0 {
        draw_overlay_bar(pad_px, s.rows - 1, s.cols, cell_w, cell_h, "U+", m.uni_hex, 0xEAEAEA, 0x1E2A38);
    }

    if s.cursor_visible && s.view_offset == 0 && m.kind == modes::MODE_NORMAL {
        let cx_px = pad_px + s.cx * cell_w;
        let cy_px = pad_px + s.cy * cell_h;
        let style = if focused { s.cursor_style } else { config::CURSOR_UNDERLINE };
        ffi::render_cursor(cx_px, cy_px, cell_w, cell_h, cfg.palette.cursor, style);
    }

    ffi::render_end(true);
    screen::clear_dirty(s);
}
