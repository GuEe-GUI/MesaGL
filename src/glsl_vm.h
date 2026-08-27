#ifndef MESAGL_GLSL_VM_H
#define MESAGL_GLSL_VM_H

#include "mesaGL/config.h"

#include <stddef.h>

typedef struct MesaGLSLValue MesaGLSLValue;

typedef enum MesaGLSLType {
    MESAGL_GLSL_TYPE_UNKNOWN,
    MESAGL_GLSL_TYPE_FLOAT,
    MESAGL_GLSL_TYPE_INT,
    MESAGL_GLSL_TYPE_BOOL,
    MESAGL_GLSL_TYPE_STRUCT,
    MESAGL_GLSL_TYPE_SAMPLER2D,
    MESAGL_GLSL_TYPE_SAMPLERCUBE
} MesaGLSLType;

struct MesaGLSLValue {
    float data[16];
    int rows;
    int columns;
    const MesaGLSLValue *array;
    int array_size;
    char (*member_names)[MESAGL_MAX_SHADER_LVALUE_PATH_LENGTH];
    const MesaGLSLValue *members;
    int member_count;
    float dfdx[16];
    float dfdy[16];
    int has_derivatives;
    MesaGLSLType type;
    const char *struct_type_name;
    size_t struct_type_length;
};

typedef int (*MesaGLSLLookupFn)(void *user, const char *name, size_t length,
                                MesaGLSLValue *value);
typedef int (*MesaGLSLCallFn)(void *user, const char *name, size_t length,
                              const MesaGLSLValue *arguments, int argument_count,
                              MesaGLSLValue *value);
typedef int (*MesaGLSLAssignFn)(void *user, const char *name, size_t length, const char *swizzle,
                                size_t swizzle_length, int array_index,
                                const MesaGLSLValue *value);

int mesaGLSLExpression(const char *source, const char *end, MesaGLSLLookupFn lookup,
                       MesaGLSLCallFn call, void *user, MesaGLSLValue *result,
                       const char **error_at);
int mesaGLSLExecute(const char *source, MesaGLSLLookupFn lookup, MesaGLSLCallFn call,
                    MesaGLSLAssignFn assign, void *user, int *discarded, const char **error_at);
int mesaGLSLExecuteProgram(const char *program_source, const char *body, MesaGLSLLookupFn lookup,
                           MesaGLSLCallFn call, MesaGLSLAssignFn assign, void *user,
                           int *discarded, const char **error_at);

#endif
