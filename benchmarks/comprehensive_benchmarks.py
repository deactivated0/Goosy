#!/usr/bin/env python3
"""
GOOSY SCIENTIFIC BENCHMARK SUITE
Rigorous, reproducible, synchronized terminal benchmarking.
Measures cold/warm startup, idle memory (RSS/PSS), idle CPU ticks,
engine microbenchmarks, and sustained end-to-end streaming throughput
with explicit primary device attribute (\033[c) drain verification.
"""

import os
import sys
import time
import subprocess
import statistics
import shutil
import json
import platform

REPO_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
GOOSY_BIN = os.path.join(REPO_DIR, "build", "goosy")
WORKER_SCRIPT = os.path.join(REPO_DIR, "benchmarks", "bench_drain_worker.py")
PAYLOAD_PATH = "/tmp/goosy_bench_10mb.txt"

TERMS = [
    ("Goosy (Pixman)", [GOOSY_BIN, "--renderer=pixman", "-e"]),
    ("Goosy (OpenGL)", [GOOSY_BIN, "--renderer=opengl", "-e"]),
    ("Goosy (Vulkan)", [GOOSY_BIN, "--renderer=vulkan", "-e"]),
    ("Foot", ["foot", "-e"]),
    ("Alacritty", ["alacritty", "-e"]),
    ("Kitty", ["kitty", "--"]),
    ("Konsole", ["konsole", "--nofork", "-e"])
]

def get_env_metadata():
    cpu_model = "Unknown CPU"
    try:
        with open("/proc/cpuinfo") as f:
            for line in f:
                if "model name" in line:
                    cpu_model = line.split(":", 1)[1].strip()
                    break
    except Exception:
        pass

    return {
        "os": f"{platform.system()} {platform.release()} ({platform.machine()})",
        "cpu": cpu_model,
        "cores": os.cpu_count(),
        "wayland_display": os.environ.get("WAYLAND_DISPLAY", "unknown")
    }

def ensure_payload():
    if not os.path.exists(PAYLOAD_PATH) or os.path.getsize(PAYLOAD_PATH) < 10 * 1024 * 1024:
        print("[bench] Generating 10 MB truecolor/plain test payload...")
        with open(PAYLOAD_PATH, "wb") as f:
            p_line = b"The quick brown fox jumps over the lazy dog 0123456789! Standard terminal benchmark text.\r\n"
            s_line = b"\033[38;2;120;200;255;48;2;20;30;40;1mBoldColor\033[0m \033[4:3m\033[58;2;255;100;50mCurly\033[59m\033[24m text line.\r\n"
            total = 0
            target = 10 * 1024 * 1024
            while total < target:
                f.write(p_line)
                f.write(s_line)
                total += len(p_line) + len(s_line)
    return os.path.getsize(PAYLOAD_PATH)

def get_binary_size(path):
    resolved = shutil.which(path) or (path if os.path.exists(path) else None)
    if resolved and os.path.exists(resolved):
        return os.path.getsize(resolved) / (1024.0 * 1024.0)
    return 0.0

def measure_startup(term_prefix, runs=5):
    times = []
    for _ in range(runs):
        t0 = time.perf_counter()
        cmd = term_prefix + ["true"]
        try:
            subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=10)
            t1 = time.perf_counter()
            times.append((t1 - t0) * 1000.0)
        except Exception:
            pass

    if not times:
        return {"cold_ms": 0.0, "median_ms": 0.0, "p95_ms": 0.0, "stdev_ms": 0.0, "samples": []}
    
    cold_ms = times[0]
    warm_samples = times[1:] if len(times) > 1 else times
    warm_sorted = sorted(warm_samples)
    p95_idx = min(int(0.95 * len(warm_sorted)), len(warm_sorted) - 1)
    return {
        "cold_ms": cold_ms,
        "median_ms": statistics.median(warm_sorted),
        "p95_ms": warm_sorted[p95_idx],
        "stdev_ms": statistics.stdev(warm_sorted) if len(warm_sorted) > 1 else 0.0,
        "samples": times
    }

def measure_idle(term_prefix):
    cmd = term_prefix + ["sleep", "10"]
    proc = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(2.0)
    
    pid = proc.pid
    rss_kb = 0
    pss_kb = 0
    cpu_ticks = 0

    try:
        pids = [pid]
        try:
            pgrep = subprocess.check_output(["pgrep", "-P", str(pid)]).decode().split()
            pids.extend([int(p) for p in pgrep])
        except Exception:
            pass

        target_pid = pid
        for p in pids:
            try:
                with open(f"/proc/{p}/status") as f:
                    for line in f:
                        if line.startswith("VmRSS:"):
                            val = int(line.split()[1])
                            if val > rss_kb:
                                rss_kb = val
                                target_pid = p
            except Exception:
                pass

        try:
            with open(f"/proc/{target_pid}/smaps_rollup") as f:
                for line in f:
                    if line.startswith("Rss:"):
                        rss_kb = int(line.split()[1])
                    elif line.startswith("Pss:"):
                        pss_kb = int(line.split()[1])
        except Exception:
            pass

        def get_cpu():
            with open(f"/proc/{target_pid}/stat") as f:
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

    return {
        "rss_mb": rss_kb / 1024.0,
        "pss_mb": pss_kb / 1024.0,
        "cpu_ticks": cpu_ticks
    }

