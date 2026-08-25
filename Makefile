CC ?= cc
CXX ?= c++
AR ?= ar
CFLAGS ?= -O2
CXXFLAGS ?= -O2
CPPFLAGS += -Iinclude
CFLAGS += -std=c99 -Wall -Wextra -Wpedantic
CXXFLAGS += -std=c++11 -Wall -Wextra -Wpedantic

IMGUI_DIR := examples/imgui
BUILD_DIR := .build
X11_CFLAGS := $(shell pkg-config --cflags x11 xext 2>/dev/null)
X11_LIBS := $(shell pkg-config --libs x11 xext 2>/dev/null)

CORE_OBJECTS := $(BUILD_DIR)/ntgl.o $(BUILD_DIR)/gl_compat.o \
	$(BUILD_DIR)/gles2.o $(BUILD_DIR)/port.o
IMGUI_OBJECTS := $(BUILD_DIR)/imgui.o $(BUILD_DIR)/imgui_draw.o \
	$(BUILD_DIR)/imgui_tables.o $(BUILD_DIR)/imgui_widgets.o \
	$(BUILD_DIR)/imgui_impl_opengl2.o
IMGUI_GLES2_OBJECT := $(BUILD_DIR)/imgui_impl_opengl3_es2.o

.PHONY: all core micro lite imgui examples example showcase x11-example x11-blend-example x11-stencil-example \
	x11-imgui-example run-example run-x11-example run-x11-imgui-example \
	run-x11-blend-example run-x11-stencil-example run-showcase test clean check-imgui check-x11

all: core
core: libmesaGL.a
micro: libmesaGL.a
lite: libmesaGL.a
imgui: libmesaGL_imgui.a
examples: example x11-example x11-blend-example x11-stencil-example x11-imgui-example showcase
example: mesaGL_imgui_micro_framebuffer
showcase: mesaGL_x11_showcase

run-example: mesaGL_imgui_micro_framebuffer
	./mesaGL_imgui_micro_framebuffer

check-imgui:
	@test -f $(IMGUI_DIR)/imgui.cpp || { echo "Dear ImGui is missing from $(IMGUI_DIR)" >&2; exit 1; }

check-x11:
	@pkg-config --exists x11 xext || { echo "X11 and Xext development files are required" >&2; exit 1; }

$(BUILD_DIR):
	mkdir -p $@

