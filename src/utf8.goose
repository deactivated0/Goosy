// Goot — Streaming UTF-8 Decoder, Combining Mark & Wide-Character Cell Width
// Pure Goose implementation with strict rejection of overlong encodings,
// UTF-16 surrogates (U+D800..U+DFFF), and out-of-range (> U+10FFFF) sequences.

namespace utf8;

let REPLACEMENT_CHAR: u32 = 0xFFFD;

struct Decoder {
    codepoint: u32 = 0,
    expected: u8 = 0,
    seen: u8 = 0,
    min_cp: u32 = 0,
}

fn reset(d: Decoder&) {
    d.codepoint = 0;
    d.expected = 0;
    d.seen = 0;
    d.min_cp = 0;
}

// Feed one byte to the streaming UTF-8 decoder.
// Returns (codepoint, ready). When ready is true, codepoint is a valid Unicode
// scalar value or REPLACEMENT_CHAR (U+FFFD) on malformed input.
fn feed(d: Decoder&, b: u8) -> u32, bool {
    if d.expected == 0 {
        if b < 0x80 {
            return b, true;
        }
        if (b & 0xE0) == 0xC0 {
            let cp = (b & 0x1F) as u32;
            if cp < 2 {
                return REPLACEMENT_CHAR, true; // overlong 2-byte
            }
            d.codepoint = cp;
            d.expected = 1;
            d.seen = 0;
            d.min_cp = 0x80;
            return 0, false;
        }
        if (b & 0xF0) == 0xE0 {
            d.codepoint = (b & 0x0F);
            d.expected = 2;
            d.seen = 0;
            d.min_cp = 0x800;
            return 0, false;
        }
        if (b & 0xF8) == 0xF0 {
            let cp = (b & 0x07) as u32;
            if cp > 4 {
                return REPLACEMENT_CHAR, true; // > U+10FFFF
            }
            d.codepoint = cp;
            d.expected = 3;
            d.seen = 0;
            d.min_cp = 0x10000;
            return 0, false;
        }
        // Stray continuation byte or 0xF8..0xFF
        return REPLACEMENT_CHAR, true;
    }

    // Expecting continuation byte 10xxxxxx
    if (b & 0xC0) != 0x80 {
        reset(d);
        return REPLACEMENT_CHAR, true;
    }

    d.codepoint = (d.codepoint << 6) | (b & 0x3F);
    d.seen += 1;

    if d.seen == d.expected {
        let cp = d.codepoint;
        let min_cp = d.min_cp;
        reset(d);
        if cp < min_cp || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF) {
            return REPLACEMENT_CHAR, true;
        }
        return cp, true;
    }

    return 0, false;
}

// Returns true for Unicode zero-width combining marks and format characters.
fn is_combining(cp: u32) -> bool {
    if cp < 0x0300 { return false; }
    if cp <= 0x036F { return true; } // Combining Diacritical Marks
    if cp >= 0x1AB0 && cp <= 0x1AFF { return true; } // Combining Diacritical Marks Extended
    if cp >= 0x1DC0 && cp <= 0x1DFF { return true; } // Combining Diacritical Marks Supplement
    if cp >= 0x20D0 && cp <= 0x20FF { return true; } // Combining Diacritical Marks for Symbols
    if cp >= 0xFE00 && cp <= 0xFE0F { return true; } // Variation Selectors
    if cp >= 0xFE20 && cp <= 0xFE2F { return true; } // Combining Half Marks
    if cp == 0x200B || cp == 0x200C || cp == 0x200D || cp == 0xFEFF { return true; }
    return false;
}

// Returns terminal cell width (0, 1, or 2) for a Unicode codepoint.
fn cell_width(cp: u32) -> i32 {
    if cp >= 0x20 && cp < 0x7F { return 1; }
    if cp < 0x20 || (cp >= 0x7F && cp < 0xA0) { return 0; }
    if cp < 0x0300 { return 1; }
    if is_combining(cp) { return 0; }

    // East Asian Wide / Fullwidth & common Emoji blocks
    if (cp >= 0x1100 && cp <= 0x115F) ||
       cp == 0x2329 || cp == 0x232A ||
       (cp >= 0x2E80 && cp <= 0x303E) ||
       (cp >= 0x3040 && cp <= 0xA4CF) ||
       (cp >= 0xAC00 && cp <= 0xD7A3) ||
       (cp >= 0xF900 && cp <= 0xFAFF) ||
       (cp >= 0xFE10 && cp <= 0xFE19) ||
       (cp >= 0xFE30 && cp <= 0xFE6F) ||
       (cp >= 0xFF00 && cp <= 0xFF60) ||
       (cp >= 0xFFE0 && cp <= 0xFFE6) ||
       (cp >= 0x1F300 && cp <= 0x1FAFF) ||
       (cp >= 0x20000 && cp <= 0x3FFFD) {
        return 2;
    }
    return 1;
}

// Encode a single Unicode codepoint to UTF-8 bytes inside a builder.
fn encode_to(out: u8[>..]&, cp: u32) {
    if cp < 0x80 {
        out.push(cp as! u8);
    } else if cp < 0x800 {
        out.push((0xC0 | (cp >> 6)) as! u8);
        out.push((0x80 | (cp & 0x3F)) as! u8);
    } else if cp < 0x10000 {
        out.push((0xE0 | (cp >> 12)) as! u8);
        out.push((0x80 | ((cp >> 6) & 0x3F)) as! u8);
        out.push((0x80 | (cp & 0x3F)) as! u8);
    } else if cp <= 0x10FFFF {
        out.push((0xF0 | (cp >> 18)) as! u8);
        out.push((0x80 | ((cp >> 12) & 0x3F)) as! u8);
        out.push((0x80 | ((cp >> 6) & 0x3F)) as! u8);
        out.push((0x80 | (cp & 0x3F)) as! u8);
    } else {
        out.push(0xEF);
        out.push(0xBF);
        out.push(0xBD);
    }
}
