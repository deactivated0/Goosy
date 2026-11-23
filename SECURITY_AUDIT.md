# Goosy Security Audit & Vulnerability Analysis

## Threat Model & Boundaries
Goosy interacts with:
1. Untrusted terminal data streams (VT/ANSI sequences from local and remote shells).
2. Graphic image payloads (Sixel and Kitty graphics streams).
3. Clipboard data (OSC 52 and Wayland selection transfers).
4. System environment and command line arguments.

---

## Audit Findings & Remediation Plan

### 1. Sixel Parsing Integer Overflow & Memory Budget
- **Severity:** High (Potential Denial of Service / Allocation Exhaustion)
- **Location:** `native/goosy_image.c:goosy_sixel_decode`
- **Issue:** Sixel repeat count (`!count`) and width/height expansion had no hard ceiling. A malicious sequence could request allocating gigabytes of memory.
- **Fix:** Enforced maximum image dimensions (`4096 x 4096` pixels max, 16 MB pixel buffer cap). Clamped repeat count and dimension bursts. Verified with `test_security_fuzz.c`.
- **Status:** Resolved & Verified.

### 2. Kitty Graphics Protocol Memory Safety & Chunking
- **Severity:** Medium (Resource Exhaustion / Buffer Over-read)
- **Location:** `native/goosy_image.c:goosy_kitty_graphics`
- **Issue:** Base64 decoding lacked strict bounds on input payload length and maximum decoded dimensions for raw RGBA/RGB and PNG. Multipacket transfers (`m=1`) were unhandled.
- **Fix:** Enforced 32 MB max payload cap, validated image dimensions (4096×4096, 16 MP max) prior to memory allocation. Implemented bounded chunk reassembly buffer (`m=1`/`m=0`) and targeted deletion by ID. Safe PTY write routing for query responses.
- **Status:** Resolved & Verified.

### 3. PTY Write Buffer Overflow & Data Truncation
- **Severity:** Medium (Data Loss / Truncated Inputs)
- **Location:** `native/goosy_pty.c:goosy_pty_write`
- **Issue:** Fixed 64 KiB buffer silently drops bytes if incoming data exceeds available free buffer space during burst pastes.
- **Fix:** Implemented dynamically growable FIFO buffer up to 16 MiB with non-blocking write loop, backpressure handling, and drain via `EPOLLOUT`.
- **Status:** Resolved & Verified.

### 4. URL Opening Security
- **Severity:** High (Command Injection Risk if Malformed)
- **Location:** `native/goosy_pty.c:goosy_open_url`
- **Issue:** Must guarantee that URLs are strictly validated for safe schemes (`http://`, `https://`) and executed directly via `fork` + `execv` with `xdg-open`, never passed to a shell (`sh -c` or `system()`).
- **Status:** Verified safe in `native/goosy_pty.c` (uses `execvp("xdg-open", ...)` directly without shell).