$(BUILD_DIR)/ntgl.o: src/ntgl.c include/mesaGL/ntgl.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/gl_compat.o: src/gl_compat.c include/GL/gl.h include/mesaGL/ntgl.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/port.o: src/port.c include/mesaGL/port.h include/mesaGL/ntgl.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/mesaGL_x11.o: ports/x11/mesaGL_x11.c ports/x11/mesaGL_x11.h | $(BUILD_DIR) check-x11
	$(CC) $(CPPFLAGS) -Iports/x11 $(X11_CFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/gles2.o: src/gles2.c src/gles2_internal.h include/GLES2/gl2.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/imgui.o: $(IMGUI_DIR)/imgui.cpp | $(BUILD_DIR) check-imgui
	$(CXX) $(CPPFLAGS) -I$(IMGUI_DIR) -I$(IMGUI_DIR)/backends $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/imgui_draw.o: $(IMGUI_DIR)/imgui_draw.cpp | $(BUILD_DIR) check-imgui
	$(CXX) $(CPPFLAGS) -I$(IMGUI_DIR) -I$(IMGUI_DIR)/backends $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/imgui_tables.o: $(IMGUI_DIR)/imgui_tables.cpp | $(BUILD_DIR) check-imgui
	$(CXX) $(CPPFLAGS) -I$(IMGUI_DIR) -I$(IMGUI_DIR)/backends $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/imgui_widgets.o: $(IMGUI_DIR)/imgui_widgets.cpp | $(BUILD_DIR) check-imgui
	$(CXX) $(CPPFLAGS) -I$(IMGUI_DIR) -I$(IMGUI_DIR)/backends $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/imgui_impl_opengl2.o: $(IMGUI_DIR)/backends/imgui_impl_opengl2.cpp | $(BUILD_DIR) check-imgui
	$(CXX) $(CPPFLAGS) -I$(IMGUI_DIR) -I$(IMGUI_DIR)/backends $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/imgui_impl_opengl3_es2.o: $(IMGUI_DIR)/backends/imgui_impl_opengl3.cpp | $(BUILD_DIR) check-imgui
	$(CXX) $(CPPFLAGS) -DIMGUI_IMPL_OPENGL_ES2 -I$(IMGUI_DIR) -I$(IMGUI_DIR)/backends $(CXXFLAGS) -c $< -o $@

libmesaGL.a: $(CORE_OBJECTS)
	$(AR) rcs $@ $^

libmesaGL_imgui.a: $(IMGUI_OBJECTS)
	$(AR) rcs $@ $^

mesaGL_imgui_micro_framebuffer: examples/imgui_micro_framebuffer.cpp libmesaGL_imgui.a libmesaGL.a
	$(CXX) $(CPPFLAGS) -I$(IMGUI_DIR) -I$(IMGUI_DIR)/backends $(CXXFLAGS) $< libmesaGL_imgui.a libmesaGL.a -lm -o $@

mesaGL_imgui_gles2_framebuffer: examples/imgui_gles2_framebuffer.cpp libmesaGL_imgui.a $(IMGUI_GLES2_OBJECT) libmesaGL.a
	$(CXX) $(CPPFLAGS) -DIMGUI_IMPL_OPENGL_ES2 -I$(IMGUI_DIR) -I$(IMGUI_DIR)/backends $(CXXFLAGS) $< $(IMGUI_GLES2_OBJECT) libmesaGL_imgui.a libmesaGL.a -lm -o $@

mesaGL_x11_example: examples/x11.c $(BUILD_DIR)/mesaGL_x11.o libmesaGL.a
	$(CC) $(CPPFLAGS) -Iports/x11 $(X11_CFLAGS) $(CFLAGS) examples/x11.c $(BUILD_DIR)/mesaGL_x11.o libmesaGL.a -lm $(X11_LIBS) -o $@

mesaGL_x11_blend_example: examples/x11_blend.c $(BUILD_DIR)/mesaGL_x11.o libmesaGL.a
	$(CC) $(CPPFLAGS) -Iports/x11 $(X11_CFLAGS) $(CFLAGS) $< $(BUILD_DIR)/mesaGL_x11.o libmesaGL.a -lm $(X11_LIBS) -o $@

mesaGL_x11_stencil_example: examples/x11_stencil.c $(BUILD_DIR)/mesaGL_x11.o libmesaGL.a
	$(CC) $(CPPFLAGS) -Iports/x11 $(X11_CFLAGS) $(CFLAGS) $< $(BUILD_DIR)/mesaGL_x11.o libmesaGL.a -lm $(X11_LIBS) -o $@

mesaGL_x11_fbo_example: examples/x11_fbo.c $(BUILD_DIR)/mesaGL_x11.o libmesaGL.a
	$(CC) $(CPPFLAGS) -Iports/x11 $(X11_CFLAGS) $(CFLAGS) $< $(BUILD_DIR)/mesaGL_x11.o libmesaGL.a -lm $(X11_LIBS) -o $@

mesaGL_x11_contexts_example: examples/x11_contexts.c $(BUILD_DIR)/mesaGL_x11.o libmesaGL.a
	$(CC) $(CPPFLAGS) -Iports/x11 $(X11_CFLAGS) $(CFLAGS) $< $(BUILD_DIR)/mesaGL_x11.o libmesaGL.a -lm $(X11_LIBS) -o $@

mesaGL_x11_texture_formats_example: examples/x11_texture_formats.c $(BUILD_DIR)/mesaGL_x11.o libmesaGL.a
	$(CC) $(CPPFLAGS) -Iports/x11 $(X11_CFLAGS) $(CFLAGS) $< $(BUILD_DIR)/mesaGL_x11.o libmesaGL.a -lm $(X11_LIBS) -o $@

mesaGL_x11_texenv_example: examples/x11_texenv.c $(BUILD_DIR)/mesaGL_x11.o libmesaGL.a
	$(CC) $(CPPFLAGS) -Iports/x11 $(X11_CFLAGS) $(CFLAGS) $< $(BUILD_DIR)/mesaGL_x11.o libmesaGL.a -lm $(X11_LIBS) -o $@

mesaGL_x11_alpha_test_example: examples/x11_alpha_test.c $(BUILD_DIR)/mesaGL_x11.o libmesaGL.a
	$(CC) $(CPPFLAGS) -Iports/x11 $(X11_CFLAGS) $(CFLAGS) $< $(BUILD_DIR)/mesaGL_x11.o libmesaGL.a -lm $(X11_LIBS) -o $@

mesaGL_x11_shading_example: examples/x11_shading.c $(BUILD_DIR)/mesaGL_x11.o libmesaGL.a
	$(CC) $(CPPFLAGS) -Iports/x11 $(X11_CFLAGS) $(CFLAGS) $< $(BUILD_DIR)/mesaGL_x11.o libmesaGL.a -lm $(X11_LIBS) -o $@

mesaGL_x11_polygon_mode_example: examples/x11_polygon_mode.c $(BUILD_DIR)/mesaGL_x11.o libmesaGL.a
	$(CC) $(CPPFLAGS) -Iports/x11 $(X11_CFLAGS) $(CFLAGS) $< $(BUILD_DIR)/mesaGL_x11.o libmesaGL.a -lm $(X11_LIBS) -o $@

mesaGL_x11_line_width_example: examples/x11_line_width.c $(BUILD_DIR)/mesaGL_x11.o libmesaGL.a
	$(CC) $(CPPFLAGS) -Iports/x11 $(X11_CFLAGS) $(CFLAGS) $< $(BUILD_DIR)/mesaGL_x11.o libmesaGL.a -lm $(X11_LIBS) -o $@

mesaGL_x11_texture_matrix_example: examples/x11_texture_matrix.c $(BUILD_DIR)/mesaGL_x11.o libmesaGL.a
	$(CC) $(CPPFLAGS) -Iports/x11 $(X11_CFLAGS) $(CFLAGS) $< $(BUILD_DIR)/mesaGL_x11.o libmesaGL.a -lm $(X11_LIBS) -o $@

mesaGL_x11_lighting_example: examples/x11_lighting.c $(BUILD_DIR)/mesaGL_x11.o libmesaGL.a
	$(CC) $(CPPFLAGS) -Iports/x11 $(X11_CFLAGS) $(CFLAGS) $< $(BUILD_DIR)/mesaGL_x11.o libmesaGL.a -lm $(X11_LIBS) -o $@

mesaGL_x11_specular_example: examples/x11_specular.c $(BUILD_DIR)/mesaGL_x11.o libmesaGL.a
	$(CC) $(CPPFLAGS) -Iports/x11 $(X11_CFLAGS) $(CFLAGS) $< $(BUILD_DIR)/mesaGL_x11.o libmesaGL.a -lm $(X11_LIBS) -o $@

mesaGL_x11_multilight_example: examples/x11_multilight.c $(BUILD_DIR)/mesaGL_x11.o libmesaGL.a
	$(CC) $(CPPFLAGS) -Iports/x11 $(X11_CFLAGS) $(CFLAGS) $< $(BUILD_DIR)/mesaGL_x11.o libmesaGL.a -lm $(X11_LIBS) -o $@

mesaGL_x11_fog_example: examples/x11_fog.c $(BUILD_DIR)/mesaGL_x11.o libmesaGL.a
	$(CC) $(CPPFLAGS) -Iports/x11 $(X11_CFLAGS) $(CFLAGS) $< $(BUILD_DIR)/mesaGL_x11.o libmesaGL.a -lm $(X11_LIBS) -o $@

mesaGL_x11_normalize_example: examples/x11_normalize.c $(BUILD_DIR)/mesaGL_x11.o libmesaGL.a
	$(CC) $(CPPFLAGS) -Iports/x11 $(X11_CFLAGS) $(CFLAGS) $< $(BUILD_DIR)/mesaGL_x11.o libmesaGL.a -lm $(X11_LIBS) -o $@

x11-fbo-example: mesaGL_x11_fbo_example

run-x11-fbo-example: mesaGL_x11_fbo_example
	./mesaGL_x11_fbo_example

run-x11-contexts-example: mesaGL_x11_contexts_example
	./mesaGL_x11_contexts_example

run-x11-texture-formats-example: mesaGL_x11_texture_formats_example
	./mesaGL_x11_texture_formats_example

run-x11-texenv-example: mesaGL_x11_texenv_example
	./mesaGL_x11_texenv_example

run-x11-alpha-test-example: mesaGL_x11_alpha_test_example
	./mesaGL_x11_alpha_test_example

run-x11-shading-example: mesaGL_x11_shading_example
	./mesaGL_x11_shading_example

run-x11-polygon-mode-example: mesaGL_x11_polygon_mode_example
	./mesaGL_x11_polygon_mode_example

run-x11-line-width-example: mesaGL_x11_line_width_example
	./mesaGL_x11_line_width_example

run-x11-texture-matrix-example: mesaGL_x11_texture_matrix_example
	./mesaGL_x11_texture_matrix_example

run-x11-lighting-example: mesaGL_x11_lighting_example
	./mesaGL_x11_lighting_example

run-x11-specular-example: mesaGL_x11_specular_example
	./mesaGL_x11_specular_example

run-x11-multilight-example: mesaGL_x11_multilight_example
	./mesaGL_x11_multilight_example

run-x11-fog-example: mesaGL_x11_fog_example
	./mesaGL_x11_fog_example

run-x11-normalize-example: mesaGL_x11_normalize_example
	./mesaGL_x11_normalize_example

mesaGL_x11_imgui_example: examples/x11_imgui.cpp $(BUILD_DIR)/mesaGL_x11.o libmesaGL_imgui.a $(IMGUI_GLES2_OBJECT) libmesaGL.a
	$(CXX) $(CPPFLAGS) -DIMGUI_IMPL_OPENGL_ES2 -Iports/x11 -I$(IMGUI_DIR) -I$(IMGUI_DIR)/backends $(CXXFLAGS) $< $(BUILD_DIR)/mesaGL_x11.o $(IMGUI_GLES2_OBJECT) libmesaGL_imgui.a libmesaGL.a -lm $(X11_LIBS) -o $@

mesaGL_x11_showcase: examples/x11_showcase.cpp $(BUILD_DIR)/mesaGL_x11.o libmesaGL_imgui.a $(IMGUI_GLES2_OBJECT) libmesaGL.a
	$(CXX) $(CPPFLAGS) -DIMGUI_IMPL_OPENGL_ES2 -Iports/x11 -I$(IMGUI_DIR) -I$(IMGUI_DIR)/backends $(CXXFLAGS) $< $(BUILD_DIR)/mesaGL_x11.o $(IMGUI_GLES2_OBJECT) libmesaGL_imgui.a libmesaGL.a -lm $(X11_LIBS) -o $@

x11-example: mesaGL_x11_example
x11-blend-example: mesaGL_x11_blend_example
x11-stencil-example: mesaGL_x11_stencil_example
x11-imgui-example: mesaGL_x11_imgui_example

run-x11-example: mesaGL_x11_example
	./mesaGL_x11_example

run-x11-blend-example: mesaGL_x11_blend_example
	./mesaGL_x11_blend_example

run-x11-stencil-example: mesaGL_x11_stencil_example
	./mesaGL_x11_stencil_example

run-x11-imgui-example: mesaGL_x11_imgui_example
	./mesaGL_x11_imgui_example

run-showcase: mesaGL_x11_showcase
	./mesaGL_x11_showcase

mesaGL_lite_test: tests/test_render.c libmesaGL.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $< libmesaGL.a -lm -o $@

mesaGL_gl_test: tests/test_gl_compat.c libmesaGL.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $< libmesaGL.a -lm -o $@

mesaGL_rgb565_test: tests/test_rgb565.c libmesaGL.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $< libmesaGL.a -lm -o $@

mesaGL_gles2_test: tests/test_gles2.c libmesaGL.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $< libmesaGL.a -lm -o $@

mesaGL_context_test: tests/test_contexts.c libmesaGL.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $< libmesaGL.a -lm -o $@

mesaGL_lifecycle_test: tests/test_lifecycle.c libmesaGL.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $< libmesaGL.a -lm -o $@

mesaGL_lighting_test: tests/test_lighting.c libmesaGL.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $< libmesaGL.a -lm -o $@

mesaGL_readpixels_test: tests/test_readpixels.c libmesaGL.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $< libmesaGL.a -lm -o $@

test: mesaGL_lite_test mesaGL_gl_test mesaGL_rgb565_test mesaGL_gles2_test mesaGL_context_test mesaGL_lifecycle_test mesaGL_lighting_test mesaGL_readpixels_test mesaGL_imgui_micro_framebuffer mesaGL_imgui_gles2_framebuffer
	./mesaGL_lite_test
	./mesaGL_gl_test
	./mesaGL_rgb565_test
	./mesaGL_gles2_test
	./mesaGL_context_test
	./mesaGL_lifecycle_test
	./mesaGL_lighting_test
	./mesaGL_readpixels_test
	./mesaGL_imgui_micro_framebuffer
	./mesaGL_imgui_gles2_framebuffer

clean:
	$(RM) -r $(BUILD_DIR)
	$(RM) libmesaGL.a libmesaGL_imgui.a
	$(RM) mesaGL_lite_test mesaGL_gl_test mesaGL_rgb565_test mesaGL_gles2_test
	$(RM) mesaGL_context_test
	$(RM) mesaGL_lifecycle_test
	$(RM) mesaGL_lighting_test
	$(RM) mesaGL_readpixels_test
	$(RM) mesaGL_imgui_micro_framebuffer
	$(RM) mesaGL_imgui_gles2_framebuffer
	$(RM) mesaGL_x11_example
	$(RM) mesaGL_x11_blend_example
	$(RM) mesaGL_x11_stencil_example
	$(RM) mesaGL_x11_fbo_example
	$(RM) mesaGL_x11_contexts_example
	$(RM) mesaGL_x11_texture_formats_example
	$(RM) mesaGL_x11_texenv_example
	$(RM) mesaGL_x11_alpha_test_example
	$(RM) mesaGL_x11_shading_example
	$(RM) mesaGL_x11_polygon_mode_example
	$(RM) mesaGL_x11_line_width_example
	$(RM) mesaGL_x11_texture_matrix_example
	$(RM) mesaGL_x11_lighting_example
	$(RM) mesaGL_x11_specular_example
	$(RM) mesaGL_x11_multilight_example
	$(RM) mesaGL_x11_fog_example
	$(RM) mesaGL_x11_normalize_example
	$(RM) mesaGL_x11_imgui_example
	$(RM) mesaGL_x11_showcase
