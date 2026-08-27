CC ?= cc
CXX ?= c++
AR ?= ar
.DEFAULT_GOAL := all
CFLAGS ?= -O2
CXXFLAGS ?= -O2
CPPFLAGS += -Iinclude
CFLAGS += -std=c99 -Wall -Wextra -Wpedantic
CXXFLAGS += -std=c++11 -Wall -Wextra -Wpedantic

IMGUI_DIR := examples/imgui
BUILD_DIR := .build
LITE_BUILD_DIR := .build-lite
GLES2_BUILD_DIR := .build-gles2
X11_CFLAGS := $(shell pkg-config --cflags x11 xext 2>/dev/null)
X11_LIBS := $(shell pkg-config --libs x11 xext 2>/dev/null)

CORE_OBJECTS := $(BUILD_DIR)/ntgl.o $(BUILD_DIR)/gl_compat.o \
	$(BUILD_DIR)/glsl_vm.o $(BUILD_DIR)/glsl_preprocessor.o $(BUILD_DIR)/gles2.o \
	$(BUILD_DIR)/port.o
IMGUI_OBJECTS := $(BUILD_DIR)/imgui.o $(BUILD_DIR)/imgui_draw.o \
	$(BUILD_DIR)/imgui_tables.o $(BUILD_DIR)/imgui_widgets.o \
	$(BUILD_DIR)/imgui_impl_opengl2.o
IMGUI_GLES2_OBJECT := $(BUILD_DIR)/imgui_impl_opengl3_es2.o
IMGUI_X11_OBJECT := $(BUILD_DIR)/mesaGL_imgui_x11.o
LITE_CORE_OBJECTS := $(LITE_BUILD_DIR)/ntgl.o $(LITE_BUILD_DIR)/gl_compat.o \
	$(LITE_BUILD_DIR)/glsl_preprocessor.o $(LITE_BUILD_DIR)/gles2.o \
	$(LITE_BUILD_DIR)/port.o
GLES2_CORE_OBJECTS := $(GLES2_BUILD_DIR)/ntgl.o $(GLES2_BUILD_DIR)/gl_compat.o \
	$(GLES2_BUILD_DIR)/glsl_vm.o $(GLES2_BUILD_DIR)/glsl_preprocessor.o \
	$(GLES2_BUILD_DIR)/gles2.o $(GLES2_BUILD_DIR)/port.o

CORE_OBJECTS += $(BUILD_DIR)/simd.o
LITE_CORE_OBJECTS += $(LITE_BUILD_DIR)/simd.o
GLES2_CORE_OBJECTS += $(GLES2_BUILD_DIR)/simd.o

$(CORE_OBJECTS) $(LITE_CORE_OBJECTS) $(GLES2_CORE_OBJECTS): include/mesaGL/config.h

.PHONY: all core gles2 micro lite imgui examples example showcase x11-example x11-blend-example x11-stencil-example \
	x11-imgui-example run-example run-x11-example run-x11-imgui-example \
	run-x11-blend-example run-x11-stencil-example run-showcase run-x11-showcase \
	test clean check-imgui check-x11 \
	check-gles2-symbols shader-compile-probe

all: core gles2 lite
core: libmesaGL.a
gles2: libmesaGL_gles2.a
micro: libmesaGL.a
lite: libmesaGL_lite.a
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

$(LITE_BUILD_DIR):
	mkdir -p $@

$(GLES2_BUILD_DIR):
	mkdir -p $@

$(LITE_BUILD_DIR)/%.o: src/%.c | $(LITE_BUILD_DIR)
	$(CC) $(CPPFLAGS) -DMESAGL_GLES2_PROFILE=MESAGL_GLES2_PROFILE_LITE $(CFLAGS) -c $< -o $@

$(GLES2_BUILD_DIR)/%.o: src/%.c | $(GLES2_BUILD_DIR)
	$(CC) $(CPPFLAGS) -DMESAGL_STRICT_GLES2=1 $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/ntgl.o: src/ntgl.c include/mesaGL/ntgl.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/gl_compat.o: src/gl_compat.c include/GL/gl.h include/mesaGL/ntgl.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/glsl_vm.o: src/glsl_vm.c src/glsl_vm.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/glsl_preprocessor.o: src/glsl_preprocessor.c src/glsl_preprocessor.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/port.o: src/port.c include/mesaGL/port.h include/mesaGL/ntgl.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/simd.o: src/simd.c include/mesaGL/simd.h include/mesaGL/ntgl.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/mesaGL_x11.o: ports/x11/mesaGL_x11.c ports/x11/mesaGL_x11.h | $(BUILD_DIR) check-x11
	$(CC) $(CPPFLAGS) -Iports/x11 $(X11_CFLAGS) $(CFLAGS) -c $< -o $@

