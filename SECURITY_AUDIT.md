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
- **Location:** `native/goot_image.c:goot_sixel_decode`
- **Issue:** Sixel repeat count (`!count`) and width/height expansion had no hard ceiling. A malicious sequence could request allocating gigabytes of memory.
- **Fix:** Enforce maximum image dimensions (`4096 x 4096` pixels max, 16 MB pixel buffer cap). Reject out-of-range repeat counts and dimension bursts. Ensure proper cleanup if reallocations fail.

### 2. Kitty Graphics Protocol Memory Safety
- **Severity:** Medium (Resource Exhaustion / Buffer Over-read)
- **Location:** `native/goot_image.c:goot_kitty_graphics`
- **Issue:** Base64 decoding lacked strict bounds on input payload length and maximum decoded dimensions for raw RGBA/RGB and PNG.
- **Fix:** Impose strict decoded length bounds and validate image dimensions prior to memory allocation. Use safe PTY write routing for query responses.

### 3. PTY Write Buffer Overflow & Data Truncation
- **Severity:** Medium (Data Loss / Truncated Inputs)
- **Location:** `native/goot_pty.c:goot_pty_write`
- **Issue:** Fixed 64 KiB buffer silently drops bytes if incoming data exceeds available free buffer space during burst pastes.
- **Fix:** Implement non-blocking write loop with dynamic buffer growth or reliable backpressure handling.

### 4. URL Opening Security
- **Severity:** High (Command Injection Risk if Malformed)
- **Location:** `native/goot_pty.c:goot_open_url`
- **Issue:** Must guarantee that URLs are strictly validated for safe schemes (`http://`, `https://`) and executed directly via `fork` + `execv` with `xdg-open`, never passed to a shell (`sh -c` or `system()`).
- **Status:** Verified safe in `native/goot_pty.c` (uses `execvp("xdg-open", ...)` directly without shell).
