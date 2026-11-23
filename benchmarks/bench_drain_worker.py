#!/usr/bin/env python3
import sys
import os
import time
import json
import termios
import tty
import select

def run_worker(payload_path, result_path):
    fd_in = sys.stdin.fileno()
    old_attrs = None
    is_tty = os.isatty(fd_in)
    if is_tty:
        old_attrs = termios.tcgetattr(fd_in)
        tty.setraw(fd_in)
    
    try:
        with open(payload_path, "rb") as f:
            data = f.read()
        payload_bytes = len(data)
        
        if is_tty:
            termios.tcflush(fd_in, termios.TCIFLUSH)
        
        stdout_fd = sys.stdout.fileno()
        t_start = time.perf_counter()
        
        # Write payload in 64 KB chunks
        chunk_size = 65536
        offset = 0
        while offset < payload_bytes:
            end = min(offset + chunk_size, payload_bytes)
            chunk = data[offset:end]
            written = os.write(stdout_fd, chunk)
            offset += written
            
        t_write_done = time.perf_counter()
        
        # Send synchronization DA query: \033[c
        os.write(stdout_fd, b"\033[c")
        
        response_bytes = bytearray()
        deadline = time.perf_counter() + 30.0
        drain_verified = False
        
        while time.perf_counter() < deadline:
            r, _, _ = select.select([fd_in], [], [], 0.1)
            if r:
                b = os.read(fd_in, 1024)
                if not b:
                    break
                response_bytes.extend(b)
                if b'c' in response_bytes or b'R' in response_bytes or b';' in response_bytes:
                    drain_verified = True
                    break
        
        t_drain_done = time.perf_counter()
        
        total_seconds = t_drain_done - t_start
        write_seconds = t_write_done - t_start
        drain_seconds = t_drain_done - t_write_done
        mbs = (payload_bytes / (1024.0 * 1024.0)) / total_seconds if total_seconds > 0 else 0.0
        
        result = {
            "payload_bytes": payload_bytes,
            "write_seconds": write_seconds,
            "drain_seconds": drain_seconds,
            "total_seconds": total_seconds,
            "mbs": mbs,
            "drain_verified": drain_verified,
            "response": bytes(response_bytes).decode('ascii', errors='replace')
        }
        
        with open(result_path, "w") as rf:
            json.dump(result, rf, indent=2)
            
    finally:
        if is_tty and old_attrs:
            termios.tcsetattr(fd_in, termios.TCSADRAIN, old_attrs)

if __name__ == "__main__":
    if len(sys.argv) >= 3:
        run_worker(sys.argv[1], sys.argv[2])
    else:
        sys.exit(1)
