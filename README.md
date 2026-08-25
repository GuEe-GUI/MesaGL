# MesaGL

MesaGL is a compact software OpenGL-compatible renderer for framebuffer-only systems without a
GPU driver. The core library has no dependency on Linux, X11, EGL, Mesa, or RT-Thread. A platform
only needs to provide writable framebuffer memory, allocation functions, and an optional present
callback.

It is intended for operating systems without graphics drivers, direct-framebuffer applications,
and reasonably capable MCUs. The X11 backend is provided only as a Linux reference port and
development environment.

## Features

- Fixed-function OpenGL compatibility API with matrix stacks, immediate mode, vertex arrays,
  colors, textures, depth testing, blending, scissoring, and face culling.
- An OpenGL ES 2.0 API subset for UI renderers, including shader/program objects, VBOs, index
  buffers, and vertex attributes.
- RGB565, RGB888, BGR888, XRGB8888, ARGB8888, RGBA8888, and BGRA8888 framebuffers.
- Positive or negative framebuffer stride and top-left or bottom-left memory origins.
- Dear ImGui support through `imgui_impl_opengl2` and the OpenGL ES 2 mode of
  `imgui_impl_opengl3`.
- No EGL or window-system dependency in the core library.

The fixed-function path currently provides:

| Category | Supported functionality |
| --- | --- |
| Primitives | Points, lines, line strips/loops, triangles, triangle strips/fans, and quads |
| Transforms | Model-view, projection, and texture stacks; ortho, frustum, translation, scaling, and rotation |
| Depth/stencil | All eight comparison functions, write masks, and 8-bit stencil operations |
| Color | RGBA interpolation, per-channel write masking, and 16-bit through 32-bit framebuffers |
| Lighting | Eight lights, ambient/diffuse/specular material, inverse-transpose normal transform, normalization, and color material |
| Fog | Linear, exponential, and squared-exponential eye-distance fog |
| Blending | Source/destination and constant color/alpha factors, with separate add/subtract/min/max RGB and alpha equations |
| Textures | RGBA upload and sub-update, nearest/linear filtering, repeat/clamp, and texture env |
| Raster state | Viewport, scissor, front-face selection, culling, polygon modes, point size, and line width |
| Render targets | Default framebuffer and texture-backed FBOs with depth/stencil attachments |
| Queries | Common enable, binding, viewport, scissor, depth, color-mask, and GLES2 state queries |

MesaGL is not a complete or formally conformant OpenGL 2.0 implementation. Its GLES2 shader
compiler recognizes the UI shader subset required by the included use cases; it does not execute
arbitrary GLSL programs. The project targets software-rendered user interfaces and simple 2D/3D
graphics rather than desktop OpenGL games or shader-heavy applications.

Mipmaps, multiple simultaneously sampled texture units, and general GLSL execution are not
currently implemented. Framebuffer objects support one color
texture plus depth/stencil renderbuffer attachments; multiple render targets are not supported.
The GLES2 frontend accepts the attribute, textured-fragment, sampler, and projection-matrix shader
patterns used by the included UI backends. Uniforms outside that recognized subset do not drive a
general-purpose shader virtual machine.

## Building

The core library requires a C99 compiler, Make, a C library, and a small subset of the math
library:

```sh
make
```

Available targets include:

```sh
make test
make examples
make run-showcase
make x11-example
make x11-blend-example
make x11-stencil-example
make run-x11-polygon-mode-example
make run-x11-line-width-example
make run-x11-texture-matrix-example
make run-x11-lighting-example
make run-x11-specular-example
make run-x11-multilight-example
make run-x11-fog-example
make run-x11-normalize-example
make x11-imgui-example
make clean
```

The X11 examples additionally require the X11 and Xext development files and `pkg-config`.

`make run-showcase` is the primary integration demo. It combines an animated texture-backed FBO,
depth testing, two-light material shading, specular highlights, fog, texture compositing,
framebuffer blending, and Dear ImGui through the GLES2 UI path in one X11 window.

### Blend Validation

Run `make run-x11-blend-example` to open the visual blend test. Each cell draws the same
semi-transparent red rectangle over a blue background:

- Top left: standard source-alpha blending; the overlap is a red/blue mixture.
- Top right: additive blending; the overlap is brighter than either input.
- Bottom left: source minus destination; the blue component is removed.
- Bottom right: destination minus source; the red component is removed.
- Top far right: constant-color blending; the red source is reduced to one quarter intensity.
- Bottom far right: constant-alpha blending; the result is mostly blue with a small red component.

`make test` also verifies subtract blending and channel write masks against exact RGB565 pixel
results without requiring a window system.

### Stencil Validation

Run `make run-x11-stencil-example`. The result must be a green diamond clipped horizontally by a
rectangle on a dark background. No green pixels may appear outside the diamond stencil mask.

## Framebuffer Port

A platform port supplies a `MesaGLPortConfig`:

```c
MesaGLPortConfig config = {
    .framebuffer = {
        .pixels = framebuffer,
        .width = width,
        .height = height,
        .stride = stride_bytes,
        .format = NTGL_RGB565,
        .origin = NTGL_ORIGIN_TOP_LEFT,
    },
    .allocator = {
        .alloc = platform_alloc,
        .free = platform_free,
        .user = platform_data,
    },
    .present = platform_present,
    .user = platform_data,
};

MesaGLPortContext *context = mesaGLPortCreate(&config);
mesaGLPortMakeCurrent(context);

/* Render through the gl* or ntgl* API. */
mesaGLPortPresent(context);
mesaGLPortDestroy(context);
```

The `present` callback may be null when the framebuffer maps directly to display memory. A port
can use it to flush caches, wait for vertical blanking, copy a back buffer, or submit an LCD DMA
transfer. Platforms without a standard heap can provide custom allocation functions; otherwise,
the core uses `malloc` and `free`.

Public interfaces are organized as follows:

- `include/GL/gl.h`: fixed-function OpenGL compatibility API.
- `include/GLES2/gl2.h`: OpenGL ES 2.0 UI subset.
- `include/mesaGL/ntgl.h`: low-level software rasterizer API.
- `include/mesaGL/port.h`: platform integration API.
- `ports/x11`: X11/MIT-SHM reference port.

Memory-constrained platforms can override `MESAGL_MAX_VERTICES` at compile time.

## Code Style

Project-owned C and C++ code uses Linux-style braces with four-space indentation. The bundled
`.clang-format` captures this convention. Files in the Dear ImGui submodule retain their upstream
formatting.

## License

MesaGL is distributed under the same MIT License used by the Mesa core library. See `LICENSE` for
the complete text. The Dear ImGui submodule retains its own license.