$(IMGUI_X11_OBJECT): ports/x11/mesaGL_imgui_x11.cpp ports/x11/mesaGL_imgui_x11.h \
	ports/x11/mesaGL_x11.h $(IMGUI_DIR)/imgui.h | $(BUILD_DIR) check-imgui
	$(CXX) $(CPPFLAGS) -Iports/x11 -I$(IMGUI_DIR) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/gles2.o: src/gles2.c src/gles2_internal.h src/glsl_vm.h \
	src/glsl_preprocessor.h include/GLES2/gl2.h | $(BUILD_DIR)
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

libmesaGL_lite.a: $(LITE_CORE_OBJECTS)
	$(AR) rcs $@ $^

libmesaGL_gles2.a: $(GLES2_CORE_OBJECTS)
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

mesaGL_x11_imgui_example: examples/x11_imgui.cpp $(BUILD_DIR)/mesaGL_x11.o $(IMGUI_X11_OBJECT) libmesaGL_imgui.a $(IMGUI_GLES2_OBJECT) libmesaGL.a
	$(CXX) $(CPPFLAGS) -DIMGUI_IMPL_OPENGL_ES2 -Iports/x11 -I$(IMGUI_DIR) -I$(IMGUI_DIR)/backends $(CXXFLAGS) $< $(BUILD_DIR)/mesaGL_x11.o $(IMGUI_X11_OBJECT) $(IMGUI_GLES2_OBJECT) libmesaGL_imgui.a libmesaGL.a -lm $(X11_LIBS) -o $@

mesaGL_x11_showcase: examples/x11_showcase.cpp $(BUILD_DIR)/mesaGL_x11.o $(IMGUI_X11_OBJECT) libmesaGL_imgui.a $(IMGUI_GLES2_OBJECT) libmesaGL.a
	$(CXX) $(CPPFLAGS) -DIMGUI_IMPL_OPENGL_ES2 -Iports/x11 -I$(IMGUI_DIR) -I$(IMGUI_DIR)/backends $(CXXFLAGS) $< $(BUILD_DIR)/mesaGL_x11.o $(IMGUI_X11_OBJECT) $(IMGUI_GLES2_OBJECT) libmesaGL_imgui.a libmesaGL.a -lm $(X11_LIBS) -o $@

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

run-x11-showcase: run-showcase

mesaGL_lite_test: tests/test_render.c libmesaGL.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $< libmesaGL.a -lm -o $@

mesaGL_gl_test: tests/test_gl_compat.c libmesaGL.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $< libmesaGL.a -lm -o $@

mesaGL_rgb565_test: tests/test_rgb565.c libmesaGL.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $< libmesaGL.a -lm -o $@

mesaGL_gles2_test: tests/test_gles2.c libmesaGL.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $< libmesaGL.a -lm -o $@

mesaGL_gles2_lite_profile_test: tests/test_gles2_lite.c libmesaGL_lite.a
	$(CC) $(CPPFLAGS) -DMESAGL_GLES2_PROFILE=MESAGL_GLES2_PROFILE_LITE $(CFLAGS) $< libmesaGL_lite.a -lm -o $@

mesaGL_gles2_strict_test: tests/test_gles2_strict.c libmesaGL_gles2.a
	$(CC) $(CPPFLAGS) -DMESAGL_STRICT_GLES2=1 $(CFLAGS) $< libmesaGL_gles2.a -lm -o $@

mesaGL_context_test: tests/test_contexts.c libmesaGL.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $< libmesaGL.a -lm -o $@

mesaGL_lifecycle_test: tests/test_lifecycle.c libmesaGL.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $< libmesaGL.a -lm -o $@

mesaGL_lighting_test: tests/test_lighting.c libmesaGL.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $< libmesaGL.a -lm -o $@

