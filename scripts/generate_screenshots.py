#!/usr/bin/env python3
import os
import sys
import time
import subprocess

GOOSY_BIN = "/home/pc/.local/bin/goosy"
ASSETS_DIR = "/home/pc/builds/goot/assets/screenshots"
os.makedirs(ASSETS_DIR, exist_ok=True)

def capture_command(cmd_str, out_png, wait_sec=1.5):
    raw_path = f"/tmp/{os.path.basename(out_png)}.raw"
    dims_path = "/tmp/goosy_cap_dims.txt"
    if os.path.exists(raw_path): os.remove(raw_path)
    if os.path.exists(dims_path): os.remove(dims_path)

    env = os.environ.copy()
    env["GOOSY_CAPTURE_PATH"] = raw_path

    # Launch Goosy
    proc = subprocess.Popen([GOOSY_BIN, "sh", "-c", cmd_str], env=env)
    time.sleep(wait_sec)

    # Terminate Goosy
    proc.terminate()
    try:
        proc.wait(timeout=1.0)
    except Exception:
        proc.kill()

    if os.path.exists(raw_path) and os.path.exists(dims_path):
        with open(dims_path, "r") as f:
            w, h = f.read().strip().split()
        subprocess.run(["magick", "-size", f"{w}x{h}", "-depth", "8", f"bgra:{raw_path}", out_png], check=True)
        print(f"Captured {out_png} ({w}x{h})")
    else:
        print(f"Failed to capture {out_png}")

def main():
    # 1. Fastfetch
    capture_command("fastfetch; sleep 5", os.path.join(ASSETS_DIR, "goosy_fastfetch.png"), wait_sec=1.5)

    # 2. Colors & Text Formatting Demo
    color_demo = r"""
echo -e "\n  \033[1;36mGoosy Terminal Emulator\033[0m — \033[3mNative Wayland Client\033[0m\n"
echo -e "  \033[1mText Styles:\033[0m \033[1mBold\033[0m  \033[3mItalic\033[0m  \033[4mUnderline\033[0m  \033[4:3m\033[58;2;255;100;50mCurly\033[59m\033[24m  \033[4:2mDouble\033[24m  \033[9mStrikethrough\033[0m  \033[7mInverse\033[0m"
echo -e "\n  \033[1m24-bit Truecolor Gradients:\033[0m"
for i in $(seq 0 7 255); do
    printf "\033[48;2;%d;%d;%dm " "$i" "$((255-i))" "$(( (i*2)%256 ))"
done
printf "\033[0m\n"
for i in $(seq 0 7 255); do
    printf "\033[48;2;%d;%d;%dm " "$(( (i*3)%256 ))" "$i" "$((255-i))"
done
printf "\033[0m\n"
for i in $(seq 0 7 255); do
    printf "\033[48;2;%d;%d;%dm " "$((255-i))" "$(( (i*4)%256 ))" "$i"
done
printf "\033[0m\n\n"
echo -e "  \033[1mProcedural Box-Drawing & Block Elements:\033[0m"
echo -e "  \033[38;2;80;200;255m┌──────────────────────────────┬──────────────────────────────┐\033[0m"
echo -e "  \033[38;2;80;200;255m│\033[0m  \033[1;32m✓\033[0m Single-Pixel Precision     \033[38;2;80;200;255m│\033[0m  \033[1;33m⚡\033[0m Zero Allocation Event Loop \033[38;2;80;200;255m│\033[0m"
echo -e "  \033[38;2;80;200;255m│\033[0m  \033[1;35m●\033[0m Sub-40 MB Steady RSS       \033[38;2;80;200;255m│\033[0m  \033[1;34m★\033[0m Goose + Pixman C Engine    \033[38;2;80;200;255m│\033[0m"
echo -e "  \033[38;2;80;200;255m└──────────────────────────────┴──────────────────────────────┘\033[0m"
echo -e "  \033[38;2;255;180;50m ▂▃▄▅▆▇█  █▇▆▅▄▃▂   ▌▍▎▏   ▏▎▍▌  ░▒▓█\033[0m"
sleep 5
"""
    capture_command(color_demo, os.path.join(ASSETS_DIR, "goosy_colors.png"), wait_sec=1.5)

    # 3. Main fastfetch with CSD
    capture_command("fastfetch; sleep 5", os.path.join(ASSETS_DIR, "goosy_main.png"), wait_sec=1.5)

    # 4. Code view (showing Goose code)
    code_demo = r"""
echo -e "\033[1;34m// Goosy — High-Performance Native Wayland Terminal Engine\033[0m"
echo -e "\033[32mimport\033[0m src.screen;"
echo -e "\033[32mimport\033[0m src.renderer;"
echo -e "\033[32mimport\033[0m src.ffi;\n"
echo -e "\033[35mfn\033[0m \033[1;36mdraw_frame\033[0m(s: Screen&, cfg: Config&, focused: bool) {"
echo -e "    \033[32mlet\033[0m pad_px = cfg.padding * scale;"
echo -e "    \033[32mlet\033[0m cell_w = ffi::font_cell_width();"
echo -e "    \033[32mlet\033[0m cell_h = ffi::font_cell_height();"
echo -e "    \033[34m// Zero-drift cell composite with Pixman software bridge\033[0m"
echo -e "    \033[35mfor\033[0m row \033[35min\033[0m 0..s.rows {"
echo -e "        \033[32mlet\033[0m py = title_h + pad_px + row * cell_h;"
echo -e "        \033[35mfor\033[0m col \033[35min\033[0m 0..s.cols {"
echo -e "            ffi::render_cell_ext(px, py, cell_w, cell_h, cell.cp, fg, bg, attrs, span, ul_style, ul_color);"
echo -e "        }"
echo -e "    }"
echo -e "}"
sleep 5
"""
    capture_command(code_demo, os.path.join(ASSETS_DIR, "goosy_code.png"), wait_sec=1.5)

if __name__ == "__main__":
    main()
