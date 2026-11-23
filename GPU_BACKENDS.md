# Goosy Multi-Backend Rendering Architecture (Vulkan, OpenGL, Pixman)

## 1. Architectural Overview

Goosy implements a unified, high-performance three-backend rendering subsystem designed for native Wayland:

```text
                                  GOOSY CORE (Goose)
                                          |
                                          v
                                RENDER COORDINATOR
                             (native/goosy_render.c)
                                          |
                +-------------------------+-------------------------+
                |                         |                         |
                v                         v                         v
        Vulkan Backend             OpenGL Backend            Pixman Backend
   (native/goosy_render_vk.c) (native/goosy_render_gl.c) (native/goosy_render_pixman.c)
                |                         |                         |
        Hardware GPU (Vulkan)       Hardware GPU (EGL/GLES)    Software Fallback (wl_shm)
                |                         |                         |
                +-------------------------+-------------------------+
                                          |
                                          v
                               NATIVE WAYLAND SURFACE
                               (native/goosy_wayland.c)
```

The unified interface is declared in `native/goosy_render.h` via `goosy_renderer_backend_t`. The render coordinator probes available hardware capabilities and dispatches draw calls to the active backend with zero runtime branching in the Goose terminal engine.

---

## 2. Rendering Backends Specification

### Backend 1: Vulkan Hardware Accelerated (`native/goosy_render_vk.c`)
- **Protocol Integration:** Binds directly to the Wayland display and surface via `VK_KHR_wayland_surface`.
- **Shader Pipeline:** Embedded precompiled SPIR-V bytecode (`native/goosy_vk_shaders.h`) generated offline with `glslc -fshader-stage=vertex -mfmt=c` and `glslc -fshader-stage=fragment -mfmt=c`. Zero runtime dependencies on shader compilers (no `glslangValidator` or `shaderc` needed at runtime).
- **Batching & Memory:** Mapped host-coherent vertex buffer streaming (`VkBuffer` + `VkDeviceMemory`). Dynamically batches up to 65,536 quads per frame.
- **Texture Atlas:** Single-channel `VK_FORMAT_R8_UNORM` staging and GPU device memory atlas with sub-allocation for shaped glyphs.
- **Presentation:** Non-blocking `VK_PRESENT_MODE_MAILBOX_KHR` (triple buffering) with automatic fallback to `VK_PRESENT_MODE_FIFO_KHR`.
- **Lifecycle:** Complete swapchain recreation on Wayland `wl_surface.configure` events with zero driver validation errors.

### Backend 2: OpenGL ES Hardware Accelerated (`native/goosy_render_gl.c`)
- **Protocol Integration:** Hardware GPU rasterization using EGL 1.4+ and OpenGL ES 2.0+ via `wayland-egl` (`wl_egl_window`).
- **Pipeline:** Dynamic VBO and EBO streaming with GLSL vertex and fragment shaders. Orthographic 2D projection matrix mapped to window physical pixels.
- **Texture Atlas:** `GL_LUMINANCE` / `GL_RED` texture atlas for FreeType anti-aliased font glyphs with `GL_LINEAR` filtering for fractional scaling.
- **Buffer Management:** `wl_egl_window_resize` on `EV_CONFIGURE` with automatic swap buffers throttled via Wayland frame callbacks.

### Backend 3: Pixman Universal Software Fallback (`native/goosy_render_pixman.c`)
- **Protocol Integration:** High-performance software universal fallback using shared memory (`wl_shm`). Guaranteed to function on any Wayland compositor even with no GPU drivers installed (e.g. headless VMs, Artix/Arch chroots, CI runners).
- **Optimization:** Optimized 64-bit contiguous word fill, fast integer division (`div255_fast`) for alpha blending, and bounding damage box tracking (`wl_surface_damage_buffer`) to minimize memory traffic.
- **Pixel-Perfect Alignment:** Complete procedural box-drawing implementation (U+2500–U+257F) with exact pixel-level matching to GPU backends.

---

## 3. Dynamic Selection & Auto-Fallback Chain

Goosy supports seamless backend selection via command-line flags and user configuration:

### Command-Line Selection
- `--renderer=auto`: Dynamically probes `Vulkan` -> `OpenGL` -> `Pixman`.
- `--renderer=vulkan`: Forces the Vulkan hardware backend. Falls back to OpenGL, then Pixman if Vulkan is unavailable.
- `--renderer=opengl`: Forces the OpenGL / EGL hardware backend. Falls back to Pixman if EGL is unavailable.
- `--renderer=pixman`: Forces the Pixman software backend.
- `--renderer-info`: Prints the active and available rendering subsystems and exits.

### Configuration File (`~/.config/goosy/goosy.ini`)
```ini
[rendering]
# Rendering backend: auto, vulkan, opengl, or pixman
backend = auto
```

### Auto-Probe Fallback Hierarchy
1. Attempt `Vulkan` initialization (`VK_KHR_wayland_surface` + physical device + swapchain).
2. If Vulkan fails or physical device lacks Wayland surface support, log notice and probe `OpenGL` (EGL display + context + `wayland-egl`).
3. If OpenGL fails, seamlessly fall back to `Pixman` software renderer (`wl_shm`).
4. Zero crashes, zero unhandled errors, and 100% visual parity across all backends.

---

## 4. Visual Parity & Feature Equivalence

All three backends share identical drawing semantics:

| Feature | Vulkan Backend | OpenGL Backend | Pixman Backend |
|:---|:---:|:---:|:---:|
| **Procedural Box Drawing (U+2500..257F)** | Yes (Quad Decomp) | Yes (Quad Decomp) | Yes (Pixel Raster) |
| **FreeType Font Glyph Atlas** | Yes (R8 Atlas) | Yes (GL Atlas) | Yes (Pixman Mask) |
| **Combining Character Overlays** | Yes | Yes | Yes |
| **Truecolor (24-bit RGB)** | Yes | Yes | Yes |
| **Underline Styles (Curly, Dotted, etc.)** | Yes | Yes | Yes |
| **Selection Highlighting** | Yes | Yes | Yes |
| **Wayland CSD Title Bar** | Yes | Yes | Yes |
| **Sixel Graphics** | Yes | Yes | Yes |
| **Kitty Graphics Protocol** | Yes | Yes | Yes |
| **Fractional Scaling Support** | Yes | Yes | Yes |
