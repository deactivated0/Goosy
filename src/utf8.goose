// Goosy — Streaming UTF-8 Decoder, Combining Mark & Wide-Character Cell Width
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

    // East Asian Wide / Fullwidth & common Emoji/Symbol blocks (matching go-runewidth / Bubble Tea)
    if (cp >= 0x1100 && cp <= 0x115F) ||
       cp == 0x231A || cp == 0x231B ||
       (cp >= 0x23E9 && cp <= 0x23EC) ||
       cp == 0x23F0 || cp == 0x23F3 ||
       cp == 0x2329 || cp == 0x232A ||
       (cp >= 0x25FD && cp <= 0x25FE) ||
       (cp >= 0x2614 && cp <= 0x2615) ||
       (cp >= 0x2648 && cp <= 0x2653) ||
       cp == 0x267F || cp == 0x2693 || cp == 0x26A1 ||
       (cp >= 0x26AA && cp <= 0x26AB) ||
       (cp >= 0x26BD && cp <= 0x26BE) ||
       (cp >= 0x26C4 && cp <= 0x26C5) ||
       cp == 0x26CE || cp == 0x26D4 || cp == 0x26EA ||
       (cp >= 0x26F2 && cp <= 0x26F3) ||
       cp == 0x26F5 || cp == 0x26FA || cp == 0x26FD ||
       cp == 0x2702 || cp == 0x2705 ||
       (cp >= 0x2708 && cp <= 0x270D) ||
       cp == 0x270F || cp == 0x2712 || cp == 0x2714 || cp == 0x2716 ||
       cp == 0x271D || cp == 0x2721 || cp == 0x2728 ||
       (cp >= 0x2733 && cp <= 0x2734) ||
       cp == 0x2744 || cp == 0x2747 || cp == 0x274C || cp == 0x274E ||
       (cp >= 0x2753 && cp <= 0x2755) ||
       cp == 0x2757 || (cp >= 0x2763 && cp <= 0x2764) ||
       (cp >= 0x2795 && cp <= 0x2797) ||
       cp == 0x27A1 || cp == 0x27B0 || cp == 0x27BF ||
       (cp >= 0x2934 && cp <= 0x2935) ||
       (cp >= 0x2B05 && cp <= 0x2B07) ||
       (cp >= 0x2B1B && cp <= 0x2B1C) ||
       cp == 0x2B50 || cp == 0x2B55 ||
       (cp >= 0x2E80 && cp <= 0x303E) ||
       (cp >= 0x3040 && cp <= 0xA4CF) ||
       (cp >= 0xAC00 && cp <= 0xD7A3) ||
       (cp >= 0xF900 && cp <= 0xFAFF) ||
       (cp >= 0xFE10 && cp <= 0xFE19) ||
       (cp >= 0xFE30 && cp <= 0xFE6F) ||
       (cp >= 0xFF00 && cp <= 0xFF60) ||
       (cp >= 0xFFE0 && cp <= 0xFFE6) ||
       (cp >= 0x1F000 && cp <= 0x1FAFF) ||
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