def measure_streaming(exec_prefix, runs=3):
    samples = []
    drain_times = []
    write_times = []
    total_times = []
    verified = True

    for i in range(runs):
        res_file = f"/tmp/bench_res_{int(time.time()*1000)}_{i}.json"
        if os.path.exists(res_file):
            os.remove(res_file)
        cmd = exec_prefix + ["python3", WORKER_SCRIPT, PAYLOAD_PATH, res_file]
        try:
            subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=20)
        except subprocess.TimeoutExpired:
            verified = False
            continue

        if os.path.exists(res_file):
            try:
                with open(res_file) as f:
                    data = json.load(f)
                samples.append(data["mbs"])
                drain_times.append(data["drain_seconds"] * 1000.0)
                write_times.append(data["write_seconds"])
                total_times.append(data["total_seconds"])
                if not data.get("drain_verified", False):
                    verified = False
            finally:
                os.remove(res_file)

    if not samples:
        return None

    samples.sort()
    p95_idx = min(int(0.95 * len(samples)), len(samples) - 1)
    return {
        "median_mbs": statistics.median(samples),
        "p95_mbs": samples[p95_idx],
        "stdev_mbs": statistics.stdev(samples) if len(samples) > 1 else 0.0,
        "write_sec": statistics.median(write_times),
        "drain_ms": statistics.median(drain_times),
        "total_sec": statistics.median(total_times),
        "drain_verified": verified,
        "samples": samples
    }

def run_microbenchmarks():
    print("[bench] Running internal Goose microbenchmarks...")
    try:
        out = subprocess.check_output(
            ["goose", "run", "benchmarks/benchmark_engine.goose"],
            cwd=REPO_DIR
        ).decode()
        plain_mbs = 0.0
        sgr_mbs = 0.0
        scroll_lps = 0.0
        for line in out.splitlines():
            if "Plain throughput" in line and "MB/s" in line:
                plain_mbs = float(line.rsplit("(", 1)[1].split()[0])
            elif "SGR throughput" in line and "MB/s" in line:
                sgr_mbs = float(line.rsplit("(", 1)[1].split()[0])
            elif "Scroll 50k lines" in line and "lines/s" in line:
                scroll_lps = float(line.rsplit("(", 1)[1].split()[0])
        return {
            "plain_ascii_mbs": plain_mbs,
            "sgr_truecolor_mbs": sgr_mbs,
            "scroll_lines_per_sec": scroll_lps
        }
    except Exception as e:
        print(f"[bench] Warning: microbenchmarks failed: {e}")
        return None

def main():
    print("=" * 86)
    print("   GOOSY SCIENTIFIC BENCHMARK SUITE: SYNCHRONIZED REPRODUCIBLE MEASUREMENTS")
    print("=" * 86)
    meta = get_env_metadata()
    print(f"Platform: {meta['os']}")
    print(f"CPU:      {meta['cpu']} ({meta['cores']} cores)")
    print(f"Wayland:  {meta['wayland_display']}")
    payload_size = ensure_payload()
    print(f"Payload:  {PAYLOAD_PATH} ({payload_size / (1024*1024):.2f} MB, {payload_size} bytes)\n")

    micro = run_microbenchmarks()
    if micro:
        print(f"Engine Microbenchmarks:")
        print(f"  - Plain ASCII Streaming:       {micro['plain_ascii_mbs']:.1f} MB/s")
        print(f"  - SGR & Truecolor Parsing:     {micro['sgr_truecolor_mbs']:.1f} MB/s")
        print(f"  - O(1) Circular Scrollback:     {micro['scroll_lines_per_sec']:,.0f} lines/sec\n")

    results = []
    for name, exec_prefix in TERMS:
        bin_path = exec_prefix[0]
        resolved = shutil.which(bin_path) or (bin_path if os.path.exists(bin_path) else None)
        if not resolved:
            print(f"[SKIP] {name}: binary '{bin_path}' not found")
            continue

        print(f"--> Benchmarking {name}...")
        bin_sz = get_binary_size(resolved)
        startup = measure_startup(exec_prefix, runs=5)
        idle = measure_idle(exec_prefix)
        stream = measure_streaming(exec_prefix, runs=3)

        res = {
            "name": name,
            "bin_size_mb": bin_sz,
            "startup": startup,
            "idle": idle,
            "streaming": stream
        }
        results.append(res)
        mbs_str = f"{stream['median_mbs']:.1f} MB/s" if stream else "FAILED"
        print(f"    Startup: {startup['median_ms']:.1f} ms | Idle RSS: {idle['rss_mb']:.1f} MB | Sustained Throughput: {mbs_str}")

    print("\n" + "=" * 98)
    print(f"{'Terminal':<16} | {'Bin Size':<10} | {'Startup':<11} | {'Idle RSS':<10} | {'Idle PSS':<10} | {'Idle CPU':<9} | {'Throughput':<12}")
    print("-" * 98)
    for r in results:
        st_ms = r['startup']['median_ms']
        rss = r['idle']['rss_mb']
        pss = r['idle']['pss_mb']
        cpu = r['idle']['cpu_ticks']
        tp = r['streaming']['median_mbs'] if r['streaming'] else 0.0
        print(f"{r['name']:<16} | {r['bin_size_mb']:>7.2f} MB | {st_ms:>8.1f} ms | {rss:>7.1f} MB | {pss:>7.1f} MB | {cpu:>5d} tks | {tp:>7.1f} MB/s")
    print("=" * 98)

    output_data = {
        "metadata": meta,
        "microbenchmarks": micro,
        "terminals": results
    }
    results_path = os.path.join(REPO_DIR, "benchmarks", "benchmark_results.json")
    with open(results_path, "w") as f:
        json.dump(output_data, f, indent=2)
    print(f"\n[bench] Full scientific results saved to {results_path}")

if __name__ == "__main__":
    main()
