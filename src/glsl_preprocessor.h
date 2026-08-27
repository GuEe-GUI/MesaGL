#ifndef MESAGL_GLSL_PREPROCESSOR_H
#define MESAGL_GLSL_PREPROCESSOR_H

#include <stddef.h>

#define MESAGL_GLSL_EXTENSION_STANDARD_DERIVATIVES 0x1u
#define MESAGL_GLSL_PRAGMA_INVARIANT_ALL 0x2u
#define MESAGL_GLSL_EXTENSION_STANDARD_DERIVATIVES_WARN 0x4u

char *mesaGLSLPreprocess(const char *source, unsigned int *enabled_extensions, char *log,
                         size_t log_size);
char *mesaGLSLPreprocessSource(const char *source, size_t source_length,
                               const size_t *source_boundaries,
                               int source_boundary_count,
                               unsigned int *enabled_extensions, char *log,
                               size_t log_size);
int mesaGLSLReservedES100Identifier(const char *name, size_t length);

#endif
