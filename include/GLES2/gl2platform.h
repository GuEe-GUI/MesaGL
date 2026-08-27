#ifndef MESAGL_GLES2_GL2PLATFORM_H
#define MESAGL_GLES2_GL2PLATFORM_H

/* Override these macros from the platform build when an ABI annotation is needed. */
#ifndef GL_APICALL
#define GL_APICALL extern
#endif

#ifndef GL_APIENTRY
#define GL_APIENTRY
#endif

#ifndef GL_APIENTRYP
#define GL_APIENTRYP GL_APIENTRY *
#endif

#endif