mesaGL_readpixels_test: tests/test_readpixels.c libmesaGL.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $< libmesaGL.a -lm -o $@

mesaGL_polygon_offset_test: tests/test_polygon_offset.c libmesaGL.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $< libmesaGL.a -lm -o $@

mesaGL_depth_precision_test: tests/test_depth_precision.c libmesaGL.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $< libmesaGL.a -lm -o $@

mesaGL_raster_state_test: tests/test_raster_state.c libmesaGL.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $< libmesaGL.a -lm -o $@

mesaGL_texture_validation_test: tests/test_texture_validation.c libmesaGL.a
	$(CC) $(CPPFLAGS) -Isrc $(CFLAGS) $< libmesaGL.a -lm -o $@

mesaGL_fbo_validation_test: tests/test_fbo_validation.c libmesaGL.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $< libmesaGL.a -lm -o $@

mesaGL_execution_depth_test: tests/test_execution_depth.c libmesaGL.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $< libmesaGL.a -lm -o $@

shader-compile-probe: mesaGL_shader_compile_probe

mesaGL_shader_compile_probe: tests/shader_compile_probe.c libmesaGL.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $< libmesaGL.a -lm -o $@

mesaGL_blend_state_test: tests/test_blend_state.c libmesaGL.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $< libmesaGL.a -lm -o $@

mesaGL_builtin_derivatives_test: tests/test_builtin_derivatives.c libmesaGL.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $< libmesaGL.a -lm -o $@

mesaGL_programmable_test: tests/test_programmable.c libmesaGL.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $< libmesaGL.a -lm -o $@

mesaGL_glsl_vm_test: tests/test_glsl_vm.c libmesaGL.a
	$(CC) $(CPPFLAGS) -Isrc $(CFLAGS) $< libmesaGL.a -lm -o $@

mesaGL_gles2_api_test: tests/test_gles2_api.c libmesaGL.a
	$(CC) $(CPPFLAGS) -Isrc $(CFLAGS) $< libmesaGL.a -lm -o $@

mesaGL_draw_validation_test: tests/test_draw_validation.c libmesaGL.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $< libmesaGL.a -lm -o $@

mesaGL_multitexture_test: tests/test_multitexture.c libmesaGL.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $< libmesaGL.a -lm -o $@

mesaGL_stencil_separate_test: tests/test_stencil_separate.c libmesaGL.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $< libmesaGL.a -lm -o $@

mesaGL_cubemap_test: tests/test_cubemap.c libmesaGL.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $< libmesaGL.a -lm -o $@

mesaGL_implicit_lod_test: tests/test_implicit_lod.c libmesaGL.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $< libmesaGL.a -lm -o $@

mesaGL_texture_projection_test: tests/test_texture_projection.c libmesaGL.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $< libmesaGL.a -lm -o $@

mesaGL_texture_lod_switch_test: tests/test_texture_lod_switch.c libmesaGL.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $< libmesaGL.a -lm -o $@

mesaGL_preprocessor_test: tests/test_preprocessor.c libmesaGL.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $< libmesaGL.a -lm -o $@

mesaGL_linking_test: tests/test_linking.c libmesaGL.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $< libmesaGL.a -lm -o $@

mesaGL_multi_shader_test: tests/test_multi_shader.c libmesaGL.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $< libmesaGL.a -lm -o $@

mesaGL_uniform_declarators_test: tests/test_uniform_declarators.c libmesaGL.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $< libmesaGL.a -lm -o $@

mesaGL_uniform_struct_test: tests/test_uniform_struct.c libmesaGL.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $< libmesaGL.a -lm -o $@

mesaGL_integration_framebuffer_test: tests/test_integration_framebuffer.c libmesaGL.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $< libmesaGL.a -lm -o $@

mesaGL_programmable_lines_test: tests/test_programmable_lines.c libmesaGL.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $< libmesaGL.a -lm -o $@

mesaGL_programmable_points_test: tests/test_programmable_points.c libmesaGL.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $< libmesaGL.a -lm -o $@

mesaGL_clear_masks_test: tests/test_clear_masks.c libmesaGL.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $< libmesaGL.a -lm -o $@

mesaGL_config_limits_test: tests/test_config_limits.c libmesaGL.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $< libmesaGL.a -lm -o $@

