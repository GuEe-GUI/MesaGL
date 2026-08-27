# Include this fragment from an RT-Thread application Makefile while its
# working directory is the mesaGL source root.
MESAGL_RTTHREAD_DM_SOURCES += src/ntgl.c src/gl_compat.c src/glsl_vm.c
MESAGL_RTTHREAD_DM_SOURCES += src/glsl_preprocessor.c src/gles2.c src/port.c
MESAGL_RTTHREAD_DM_SOURCES += src/simd.c
MESAGL_RTTHREAD_DM_SOURCES += ports/rtthread_dm/mesaGL_rtthread_dm.c
MESAGL_RTTHREAD_DM_SOURCES += examples/rtthread_dm.c
MESAGL_RTTHREAD_DM_CPPFLAGS += -Iinclude -Iports/rtthread_dm

MESAGL_RTTHREAD_DM_IMGUI_SOURCES += ports/rtthread_dm/mesaGL_imgui_rtthread_dm.cpp
MESAGL_RTTHREAD_DM_IMGUI_SOURCES += examples/rtthread_dm_imgui.cpp
MESAGL_RTTHREAD_DM_IMGUI_SOURCES += examples/imgui/imgui.cpp
MESAGL_RTTHREAD_DM_IMGUI_SOURCES += examples/imgui/imgui_draw.cpp
MESAGL_RTTHREAD_DM_IMGUI_SOURCES += examples/imgui/imgui_tables.cpp
MESAGL_RTTHREAD_DM_IMGUI_SOURCES += examples/imgui/imgui_widgets.cpp
MESAGL_RTTHREAD_DM_IMGUI_SOURCES += examples/imgui/imgui_demo.cpp
MESAGL_RTTHREAD_DM_IMGUI_SOURCES += examples/imgui/backends/imgui_impl_opengl3.cpp
MESAGL_RTTHREAD_DM_IMGUI_CPPFLAGS += -DIMGUI_IMPL_OPENGL_ES2
MESAGL_RTTHREAD_DM_IMGUI_CPPFLAGS += -Iexamples/imgui -Iexamples/imgui/backends
