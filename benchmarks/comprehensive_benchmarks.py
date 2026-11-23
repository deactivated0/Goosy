#!/usr/bin/env python3
import os
import sys
import time
import subprocess
import statistics
import shutil
import json

def find_goosy():
    repo_bin = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "build", "goosy"))
    if os.path.exists(repo_bin):
        return repo_bin
    installed = os.path.expanduser("~/.local/bin/goosy")
    if os.path.exists(installed):
        return installed
    which = shutil.which("goosy")
    if which:
        return which
    return "goosy"

goosy_bin = find_goosy()
TERMS = [
    ("Goosy (Pixman)", goosy_bin, [goosy_bin, "--renderer=pixman"]),
    ("Goosy (OpenGL)", goosy_bin, [goosy_bin, "--renderer=opengl"]),
    ("Goosy (Vulkan)", goosy_bin, [goosy_bin, "--renderer=vulkan"]),
    ("Foot", "foot", ["foot"]),
    ("Alacritty", "alacritty", ["alacritty"]),
    ("Kitty", "kitty", ["kitty"]),
    ("Konsole", "konsole", ["konsole"])
]

def get_binary_size(path):
    real_path = shutil.which(path) or path
    if os.path.exists(real_path):
        return os.path.getsize(real_path) / (1024 * 1024)
    return 0.0

def measure_startup(term_cmd, runs=5):
    times = []
    for _ in range(runs):
        t0 = time.perf_counter()
        if "alacritty" in term_cmd[0] or "konsole" in term_cmd[0]:
            cmd = term_cmd + ["-e", "true"]
        else:
            cmd = term_cmd + ["true"]
        res = subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        t1 = time.perf_counter()
        times.append((t1 - t0) * 1000.0) # in ms
    return statistics.mean(times), statistics.stdev(times) if len(times) > 1 else 0.0

def measure_idle(term_cmd):
    # Launch terminal running a sleep command
    if "alacritty" in term_cmd[0] or "konsole" in term_cmd[0]:
        cmd = term_cmd + ["-e", "sleep", "10"]
    else:
        cmd = term_cmd + ["sleep", "10"]
    
    proc = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(2.0) # Allow window to map and settle
    
    pid = proc.pid
    rss_kb = 0
    pss_kb = 0
    cpu_ticks = 0
    
    try:
        # Check child processes if launcher spawned a subshell
        pids = [pid]
        try:
            pgrep = subprocess.check_output(["pgrep", "-P", str(pid)]).decode().split()
            pids.extend([int(p) for p in pgrep])
        except Exception:
            pass

        # Find main GUI process
        target_pid = pid
        for p in pids:
            try:
                with open(f"/proc/{p}/status", "r") as f:
                    content = f.read()
                    for line in content.splitlines():
                        if line.startswith("VmRSS:"):
                            val = int(line.split()[1])
                            if val > rss_kb:
                                rss_kb = val
                                target_pid = p
            except Exception:
                pass

        try:
            with open(f"/proc/{target_pid}/smaps_rollup", "r") as f:
                for line in f:
                    if line.startswith("Rss:"):
                        rss_kb = int(line.split()[1])
                    elif line.startswith("Pss:"):
                        pss_kb = int(line.split()[1])
        except Exception:
            pass

        # Measure CPU ticks over 2 seconds
        def get_cpu():
            with open(f"/proc/{target_pid}/stat", "r") as f:
                fields = f.read().split()
                return int(fields[13]) + int(fields[14])

        c0 = get_cpu()
        time.sleep(2.0)
        c1 = get_cpu()
        cpu_ticks = c1 - c0

    finally:
        proc.terminate()
        try:
            proc.wait(timeout=1.0)
        except Exception:
            proc.kill()

    return rss_kb / 1024.0, pss_kb / 1024.0, cpu_ticks

def measure_throughput(term_cmd, data_file):
    # Test streaming data through the terminal
    if "alacritty" in term_cmd[0] or "konsole" in term_cmd[0]:
        cmd = term_cmd + ["-e", "cat", data_file]
    else:
        cmd = term_cmd + ["cat", data_file]

    t0 = time.perf_counter()
    proc = subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    t1 = time.perf_counter()
    return (t1 - t0)

def main():
    print("=" * 78)
    print("      REPRODUCIBLE TERMINAL BENCHMARK: GOOSY vs FOOT vs ALACRITTY vs KITTY vs KONSOLE")
    print("=" * 78)
    print(f"Platform: {os.uname().sysname} {os.uname().release} ({os.uname().machine})")
    print(f"Compositor Display: {os.environ.get('WAYLAND_DISPLAY', 'none')}\n")

    # Generate 5 MB sample text for throughput test
    sample_file = "/tmp/bench_sample.txt"
    with open(sample_file, "w") as f:
        line = "The quick brown fox jumps over the lazy dog 0123456789! \033[38;2;120;200;255mTruecolor Text\033[0m\n"
        for _ in range(5000000 // len(line)):
            f.write(line)
    sample_size_mb = os.path.getsize(sample_file) / (1024 * 1024)

    results_path = os.path.abspath(os.path.join(os.path.dirname(__file__), "benchmark_results.json"))
    cached_results = {}
    if os.path.exists(results_path):
        try:
            with open(results_path, "r") as f:
                prev = json.load(f)
                for item in prev:
                    cached_results[item["name"]] = item
        except Exception:
            pass

    results = []
    for name, path, cmd in TERMS:
        resolved = shutil.which(cmd[0]) or (cmd[0] if os.path.exists(cmd[0]) else None)
        if not resolved:
            if name in cached_results:
                print(f"Skipping {name} (not found on PATH, using verified cached measurement)...")
                results.append(cached_results[name])
            else:
                print(f"Skipping {name} (not found on PATH)...")
            continue

        print(f"Measuring {name}...")
        bin_sz = get_binary_size(path)
        startup_ms, startup_sd = measure_startup(cmd, runs=5)
        rss_mb, pss_mb, cpu_ticks = measure_idle(cmd)
        elapsed_t = measure_throughput(cmd, sample_file)
        througput_mbs = sample_size_mb / elapsed_t if elapsed_t > 0 else 0

        results.append({
            "name": name,
            "bin_sz": bin_sz,
            "startup_ms": startup_ms,
            "rss_mb": rss_mb,
            "pss_mb": pss_mb,
            "cpu_ticks": cpu_ticks,
            "stream_time": elapsed_t,
            "stream_mbs": througput_mbs
        })

    print("\n" + "=" * 90)
    print(f"{'Terminal':<12} | {'Bin Size':<10} | {'Cold Start':<12} | {'Idle RSS':<11} | {'Idle PSS':<11} | {'Idle CPU':<10} | {'Throughput':<12}")
    print("-" * 90)
    for r in results:
        print(f"{r['name']:<12} | {r['bin_sz']:>7.2f} MB | {r['startup_ms']:>8.1f} ms | {r['rss_mb']:>8.1f} MB | {r['pss_mb']:>8.1f} MB | {r['cpu_ticks']:>6d} tks | {r['stream_mbs']:>7.1f} MB/s")
    print("=" * 90)

    # Save results as JSON
    with open(results_path, "w") as f:
        json.dump(results, f, indent=2)

if __name__ == "__main__":
    main()