mesaGL_config_limits_lite_test: tests/test_config_limits.c libmesaGL_lite.a
	$(CC) $(CPPFLAGS) -DMESAGL_GLES2_PROFILE=MESAGL_GLES2_PROFILE_LITE $(CFLAGS) $< libmesaGL_lite.a -lm -o $@

mesaGL_relink_test: tests/test_relink.c libmesaGL.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $< libmesaGL.a -lm -o $@

mesaGL_fragment_builtins_test: tests/test_fragment_builtins.c libmesaGL.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $< libmesaGL.a -lm -o $@

mesaGL_sampler_validation_test: tests/test_sampler_validation.c libmesaGL.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $< libmesaGL.a -lm -o $@

mesaGL_matrix_attribute_test: tests/test_matrix_attribute.c libmesaGL.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $< libmesaGL.a -lm -o $@

mesaGL_function_arrays_test: tests/test_function_arrays.c libmesaGL.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $< libmesaGL.a -lm -o $@

mesaGL_state_conversion_test: tests/test_state_conversion.c libmesaGL.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $< libmesaGL.a -lm -o $@

mesaGL_vertex_conversion_test: tests/test_vertex_conversion.c libmesaGL.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $< libmesaGL.a -lm -o $@

mesaGL_dither_test: tests/test_dither.c libmesaGL.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $< libmesaGL.a -lm -o $@

mesaGL_large_draws_test: tests/test_large_draws.c libmesaGL.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $< libmesaGL.a -lm -o $@

mesaGL_attachment_lifetime_test: tests/test_attachment_lifetime.c libmesaGL.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $< libmesaGL.a -lm -o $@

mesaGL_clip_planes_test: tests/test_clip_planes.c libmesaGL.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $< libmesaGL.a -lm -o $@

mesaGL_texture_centers_test: tests/test_texture_centers.c libmesaGL.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $< libmesaGL.a -lm -o $@

mesaGL_shader_semantics_test: tests/test_shader_semantics.c libmesaGL.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $< libmesaGL.a -lm -o $@

mesaGL_long_identifiers_test: tests/test_long_identifiers.c libmesaGL.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $< libmesaGL.a -lm -o $@

mesaGL_integer_builtins_test: tests/test_integer_builtins.c libmesaGL.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $< libmesaGL.a -lm -o $@

mesaGL_boolean_uniform_test: tests/test_boolean_uniform.c libmesaGL.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $< libmesaGL.a -lm -o $@

mesaGL_uniform_entrypoints_test: tests/test_uniform_entrypoints.c libmesaGL.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $< libmesaGL.a -lm -o $@

mesaGL_builtin_conformance_test: tests/test_builtin_conformance.c libmesaGL.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $< libmesaGL.a -lm -o $@

mesaGL_chained_lvalues_test: tests/test_chained_lvalues.c libmesaGL.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $< libmesaGL.a -lm -o $@

mesaGL_for_declarators_test: tests/test_for_declarators.c libmesaGL.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $< libmesaGL.a -lm -o $@

check-gles2-symbols: libmesaGL_gles2.a tests/gles2_core_symbols.txt
	@nm -g --defined-only libmesaGL_gles2.a | \
		awk 'NR == FNR { required[$$1] = 1; next } \
		     $$3 in required { delete required[$$3] } \
		     END { for (symbol in required) { \
		         print "missing GLES2 core symbol: " symbol > "/dev/stderr"; missing = 1 \
		     } exit missing }' tests/gles2_core_symbols.txt -

test: check-gles2-symbols

mesaGL_varying_matrix_lvalue_test: tests/test_varying_matrix_lvalue.c libmesaGL.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $< libmesaGL.a -lm -o $@

mesaGL_perspective_interpolation_test: tests/test_perspective_interpolation.c libmesaGL.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $< libmesaGL.a -lm -o $@

mesaGL_varying_packing_test: tests/test_varying_packing.c libmesaGL.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $< libmesaGL.a -lm -o $@

mesaGL_uniform_packing_test: tests/test_uniform_packing.c libmesaGL.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $< libmesaGL.a -lm -o $@

mesaGL_simd_test: tests/test_simd.c libmesaGL.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $< libmesaGL.a -lm -o $@

