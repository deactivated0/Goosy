import src.colors;
import src.parser;
import src.screen;
import src.ffi;

fn make_sample_text(len: i32) -> u8[>..] {
    var out: u8[>..] = [];
    let line = "The quick brown fox jumps over the lazy dog 0123456789! Regular line output.\r\n";
    let reps = len / 80;
    for _ in 0..reps {
        out.append(line);
    }
    return out;
}

fn make_sample_sgr(len: i32) -> u8[>..] {
    var out: u8[>..] = [];
    let sgr_line = "\x1b[38;2;120;200;255;48;2;20;30;40;1mBoldColor\x1b[0m \x1b[4:3m\x1b[58;2;255;100;50mCurly\x1b[59m\x1b[24m text.\r\n";
    let reps = len / 80;
    for _ in 0..reps {
        out.append(sgr_line);
    }
    return out;
}

fn main() {
    var pal = colors::default_palette();
    var scr = screen::Screen { .. };
    screen::init(scr, 120, 40, pal.fg, pal.bg, 4000, 0);
    var par = parser::Parser { .. };

    let plain_data = make_sample_text(500000); // 500 KB plain text
    let sgr_data = make_sample_sgr(500000);     // 500 KB SGR text

    // Warmup
    parser::feed_slice(par, scr, pal, plain_data);

    // Benchmark 1: Plain ASCII streaming throughput (5 MB)
    let t0 = ffi::time_monotonic_ns();
    for _ in 0..10 {
        parser::feed_slice(par, scr, pal, plain_data);
    }
    let t1 = ffi::time_monotonic_ns();
    let plain_ms = (t1 - t0) / 1000000;

    // Benchmark 2: SGR & formatting escape sequence throughput (5 MB)
    let t2 = ffi::time_monotonic_ns();
    for _ in 0..10 {
        parser::feed_slice(par, scr, pal, sgr_data);
    }
    let t3 = ffi::time_monotonic_ns();
    let sgr_ms = (t3 - t2) / 1000000;

    // Benchmark 3: Screen scrolling (50,000 scroll operations)
    let t4 = ffi::time_monotonic_ns();
    for _ in 0..50000 {
        screen::scroll_up(scr, 0, scr.rows - 1, 1);
    }
    let t5 = ffi::time_monotonic_ns();
    let scroll_ms = (t5 - t4) / 1000000;

    print("BENCHMARK_RESULTS:");
    print("Plain throughput (5 MB): ", plain_ms, " ms (", (5000000000 / (plain_ms * 1000000)), " MB/s)");
    print("SGR throughput (5 MB):   ", sgr_ms, " ms (", (5000000000 / (sgr_ms * 1000000)), " MB/s)");
    print("Scroll 50k lines:        ", scroll_ms, " ms (", (50000 * 1000 / scroll_ms), " lines/s)");
}