TEST_BINS := \
	mesaGL_lite_test mesaGL_gl_test mesaGL_rgb565_test mesaGL_gles2_test \
	mesaGL_gles2_lite_profile_test mesaGL_gles2_strict_test mesaGL_context_test \
	mesaGL_lifecycle_test mesaGL_lighting_test mesaGL_readpixels_test \
	mesaGL_polygon_offset_test mesaGL_depth_precision_test mesaGL_raster_state_test \
	mesaGL_texture_validation_test mesaGL_fbo_validation_test mesaGL_execution_depth_test \
	mesaGL_blend_state_test \
	mesaGL_builtin_derivatives_test mesaGL_programmable_test mesaGL_glsl_vm_test \
	mesaGL_gles2_api_test mesaGL_draw_validation_test mesaGL_multitexture_test \
	mesaGL_stencil_separate_test mesaGL_cubemap_test mesaGL_implicit_lod_test \
	mesaGL_texture_projection_test mesaGL_texture_lod_switch_test mesaGL_preprocessor_test \
	mesaGL_linking_test mesaGL_multi_shader_test mesaGL_uniform_declarators_test \
	mesaGL_uniform_struct_test mesaGL_integration_framebuffer_test \
	mesaGL_programmable_lines_test mesaGL_programmable_points_test mesaGL_clear_masks_test \
	mesaGL_config_limits_test mesaGL_config_limits_lite_test mesaGL_relink_test \
	mesaGL_fragment_builtins_test mesaGL_sampler_validation_test mesaGL_matrix_attribute_test \
	mesaGL_function_arrays_test mesaGL_state_conversion_test mesaGL_vertex_conversion_test \
	mesaGL_dither_test \
	mesaGL_large_draws_test mesaGL_attachment_lifetime_test mesaGL_clip_planes_test \
	mesaGL_texture_centers_test mesaGL_shader_semantics_test mesaGL_long_identifiers_test \
	mesaGL_integer_builtins_test \
	mesaGL_boolean_uniform_test mesaGL_uniform_entrypoints_test \
	mesaGL_builtin_conformance_test mesaGL_chained_lvalues_test \
	mesaGL_for_declarators_test mesaGL_varying_matrix_lvalue_test \
	mesaGL_perspective_interpolation_test mesaGL_varying_packing_test \
	mesaGL_uniform_packing_test mesaGL_simd_test \
	mesaGL_imgui_micro_framebuffer mesaGL_imgui_gles2_framebuffer

test: $(TEST_BINS)
	./mesaGL_lite_test
	./mesaGL_gl_test
	./mesaGL_rgb565_test
	./mesaGL_gles2_test
	./mesaGL_gles2_lite_profile_test
	./mesaGL_gles2_strict_test
	./mesaGL_context_test
	./mesaGL_lifecycle_test
	./mesaGL_lighting_test
	./mesaGL_readpixels_test
	./mesaGL_polygon_offset_test
	./mesaGL_depth_precision_test
	./mesaGL_raster_state_test
	./mesaGL_texture_validation_test
	./mesaGL_fbo_validation_test
	./mesaGL_execution_depth_test
	./mesaGL_blend_state_test
	./mesaGL_builtin_derivatives_test
	./mesaGL_programmable_test
	./mesaGL_glsl_vm_test
	./mesaGL_gles2_api_test
	./mesaGL_draw_validation_test
	./mesaGL_multitexture_test
	./mesaGL_stencil_separate_test
	./mesaGL_cubemap_test
	./mesaGL_implicit_lod_test
	./mesaGL_texture_projection_test
	./mesaGL_texture_lod_switch_test
	./mesaGL_preprocessor_test
	./mesaGL_linking_test
	./mesaGL_multi_shader_test
	./mesaGL_uniform_declarators_test
	./mesaGL_uniform_struct_test
	./mesaGL_integration_framebuffer_test
	./mesaGL_programmable_lines_test
	./mesaGL_programmable_points_test
	./mesaGL_clear_masks_test
	./mesaGL_config_limits_test
	./mesaGL_config_limits_lite_test
	./mesaGL_relink_test
	./mesaGL_fragment_builtins_test
	./mesaGL_sampler_validation_test
	./mesaGL_matrix_attribute_test
	./mesaGL_function_arrays_test
	./mesaGL_state_conversion_test
	./mesaGL_vertex_conversion_test
	./mesaGL_dither_test
	./mesaGL_large_draws_test
	./mesaGL_attachment_lifetime_test
	./mesaGL_clip_planes_test
	./mesaGL_texture_centers_test
	./mesaGL_shader_semantics_test
	./mesaGL_long_identifiers_test
	./mesaGL_integer_builtins_test
	./mesaGL_boolean_uniform_test
	./mesaGL_uniform_entrypoints_test
	./mesaGL_builtin_conformance_test
	./mesaGL_chained_lvalues_test
	./mesaGL_for_declarators_test
	./mesaGL_varying_matrix_lvalue_test
	./mesaGL_perspective_interpolation_test
	./mesaGL_varying_packing_test
	./mesaGL_uniform_packing_test
	./mesaGL_simd_test
	./mesaGL_imgui_micro_framebuffer
	./mesaGL_imgui_gles2_framebuffer

clean:
	$(RM) -r $(BUILD_DIR) $(LITE_BUILD_DIR) $(GLES2_BUILD_DIR)
	$(RM) libmesaGL.a libmesaGL_lite.a libmesaGL_gles2.a libmesaGL_imgui.a
	$(RM) mesaGL_lite_test mesaGL_gl_test mesaGL_rgb565_test mesaGL_gles2_test
	$(RM) mesaGL_gles2_lite_profile_test
	$(RM) mesaGL_gles2_strict_test
	$(RM) mesaGL_context_test
	$(RM) mesaGL_lifecycle_test
	$(RM) mesaGL_lighting_test
	$(RM) mesaGL_readpixels_test
	$(RM) mesaGL_polygon_offset_test
	$(RM) mesaGL_depth_precision_test
	$(RM) mesaGL_raster_state_test
	$(RM) mesaGL_texture_validation_test
	$(RM) mesaGL_fbo_validation_test
	$(RM) mesaGL_execution_depth_test
	$(RM) mesaGL_shader_compile_probe
	$(RM) mesaGL_simd_test
	$(RM) mesaGL_blend_state_test
	$(RM) mesaGL_builtin_derivatives_test
	$(RM) mesaGL_programmable_test
	$(RM) mesaGL_glsl_vm_test
	$(RM) mesaGL_gles2_api_test
	$(RM) mesaGL_draw_validation_test
	$(RM) mesaGL_multitexture_test
	$(RM) mesaGL_stencil_separate_test
	$(RM) mesaGL_cubemap_test
	$(RM) mesaGL_implicit_lod_test
	$(RM) mesaGL_texture_projection_test
	$(RM) mesaGL_texture_lod_switch_test
	$(RM) mesaGL_preprocessor_test
	$(RM) mesaGL_linking_test
	$(RM) mesaGL_multi_shader_test
	$(RM) mesaGL_uniform_declarators_test
	$(RM) mesaGL_uniform_struct_test
	$(RM) mesaGL_integration_framebuffer_test
	$(RM) mesaGL_programmable_lines_test
	$(RM) mesaGL_programmable_points_test
	$(RM) mesaGL_clear_masks_test
	$(RM) mesaGL_config_limits_test mesaGL_config_limits_lite_test
	$(RM) mesaGL_relink_test
	$(RM) mesaGL_fragment_builtins_test
	$(RM) mesaGL_sampler_validation_test
	$(RM) mesaGL_matrix_attribute_test
	$(RM) mesaGL_attribute_arrays_test
	$(RM) mesaGL_function_arrays_test
	$(RM) mesaGL_state_conversion_test
	$(RM) mesaGL_dither_test
	$(RM) mesaGL_large_draws_test
	$(RM) mesaGL_attachment_lifetime_test
	$(RM) mesaGL_clip_planes_test
	$(RM) mesaGL_texture_centers_test
	$(RM) mesaGL_shader_semantics_test
	$(RM) mesaGL_long_identifiers_test
	$(RM) mesaGL_integer_builtins_test
	$(RM) mesaGL_boolean_uniform_test
	$(RM) mesaGL_uniform_entrypoints_test
	$(RM) mesaGL_builtin_conformance_test
	$(RM) mesaGL_chained_lvalues_test
	$(RM) mesaGL_for_declarators_test
	$(RM) mesaGL_varying_matrix_lvalue_test
	$(RM) mesaGL_perspective_interpolation_test
	$(RM) mesaGL_varying_packing_test
	$(RM) mesaGL_uniform_packing_test
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
