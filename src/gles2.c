#include "GLES2/gl2.h"
#include "gles2_internal.h"
#include "glsl_preprocessor.h"
#include "glsl_vm.h"
#include "mesaGL/config.h"
#include "mesaGL/ntgl.h"

#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_SHADERS MESAGL_MAX_SHADERS
#define MAX_PROGRAMS MESAGL_MAX_PROGRAMS
#define MAX_BUFFERS MESAGL_MAX_BUFFERS
#define MAX_ATTRIBUTES MESAGL_MAX_VERTEX_ATTRIBS
#define MAX_BINDINGS (MAX_ATTRIBUTES * 2)
#define LOG_SIZE 128
#define MAX_CONTEXTS MESAGL_MAX_CONTEXTS
#define LINK_TYPE_CAPACITY MESAGL_MAX_SHADER_LVALUE_PATH_LENGTH

typedef struct Shader {
    GLuint name;
    GLenum type;
    char *source;
    size_t source_length;
    size_t *source_boundaries;
    int source_boundary_count;
    char *compiled_source;
    int compiled;
    int invariant_all;
    int delete_pending;
    char log[LOG_SIZE];
} Shader;

typedef struct Binding {
    char name[MESAGL_MAX_SHADER_IDENTIFIER_LENGTH];
    GLuint index;
    GLuint requested_index;
    GLenum type;
    int requested;
    int active;
} Binding;

typedef enum UniformType {
    UNIFORM_OTHER,
    UNIFORM_FLOAT,
    UNIFORM_VEC2,
    UNIFORM_VEC3,
    UNIFORM_VEC4,
    UNIFORM_INT,
    UNIFORM_IVEC2,
    UNIFORM_IVEC3,
    UNIFORM_IVEC4,
    UNIFORM_BOOL,
    UNIFORM_BVEC2,
    UNIFORM_BVEC3,
    UNIFORM_BVEC4,
    UNIFORM_MAT2,
    UNIFORM_MAT3,
    UNIFORM_MAT4,
    UNIFORM_SAMPLER2D,
    UNIFORM_SAMPLERCUBE
} UniformType;

typedef struct Uniform {
    char name[MESAGL_MAX_SHADER_IDENTIFIER_LENGTH];
    char aggregate_name[MESAGL_MAX_SHADER_IDENTIFIER_LENGTH];
    char aggregate_type[MESAGL_MAX_SHADER_IDENTIFIER_LENGTH];
    char member_name[MESAGL_MAX_SHADER_IDENTIFIER_LENGTH];
    GLint aggregate_size;
    GLint member_size;
    GLint aggregate_element;
    uint64_t aggregate_signature;
    GLint location;
    GLint size;
    int array_declared;
    UniformType type;
    float value[16];
    GLint integer[4];
    float *array_value;
    GLint *array_integer;
} Uniform;

static int shader_keyword_at(const char *source, const char *cursor,
                             size_t length);
static int shader_identifier_character(char character);

typedef struct Varying {
    char name[MESAGL_MAX_SHADER_IDENTIFIER_LENGTH];
    GLenum type;
    int size;
    int slot;
    int invariant;
    int active;
} Varying;

#if MESAGL_GLES2_PROFILE == MESAGL_GLES2_PROFILE_FULL
static int varying_declaration_is_invariant(const char *source,
                                            const char *varying)
{
    const char *start = varying;
    const char *cursor;

    while (start > source && start[-1] != ';' && start[-1] != '{' &&
           start[-1] != '}')
        --start;
    for (cursor = start; cursor < varying; ++cursor)
        if ((size_t)(varying - cursor) >= 9 &&
            !strncmp(cursor, "invariant", 9) &&
            shader_keyword_at(source, cursor, 9))
            return 1;
    return 0;
}
#endif

typedef struct Program {
    GLuint name;
    GLuint attached[MAX_SHADERS];
    int attached_count;
    int linked;
    int executable;
    int validated;
    int delete_pending;
    int uses_texture;
    GLint sampler;
    Binding bindings[MAX_BINDINGS];
    int binding_count;
    Uniform uniforms[MESAGL_MAX_SHADER_UNIFORMS];
    int uniform_count;
    GLint fragment_output_uniform;
    char vertex_position[256];
    char fragment_color[256];
    char fragment_discard[256];
    char *vertex_body;
    char *fragment_body;
    char *linked_vertex_source;
    char *linked_fragment_source;
    Varying varyings[MESAGL_MAX_VARYING_DECLARATIONS];
    int varying_count;
    int varying_slot_count;
    int imgui_fast_path;
    int imgui_position;
    int imgui_uv;
    int imgui_color;
    int imgui_uv_varying;
    int imgui_color_varying;
    GLint imgui_projection;
    GLint imgui_texture;
    char log[LOG_SIZE];
} Program;

#define MAX_LINK_FUNCTIONS 64
#define MAX_LINK_PARAMETERS 16

typedef struct LinkFunction {
    char name[MESAGL_MAX_SHADER_IDENTIFIER_LENGTH];
    char return_type[MESAGL_MAX_SHADER_IDENTIFIER_LENGTH];
    unsigned char return_precision;
    char parameters[MAX_LINK_PARAMETERS][LINK_TYPE_CAPACITY];
    char parameter_names[MAX_LINK_PARAMETERS]
                        [MESAGL_MAX_SHADER_IDENTIFIER_LENGTH];
    unsigned char parameter_modes[MAX_LINK_PARAMETERS];
    unsigned char parameter_consts[MAX_LINK_PARAMETERS];
    unsigned char parameter_precisions[MAX_LINK_PARAMETERS];
    int parameter_count;
    int definition;
    const char *declaration_start;
    const char *body_start;
    const char *body_end;
} LinkFunction;

typedef struct ShaderReachability {
    const char *source;
    LinkFunction *functions;
    unsigned char reachable[MAX_LINK_FUNCTIONS];
    int function_count;
} ShaderReachability;

#if MESAGL_GLES2_PROFILE == MESAGL_GLES2_PROFILE_FULL
static int validate_function_prototypes(Program *program, const char *source,
                                        int fragment_stage, int require_definitions);
static int parse_link_parameters(const char *source, const char *open,
                                 const char *close, LinkFunction *function,
                                 int fragment_stage);
static int copy_link_token(char *destination, size_t size,
                           const char *start, const char *end);
static int collect_link_argument_types(
    const char *source, const char *open, const char *close,
    const LinkFunction *functions, int function_count,
    char types[MAX_LINK_PARAMETERS][LINK_TYPE_CAPACITY], int known[16]);
static int link_declaration_visible(const char *source,
                                    const char *declaration,
                                    const char *limit);
static int link_identifier_is_declarator(const char *source,
                                         const char *name);
static int link_identifier_is_inline_struct_declarator(const char *source,
                                                       const char *name);
#endif

typedef struct Buffer {
    GLuint name;
    int created;
    unsigned char *data;
    size_t size;
    GLenum usage;
} Buffer;

typedef struct AttribState {
    GLint enabled;
    GLint size;
    GLenum type;
    GLboolean normalized;
    GLsizei stride;
    const void *pointer;
    GLuint buffer;
    GLfloat current[4];
} AttribState;

typedef struct GLESContextState {
    NTGLcontext *context;
    Shader shaders[MAX_SHADERS];
    Program programs[MAX_PROGRAMS];
    Buffer buffers[MAX_BUFFERS];
    GLuint next_shader_program, next_buffer;
    GLuint current_program, array_buffer, element_buffer;
    GLenum active_texture;
    AttribState attributes[MAX_ATTRIBUTES];
} GLESContextState;

static GLESContextState context_states[MAX_CONTEXTS];

static GLESContextState *current_state(void)
{
    NTGLcontext *context = ntglGetCurrent();
    int i, free_slot = -1;
    for (i = 0; i < MAX_CONTEXTS; ++i) {
        if (context_states[i].context == context)
            return &context_states[i];
        if (!context_states[i].context && free_slot < 0)
            free_slot = i;
    }
    if (free_slot < 0)
        return &context_states[0];
    context_states[free_slot].context = context;
    context_states[free_slot].next_shader_program = 1;
    context_states[free_slot].next_buffer = 1;
    context_states[free_slot].active_texture = GL_TEXTURE0;
    for (i = 0; i < MAX_ATTRIBUTES; ++i) {
        context_states[free_slot].attributes[i].size = 4;
        context_states[free_slot].attributes[i].type = GL_FLOAT;
        context_states[free_slot].attributes[i].current[3] = 1.0f;
    }
    return &context_states[free_slot];
}

void mesaGLGLES2ReleaseCurrentContext(void)
{
    NTGLcontext *context = ntglGetCurrent();
    int i, slot;
    for (slot = 0; slot < MAX_CONTEXTS; ++slot)
        if (context_states[slot].context == context) {
            for (i = 0; i < MAX_SHADERS; ++i) {
                ntglFree(context_states[slot].shaders[i].source);
                ntglFree(context_states[slot].shaders[i].compiled_source);
            }
            for (i = 0; i < MAX_PROGRAMS; ++i) {
                int uniform;

                for (uniform = 0; uniform < context_states[slot].programs[i].uniform_count;
                     ++uniform) {
                    ntglFree(context_states[slot].programs[i].uniforms[uniform].array_value);
                    ntglFree(context_states[slot].programs[i].uniforms[uniform].array_integer);
                }
                ntglFree(context_states[slot].programs[i].linked_vertex_source);
                ntglFree(context_states[slot].programs[i].linked_fragment_source);
            }
            for (i = 0; i < MAX_BUFFERS; ++i)
                ntglFree(context_states[slot].buffers[i].data);
            memset(&context_states[slot], 0, sizeof(context_states[slot]));
            return;
        }
}

#define shaders (current_state()->shaders)
#define programs (current_state()->programs)
#define buffers (current_state()->buffers)
#define next_shader_program (current_state()->next_shader_program)
#define next_buffer (current_state()->next_buffer)
#define current_program (current_state()->current_program)
#define array_buffer (current_state()->array_buffer)
#define element_buffer (current_state()->element_buffer)
#define active_texture (current_state()->active_texture)
#define attributes (current_state()->attributes)

static Shader *find_shader(GLuint name)
{
    int i;
    if (!name)
        return NULL;
    for (i = 0; i < MAX_SHADERS; ++i)
        if (shaders[i].name == name)
            return &shaders[i];
    return NULL;
}

static Program *find_program(GLuint name)
{
    int i;
    if (!name)
        return NULL;
    for (i = 0; i < MAX_PROGRAMS; ++i)
        if (programs[i].name == name)
            return &programs[i];
    return NULL;
}

static void shader_name_error(GLuint name)
{
    mesaGLSetError(find_program(name) ? GL_INVALID_OPERATION : GL_INVALID_VALUE);
}

static void program_name_error(GLuint name)
{
    mesaGLSetError(find_shader(name) ? GL_INVALID_OPERATION : GL_INVALID_VALUE);
}

static void free_program_uniforms(Program *program)
{
    int i;

    if (!program)
        return;
    for (i = 0; i < program->uniform_count; ++i) {
        ntglFree(program->uniforms[i].array_value);
        ntglFree(program->uniforms[i].array_integer);
        program->uniforms[i].array_value = NULL;
        program->uniforms[i].array_integer = NULL;
    }
    program->uniform_count = 0;
}

static int shader_is_attached(GLuint name)
{
    int i;
    int attachment;

    for (i = 0; i < MAX_PROGRAMS; ++i)
        if (programs[i].name)
            for (attachment = 0; attachment < programs[i].attached_count; ++attachment)
                if (programs[i].attached[attachment] == name)
                    return 1;
    return 0;
}

static void destroy_shader(Shader *shader)
{
    if (!shader)
        return;
    ntglFree(shader->source);
    ntglFree(shader->source_boundaries);
    ntglFree(shader->compiled_source);
    memset(shader, 0, sizeof(*shader));
}

static void release_deleted_shader(GLuint name)
{
    Shader *shader = find_shader(name);

    if (shader && shader->delete_pending && !shader_is_attached(name))
        destroy_shader(shader);
}

static Buffer *find_buffer(GLuint name)
{
    int i;
    if (!name)
        return NULL;
    for (i = 0; i < MAX_BUFFERS; ++i)
        if (buffers[i].name == name)
            return &buffers[i];
    return NULL;
}

static Buffer *get_or_create_buffer(GLuint name)
{
    Buffer *buffer = find_buffer(name);
    int index;

    if (buffer || !name)
        return buffer;
    for (index = 0; index < MAX_BUFFERS; ++index)
        if (!buffers[index].name) {
            buffers[index].name = name;
            return &buffers[index];
        }
    mesaGLSetError(GL_OUT_OF_MEMORY);
    return NULL;
}

static void copy_log(const char *source, GLsizei size, GLsizei *length, GLchar *destination)
{
    size_t count = source ? strlen(source) : 0;

    if (size > 0 && destination) {
        if (count >= (size_t)size)
            count = (size_t)size - 1;
        memcpy(destination, source, count);
        destination[count] = '\0';
    } else
        count = 0;
    if (length)
        *length = (GLsizei)count;
}

static void copy_array_name(const char *name, GLsizei size, GLsizei *length,
                            GLchar *destination)
{
    static const char suffix[] = "[0]";
    size_t name_length = strlen(name);
    size_t total_length = name_length + sizeof(suffix) - 1;
    size_t count = 0;

    if (size > 0 && destination) {
        count = total_length;
        if (count >= (size_t)size)
            count = (size_t)size - 1;
        if (count <= name_length) {
            memcpy(destination, name, count);
        } else {
            memcpy(destination, name, name_length);
            memcpy(destination + name_length, suffix, count - name_length);
        }
        destination[count] = '\0';
    }
    if (length)
        *length = (GLsizei)count;
}

#if MESAGL_GLES2_PROFILE == MESAGL_GLES2_PROFILE_LITE
static int source_has(const Shader *shader, const char *text)
{
    const char *source = shader && shader->compiled_source ? shader->compiled_source
                                                           : shader ? shader->source : NULL;

    return source && strstr(source, text) != NULL;
}
#endif

static int source_delimiters_balanced(const char *source)
{
    size_t source_length;
    char *stack;
    size_t depth = 0;
    int balanced = 1;

    if (!source)
        return 1;
    source_length = strlen(source);
    stack = (char *)ntglAlloc(source_length ? source_length : 1);
    if (!stack)
        return 0;

    while (*source) {
        if (source[0] == '/' && source[1] == '/') {
            source += 2;
            while (*source && *source != '\n')
                ++source;
            continue;
        }
        if (source[0] == '/' && source[1] == '*') {
            source += 2;
            while (*source && !(source[0] == '*' && source[1] == '/'))
                ++source;
            if (!*source) {
                balanced = 0;
                break;
            }
            source += 2;
            continue;
        }
        if (*source == '(' || *source == '[' || *source == '{') {
            stack[depth++] = *source;
        } else if (*source == ')' || *source == ']' || *source == '}') {
            char expected = *source == ')' ? '(' : *source == ']' ? '[' : '{';

            if (!depth || stack[--depth] != expected) {
                balanced = 0;
                break;
            }
        }
        ++source;
    }
    balanced = balanced && depth == 0;
    ntglFree(stack);
    return balanced;
}

static void extract_assignment(const char *source, const char *target, char *output, size_t size);

static const char *skip_precision(const char *cursor)
{
    const char *start = cursor;

    while ((*cursor >= 'a' && *cursor <= 'z') || (*cursor >= 'A' && *cursor <= 'Z') ||
           (*cursor >= '0' && *cursor <= '9') || *cursor == '_')
        ++cursor;
    if ((cursor - start == 4 && !strncmp(start, "lowp", 4)) ||
        (cursor - start == 7 && !strncmp(start, "mediump", 7)) ||
        (cursor - start == 5 && !strncmp(start, "highp", 5))) {
        while (*cursor == ' ' || *cursor == '\t' || *cursor == '\r' || *cursor == '\n')
            ++cursor;
        return cursor;
    }
    return start;
}

static const char *skip_shader_space(const char *cursor)
{
    while (*cursor == ' ' || *cursor == '\t' || *cursor == '\r' || *cursor == '\n')
        ++cursor;
    return cursor;
}

static int shader_keyword_at(const char *source, const char *cursor, size_t length)
{
    const char *scan = source;
    char before = cursor == source ? '\0' : cursor[-1];
    char after = cursor[length];
    int before_identifier =
        (before >= 'a' && before <= 'z') || (before >= 'A' && before <= 'Z') ||
        (before >= '0' && before <= '9') || before == '_';
    int after_identifier =
        (after >= 'a' && after <= 'z') || (after >= 'A' && after <= 'Z') ||
        (after >= '0' && after <= '9') || after == '_';

    while (scan < cursor) {
        if (scan[0] == '/' && scan[1] == '/') {
            const char *comment = scan;

            scan += 2;
            while (scan < cursor && *scan != '\n')
                ++scan;
            if (cursor >= comment && cursor <= scan)
                return 0;
        } else if (scan[0] == '/' && scan[1] == '*') {
            const char *comment = scan;

            scan += 2;
            while (scan < cursor && !(scan[0] == '*' && scan[1] == '/'))
                ++scan;
            if (cursor >= comment && cursor <= scan + 2)
                return 0;
            if (scan < cursor)
                scan += 2;
        } else
            ++scan;
    }

    return !before_identifier && !after_identifier;
}

static int shader_has_call(const char *source, const char *name)
{
    const char *cursor = source;
    size_t length = strlen(name);

    while ((cursor = strstr(cursor, name)) != NULL) {
        const char *after = cursor + length;

        while (*after == ' ' || *after == '\t' || *after == '\r' || *after == '\n')
            ++after;
        if (*after == '(' && shader_keyword_at(source, cursor, length))
            return 1;
        cursor += length;
    }
    return 0;
}

static int shader_has_identifier(const char *source, const char *name)
{
    const char *cursor = source;
    size_t length = strlen(name);

    while (*cursor) {
        if (cursor[0] == '/' && cursor[1] == '/') {
            cursor += 2;
            while (*cursor && *cursor != '\n')
                ++cursor;
            continue;
        }
        if (cursor[0] == '/' && cursor[1] == '*') {
            cursor += 2;
            while (*cursor && !(cursor[0] == '*' && cursor[1] == '/'))
                ++cursor;
            if (*cursor)
                cursor += 2;
            continue;
        }
        if (!strncmp(cursor, name, length) &&
            shader_keyword_at(source, cursor, length))
            return 1;
        ++cursor;
    }
    return 0;
}

static int core_gl_identifier(const char *name, size_t length)
{
    static const char *const names[] = {
        "gl_Position",
        "gl_PointSize",
        "gl_FragCoord",
        "gl_FrontFacing",
        "gl_PointCoord",
        "gl_FragColor",
        "gl_FragData",
        "gl_DepthRange",
        "gl_DepthRangeParameters",
        "gl_MaxVertexAttribs",
        "gl_MaxVertexUniformVectors",
        "gl_MaxVaryingVectors",
        "gl_MaxVertexTextureImageUnits",
        "gl_MaxCombinedTextureImageUnits",
        "gl_MaxTextureImageUnits",
        "gl_MaxFragmentUniformVectors",
        "gl_MaxDrawBuffers",
    };
    size_t index;

    for (index = 0; index < sizeof(names) / sizeof(names[0]); ++index)
        if (strlen(names[index]) == length &&
            !strncmp(name, names[index], length))
            return 1;
    return 0;
}

static int validate_reserved_gl_identifiers(const char *source, char *log)
{
    const char *cursor = source;

    while (*cursor) {
        const char *end;

        if (cursor[0] == '/' && cursor[1] == '/') {
            cursor += 2;
            while (*cursor && *cursor != '\n')
                ++cursor;
            continue;
        }
        if (cursor[0] == '/' && cursor[1] == '*') {
            cursor += 2;
            while (*cursor && !(cursor[0] == '*' && cursor[1] == '/'))
                ++cursor;
            if (*cursor)
                cursor += 2;
            continue;
        }
        if (!(cursor[0] == 'g' && cursor[1] == 'l' && cursor[2] == '_' &&
              (cursor == source || !shader_identifier_character(cursor[-1])))) {
            ++cursor;
            continue;
        }
        end = cursor + 3;
        while (shader_identifier_character(*end))
            ++end;
        if (!core_gl_identifier(cursor, (size_t)(end - cursor))) {
            snprintf(log, LOG_SIZE, "reserved gl_ identifier: %.*s",
                     (int)(end - cursor), cursor);
            return 0;
        }
        if (*skip_shader_space(end) == '(') {
            snprintf(log, LOG_SIZE,
                     "built-in gl_ identifier cannot be used as a function: %.*s",
                     (int)(end - cursor), cursor);
            return 0;
        }
        cursor = end;
    }
    return 1;
}

static int validate_reserved_es100_words(const char *source, char *log)
{
    const char *cursor = source;

    while (*cursor) {
        const char *end;

        if (!((*cursor >= 'a' && *cursor <= 'z') ||
              (*cursor >= 'A' && *cursor <= 'Z') || *cursor == '_')) {
            ++cursor;
            continue;
        }
        end = cursor + 1;
        while (shader_identifier_character(*end))
            ++end;
        if (mesaGLSLReservedES100Identifier(cursor,
                                            (size_t)(end - cursor))) {
            snprintf(log, LOG_SIZE, "reserved GLSL ES 1.00 word: %.*s",
                     (int)(end - cursor), cursor);
            return 0;
        }
        cursor = end;
    }
    return 1;
}

static int validate_invariant_usage(GLenum stage, const char *source,
                                    char *log)
{
    const char *cursor = source;
    int depth = 0;

    while (*cursor) {
        const char *after;

        if (cursor[0] == '/' && cursor[1] == '/') {
            cursor += 2;
            while (*cursor && *cursor != '\n')
                ++cursor;
            continue;
        }
        if (cursor[0] == '/' && cursor[1] == '*') {
            cursor += 2;
            while (*cursor && !(cursor[0] == '*' && cursor[1] == '/'))
                ++cursor;
            if (*cursor)
                cursor += 2;
            continue;
        }
        if (*cursor == '{') {
            ++depth;
            ++cursor;
            continue;
        }
        if (*cursor == '}') {
            if (depth)
                --depth;
            ++cursor;
            continue;
        }
        if (strncmp(cursor, "invariant", 9) ||
            !shader_keyword_at(source, cursor, 9)) {
            ++cursor;
            continue;
        }
        if (depth) {
            strcpy(log, "invariant declarations must have global scope");
            return 0;
        }
        after = skip_shader_space(cursor + 9);
        if (!strncmp(after, "varying", 7) &&
            shader_keyword_at(source, after, 7)) {
            cursor = after + 7;
            continue;
        }
        for (;;) {
            const char *name = after;
            size_t length;
            int allowed;

            if (!((*name >= 'a' && *name <= 'z') ||
                  (*name >= 'A' && *name <= 'Z') || *name == '_')) {
                strcpy(log, "invalid invariant declaration");
                return 0;
            }
            ++after;
            while ((*after >= 'a' && *after <= 'z') ||
                   (*after >= 'A' && *after <= 'Z') ||
                   (*after >= '0' && *after <= '9') || *after == '_')
                ++after;
            length = (size_t)(after - name);
            allowed = stage == GL_VERTEX_SHADER
                          ? ((length == 11 && !strncmp(name, "gl_Position", length)) ||
                             (length == 12 && !strncmp(name, "gl_PointSize", length)))
                          : ((length == 12 && !strncmp(name, "gl_FragCoord", length)) ||
                             (length == 13 && !strncmp(name, "gl_PointCoord", length)) ||
                             (length == 12 && !strncmp(name, "gl_FragColor", length)) ||
                             (length == 11 && !strncmp(name, "gl_FragData", length)));
            if (!allowed) {
                strcpy(log,
                       "invariant qualifier is only valid on shader interfaces");
                return 0;
            }
            after = skip_shader_space(after);
            if (*after == ';') {
                cursor = after + 1;
                break;
            }
            if (*after != ',') {
                strcpy(log, "invalid invariant redeclaration");
                return 0;
            }
            after = skip_shader_space(after + 1);
        }
    }
    return 1;
}

static int shader_position_in_structure(const char *source,
                                        const char *position);

static int validate_precision_statements(const char *source, char *log)
{
    const char *cursor = source;

    while (*cursor) {
        const char *qualifier;
        const char *type;
        const char *after;
        size_t qualifier_length;
        size_t type_length;
        int qualifier_valid;
        int type_valid;

        if (cursor[0] == '/' && cursor[1] == '/') {
            cursor += 2;
            while (*cursor && *cursor != '\n')
                ++cursor;
            continue;
        }
        if (cursor[0] == '/' && cursor[1] == '*') {
            cursor += 2;
            while (*cursor && !(cursor[0] == '*' && cursor[1] == '/'))
                ++cursor;
            if (*cursor)
                cursor += 2;
            continue;
        }
        if (strncmp(cursor, "precision", 9) ||
            !shader_keyword_at(source, cursor, 9)) {
            ++cursor;
            continue;
        }
        if (shader_position_in_structure(source, cursor)) {
            strcpy(log, "default precision declaration is invalid in a structure");
            return 0;
        }
        qualifier = skip_shader_space(cursor + 9);
        after = qualifier;
        while (shader_identifier_character(*after))
            ++after;
        qualifier_length = (size_t)(after - qualifier);
        qualifier_valid =
            (qualifier_length == 4 && !strncmp(qualifier, "lowp", 4)) ||
            (qualifier_length == 7 && !strncmp(qualifier, "mediump", 7)) ||
            (qualifier_length == 5 && !strncmp(qualifier, "highp", 5));
        type = skip_shader_space(after);
        after = type;
        while (shader_identifier_character(*after))
            ++after;
        type_length = (size_t)(after - type);
        type_valid =
            (type_length == 5 && !strncmp(type, "float", 5)) ||
            (type_length == 3 && !strncmp(type, "int", 3)) ||
            (type_length == 9 && !strncmp(type, "sampler2D", 9)) ||
            (type_length == 11 && !strncmp(type, "samplerCube", 11));
        after = skip_shader_space(after);
        if (!qualifier_valid || !type_valid || *after != ';') {
            strcpy(log, "invalid default precision declaration");
            return 0;
        }
        cursor = after + 1;
    }
    return 1;
}

static int validate_storage_qualifier_scope(const char *source, char *log)
{
    const char *cursor = source;
    int brace_depth = 0;
    int parenthesis_depth = 0;

    while (*cursor) {
        size_t length = 0;

        if (*cursor == '{')
            ++brace_depth;
        else if (*cursor == '}' && brace_depth)
            --brace_depth;
        else if (*cursor == '(')
            ++parenthesis_depth;
        else if (*cursor == ')' && parenthesis_depth)
            --parenthesis_depth;
        if (!strncmp(cursor, "uniform", 7))
            length = 7;
        else if (!strncmp(cursor, "varying", 7))
            length = 7;
        else if (!strncmp(cursor, "attribute", 9))
            length = 9;
        if (length && shader_keyword_at(source, cursor, length) &&
            (brace_depth || parenthesis_depth)) {
            strcpy(log,
                   "attribute, uniform, and varying declarations require global scope");
            return 0;
        }
        cursor += length ? length : 1;
    }
    return 1;
}

static int range_has_keyword(const char *start, const char *end,
                             const char *keyword)
{
    size_t length = strlen(keyword);
    const char *cursor = start;

    while (cursor + length <= end) {
        if (!strncmp(cursor, keyword, length) &&
            (cursor == start || !shader_identifier_character(cursor[-1])) &&
            (cursor + length == end ||
             !shader_identifier_character(cursor[length])))
            return 1;
        ++cursor;
    }
    return 0;
}

static int validate_sampler_declarations(const char *source, char *log)
{
    const char *cursor = source;
    const char *declaration_start = source;
    unsigned char structure_scope[64] = {0};
    int brace_depth = 0;
    int parenthesis_depth = 0;

    while (*cursor) {
        size_t sampler_length = 0;

        if (*cursor == '{') {
            if (brace_depth < (int)sizeof(structure_scope))
                structure_scope[brace_depth] =
                    range_has_keyword(declaration_start, cursor, "struct");
            ++brace_depth;
            ++cursor;
            declaration_start = cursor;
            continue;
        }
        if (*cursor == '}') {
            if (brace_depth)
                --brace_depth;
            ++cursor;
            declaration_start = cursor;
            continue;
        }
        if (*cursor == '(') {
            ++parenthesis_depth;
            ++cursor;
            continue;
        }
        if (*cursor == ')') {
            if (parenthesis_depth)
                --parenthesis_depth;
            ++cursor;
            continue;
        }
        if (*cursor == ';') {
            ++cursor;
            declaration_start = cursor;
            continue;
        }
        if (!strncmp(cursor, "sampler2D", 9) &&
            shader_keyword_at(source, cursor, 9))
            sampler_length = 9;
        else if (!strncmp(cursor, "samplerCube", 11) &&
                 shader_keyword_at(source, cursor, 11))
            sampler_length = 11;
        if (!sampler_length) {
            ++cursor;
            continue;
        }

        /* Samplers are opaque.  ES 1.00 permits them only as uniforms and
         * function parameters; in particular they cannot be locals, return
         * values, or structure members. */
        if (parenthesis_depth > 0 && brace_depth == 0) {
            const char *parameter_start = cursor;

            while (parameter_start > declaration_start &&
                   parameter_start[-1] != '(' && parameter_start[-1] != ',')
                --parameter_start;
            if (range_has_keyword(parameter_start, cursor, "out") ||
                range_has_keyword(parameter_start, cursor, "inout")) {
                strcpy(log, "sampler parameters must use the input parameter mode");
                return 0;
            }
            cursor += sampler_length;
            continue;
        }
        if (brace_depth > 0 && brace_depth <= (int)sizeof(structure_scope) &&
            structure_scope[brace_depth - 1]) {
            cursor += sampler_length;
            continue;
        }
        if (!brace_depth &&
            (range_has_keyword(declaration_start, cursor, "uniform") ||
             range_has_keyword(declaration_start, cursor, "precision"))) {
            cursor += sampler_length;
            continue;
        }
        strcpy(log, "sampler types are restricted to uniforms and function parameters");
        return 0;
    }
    return 1;
}

typedef struct {
    const char *name;
    size_t name_length;
    const char *body_start;
    const char *body_end;
    int contains_sampler;
} ShaderSamplerStruct;

static int link_declaration_visible(const char *source,
                                    const char *declaration,
                                    const char *limit);
static int link_open_braces(const char *source, const char *limit,
                            const char **braces, int capacity);

static int visible_sampler_structure(const char *source,
                                     const ShaderSamplerStruct *structures,
                                     int structure_count, const char *name,
                                     size_t name_length, const char *position)
{
    int selected = -1;
    int index;

    for (index = 0; index < structure_count; ++index) {
        const ShaderSamplerStruct *structure = &structures[index];

        if (structure->name_length != name_length ||
            strncmp(structure->name, name, name_length) ||
            structure->body_end >= position ||
            !link_declaration_visible(source, structure->body_end + 1,
                                      position))
            continue;
        if (selected < 0 ||
            structures[selected].body_end < structure->body_end)
            selected = index;
    }
    return selected;
}

static int structure_uses_sampler_type(const char *source,
                                       const ShaderSamplerStruct *structures,
                                       int structure_count,
                                       const ShaderSamplerStruct *structure)
{
    const char *cursor = structure->body_start;

    while (cursor < structure->body_end) {
        const char *end;
        int resolved;

        if (!shader_identifier_character(*cursor) ||
            (*cursor >= '0' && *cursor <= '9')) {
            ++cursor;
            continue;
        }
        end = cursor + 1;
        while (end < structure->body_end && shader_identifier_character(*end))
            ++end;
        resolved = visible_sampler_structure(
            source, structures, structure_count, cursor,
            (size_t)(end - cursor), cursor);
        if (resolved >= 0 && structures[resolved].contains_sampler)
            return 1;
        cursor = end;
    }
    return 0;
}

static int position_in_sampler_struct(const ShaderSamplerStruct *structures,
                                      int structure_count,
                                      const char *position)
{
    int index;

    for (index = 0; index < structure_count; ++index)
        if (position >= structures[index].body_start &&
            position < structures[index].body_end)
            return 1;
    return 0;
}

static int validate_sampler_structure_storage(const char *source, char *log)
{
    ShaderSamplerStruct structures[32];
    const char *cursor = source;
    int structure_count = 0;
    int changed;
    int index;

    memset(structures, 0, sizeof(structures));
    while ((cursor = strstr(cursor, "struct")) != NULL) {
        const char *name;
        const char *name_end;
        const char *open;
        const char *close;
        int depth;

        if (!shader_keyword_at(source, cursor, 6)) {
            cursor += 6;
            continue;
        }
        name = skip_shader_space(cursor + 6);
        name_end = name;
        while (shader_identifier_character(*name_end))
            ++name_end;
        open = skip_shader_space(name_end);
        if (name == name_end || *open != '{') {
            cursor += 6;
            continue;
        }
        close = open + 1;
        depth = 1;
        while (*close && depth) {
            if (*close == '{')
                ++depth;
            else if (*close == '}')
                --depth;
            ++close;
        }
        if (depth || structure_count >= (int)(sizeof(structures) /
                                               sizeof(structures[0]))) {
            cursor += 6;
            continue;
        }
        structures[structure_count].name = name;
        structures[structure_count].name_length = (size_t)(name_end - name);
        structures[structure_count].body_start = open + 1;
        structures[structure_count].body_end = close - 1;
        structures[structure_count].contains_sampler =
            range_has_keyword(open + 1, close - 1, "sampler2D") ||
            range_has_keyword(open + 1, close - 1, "samplerCube");
        ++structure_count;
        cursor = close;
    }

    do {
        changed = 0;
        for (index = 0; index < structure_count; ++index) {
            if (structures[index].contains_sampler)
                continue;
            if (structure_uses_sampler_type(source, structures, structure_count,
                                            &structures[index])) {
                structures[index].contains_sampler = 1;
                changed = 1;
            }
        }
    } while (changed);

    cursor = source;
    {
        const char *declaration_start = source;
        int brace_depth = 0;
        int parenthesis_depth = 0;

        while (*cursor) {
            const char *token_end;
            int matched = -1;

            if (*cursor == '{') {
                ++brace_depth;
                declaration_start = ++cursor;
                continue;
            }
            if (*cursor == '}') {
                if (brace_depth)
                    --brace_depth;
                declaration_start = ++cursor;
                continue;
            }
            if (*cursor == '(') {
                ++parenthesis_depth;
                ++cursor;
                continue;
            }
            if (*cursor == ')') {
                if (parenthesis_depth)
                    --parenthesis_depth;
                ++cursor;
                continue;
            }
            if (*cursor == ';') {
                declaration_start = ++cursor;
                continue;
            }
            if (!shader_identifier_character(*cursor)) {
                ++cursor;
                continue;
            }
            token_end = cursor;
            while (shader_identifier_character(*token_end))
                ++token_end;
            matched = visible_sampler_structure(
                source, structures, structure_count, cursor,
                (size_t)(token_end - cursor), cursor);
            if (matched >= 0 && !structures[matched].contains_sampler)
                matched = -1;
            if (matched >= 0 && cursor != structures[matched].name &&
                !position_in_sampler_struct(structures, structure_count, cursor)) {
                const char *after = skip_shader_space(token_end);
                const char *declarator_end = after;

                if (*after != '(' &&
                    shader_identifier_character(*after)) {
                    while (shader_identifier_character(*declarator_end))
                        ++declarator_end;
                    if (parenthesis_depth > 0 && !brace_depth) {
                        const char *parameter_start = cursor;

                        while (parameter_start > declaration_start &&
                               parameter_start[-1] != '(' &&
                               parameter_start[-1] != ',')
                            --parameter_start;
                        if (range_has_keyword(parameter_start, cursor, "out") ||
                            range_has_keyword(parameter_start, cursor, "inout")) {
                            strcpy(log, "sampler structure parameters must use input mode");
                            return 0;
                        }
                    } else if (brace_depth ||
                               !range_has_keyword(declaration_start, cursor,
                                                  "uniform")) {
                        strcpy(log, "structures containing samplers must be uniforms or function parameters");
                        return 0;
                    }
                }
            }
            cursor = token_end;
        }
    }
    return 1;
}

static int shader_has_named_structure(const char *source, const char *name,
                                      size_t name_length)
{
    const char *cursor = source;

    while ((cursor = strstr(cursor, "struct")) != NULL) {
        const char *candidate;
        const char *candidate_end;

        if (!shader_keyword_at(source, cursor, 6)) {
            cursor += 6;
            continue;
        }
        candidate = skip_shader_space(cursor + 6);
        candidate_end = candidate;
        while (shader_identifier_character(*candidate_end))
            ++candidate_end;
        if ((size_t)(candidate_end - candidate) == name_length &&
            !strncmp(candidate, name, name_length) &&
            *skip_shader_space(candidate_end) == '{')
            return 1;
        cursor += 6;
    }
    return 0;
}

static int shader_declaration_type(const char *source, const char *type,
                                   size_t type_length)
{
    static const char *const types[] = {
        "float", "vec2", "vec3", "vec4", "mat2", "mat3", "mat4",
        "int", "ivec2", "ivec3", "ivec4", "bool", "bvec2", "bvec3",
        "bvec4", "sampler2D", "samplerCube",
    };
    size_t index;

    for (index = 0; index < sizeof(types) / sizeof(types[0]); ++index)
        if (strlen(types[index]) == type_length &&
            !strncmp(types[index], type, type_length))
            return 1;
    return shader_has_named_structure(source, type, type_length);
}

static int shader_declaration_qualifier(const char *token, size_t length)
{
    return (length == 5 && !strncmp(token, "const", 5)) ||
           (length == 7 && !strncmp(token, "uniform", 7)) ||
           (length == 7 && !strncmp(token, "varying", 7)) ||
           (length == 9 && !strncmp(token, "attribute", 9)) ||
           (length == 4 && !strncmp(token, "lowp", 4)) ||
           (length == 7 && !strncmp(token, "mediump", 7)) ||
           (length == 5 && !strncmp(token, "highp", 5)) ||
           (length == 9 && !strncmp(token, "invariant", 9));
}

static int comma_declarator_has_type(const char *source, const char *name)
{
    const char *before = name;
    const char *statement;
    const char *token;
    const char *token_end;
    int parentheses = 0;

    while (before > source && (before[-1] == ' ' || before[-1] == '\t' ||
                               before[-1] == '\r' || before[-1] == '\n'))
        --before;
    if (before == source || before[-1] != ',')
        return 0;
    statement = before - 1;
    while (statement > source && statement[-1] != ';' &&
           statement[-1] != '{' && statement[-1] != '}')
        --statement;
    for (token = statement; token < name; ++token) {
        if (*token == '(')
            ++parentheses;
        else if (*token == ')' && parentheses)
            --parentheses;
    }
    if (parentheses)
        return 0;
    token = skip_shader_space(statement);
    do {
        token_end = token;
        while (token_end < name && shader_identifier_character(*token_end))
            ++token_end;
        if (token == token_end)
            return 0;
        if (!shader_declaration_qualifier(token,
                                          (size_t)(token_end - token)))
            break;
        token = skip_shader_space(token_end);
    } while (token < name);
    return shader_declaration_type(source, token,
                                   (size_t)(token_end - token));
}

static int shader_array_declarator(const char *source, const char *name)
{
    const char *type_end = name;
    const char *type_start;

    while (type_end > source && (type_end[-1] == ' ' || type_end[-1] == '\t' ||
                                 type_end[-1] == '\r' || type_end[-1] == '\n'))
        --type_end;
    type_start = type_end;
    while (type_start > source && shader_identifier_character(type_start[-1]))
        --type_start;
    return (type_start < type_end &&
            shader_declaration_type(source, type_start,
                                    (size_t)(type_end - type_start))) ||
           comma_declarator_has_type(source, name);
}

static int shader_position_in_structure(const char *source,
                                        const char *position)
{
    const char *cursor = source;

    while ((cursor = strstr(cursor, "struct")) != NULL && cursor < position) {
        const char *open;
        const char *close;
        int braces;

        if (!shader_keyword_at(source, cursor, 6)) {
            cursor += 6;
            continue;
        }
        open = strchr(cursor + 6, '{');
        if (!open || open >= position) {
            cursor += 6;
            continue;
        }
        close = open + 1;
        braces = 1;
        while (*close && braces) {
            if (*close == '{')
                ++braces;
            else if (*close == '}')
                --braces;
            ++close;
        }
        if (!braces && position > open && position < close)
            return 1;
        cursor = close > cursor ? close : cursor + 6;
    }
    return 0;
}

static int shader_global_const_int(const char *source, const char *limit,
                                   const char *name, size_t name_length)
{
    const char *statement = source;
    int brace_depth = 0;

    while (statement < limit) {
        const char *end = statement;
        const char *const_token;
        const char *int_token;
        const char *declarator;

        while (end < limit) {
            if (*end == '{')
                ++brace_depth;
            else if (*end == '}' && brace_depth)
                --brace_depth;
            if (*end == ';' && !brace_depth)
                break;
            ++end;
        }
        if (end >= limit)
            break;
        const_token = statement;
        while (const_token < end &&
               !(end - const_token >= 5 &&
                 !strncmp(const_token, "const", 5) &&
                 (const_token == statement ||
                  !shader_identifier_character(const_token[-1])) &&
                 !shader_identifier_character(const_token[5])))
            ++const_token;
        if (const_token == end) {
            statement = end + 1;
            continue;
        }
        int_token = const_token + 5;
        while (int_token < end &&
               !(end - int_token >= 3 && !strncmp(int_token, "int", 3) &&
                 !shader_identifier_character(int_token[-1]) &&
                 !shader_identifier_character(int_token[3])))
            ++int_token;
        if (int_token == end) {
            statement = end + 1;
            continue;
        }
        declarator = int_token + 3;
        while (declarator < end) {
            const char *candidate = skip_shader_space(declarator);
            const char *candidate_end = candidate;
            const char *scan;
            int parentheses = 0;

            while (candidate_end < end &&
                   shader_identifier_character(*candidate_end))
                ++candidate_end;
            if ((size_t)(candidate_end - candidate) == name_length &&
                !strncmp(candidate, name, name_length))
                return 1;
            scan = candidate_end;
            while (scan < end) {
                if (*scan == '(')
                    ++parentheses;
                else if (*scan == ')' && parentheses)
                    --parentheses;
                else if (*scan == ',' && !parentheses)
                    break;
                ++scan;
            }
            if (scan == end)
                break;
            declarator = scan + 1;
        }
        statement = end + 1;
    }
    return 0;
}

static int validate_structure_array_bound(const char *source,
                                          const char *declaration,
                                          const char *start, const char *end,
                                          char *log)
{
    const char *cursor = start;

    if (!shader_position_in_structure(source, declaration))
        return 1;
    while (cursor < end) {
        const char *name;
        const char *name_end;
        const char *after;

        if (!shader_identifier_character(*cursor) ||
            (*cursor >= '0' && *cursor <= '9')) {
            ++cursor;
            continue;
        }
        name = cursor;
        name_end = name;
        while (name_end < end && shader_identifier_character(*name_end))
            ++name_end;
        after = skip_shader_space(name_end);
        if ((name_end - name >= 3 && !strncmp(name, "gl_", 3)) ||
            (after < end && *after == '(')) {
            cursor = name_end;
            continue;
        }
        if (!shader_global_const_int(source, declaration, name,
                                     (size_t)(name_end - name))) {
            strcpy(log, "structure array size must use a global const int expression");
            return 0;
        }
        cursor = name_end;
    }
    return 1;
}

static int validate_no_array_of_array_declarations(const char *source, char *log)
{
    const char *cursor = source;

    while (*cursor) {
        const char *name;
        const char *name_end;
        const char *first_open;
        const char *first_close;
        const char *second_open;
        int declaration;
        int brackets;

        if (!shader_identifier_character(*cursor)) {
            ++cursor;
            continue;
        }
        name = cursor;
        name_end = name;
        while (shader_identifier_character(*name_end))
            ++name_end;
        first_open = skip_shader_space(name_end);
        if (*first_open != '[') {
            cursor = name_end;
            continue;
        }
        first_close = first_open + 1;
        brackets = 1;
        while (*first_close && brackets) {
            if (*first_close == '[')
                ++brackets;
            else if (*first_close == ']')
                --brackets;
            ++first_close;
        }
        if (brackets) {
            cursor = name_end;
            continue;
        }
        declaration = shader_array_declarator(source, name);
        if (declaration) {
            const char *dimension_start = skip_shader_space(first_open + 1);
            const char *dimension_end = first_close - 1;

            while (dimension_end > dimension_start &&
                   (dimension_end[-1] == ' ' || dimension_end[-1] == '\t' ||
                    dimension_end[-1] == '\r' || dimension_end[-1] == '\n'))
                --dimension_end;
            if (dimension_start == dimension_end) {
                strcpy(log, "array declarations require an explicit size");
                return 0;
            }
            if ((size_t)(dimension_end - dimension_start) < 64 &&
                ((*dimension_start >= '0' && *dimension_start <= '9') ||
                 *dimension_start == '+' || *dimension_start == '-')) {
                char dimension[64];
                char *parse_end;
                long value;
                size_t dimension_length =
                    (size_t)(dimension_end - dimension_start);

                memcpy(dimension, dimension_start, dimension_length);
                dimension[dimension_length] = '\0';
                value = strtol(dimension, &parse_end, 0);
                if (!*parse_end && value <= 0) {
                    strcpy(log, "array size must be a positive integer constant");
                    return 0;
                }
                if (*parse_end) {
                    while (*parse_end == ' ' || *parse_end == '\t' ||
                           *parse_end == '\r' || *parse_end == '\n')
                        ++parse_end;
                    if (*parse_end && !strchr("+-*/()", *parse_end)) {
                        strcpy(log, "array size must be an integer constant expression");
                        return 0;
                    }
                }
            }
            if (!validate_structure_array_bound(source, name, dimension_start,
                                                dimension_end, log))
                return 0;
        }
        second_open = skip_shader_space(first_close);
        if (*second_open != '[') {
            cursor = first_close;
            continue;
        }

        if (declaration) {
            strcpy(log, "arrays of arrays are not supported in GLSL ES 1.00");
            return 0;
        }
        cursor = first_close;
    }
    return 1;
}

static int declaration_qualifier_kind(const char *token, size_t length)
{
    if ((length == 5 && !strncmp(token, "const", 5)) ||
        (length == 7 && !strncmp(token, "uniform", 7)) ||
        (length == 7 && !strncmp(token, "varying", 7)) ||
        (length == 9 && !strncmp(token, "attribute", 9)))
        return 1;
    if ((length == 4 && !strncmp(token, "lowp", 4)) ||
        (length == 7 && !strncmp(token, "mediump", 7)) ||
        (length == 5 && !strncmp(token, "highp", 5)))
        return 2;
    if (length == 9 && !strncmp(token, "invariant", 9))
        return 3;
    return 0;
}

static int type_accepts_precision(const char *type, size_t length)
{
    static const char *const types[] = {
        "float", "vec2", "vec3", "vec4", "mat2", "mat3", "mat4",
        "int", "ivec2", "ivec3", "ivec4", "sampler2D", "samplerCube",
    };
    size_t index;

    for (index = 0; index < sizeof(types) / sizeof(types[0]); ++index)
        if (strlen(types[index]) == length && !strncmp(types[index], type, length))
            return 1;
    return 0;
}

static int validate_declaration_qualifiers(const char *source, char *log)
{
    const char *cursor = source;

    while (*cursor) {
        const char *token;
        const char *token_end;
        const char *scan;
        int first_kind;
        int storage_count = 0;
        int storage_is_varying = 0;
        int precision_seen = 0;
        int invariant_seen = 0;

        if (!((*cursor >= 'a' && *cursor <= 'z') ||
              (*cursor >= 'A' && *cursor <= 'Z') || *cursor == '_')) {
            ++cursor;
            continue;
        }
        token = cursor;
        token_end = cursor + 1;
        while (shader_identifier_character(*token_end))
            ++token_end;
        first_kind = declaration_qualifier_kind(
            token, (size_t)(token_end - token));
        if (!first_kind) {
            cursor = token_end;
            continue;
        }
        scan = token;
        for (;;) {
            size_t length;
            int kind;

            token = scan;
            token_end = token;
            while (shader_identifier_character(*token_end))
                ++token_end;
            length = (size_t)(token_end - token);
            kind = declaration_qualifier_kind(token, length);
            if (!kind)
                break;
            if (precision_seen) {
                strcpy(log, "precision qualifier must be the final qualifier");
                return 0;
            }
            if (kind == 1) {
                if (++storage_count > 1) {
                    strcpy(log, "declaration has multiple storage qualifiers");
                    return 0;
                }
                storage_is_varying = length == 7 &&
                                     !strncmp(token, "varying", 7);
            } else if (kind == 2) {
                precision_seen = 1;
            } else {
                if (invariant_seen || storage_count) {
                    strcpy(log, "invariant must precede the varying qualifier");
                    return 0;
                }
                invariant_seen = 1;
            }
            scan = skip_shader_space(token_end);
            if (!shader_identifier_character(*scan))
                break;
        }
        if (precision_seen &&
            !type_accepts_precision(token, (size_t)(token_end - token))) {
            strcpy(log, "type cannot have a precision qualifier");
            return 0;
        }
        if (invariant_seen && !storage_is_varying &&
            !(!strncmp(token, "gl_Position", 11) &&
              !shader_identifier_character(token[11])) &&
            !(!strncmp(token, "gl_PointSize", 12) &&
              !shader_identifier_character(token[12])) &&
            !(!strncmp(token, "gl_FragCoord", 12) &&
              !shader_identifier_character(token[12])) &&
            !(!strncmp(token, "gl_PointCoord", 13) &&
              !shader_identifier_character(token[13])) &&
            !(!strncmp(token, "gl_FragColor", 12) &&
              !shader_identifier_character(token[12])) &&
            !(!strncmp(token, "gl_FragData", 11) &&
              !shader_identifier_character(token[11]))) {
            strcpy(log, "invariant qualifier requires a shader interface variable");
            return 0;
        }
        cursor = token_end;
    }
    return 1;
}

static int precision_is_unbraced_control_body(const char *source,
                                              const char *declaration)
{
    const char *before = declaration;

    while (before > source && (before[-1] == ' ' || before[-1] == '\t' ||
                               before[-1] == '\r' || before[-1] == '\n'))
        --before;
    if (before > source && before[-1] == ')') {
        const char *cursor = before - 1;
        int depth = 1;

        while (cursor > source && depth) {
            --cursor;
            if (*cursor == ')')
                ++depth;
            else if (*cursor == '(')
                --depth;
        }
        if (!depth) {
            const char *word_end = cursor;
            const char *word;

            while (word_end > source &&
                   (word_end[-1] == ' ' || word_end[-1] == '\t' ||
                    word_end[-1] == '\r' || word_end[-1] == '\n'))
                --word_end;
            word = word_end;
            while (word > source && shader_identifier_character(word[-1]))
                --word;
            if ((word_end - word == 2 && !strncmp(word, "if", 2)) ||
                (word_end - word == 3 && !strncmp(word, "for", 3)) ||
                (word_end - word == 5 && !strncmp(word, "while", 5)))
                return 1;
        }
    }
    {
        const char *word_end = before;
        const char *word = word_end;

        while (word > source && shader_identifier_character(word[-1]))
            --word;
        return (word_end - word == 2 && !strncmp(word, "do", 2)) ||
               (word_end - word == 4 && !strncmp(word, "else", 4));
    }
}

static int fragment_has_default_float_precision_at(const char *source,
                                                   const char *position)
{
    const char *cursor = source;
    int found = 0;

    while (cursor < position &&
           (cursor = strstr(cursor, "precision")) != NULL && cursor < position) {
        const char *qualifier;
        const char *type;

        if (!shader_keyword_at(source, cursor, 9)) {
            cursor += 9;
            continue;
        }
        qualifier = skip_shader_space(cursor + 9);
        qualifier = skip_precision(qualifier);
        type = qualifier;
        if (!strncmp(type, "float", 5) &&
            !shader_identifier_character(type[5]) &&
            !precision_is_unbraced_control_body(source, cursor) &&
            link_declaration_visible(source, cursor, position))
            found = 1;
        cursor += 9;
    }
    return found;
}

static int declaration_has_explicit_precision(const char *source, const char *type)
{
    const char *end = type;
    const char *start;
    size_t length;

    while (end > source && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' ||
                            end[-1] == '\n'))
        --end;
    start = end;
    while (start > source && shader_identifier_character(start[-1]))
        --start;
    length = (size_t)(end - start);
    return (length == 4 && !strncmp(start, "lowp", 4)) ||
           (length == 7 && !strncmp(start, "mediump", 7)) ||
           (length == 5 && !strncmp(start, "highp", 5));
}

static int validate_fragment_float_precision(const char *source, char *log)
{
    static const char *const types[] = {
        "float", "vec2", "vec3", "vec4", "mat2", "mat3", "mat4",
    };
    size_t type_index;

    for (type_index = 0; type_index < sizeof(types) / sizeof(types[0]); ++type_index) {
        const char *cursor = source;
        size_t type_length = strlen(types[type_index]);

        while ((cursor = strstr(cursor, types[type_index])) != NULL) {
            const char *after;

            if (!shader_keyword_at(source, cursor, type_length)) {
                cursor += type_length;
                continue;
            }
            after = skip_shader_space(cursor + type_length);
            if (((*after >= 'a' && *after <= 'z') ||
                 (*after >= 'A' && *after <= 'Z') || *after == '_') &&
                !declaration_has_explicit_precision(source, cursor) &&
                !fragment_has_default_float_precision_at(source, cursor)) {
                strcpy(log,
                       "fragment floating-point declaration requires a precision qualifier");
                return 0;
            }
            cursor += type_length;
        }
    }
    return 1;
}

static const char *control_space(const char *cursor, const char *end)
{
    for (;;) {
        while (cursor < end && (*cursor == ' ' || *cursor == '\t' ||
                                *cursor == '\r' || *cursor == '\n'))
            ++cursor;
        if (end - cursor >= 2 && cursor[0] == '/' && cursor[1] == '/') {
            cursor += 2;
            while (cursor < end && *cursor != '\n')
                ++cursor;
            continue;
        }
        if (end - cursor >= 2 && cursor[0] == '/' && cursor[1] == '*') {
            cursor += 2;
            while (end - cursor >= 2 && !(cursor[0] == '*' && cursor[1] == '/'))
                ++cursor;
            if (end - cursor >= 2)
                cursor += 2;
            continue;
        }
        return cursor;
    }
}

static int control_word(const char *cursor, const char *end, const char *word,
                        const char **after)
{
    size_t length = strlen(word);

    cursor = control_space(cursor, end);
    if ((size_t)(end - cursor) < length || strncmp(cursor, word, length) ||
        (cursor + length < end &&
         ((cursor[length] >= 'a' && cursor[length] <= 'z') ||
          (cursor[length] >= 'A' && cursor[length] <= 'Z') ||
          (cursor[length] >= '0' && cursor[length] <= '9') ||
          cursor[length] == '_')))
        return 0;
    if (after)
        *after = cursor + length;
    return 1;
}

static const char *control_matching(const char *open, const char *end,
                                    char opening, char closing)
{
    const char *cursor;
    int depth = 0;

    for (cursor = open; cursor < end; ++cursor) {
        if (end - cursor >= 2 && cursor[0] == '/' && cursor[1] == '/') {
            while (cursor < end && *cursor != '\n')
                ++cursor;
            if (cursor >= end)
                break;
        } else if (end - cursor >= 2 && cursor[0] == '/' && cursor[1] == '*') {
            cursor += 2;
            while (end - cursor >= 2 && !(cursor[0] == '*' && cursor[1] == '/'))
                ++cursor;
            if (cursor >= end)
                break;
            ++cursor;
        } else if (*cursor == opening) {
            ++depth;
        } else if (*cursor == closing && !--depth) {
            return cursor;
        }
    }
    return NULL;
}

static int structure_basic_member_type(const char *type, size_t length)
{
    static const char *const types[] = {
        "float", "vec2", "vec3", "vec4", "mat2", "mat3", "mat4",
        "int", "ivec2", "ivec3", "ivec4", "bool", "bvec2", "bvec3",
        "bvec4", "sampler2D", "samplerCube",
    };
    size_t index;

    for (index = 0; index < sizeof(types) / sizeof(types[0]); ++index)
        if (strlen(types[index]) == length &&
            !strncmp(types[index], type, length))
            return 1;
    return 0;
}

static int structure_type_declared_before(const char *source,
                                          const char *position,
                                          const char *type, size_t length)
{
    const char *cursor = source;

    while ((cursor = strstr(cursor, "struct")) != NULL && cursor < position) {
        const char *name;
        const char *name_end;
        const char *open;
        const char *close;

        if (!shader_keyword_at(source, cursor, 6)) {
            cursor += 6;
            continue;
        }
        name = control_space(cursor + 6, position);
        name_end = name;
        while (name_end < position && shader_identifier_character(*name_end))
            ++name_end;
        open = control_space(name_end, position);
        if ((size_t)(name_end - name) == length &&
            !strncmp(name, type, length) && open < position && *open == '{' &&
            (close = control_matching(open, source + strlen(source), '{', '}')) &&
            close < position)
            return 1;
        cursor += 6;
    }
    return 0;
}

static int validate_structure_declarations(const char *source, char *log)
{
    const char *source_end = source + strlen(source);
    const char *structure = source;

    while ((structure = strstr(structure, "struct")) != NULL) {
        const char *cursor;
        const char *open;
        const char *close;
        char member_names[MESAGL_MAX_SHADER_STRUCT_STORAGE]
                         [MESAGL_MAX_SHADER_LVALUE_PATH_LENGTH];
        int member_count = 0;

        if (!shader_keyword_at(source, structure, 6)) {
            structure += 6;
            continue;
        }
        cursor = control_space(structure + 6, source_end);
        if (cursor < source_end && shader_identifier_character(*cursor))
            while (cursor < source_end && shader_identifier_character(*cursor))
                ++cursor;
        open = control_space(cursor, source_end);
        if (open >= source_end || *open != '{') {
            structure += 6;
            continue;
        }
        close = control_matching(open, source_end, '{', '}');
        if (!close) {
            strcpy(log, "unterminated structure declaration");
            return 0;
        }
        cursor = control_space(open + 1, close);
        if (cursor >= close) {
            strcpy(log, "structure must declare at least one member");
            return 0;
        }
        while (cursor < close) {
            const char *statement_end = cursor;
            const char *type;
            const char *type_end;
            const char *declarator;
            int braces = 0;
            int brackets = 0;

            while (statement_end < close) {
                if (*statement_end == '{')
                    ++braces;
                else if (*statement_end == '}')
                    --braces;
                else if (*statement_end == '[')
                    ++brackets;
                else if (*statement_end == ']')
                    --brackets;
                else if (*statement_end == '=' && !braces && !brackets) {
                    strcpy(log, "structure members cannot have initializers");
                    return 0;
                } else if (*statement_end == ';' && !braces && !brackets) {
                    break;
                }
                ++statement_end;
            }
            if (statement_end >= close) {
                strcpy(log, "unterminated structure member declaration");
                return 0;
            }
            type = cursor;
            type_end = type;
            while (type_end < statement_end &&
                   shader_identifier_character(*type_end))
                ++type_end;
            if ((type_end - type == 5 && !strncmp(type, "const", 5)) ||
                (type_end - type == 9 && !strncmp(type, "attribute", 9)) ||
                (type_end - type == 7 && !strncmp(type, "varying", 7)) ||
                (type_end - type == 7 && !strncmp(type, "uniform", 7)) ||
                (type_end - type == 9 && !strncmp(type, "invariant", 9))) {
                strcpy(log, "structure members cannot have type qualifiers other than precision");
                return 0;
            }
            if (type_end - type == 6 && !strncmp(type, "struct", 6)) {
                strcpy(log, "embedded structure definitions are not allowed");
                return 0;
            }
            if ((type_end - type == 4 && !strncmp(type, "lowp", 4)) ||
                (type_end - type == 7 && !strncmp(type, "mediump", 7)) ||
                (type_end - type == 5 && !strncmp(type, "highp", 5))) {
                type = control_space(type_end, statement_end);
                type_end = type;
                while (type_end < statement_end &&
                       shader_identifier_character(*type_end))
                    ++type_end;
            }
            if (type_end - type == 6 && !strncmp(type, "struct", 6)) {
                strcpy(log, "embedded structure definitions are not allowed");
                return 0;
            }
            if (type_end - type == 4 && !strncmp(type, "void", 4)) {
                strcpy(log, "structure members cannot have void type");
                return 0;
            }
            if (!(type_end - type == 6 && !strncmp(type, "struct", 6)) &&
                !structure_basic_member_type(type,
                                             (size_t)(type_end - type)) &&
                !structure_type_declared_before(source, type, type,
                                                (size_t)(type_end - type))) {
                strcpy(log, "structure member uses an undeclared type");
                return 0;
            }
            declarator = control_space(type_end, statement_end);
            if (type_end - type == 6 && !strncmp(type, "struct", 6)) {
                const char *nested = declarator;

                if (nested < statement_end &&
                    shader_identifier_character(*nested)) {
                    while (nested < statement_end &&
                           shader_identifier_character(*nested))
                        ++nested;
                    nested = control_space(nested, statement_end);
                }
                if (nested < statement_end && *nested == '{') {
                    const char *nested_close =
                        control_matching(nested, statement_end, '{', '}');

                    if (!nested_close) {
                        strcpy(log, "unterminated nested structure member");
                        return 0;
                    }
                    declarator = control_space(nested_close + 1,
                                               statement_end);
                }
            }
            while (declarator < statement_end) {
                const char *name = declarator;
                const char *name_end = name;
                const char *after;
                int existing;

                while (name_end < statement_end &&
                       shader_identifier_character(*name_end))
                    ++name_end;
                if (name == name_end || name_end - name >= MESAGL_MAX_SHADER_IDENTIFIER_LENGTH) {
                    strcpy(log, "invalid or overlong structure member name");
                    return 0;
                }
                for (existing = 0; existing < member_count; ++existing)
                    if (strlen(member_names[existing]) ==
                            (size_t)(name_end - name) &&
                        !strncmp(member_names[existing], name,
                                 (size_t)(name_end - name))) {
                        strcpy(log, "duplicate structure member name");
                        return 0;
                    }
                if (member_count >= MESAGL_MAX_SHADER_STRUCT_STORAGE) {
                    strcpy(log, "structure member limit exceeded");
                    return 0;
                }
                memcpy(member_names[member_count], name,
                       (size_t)(name_end - name));
                member_names[member_count][name_end - name] = '\0';
                ++member_count;
                after = control_space(name_end, statement_end);
                if (after < statement_end && *after == '[') {
                    const char *array_end =
                        control_matching(after, statement_end, '[', ']');

                    if (!array_end) {
                        strcpy(log, "unterminated structure member array");
                        return 0;
                    }
                    after = control_space(array_end + 1, statement_end);
                }
                if (after == statement_end)
                    break;
                if (*after != ',') {
                    strcpy(log, "invalid structure member declarator");
                    return 0;
                }
                declarator = control_space(after + 1, statement_end);
            }
            cursor = control_space(statement_end + 1, close);
        }
        structure += 6;
    }
    return 1;
}

static int validate_control_statement(const char *start, const char *end,
                                      int loop_depth, const char **next,
                                      char *log);

static int validate_control_range(const char *start, const char *end,
                                  int loop_depth, char *log)
{
    const char *cursor = start;

    while ((cursor = control_space(cursor, end)) < end) {
        const char *next;

        if (!validate_control_statement(cursor, end, loop_depth, &next, log))
            return 0;
        if (next <= cursor)
            return 0;
        cursor = next;
    }
    return 1;
}

static int validate_control_statement(const char *start, const char *end,
                                      int loop_depth, const char **next,
                                      char *log)
{
    const char *cursor = control_space(start, end);
    const char *after;
    const char *close;

    if (cursor >= end) {
        *next = end;
        return 1;
    }
    if (*cursor == '{') {
        close = control_matching(cursor, end, '{', '}');
        if (!close || !validate_control_range(cursor + 1, close, loop_depth, log))
            return 0;
        *next = close + 1;
        return 1;
    }
    if (control_word(cursor, end, "break", &after) ||
        control_word(cursor, end, "continue", &after)) {
        if (!loop_depth) {
            strcpy(log, "break or continue used outside a loop body");
            return 0;
        }
        after = control_space(after, end);
        if (after >= end || *after != ';')
            return 0;
        *next = after + 1;
        return 1;
    }
    if (control_word(cursor, end, "for", &after) ||
        control_word(cursor, end, "while", &after)) {
        after = control_space(after, end);
        if (after >= end || *after != '(' ||
            !(close = control_matching(after, end, '(', ')')))
            return 0;
        return validate_control_statement(close + 1, end, loop_depth + 1,
                                          next, log);
    }
    if (control_word(cursor, end, "do", &after)) {
        const char *body_end;

        if (!validate_control_statement(after, end, loop_depth + 1,
                                        &body_end, log))
            return 0;
        if (!control_word(body_end, end, "while", &after))
            return 0;
        after = control_space(after, end);
        if (after >= end || *after != '(' ||
            !(close = control_matching(after, end, '(', ')')))
            return 0;
        after = control_space(close + 1, end);
        if (after >= end || *after != ';')
            return 0;
        *next = after + 1;
        return 1;
    }
    if (control_word(cursor, end, "if", &after)) {
        const char *then_end;

        after = control_space(after, end);
        if (after >= end || *after != '(' ||
            !(close = control_matching(after, end, '(', ')')) ||
            !validate_control_statement(close + 1, end, loop_depth,
                                        &then_end, log))
            return 0;
        if (control_word(then_end, end, "else", &after))
            return validate_control_statement(after, end, loop_depth, next, log);
        *next = then_end;
        return 1;
    }
    for (after = cursor; after < end; ++after) {
        if (end - after >= 2 && after[0] == '/' && after[1] == '/') {
            while (after < end && *after != '\n')
                ++after;
            if (after >= end)
                break;
        } else if (end - after >= 2 && after[0] == '/' && after[1] == '*') {
            after += 2;
            while (end - after >= 2 && !(after[0] == '*' && after[1] == '/'))
                ++after;
            if (after >= end)
                break;
            ++after;
        } else if (*after == '(') {
            close = control_matching(after, end, '(', ')');
            if (!close)
                return 0;
            after = close;
        } else if (*after == '{') {
            close = control_matching(after, end, '{', '}');
            if (!close || !validate_control_range(after + 1, close, 0, log))
                return 0;
            *next = close + 1;
            return 1;
        } else if (*after == ';') {
            *next = after + 1;
            return 1;
        }
    }
    *next = end;
    return 1;
}

static int validate_loop_control(const char *source, char *log)
{
    return validate_control_range(source, source + strlen(source), 0, log);
}

static int validate_reserved_operators(const char *source, char *log)
{
    const char *cursor;

    for (cursor = source; *cursor; ++cursor) {
        if (*cursor == '%' || *cursor == '~' ||
            (*cursor == '<' && cursor[1] == '<') ||
            (*cursor == '>' && cursor[1] == '>') ||
            (*cursor == '&' && cursor[1] != '&' &&
             (cursor == source || cursor[-1] != '&')) ||
            (*cursor == '|' && cursor[1] != '|' &&
             (cursor == source || cursor[-1] != '|')) ||
            (*cursor == '^' && cursor[1] != '^' &&
             (cursor == source || cursor[-1] != '^'))) {
            strcpy(log, "reserved operator is not available in GLSL ES 1.00");
            return 0;
        }
    }
    return 1;
}

static int shader_hexadecimal_digit(char character)
{
    return (character >= '0' && character <= '9') ||
           (character >= 'a' && character <= 'f') ||
           (character >= 'A' && character <= 'F');
}

static int shader_integer_literal_in_range(const char *start, const char *end)
{
    char *parse_end;
    unsigned long long value = strtoull(start, &parse_end, 0);

    return parse_end == end && value <= UINT32_MAX;
}

static int validate_numeric_literals(const char *source, char *log)
{
    const char *cursor = source;

    while (*cursor) {
        const char *end = cursor;
        int invalid_digit = 0;
        int floating = 0;
        int octal = 0;

        if (cursor > source && shader_identifier_character(cursor[-1])) {
            ++cursor;
            continue;
        }
        if (*end == '.' && end[1] >= '0' && end[1] <= '9') {
            floating = 1;
            ++end;
            while (*end >= '0' && *end <= '9')
                ++end;
        } else if (*end >= '0' && *end <= '9') {
            if (end[0] == '0' && (end[1] == 'x' || end[1] == 'X')) {
                end += 2;
                if (!shader_hexadecimal_digit(*end))
                    goto invalid;
                while (shader_hexadecimal_digit(*end))
                    ++end;
                if (shader_identifier_character(*end) || *end == '.')
                    goto invalid;
                if (!shader_integer_literal_in_range(cursor, end))
                    goto out_of_range;
                cursor = end;
                continue;
            }
            octal = *end == '0';
            while (*end >= '0' && *end <= '9') {
                if (*end == '8' || *end == '9')
                    invalid_digit = 1;
                ++end;
            }
            if (*end == '.') {
                floating = 1;
                ++end;
                while (*end >= '0' && *end <= '9')
                    ++end;
            }
        } else {
            ++end;
            cursor = end;
            continue;
        }
        if (*end == 'e' || *end == 'E') {
            floating = 1;
            ++end;
            if (*end == '+' || *end == '-')
                ++end;
            if (*end < '0' || *end > '9')
                goto invalid;
            while (*end >= '0' && *end <= '9')
                ++end;
        }
        if (shader_identifier_character(*end) || *end == '.')
            goto invalid;
        if (octal && invalid_digit && !floating) {
            snprintf(log, LOG_SIZE, "invalid octal integer literal: %.*s",
                     (int)(end - cursor), cursor);
            return 0;
        }
        if (!floating && !shader_integer_literal_in_range(cursor, end))
            goto out_of_range;
        cursor = end;
        continue;

invalid:
        while (shader_identifier_character(*end) || *end == '.' || *end == '+' ||
               *end == '-')
            ++end;
        snprintf(log, LOG_SIZE, "invalid GLSL ES 1.00 numeric literal: %.*s",
                 (int)(end - cursor), cursor);
        return 0;

out_of_range:
        snprintf(log, LOG_SIZE,
                 "integer literal exceeds the supported 32-bit spelling range: %.*s",
                 (int)(end - cursor), cursor);
        return 0;
    }
    return 1;
}

static int shader_storage_has_initializer(const char *source, const char *qualifier)
{
    const char *cursor = source;
    size_t length = strlen(qualifier);

    while ((cursor = strstr(cursor, qualifier)) != NULL) {
        const char *end;
        const char *scan;
        int brackets = 0;

        if (!shader_keyword_at(source, cursor, length)) {
            cursor += length;
            continue;
        }
        end = strchr(cursor + length, ';');
        if (!end)
            return 1;
        for (scan = cursor + length; scan < end; ++scan) {
            if (*scan == '[')
                ++brackets;
            else if (*scan == ']')
                --brackets;
            else if (*scan == '=' && !brackets && scan[1] != '=')
                return 1;
        }
        cursor = end + 1;
    }
    return 0;
}

static int shader_storage_has_array(const char *source, const char *qualifier)
{
    const char *cursor = source;
    size_t length = strlen(qualifier);

    while ((cursor = strstr(cursor, qualifier)) != NULL) {
        const char *end;

        if (!shader_keyword_at(source, cursor, length)) {
            cursor += length;
            continue;
        }
        end = strchr(cursor + length, ';');
        if (!end)
            return 1;
        if (memchr(cursor + length, '[', (size_t)(end - cursor - length)))
            return 1;
        cursor = end + 1;
    }
    return 0;
}

static int validate_vertex_attribute_declarations(const char *source, char *log)
{
    const char *cursor = source;

    while ((cursor = strstr(cursor, "attribute")) != NULL) {
        const char *type;
        const char *type_end;
        size_t length;
        int valid;

        if (!shader_keyword_at(source, cursor, 9)) {
            cursor += 9;
            continue;
        }
        type = skip_precision(skip_shader_space(cursor + 9));
        type_end = type;
        while (shader_identifier_character(*type_end))
            ++type_end;
        length = (size_t)(type_end - type);
        valid = (length == 5 && !strncmp(type, "float", 5)) ||
                (length == 4 && (!strncmp(type, "vec2", 4) ||
                                 !strncmp(type, "vec3", 4) ||
                                 !strncmp(type, "vec4", 4) ||
                                 !strncmp(type, "mat2", 4) ||
                                 !strncmp(type, "mat3", 4) ||
                                 !strncmp(type, "mat4", 4)));
        if (!valid) {
            strcpy(log, "vertex attribute has an invalid GLSL ES 1.00 type");
            return 0;
        }
        cursor = type_end;
    }
    if (shader_storage_has_array(source, "attribute")) {
        strcpy(log, "vertex attribute cannot have array type in GLSL ES 1.00");
        return 0;
    }
    return 1;
}

static int validate_varying_declaration_types(const char *source, char *log)
{
    const char *cursor = source;

    while ((cursor = strstr(cursor, "varying")) != NULL) {
        const char *type;
        const char *type_end;
        size_t length;
        int valid;

        if (!shader_keyword_at(source, cursor, 7)) {
            cursor += 7;
            continue;
        }
        type = skip_precision(skip_shader_space(cursor + 7));
        type_end = type;
        while (shader_identifier_character(*type_end))
            ++type_end;
        length = (size_t)(type_end - type);
        valid = (length == 5 && !strncmp(type, "float", 5)) ||
                (length == 4 && (!strncmp(type, "vec2", 4) ||
                                 !strncmp(type, "vec3", 4) ||
                                 !strncmp(type, "vec4", 4) ||
                                 !strncmp(type, "mat2", 4) ||
                                 !strncmp(type, "mat3", 4) ||
                                 !strncmp(type, "mat4", 4)));
        if (!valid) {
            strcpy(log, "varying has an invalid GLSL ES 1.00 type");
            return 0;
        }
        cursor = type_end;
    }
    return 1;
}

static int shader_has_call_arity(const char *source, const char *name, int expected_arity)
{
    const char *cursor = source;
    size_t length = strlen(name);

    while ((cursor = strstr(cursor, name)) != NULL) {
        const char *open = cursor + length;
        const char *scan;
        int parentheses = 0;
        int brackets = 0;
        int arity = 1;

        while (*open == ' ' || *open == '\t' || *open == '\r' || *open == '\n')
            ++open;
        if (*open != '(' || !shader_keyword_at(source, cursor, length)) {
            cursor += length;
            continue;
        }
        scan = open + 1;
        while (*scan == ' ' || *scan == '\t' || *scan == '\r' || *scan == '\n')
            ++scan;
        if (*scan == ')')
            arity = 0;
        for (; *scan; ++scan) {
            if (*scan == '(')
                ++parentheses;
            else if (*scan == ')' && !parentheses)
                break;
            else if (*scan == ')')
                --parentheses;
            else if (*scan == '[')
                ++brackets;
            else if (*scan == ']')
                --brackets;
            else if (*scan == ',' && !parentheses && !brackets)
                ++arity;
        }
        if (*scan == ')' && arity == expected_arity)
            return 1;
        cursor += length;
    }
    return 0;
}

#if MESAGL_GLES2_PROFILE == MESAGL_GLES2_PROFILE_FULL
static int shader_integer_constant(const char *source, const char *start,
                                   const char *end, GLint *value);

static int shader_identifier_occurrences(const char *source, const char *name, size_t length)
{
    const char *cursor = source;
    int count = 0;

    while (*cursor) {
        if (!strncmp(cursor, name, length) && shader_keyword_at(source, cursor, length))
            ++count;
        ++cursor;
    }
    return count;
}

static int range_calls_function(const char *source, const char *start,
                                const char *end,
                                const LinkFunction *functions,
                                int function_count, int candidate)
{
    const char *cursor = start;
    const LinkFunction *function = &functions[candidate];
    size_t name_length = strlen(function->name);

    while (cursor < end) {
        if ((size_t)(end - cursor) >= name_length &&
            !strncmp(cursor, function->name, name_length) &&
            (cursor == start || !shader_identifier_character(cursor[-1])) &&
            (cursor + name_length == end ||
             !shader_identifier_character(cursor[name_length]))) {
            const char *open = control_space(cursor + name_length, end);

            if (*open == '(') {
                const char *close = control_matching(open, end, '(', ')');
                char argument_types[MAX_LINK_PARAMETERS][LINK_TYPE_CAPACITY] = {{0}};
                int known[MAX_LINK_PARAMETERS] = {0};
                int argument_count;
                int argument;

                if (!close)
                    return 1;
                argument_count = collect_link_argument_types(
                    source, open, close, functions, function_count,
                    argument_types, known);
                if (argument_count == function->parameter_count) {
                    int matches = 1;

                    for (argument = 0; argument < argument_count; ++argument) {
                        if (!known[argument])
                            return 1;
                        if (strcmp(argument_types[argument],
                                   function->parameters[argument])) {
                            matches = 0;
                            break;
                        }
                    }
                    if (matches)
                        return 1;
                }
                cursor = close;
            }
        }
        ++cursor;
    }
    return 0;
}

static int collect_active_functions(const char *source,
                                    LinkFunction *functions,
                                    unsigned char *reachable)
{
    const char *source_end = source + strlen(source);
    const char *cursor = source;
    int count = 0;
    int brace_depth = 0;

    while (cursor < source_end) {
        if (*cursor == '{') {
            ++brace_depth;
        } else if (*cursor == '}' && brace_depth) {
            --brace_depth;
        } else if (*cursor == '(' && !brace_depth) {
            const char *close = control_matching(cursor, source_end, '(', ')');
            const char *after;
            const char *name_end = cursor;
            const char *name_start;
            const char *return_end;
            const char *return_start;
            const char *body_end;

            if (!close)
                break;
            after = control_space(close + 1, source_end);
            if (*after != '{') {
                cursor = close + 1;
                continue;
            }
            while (name_end > source &&
                   (name_end[-1] == ' ' || name_end[-1] == '\t' ||
                    name_end[-1] == '\r' || name_end[-1] == '\n'))
                --name_end;
            name_start = name_end;
            while (name_start > source &&
                   shader_identifier_character(name_start[-1]))
                --name_start;
            return_end = name_start;
            while (return_end > source &&
                   (return_end[-1] == ' ' || return_end[-1] == '\t' ||
                    return_end[-1] == '\r' || return_end[-1] == '\n'))
                --return_end;
            return_start = return_end;
            while (return_start > source &&
                   shader_identifier_character(return_start[-1]))
                --return_start;
            body_end = control_matching(after, source_end, '{', '}');
            if (!body_end)
                break;
            if (name_start < name_end && return_start < return_end &&
                count < MAX_LINK_FUNCTIONS) {
                memset(&functions[count], 0, sizeof(functions[count]));
                functions[count].declaration_start = return_start;
                if (!copy_link_token(functions[count].name,
                                     sizeof(functions[count].name),
                                     name_start, name_end) ||
                    !copy_link_token(functions[count].return_type,
                                     sizeof(functions[count].return_type),
                                     return_start, return_end) ||
                    !parse_link_parameters(source, cursor, close,
                                           &functions[count], 0))
                    return 0;
                functions[count].body_start = after + 1;
                functions[count].body_end = body_end;
                functions[count].definition = 1;
                reachable[count] =
                    name_end - name_start == 4 &&
                    !strncmp(name_start, "main", 4);
                ++count;
            }
            cursor = body_end + 1;
            continue;
        }
        ++cursor;
    }
    return count;
}

static int build_function_reachability(const char *source,
                                       LinkFunction *functions,
                                       unsigned char *reachable)
{
    int function_count = collect_active_functions(source, functions, reachable);
    int changed;
    int caller;

    do {
        changed = 0;
        for (caller = 0; caller < function_count; ++caller) {
            int callee;

            if (!reachable[caller])
                continue;
            for (callee = 0; callee < function_count; ++callee) {
                if (!reachable[callee] &&
                    range_calls_function(source,
                                         functions[caller].body_start,
                                         functions[caller].body_end,
                                         functions, function_count, callee)) {
                    reachable[callee] = 1;
                    changed = 1;
                }
            }
        }
    } while (changed);
    return function_count;
}

static int resource_occurrence_is_unshadowed(
    const ShaderReachability *reachability, int function,
    const char *occurrence, const char *name, size_t length)
{
    const LinkFunction *link_function = &reachability->functions[function];
    const char *cursor;
    const char *before = occurrence;
    int parameter;

    while (before > link_function->body_start &&
           (before[-1] == ' ' || before[-1] == '\t' ||
            before[-1] == '\r' || before[-1] == '\n'))
        --before;
    if (before > link_function->body_start && before[-1] == '.')
        return 0;
    for (parameter = 0; parameter < link_function->parameter_count; ++parameter)
        if (strlen(link_function->parameter_names[parameter]) == length &&
            !strncmp(link_function->parameter_names[parameter], name, length))
            return 0;
    cursor = link_function->body_start;
    while (cursor <= occurrence) {
        if ((size_t)(occurrence + length - cursor) >= length &&
            !strncmp(cursor, name, length) &&
            shader_keyword_at(reachability->source, cursor, length) &&
            (link_identifier_is_declarator(reachability->source, cursor) ||
             link_identifier_is_inline_struct_declarator(
                 reachability->source, cursor)) &&
            (cursor == occurrence ||
             link_declaration_visible(reachability->source, cursor,
                                      occurrence)))
            return 0;
        ++cursor;
    }
    return 1;
}

static int shader_identifier_reachable_used(const ShaderReachability *reachability,
                                            const char *name, size_t length)
{
    const char *source = reachability->source;
    int caller;
    for (caller = 0; caller < reachability->function_count; ++caller) {
        const char *cursor;

        if (!reachability->reachable[caller])
            continue;
        cursor = reachability->functions[caller].body_start;
        while (cursor < reachability->functions[caller].body_end) {
            if ((size_t)(reachability->functions[caller].body_end - cursor) >= length &&
                !strncmp(cursor, name, length) &&
                shader_keyword_at(source, cursor, length) &&
                resource_occurrence_is_unshadowed(
                    reachability, caller, cursor, name, length))
                return 1;
            ++cursor;
        }
    }
    return 0;
}

static int shader_resource_position_reachable(
    const ShaderReachability *reachability, const char *position,
    const char *name, size_t length)
{
    int function;

    for (function = 0; function < reachability->function_count; ++function)
        if (reachability->reachable[function] &&
            position >= reachability->functions[function].body_start &&
            position < reachability->functions[function].body_end)
            return resource_occurrence_is_unshadowed(
                reachability, function, position, name, length);
    return 0;
}

static int shader_resource_position_unshadowed(
    const ShaderReachability *reachability, const char *position,
    const char *name, size_t length)
{
    int function;

    for (function = 0; function < reachability->function_count; ++function)
        if (position >= reachability->functions[function].body_start &&
            position < reachability->functions[function].body_end)
            return resource_occurrence_is_unshadowed(
                reachability, function, position, name, length);
    return 1;
}

static int uniform_index_constant_expression(const char *source,
                                             const char *start,
                                             const char *end,
                                             const char *use_position,
                                             GLint *value)
{
    static const char *const builtin_constants[] = {
        "gl_MaxVertexAttribs",
        "gl_MaxVertexUniformVectors",
        "gl_MaxVaryingVectors",
        "gl_MaxVertexTextureImageUnits",
        "gl_MaxCombinedTextureImageUnits",
        "gl_MaxTextureImageUnits",
        "gl_MaxFragmentUniformVectors",
        "gl_MaxDrawBuffers",
    };
    const char *cursor = start;

    while (cursor < end) {
        const char *name;
        const char *name_end;
        const char *after;
        size_t length;
        size_t index;
        int builtin = 0;

        if (!shader_identifier_character(*cursor) ||
            (*cursor >= '0' && *cursor <= '9')) {
            ++cursor;
            continue;
        }
        name = cursor;
        name_end = name;
        while (name_end < end && shader_identifier_character(*name_end))
            ++name_end;
        length = (size_t)(name_end - name);
        after = control_space(name_end, end);
        if (length == 3 && !strncmp(name, "int", 3) && after < end &&
            *after == '(') {
            cursor = name_end;
            continue;
        }
        for (index = 0;
             index < sizeof(builtin_constants) / sizeof(builtin_constants[0]);
             ++index)
            if (strlen(builtin_constants[index]) == length &&
                !strncmp(builtin_constants[index], name, length)) {
                builtin = 1;
                break;
            }
        if (!builtin &&
            !shader_global_const_int(source, use_position, name, length))
            return 0;
        cursor = name_end;
    }
    return shader_integer_constant(source, start, end, value);
}

static int shader_uniform_active_array_size(const ShaderReachability *reachability,
                                            const char *name, size_t length,
                                            int declared_size)
{
    const char *source = reachability->source;
    int function;
    int active_size = 0;

    if (declared_size <= 1)
        return declared_size;
    for (function = 0; function < reachability->function_count; ++function) {
        const char *cursor;

        if (!reachability->reachable[function])
            continue;
        cursor = reachability->functions[function].body_start;
        while (cursor < reachability->functions[function].body_end) {
            if ((size_t)(reachability->functions[function].body_end - cursor) >= length &&
                !strncmp(cursor, name, length) &&
                shader_keyword_at(source, cursor, length) &&
                resource_occurrence_is_unshadowed(
                    reachability, function, cursor, name, length)) {
                const char *after = control_space(cursor + length,
                                                  reachability->functions[function].body_end);

                if (*after != '[') {
                    active_size = declared_size;
                    break;
                }
                {
                    const char *close = control_matching(
                        after, reachability->functions[function].body_end, '[', ']');
                    const char *number = control_space(
                        after + 1,
                        close ? close : reachability->functions[function].body_end);
                    GLint index;

                    if (!close) {
                        active_size = declared_size;
                        break;
                    }
                    if (!uniform_index_constant_expression(
                            source, number, close, cursor, &index) ||
                        index < 0 ||
                        index >= declared_size) {
                        active_size = declared_size;
                        break;
                    }
                    if (active_size < index + 1)
                        active_size = (int)index + 1;
                    cursor = close;
                }
            }
            ++cursor;
        }
        if (active_size == declared_size)
            break;
    }
    return active_size;
}

static int shader_has_keyword(const char *source, const char *keyword)
{
    return shader_identifier_occurrences(source, keyword, strlen(keyword)) > 0;
}

static int shader_scope_depth(const char *source, const char *limit)
{
    const char *cursor = source;
    int depth = 0;

    while (cursor < limit) {
        if (cursor + 1 < limit && cursor[0] == '/' && cursor[1] == '/') {
            cursor += 2;
            while (cursor < limit && *cursor != '\n')
                ++cursor;
        } else if (cursor + 1 < limit && cursor[0] == '/' && cursor[1] == '*') {
            cursor += 2;
            while (cursor + 1 < limit && !(cursor[0] == '*' && cursor[1] == '/'))
                ++cursor;
            if (cursor + 1 < limit)
                cursor += 2;
        } else {
            if (*cursor == '{')
                ++depth;
            else if (*cursor == '}' && depth)
                --depth;
            ++cursor;
        }
    }
    return depth;
}
#endif

#if MESAGL_GLES2_PROFILE == MESAGL_GLES2_PROFILE_LITE
static int shader_resource_position_unshadowed(
    const ShaderReachability *reachability, const char *position,
    const char *name, size_t length)
{
    (void)reachability;
    (void)position;
    (void)name;
    (void)length;
    return 1;
}

static int shader_identifier_reachable_used(const ShaderReachability *reachability,
                                            const char *name, size_t length)
{
    const char *source = reachability->source;
    const char *cursor = source;
    int occurrences = 0;

    while (*cursor) {
        if (!strncmp(cursor, name, length) &&
            shader_keyword_at(source, cursor, length) && ++occurrences > 1)
            return 1;
        ++cursor;
    }
    return 0;
}
#endif

static int initialize_shader_reachability(ShaderReachability *reachability,
                                          const char *source)
{
    memset(reachability, 0, sizeof(*reachability));
    reachability->source = source;
#if MESAGL_GLES2_PROFILE == MESAGL_GLES2_PROFILE_FULL
    reachability->functions = ntglAlloc(MAX_LINK_FUNCTIONS *
                                        sizeof(*reachability->functions));
    if (!reachability->functions)
        return 0;
    reachability->function_count = build_function_reachability(
        source, reachability->functions, reachability->reachable);
#endif
    return 1;
}

static void destroy_shader_reachability(ShaderReachability *reachability)
{
    ntglFree(reachability->functions);
    reachability->functions = NULL;
}

static int shader_identifier_written_scoped(
    const char *source, const ShaderReachability *reachability,
    const char *name, size_t length, const char *declaration)
{
    const char *cursor = source;

    while (*cursor) {
        const char *after;
        const char *before;

        if (cursor[0] == '/' && cursor[1] == '/') {
            cursor += 2;
            while (*cursor && *cursor != '\n')
                ++cursor;
            continue;
        }
        if (cursor[0] == '/' && cursor[1] == '*') {
            cursor += 2;
            while (*cursor && !(cursor[0] == '*' && cursor[1] == '/'))
                ++cursor;
            if (*cursor)
                cursor += 2;
            continue;
        }
        if (strncmp(cursor, name, length) || cursor == declaration ||
            !shader_keyword_at(source, cursor, length)) {
            ++cursor;
            continue;
        }
        if (reachability && !shader_resource_position_unshadowed(
                                reachability, cursor, name, length)) {
            cursor += length;
            continue;
        }
        after = skip_shader_space(cursor + length);
        while (*after == '[' || *after == '.') {
            if (*after == '[') {
                int depth = 1;

                ++after;
                while (*after && depth) {
                    if (*after == '[')
                        ++depth;
                    else if (*after == ']')
                        --depth;
                    ++after;
                }
            } else {
                ++after;
                while ((*after >= 'a' && *after <= 'z') ||
                       (*after >= 'A' && *after <= 'Z') ||
                       (*after >= '0' && *after <= '9') || *after == '_')
                    ++after;
            }
            after = skip_shader_space(after);
        }
        before = cursor;
        while (before > source && (before[-1] == ' ' || before[-1] == '\t' ||
                                   before[-1] == '\r' || before[-1] == '\n'))
            --before;
        if ((after[0] == '=' && after[1] != '=') ||
            ((after[0] == '+' || after[0] == '-' || after[0] == '*' || after[0] == '/') &&
             after[1] == '=') ||
            (after[0] == '+' && after[1] == '+') ||
            (after[0] == '-' && after[1] == '-') ||
            (before - source >= 2 &&
             ((before[-2] == '+' && before[-1] == '+') ||
              (before[-2] == '-' && before[-1] == '-'))))
            return 1;
        ++cursor;
    }
    return 0;
}

static int shader_identifier_written(const char *source, const char *name,
                                     size_t length, const char *declaration)
{
    return shader_identifier_written_scoped(source, NULL, name, length,
                                            declaration);
}

static int shader_read_only_builtin_written(const char *source)
{
    static const char *const names[] = {
        "gl_DepthRange",
        "gl_FragCoord",
        "gl_FrontFacing",
        "gl_PointCoord",
        "gl_MaxVertexAttribs",
        "gl_MaxVertexUniformVectors",
        "gl_MaxVaryingVectors",
        "gl_MaxVertexTextureImageUnits",
        "gl_MaxCombinedTextureImageUnits",
        "gl_MaxTextureImageUnits",
        "gl_MaxFragmentUniformVectors",
        "gl_MaxDrawBuffers",
    };
    size_t index;

    for (index = 0; index < sizeof(names) / sizeof(names[0]); ++index)
        if (shader_identifier_written(source, names[index], strlen(names[index]), NULL))
            return 1;
    return 0;
}

static UniformType uniform_type(const char *name, size_t length)
{
    if (length == 5 && !strncmp(name, "float", length))
        return UNIFORM_FLOAT;
    if (length == 4 && !strncmp(name, "vec2", length))
        return UNIFORM_VEC2;
    if (length == 4 && !strncmp(name, "vec3", length))
        return UNIFORM_VEC3;
    if (length == 4 && !strncmp(name, "vec4", length))
        return UNIFORM_VEC4;
    if (length == 3 && !strncmp(name, "int", length))
        return UNIFORM_INT;
    if (length == 5 && !strncmp(name, "ivec2", length))
        return UNIFORM_IVEC2;
    if (length == 5 && !strncmp(name, "ivec3", length))
        return UNIFORM_IVEC3;
    if (length == 5 && !strncmp(name, "ivec4", length))
        return UNIFORM_IVEC4;
    if (length == 4 && !strncmp(name, "bool", length))
        return UNIFORM_BOOL;
    if (length == 5 && !strncmp(name, "bvec2", length))
        return UNIFORM_BVEC2;
    if (length == 5 && !strncmp(name, "bvec3", length))
        return UNIFORM_BVEC3;
    if (length == 5 && !strncmp(name, "bvec4", length))
        return UNIFORM_BVEC4;
    if (length == 4 && !strncmp(name, "mat4", length))
        return UNIFORM_MAT4;
    if (length == 4 && !strncmp(name, "mat2", length))
        return UNIFORM_MAT2;
    if (length == 4 && !strncmp(name, "mat3", length))
        return UNIFORM_MAT3;
    if (length == 9 && !strncmp(name, "sampler2D", length))
        return UNIFORM_SAMPLER2D;
    if (length == 11 && !strncmp(name, "samplerCube", length))
        return UNIFORM_SAMPLERCUBE;
    return UNIFORM_OTHER;
}

#if MESAGL_GLES2_PROFILE == MESAGL_GLES2_PROFILE_FULL
static int shader_integer_constant(const char *source, const char *start, const char *end,
                                   GLint *value);

static GLenum attribute_type(const char *name, size_t length)
{
    if (length == 5 && !strncmp(name, "float", length))
        return GL_FLOAT;
    if (length == 4 && !strncmp(name, "vec2", length))
        return GL_FLOAT_VEC2;
    if (length == 4 && !strncmp(name, "vec3", length))
        return GL_FLOAT_VEC3;
    if (length == 4 && !strncmp(name, "vec4", length))
        return GL_FLOAT_VEC4;
    if (length == 4 && !strncmp(name, "mat2", length))
        return GL_FLOAT_MAT2;
    if (length == 4 && !strncmp(name, "mat3", length))
        return GL_FLOAT_MAT3;
    if (length == 4 && !strncmp(name, "mat4", length))
        return GL_FLOAT_MAT4;
    return 0;
}

static int attribute_slots(GLenum type)
{
    return type == GL_FLOAT_MAT2   ? 2
           : type == GL_FLOAT_MAT3 ? 3
           : type == GL_FLOAT_MAT4 ? 4
                                   : 1;
}

static int varying_slots(GLenum type)
{
    return attribute_slots(type);
}

static int shader_type_rows(GLenum type)
{
    if (type == GL_FLOAT_VEC2 || type == GL_FLOAT_MAT2)
        return 2;
    if (type == GL_FLOAT_VEC3 || type == GL_FLOAT_MAT3)
        return 3;
    if (type == GL_FLOAT_VEC4 || type == GL_FLOAT_MAT4)
        return 4;
    return 1;
}

static int binding_range_used(const Program *program, GLuint index, int slots, int ignored)
{
    int i;

    for (i = 0; i < program->binding_count; ++i)
        if (i != ignored && program->bindings[i].active &&
            index < program->bindings[i].index +
                                      (GLuint)attribute_slots(program->bindings[i].type) &&
            index + (GLuint)slots > program->bindings[i].index)
            return 1;
    return 0;
}

static int add_program_attribute(Program *program, const char *source,
                                 const ShaderReachability *reachability,
                                 const char *name_start, const char *name_end,
                                 GLenum type)
{
    Binding *binding;
    GLuint index;
    int i;

    if (shader_identifier_written_scoped(
            source, reachability, name_start,
            (size_t)(name_end - name_start), name_start)) {
        strcpy(program->log, "vertex attribute is read-only");
        return 0;
    }
    if (!shader_identifier_reachable_used(reachability, name_start,
                                          (size_t)(name_end - name_start)))
        return 1;
    for (i = 0; i < program->binding_count; ++i)
        if (strlen(program->bindings[i].name) == (size_t)(name_end - name_start) &&
            !strncmp(program->bindings[i].name, name_start,
                     (size_t)(name_end - name_start)))
            break;
    if (i < program->binding_count) {
        program->bindings[i].type = type;
        program->bindings[i].active = 1;
        if (program->bindings[i].index + (GLuint)attribute_slots(type) > MAX_ATTRIBUTES ||
            binding_range_used(program, program->bindings[i].index,
                               attribute_slots(type), i)) {
            strcpy(program->log, "vertex attribute location conflict");
            return 0;
        }
        return 1;
    }
    if (program->binding_count >= MAX_BINDINGS) {
        strcpy(program->log, "vertex attribute limit exceeded");
        return 0;
    }
    for (index = 0;
         index + (GLuint)attribute_slots(type) <= MAX_ATTRIBUTES &&
         binding_range_used(program, index, attribute_slots(type), -1);
         ++index) {
    }
    if (index + (GLuint)attribute_slots(type) > MAX_ATTRIBUTES) {
        strcpy(program->log, "vertex attribute location limit exceeded");
        return 0;
    }
    binding = &program->bindings[program->binding_count++];
    memset(binding, 0, sizeof(*binding));
    memcpy(binding->name, name_start,
           (size_t)(name_end - name_start) < sizeof(binding->name) - 1
               ? (size_t)(name_end - name_start)
               : sizeof(binding->name) - 1);
    binding->index = index;
    binding->type = type;
    binding->active = 1;
    return 1;
}

static int collect_attributes(Program *program, const char *source,
                              const ShaderReachability *reachability)
{
    const char *cursor = source;
    int retained = 0;
    int i;

    for (i = 0; i < program->binding_count; ++i) {
        if (!program->bindings[i].requested)
            continue;
        program->bindings[retained] = program->bindings[i];
        program->bindings[retained].active = 0;
        ++retained;
    }
    program->binding_count = retained;

    while (cursor && (cursor = strstr(cursor, "attribute")) != NULL) {
        const char *type_start = cursor + 9;
        const char *type_end;
        const char *declarator;
        const char *statement_end;
        GLenum type;

        if (!shader_keyword_at(source, cursor, 9)) {
            cursor += 9;
            continue;
        }
        if (shader_scope_depth(source, cursor)) {
            strcpy(program->log, "vertex attribute must have global scope");
            return 0;
        }

        while (*type_start == ' ' || *type_start == '\t' || *type_start == '\n' ||
               *type_start == '\r')
            ++type_start;
        type_start = skip_precision(type_start);
        type_end = type_start;
        while ((*type_end >= 'a' && *type_end <= 'z') || (*type_end >= 'A' && *type_end <= 'Z') ||
               (*type_end >= '0' && *type_end <= '9') || *type_end == '_')
            ++type_end;
        type = attribute_type(type_start, (size_t)(type_end - type_start));
        statement_end = strchr(type_end, ';');
        if (!type || !statement_end) {
            strcpy(program->log, "unsupported or malformed vertex attribute declaration");
            return 0;
        }
        declarator = type_end;
        while (declarator < statement_end) {
            const char *name_start = skip_shader_space(declarator);
            const char *name_end = name_start;
            const char *after;

            while ((*name_end >= 'a' && *name_end <= 'z') ||
                   (*name_end >= 'A' && *name_end <= 'Z') ||
                   (*name_end >= '0' && *name_end <= '9') || *name_end == '_')
                ++name_end;
            if (name_end == name_start) {
                strcpy(program->log, "malformed vertex attribute declarator list");
                return 0;
            }
            after = skip_shader_space(name_end);
            if (*after == '[') {
                strcpy(program->log, "vertex attribute cannot have array type in GLSL ES 1.00");
                return 0;
            }
            if (!add_program_attribute(program, source, reachability,
                                       name_start, name_end, type))
                return 0;
            if (after == statement_end)
                break;
            if (*after != ',') {
                strcpy(program->log, "malformed vertex attribute declarator list");
                return 0;
            }
            declarator = after + 1;
        }
        cursor = statement_end + 1;
    }
    return 1;
}

static int parse_varying_array_size(const char *source, const char *cursor,
                                    const char **after, GLint *array_size)
{
    cursor = skip_shader_space(cursor);
    *array_size = 1;
    if (*cursor == '[') {
        const char *array_end = strchr(cursor + 1, ']');

        if (!array_end ||
            !shader_integer_constant(source, cursor + 1, array_end, array_size) ||
            *array_size < 1) {
            return 0;
        }
        cursor = array_end + 1;
    }
    *after = skip_shader_space(cursor);
    return 1;
}

static int add_program_varying(Program *program, const char *name_start,
                               const char *name_end,
                               GLenum type, GLint array_size, int invariant)
{
    Varying *varying;
    int existing;

    for (existing = 0; existing < program->varying_count; ++existing)
        if (strlen(program->varyings[existing].name) ==
                (size_t)(name_end - name_start) &&
            !strncmp(program->varyings[existing].name, name_start,
                     (size_t)(name_end - name_start)))
            break;
    if (existing < program->varying_count) {
        if (program->varyings[existing].type != type ||
            program->varyings[existing].size != array_size ||
            program->varyings[existing].invariant != invariant) {
            strcpy(program->log, "vertex varying is redeclared with a different type");
            return 0;
        }
        return 1;
    }
    if (program->varying_count >= MESAGL_MAX_VARYING_DECLARATIONS) {
        strcpy(program->log, "varying declaration limit exceeded");
        return 0;
    }
    varying = &program->varyings[program->varying_count++];
    memset(varying, 0, sizeof(*varying));
    memcpy(varying->name, name_start,
           (size_t)(name_end - name_start) < sizeof(varying->name) - 1
               ? (size_t)(name_end - name_start)
               : sizeof(varying->name) - 1);
    varying->type = type;
    varying->size = array_size;
    varying->invariant = invariant;
    return 1;
}

static int collect_varyings(Program *program, const char *source, int invariant_all)
{
    const char *cursor = source;

    program->varying_count = 0;
    program->varying_slot_count = 0;
    while (cursor && (cursor = strstr(cursor, "varying")) != NULL) {
        const char *type_start = cursor + 7;
        const char *type_end;
        const char *declarator;
        const char *statement_end;
        GLenum type;
        int invariant;

        if (!shader_keyword_at(source, cursor, 7)) {
            cursor += 7;
            continue;
        }
        invariant = invariant_all || varying_declaration_is_invariant(source, cursor);

        while (*type_start == ' ' || *type_start == '\t' || *type_start == '\r' ||
               *type_start == '\n')
            ++type_start;
        type_start = skip_precision(type_start);
        type_end = type_start;
        while ((*type_end >= 'a' && *type_end <= 'z') || (*type_end >= 'A' && *type_end <= 'Z') ||
               (*type_end >= '0' && *type_end <= '9') || *type_end == '_')
            ++type_end;
        type = attribute_type(type_start, (size_t)(type_end - type_start));
        statement_end = strchr(type_end, ';');
        if (!type || !statement_end) {
            strcpy(program->log, "unsupported or malformed vertex varying declaration");
            return 0;
        }
        declarator = type_end;
        while (declarator < statement_end) {
            const char *name_start = skip_shader_space(declarator);
            const char *name_end = name_start;
            const char *after;
            GLint array_size;

            while ((*name_end >= 'a' && *name_end <= 'z') ||
                   (*name_end >= 'A' && *name_end <= 'Z') ||
                   (*name_end >= '0' && *name_end <= '9') || *name_end == '_')
                ++name_end;
            if (name_end == name_start ||
                !parse_varying_array_size(source, name_end, &after, &array_size)) {
                strcpy(program->log, "invalid vertex varying declarator");
                return 0;
            }
            if (!add_program_varying(program, name_start, name_end, type,
                                     array_size, invariant))
                return 0;
            if (after == statement_end)
                break;
            if (*after != ',') {
                strcpy(program->log, "malformed vertex varying declarator list");
                return 0;
            }
            declarator = after + 1;
        }
        cursor = statement_end + 1;
    }
    return 1;
}

static int validate_fragment_varying(Program *program, const char *source,
                                     const ShaderReachability *reachability,
                                     const char *name_start, const char *name_end,
                                     GLenum type, GLint array_size, int invariant)
{
    int i;

    for (i = 0; i < program->varying_count; ++i)
        if (strlen(program->varyings[i].name) ==
                (size_t)(name_end - name_start) &&
            !strncmp(program->varyings[i].name, name_start,
                     (size_t)(name_end - name_start)))
            break;
    if (i < program->varying_count &&
        program->varyings[i].invariant != invariant) {
        strcpy(program->log,
               "vertex and fragment varying invariance qualifiers must match");
        return 0;
    }
    if (!shader_identifier_reachable_used(reachability, name_start,
                                          (size_t)(name_end - name_start)))
        return 1;

    if (shader_identifier_written_scoped(
            source, reachability, name_start,
            (size_t)(name_end - name_start), name_start)) {
        strcpy(program->log, "fragment varying is read-only");
        return 0;
    }
    if (i == program->varying_count || program->varyings[i].type != type ||
        program->varyings[i].size != array_size) {
        strcpy(program->log, "fragment varying is missing or has a different vertex type");
        return 0;
    }
    program->varyings[i].active = 1;
    return 1;
}

typedef struct VaryingPackRectangle {
    int width;
    int height;
} VaryingPackRectangle;

static void varying_pack_dimensions(const Varying *varying, int *width,
                                    int *height)
{
    if (varying->type == GL_FLOAT_MAT4) {
        *width = 4;
        *height = 4 * varying->size;
    } else if (varying->type == GL_FLOAT_MAT3) {
        *width = 3;
        *height = 3 * varying->size;
    } else if (varying->type == GL_FLOAT_MAT2) {
        *width = 2;
        *height = 2 * varying->size;
    } else if (varying->type == GL_FLOAT_VEC4) {
        *width = 4;
        *height = varying->size;
    } else if (varying->type == GL_FLOAT_VEC3) {
        *width = 3;
        *height = varying->size;
    } else if (varying->type == GL_FLOAT_VEC2) {
        *width = 2;
        *height = varying->size;
    } else {
        *width = 1;
        *height = varying->size;
    }
}

static int varying_pack_rectangles(
    const VaryingPackRectangle *rectangles, int rectangle_count, int index,
    unsigned char occupied[MESAGL_MAX_VARYING_VECTORS][4])
{
    const VaryingPackRectangle *rectangle;
    int row;
    int column;

    if (index == rectangle_count)
        return 1;
    rectangle = &rectangles[index];
    for (row = 0; row + rectangle->height <= MESAGL_MAX_VARYING_VECTORS;
         ++row) {
        for (column = 0; column + rectangle->width <= 4; ++column) {
            int y;
            int x;
            int available = 1;

            for (y = row; y < row + rectangle->height && available; ++y)
                for (x = column; x < column + rectangle->width; ++x)
                    if (occupied[y][x]) {
                        available = 0;
                        break;
                    }
            if (!available)
                continue;
            for (y = row; y < row + rectangle->height; ++y)
                for (x = column; x < column + rectangle->width; ++x)
                    occupied[y][x] = 1;
            if (varying_pack_rectangles(rectangles, rectangle_count,
                                        index + 1, occupied))
                return 1;
            for (y = row; y < row + rectangle->height; ++y)
                for (x = column; x < column + rectangle->width; ++x)
                    occupied[y][x] = 0;
        }
    }
    return 0;
}

static int finalize_active_varyings(Program *program)
{
    VaryingPackRectangle rectangles[MESAGL_MAX_VARYING_DECLARATIONS];
    unsigned char occupied[MESAGL_MAX_VARYING_VECTORS][4] = {{0}};
    int rectangle_count = 0;
    int area = 0;
    int source;
    int slots = 0;

    for (source = 0; source < program->varying_count; ++source) {
        Varying *varying = &program->varyings[source];
        int varying_size;

        if (!varying->active) {
            varying->slot = -1;
            continue;
        }
        varying_size = varying_slots(varying->type) * varying->size;
        if (slots + varying_size > MESAGL_MAX_VARYING_INTERPOLATORS) {
            strcpy(program->log, "varying vector limit exceeded");
            return 0;
        }
        varying_pack_dimensions(varying,
                                &rectangles[rectangle_count].width,
                                &rectangles[rectangle_count].height);
        area += rectangles[rectangle_count].width *
                rectangles[rectangle_count].height;
        ++rectangle_count;
        varying->slot = slots;
        slots += varying_size;
    }
    for (source = 1; source < rectangle_count; ++source) {
        VaryingPackRectangle rectangle = rectangles[source];
        int destination = source;
        int rectangle_area = rectangle.width * rectangle.height;

        while (destination > 0) {
            VaryingPackRectangle *previous = &rectangles[destination - 1];
            int previous_area = previous->width * previous->height;

            if (previous_area > rectangle_area ||
                (previous_area == rectangle_area &&
                 previous->width >= rectangle.width))
                break;
            rectangles[destination] = *previous;
            --destination;
        }
        rectangles[destination] = rectangle;
    }
    if (area > MESAGL_MAX_VARYING_VECTORS * 4 ||
        !varying_pack_rectangles(rectangles, rectangle_count, 0, occupied)) {
        strcpy(program->log, "varying vector limit exceeded");
        return 0;
    }
    program->varying_slot_count = slots;
    return 1;
}

static int validate_fragment_varyings(Program *program, const char *source,
                                      const ShaderReachability *reachability)
{
    const char *cursor = source;

    while (cursor && (cursor = strstr(cursor, "varying")) != NULL) {
        const char *type_start = cursor + 7;
        const char *type_end;
        const char *declarator;
        const char *statement_end;
        GLenum type;
        int invariant;

        if (!shader_keyword_at(source, cursor, 7)) {
            cursor += 7;
            continue;
        }
        invariant = varying_declaration_is_invariant(source, cursor);
        while (*type_start == ' ' || *type_start == '\t' || *type_start == '\r' ||
               *type_start == '\n')
            ++type_start;
        type_start = skip_precision(type_start);
        type_end = type_start;
        while ((*type_end >= 'a' && *type_end <= 'z') ||
               (*type_end >= 'A' && *type_end <= 'Z') ||
               (*type_end >= '0' && *type_end <= '9') || *type_end == '_')
            ++type_end;
        type = attribute_type(type_start, (size_t)(type_end - type_start));
        statement_end = strchr(type_end, ';');
        if (!type || !statement_end) {
            strcpy(program->log, "unsupported fragment varying declaration");
            return 0;
        }
        declarator = type_end;
        while (declarator < statement_end) {
            const char *name_start = skip_shader_space(declarator);
            const char *name_end = name_start;
            const char *after;
            GLint array_size;

            while ((*name_end >= 'a' && *name_end <= 'z') ||
                   (*name_end >= 'A' && *name_end <= 'Z') ||
                   (*name_end >= '0' && *name_end <= '9') || *name_end == '_')
                ++name_end;
            if (name_end == name_start ||
                !parse_varying_array_size(source, name_end, &after, &array_size)) {
                strcpy(program->log, "invalid fragment varying declarator");
                return 0;
            }
            if (!validate_fragment_varying(program, source, reachability,
                                            name_start, name_end, type,
                                            array_size, invariant))
                return 0;
            if (after == statement_end)
                break;
            if (*after != ',') {
                strcpy(program->log, "malformed fragment varying declarator list");
                return 0;
            }
            declarator = after + 1;
        }
        cursor = statement_end + 1;
    }
    return 1;
}
#endif

#if MESAGL_GLES2_PROFILE == MESAGL_GLES2_PROFILE_FULL
typedef struct ConstantOutput {
    MesaGLSLValue value;
    int assigned;
} ConstantOutput;

static int assign_constant_output(void *user, const char *name, size_t length,
                                  const char *swizzle, size_t swizzle_length,
                                  int array_index, const MesaGLSLValue *value)
{
    ConstantOutput *output = (ConstantOutput *)user;

    if (length != 6 || strncmp(name, "output", length) || swizzle || swizzle_length ||
        array_index >= 0)
        return 0;
    output->value = *value;
    output->assigned = 1;
    return 1;
}

static int shader_integer_constant(const char *source, const char *start, const char *end,
                                   GLint *value)
{
    static const char prefix[] = "output = ";
    ConstantOutput output;
    char *body;
    size_t expression_length = (size_t)(end - start);
    size_t body_length = sizeof(prefix) - 1 + expression_length + 1;
    int discarded = 0;
    int success;

    body = (char *)ntglAlloc(body_length + 1);
    if (!body)
        return 0;
    memcpy(body, prefix, sizeof(prefix) - 1);
    memcpy(body + sizeof(prefix) - 1, start, expression_length);
    body[body_length - 1] = ';';
    body[body_length] = '\0';
    memset(&output, 0, sizeof(output));
    success = mesaGLSLExecuteProgram(source, body, NULL, NULL, assign_constant_output,
                                     &output, &discarded, NULL);
    ntglFree(body);
    if (!success || discarded || !output.assigned || output.value.rows != 1 ||
        output.value.columns != 1 || output.value.type != MESAGL_GLSL_TYPE_INT ||
        output.value.data[0] != (float)(GLint)output.value.data[0])
        return 0;
    *value = (GLint)output.value.data[0];
    return 1;
}
#endif

static int validate_fragment_data_indices(Program *program, const char *source)
{
    const char *cursor = source;
    static const char name[] = "gl_FragData";

    while (*cursor) {
        const char *open;
        const char *close;
        const char *scan;
        int depth;
        GLint index = -1;

        if (cursor[0] == '/' && cursor[1] == '/') {
            cursor += 2;
            while (*cursor && *cursor != '\n')
                ++cursor;
            continue;
        }
        if (cursor[0] == '/' && cursor[1] == '*') {
            cursor += 2;
            while (*cursor && !(cursor[0] == '*' && cursor[1] == '/'))
                ++cursor;
            if (*cursor)
                cursor += 2;
            continue;
        }
        if (strncmp(cursor, name, sizeof(name) - 1) ||
            !shader_keyword_at(source, cursor, sizeof(name) - 1)) {
            ++cursor;
            continue;
        }
        open = skip_shader_space(cursor + sizeof(name) - 1);
        if (*open != '[') {
            strcpy(program->log, "gl_FragData requires a constant array index");
            return 0;
        }
        depth = 1;
        scan = open + 1;
        while (*scan && depth) {
            if (*scan == '[')
                ++depth;
            else if (*scan == ']')
                --depth;
            ++scan;
        }
        if (depth) {
            strcpy(program->log, "unterminated gl_FragData array index");
            return 0;
        }
        close = scan - 1;
#if MESAGL_GLES2_PROFILE == MESAGL_GLES2_PROFILE_FULL
        if (!shader_integer_constant(source, open + 1, close, &index)) {
            strcpy(program->log, "gl_FragData index must be a constant integer");
            return 0;
        }
#else
        {
            const char *number = skip_shader_space(open + 1);
            const char *after = number;
            const char *finish;

            while (after < close && *after >= '0' && *after <= '9')
                ++after;
            finish = after;
            while (finish < close && (*finish == ' ' || *finish == '\t' ||
                                      *finish == '\r' || *finish == '\n'))
                ++finish;
            if (number == after || finish != close) {
                strcpy(program->log,
                       "LITE gl_FragData index must be an integer literal");
                return 0;
            }
            index = 0;
            while (number < after)
                index = index * 10 + (*number++ - '0');
        }
#endif
        if (index != 0) {
            strcpy(program->log, "gl_FragData index exceeds gl_MaxDrawBuffers");
            return 0;
        }
        cursor = scan;
    }
    return 1;
}

static int add_program_uniform(Program *program, const char *name_start,
                               const char *name_end, UniformType declared_type,
                               GLint array_size, int array_declared)
{
    Uniform *uniform;
    GLint next_location = 1;
    int i;

    for (i = 0; i < program->uniform_count; ++i)
        if (strlen(program->uniforms[i].name) == (size_t)(name_end - name_start) &&
            !strncmp(program->uniforms[i].name, name_start,
                     (size_t)(name_end - name_start)))
            break;
    if (i != program->uniform_count) {
        if (program->uniforms[i].type != declared_type ||
            program->uniforms[i].size != array_size ||
            program->uniforms[i].array_declared != array_declared) {
            strcpy(program->log, "uniform type or array size differs between shaders");
            return 0;
        }
        return 1;
    }
    if (program->uniform_count >= MESAGL_MAX_SHADER_UNIFORMS) {
        strcpy(program->log, "uniform limit exceeded");
        return 0;
    }
    uniform = &program->uniforms[program->uniform_count++];
    memset(uniform, 0, sizeof(*uniform));
    uniform->aggregate_element = -1;
    memcpy(uniform->name, name_start,
           (size_t)(name_end - name_start) < sizeof(uniform->name) - 1
               ? (size_t)(name_end - name_start)
               : sizeof(uniform->name) - 1);
    uniform->type = declared_type;
    uniform->size = array_size;
    uniform->array_declared = array_declared;
    for (i = 0; i < program->uniform_count - 1; ++i) {
        GLint after = program->uniforms[i].location + program->uniforms[i].size;

        if (after > next_location)
            next_location = after;
    }
    uniform->location = next_location;
    if (uniform->location + uniform->size - 1 >
        MESAGL_MAX_SHADER_UNIFORM_STORAGE) {
        --program->uniform_count;
        strcpy(program->log, "uniform storage limit exceeded");
        return 0;
    }
    if (array_size > 1) {
        uniform->array_value = (float *)ntglAlloc((size_t)array_size * 16 * sizeof(float));
        uniform->array_integer =
            (GLint *)ntglAlloc((size_t)array_size * 4 * sizeof(GLint));
        if (!uniform->array_value || !uniform->array_integer) {
            ntglFree(uniform->array_value);
            ntglFree(uniform->array_integer);
            uniform->array_value = NULL;
            uniform->array_integer = NULL;
            --program->uniform_count;
            strcpy(program->log, "out of memory while linking uniform array");
            return 0;
        }
        memset(uniform->array_value, 0, (size_t)array_size * 16 * sizeof(float));
        memset(uniform->array_integer, 0, (size_t)array_size * 4 * sizeof(GLint));
    }
    return 1;
}

static int parse_uniform_array_size(const char *source, const char *cursor,
                                    const char **after, GLint *array_size)
{
#if MESAGL_GLES2_PROFILE == MESAGL_GLES2_PROFILE_LITE
    (void)source;
#endif
    cursor = skip_shader_space(cursor);
    *array_size = 1;
    if (*cursor == '[') {
        const char *array_end = strchr(cursor + 1, ']');
        long parsed = 0;

#if MESAGL_GLES2_PROFILE == MESAGL_GLES2_PROFILE_FULL
        {
            GLint constant_value;

            if (array_end && shader_integer_constant(source, cursor + 1, array_end,
                                                     &constant_value))
                parsed = constant_value;
        }
#else
        if (array_end) {
            char *literal_end;

            parsed = strtol(cursor + 1, &literal_end, 10);
            while (literal_end < array_end &&
                   (*literal_end == ' ' || *literal_end == '\t' ||
                    *literal_end == '\n' || *literal_end == '\r'))
                ++literal_end;
            if (literal_end != array_end)
                parsed = 0;
        }
#endif
        if (!array_end || parsed < 1 || parsed > MESAGL_MAX_SHADER_ARRAY_ELEMENTS)
            return 0;
        *array_size = (GLint)parsed;
        cursor = array_end + 1;
    }
    *after = skip_shader_space(cursor);
    return 1;
}

static int uniform_vector_slots(UniformType type)
{
    if (type == UNIFORM_MAT2)
        return 2;
    if (type == UNIFORM_MAT3)
        return 3;
    if (type == UNIFORM_MAT4)
        return 4;
    return 1;
}

typedef struct UniformPackRectangle {
    int width;
    int height;
} UniformPackRectangle;

typedef struct UniformPackState {
    UniformPackRectangle rectangles[MESAGL_MAX_SHADER_UNIFORMS];
    int rectangle_count;
    int area;
    int vector_count;
    int vector_limit;
} UniformPackState;

static void uniform_pack_dimensions(UniformType type, int size, int *width,
                                    int *height)
{
    if (type == UNIFORM_MAT4) {
        *width = 4;
        *height = 4 * size;
    } else if (type == UNIFORM_MAT3) {
        *width = 3;
        *height = 3 * size;
    } else if (type == UNIFORM_MAT2) {
        *width = 4;
        *height = 2 * size;
    } else if (type == UNIFORM_VEC4 || type == UNIFORM_IVEC4 ||
               type == UNIFORM_BVEC4) {
        *width = 4;
        *height = size;
    } else if (type == UNIFORM_VEC3 || type == UNIFORM_IVEC3 ||
               type == UNIFORM_BVEC3) {
        *width = 3;
        *height = size;
    } else if (type == UNIFORM_VEC2 || type == UNIFORM_IVEC2 ||
               type == UNIFORM_BVEC2) {
        *width = 2;
        *height = size;
    } else {
        *width = 1;
        *height = size;
    }
}

static int add_uniform_pack_rectangle(UniformPackState *state,
                                      UniformType type, int size)
{
    UniformPackRectangle *rectangle;
    int slots = uniform_vector_slots(type) * size;

    if (state->rectangle_count >= MESAGL_MAX_SHADER_UNIFORMS)
        return 0;
    rectangle = &state->rectangles[state->rectangle_count++];
    uniform_pack_dimensions(type, size, &rectangle->width,
                            &rectangle->height);
    state->area += rectangle->width * rectangle->height;
    state->vector_count += slots;
    return 1;
}

#if MESAGL_GLES2_PROFILE == MESAGL_GLES2_PROFILE_FULL
static int uniform_pack_rectangles(
    const UniformPackRectangle *rectangles, int rectangle_count, int index,
    int row_limit,
    unsigned char occupied[MESAGL_MAX_SHADER_UNIFORM_STORAGE][4])
{
    const UniformPackRectangle *rectangle;
    int row;
    int column;

    if (index == rectangle_count)
        return 1;
    rectangle = &rectangles[index];
    for (row = 0; row + rectangle->height <= row_limit; ++row) {
        for (column = 0; column + rectangle->width <= 4; ++column) {
            int y;
            int x;
            int available = 1;

            for (y = row; y < row + rectangle->height && available; ++y)
                for (x = column; x < column + rectangle->width; ++x)
                    if (occupied[y][x]) {
                        available = 0;
                        break;
                    }
            if (!available)
                continue;
            for (y = row; y < row + rectangle->height; ++y)
                for (x = column; x < column + rectangle->width; ++x)
                    occupied[y][x] = 1;
            if (uniform_pack_rectangles(rectangles, rectangle_count,
                                        index + 1, row_limit, occupied))
                return 1;
            for (y = row; y < row + rectangle->height; ++y)
                for (x = column; x < column + rectangle->width; ++x)
                    occupied[y][x] = 0;
        }
    }
    return 0;
}

static int finalize_uniform_pack(Program *program, UniformPackState *state)
{
    unsigned char occupied[MESAGL_MAX_SHADER_UNIFORM_STORAGE][4] = {{0}};
    int source;

    for (source = 1; source < state->rectangle_count; ++source) {
        UniformPackRectangle rectangle = state->rectangles[source];
        int destination = source;
        int rectangle_area = rectangle.width * rectangle.height;

        while (destination > 0) {
            UniformPackRectangle *previous =
                &state->rectangles[destination - 1];
            int previous_area = previous->width * previous->height;

            if (previous_area > rectangle_area ||
                (previous_area == rectangle_area &&
                 previous->width >= rectangle.width))
                break;
            state->rectangles[destination] = *previous;
            --destination;
        }
        state->rectangles[destination] = rectangle;
    }
    if (state->area > state->vector_limit * 4 ||
        !uniform_pack_rectangles(state->rectangles, state->rectangle_count, 0,
                                 state->vector_limit, occupied)) {
        strcpy(program->log, "stage uniform vector limit exceeded");
        return 0;
    }
    return 1;
}
#endif

#if MESAGL_GLES2_PROFILE == MESAGL_GLES2_PROFILE_FULL
typedef struct StructUniformMember {
    char name[MESAGL_MAX_SHADER_IDENTIFIER_LENGTH];
    UniformType type;
    GLint size;
    int precision;
} StructUniformMember;

typedef struct InterfaceUniform {
    char name[MESAGL_MAX_SHADER_IDENTIFIER_LENGTH];
    UniformType type;
    GLint size;
    int precision;
    uint64_t structure_signature;
} InterfaceUniform;

static int uniform_precision(const char *cursor);
static int default_precision_for_type(const char *source, const char *type_name,
                                      GLenum stage);

static uint64_t struct_uniform_signature(const char *source, GLenum stage,
                                         const StructUniformMember *members,
                                         int member_count)
{
    uint64_t signature = UINT64_C(1469598103934665603);
    int member;

    for (member = 0; member < member_count; ++member) {
        const unsigned char *name = (const unsigned char *)members[member].name;

        while (*name) {
            signature ^= *name++;
            signature *= UINT64_C(1099511628211);
        }
        signature ^= (uint64_t)members[member].type;
        signature *= UINT64_C(1099511628211);
        signature ^= (uint64_t)members[member].size;
        signature *= UINT64_C(1099511628211);
        signature ^= (uint64_t)(members[member].precision
                                    ? members[member].precision
                                : members[member].type == UNIFORM_INT ||
                                          members[member].type == UNIFORM_IVEC2 ||
                                          members[member].type == UNIFORM_IVEC3 ||
                                          members[member].type == UNIFORM_IVEC4 ||
                                          members[member].type == UNIFORM_BOOL ||
                                          members[member].type == UNIFORM_BVEC2 ||
                                          members[member].type == UNIFORM_BVEC3 ||
                                          members[member].type == UNIFORM_BVEC4
                                    ? default_precision_for_type(source, "int", stage)
                                : members[member].type == UNIFORM_SAMPLER2D
                                    ? default_precision_for_type(source, "sampler2D", stage)
                                : members[member].type == UNIFORM_SAMPLERCUBE
                                    ? default_precision_for_type(source, "samplerCube", stage)
                                    : default_precision_for_type(source, "float", stage));
        signature *= UINT64_C(1099511628211);
    }
    return signature;
}

static int find_struct_uniform_members(const char *source, const char *type_name,
                                       size_t type_length, StructUniformMember *members,
                                       int *member_count, char *log)
{
    const char *cursor = source;

    *member_count = 0;
    while ((cursor = strstr(cursor, "struct")) != NULL) {
        const char *name;
        const char *name_end;
        const char *body;
        const char *body_end;

        if (!shader_keyword_at(source, cursor, 6)) {
            cursor += 6;
            continue;
        }
        name = skip_shader_space(cursor + 6);
        name_end = name;
        while (shader_identifier_character(*name_end))
            ++name_end;
        body = skip_shader_space(name_end);
        if ((size_t)(name_end - name) != type_length ||
            strncmp(name, type_name, type_length) || *body != '{') {
            cursor = name_end;
            continue;
        }
        body_end = control_matching(body, source + strlen(source), '{', '}');
        if (!body_end) {
            strcpy(log, "unterminated uniform structure declaration");
            return 0;
        }
        cursor = body + 1;
        while (cursor < body_end) {
            const char *member_type_start = skip_shader_space(cursor);
            const char *qualified_member_type = member_type_start;
            const char *member_type_end = member_type_start;
            const char *statement_end;
            const char *declarator;
            UniformType member_type;

            if (member_type_start == body_end)
                break;
            member_type_start = skip_precision(member_type_start);
            member_type_end = member_type_start;
            while (shader_identifier_character(*member_type_end))
                ++member_type_end;
            member_type = uniform_type(member_type_start,
                                       (size_t)(member_type_end - member_type_start));
            statement_end = strchr(member_type_end, ';');
            if (!statement_end || statement_end > body_end) {
                strcpy(log, "malformed uniform structure member declaration");
                return 0;
            }
            if (member_type == UNIFORM_OTHER) {
                const char *nested_declarator = member_type_end;

                while (nested_declarator < statement_end) {
                    StructUniformMember nested[MESAGL_MAX_SHADER_UNIFORMS];
                    const char *field_name = skip_shader_space(nested_declarator);
                    const char *field_name_end = field_name;
                    const char *after;
                    size_t field_length;
                    GLint field_size;
                    int nested_count;
                    int nested_index;

                    while (shader_identifier_character(*field_name_end))
                        ++field_name_end;
                    field_length = (size_t)(field_name_end - field_name);
                    if (!field_length ||
                        !parse_uniform_array_size(source, field_name_end, &after,
                                                  &field_size) ||
                        !find_struct_uniform_members(
                            source, member_type_start,
                            (size_t)(member_type_end - member_type_start), nested,
                            &nested_count, log)) {
                        return 0;
                    }
                    for (nested_index = 0; nested_index < nested_count; ++nested_index) {
                        int field_element;
                        int field_records = field_size > 1 && nested[nested_index].size > 1
                                                ? field_size
                                                : 1;

                        for (field_element = 0; field_element < field_records;
                             ++field_element) {
                            int length;

                            if (*member_count >= MESAGL_MAX_SHADER_UNIFORMS) {
                                strcpy(log, "uniform structure member limit exceeded");
                                return 0;
                            }
                            memset(&members[*member_count], 0,
                                   sizeof(members[*member_count]));
                            if (field_size > 1)
                                length = snprintf(
                                    members[*member_count].name,
                                    sizeof(members[*member_count].name), "%.*s[%d].%s",
                                    (int)field_length, field_name, field_element,
                                    nested[nested_index].name);
                            else
                                length = snprintf(
                                    members[*member_count].name,
                                    sizeof(members[*member_count].name), "%.*s.%s",
                                    (int)field_length, field_name,
                                    nested[nested_index].name);
                            if (length < 0 ||
                                length >= (int)sizeof(members[*member_count].name)) {
                                strcpy(log, "nested uniform member name is too long");
                                return 0;
                            }
                            members[*member_count].type = nested[nested_index].type;
                            members[*member_count].precision =
                                nested[nested_index].precision;
                            members[*member_count].size = field_records > 1
                                                              ? nested[nested_index].size
                                                              : field_size > 1
                                                                    ? field_size
                                                                    : nested[nested_index].size;
                            ++*member_count;
                        }
                    }
                    if (after == statement_end)
                        break;
                    if (*after != ',') {
                        strcpy(log, "malformed nested uniform structure member list");
                        return 0;
                    }
                    nested_declarator = after + 1;
                }
                cursor = statement_end + 1;
                continue;
            }
            declarator = member_type_end;
            while (declarator < statement_end) {
                const char *member_name = skip_shader_space(declarator);
                const char *member_name_end = member_name;
                const char *after;
                size_t member_length;

                while (shader_identifier_character(*member_name_end))
                    ++member_name_end;
                member_length = (size_t)(member_name_end - member_name);
                if (!member_length || member_length >= sizeof(members[0].name) ||
                    *member_count >= MESAGL_MAX_SHADER_UNIFORMS) {
                    strcpy(log, "invalid uniform structure member declaration");
                    return 0;
                }
                memset(&members[*member_count], 0, sizeof(members[*member_count]));
                memcpy(members[*member_count].name, member_name, member_length);
                members[*member_count].type = member_type;
                members[*member_count].precision =
                    uniform_precision(qualified_member_type);
                members[*member_count].size = 1;
                if (!parse_uniform_array_size(source, member_name_end, &after,
                                              &members[*member_count].size)) {
                    strcpy(log, "invalid uniform structure member array");
                    return 0;
                }
                ++*member_count;
                if (after == statement_end)
                    break;
                if (*after != ',') {
                    strcpy(log, "malformed uniform structure member list");
                    return 0;
                }
                declarator = after + 1;
            }
            cursor = statement_end + 1;
        }
        return 1;
    }
    strcpy(log, "uniform structure type is not defined");
    return 0;
}

static int shader_struct_member_used(const char *source,
                                     const ShaderReachability *reachability,
                                     const char *instance,
                                     size_t instance_length, const char *member,
                                     const char *excluded_start, const char *excluded_end)
{
    const char *cursor = source;

    while (*cursor) {
        const char *after;

        if (cursor >= excluded_start && cursor < excluded_end) {
            cursor = excluded_end;
            continue;
        }
        if (strncmp(cursor, instance, instance_length) ||
            !shader_keyword_at(source, cursor, instance_length)) {
            ++cursor;
            continue;
        }
        if (!shader_resource_position_reachable(
                reachability, cursor, instance, instance_length)) {
            cursor += instance_length;
            continue;
        }
        after = skip_shader_space(cursor + instance_length);
        if (*after == '[') {
            const char *close = control_matching(after, source + strlen(source), '[', ']');

            if (!close)
                return 0;
            after = skip_shader_space(close + 1);
        }
        if (*after == '.') {
            const char *member_cursor = member;

            after = skip_shader_space(after + 1);
            while (*member_cursor) {
                if (*member_cursor == '[') {
                    const char *close;
                    const char *member_close = member_cursor + 1;
                    const char *source_index;
                    const char *source_index_end;
                    int member_index = 0;
                    int source_value = 0;

                    while (*member_close >= '0' && *member_close <= '9') {
                        member_index = member_index * 10 + (*member_close - '0');
                        ++member_close;
                    }
                    if (*member_close != ']')
                        break;

                    after = skip_shader_space(after);
                    if (*after != '[' ||
                        !(close = control_matching(after, source + strlen(source), '[', ']')))
                        break;
                    source_index = skip_shader_space(after + 1);
                    source_index_end = source_index;
                    while (*source_index_end >= '0' && *source_index_end <= '9') {
                        source_value = source_value * 10 + (*source_index_end - '0');
                        ++source_index_end;
                    }
                    source_index_end = skip_shader_space(source_index_end);
                    if (strchr(member_close + 1, '[') && source_index_end == close &&
                        source_value != member_index)
                        break;
                    after = close + 1;
                    member_cursor = member_close + 1;
                    continue;
                }
                if (*member_cursor == '.') {
                    after = skip_shader_space(after);
                    if (*after != '.')
                        break;
                    after = skip_shader_space(after + 1);
                    ++member_cursor;
                    continue;
                }
                if (*after != *member_cursor)
                    break;
                ++after;
                ++member_cursor;
            }
            if (!*member_cursor &&
                !shader_identifier_character(*after))
                return 1;
        }
        cursor += instance_length;
    }
    return 0;
}

static int collect_struct_uniform(Program *program, const char *source,
                                  const ShaderReachability *reachability,
                                  const char *type_start, const char *type_end,
                                  const char *declaration_start, const char *statement_end,
                                  UniformPackState *pack, int *sampler_count,
                                  GLenum stage)
{
    StructUniformMember members[MESAGL_MAX_SHADER_UNIFORMS];
    const char *declarator = type_end;
    uint64_t signature;
    int member_count;

    if (!find_struct_uniform_members(source, type_start,
                                     (size_t)(type_end - type_start), members,
                                     &member_count, program->log))
        return 0;
    signature = struct_uniform_signature(source, stage, members, member_count);
    while (declarator < statement_end) {
        const char *instance = skip_shader_space(declarator);
        const char *instance_end = instance;
        const char *after;
        size_t instance_length;
        GLint instance_size;
        int member;

        while (shader_identifier_character(*instance_end))
            ++instance_end;
        instance_length = (size_t)(instance_end - instance);
        if (!instance_length || instance_length >= sizeof(program->uniforms[0].aggregate_name) ||
            !parse_uniform_array_size(source, instance_end, &after, &instance_size)) {
            strcpy(program->log, "invalid uniform structure declaration");
            return 0;
        }
        if (shader_identifier_written_scoped(
                source, reachability, instance, instance_length, instance)) {
            strcpy(program->log, "uniform is read-only");
            return 0;
        }
        for (member = 0; member < member_count; ++member) {
            int aggregate_element;
            int aggregate_records;

            if (!shader_struct_member_used(source, reachability,
                                           instance, instance_length,
                                           members[member].name, declaration_start,
                                           statement_end + 1))
                continue;
            aggregate_records = instance_size > 1 && members[member].size > 1
                                    ? instance_size
                                    : 1;
            for (aggregate_element = 0; aggregate_element < aggregate_records;
                 ++aggregate_element) {
                char full_name[MESAGL_MAX_SHADER_LVALUE_PATH_LENGTH];
                int length;
                int before_count = program->uniform_count;
                Uniform *uniform;
                GLint storage_size = aggregate_records > 1
                                         ? members[member].size
                                     : instance_size > 1 ? instance_size
                                                         : members[member].size;
                if (!add_uniform_pack_rectangle(
                        pack, members[member].type, storage_size)) {
                    strcpy(program->log, "stage uniform vector limit exceeded");
                    return 0;
                }
                if (members[member].type == UNIFORM_SAMPLER2D ||
                    members[member].type == UNIFORM_SAMPLERCUBE)
                    *sampler_count += storage_size;
                if (aggregate_records > 1)
                    length = snprintf(full_name, sizeof(full_name),
                                      strstr(members[member].name, "[0].")
                                          ? "%.*s[%d].%s"
                                          : "%.*s[%d].%s[0]",
                                      (int)instance_length, instance, aggregate_element,
                                      members[member].name);
                else if (instance_size > 1)
                    length = snprintf(full_name, sizeof(full_name), "%.*s[0].%s",
                                      (int)instance_length, instance, members[member].name);
                else if (members[member].size > 1)
                    length = snprintf(full_name, sizeof(full_name),
                                      strstr(members[member].name, "[0].")
                                          ? "%.*s.%s"
                                          : "%.*s.%s[0]",
                                      (int)instance_length, instance, members[member].name);
                else
                    length = snprintf(full_name, sizeof(full_name), "%.*s.%s",
                                      (int)instance_length, instance, members[member].name);
                if (length < 0 || length >= (int)sizeof(full_name) ||
                    !add_program_uniform(program, full_name, full_name + length,
                                         members[member].type, storage_size, 0))
                    return 0;
                uniform = NULL;
                for (length = 0; length < program->uniform_count; ++length)
                    if (!strcmp(program->uniforms[length].name, full_name)) {
                        uniform = &program->uniforms[length];
                        break;
                    }
                if (!uniform)
                    return 0;
                if (uniform->aggregate_name[0] &&
                    (strlen(uniform->aggregate_type) != (size_t)(type_end - type_start) ||
                     strncmp(uniform->aggregate_type, type_start,
                             (size_t)(type_end - type_start)) ||
                     strcmp(uniform->member_name, members[member].name) ||
                     uniform->aggregate_size != instance_size ||
                     uniform->member_size != members[member].size ||
                     uniform->aggregate_signature != signature ||
                     uniform->aggregate_element !=
                         (aggregate_records > 1 ? aggregate_element : -1))) {
                    strcpy(program->log, "uniform structure type differs between shaders");
                    return 0;
                }
                if (before_count != program->uniform_count || !uniform->aggregate_name[0]) {
                    memcpy(uniform->aggregate_name, instance, instance_length);
                    memcpy(uniform->aggregate_type, type_start,
                           (size_t)(type_end - type_start));
                    strcpy(uniform->member_name, members[member].name);
                    uniform->aggregate_size = instance_size;
                    uniform->member_size = members[member].size;
                    uniform->aggregate_signature = signature;
                    uniform->aggregate_element =
                        aggregate_records > 1 ? aggregate_element : -1;
                }
            }
        }
        if (after == statement_end)
            break;
        if (*after != ',') {
            strcpy(program->log, "malformed uniform structure declarator list");
            return 0;
        }
        declarator = after + 1;
    }
    return 1;
}

static int uniform_precision(const char *cursor)
{
    const char *end = cursor;

    while (shader_identifier_character(*end))
        ++end;
    if (end - cursor == 4 && !strncmp(cursor, "lowp", 4))
        return 1;
    if (end - cursor == 7 && !strncmp(cursor, "mediump", 7))
        return 2;
    if (end - cursor == 5 && !strncmp(cursor, "highp", 5))
        return 3;
    return 0;
}

static int default_precision_for_type(const char *source, const char *type_name,
                                      GLenum stage)
{
    const char *cursor = source;
    size_t type_length = strlen(type_name);

    while ((cursor = strstr(cursor, "precision")) != NULL) {
        const char *qualifier;
        const char *declared_type;
        const char *scopes[64];
        int precision;

        if (!shader_keyword_at(source, cursor, 9)) {
            cursor += 9;
            continue;
        }
        qualifier = skip_shader_space(cursor + 9);
        precision = uniform_precision(qualifier);
        declared_type = skip_precision(qualifier);
        if (link_open_braces(source, cursor, scopes, 64) == 0 && precision &&
            !strncmp(declared_type, type_name, type_length) &&
            !shader_identifier_character(declared_type[type_length]))
            return precision;
        cursor += 9;
    }
    if (!strcmp(type_name, "float"))
        return stage == GL_VERTEX_SHADER ? 3 : 0;
    if (!strcmp(type_name, "int"))
        return stage == GL_VERTEX_SHADER ? 3 : 2;
    return 1;
}

static int resolved_uniform_precision(const char *source, const char *qualified_type,
                                      UniformType type, GLenum stage)
{
    int precision = uniform_precision(qualified_type);

    if (precision)
        return precision;
    if (type == UNIFORM_SAMPLER2D)
        return default_precision_for_type(source, "sampler2D", stage);
    if (type == UNIFORM_SAMPLERCUBE)
        return default_precision_for_type(source, "samplerCube", stage);
    if (type == UNIFORM_INT || type == UNIFORM_IVEC2 || type == UNIFORM_IVEC3 ||
        type == UNIFORM_IVEC4 || type == UNIFORM_BOOL || type == UNIFORM_BVEC2 ||
        type == UNIFORM_BVEC3 || type == UNIFORM_BVEC4)
        return default_precision_for_type(source, "int", stage);
    if (type != UNIFORM_OTHER)
        return default_precision_for_type(source, "float", stage);
    return 0;
}

static int collect_uniform_interfaces(const char *source, InterfaceUniform *interfaces,
                                      int *interface_count, GLenum stage, char *log)
{
    const char *cursor = source;

    *interface_count = 0;
    while ((cursor = strstr(cursor, "uniform")) != NULL) {
        const char *qualified_type;
        const char *type_start;
        const char *type_end;
        const char *statement_end;
        const char *declarator;
        UniformType type;
        uint64_t signature = 0;
        int precision;

        if (!shader_keyword_at(source, cursor, 7)) {
            cursor += 7;
            continue;
        }
        qualified_type = skip_shader_space(cursor + 7);
        type_start = skip_precision(qualified_type);
        type_end = type_start;
        while (shader_identifier_character(*type_end))
            ++type_end;
        type = uniform_type(type_start, (size_t)(type_end - type_start));
        precision = resolved_uniform_precision(source, qualified_type, type, stage);
        statement_end = strchr(type_end, ';');
        if (!statement_end) {
            strcpy(log, "unterminated uniform declaration");
            return 0;
        }
        if (type == UNIFORM_OTHER) {
            StructUniformMember members[MESAGL_MAX_SHADER_UNIFORMS];
            int member_count;

            if (!find_struct_uniform_members(source, type_start,
                                             (size_t)(type_end - type_start), members,
                                             &member_count, log))
                return 0;
            signature = struct_uniform_signature(source, stage, members, member_count);
        }
        declarator = type_end;
        while (declarator < statement_end) {
            const char *name = skip_shader_space(declarator);
            const char *name_end = name;
            const char *after;
            GLint size;
            size_t name_length;
            int previous;
            int duplicate = -1;

            while (shader_identifier_character(*name_end))
                ++name_end;
            name_length = (size_t)(name_end - name);
            if (!name_length || name_length >= sizeof(interfaces[0].name) ||
                !parse_uniform_array_size(source, name_end, &after, &size)) {
                strcpy(log, "invalid uniform interface or array declaration");
                return 0;
            }
            for (previous = 0; previous < *interface_count; ++previous)
                if (strlen(interfaces[previous].name) == name_length &&
                    !strncmp(interfaces[previous].name, name, name_length)) {
                    duplicate = previous;
                    break;
                }
            if (duplicate >= 0 &&
                (interfaces[duplicate].type != type ||
                 interfaces[duplicate].size != size ||
                 interfaces[duplicate].precision != precision ||
                 interfaces[duplicate].structure_signature != signature)) {
                strcpy(log, "uniform declarations conflict within one shader stage");
                return 0;
            }
            if (duplicate < 0 && *interface_count >= MESAGL_MAX_SHADER_UNIFORMS) {
                strcpy(log, "uniform declaration limit exceeded");
                return 0;
            }
            if (duplicate < 0) {
                memset(&interfaces[*interface_count], 0,
                       sizeof(interfaces[*interface_count]));
                memcpy(interfaces[*interface_count].name, name, name_length);
                interfaces[*interface_count].type = type;
                interfaces[*interface_count].size = size;
                interfaces[*interface_count].precision = precision;
                interfaces[*interface_count].structure_signature = signature;
                ++*interface_count;
            }
            if (after == statement_end)
                break;
            if (*after != ',') {
                strcpy(log, "malformed uniform interface declarator list");
                return 0;
            }
            declarator = after + 1;
        }
        cursor = statement_end + 1;
    }
    return 1;
}

static int validate_uniform_interfaces(Program *program, const char *vertex_source,
                                       const char *fragment_source)
{
    InterfaceUniform *vertex;
    InterfaceUniform *fragment;
    int vertex_count;
    int fragment_count;
    int vertex_index;
    int fragment_index;
    int valid = 0;

    vertex = (InterfaceUniform *)ntglAlloc(MESAGL_MAX_SHADER_UNIFORMS * sizeof(*vertex));
    fragment = (InterfaceUniform *)ntglAlloc(MESAGL_MAX_SHADER_UNIFORMS * sizeof(*fragment));
    if (!vertex || !fragment) {
        strcpy(program->log, "out of memory while validating uniform interfaces");
        goto done;
    }
    if (!collect_uniform_interfaces(vertex_source, vertex, &vertex_count,
                                    GL_VERTEX_SHADER, program->log) ||
        !collect_uniform_interfaces(fragment_source, fragment, &fragment_count,
                                    GL_FRAGMENT_SHADER, program->log))
        goto done;
    for (vertex_index = 0; vertex_index < vertex_count; ++vertex_index)
        for (fragment_index = 0; fragment_index < fragment_count; ++fragment_index) {
            InterfaceUniform *left = &vertex[vertex_index];
            InterfaceUniform *right = &fragment[fragment_index];

            if (strcmp(left->name, right->name))
                continue;
            if (left->type != right->type || left->size != right->size ||
                left->precision != right->precision ||
                left->structure_signature != right->structure_signature) {
                strcpy(program->log,
                       "uniform type, precision, or array size differs between shader stages");
                goto done;
            }
        }
    valid = 1;

done:
    ntglFree(fragment);
    ntglFree(vertex);
    return valid;
}
#endif

static void collect_uniforms(Program *program, const char *source,
                             const ShaderReachability *reachability,
                             const ShaderReachability *other_reachability,
                             int vector_limit, int *sampler_count,
                             GLenum stage)
{
    const char *cursor = source;
    UniformPackState pack;

    memset(&pack, 0, sizeof(pack));
    pack.vector_limit = vector_limit;
    *sampler_count = 0;

#if MESAGL_GLES2_PROFILE == MESAGL_GLES2_PROFILE_LITE
    (void)other_reachability;
    (void)stage;
#endif

    while (cursor && (cursor = strstr(cursor, "uniform")) != NULL) {
        const char *type_start;
        const char *type_end;
        const char *declarator;
        const char *statement_end;
        UniformType declared_type;

        if (!shader_keyword_at(source, cursor, 7)) {
            cursor += 7;
            continue;
        }
        type_start = cursor + 7;
        while (*type_start == ' ' || *type_start == '\t' || *type_start == '\n' ||
               *type_start == '\r')
            ++type_start;
        type_start = skip_precision(type_start);
        type_end = type_start;
        while ((*type_end >= 'a' && *type_end <= 'z') || (*type_end >= 'A' && *type_end <= 'Z') ||
               (*type_end >= '0' && *type_end <= '9') || *type_end == '_')
            ++type_end;
        declared_type = uniform_type(type_start, (size_t)(type_end - type_start));
        if (declared_type == UNIFORM_OTHER) {
#if MESAGL_GLES2_PROFILE == MESAGL_GLES2_PROFILE_FULL
            statement_end = strchr(type_end, ';');
            if (!statement_end ||
                !collect_struct_uniform(program, source, reachability, type_start,
                                        type_end, cursor, statement_end,
                                        &pack, sampler_count, stage))
                return;
            cursor = statement_end + 1;
            continue;
#else
            strcpy(program->log, "unsupported uniform type");
            return;
#endif
        }
        statement_end = strchr(type_end, ';');
        if (!statement_end) {
            strcpy(program->log, "unterminated uniform declaration");
            return;
        }
        declarator = type_end;
        while (declarator < statement_end) {
            const char *name_start = skip_shader_space(declarator);
            const char *name_end = name_start;
            const char *after;
            GLint array_size;

            while ((*name_end >= 'a' && *name_end <= 'z') ||
                   (*name_end >= 'A' && *name_end <= 'Z') ||
                   (*name_end >= '0' && *name_end <= '9') || *name_end == '_')
                ++name_end;
            if (name_end == name_start ||
                shader_identifier_written_scoped(
                    source, reachability, name_start,
                    (size_t)(name_end - name_start), name_start)) {
                strcpy(program->log, name_end == name_start ? "malformed uniform declaration"
                                                           : "uniform is read-only");
                return;
            }
            if (!parse_uniform_array_size(source, name_end, &after, &array_size)) {
                strcpy(program->log, "invalid or oversized uniform array");
                return;
            }
            if (shader_identifier_reachable_used(
                    reachability, name_start, (size_t)(name_end - name_start))) {
#if MESAGL_GLES2_PROFILE == MESAGL_GLES2_PROFILE_FULL
                int active_array_size = shader_uniform_active_array_size(
                    reachability, name_start, (size_t)(name_end - name_start),
                    array_size);

                if (shader_identifier_reachable_used(
                                        other_reachability, name_start,
                                        (size_t)(name_end - name_start))) {
                    int other_size = shader_uniform_active_array_size(
                        other_reachability, name_start,
                        (size_t)(name_end - name_start), array_size);

                    if (active_array_size < other_size)
                        active_array_size = other_size;
                }
#else
                int active_array_size = array_size;
#endif
                if (!add_uniform_pack_rectangle(&pack, declared_type,
                                                active_array_size)) {
                    strcpy(program->log, "stage uniform vector limit exceeded");
                    return;
                }
                if (declared_type == UNIFORM_SAMPLER2D ||
                    declared_type == UNIFORM_SAMPLERCUBE)
                    *sampler_count += active_array_size;
#if MESAGL_GLES2_PROFILE == MESAGL_GLES2_PROFILE_LITE
                if (pack.vector_count > vector_limit) {
                    strcpy(program->log, "stage uniform vector limit exceeded");
                    return;
                }
#endif
                if (!add_program_uniform(program, name_start, name_end, declared_type,
                                         active_array_size, array_size > 1))
                    return;
            }
            if (after == statement_end)
                break;
            if (*after != ',') {
                strcpy(program->log, "malformed uniform declarator list");
                return;
            }
            declarator = after + 1;
        }
        cursor = statement_end + 1;
    }
#if MESAGL_GLES2_PROFILE == MESAGL_GLES2_PROFILE_FULL
    finalize_uniform_pack(program, &pack);
#endif
}

static Uniform *find_uniform(Program *program, GLint location)
{
    int i;

    if (!program)
        return NULL;
    for (i = 0; i < program->uniform_count; ++i)
        if (location >= program->uniforms[i].location &&
            location < program->uniforms[i].location + program->uniforms[i].size)
            return &program->uniforms[i];
    return NULL;
}

static float *uniform_float_value(Uniform *uniform, GLint location)
{
    int index;

    if (!uniform)
        return NULL;
    index = location - uniform->location;
    if (index < 0 || index >= uniform->size)
        return NULL;
    return uniform->array_value ? uniform->array_value + index * 16 : uniform->value;
}

static GLint *uniform_integer_value(Uniform *uniform, GLint location)
{
    int index;

    if (!uniform)
        return NULL;
    index = location - uniform->location;
    if (index < 0 || index >= uniform->size)
        return NULL;
    return uniform->array_integer ? uniform->array_integer + index * 4 : uniform->integer;
}

static void find_fragment_output(Program *program, const char *source)
{
    const char *assignment = strstr(source, "gl_FragColor");
    int i;

    program->fragment_output_uniform = -1;
    if (!assignment || !(assignment = strchr(assignment, '=')))
        return;
    ++assignment;
    while (*assignment == ' ' || *assignment == '\t' || *assignment == '\n' ||
           *assignment == '\r')
        ++assignment;
    for (i = 0; i < program->uniform_count; ++i) {
        size_t length = strlen(program->uniforms[i].name);

        if (!strncmp(assignment, program->uniforms[i].name, length) &&
            (assignment[length] == ';' || assignment[length] == ' ' ||
             assignment[length] == '\t' || assignment[length] == '\n')) {
            program->fragment_output_uniform = program->uniforms[i].location;
            return;
        }
    }
}

static void extract_assignment(const char *source, const char *target, char *output, size_t size)
{
    const char *start = source;
    const char *end;
    size_t length;
    size_t target_length = strlen(target);

    output[0] = '\0';
    while ((start = strstr(start, target)) != NULL) {
        const char *after = start + target_length;

        while (*after == ' ' || *after == '\t' || *after == '\r' || *after == '\n')
            ++after;
        if (*after == '=') {
            start = after + 1;
            break;
        }
        start += target_length;
    }
    if (!start)
        return;
    while (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n')
        ++start;
    end = strchr(start, ';');
    if (!end)
        return;
    while (end > start && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' ||
                           end[-1] == '\n'))
        --end;
    length = (size_t)(end - start);
    if (length >= size)
        length = size - 1;
    memcpy(output, start, length);
    output[length] = '\0';
}

static void extract_discard_condition(const char *source, char *output, size_t size)
{
    const char *discard = strstr(source, "discard");
    const char *cursor;
    const char *close = NULL;
    int depth = 0;

    output[0] = '\0';
    if (!discard)
        return;
    for (cursor = discard; cursor > source; --cursor) {
        if (cursor[-1] == ')') {
            if (!depth)
                close = cursor - 1;
            ++depth;
        } else if (cursor[-1] == '(' && depth && !--depth && close) {
            size_t length = (size_t)(close - cursor);

            if (length >= size)
                length = size - 1;
            memcpy(output, cursor, length);
            output[length] = '\0';
            return;
        }
    }
}

static char *copy_main_body(const char *source)
{
    const char *source_end = source + strlen(source);
    const char *main_function = source;
    const char *open = NULL;
    const char *cursor;
    int depth = 0;

    while ((main_function = strstr(main_function, "main"))) {
        const char *parameters;
        const char *parameters_end;

        if (!shader_keyword_at(source, main_function, 4)) {
            main_function += 4;
            continue;
        }
        parameters = skip_shader_space(main_function + 4);
        if (*parameters != '(' ||
            !(parameters_end = control_matching(parameters, source_end,
                                                '(', ')'))) {
            main_function += 4;
            continue;
        }
        open = skip_shader_space(parameters_end + 1);
        if (*open == '{')
            break;
        main_function += 4;
        open = NULL;
    }
    if (!open)
        return NULL;
    for (cursor = open; *cursor; ++cursor) {
        if (*cursor == '{')
            ++depth;
        else if (*cursor == '}' && !--depth) {
            size_t length = (size_t)(cursor - open - 1);
            char *output = (char *)ntglAlloc(length + 1);

            if (!output)
                return NULL;
            memcpy(output, open + 1, length);
            output[length] = '\0';
            return output;
        }
    }
    return NULL;
}

#if MESAGL_GLES2_PROFILE == MESAGL_GLES2_PROFILE_FULL && \
    MESAGL_ENABLE_SHADER_FAST_PATHS
static int compact_shader_body_equals(const char *source, const char *expected)
{
    while (*source || *expected) {
        while (*source == ' ' || *source == '\t' || *source == '\r' ||
               *source == '\n')
            ++source;
        if (*source != *expected)
            return 0;
        if (!*source)
            return 1;
        ++source;
        ++expected;
    }
    return 1;
}

static int named_binding_index(const Program *program, const char *name,
                               GLenum type)
{
    int i;

    for (i = 0; i < program->binding_count; ++i)
        if (program->bindings[i].active &&
            program->bindings[i].type == type &&
            !strcmp(program->bindings[i].name, name))
            return (int)program->bindings[i].index;
    return -1;
}

static int named_varying_slot(const Program *program, const char *name,
                              GLenum type)
{
    int i;

    for (i = 0; i < program->varying_count; ++i)
        if (program->varyings[i].active &&
            program->varyings[i].type == type &&
            !strcmp(program->varyings[i].name, name))
            return program->varyings[i].slot;
    return -1;
}

static GLint named_uniform_location(const Program *program, const char *name,
                                    UniformType type)
{
    int i;

    for (i = 0; i < program->uniform_count; ++i)
        if (program->uniforms[i].type == type &&
            !strcmp(program->uniforms[i].name, name))
            return program->uniforms[i].location;
    return -1;
}

static void configure_imgui_fast_path(Program *program)
{
    static const char vertex_body[] =
        "Frag_UV=UV;Frag_Color=Color;"
        "gl_Position=ProjMtx*vec4(Position.xy,0,1);";
    static const char fragment_body[] =
        "gl_FragColor=Frag_Color*texture2D(Texture,Frag_UV.st);";

    program->imgui_position = named_binding_index(
        program, "Position", GL_FLOAT_VEC2);
    program->imgui_uv = named_binding_index(program, "UV", GL_FLOAT_VEC2);
    program->imgui_color = named_binding_index(program, "Color", GL_FLOAT_VEC4);
    program->imgui_uv_varying = named_varying_slot(
        program, "Frag_UV", GL_FLOAT_VEC2);
    program->imgui_color_varying = named_varying_slot(
        program, "Frag_Color", GL_FLOAT_VEC4);
    program->imgui_projection = named_uniform_location(
        program, "ProjMtx", UNIFORM_MAT4);
    program->imgui_texture = named_uniform_location(
        program, "Texture", UNIFORM_SAMPLER2D);
    program->imgui_fast_path =
        program->binding_count == 3 && program->varying_count == 2 &&
        program->uniform_count == 2 && program->imgui_position >= 0 &&
        program->imgui_uv >= 0 && program->imgui_color >= 0 &&
        program->imgui_uv_varying >= 0 && program->imgui_color_varying >= 0 &&
        program->imgui_projection >= 0 && program->imgui_texture >= 0 &&
        compact_shader_body_equals(program->vertex_body, vertex_body) &&
        compact_shader_body_equals(program->fragment_body, fragment_body);
}
#endif

static void execute_fragment(void *user, float color[4])
{
    Program *program = (Program *)user;
    Uniform *uniform = find_uniform(program, program->fragment_output_uniform);
    float *storage = uniform_float_value(uniform, program->fragment_output_uniform);

    if (storage && uniform->type == UNIFORM_VEC4)
        memcpy(color, storage, 4 * sizeof(float));
}

GLuint glCreateShader(GLenum type)
{
    int i;
    if (type != GL_VERTEX_SHADER && type != GL_FRAGMENT_SHADER) {
        mesaGLSetError(GL_INVALID_ENUM);
        return 0;
    }
    for (i = 0; i < MAX_SHADERS; ++i) {
        if (!shaders[i].name) {
            shaders[i].name = next_shader_program++;
            shaders[i].type = type;
            return shaders[i].name;
        }
    }
    mesaGLSetError(GL_OUT_OF_MEMORY);
    return 0;
}

void glDeleteShader(GLuint name)
{
    Shader *shader = find_shader(name);

    if (!name)
        return;
    if (!shader) {
        shader_name_error(name);
        return;
    }
    if (shader_is_attached(name))
        shader->delete_pending = 1;
    else
        destroy_shader(shader);
}

void glShaderSource(GLuint name, GLsizei count, const GLchar *const *strings, const GLint *lengths)
{
    Shader *shader = find_shader(name);
    size_t total = 0;
    char *source;
    size_t *boundaries = NULL;
    int i;
    if (!shader) {
        shader_name_error(name);
        return;
    }
    if (count < 0 || (count && !strings)) {
        mesaGLSetError(GL_INVALID_VALUE);
        return;
    }
    for (i = 0; i < count; ++i) {
        size_t part;

        if (!strings[i]) {
            mesaGLSetError(GL_INVALID_VALUE);
            return;
        }
        part = lengths && lengths[i] >= 0 ? (size_t)lengths[i] : strlen(strings[i]);
        if (part > SIZE_MAX - total - 1) {
            mesaGLSetError(GL_OUT_OF_MEMORY);
            return;
        }
        total += part;
    }
    source = (char *)ntglAlloc(total + 1);
    if (!source) {
        mesaGLSetError(GL_OUT_OF_MEMORY);
        return;
    }
    if (count > 1) {
        boundaries = (size_t *)ntglAlloc((size_t)(count - 1) *
                                         sizeof(*boundaries));
        if (!boundaries) {
            ntglFree(source);
            mesaGLSetError(GL_OUT_OF_MEMORY);
            return;
        }
    }
    total = 0;
    for (i = 0; i < count; ++i) {
        size_t part = lengths && lengths[i] >= 0 ? (size_t)lengths[i] : strlen(strings[i]);
        memcpy(source + total, strings[i], part);
        total += part;
        if (i + 1 < count)
            boundaries[i] = total;
    }
    source[total] = '\0';
    ntglFree(shader->source);
    ntglFree(shader->source_boundaries);
    ntglFree(shader->compiled_source);
    shader->source = source;
    shader->source_length = total;
    shader->source_boundaries = boundaries;
    shader->source_boundary_count = count > 1 ? count - 1 : 0;
    shader->compiled_source = NULL;
    shader->compiled = 0;
    shader->invariant_all = 0;
}

void glCompileShader(GLuint name)
{
    Shader *shader = find_shader(name);
    unsigned int enabled_extensions = 0;
    if (!shader) {
        shader_name_error(name);
        return;
    }
    shader->compiled = 0;
    shader->log[0] = '\0';
    ntglFree(shader->compiled_source);
    shader->compiled_source = NULL;
    if (!shader->source) {
        strcpy(shader->log, "shader source is missing");
        return;
    }
    shader->compiled_source = mesaGLSLPreprocessSource(
        shader->source, shader->source_length, shader->source_boundaries,
        shader->source_boundary_count, &enabled_extensions, shader->log,
        sizeof(shader->log));
    if (!shader->compiled_source)
        return;
    shader->invariant_all =
        !!(enabled_extensions & MESAGL_GLSL_PRAGMA_INVARIANT_ALL);
    if (!source_delimiters_balanced(shader->compiled_source)) {
        strcpy(shader->log, "unbalanced shader delimiters or comment");
        return;
    }
    if (!validate_reserved_gl_identifiers(shader->compiled_source,
                                          shader->log))
        return;
    if (!validate_reserved_es100_words(shader->compiled_source, shader->log))
        return;
    if (!validate_precision_statements(shader->compiled_source, shader->log))
        return;
    if (!validate_storage_qualifier_scope(shader->compiled_source, shader->log))
        return;
    if (!validate_structure_declarations(shader->compiled_source, shader->log))
        return;
    if (!validate_sampler_declarations(shader->compiled_source, shader->log))
        return;
    if (!validate_sampler_structure_storage(shader->compiled_source, shader->log))
        return;
    if (!validate_no_array_of_array_declarations(shader->compiled_source,
                                                 shader->log))
        return;
    if (!validate_declaration_qualifiers(shader->compiled_source, shader->log))
        return;
    if (shader->type == GL_FRAGMENT_SHADER &&
        !validate_fragment_float_precision(shader->compiled_source, shader->log))
        return;
    if (!validate_invariant_usage(shader->type, shader->compiled_source,
                                  shader->log))
        return;
    if (!validate_loop_control(shader->compiled_source, shader->log))
        return;
    if (!validate_reserved_operators(shader->compiled_source, shader->log))
        return;
    if (!validate_numeric_literals(shader->compiled_source, shader->log))
        return;
    if (shader->type == GL_VERTEX_SHADER &&
        (shader_has_call(shader->compiled_source, "dFdx") ||
         shader_has_call(shader->compiled_source, "dFdy") ||
         shader_has_call(shader->compiled_source, "fwidth"))) {
        strcpy(shader->log, "derivative function is only valid in a fragment shader");
        return;
    }
    if (shader->type == GL_VERTEX_SHADER &&
        (shader_has_identifier(shader->compiled_source, "discard") ||
         shader_has_identifier(shader->compiled_source, "gl_FragCoord") ||
         shader_has_identifier(shader->compiled_source, "gl_FrontFacing") ||
         shader_has_identifier(shader->compiled_source, "gl_PointCoord") ||
         shader_has_identifier(shader->compiled_source, "gl_FragColor") ||
         shader_has_identifier(shader->compiled_source, "gl_FragData"))) {
        strcpy(shader->log, "fragment-only language feature used in a vertex shader");
        return;
    }
    if (shader->type == GL_FRAGMENT_SHADER &&
        (shader_has_identifier(shader->compiled_source, "gl_Position") ||
         shader_has_identifier(shader->compiled_source, "gl_PointSize"))) {
        strcpy(shader->log, "vertex-only built-in used in a fragment shader");
        return;
    }
    if (shader->type == GL_FRAGMENT_SHADER &&
        shader_has_identifier(shader->compiled_source, "attribute")) {
        strcpy(shader->log, "attribute declaration is only valid in a vertex shader");
        return;
    }
    if (shader_read_only_builtin_written(shader->compiled_source)) {
        strcpy(shader->log, "read-only shader built-in was written");
        return;
    }
    if (shader_storage_has_initializer(shader->compiled_source, "uniform") ||
        shader_storage_has_initializer(shader->compiled_source, "varying") ||
        shader_storage_has_initializer(shader->compiled_source, "attribute")) {
        strcpy(shader->log, "storage-qualified declarations cannot have initializers");
        return;
    }
    if (shader->type == GL_VERTEX_SHADER &&
        !validate_vertex_attribute_declarations(shader->compiled_source, shader->log)) {
        return;
    }
    if (!validate_varying_declaration_types(shader->compiled_source, shader->log))
        return;
    if (shader->type == GL_FRAGMENT_SHADER &&
        (shader_has_call(shader->compiled_source, "dFdx") ||
         shader_has_call(shader->compiled_source, "dFdy") ||
         shader_has_call(shader->compiled_source, "fwidth")) &&
        !(enabled_extensions & MESAGL_GLSL_EXTENSION_STANDARD_DERIVATIVES)) {
        strcpy(shader->log, "derivative function requires GL_OES_standard_derivatives");
        return;
    }
    if (shader->type == GL_FRAGMENT_SHADER &&
        (shader_has_call(shader->compiled_source, "dFdx") ||
         shader_has_call(shader->compiled_source, "dFdy") ||
         shader_has_call(shader->compiled_source, "fwidth")) &&
        (enabled_extensions &
         MESAGL_GLSL_EXTENSION_STANDARD_DERIVATIVES_WARN))
        strcpy(shader->log,
               "warning: GL_OES_standard_derivatives feature used");
    if (shader->type == GL_FRAGMENT_SHADER &&
        (shader_has_call(shader->compiled_source, "texture2DLod") ||
         shader_has_call(shader->compiled_source, "texture2DProjLod") ||
         shader_has_call(shader->compiled_source, "textureCubeLod"))) {
        strcpy(shader->log, "explicit texture LOD function is only valid in a vertex shader");
        return;
    }
    if (shader->type == GL_VERTEX_SHADER &&
        (shader_has_call_arity(shader->compiled_source, "texture2D", 3) ||
         shader_has_call_arity(shader->compiled_source, "texture2DProj", 3) ||
         shader_has_call_arity(shader->compiled_source, "textureCube", 3))) {
        strcpy(shader->log, "texture bias argument is only valid in a fragment shader");
        return;
    }
#if MESAGL_GLES2_PROFILE == MESAGL_GLES2_PROFILE_LITE
    if (source_has(shader, "for(") || source_has(shader, "for (") || source_has(shader, "while(") ||
        source_has(shader, "while (") || source_has(shader, "discard")) {
        strcpy(shader->log, "control flow is outside the UI shader subset");
        return;
    }
#else
    {
        int discarded = 0;
        Program validation;

        memset(&validation, 0, sizeof(validation));
        if (!validate_function_prototypes(&validation, shader->compiled_source,
                                          shader->type == GL_FRAGMENT_SHADER, 0)) {
            memcpy(shader->log, validation.log, sizeof(shader->log));
            free_program_uniforms(&validation);
            return;
        }
        free_program_uniforms(&validation);

        if (!mesaGLSLExecuteProgram(shader->compiled_source, "", NULL, NULL, NULL, NULL,
                                    &discarded, NULL)) {
            strcpy(shader->log, "invalid global constant declaration");
            return;
        }
    }
#endif
    shader->compiled = 1;
}

void glGetShaderiv(GLuint name, GLenum pname, GLint *params)
{
    Shader *shader = find_shader(name);
    if (!params)
        return;
    if (!shader) {
        shader_name_error(name);
        return;
    }
    if (pname == GL_COMPILE_STATUS)
        *params = shader->compiled;
    else if (pname == GL_DELETE_STATUS)
        *params = shader->delete_pending;
    else if (pname == GL_INFO_LOG_LENGTH)
        *params = shader->log[0] ? (GLint)strlen(shader->log) + 1 : 0;
    else if (pname == GL_SHADER_SOURCE_LENGTH)
        *params = shader->source ? (GLint)shader->source_length + 1 : 0;
    else if (pname == GL_SHADER_TYPE)
        *params = (GLint)shader->type;
    else
        mesaGLSetError(GL_INVALID_ENUM);
}

void glGetShaderInfoLog(GLuint name, GLsizei size, GLsizei *length, GLchar *log)
{
    Shader *shader = find_shader(name);

    if (!shader) {
        shader_name_error(name);
        return;
    }
    if (size < 0) {
        mesaGLSetError(GL_INVALID_VALUE);
        return;
    }
    copy_log(shader->log, size, length, log);
}

void glReleaseShaderCompiler(void)
{
}

void glGetShaderPrecisionFormat(GLenum shader_type, GLenum precision_type, GLint *range,
                                GLint *precision)
{
    if (shader_type != GL_VERTEX_SHADER && shader_type != GL_FRAGMENT_SHADER) {
        mesaGLSetError(GL_INVALID_ENUM);
        return;
    }
    if (precision_type < GL_LOW_FLOAT || precision_type > GL_HIGH_INT) {
        mesaGLSetError(GL_INVALID_ENUM);
        return;
    }
    if (range) {
        range[0] = precision_type <= GL_HIGH_FLOAT ? 127 : 24;
        range[1] = precision_type <= GL_HIGH_FLOAT ? 127 : 24;
    }
    if (precision)
        *precision = precision_type <= GL_HIGH_FLOAT ? 23 : 0;
}

void glShaderBinary(GLsizei count, const GLuint *names, GLenum binary_format, const void *binary,
                    GLsizei length)
{
    (void)binary_format;
    (void)binary;
    if (count < 0 || length < 0 || (count && !names)) {
        mesaGLSetError(GL_INVALID_VALUE);
        return;
    }
    mesaGLSetError(GL_INVALID_ENUM);
}

void glGetShaderSource(GLuint name, GLsizei size, GLsizei *length, GLchar *source)
{
    Shader *shader = find_shader(name);

    if (!shader) {
        shader_name_error(name);
        return;
    }
    if (size < 0) {
        mesaGLSetError(GL_INVALID_VALUE);
        return;
    }
    if (!shader->source) {
        copy_log("", size, length, source);
    } else {
        GLsizei copied = size <= 0
                             ? 0
                             : (size_t)(size - 1) < shader->source_length
                                   ? size - 1
                                   : (GLsizei)shader->source_length;

        if (source && size > 0) {
            memcpy(source, shader->source, (size_t)copied);
            source[copied] = '\0';
        }
        if (length)
            *length = copied;
    }
}

GLboolean glIsShader(GLuint name)
{
    return find_shader(name) ? GL_TRUE : GL_FALSE;
}

GLuint glCreateProgram(void)
{
    int i;
    for (i = 0; i < MAX_PROGRAMS; ++i) {
        if (!programs[i].name) {
            programs[i].name = next_shader_program++;
            return programs[i].name;
        }
    }
    mesaGLSetError(GL_OUT_OF_MEMORY);
    return 0;
}

static void destroy_program(Program *program)
{
    GLuint attached[MAX_SHADERS];
    int attached_count;
    int i;

    if (!program)
        return;
    attached_count = program->attached_count;
    memcpy(attached, program->attached, (size_t)attached_count * sizeof(attached[0]));
    free_program_uniforms(program);
    ntglFree(program->linked_vertex_source);
    ntglFree(program->linked_fragment_source);
    ntglFree(program->vertex_body);
    ntglFree(program->fragment_body);
    memset(program, 0, sizeof(*program));
    for (i = 0; i < attached_count; ++i)
        release_deleted_shader(attached[i]);
}

void glDeleteProgram(GLuint name)
{
    Program *program = find_program(name);

    if (!name)
        return;
    if (!program) {
        program_name_error(name);
        return;
    }
    if (current_program == name)
        program->delete_pending = 1;
    else
        destroy_program(program);
}

void glAttachShader(GLuint program_name, GLuint shader_name)
{
    Program *program = find_program(program_name);
    Shader *shader = find_shader(shader_name);
    int i;

    if (!program) {
        program_name_error(program_name);
        return;
    }
    if (!shader) {
        shader_name_error(shader_name);
        return;
    }
    for (i = 0; i < program->attached_count; ++i)
        if (program->attached[i] == shader_name) {
            mesaGLSetError(GL_INVALID_OPERATION);
            return;
        }
    if (program->attached_count >= MAX_SHADERS) {
        mesaGLSetError(GL_OUT_OF_MEMORY);
        return;
    }
    program->attached[program->attached_count++] = shader_name;
}

void glDetachShader(GLuint program_name, GLuint shader_name)
{
    Program *program = find_program(program_name);
    Shader *shader = find_shader(shader_name);
    int i;

    if (!program) {
        program_name_error(program_name);
        return;
    }
    if (!shader) {
        shader_name_error(shader_name);
        return;
    }
    for (i = 0; i < program->attached_count; ++i)
        if (program->attached[i] == shader_name)
            break;
    if (i == program->attached_count) {
        mesaGLSetError(GL_INVALID_OPERATION);
        return;
    }
    memmove(&program->attached[i], &program->attached[i + 1],
            (size_t)(program->attached_count - i - 1) * sizeof(program->attached[0]));
    --program->attached_count;
    release_deleted_shader(shader_name);
}

void glBindAttribLocation(GLuint program_name, GLuint index, const GLchar *name)
{
    Program *program = find_program(program_name);
    Binding *binding;
    int i;

    if (!program) {
        program_name_error(program_name);
        return;
    }
    if (!name || index >= MAX_ATTRIBUTES) {
        mesaGLSetError(GL_INVALID_VALUE);
        return;
    }
    if (!strncmp(name, "gl_", 3)) {
        mesaGLSetError(GL_INVALID_OPERATION);
        return;
    }
    for (i = 0; i < program->binding_count; ++i) {
        binding = &program->bindings[i];
        if (binding->requested && !strcmp(binding->name, name)) {
            binding->requested_index = index;
            return;
        }
    }
    if (program->binding_count >= MAX_BINDINGS) {
        mesaGLSetError(GL_OUT_OF_MEMORY);
        return;
    }
    binding = &program->bindings[program->binding_count++];
    strncpy(binding->name, name, sizeof(binding->name) - 1);
    binding->name[sizeof(binding->name) - 1] = '\0';
    binding->index = index;
    binding->requested_index = index;
    binding->type = GL_FLOAT_VEC4;
    binding->requested = 1;
    binding->active = 0;
}

static char *combine_stage_sources(const Program *program, GLenum type, int *shader_count,
                                   int *all_compiled)
{
    size_t size = 1;
    char *combined;
    char *output;
    int i;

    *shader_count = 0;
    *all_compiled = 1;
    for (i = 0; i < program->attached_count; ++i) {
        Shader *shader = find_shader(program->attached[i]);

        if (!shader || shader->type != type)
            continue;
        ++*shader_count;
        if (!shader->compiled || !shader->compiled_source) {
            *all_compiled = 0;
            continue;
        }
        if (strlen(shader->compiled_source) > SIZE_MAX - size - 1)
            return NULL;
        size += strlen(shader->compiled_source) + 1;
    }
    if (!*shader_count || !*all_compiled)
        return NULL;
    combined = (char *)ntglAlloc(size);
    if (!combined)
        return NULL;
    output = combined;
    for (i = 0; i < program->attached_count; ++i) {
        Shader *shader = find_shader(program->attached[i]);
        size_t length;

        if (!shader || shader->type != type)
            continue;
        length = strlen(shader->compiled_source);
        memcpy(output, shader->compiled_source, length);
        output += length;
        *output++ = '\n';
    }
    *output = '\0';
    return combined;
}

#if MESAGL_GLES2_PROFILE == MESAGL_GLES2_PROFILE_FULL
static int stage_has_invariant_all(const Program *program, GLenum type)
{
    int index;

    for (index = 0; index < program->attached_count; ++index) {
        Shader *shader = find_shader(program->attached[index]);

        if (shader && shader->type == type && shader->invariant_all)
            return 1;
    }
    return 0;
}

static int stage_has_invariant_identifier(const char *source,
                                          const char *identifier)
{
    const char *cursor = source;
    size_t identifier_length = strlen(identifier);

    while ((cursor = strstr(cursor, "invariant")) != NULL) {
        const char *name;

        if (!shader_keyword_at(source, cursor, 9)) {
            cursor += 9;
            continue;
        }
        name = skip_shader_space(cursor + 9);
        if (!strncmp(name, "varying", 7) &&
            shader_keyword_at(source, name, 7)) {
            cursor = name + 7;
            continue;
        }
        while (*name) {
            const char *name_end = name;

            while (shader_identifier_character(*name_end))
                ++name_end;
            if ((size_t)(name_end - name) == identifier_length &&
                !strncmp(name, identifier, identifier_length))
                return 1;
            name = skip_shader_space(name_end);
            if (*name == ';')
                break;
            if (*name != ',')
                break;
            name = skip_shader_space(name + 1);
        }
        cursor += 9;
    }
    return 0;
}

static int validate_builtin_invariant_linkage(Program *program)
{
    int position = stage_has_invariant_all(program, GL_VERTEX_SHADER) ||
                   stage_has_invariant_identifier(program->linked_vertex_source,
                                                  "gl_Position");
    int point_size = stage_has_invariant_all(program, GL_VERTEX_SHADER) ||
                     stage_has_invariant_identifier(program->linked_vertex_source,
                                                    "gl_PointSize");
    int frag_coord = stage_has_invariant_identifier(program->linked_fragment_source,
                                                    "gl_FragCoord");
    int point_coord = stage_has_invariant_identifier(program->linked_fragment_source,
                                                     "gl_PointCoord");

    if (position != frag_coord) {
        strcpy(program->log,
               "gl_Position and gl_FragCoord invariance must match across stages");
        return 0;
    }
    if (point_size != point_coord) {
        strcpy(program->log,
               "gl_PointSize and gl_PointCoord invariance must match across stages");
        return 0;
    }
    return 1;
}
#endif

static int main_function_count(const char *source)
{
    const char *cursor = source;
    int count = 0;

    while ((cursor = strstr(cursor, "main")) != NULL) {
        const char *after = cursor + 4;
        const char *before = cursor;

        while (before > source && (before[-1] == ' ' || before[-1] == '\t' ||
                                   before[-1] == '\r' || before[-1] == '\n'))
            --before;
        if (before >= source + 4 && !strncmp(before - 4, "void", 4) &&
            !((*after >= 'a' && *after <= 'z') || (*after >= 'A' && *after <= 'Z') ||
              (*after >= '0' && *after <= '9') || *after == '_'))
            ++count;
        cursor = after;
    }
    return count;
}

static int shader_identifier_character(char character)
{
    return (character >= 'a' && character <= 'z') ||
           (character >= 'A' && character <= 'Z') ||
           (character >= '0' && character <= '9') || character == '_';
}

static const char *matching_parenthesis(const char *open)
{
    const char *cursor = open;
    int depth = 0;

    while (*cursor) {
        if (*cursor == '(')
            ++depth;
        else if (*cursor == ')' && --depth == 0)
            return cursor;
        ++cursor;
    }
    return NULL;
}

static int shader_struct_contains_sampler(const char *source, const char *type,
                                          size_t type_length, int depth);
static int shader_struct_contains_array(const char *source, const char *type,
                                        size_t type_length, int depth);
static int shader_struct_contains_sampler_at(const char *source,
                                             const char *type,
                                             size_t type_length,
                                             const char *position, int depth);
static int shader_struct_contains_array_at(const char *source, const char *type,
                                           size_t type_length,
                                           const char *position, int depth);

static int link_qualifier(const char *token, size_t length)
{
    return (length == 5 && !strncmp(token, "const", 5)) ||
           (length == 2 && !strncmp(token, "in", 2)) ||
           (length == 3 && !strncmp(token, "out", 3)) ||
           (length == 5 && !strncmp(token, "inout", 5)) ||
           (length == 4 && !strncmp(token, "lowp", 4)) ||
           (length == 7 && !strncmp(token, "mediump", 7)) ||
           (length == 5 && !strncmp(token, "highp", 5));
}

static int link_precision_category(const char *type)
{
    if (!strcmp(type, "float") || !strncmp(type, "vec", 3) ||
        !strncmp(type, "mat", 3))
        return 1;
    if (!strcmp(type, "int") || !strncmp(type, "ivec", 4))
        return 2;
    if (!strncmp(type, "sampler2D", 9))
        return 3;
    if (!strncmp(type, "samplerCube", 11))
        return 4;
    return 0;
}

static unsigned char link_effective_precision(const char *source,
                                              const char *position,
                                              const char *type,
                                              unsigned char explicit_precision,
                                              int fragment_stage)
{
    const char *cursor = source;
    int category = link_precision_category(type);
    unsigned char precision = 0;

    if (explicit_precision || !category)
        return explicit_precision;
    while ((cursor = strstr(cursor, "precision")) != NULL && cursor < position) {
        const char *qualifier;
        const char *qualifier_end;
        const char *declared_type;
        const char *declared_type_end;
        unsigned char candidate = 0;
        int declared_category;

        if (!shader_keyword_at(source, cursor, 9) ||
            !link_declaration_visible(source, cursor, position)) {
            cursor += 9;
            continue;
        }
        qualifier = skip_shader_space(cursor + 9);
        qualifier_end = qualifier;
        while (shader_identifier_character(*qualifier_end))
            ++qualifier_end;
        if (qualifier_end - qualifier == 4 && !strncmp(qualifier, "lowp", 4))
            candidate = 1;
        else if (qualifier_end - qualifier == 7 &&
                 !strncmp(qualifier, "mediump", 7))
            candidate = 2;
        else if (qualifier_end - qualifier == 5 &&
                 !strncmp(qualifier, "highp", 5))
            candidate = 3;
        declared_type = skip_shader_space(qualifier_end);
        declared_type_end = declared_type;
        while (shader_identifier_character(*declared_type_end))
            ++declared_type_end;
        if (declared_type_end - declared_type == 5 &&
            !strncmp(declared_type, "float", 5))
            declared_category = 1;
        else if (declared_type_end - declared_type == 3 &&
                 !strncmp(declared_type, "int", 3))
            declared_category = 2;
        else if (declared_type_end - declared_type == 9 &&
                 !strncmp(declared_type, "sampler2D", 9))
            declared_category = 3;
        else if (declared_type_end - declared_type == 11 &&
                 !strncmp(declared_type, "samplerCube", 11))
            declared_category = 4;
        else
            declared_category = 0;
        if (candidate && declared_category == category)
            precision = candidate;
        cursor += 9;
    }
    if (precision)
        return precision;
    if (category == 1)
        return fragment_stage ? 0 : 3;
    if (category == 2)
        return fragment_stage ? 2 : 3;
    return 1;
}

static int copy_link_token(char *destination, size_t size, const char *start, const char *end)
{
    size_t length = (size_t)(end - start);

    if (!length || length >= size)
        return 0;
    memcpy(destination, start, length);
    destination[length] = '\0';
    return 1;
}

static int parse_link_parameter(
    const char *start, const char *end, char output[LINK_TYPE_CAPACITY],
    char parameter_name[MESAGL_MAX_SHADER_IDENTIFIER_LENGTH],
    unsigned char *mode, unsigned char *const_value,
    unsigned char *precision_value)
{
    const char *cursor = start;
    const char *type_start;
    const char *type_end;
    const char *array;
    size_t length;
    int const_qualified = 0;
    int output_qualified = 0;
    int precision_seen = 0;
    int direction_seen = 0;

    *mode = 0;
    *const_value = 0;
    *precision_value = 0;

    do {
        cursor = skip_shader_space(cursor);
        type_start = cursor;
        while (cursor < end && shader_identifier_character(*cursor))
            ++cursor;
        type_end = cursor;
        if (type_start < type_end &&
            link_qualifier(type_start, (size_t)(type_end - type_start))) {
            size_t qualifier_length = (size_t)(type_end - type_start);
            int precision =
                (qualifier_length == 4 && !strncmp(type_start, "lowp", 4)) ||
                (qualifier_length == 7 && !strncmp(type_start, "mediump", 7)) ||
                (qualifier_length == 5 && !strncmp(type_start, "highp", 5));

            if (precision_seen)
                return 0;
            if (precision) {
                precision_seen = 1;
                if (qualifier_length == 4)
                    *precision_value = 1;
                else if (qualifier_length == 7)
                    *precision_value = 2;
                else
                    *precision_value = 3;
            }
            if (qualifier_length == 5 && !strncmp(type_start, "const", 5)) {
                if (const_qualified || direction_seen)
                    return 0;
                const_qualified = 1;
                *const_value = 1;
            }
            if ((qualifier_length == 2 && !strncmp(type_start, "in", 2)) ||
                (qualifier_length == 3 && !strncmp(type_start, "out", 3)) ||
                (qualifier_length == 5 && !strncmp(type_start, "inout", 5))) {
                if (direction_seen)
                    return 0;
                direction_seen = 1;
                if (qualifier_length != 2) {
                    output_qualified = 1;
                    *mode = qualifier_length == 3 ? 1 : 2;
                }
            }
        }
    } while (type_start < type_end && link_qualifier(type_start,
                                                      (size_t)(type_end - type_start)));
    if (const_qualified && output_qualified)
        return 0;
    if (!copy_link_token(output, LINK_TYPE_CAPACITY, type_start, type_end))
        return 0;

    cursor = skip_shader_space(type_end);
    if (cursor < end && shader_identifier_character(*cursor)) {
        const char *name_end = cursor;

        while (name_end < end && shader_identifier_character(*name_end))
            ++name_end;
        if (!copy_link_token(parameter_name,
                             MESAGL_MAX_SHADER_IDENTIFIER_LENGTH,
                             cursor, name_end))
            return 0;
    }

    array = type_end;
    while (array < end && *array != '[')
        ++array;
    if (array == end)
        return 1;
    length = strlen(output);
    if (length + 2 >= LINK_TYPE_CAPACITY)
        return 0;
    output[length++] = '[';
    ++array;
    while (array < end && *array != ']') {
        if (*array != ' ' && *array != '\t' && *array != '\r' && *array != '\n') {
            if (length + 2 >= LINK_TYPE_CAPACITY)
                return 0;
            output[length++] = *array;
        }
        ++array;
    }
    if (array == end)
        return 0;
    output[length++] = ']';
    output[length] = '\0';
    return 1;
}

static int parse_link_parameters(const char *source, const char *open,
                                 const char *close, LinkFunction *function,
                                 int fragment_stage)
{
    const char *cursor = skip_shader_space(open + 1);

    if (cursor == close)
        return 1;
    if (close - cursor == 4 && !strncmp(cursor, "void", 4))
        return 1;
    while (cursor < close) {
        const char *end = cursor;
        int bracket_depth = 0;

        while (end < close) {
            if (*end == '[')
                ++bracket_depth;
            else if (*end == ']')
                --bracket_depth;
            else if (*end == ',' && !bracket_depth)
                break;
            ++end;
        }
        if (function->parameter_count >= MAX_LINK_PARAMETERS ||
            !parse_link_parameter(cursor, end,
                                  function->parameters[function->parameter_count],
                                  function->parameter_names[function->parameter_count],
                                  &function->parameter_modes[function->parameter_count],
                                  &function->parameter_consts[function->parameter_count],
                                  &function->parameter_precisions[function->parameter_count]))
            return 0;
        function->parameter_precisions[function->parameter_count] =
            link_effective_precision(
                source, function->declaration_start,
                function->parameters[function->parameter_count],
                function->parameter_precisions[function->parameter_count],
                fragment_stage);
        {
            int previous;

            for (previous = 0; previous < function->parameter_count; ++previous)
                if (function->parameter_names[function->parameter_count][0] &&
                    !strcmp(function->parameter_names[previous],
                            function->parameter_names[function->parameter_count]))
                    return 0;
        }
        ++function->parameter_count;
        cursor = end < close ? skip_shader_space(end + 1) : end;
    }
    return 1;
}

static int same_link_signature(const LinkFunction *left, const LinkFunction *right)
{
    int parameter;

    if (strcmp(left->name, right->name) || strcmp(left->return_type, right->return_type) ||
        left->parameter_count != right->parameter_count)
        return 0;
    for (parameter = 0; parameter < left->parameter_count; ++parameter)
        if (strcmp(left->parameters[parameter], right->parameters[parameter]) ||
            left->parameter_modes[parameter] != right->parameter_modes[parameter] ||
            left->parameter_consts[parameter] != right->parameter_consts[parameter])
            return 0;
    return 1;
}

static int same_link_parameter_signature(const LinkFunction *left,
                                         const LinkFunction *right)
{
    int parameter;

    if (strcmp(left->name, right->name) ||
        left->parameter_count != right->parameter_count)
        return 0;
    for (parameter = 0; parameter < left->parameter_count; ++parameter)
        if (strcmp(left->parameters[parameter], right->parameters[parameter]))
            return 0;
    return 1;
}

static int link_builtin_name(const char *name, size_t length)
{
    static const char *const names[] = {
        "abs",          "acos",       "all",          "any",
        "asin",         "atan",       "bool",         "bvec2",
        "bvec3",        "bvec4",      "ceil",         "clamp",
        "cos",          "cross",      "degrees",      "dFdx",
        "dFdy",         "distance",   "dot",          "equal",
        "exp",          "exp2",       "faceforward",  "float",
        "floor",        "fract",      "fwidth",       "greaterThan",
        "greaterThanEqual", "int",    "inversesqrt",  "ivec2",
        "ivec3",        "ivec4",      "length",       "lessThan",
        "lessThanEqual", "log",       "log2",         "mat2",
        "mat3",         "mat4",       "matrixCompMult", "max",
        "min",          "mix",        "mod",          "normalize",
        "not",          "notEqual",   "pow",          "radians",
        "reflect",      "refract",    "sign",         "sin",
        "smoothstep",   "sqrt",       "step",         "tan",
        "texture2D",    "texture2DLod", "texture2DProj",
        "texture2DProjLod", "textureCube", "textureCubeLod", "vec2",
        "vec3",         "vec4",
    };
    size_t index;

    for (index = 0; index < sizeof(names) / sizeof(names[0]); ++index)
        if (strlen(names[index]) == length && !strncmp(name, names[index], length))
            return 1;
    return 0;
}

static int link_declaration_visible(const char *source,
                                    const char *declaration,
                                    const char *limit);

static int stage_has_struct_type_at(const char *source, const char *name,
                                    size_t length, const char *position)
{
    const char *cursor = source;

    while ((cursor = strstr(cursor, "struct")) != NULL) {
        const char *type;
        const char *end;
        const char *open;
        const char *close;

        if ((cursor > source && shader_identifier_character(cursor[-1])) ||
            shader_identifier_character(cursor[6])) {
            cursor += 6;
            continue;
        }
        type = skip_shader_space(cursor + 6);
        end = type;
        while (shader_identifier_character(*end))
            ++end;
        open = skip_shader_space(end);
        if (*open != '{') {
            cursor = end;
            continue;
        }
        close = control_matching(open, source + strlen(source), '{', '}');
        if (!close) {
            cursor = open + 1;
            continue;
        }
        if ((size_t)(end - type) == length && !strncmp(type, name, length) &&
            close < position &&
            (!precision_is_unbraced_control_body(source, cursor) ||
             (strchr(close, ';') && position <= strchr(close, ';'))) &&
            link_declaration_visible(source, close + 1, position))
            return 1;
        cursor = close + 1;
    }
    return 0;
}

static int stage_has_struct_type(const char *source, const char *name,
                                 size_t length)
{
    return stage_has_struct_type_at(source, name, length,
                                    source + strlen(source));
}

static int global_declaration_qualifier(const char *token, size_t length)
{
    return link_qualifier(token, length) ||
           (length == 7 && !strncmp(token, "uniform", 7)) ||
           (length == 7 && !strncmp(token, "varying", 7)) ||
           (length == 9 && !strncmp(token, "attribute", 9)) ||
           (length == 9 && !strncmp(token, "invariant", 9));
}

static int declaration_range_has_name(const char *start, const char *end,
                                      const char *name, size_t name_length)
{
    const char *cursor = start;

    while (cursor < end) {
        const char *identifier;
        const char *identifier_end;
        int parentheses = 0;
        int brackets = 0;

        cursor = skip_shader_space(cursor);
        identifier = cursor;
        while (cursor < end && shader_identifier_character(*cursor))
            ++cursor;
        identifier_end = cursor;
        if (identifier == identifier_end)
            return 0;
        cursor = skip_shader_space(cursor);
        if (*cursor == '(')
            return 0;
        if ((size_t)(identifier_end - identifier) == name_length &&
            !strncmp(identifier, name, name_length))
            return 1;
        while (cursor < end) {
            if (*cursor == '(')
                ++parentheses;
            else if (*cursor == ')')
                --parentheses;
            else if (*cursor == '[')
                ++brackets;
            else if (*cursor == ']')
                --brackets;
            else if (*cursor == ',' && !parentheses && !brackets) {
                ++cursor;
                break;
            }
            ++cursor;
        }
    }
    return 0;
}

static int global_variable_named_until(const char *source, const char *source_end,
                                       const char *name, size_t name_length)
{
    const char *cursor = source;

    while ((cursor = skip_shader_space(cursor)) < source_end) {
        const char *semicolon = memchr(cursor, ';', (size_t)(source_end - cursor));
        const char *brace = memchr(cursor, '{', (size_t)(source_end - cursor));
        const char *declarators;

        if (brace && (!semicolon || brace < semicolon)) {
            const char *close = control_matching(brace, source_end, '{', '}');

            if (!close)
                return 0;
            if (!strncmp(cursor, "struct", 6) &&
                !shader_identifier_character(cursor[6])) {
                if (close >= source_end)
                    return 0;
                semicolon = memchr(close, ';', (size_t)(source_end - close));
                if (!semicolon)
                    return 0;
                declarators = close + 1;
                if (declaration_range_has_name(declarators, semicolon, name,
                                               name_length))
                    return 1;
                cursor = semicolon + 1;
                continue;
            }
            cursor = close + 1;
            continue;
        }
        if (!semicolon)
            break;
        if (!strncmp(cursor, "precision", 9) &&
            !shader_identifier_character(cursor[9])) {
            cursor = semicolon + 1;
            continue;
        }
        declarators = cursor;
        for (;;) {
            const char *token = declarators;
            const char *token_end;

            while (token < semicolon && !shader_identifier_character(*token))
                ++token;
            token_end = token;
            while (token_end < semicolon && shader_identifier_character(*token_end))
                ++token_end;
            if (token == token_end) {
                declarators = semicolon;
                break;
            }
            declarators = token_end;
            if (!global_declaration_qualifier(token,
                                              (size_t)(token_end - token)))
                break;
        }
        if (declaration_range_has_name(declarators, semicolon, name,
                                       name_length))
            return 1;
        cursor = semicolon + 1;
    }
    return 0;
}

static int global_variable_named(const char *source, const char *name,
                                 size_t name_length)
{
    return global_variable_named_until(source, source + strlen(source), name,
                                       name_length);
}

typedef struct LinkGlobal {
    char name[MESAGL_MAX_SHADER_IDENTIFIER_LENGTH];
    int interface_declaration;
} LinkGlobal;

static int copy_known_link_type(const char *source, const char *start,
                                const char *end, char output[LINK_TYPE_CAPACITY]);
static int link_open_braces(const char *source, const char *limit,
                            const char **braces, int capacity);

static int collect_global_declarators(Program *program, const char *start,
                                      const char *end, int interface_declaration,
                                      LinkGlobal *globals, int *global_count)
{
    const char *cursor = start;

    while (cursor < end) {
        const char *identifier;
        const char *identifier_end;
        size_t length;
        int previous;
        int parentheses = 0;
        int brackets = 0;

        cursor = skip_shader_space(cursor);
        identifier = cursor;
        while (cursor < end && shader_identifier_character(*cursor))
            ++cursor;
        identifier_end = cursor;
        if (identifier == identifier_end)
            return 1;
        cursor = skip_shader_space(cursor);
        if (*cursor == '(')
            return 1;
        length = (size_t)(identifier_end - identifier);
        if (length >= sizeof(globals[0].name)) {
            strcpy(program->log, "global variable name is too long");
            return 0;
        }
        for (previous = 0; previous < *global_count; ++previous) {
            if (strlen(globals[previous].name) == length &&
                !strncmp(globals[previous].name, identifier, length)) {
                if (!interface_declaration ||
                    !globals[previous].interface_declaration) {
                    snprintf(program->log, sizeof(program->log),
                             "duplicate global variable declaration: %.*s",
                             (int)length, identifier);
                    return 0;
                }
                break;
            }
        }
        if (previous == *global_count) {
            if (*global_count >= 128) {
                strcpy(program->log, "too many global variable declarations");
                return 0;
            }
            memset(&globals[*global_count], 0, sizeof(globals[*global_count]));
            memcpy(globals[*global_count].name, identifier, length);
            globals[*global_count].interface_declaration = interface_declaration;
            ++*global_count;
        }
        while (cursor < end) {
            if (*cursor == '(')
                ++parentheses;
            else if (*cursor == ')')
                --parentheses;
            else if (*cursor == '[')
                ++brackets;
            else if (*cursor == ']')
                --brackets;
            else if (*cursor == ',' && !parentheses && !brackets) {
                ++cursor;
                break;
            }
            ++cursor;
        }
    }
    return 1;
}

static int validate_global_variable_declarations(Program *program,
                                                 const char *source)
{
    LinkGlobal globals[128];
    const char *cursor = source;
    const char *source_end = source + strlen(source);
    int global_count = 0;

    memset(globals, 0, sizeof(globals));
    while ((cursor = skip_shader_space(cursor)) < source_end) {
        const char *semicolon = strchr(cursor, ';');
        const char *brace = strchr(cursor, '{');
        const char *declarators;
        const char *type = NULL;
        const char *type_end = NULL;
        int interface_declaration = 0;
        int invariant_declaration = 0;

        if (brace && (!semicolon || brace < semicolon)) {
            const char *close = control_matching(brace, source_end, '{', '}');

            if (!close)
                return 1;
            if (!strncmp(cursor, "struct", 6) &&
                !shader_identifier_character(cursor[6])) {
                semicolon = strchr(close, ';');
                if (!semicolon)
                    return 1;
                if (!collect_global_declarators(program, close + 1, semicolon, 0,
                                                globals, &global_count))
                    return 0;
                cursor = semicolon + 1;
                continue;
            }
            cursor = close + 1;
            continue;
        }
        if (!semicolon)
            break;
        if (!strncmp(cursor, "precision", 9) &&
            !shader_identifier_character(cursor[9])) {
            cursor = semicolon + 1;
            continue;
        }
        declarators = cursor;
        for (;;) {
            const char *token = declarators;
            const char *token_end;
            size_t token_length;

            while (token < semicolon && !shader_identifier_character(*token))
                ++token;
            token_end = token;
            while (token_end < semicolon && shader_identifier_character(*token_end))
                ++token_end;
            if (token == token_end) {
                declarators = semicolon;
                break;
            }
            token_length = (size_t)(token_end - token);
            declarators = token_end;
            if ((token_length == 7 && !strncmp(token, "uniform", 7)) ||
                (token_length == 7 && !strncmp(token, "varying", 7)) ||
                (token_length == 9 && !strncmp(token, "attribute", 9)))
                interface_declaration = 1;
            if (token_length == 9 && !strncmp(token, "invariant", 9))
                invariant_declaration = 1;
            if (!global_declaration_qualifier(token, token_length)) {
                type = token;
                type_end = token_end;
                break;
            }
        }
        if (invariant_declaration && !interface_declaration && type &&
            type_end - type >= 3 && !strncmp(type, "gl_", 3)) {
            cursor = semicolon + 1;
            continue;
        }
        if (type && !(type_end - type == 4 && !strncmp(type, "void", 4)) &&
            !copy_known_link_type(source, type, type_end, (char[LINK_TYPE_CAPACITY]){0})) {
            snprintf(program->log, sizeof(program->log),
                     "unknown global declaration type: %.*s",
                     (int)(type_end - type), type);
            return 0;
        }
        if (!collect_global_declarators(program, declarators, semicolon,
                                        interface_declaration, globals,
                                        &global_count))
            return 0;
        cursor = semicolon + 1;
    }
    return 1;
}

static int validate_struct_type_namespace(Program *program, const char *source)
{
    const char *cursor = source;
    const char *names[128];
    const char *declarations[128];
    size_t lengths[128];
    int structure_count = 0;

    while ((cursor = strstr(cursor, "struct")) != NULL) {
        const char *name;
        const char *name_end;
        const char *scopes[64];
        size_t length;
        int previous;

        if (!shader_keyword_at(source, cursor, 6)) {
            cursor += 6;
            continue;
        }
        name = skip_shader_space(cursor + 6);
        name_end = name;
        while (shader_identifier_character(*name_end))
            ++name_end;
        length = (size_t)(name_end - name);
        for (previous = 0; previous < structure_count; ++previous) {
            const char *left_scopes[64];
            const char *right_scopes[64];
            int left_depth;
            int right_depth;
            int depth;
            int same_scope = 1;

            if (lengths[previous] != length ||
                strncmp(names[previous], name, length))
                continue;
            if (precision_is_unbraced_control_body(source, cursor) ||
                precision_is_unbraced_control_body(source,
                                                    declarations[previous]))
                continue;
            left_depth = link_open_braces(source, declarations[previous],
                                          left_scopes, 64);
            right_depth = link_open_braces(source, cursor, right_scopes, 64);
            if (left_depth != right_depth || left_depth < 0)
                continue;
            for (depth = 0; depth < left_depth; ++depth)
                if (left_scopes[depth] != right_scopes[depth]) {
                    same_scope = 0;
                    break;
                }
            if (same_scope) {
                snprintf(program->log, sizeof(program->log),
                         "duplicate structure type declaration: %.*s",
                         (int)length, name);
                return 0;
            }
        }
        if (length && structure_count < 128) {
            names[structure_count] = name;
            lengths[structure_count] = length;
            declarations[structure_count++] = cursor;
        }
        if (length && link_open_braces(source, cursor, scopes, 64) == 0 &&
            global_variable_named(source, name, length)) {
            snprintf(program->log, sizeof(program->log),
                     "structure type conflicts with a global variable: %.*s",
                     (int)length, name);
            return 0;
        }
        cursor = name_end > cursor ? name_end : cursor + 6;
    }
    return 1;
}

static int link_call_argument_count(const char *open, const char *close)
{
    const char *cursor = skip_shader_space(open + 1);
    int parentheses = 0;
    int brackets = 0;
    int braces = 0;
    int count = 1;

    if (cursor == close ||
        (close - cursor == 4 && !strncmp(cursor, "void", 4)))
        return 0;
    while (cursor < close) {
        if (*cursor == '(')
            ++parentheses;
        else if (*cursor == ')')
            --parentheses;
        else if (*cursor == '[')
            ++brackets;
        else if (*cursor == ']')
            --brackets;
        else if (*cursor == '{')
            ++braces;
        else if (*cursor == '}')
            --braces;
        else if (*cursor == ',' && !parentheses && !brackets && !braces)
            ++count;
        ++cursor;
    }
    return count;
}

static int link_constructor_type(const char *name, size_t length,
                                 char output[LINK_TYPE_CAPACITY])
{
    static const char *const types[] = {
        "float", "int", "bool", "vec2", "vec3", "vec4", "ivec2", "ivec3",
        "ivec4", "bvec2", "bvec3", "bvec4", "mat2", "mat3", "mat4",
    };
    size_t index;

    for (index = 0; index < sizeof(types) / sizeof(types[0]); ++index)
        if (strlen(types[index]) == length && !strncmp(types[index], name, length)) {
            strcpy(output, types[index]);
            return 1;
        }
    return 0;
}

static int copy_known_link_type(const char *source, const char *start, const char *end,
                                char output[LINK_TYPE_CAPACITY])
{
    size_t length = (size_t)(end - start);

    if (link_constructor_type(start, length, output))
        return 1;
    if (length == 9 && !strncmp(start, "sampler2D", 9)) {
        strcpy(output, "sampler2D");
        return 1;
    }
    if (length == 11 && !strncmp(start, "samplerCube", 11)) {
        strcpy(output, "samplerCube");
        return 1;
    }
    return stage_has_struct_type_at(source, start, length, start) &&
           copy_link_token(output, LINK_TYPE_CAPACITY, start, end);
}

static int link_open_braces(const char *source, const char *limit,
                            const char **braces, int capacity)
{
    const char *cursor = source;
    int count = 0;

    while (cursor < limit) {
        if (cursor + 1 < limit && cursor[0] == '/' && cursor[1] == '/') {
            cursor += 2;
            while (cursor < limit && *cursor != '\n')
                ++cursor;
            continue;
        }
        if (cursor + 1 < limit && cursor[0] == '/' && cursor[1] == '*') {
            cursor += 2;
            while (cursor + 1 < limit &&
                   !(cursor[0] == '*' && cursor[1] == '/'))
                ++cursor;
            if (cursor + 1 < limit)
                cursor += 2;
            continue;
        }
        if (*cursor == '{') {
            if (count >= capacity)
                return -1;
            braces[count++] = cursor;
        } else if (*cursor == '}' && count > 0) {
            --count;
        }
        ++cursor;
    }
    return count;
}

static int link_parameter_body(const char *source, const char *name,
                               const char **body_start, const char **body_end)
{
    const char *cursor = name;
    const char *open = NULL;
    int parentheses = 0;

    while (cursor > source) {
        char character = cursor[-1];

        --cursor;
        if (character == ')')
            ++parentheses;
        else if (character == '(') {
            if (!parentheses) {
                open = cursor;
                break;
            }
            --parentheses;
        } else if (!parentheses && (character == ';' || character == '{' ||
                                    character == '}')) {
            break;
        }
    }
    if (!open)
        return 0;
    {
        const char *function_end = open;
        const char *function_start;
        const char *close = matching_parenthesis(open);
        const char *body;

        while (function_end > source &&
               (function_end[-1] == ' ' || function_end[-1] == '\t' ||
                function_end[-1] == '\r' || function_end[-1] == '\n'))
            --function_end;
        function_start = function_end;
        while (function_start > source &&
               shader_identifier_character(function_start[-1]))
            --function_start;
        if (function_start == function_end ||
            (function_end - function_start == 3 &&
             !strncmp(function_start, "for", 3)) ||
            (function_end - function_start == 2 &&
             !strncmp(function_start, "if", 2)) ||
            (function_end - function_start == 5 &&
             !strncmp(function_start, "while", 5)) ||
            (function_end - function_start == 6 &&
             !strncmp(function_start, "switch", 6)))
            return 0;
        if (!close || name >= close)
            return 0;
        body = skip_shader_space(close + 1);
        if (*body != '{')
            return 0;
        *body_start = body;
        *body_end = control_matching(body, source + strlen(source), '{', '}');
        return *body_end != NULL;
    }
}

static int link_for_declaration_end(const char *source, const char *name,
                                    const char **scope_end)
{
    const char *open = name;
    const char *word_end;
    const char *word_start;

    while (open > source && *--open != '(')
        if (*open == ';' || *open == '{' || *open == '}')
            return 0;
    if (*open != '(')
        return 0;
    word_end = open;
    while (word_end > source &&
           (word_end[-1] == ' ' || word_end[-1] == '\t' || word_end[-1] == '\r' ||
            word_end[-1] == '\n'))
        --word_end;
    word_start = word_end;
    while (word_start > source && shader_identifier_character(word_start[-1]))
        --word_start;
    if (word_end - word_start != 3 || strncmp(word_start, "for", 3))
        return 0;
    {
        const char *close = matching_parenthesis(open);
        const char *body;

        if (!close || name >= close)
            return 0;
        body = skip_shader_space(close + 1);
        if (*body == '{') {
            *scope_end = control_matching(body, source + strlen(source), '{', '}');
            return *scope_end != NULL;
        }
        *scope_end = body;
        while (**scope_end && **scope_end != ';')
            ++*scope_end;
        return **scope_end == ';';
    }
}

static int link_loop_condition_declaration_end(const char *source,
                                               const char *name,
                                               const char **scope_end)
{
    const char *open = name;
    const char *close;
    const char *word_end;
    const char *word_start;
    const char *body;
    int nested = 0;
    char log[LOG_SIZE] = {0};

    while (open > source) {
        --open;
        if (*open == ')')
            ++nested;
        else if (*open == '(') {
            if (!nested)
                break;
            --nested;
        } else if (!nested && (*open == '{' || *open == '}'))
            return 0;
    }
    if (*open != '(' || !(close = matching_parenthesis(open)) || name >= close)
        return 0;
    word_end = open;
    while (word_end > source &&
           (word_end[-1] == ' ' || word_end[-1] == '\t' ||
            word_end[-1] == '\r' || word_end[-1] == '\n'))
        --word_end;
    word_start = word_end;
    while (word_start > source && shader_identifier_character(word_start[-1]))
        --word_start;
    if (word_end - word_start == 3 && !strncmp(word_start, "for", 3)) {
        const char *first = open + 1;
        const char *second;
        int parentheses = 0;

        while (first < close) {
            if (*first == '(')
                ++parentheses;
            else if (*first == ')')
                --parentheses;
            else if (*first == ';' && !parentheses)
                break;
            ++first;
        }
        second = first < close ? first + 1 : close;
        while (second < close) {
            if (*second == '(')
                ++parentheses;
            else if (*second == ')')
                --parentheses;
            else if (*second == ';' && !parentheses)
                break;
            ++second;
        }
        if (name <= first || name >= second)
            return 0;
    } else if (word_end - word_start != 5 ||
               strncmp(word_start, "while", 5)) {
        return 0;
    }
    body = close + 1;
    return validate_control_statement(body, source + strlen(source), 1,
                                      scope_end, log);
}

static int link_unbraced_declaration_end(const char *source,
                                         const char *declaration,
                                         const char **scope_end)
{
    const char *type_end = declaration;
    const char *type_start;
    const char *semicolon;

    while (type_end > source &&
           (type_end[-1] == ' ' || type_end[-1] == '\t' ||
            type_end[-1] == '\r' || type_end[-1] == '\n'))
        --type_end;
    type_start = type_end;
    while (type_start > source &&
           shader_identifier_character(type_start[-1]))
        --type_start;
    if (type_start == type_end ||
        !precision_is_unbraced_control_body(source, type_start))
        return 0;
    semicolon = strchr(declaration, ';');
    if (!semicolon)
        return 0;
    *scope_end = semicolon + 1;
    return 1;
}

static int link_declaration_initializer_end(const char *declaration,
                                            const char **initializer_end)
{
    const char *cursor = declaration;
    int parentheses = 0;
    int brackets = 0;

    while (shader_identifier_character(*cursor))
        ++cursor;
    cursor = skip_shader_space(cursor);
    if (*cursor == '[') {
        const char *close = control_matching(
            cursor, cursor + strlen(cursor), '[', ']');

        if (!close)
            return 0;
        cursor = skip_shader_space(close + 1);
    }
    if (*cursor != '=' || cursor[1] == '=')
        return 0;
    ++cursor;
    while (*cursor) {
        if (*cursor == '(')
            ++parentheses;
        else if (*cursor == ')' && parentheses)
            --parentheses;
        else if (*cursor == '[')
            ++brackets;
        else if (*cursor == ']' && brackets)
            --brackets;
        else if (!parentheses && !brackets &&
                 (*cursor == ',' || *cursor == ';')) {
            *initializer_end = cursor;
            return 1;
        }
        ++cursor;
    }
    return 0;
}

static int link_declaration_visible(const char *source, const char *declaration,
                                    const char *limit)
{
    const char *declaration_braces[64];
    const char *limit_braces[64];
    const char *parameter_body_start;
    const char *parameter_body_end;
    const char *for_scope_end;
    int declaration_depth;
    int limit_depth;
    int depth;

    declaration_depth = link_open_braces(source, declaration,
                                         declaration_braces, 64);
    limit_depth = link_open_braces(source, limit, limit_braces, 64);
    if (declaration_depth < 0 || limit_depth < 0)
        return 0;
    if (link_parameter_body(source, declaration, &parameter_body_start,
                            &parameter_body_end)) {
        if (limit <= parameter_body_start || limit >= parameter_body_end ||
            declaration_depth >= 64)
            return 0;
        declaration_braces[declaration_depth++] = parameter_body_start;
    }
    if (link_for_declaration_end(source, declaration, &for_scope_end) &&
        limit > for_scope_end)
        return 0;
    if (link_loop_condition_declaration_end(source, declaration,
                                            &for_scope_end) &&
        limit >= for_scope_end)
        return 0;
    if (link_unbraced_declaration_end(source, declaration, &for_scope_end) &&
        limit >= for_scope_end)
        return 0;
    if (link_declaration_initializer_end(declaration, &for_scope_end) &&
        limit <= for_scope_end)
        return 0;
    if (declaration_depth > limit_depth)
        return 0;
    for (depth = 0; depth < declaration_depth; ++depth)
        if (declaration_braces[depth] != limit_braces[depth])
            return 0;
    return 1;
}

static int infer_link_declarator_list_type(const char *source,
                                           const char *declaration_name,
                                           size_t name_length,
                                           char output[LINK_TYPE_CAPACITY])
{
    const char *statement = declaration_name;
    const char *cursor;
    const char *token;
    const char *token_end;
    int parentheses = 0;
    int brackets = 0;

    while (statement > source) {
        char character = statement[-1];

        if (character == ')')
            ++parentheses;
        else if (character == '(') {
            if (!parentheses && !brackets)
                break;
            --parentheses;
        } else if (character == ']')
            ++brackets;
        else if (character == '[')
            --brackets;
        else if (!parentheses && !brackets &&
                 (character == ';' || character == '{' || character == '}'))
            break;
        --statement;
    }
    cursor = skip_shader_space(statement);
    token = cursor;
    token_end = token;
    while (shader_identifier_character(*token_end))
        ++token_end;
    while (token < token_end &&
           (link_qualifier(token, (size_t)(token_end - token)) ||
            global_declaration_qualifier(token,
                                         (size_t)(token_end - token)))) {
        token = skip_shader_space(token_end);
        token_end = token;
        while (shader_identifier_character(*token_end))
            ++token_end;
    }
    if (token == token_end ||
        !copy_known_link_type(source, token, token_end, output))
        return 0;
    cursor = skip_shader_space(token_end);
    while (*cursor && *cursor != ';' && *cursor != ')') {
        const char *name = cursor;
        const char *name_end = name;
        const char *after;
        int parentheses = 0;
        int brackets = 0;

        if (!shader_identifier_character(*name))
            return 0;
        while (shader_identifier_character(*name_end))
            ++name_end;
        after = skip_shader_space(name_end);
        if (name == declaration_name &&
            (size_t)(name_end - name) == name_length) {
            if (*after == '(')
                return 0;
            if (*after == '[') {
                const char *close = strchr(after + 1, ']');
                size_t length = strlen(output);

                if (!close || length + (size_t)(close - after) + 1 >= LINK_TYPE_CAPACITY)
                    return 0;
                while (after <= close) {
                    if (*after != ' ' && *after != '\t' && *after != '\r' &&
                        *after != '\n')
                        output[length++] = *after;
                    ++after;
                }
                output[length] = '\0';
            }
            return 1;
        }
        cursor = after;
        while (*cursor) {
            if (*cursor == '(')
                ++parentheses;
            else if (*cursor == ')') {
                if (!parentheses)
                    return 0;
                --parentheses;
            } else if (*cursor == '[')
                ++brackets;
            else if (*cursor == ']')
                --brackets;
            else if (!parentheses && !brackets && *cursor == ',') {
                cursor = skip_shader_space(cursor + 1);
                break;
            } else if (!parentheses && !brackets && *cursor == ';') {
                return 0;
            }
            ++cursor;
        }
    }
    return 0;
}

static int infer_link_declared_type(const char *source, const char *limit,
                                    const char *name, size_t name_length,
                                    char output[LINK_TYPE_CAPACITY])
{
    const char *cursor = source;
    int found = 0;

    while (cursor < limit) {
        const char *match = cursor;
        const char *type_end;
        const char *type_start;
        const char *after;
        char type[LINK_TYPE_CAPACITY];

        while (match < limit &&
               (strncmp(match, name, name_length) ||
                (match > source && shader_identifier_character(match[-1])) ||
                shader_identifier_character(match[name_length])))
            ++match;
        if (match >= limit)
            break;
        type_end = match;
        while (type_end > source &&
               (type_end[-1] == ' ' || type_end[-1] == '\t' || type_end[-1] == '\r' ||
                type_end[-1] == '\n'))
            --type_end;
        type_start = type_end;
        while (type_start > source && shader_identifier_character(type_start[-1]))
            --type_start;
        after = skip_shader_space(match + name_length);
        if (link_declaration_visible(source, match, limit) &&
            (((type_start < type_end &&
               copy_known_link_type(source, type_start, type_end, type) &&
               (*after == '=' || *after == ';' || *after == ',' || *after == ')' ||
                *after == '['))) ||
             infer_link_declarator_list_type(source, match, name_length, type))) {
            strcpy(output, type);
            if (*after == '[') {
                const char *close = strchr(after + 1, ']');
                size_t length = strlen(output);

                if (close && close < limit && length + (size_t)(close - after) + 1 < LINK_TYPE_CAPACITY) {
                    const char *array = after;

                    while (array <= close) {
                        if (*array != ' ' && *array != '\t' && *array != '\r' &&
                            *array != '\n')
                            output[length++] = *array;
                        ++array;
                    }
                    output[length] = '\0';
                }
            }
            found = 1;
        }
        cursor = match + name_length;
    }
    return found;
}

static const char *find_link_struct_definition(const char *source,
                                               const char *type,
                                               size_t type_length,
                                               const char *position)
{
    const char *cursor = source;
    const char *selected = NULL;

    while ((cursor = strstr(cursor, "struct")) != NULL) {
        const char *name;
        const char *name_end;
        const char *open;
        const char *close;

        if ((cursor > source && shader_identifier_character(cursor[-1])) ||
            shader_identifier_character(cursor[6])) {
            cursor += 6;
            continue;
        }
        name = skip_shader_space(cursor + 6);
        name_end = name;
        while (shader_identifier_character(*name_end))
            ++name_end;
        open = skip_shader_space(name_end);
        if (*open != '{' || !(close = control_matching(
                                  open, source + strlen(source), '{', '}'))) {
            cursor += 6;
            continue;
        }
        if ((size_t)(name_end - name) == type_length &&
            !strncmp(name, type, type_length) && close < position &&
            (!precision_is_unbraced_control_body(source, cursor) ||
             (strchr(close, ';') && position <= strchr(close, ';'))) &&
            link_declaration_visible(source, close + 1, position))
            selected = cursor;
        cursor = close + 1;
    }
    return selected;
}

static int infer_link_struct_member_type(const char *source,
                                         const char *structure_type,
                                         const char *position,
                                         const char *member,
                                         size_t member_length,
                                         char output[LINK_TYPE_CAPACITY])
{
    const char *cursor;
    size_t type_length = strlen(structure_type);

    cursor = find_link_struct_definition(source, structure_type, type_length,
                                         position);
    if (cursor) {
        const char *name_end;
        const char *open;
        const char *close;
        const char *field;

        name_end = skip_shader_space(cursor + 6);
        while (shader_identifier_character(*name_end))
            ++name_end;
        open = skip_shader_space(name_end);
        if (*open != '{')
            return 0;
        close = control_matching(open, source + strlen(source), '{', '}');
        if (!close)
            return 0;
        field = open + 1;
        while (field < close) {
            const char *field_type;
            const char *field_type_end = NULL;
            const char *declarator;
            char resolved_type[LINK_TYPE_CAPACITY];

            field = skip_shader_space(field);
            if (field >= close)
                break;
            field_type = field;
            while (field_type < close) {
                field_type_end = field_type;
                while (field_type_end < close &&
                       shader_identifier_character(*field_type_end))
                    ++field_type_end;
                if (field_type == field_type_end)
                    return 0;
                if (!link_qualifier(field_type,
                                    (size_t)(field_type_end - field_type)))
                    break;
                field_type = skip_shader_space(field_type_end);
            }
            if (!copy_known_link_type(source, field_type, field_type_end,
                                      resolved_type))
                return 0;
            declarator = skip_shader_space(field_type_end);
            while (declarator < close) {
                const char *declarator_end = declarator;
                const char *after;

                while (declarator_end < close &&
                       shader_identifier_character(*declarator_end))
                    ++declarator_end;
                if (declarator == declarator_end)
                    return 0;
                after = skip_shader_space(declarator_end);
                if ((size_t)(declarator_end - declarator) == member_length &&
                    !strncmp(declarator, member, member_length)) {
                    strcpy(output, resolved_type);
                    if (*after == '[') {
                        const char *array_end = strchr(after + 1, ']');
                        size_t length = strlen(output);

                        if (!array_end || array_end >= close ||
                            length + (size_t)(array_end - after) + 1 >= LINK_TYPE_CAPACITY)
                            return 0;
                        while (after <= array_end)
                            output[length++] = *after++;
                        output[length] = '\0';
                    }
                    return 1;
                }
                while (after < close && *after != ',' && *after != ';') {
                    if (*after == '[') {
                        const char *array_end = strchr(after + 1, ']');

                        if (!array_end || array_end >= close)
                            return 0;
                        after = array_end;
                    }
                    ++after;
                }
                if (after >= close)
                    return 0;
                if (*after == ';') {
                    field = after + 1;
                    break;
                }
                declarator = skip_shader_space(after + 1);
            }
        }
        return 0;
    }
    return 0;
}

static int link_struct_member_count(const char *source,
                                    const char *structure_type,
                                    const char *position)
{
    const char *cursor;
    size_t type_length = strlen(structure_type);

    cursor = find_link_struct_definition(source, structure_type, type_length,
                                         position);
    if (cursor) {
        const char *name_end;
        const char *open;
        const char *close;
        const char *field;
        int count = 0;

        name_end = skip_shader_space(cursor + 6);
        while (shader_identifier_character(*name_end))
            ++name_end;
        open = skip_shader_space(name_end);
        if (*open != '{')
            return -1;
        close = control_matching(open, source + strlen(source), '{', '}');
        if (!close)
            return -1;
        field = open + 1;
        while (field < close) {
            const char *semicolon = field;
            int brackets = 0;
            int braces = 0;

            field = skip_shader_space(field);
            if (field >= close)
                break;
            semicolon = field;

            while (semicolon < close) {
                if (*semicolon == '[')
                    ++brackets;
                else if (*semicolon == ']')
                    --brackets;
                else if (*semicolon == '{')
                    ++braces;
                else if (*semicolon == '}')
                    --braces;
                else if (*semicolon == ';' && !brackets && !braces)
                    break;
                ++semicolon;
            }
            if (semicolon >= close)
                return -1;
            ++count;
            for (const char *scan = field; scan < semicolon; ++scan) {
                if (*scan == '[')
                    ++brackets;
                else if (*scan == ']')
                    --brackets;
                else if (*scan == '{')
                    ++braces;
                else if (*scan == '}')
                    --braces;
                else if (*scan == ',' && !brackets && !braces)
                    ++count;
            }
            field = semicolon + 1;
        }
        return count;
    }
    return -1;
}

static int link_struct_member_type_at(const char *source,
                                      const char *structure_type,
                                      const char *position, int wanted_index,
                                      char output[LINK_TYPE_CAPACITY])
{
    const char *cursor;
    size_t type_length = strlen(structure_type);

    cursor = find_link_struct_definition(source, structure_type, type_length,
                                         position);
    if (cursor) {
        const char *name_end;
        const char *open;
        const char *close;
        const char *field;
        int member_index = 0;

        name_end = skip_shader_space(cursor + 6);
        while (shader_identifier_character(*name_end))
            ++name_end;
        open = skip_shader_space(name_end);
        if (*open != '{')
            return 0;
        close = control_matching(open, source + strlen(source), '{', '}');
        if (!close)
            return 0;
        field = open + 1;
        while (field < close) {
            const char *type_start;
            const char *type_end = NULL;
            const char *declarator;
            char base_type[LINK_TYPE_CAPACITY];

            field = skip_shader_space(field);
            if (field >= close)
                break;
            type_start = field;
            while (type_start < close) {
                type_end = type_start;
                while (type_end < close && shader_identifier_character(*type_end))
                    ++type_end;
                if (type_start == type_end)
                    return 0;
                if (!link_qualifier(type_start, (size_t)(type_end - type_start)))
                    break;
                type_start = skip_shader_space(type_end);
            }
            if (!copy_known_link_type(source, type_start, type_end, base_type))
                return 0;
            declarator = skip_shader_space(type_end);
            while (declarator < close) {
                const char *declarator_end = declarator;
                const char *after;

                while (declarator_end < close &&
                       shader_identifier_character(*declarator_end))
                    ++declarator_end;
                if (declarator == declarator_end)
                    return 0;
                after = skip_shader_space(declarator_end);
                if (member_index++ == wanted_index) {
                    strcpy(output, base_type);
                    if (*after == '[') {
                        const char *array_end = strchr(after + 1, ']');
                        size_t length = strlen(output);

                        if (!array_end || array_end >= close ||
                            length + (size_t)(array_end - after) + 1 >= LINK_TYPE_CAPACITY)
                            return 0;
                        while (after <= array_end) {
                            if (*after != ' ' && *after != '\t' &&
                                *after != '\r' && *after != '\n')
                                output[length++] = *after;
                            ++after;
                        }
                        output[length] = '\0';
                    }
                    return 1;
                }
                while (after < close && *after != ',' && *after != ';') {
                    if (*after == '[') {
                        const char *array_end = strchr(after + 1, ']');

                        if (!array_end || array_end >= close)
                            return 0;
                        after = array_end;
                    }
                    ++after;
                }
                if (after >= close)
                    return 0;
                if (*after == ';') {
                    field = after + 1;
                    break;
                }
                declarator = skip_shader_space(after + 1);
            }
        }
        return 0;
    }
    return 0;
}

static void apply_inferred_swizzle(const char *source,
                                   char output[LINK_TYPE_CAPACITY],
                                   const char *suffix, const char *end)
{
    suffix = skip_shader_space(suffix);
    while (suffix < end && *suffix == '[') {
        const char *close = strchr(suffix + 1, ']');
        char *array_suffix;

        if (!close || close >= end) {
            output[0] = '\0';
            return;
        }
        array_suffix = strchr(output, '[');
        if (array_suffix)
            *array_suffix = '\0';
        else if (!strncmp(output, "vec", 3))
            strcpy(output, "float");
        else if (!strncmp(output, "ivec", 4))
            strcpy(output, "int");
        else if (!strncmp(output, "bvec", 4))
            strcpy(output, "bool");
        else if (!strncmp(output, "mat", 3) && output[3] >= '2' &&
                 output[3] <= '4' && !output[4]) {
            char dimension = output[3];

            snprintf(output, LINK_TYPE_CAPACITY, "vec%c", dimension);
        } else {
            output[0] = '\0';
            return;
        }
        suffix = skip_shader_space(close + 1);
    }
    if (suffix < end && *suffix == '.') {
        const char *swizzle = suffix + 1;
        const char *swizzle_end = swizzle;
        size_t swizzle_length;
        char prefix = output[0] == 'i' ? 'i' : output[0] == 'b' ? 'b' : '\0';

        while (swizzle_end < end && shader_identifier_character(*swizzle_end))
            ++swizzle_end;
        swizzle_length = (size_t)(swizzle_end - swizzle);
        if (stage_has_struct_type_at(source, output, strlen(output), suffix)) {
            char member_type[LINK_TYPE_CAPACITY];

            if (!infer_link_struct_member_type(source, output, suffix, swizzle,
                                               swizzle_length, member_type)) {
                output[0] = '\0';
                return;
            }
            strcpy(output, member_type);
            apply_inferred_swizzle(source, output, swizzle_end, end);
            return;
        }
        if (!swizzle_length || swizzle_length > 4) {
            output[0] = '\0';
            return;
        }
        if (swizzle_length == 1)
            strcpy(output, prefix == 'i' ? "int" : prefix == 'b' ? "bool" : "float");
        else
            snprintf(output, LINK_TYPE_CAPACITY, "%svec%u", prefix == 'i' ? "i" : prefix == 'b' ? "b" : "",
                     (unsigned)swizzle_length);
    }
}

static int combine_link_arithmetic_types(const char *left, const char *right, char operation,
                                         char output[LINK_TYPE_CAPACITY])
{
    int left_float_scalar = !strcmp(left, "float");
    int right_float_scalar = !strcmp(right, "float");
    int left_int_scalar = !strcmp(left, "int");
    int right_int_scalar = !strcmp(right, "int");

    if (!strcmp(left, right) &&
        (!strcmp(left, "float") || !strcmp(left, "int") ||
         !strncmp(left, "vec", 3) || !strncmp(left, "ivec", 4) ||
         !strncmp(left, "mat", 3))) {
        strcpy(output, left);
        return 1;
    }
    if ((left_float_scalar && (!strncmp(right, "vec", 3) || !strncmp(right, "mat", 3))) ||
        (right_float_scalar && (!strncmp(left, "vec", 3) || !strncmp(left, "mat", 3)))) {
        strcpy(output, left_float_scalar ? right : left);
        return 1;
    }
    if ((left_int_scalar && !strncmp(right, "ivec", 4)) ||
        (right_int_scalar && !strncmp(left, "ivec", 4))) {
        strcpy(output, left_int_scalar ? right : left);
        return 1;
    }
    if (operation == '*' && !strncmp(left, "mat", 3) && !strncmp(right, "vec", 3) &&
        left[3] == right[3] && !left[4] && !right[4]) {
        strcpy(output, right);
        return 1;
    }
    if (operation == '*' && !strncmp(left, "vec", 3) && !strncmp(right, "mat", 3) &&
        left[3] == right[3] && !left[4] && !right[4]) {
        strcpy(output, left);
        return 1;
    }
    return 0;
}

static int link_operator_is_exponent_sign(const char *start,
                                          const char *operator_cursor)
{
    const char *cursor;
    int have_digit = 0;

    if (operator_cursor <= start ||
        (*operator_cursor != '+' && *operator_cursor != '-') ||
        (operator_cursor[-1] != 'e' && operator_cursor[-1] != 'E'))
        return 0;
    cursor = operator_cursor - 1;
    while (cursor > start &&
           ((cursor[-1] >= '0' && cursor[-1] <= '9') || cursor[-1] == '.')) {
        if (cursor[-1] >= '0' && cursor[-1] <= '9')
            have_digit = 1;
        --cursor;
    }
    if (!have_digit)
        return 0;
    return cursor == start || !shader_identifier_character(cursor[-1]);
}

static int infer_link_argument_type(const char *source, const char *start, const char *end,
                                    const LinkFunction *functions, int function_count,
                                    char output[LINK_TYPE_CAPACITY]);

static int link_name_is(const char *name, size_t length, const char *expected)
{
    return strlen(expected) == length && !strncmp(name, expected, length);
}

static int link_builtin_accepts_arity(const char *name, size_t length, int argument_count)
{
    static const char *const unary[] = {
        "abs", "acos", "all", "any", "asin", "ceil", "cos", "degrees", "dFdx",
        "dFdy", "exp", "exp2", "floor", "fract", "fwidth", "inversesqrt", "length",
        "log", "log2", "normalize", "not", "radians", "sign", "sin", "sqrt", "tan"
    };
    static const char *const binary[] = {
        "cross", "distance", "dot", "equal", "greaterThan", "greaterThanEqual",
        "lessThan", "lessThanEqual", "matrixCompMult", "max", "min", "mod", "notEqual",
        "pow", "reflect", "step"
    };
    static const char *const ternary[] = {
        "clamp", "faceforward", "mix", "refract", "smoothstep"
    };
    int index;

    if (link_name_is(name, length, "atan"))
        return argument_count == 1 || argument_count == 2;
    if (link_name_is(name, length, "texture2D") ||
        link_name_is(name, length, "texture2DProj") ||
        link_name_is(name, length, "textureCube"))
        return argument_count == 2 || argument_count == 3;
    if (link_name_is(name, length, "texture2DLod") ||
        link_name_is(name, length, "texture2DProjLod") ||
        link_name_is(name, length, "textureCubeLod"))
        return argument_count == 3;
    for (index = 0; index < (int)(sizeof(unary) / sizeof(unary[0])); ++index)
        if (link_name_is(name, length, unary[index]))
            return argument_count == 1;
    for (index = 0; index < (int)(sizeof(binary) / sizeof(binary[0])); ++index)
        if (link_name_is(name, length, binary[index]))
            return argument_count == 2;
    for (index = 0; index < (int)(sizeof(ternary) / sizeof(ternary[0])); ++index)
        if (link_name_is(name, length, ternary[index]))
            return argument_count == 3;
    return 1;
}

static int link_type_is_float_vector(const char *type)
{
    return !strcmp(type, "vec2") || !strcmp(type, "vec3") || !strcmp(type, "vec4");
}

static int link_type_is_float_gen(const char *type)
{
    return !strcmp(type, "float") || link_type_is_float_vector(type);
}

static int link_type_is_int_vector(const char *type)
{
    return !strcmp(type, "ivec2") || !strcmp(type, "ivec3") ||
           !strcmp(type, "ivec4");
}

static int link_type_is_bool_vector(const char *type)
{
    return !strcmp(type, "bvec2") || !strcmp(type, "bvec3") || !strcmp(type, "bvec4");
}

static int link_type_is_matrix(const char *type)
{
    return !strcmp(type, "mat2") || !strcmp(type, "mat3") || !strcmp(type, "mat4");
}

static int link_constructor_component_count(const char *type)
{
    if (!strcmp(type, "float") || !strcmp(type, "int") || !strcmp(type, "bool"))
        return 1;
    if (!strcmp(type, "vec2") || !strcmp(type, "ivec2") || !strcmp(type, "bvec2"))
        return 2;
    if (!strcmp(type, "vec3") || !strcmp(type, "ivec3") || !strcmp(type, "bvec3"))
        return 3;
    if (!strcmp(type, "vec4") || !strcmp(type, "ivec4") || !strcmp(type, "bvec4"))
        return 4;
    if (!strcmp(type, "mat2"))
        return 4;
    if (!strcmp(type, "mat3"))
        return 9;
    if (!strcmp(type, "mat4"))
        return 16;
    return 0;
}

static int link_constructor_known_types_valid(const char *name, size_t length,
                                              char argument_types[][LINK_TYPE_CAPACITY],
                                              const int *argument_known,
                                              int argument_count)
{
    char constructor[LINK_TYPE_CAPACITY];
    int wanted;
    int matrix;
    int supplied = 0;
    int argument;

    if (!link_constructor_type(name, length, constructor))
        return 1;
    wanted = link_constructor_component_count(constructor);
    matrix = link_type_is_matrix(constructor);
    if (!argument_count)
        return 0;
    for (argument = 0; argument < argument_count; ++argument) {
        int count;

        if (!argument_known[argument])
            return 1;
        count = link_constructor_component_count(argument_types[argument]);
        if (!count || (link_type_is_matrix(argument_types[argument]) &&
                       argument_count != 1))
            return 0;
        supplied += count;
    }
    if (wanted == 1)
        return argument_count == 1 && supplied >= 1;
    if (!matrix && argument_count == 1 && supplied >= wanted)
        return 1;
    if (argument_count == 1 && supplied == 1)
        return 1;
    if (matrix && argument_count == 1 && link_type_is_matrix(argument_types[0]))
        return 1;
    return supplied == wanted;
}

static int link_builtin_known_types_valid(const char *name, size_t length,
                                          char argument_types[static MAX_LINK_PARAMETERS][LINK_TYPE_CAPACITY],
                                          const int *argument_known, int argument_count)
{
    int argument;

    if (!link_builtin_accepts_arity(name, length, argument_count))
        return 0;
    for (argument = 0; argument < argument_count; ++argument)
        if (!argument_known[argument])
            return 1;
    if (link_name_is(name, length, "any") || link_name_is(name, length, "all") ||
        link_name_is(name, length, "not"))
        return link_type_is_bool_vector(argument_types[0]);
    if (link_name_is(name, length, "length") ||
        link_name_is(name, length, "normalize"))
        return link_type_is_float_gen(argument_types[0]);
    if (link_name_is(name, length, "distance") || link_name_is(name, length, "dot"))
        return link_type_is_float_gen(argument_types[0]) &&
               !strcmp(argument_types[0], argument_types[1]);
    if (link_name_is(name, length, "cross"))
        return !strcmp(argument_types[0], "vec3") &&
               !strcmp(argument_types[1], "vec3");
    if (link_name_is(name, length, "matrixCompMult"))
        return link_type_is_matrix(argument_types[0]) &&
               !strcmp(argument_types[0], argument_types[1]);
    if (link_name_is(name, length, "lessThan") ||
        link_name_is(name, length, "lessThanEqual") ||
        link_name_is(name, length, "greaterThan") ||
        link_name_is(name, length, "greaterThanEqual"))
        return (link_type_is_float_vector(argument_types[0]) ||
                link_type_is_int_vector(argument_types[0])) &&
               !strcmp(argument_types[0], argument_types[1]);
    if (link_name_is(name, length, "equal") || link_name_is(name, length, "notEqual"))
        return (link_type_is_float_vector(argument_types[0]) ||
                link_type_is_int_vector(argument_types[0]) ||
                link_type_is_bool_vector(argument_types[0])) &&
               !strcmp(argument_types[0], argument_types[1]);
    if (link_name_is(name, length, "radians") ||
        link_name_is(name, length, "degrees") ||
        link_name_is(name, length, "sin") || link_name_is(name, length, "cos") ||
        link_name_is(name, length, "tan") || link_name_is(name, length, "asin") ||
        link_name_is(name, length, "acos") || link_name_is(name, length, "exp") ||
        link_name_is(name, length, "log") || link_name_is(name, length, "exp2") ||
        link_name_is(name, length, "log2") || link_name_is(name, length, "sqrt") ||
        link_name_is(name, length, "inversesqrt") ||
        link_name_is(name, length, "floor") || link_name_is(name, length, "ceil") ||
        link_name_is(name, length, "fract") || link_name_is(name, length, "dFdx") ||
        link_name_is(name, length, "dFdy") || link_name_is(name, length, "fwidth"))
        return link_type_is_float_gen(argument_types[0]);
    if (link_name_is(name, length, "abs") || link_name_is(name, length, "sign"))
        return link_type_is_float_gen(argument_types[0]);
    if (link_name_is(name, length, "atan") && argument_count == 1)
        return link_type_is_float_gen(argument_types[0]);
    if (link_name_is(name, length, "pow") ||
        (link_name_is(name, length, "atan") && argument_count == 2))
        return link_type_is_float_gen(argument_types[0]) &&
               !strcmp(argument_types[0], argument_types[1]);
    if (link_name_is(name, length, "mod"))
        return link_type_is_float_gen(argument_types[0]) &&
               (!strcmp(argument_types[0], argument_types[1]) ||
                !strcmp(argument_types[1], "float"));
    if (link_name_is(name, length, "min") || link_name_is(name, length, "max"))
        return link_type_is_float_gen(argument_types[0]) &&
               (!strcmp(argument_types[0], argument_types[1]) ||
                !strcmp(argument_types[1], "float"));
    if (link_name_is(name, length, "step"))
        return argument_count == 2 &&
               link_type_is_float_gen(argument_types[1]) &&
               (!strcmp(argument_types[0], argument_types[1]) ||
                !strcmp(argument_types[0], "float"));
    if (link_name_is(name, length, "clamp"))
        return argument_count == 3 &&
               link_type_is_float_gen(argument_types[0]) &&
               ((!strcmp(argument_types[0], argument_types[1]) &&
                 !strcmp(argument_types[0], argument_types[2])) ||
                (!strcmp(argument_types[1], "float") &&
                 !strcmp(argument_types[2], "float")));
    if (link_name_is(name, length, "mix"))
        return argument_count == 3 &&
               link_type_is_float_gen(argument_types[0]) &&
               !strcmp(argument_types[0], argument_types[1]) &&
               (!strcmp(argument_types[0], argument_types[2]) ||
                !strcmp(argument_types[2], "float"));
    if (link_name_is(name, length, "smoothstep"))
        return argument_count == 3 &&
               link_type_is_float_gen(argument_types[2]) &&
               ((!strcmp(argument_types[0], argument_types[2]) &&
                 !strcmp(argument_types[1], argument_types[2])) ||
                (!strcmp(argument_types[0], "float") &&
                 !strcmp(argument_types[1], "float")));
    if (link_name_is(name, length, "reflect"))
        return link_type_is_float_gen(argument_types[0]) &&
               !strcmp(argument_types[0], argument_types[1]);
    if (link_name_is(name, length, "faceforward"))
        return argument_count == 3 &&
               link_type_is_float_gen(argument_types[0]) &&
               !strcmp(argument_types[0], argument_types[1]) &&
               !strcmp(argument_types[0], argument_types[2]);
    if (link_name_is(name, length, "refract"))
        return link_type_is_float_gen(argument_types[0]) &&
               !strcmp(argument_types[0], argument_types[1]) &&
               !strcmp(argument_types[2], "float");
    if (link_name_is(name, length, "texture2D") ||
        link_name_is(name, length, "texture2DLod"))
        return !strcmp(argument_types[0], "sampler2D") &&
               !strcmp(argument_types[1], "vec2") &&
               (argument_count < 3 || !strcmp(argument_types[2], "float"));
    if (link_name_is(name, length, "texture2DProj") ||
        link_name_is(name, length, "texture2DProjLod"))
        return !strcmp(argument_types[0], "sampler2D") &&
               (!strcmp(argument_types[1], "vec3") ||
                !strcmp(argument_types[1], "vec4")) &&
               (argument_count < 3 || !strcmp(argument_types[2], "float"));
    if (link_name_is(name, length, "textureCube") ||
        link_name_is(name, length, "textureCubeLod"))
        return !strcmp(argument_types[0], "samplerCube") &&
               !strcmp(argument_types[1], "vec3") &&
               (argument_count < 3 || !strcmp(argument_types[2], "float"));
    return 1;
}

static int infer_link_call_argument(const char *source, const char *open, const char *close,
                                    int argument, const LinkFunction *functions,
                                    int function_count, char output[LINK_TYPE_CAPACITY])
{
    const char *start = skip_shader_space(open + 1);
    const char *cursor = start;
    int parentheses = 0;
    int brackets = 0;
    int index = 0;

    while (cursor <= close) {
        int separator = cursor == close ||
                        (*cursor == ',' && !parentheses && !brackets);

        if (separator) {
            if (index == argument)
                return infer_link_argument_type(source, start, cursor, functions,
                                                function_count, output);
            ++index;
            start = skip_shader_space(cursor + 1);
        } else if (*cursor == '(') {
            ++parentheses;
        } else if (*cursor == ')') {
            --parentheses;
        } else if (*cursor == '[') {
            ++brackets;
        } else if (*cursor == ']') {
            --brackets;
        }
        ++cursor;
    }
    return 0;
}

static int infer_link_builtin_call(const char *source, const char *name, size_t length,
                                   const char *open, const char *close,
                                   const LinkFunction *functions, int function_count,
                                   char output[LINK_TYPE_CAPACITY])
{
    char first_type[LINK_TYPE_CAPACITY];

    if (link_name_is(name, length, "texture2D") ||
        link_name_is(name, length, "texture2DProj") ||
        link_name_is(name, length, "texture2DLod") ||
        link_name_is(name, length, "texture2DProjLod") ||
        link_name_is(name, length, "textureCube") ||
        link_name_is(name, length, "textureCubeLod")) {
        strcpy(output, "vec4");
        return 1;
    }
    if (link_name_is(name, length, "length") || link_name_is(name, length, "distance") ||
        link_name_is(name, length, "dot")) {
        strcpy(output, "float");
        return 1;
    }
    if (link_name_is(name, length, "any") || link_name_is(name, length, "all")) {
        strcpy(output, "bool");
        return 1;
    }
    if (link_name_is(name, length, "cross")) {
        strcpy(output, "vec3");
        return 1;
    }
    if (link_name_is(name, length, "step"))
        return infer_link_call_argument(source, open, close, 1, functions, function_count,
                                        output);
    if (link_name_is(name, length, "smoothstep"))
        return infer_link_call_argument(source, open, close, 2, functions, function_count,
                                        output);
    if (!infer_link_call_argument(source, open, close, 0, functions, function_count,
                                  first_type))
        return 0;
    if (link_name_is(name, length, "lessThan") ||
        link_name_is(name, length, "lessThanEqual") ||
        link_name_is(name, length, "greaterThan") ||
        link_name_is(name, length, "greaterThanEqual") ||
        link_name_is(name, length, "equal") || link_name_is(name, length, "notEqual")) {
        const char *dimension = first_type[0] == 'i' || first_type[0] == 'b'
                                    ? first_type + 4
                                    : first_type + 3;

        if ((*dimension < '2' || *dimension > '4') || dimension[1])
            return 0;
        snprintf(output, LINK_TYPE_CAPACITY, "bvec%c", *dimension);
        return 1;
    }
    if (link_name_is(name, length, "radians") || link_name_is(name, length, "degrees") ||
        link_name_is(name, length, "sin") || link_name_is(name, length, "cos") ||
        link_name_is(name, length, "tan") || link_name_is(name, length, "asin") ||
        link_name_is(name, length, "acos") || link_name_is(name, length, "atan") ||
        link_name_is(name, length, "pow") || link_name_is(name, length, "exp") ||
        link_name_is(name, length, "log") || link_name_is(name, length, "exp2") ||
        link_name_is(name, length, "log2") || link_name_is(name, length, "sqrt") ||
        link_name_is(name, length, "inversesqrt") || link_name_is(name, length, "abs") ||
        link_name_is(name, length, "sign") || link_name_is(name, length, "floor") ||
        link_name_is(name, length, "ceil") || link_name_is(name, length, "fract") ||
        link_name_is(name, length, "mod") || link_name_is(name, length, "min") ||
        link_name_is(name, length, "max") || link_name_is(name, length, "clamp") ||
        link_name_is(name, length, "mix") ||
        link_name_is(name, length, "normalize") || link_name_is(name, length, "faceforward") ||
        link_name_is(name, length, "reflect") || link_name_is(name, length, "refract") ||
        link_name_is(name, length, "matrixCompMult") || link_name_is(name, length, "not") ||
        link_name_is(name, length, "dFdx") || link_name_is(name, length, "dFdy") ||
        link_name_is(name, length, "fwidth")) {
        strcpy(output, first_type);
        return 1;
    }
    return 0;
}

static int infer_link_argument_type(const char *source, const char *start, const char *end,
                                    const LinkFunction *functions, int function_count,
                                    char output[LINK_TYPE_CAPACITY])
{
    const char *name;
    const char *name_end;
    const char *open;
    const char *close;
    const char *suffix;
    size_t length;

    while (start < end && (*start == ' ' || *start == '\t' || *start == '\r' ||
                           *start == '\n'))
        ++start;
    while (end > start && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' ||
                           end[-1] == '\n'))
        --end;
    if (start >= end)
        return 0;
    if (end - start >= 2 &&
        ((start[0] == '+' && start[1] == '+') ||
         (start[0] == '-' && start[1] == '-'))) {
        char operand[LINK_TYPE_CAPACITY];

        if (infer_link_argument_type(source, start + 2, end, functions,
                                     function_count, operand) &&
            (!strcmp(operand, "float") || !strcmp(operand, "int") ||
             !strncmp(operand, "vec", 3) || !strncmp(operand, "ivec", 4) ||
             !strncmp(operand, "mat", 3))) {
            strcpy(output, operand);
            return 1;
        }
        return 0;
    }
    if (end - start >= 2 &&
        ((end[-2] == '+' && end[-1] == '+') ||
         (end[-2] == '-' && end[-1] == '-'))) {
        char operand[LINK_TYPE_CAPACITY];

        if (infer_link_argument_type(source, start, end - 2, functions,
                                     function_count, operand) &&
            (!strcmp(operand, "float") || !strcmp(operand, "int") ||
             !strncmp(operand, "vec", 3) || !strncmp(operand, "ivec", 4) ||
             !strncmp(operand, "mat", 3))) {
            strcpy(output, operand);
            return 1;
        }
        return 0;
    }
    {
        const char *cursor = start;
        const char *last_comma = NULL;
        int parentheses = 0;
        int brackets = 0;

        while (cursor < end) {
            if (*cursor == '(')
                ++parentheses;
            else if (*cursor == ')')
                --parentheses;
            else if (*cursor == '[')
                ++brackets;
            else if (*cursor == ']')
                --brackets;
            else if (*cursor == ',' && !parentheses && !brackets)
                last_comma = cursor;
            ++cursor;
        }
        if (last_comma)
            return infer_link_argument_type(source, last_comma + 1, end, functions,
                                            function_count, output);
    }
    {
        const char *cursor = start;
        int parentheses = 0;
        int brackets = 0;

        while (cursor < end) {
            if (*cursor == '(')
                ++parentheses;
            else if (*cursor == ')')
                --parentheses;
            else if (*cursor == '[')
                ++brackets;
            else if (*cursor == ']')
                --brackets;
            else if (*cursor == '=' && !parentheses && !brackets &&
                     (cursor + 1 >= end || cursor[1] != '=') &&
                     (cursor == start || (cursor[-1] != '=' && cursor[-1] != '!' &&
                                          cursor[-1] != '<' && cursor[-1] != '>'))) {
                return infer_link_argument_type(source, cursor + 1, end, functions,
                                                function_count, output);
            }
            ++cursor;
        }
    }
    if (*start == '(') {
        const char *outer_close = matching_parenthesis(start);

        if (outer_close == end - 1)
            return infer_link_argument_type(source, start + 1, outer_close, functions,
                                            function_count, output);
        if (outer_close && outer_close < end) {
            const char *suffix_start = skip_shader_space(outer_close + 1);

            if ((*suffix_start == '[' || *suffix_start == '.') &&
                infer_link_argument_type(source, start + 1, outer_close,
                                         functions, function_count, output)) {
                apply_inferred_swizzle(source, output, suffix_start, end);
                return output[0] != '\0';
            }
        }
    }
    if (*start == '!') {
        char operand[LINK_TYPE_CAPACITY];

        if (start + 1 < end && start[1] != '=' &&
            infer_link_argument_type(source, start + 1, end, functions, function_count,
                                     operand) &&
            !strcmp(operand, "bool")) {
            strcpy(output, "bool");
            return 1;
        }
    }
    if ((*start == '+' || *start == '-') && start + 1 < end &&
        start[1] != *start) {
        char operand[LINK_TYPE_CAPACITY];

        if (infer_link_argument_type(source, start + 1, end, functions,
                                     function_count, operand) &&
            (!strcmp(operand, "float") || !strcmp(operand, "int") ||
             !strncmp(operand, "vec", 3) || !strncmp(operand, "ivec", 4) ||
             !strncmp(operand, "mat", 3))) {
            strcpy(output, operand);
            return 1;
        }
        return 0;
    }
    {
        const char *conditional = start;
        int parentheses = 0;
        int brackets = 0;

        while (conditional < end) {
            if (*conditional == '(')
                ++parentheses;
            else if (*conditional == ')')
                --parentheses;
            else if (*conditional == '[')
                ++brackets;
            else if (*conditional == ']')
                --brackets;
            else if (*conditional == '?' && !parentheses && !brackets) {
                const char *colon = conditional + 1;
                int nested = 0;
                int inner_parentheses = 0;
                int inner_brackets = 0;
                char yes_type[LINK_TYPE_CAPACITY];
                char no_type[LINK_TYPE_CAPACITY];
                char condition_type[LINK_TYPE_CAPACITY];

                while (colon < end) {
                    if (*colon == '(')
                        ++inner_parentheses;
                    else if (*colon == ')')
                        --inner_parentheses;
                    else if (*colon == '[')
                        ++inner_brackets;
                    else if (*colon == ']')
                        --inner_brackets;
                    else if (!inner_parentheses && !inner_brackets && *colon == '?')
                        ++nested;
                    else if (!inner_parentheses && !inner_brackets && *colon == ':' &&
                             !nested)
                        break;
                    else if (!inner_parentheses && !inner_brackets && *colon == ':' && nested)
                        --nested;
                    ++colon;
                }
                if (colon < end &&
                    infer_link_argument_type(source, start, conditional, functions,
                                             function_count, condition_type) &&
                    !strcmp(condition_type, "bool") &&
                    infer_link_argument_type(source, conditional + 1, colon, functions,
                                             function_count, yes_type) &&
                    infer_link_argument_type(source, colon + 1, end, functions,
                                             function_count, no_type) &&
                    !strcmp(yes_type, no_type)) {
                    strcpy(output, yes_type);
                    return 1;
                }
                return 0;
            }
            ++conditional;
        }
    }
    {
        const char *comparison = start;
        int parentheses = 0;
        int brackets = 0;

        while (comparison < end) {
            int operator_length = 0;

            if (*comparison == '(')
                ++parentheses;
            else if (*comparison == ')')
                --parentheses;
            else if (*comparison == '[')
                ++brackets;
            else if (*comparison == ']')
                --brackets;
            else if (!parentheses && !brackets && comparison + 1 < end &&
                     ((!strncmp(comparison, "==", 2)) || (!strncmp(comparison, "!=", 2)) ||
                      (!strncmp(comparison, "<=", 2)) || (!strncmp(comparison, ">=", 2)) ||
                      (!strncmp(comparison, "&&", 2)) || (!strncmp(comparison, "^^", 2)) ||
                      (!strncmp(comparison, "||", 2))))
                operator_length = 2;
            else if (!parentheses && !brackets &&
                     (*comparison == '<' || *comparison == '>'))
                operator_length = 1;
            if (operator_length) {
                char left[LINK_TYPE_CAPACITY];
                char right[LINK_TYPE_CAPACITY];
                int equality = comparison[0] == '=' || comparison[0] == '!';
                int logical = comparison[0] == '&' || comparison[0] == '^' ||
                              comparison[0] == '|';

                if (infer_link_argument_type(source, start, comparison, functions,
                                             function_count, left) &&
                    infer_link_argument_type(source, comparison + operator_length, end,
                                             functions, function_count, right) &&
                    !strcmp(left, right) &&
                    ((logical && !strcmp(left, "bool")) ||
                     (!logical && !equality &&
                      (!strcmp(left, "float") || !strcmp(left, "int"))) ||
                     (equality &&
                      (!strcmp(left, "float") || !strcmp(left, "int") ||
                       !strcmp(left, "bool") || !strncmp(left, "vec", 3) ||
                       !strncmp(left, "ivec", 4) || !strncmp(left, "bvec", 4))))) {
                    strcpy(output, "bool");
                    return 1;
                }
                return 0;
            }
            ++comparison;
        }
    }
    {
        const char *operator_cursor = start;
        int parentheses = 0;
        int brackets = 0;

        while (operator_cursor < end) {
            if (*operator_cursor == '(')
                ++parentheses;
            else if (*operator_cursor == ')')
                --parentheses;
            else if (*operator_cursor == '[')
                ++brackets;
            else if (*operator_cursor == ']')
                --brackets;
            else if (!parentheses && !brackets && operator_cursor > start &&
                     (*operator_cursor == '+' || *operator_cursor == '-' ||
                      *operator_cursor == '*' || *operator_cursor == '/')) {
                char left[LINK_TYPE_CAPACITY];
                char right[LINK_TYPE_CAPACITY];

                if (link_operator_is_exponent_sign(start, operator_cursor)) {
                    ++operator_cursor;
                    continue;
                }

                if (infer_link_argument_type(source, start, operator_cursor, functions,
                                             function_count, left) &&
                    infer_link_argument_type(source, operator_cursor + 1, end, functions,
                                             function_count, right) &&
                    combine_link_arithmetic_types(left, right, *operator_cursor, output)) {
                    return 1;
                }
                return 0;
            }
            ++operator_cursor;
        }
    }
    if ((*start >= '0' && *start <= '9') || *start == '.' ||
        ((*start == '+' || *start == '-') && start + 1 < end &&
         ((start[1] >= '0' && start[1] <= '9') || start[1] == '.'))) {
        const char *cursor = start;
        int floating = 0;

        while (cursor < end) {
            if (*cursor == '.' || *cursor == 'e' || *cursor == 'E')
                floating = 1;
            ++cursor;
        }
        strcpy(output, floating ? "float" : "int");
        return 1;
    }
    if ((end - start == 4 && !strncmp(start, "true", 4)) ||
        (end - start == 5 && !strncmp(start, "false", 5))) {
        strcpy(output, "bool");
        return 1;
    }
    name = start;
    name_end = name;
    while (name_end < end && shader_identifier_character(*name_end))
        ++name_end;
    if (name == name_end)
        return 0;
    open = skip_shader_space(name_end);
    if (open >= end || *open != '(') {
        if (!infer_link_declared_type(source, start, name, (size_t)(name_end - name), output))
            return 0;
        suffix = skip_shader_space(name_end);
        apply_inferred_swizzle(source, output, suffix, end);
        return output[0] != '\0';
    }
    if (!(close = matching_parenthesis(open)) || close >= end)
        return 0;
    length = (size_t)(name_end - name);
    if (!link_constructor_type(name, length, output)) {
        int function;
        int arity = link_call_argument_count(open, close);
        int found = 0;
        int structure_constructor =
            stage_has_struct_type_at(source, name, length, name);

        if (structure_constructor) {
            if (!copy_link_token(output, LINK_TYPE_CAPACITY, name, name_end))
                return 0;
            found = 1;
        }

        for (function = 0; !structure_constructor && function < function_count;
             ++function) {
            int argument;
            int matches = 1;

            if (strlen(functions[function].name) != length ||
                strncmp(functions[function].name, name, length) ||
                functions[function].parameter_count != arity)
                continue;
            for (argument = 0; argument < arity; ++argument) {
                char argument_type[LINK_TYPE_CAPACITY];

                if (infer_link_call_argument(source, open, close, argument,
                                             functions, function_count,
                                             argument_type) &&
                    strcmp(argument_type,
                           functions[function].parameters[argument])) {
                    matches = 0;
                    break;
                }
            }
            if (!matches)
                continue;
            if (!found) {
                strcpy(output, functions[function].return_type);
                found = 1;
            } else if (strcmp(output, functions[function].return_type)) {
                return 0;
            }
        }
        if (!found &&
            !infer_link_builtin_call(source, name, length, open, close,
                                     functions, function_count, output))
            return 0;
    }
    suffix = skip_shader_space(close + 1);
    apply_inferred_swizzle(source, output, suffix, end);
    return output[0] != '\0';
}

static int collect_link_argument_types(const char *source, const char *open, const char *close,
                                       const LinkFunction *functions, int function_count,
                                       char types[MAX_LINK_PARAMETERS][LINK_TYPE_CAPACITY], int known[16])
{
    const char *cursor = skip_shader_space(open + 1);
    int argument = 0;

    if (cursor == close)
        return 0;
    while (cursor < close && argument < MAX_LINK_PARAMETERS) {
        const char *end = cursor;
        int parentheses = 0;
        int brackets = 0;

        while (end < close) {
            if (*end == '(')
                ++parentheses;
            else if (*end == ')')
                --parentheses;
            else if (*end == '[')
                ++brackets;
            else if (*end == ']')
                --brackets;
            else if (*end == ',' && !parentheses && !brackets)
                break;
            ++end;
        }
        known[argument] = infer_link_argument_type(source, cursor, end, functions,
                                                   function_count, types[argument]);
        ++argument;
        cursor = end < close ? skip_shader_space(end + 1) : end;
    }
    return argument;
}

static int repeated_swizzle_components(const char *start, const char *end);

static int link_call_argument_range(const char *open, const char *close, int wanted,
                                    const char **argument_start,
                                    const char **argument_end)
{
    const char *start = skip_shader_space(open + 1);
    const char *cursor = start;
    int parentheses = 0;
    int brackets = 0;
    int argument = 0;

    while (cursor <= close) {
        int separator = cursor == close ||
                        (*cursor == ',' && !parentheses && !brackets);

        if (separator) {
            if (argument == wanted) {
                const char *end = cursor;

                while (end > start && (end[-1] == ' ' || end[-1] == '\t' ||
                                       end[-1] == '\r' || end[-1] == '\n'))
                    --end;
                *argument_start = start;
                *argument_end = end;
                return start < end;
            }
            ++argument;
            start = skip_shader_space(cursor + 1);
        } else if (*cursor == '(') {
            ++parentheses;
        } else if (*cursor == ')') {
            --parentheses;
        } else if (*cursor == '[') {
            ++brackets;
        } else if (*cursor == ']') {
            --brackets;
        }
        ++cursor;
    }
    return 0;
}

static char *normalize_link_lvalue(const char *start, const char *end,
                                   const char **normalized_start,
                                   const char **normalized_end)
{
    char *buffer;
    size_t length;

    start = control_space(start, end);
    while (end > start && (end[-1] == ' ' || end[-1] == '\t' ||
                           end[-1] == '\r' || end[-1] == '\n'))
        --end;
    length = (size_t)(end - start);
    if (length == SIZE_MAX || !(buffer = (char *)ntglAlloc(length + 1)))
        return NULL;
    memcpy(buffer, start, length);
    buffer[length] = '\0';
    for (;;) {
        const char *buffer_end = buffer + length;
        const char *open = control_space(buffer, buffer_end);
        const char *close;
        size_t open_offset;
        size_t close_offset;

        if (open >= buffer_end || *open != '(')
            break;
        close = control_matching(open, buffer_end, '(', ')');
        if (!close) {
            ntglFree(buffer);
            return NULL;
        }
        open_offset = (size_t)(open - buffer);
        close_offset = (size_t)(close - buffer);
        memmove(buffer + close_offset, buffer + close_offset + 1,
                length - close_offset - 1);
        --length;
        memmove(buffer + open_offset, buffer + open_offset + 1,
                length - open_offset);
        --length;
    }
    *normalized_start = control_space(buffer, buffer + length);
    *normalized_end = buffer + length;
    return buffer;
}

static void strip_link_parentheses(const char **start, const char **end)
{
    for (;;) {
        const char *close;

        *start = control_space(*start, *end);
        while (*end > *start && ((*end)[-1] == ' ' || (*end)[-1] == '\t' ||
                                 (*end)[-1] == '\r' || (*end)[-1] == '\n'))
            --*end;
        if (*start >= *end || **start != '(')
            return;
        close = control_matching(*start, *end, '(', ')');
        if (!close || close != *end - 1)
            return;
        ++*start;
        *end = close;
    }
}

static int normalized_link_argument_is_writable(const char *start,
                                                const char *end)
{
    const char *cursor;

    cursor = start;

    if (cursor >= end || !((*cursor >= 'a' && *cursor <= 'z') ||
                           (*cursor >= 'A' && *cursor <= 'Z') || *cursor == '_'))
        return 0;
    while (cursor < end && shader_identifier_character(*cursor))
        ++cursor;
    for (;;) {
        cursor = control_space(cursor, end);
        if (cursor >= end)
            return 1;
        if (*cursor == '[') {
            const char *close = control_matching(cursor, end, '[', ']');

            if (!close)
                return 0;
            cursor = close + 1;
            continue;
        }
        if (*cursor == '.') {
            const char *member;
            const char *member_end;
            int swizzle = 1;

            member = control_space(cursor + 1, end);
            if (member >= end || !shader_identifier_character(*member))
                return 0;
            member_end = member;
            while (member_end < end && shader_identifier_character(*member_end))
                ++member_end;
            for (cursor = member; cursor < member_end; ++cursor)
                if (!strchr("xyzwrgba" "stpq", *cursor)) {
                    swizzle = 0;
                    break;
                }
            if (swizzle && repeated_swizzle_components(member - 1, member_end))
                return 0;
            cursor = member_end;
            continue;
        }
        return 0;
    }
}

static int link_argument_is_writable(const char *start, const char *end)
{
    const char *normalized_start;
    const char *normalized_end;
    char *normalized = normalize_link_lvalue(start, end, &normalized_start,
                                             &normalized_end);
    int writable;

    if (!normalized)
        return 0;
    writable = normalized_link_argument_is_writable(normalized_start,
                                                    normalized_end);
    ntglFree(normalized);
    return writable;
}

static int lvalue_range_has_conditional(const char *start, const char *end)
{
    int brackets = 0;

    while (start < end) {
        if (*start == '[')
            ++brackets;
        else if (*start == ']' && brackets)
            --brackets;
        else if (*start == '?' && !brackets)
            return 1;
        ++start;
    }
    return 0;
}

static int assignment_left_is_declaration(const char *source,
                                          const char *start,
                                          const char *end)
{
    const char *name_end = end;
    const char *name_start;
    const char *type_end;
    const char *type_start;
    char type[LINK_TYPE_CAPACITY];

    while (name_end > start && !shader_identifier_character(name_end[-1]))
        --name_end;
    name_start = name_end;
    while (name_start > start && shader_identifier_character(name_start[-1]))
        --name_start;
    if (name_start == name_end)
        return 0;
    type_end = name_start;
    while (type_end > start && (type_end[-1] == ' ' || type_end[-1] == '\t' ||
                                type_end[-1] == '\r' || type_end[-1] == '\n'))
        --type_end;
    type_start = type_end;
    while (type_start > start && shader_identifier_character(type_start[-1]))
        --type_start;
    return type_start < type_end &&
           copy_known_link_type(source, type_start, type_end, type);
}

static int link_range_has_token(const char *start, const char *end,
                                const char *wanted)
{
    size_t wanted_length = strlen(wanted);

    while (start < end) {
        const char *token;

        while (start < end && !shader_identifier_character(*start))
            ++start;
        token = start;
        while (start < end && shader_identifier_character(*start))
            ++start;
        if ((size_t)(start - token) == wanted_length &&
            !strncmp(token, wanted, wanted_length))
            return 1;
    }
    return 0;
}

static int link_argument_base_is_read_only(const char *source, const char *call,
                                           const char *argument_start,
                                           int fragment_stage)
{
    const char *argument_end = call;
    const char *name_end = argument_start;
    const char *match = source;
    size_t name_length;
    int read_only = 0;

    while (argument_end > argument_start &&
           (argument_end[-1] == ' ' || argument_end[-1] == '\t' ||
            argument_end[-1] == '\r' || argument_end[-1] == '\n'))
        --argument_end;
    if (argument_end > argument_start &&
        strchr("+-*/", argument_end[-1]))
        --argument_end;
    for (;;) {
        const char *close;

        argument_start = control_space(argument_start, argument_end);
        if (argument_start >= argument_end || *argument_start != '(')
            break;
        close = control_matching(argument_start, argument_end, '(', ')');
        if (!close)
            return 1;
        ++argument_start;
        argument_end = close;
    }
    name_end = argument_start;
    while (name_end < argument_end && shader_identifier_character(*name_end))
        ++name_end;
    name_length = (size_t)(name_end - argument_start);
    if (!name_length)
        return 1;
    if (name_length > 3 && !strncmp(argument_start, "gl_", 3) &&
        !link_name_is(argument_start, name_length, "gl_Position") &&
        !link_name_is(argument_start, name_length, "gl_PointSize") &&
        !link_name_is(argument_start, name_length, "gl_FragColor") &&
        !link_name_is(argument_start, name_length, "gl_FragData"))
        return 1;
    while (match < call) {
        const char *found = match;
        const char *type_end;
        const char *type_start;
        const char *declaration_start;
        char constructor[LINK_TYPE_CAPACITY];

        while (found < call &&
               ((size_t)(call - found) < name_length ||
                strncmp(found, argument_start, name_length) ||
                (found > source && shader_identifier_character(found[-1])) ||
                shader_identifier_character(found[name_length])))
            ++found;
        if (found >= call)
            break;
        match = found + name_length;
        type_end = found;
        while (type_end > source && (type_end[-1] == ' ' || type_end[-1] == '\t' ||
                                     type_end[-1] == '\r' || type_end[-1] == '\n'))
            --type_end;
        type_start = type_end;
        while (type_start > source && shader_identifier_character(type_start[-1]))
            --type_start;
        if (type_start == type_end ||
            (!link_constructor_type(type_start, (size_t)(type_end - type_start),
                                    constructor) &&
             !stage_has_struct_type_at(source, type_start,
                                       (size_t)(type_end - type_start), found)))
            continue;
        declaration_start = type_start;
        while (declaration_start > source && declaration_start[-1] != ';' &&
               declaration_start[-1] != '{' && declaration_start[-1] != '}' &&
               declaration_start[-1] != '(' && declaration_start[-1] != ',')
            --declaration_start;
        read_only = link_range_has_token(declaration_start, type_start, "const") ||
                    link_range_has_token(declaration_start, type_start, "uniform") ||
                    link_range_has_token(declaration_start, type_start, "attribute") ||
                    (fragment_stage &&
                     link_range_has_token(declaration_start, type_start, "varying"));
    }
    return read_only;
}

static int link_argument_matches_parameter(const char *argument, const char *parameter)
{
    return !strcmp(argument, parameter);
}

static int link_call_is_function_declaration(const char *source, const char *name,
                                             const char *close,
                                             const LinkFunction *functions,
                                             int function_count)
{
    const char *type_end = name;
    const char *type_start;
    const char *after = skip_shader_space(close + 1);
    int function;

    if (*after != ';' && *after != '{')
        return 0;
    while (type_end > source && (type_end[-1] == ' ' || type_end[-1] == '\t' ||
                                 type_end[-1] == '\r' || type_end[-1] == '\n'))
        --type_end;
    type_start = type_end;
    while (type_start > source && shader_identifier_character(type_start[-1]))
        --type_start;
    for (function = 0; function < function_count; ++function)
        if ((size_t)(type_end - type_start) == strlen(functions[function].return_type) &&
            !strncmp(type_start, functions[function].return_type,
                     (size_t)(type_end - type_start)))
            return 1;
    return 0;
}

static int validate_function_calls(Program *program, const char *source,
                                   const LinkFunction *functions, int function_count,
                                   int fragment_stage)
{
    const char *cursor = source;

    while (*cursor) {
        const char *name;
        const char *end;
        const char *after;
        const char *close;
        size_t length;
        int function;
        int resolved = 0;
        int user_function = 0;
        int matching_arity = 0;
        int matching_known_types = 0;
        int declared_before_call = 0;
        int invalid_output_argument = 0;
        int invalid_output_read_only = 0;
        int builtin_call = 0;
        int builtin_arity_valid = 0;
        int builtin_types_valid = 0;
        const char *invalid_argument_start = NULL;
        const char *invalid_argument_end = NULL;
        char argument_types[MAX_LINK_PARAMETERS][LINK_TYPE_CAPACITY] = {{0}};
        int argument_known[MAX_LINK_PARAMETERS] = {0};
        int argument_count;

        if (cursor[0] == '/' && cursor[1] == '/') {
            cursor += 2;
            while (*cursor && *cursor != '\n')
                ++cursor;
            continue;
        }
        if (cursor[0] == '/' && cursor[1] == '*') {
            cursor += 2;
            while (*cursor && !(cursor[0] == '*' && cursor[1] == '/'))
                ++cursor;
            if (*cursor)
                cursor += 2;
            continue;
        }
        if (!((*cursor >= 'a' && *cursor <= 'z') ||
              (*cursor >= 'A' && *cursor <= 'Z') || *cursor == '_')) {
            ++cursor;
            continue;
        }
        name = cursor;
        end = cursor + 1;
        while (shader_identifier_character(*end))
            ++end;
        after = skip_shader_space(end);
        if (*after != '(') {
            cursor = end;
            continue;
        }
        close = matching_parenthesis(after);
        if (!close) {
            strcpy(program->log, "unterminated function call");
            return 0;
        }
        length = (size_t)(end - name);
        if (link_call_is_function_declaration(source, name, close, functions,
                                              function_count)) {
            cursor = close + 1;
            continue;
        }
        {
            char shadow_type[LINK_TYPE_CAPACITY];

            if (infer_link_declared_type(source, name, name, length,
                                         shadow_type)) {
                snprintf(program->log, sizeof(program->log),
                         "called identifier is shadowed by a variable: %.*s",
                         (int)length, name);
                return 0;
            }
        }
        argument_count = collect_link_argument_types(source, after, close, functions,
                                                     function_count, argument_types,
                                                     argument_known);
        builtin_call = link_builtin_name(name, length);
        builtin_arity_valid =
            builtin_call && link_builtin_accepts_arity(name, length, argument_count);
        builtin_types_valid =
            builtin_arity_valid &&
            link_builtin_known_types_valid(name, length, argument_types,
                                           argument_known, argument_count);

        if (stage_has_struct_type_at(source, name, length, name)) {
            char structure_type[LINK_TYPE_CAPACITY];
            int member_count;

            if (!copy_link_token(structure_type, sizeof(structure_type), name, end)) {
                strcpy(program->log, "structure type name is too long");
                return 0;
            }
            if (shader_struct_contains_sampler_at(source, structure_type,
                                                  strlen(structure_type), name,
                                                  0)) {
                snprintf(program->log, sizeof(program->log),
                         "cannot construct a structure containing samplers: %.*s",
                         (int)length, name);
                return 0;
            }
            if (shader_struct_contains_array_at(source, structure_type,
                                                strlen(structure_type), name,
                                                0)) {
                snprintf(program->log, sizeof(program->log),
                         "cannot construct a structure containing arrays: %.*s",
                         (int)length, name);
                return 0;
            }
            member_count = link_struct_member_count(source, structure_type, name);
            if (member_count < 0 || member_count != argument_count) {
                snprintf(program->log, sizeof(program->log),
                         "invalid structure constructor arguments: %.*s (%d members, %d arguments)",
                         (int)length, name, member_count, argument_count);
                return 0;
            }
            for (function = 0; function < argument_count; ++function) {
                char member_type[LINK_TYPE_CAPACITY];

                if (argument_known[function] &&
                    (!link_struct_member_type_at(source, structure_type, name,
                                                function,
                                                member_type) ||
                     strcmp(member_type, argument_types[function]))) {
                    snprintf(program->log, sizeof(program->log),
                             "invalid structure constructor member type: %.*s",
                             (int)length, name);
                    return 0;
                }
            }
        }

        for (function = 0; function < function_count; ++function)
            if (strlen(functions[function].name) == length &&
                !strncmp(functions[function].name, name, length)) {
                int argument;
                int types_match = 1;

                user_function = 1;
                if (functions[function].declaration_start >= name)
                    continue;
                declared_before_call = 1;
                if (functions[function].parameter_count ==
                    link_call_argument_count(after, close)) {
                    matching_arity = 1;
                    for (argument = 0; argument < argument_count; ++argument)
                        if (argument_known[argument] &&
                            !link_argument_matches_parameter(
                                argument_types[argument],
                                functions[function].parameters[argument])) {
                            types_match = 0;
                            break;
                        }
                    if (types_match) {
                        for (argument = 0; argument < argument_count; ++argument) {
                            const char *argument_start = NULL;
                            const char *argument_end = NULL;
                            int writable;
                            int read_only;

                            if (!functions[function].parameter_modes[argument])
                                continue;
                            writable = link_call_argument_range(after, close, argument,
                                                                &argument_start,
                                                                &argument_end) &&
                                       link_argument_is_writable(argument_start,
                                                                 argument_end);
                            read_only = writable &&
                                        link_argument_base_is_read_only(source, close,
                                                                        argument_start,
                                                                        fragment_stage);
                            if (!writable || read_only) {
                                invalid_output_argument = 1;
                                invalid_output_read_only = read_only;
                                invalid_argument_start = argument_start ? argument_start : name;
                                invalid_argument_end = argument_end ? argument_end : end;
                                types_match = 0;
                                break;
                            }
                        }
                    }
                    if (types_match)
                        matching_known_types = 1;
                }
                resolved = 1;
            }
        if (user_function && !declared_before_call && !builtin_types_valid) {
            snprintf(program->log, sizeof(program->log),
                     "function called before declaration: %.*s", (int)length, name);
            return 0;
        }
        if (user_function && !matching_arity && !builtin_types_valid) {
            snprintf(program->log, sizeof(program->log),
                     "no matching function overload: %.*s", (int)length, name);
            return 0;
        }
        if (user_function && !matching_known_types && !builtin_types_valid) {
            if (invalid_output_argument)
                snprintf(program->log, sizeof(program->log),
                         invalid_output_read_only
                             ? "out/inout argument is read-only: %.*s(%.*s)"
                             : "out/inout argument must be a writable lvalue: %.*s(%.*s)",
                         (int)length, name,
                         (int)(invalid_argument_end - invalid_argument_start),
                         invalid_argument_start);
            else
                snprintf(program->log, sizeof(program->log),
                         "no matching typed overload: %.*s", (int)length, name);
            return 0;
        }
        if (!resolved)
            resolved = link_builtin_name(name, length) ||
                       stage_has_struct_type_at(source, name, length, name) ||
                       (length == 2 && !strncmp(name, "if", 2)) ||
                       (length == 3 && !strncmp(name, "for", 3)) ||
                       (length == 5 && !strncmp(name, "while", 5)) ||
                       (length == 6 && !strncmp(name, "switch", 6));
        if (builtin_call && !matching_known_types && !builtin_arity_valid) {
            snprintf(program->log, sizeof(program->log),
                     "invalid built-in argument count: %.*s", (int)length, name);
            return 0;
        }
        if (builtin_call && !matching_known_types && !builtin_types_valid) {
            snprintf(program->log, sizeof(program->log),
                     "invalid built-in argument types: %.*s", (int)length, name);
            return 0;
        }
        if (!link_constructor_known_types_valid(name, length, argument_types,
                                                argument_known, argument_count)) {
            snprintf(program->log, sizeof(program->log),
                     "invalid constructor arguments: %.*s", (int)length, name);
            return 0;
        }
        if (!resolved) {
            snprintf(program->log, sizeof(program->log), "unresolved function call: %.*s",
                     (int)length, name);
            return 0;
        }
        cursor = end;
    }
    return 1;
}

static int validate_nonrecursive_functions(Program *program, const char *source,
                                           const LinkFunction *functions,
                                           int function_count)
{
    unsigned char calls[MAX_LINK_FUNCTIONS][MAX_LINK_FUNCTIONS] = {{0}};
    int caller;
    int intermediate;

    for (caller = 0; caller < function_count; ++caller) {
        const char *cursor;

        if (!functions[caller].definition)
            continue;
        cursor = functions[caller].body_start;
        while (cursor < functions[caller].body_end) {
            const char *name;
            const char *name_end;
            const char *open;
            const char *close;
            char argument_types[MAX_LINK_PARAMETERS][LINK_TYPE_CAPACITY] = {{0}};
            int argument_known[MAX_LINK_PARAMETERS] = {0};
            int argument_count;
            int callee;

            if (!((*cursor >= 'a' && *cursor <= 'z') ||
                  (*cursor >= 'A' && *cursor <= 'Z') || *cursor == '_')) {
                ++cursor;
                continue;
            }
            name = cursor;
            name_end = cursor + 1;
            while (shader_identifier_character(*name_end))
                ++name_end;
            open = skip_shader_space(name_end);
            if (*open != '(' || !(close = matching_parenthesis(open)) ||
                close > functions[caller].body_end) {
                cursor = name_end;
                continue;
            }
            argument_count = collect_link_argument_types(
                source, open, close, functions, function_count, argument_types,
                argument_known);
            for (callee = 0; callee < function_count; ++callee) {
                int argument;
                int matches = functions[callee].definition &&
                              strlen(functions[callee].name) ==
                                  (size_t)(name_end - name) &&
                              !strncmp(functions[callee].name, name,
                                       (size_t)(name_end - name)) &&
                              functions[callee].parameter_count == argument_count;

                for (argument = 0; matches && argument < argument_count; ++argument)
                    if (argument_known[argument] &&
                        !link_argument_matches_parameter(
                            argument_types[argument],
                            functions[callee].parameters[argument]))
                        matches = 0;
                if (matches)
                    calls[caller][callee] = 1;
            }
            cursor = name_end;
        }
    }
    for (intermediate = 0; intermediate < function_count; ++intermediate) {
        int from;

        for (from = 0; from < function_count; ++from) {
            int to;

            if (!calls[from][intermediate])
                continue;
            for (to = 0; to < function_count; ++to)
                if (calls[intermediate][to])
                    calls[from][to] = 1;
        }
    }
    for (caller = 0; caller < function_count; ++caller) {
        if (calls[caller][caller]) {
            snprintf(program->log, sizeof(program->log),
                     "recursive function call is not allowed: %.64s",
                     functions[caller].name);
            return 0;
        }
    }
    return 1;
}

static int validate_function_return_types(Program *program, const char *source,
                                          const LinkFunction *functions,
                                          int function_count)
{
    int function_index;

    for (function_index = 0; function_index < function_count; ++function_index) {
        const LinkFunction *function = &functions[function_index];
        const char *cursor;
        int saw_return = 0;

        if (!function->definition)
            continue;
        cursor = function->body_start;
        while (cursor < function->body_end) {
            const char *expression;
            const char *semicolon;
            char expression_type[LINK_TYPE_CAPACITY];

            while (cursor < function->body_end &&
                   ((cursor > function->body_start &&
                     shader_identifier_character(cursor[-1])) ||
                    !control_word(cursor, function->body_end, "return",
                                  &expression)))
                ++cursor;
            if (cursor >= function->body_end)
                break;
            saw_return = 1;
            expression = control_space(expression, function->body_end);
            semicolon = expression;
            while (semicolon < function->body_end && *semicolon != ';') {
                if (*semicolon == '(') {
                    const char *close = control_matching(semicolon, function->body_end,
                                                         '(', ')');

                    if (!close)
                        break;
                    semicolon = close;
                }
                ++semicolon;
            }
            if (semicolon >= function->body_end) {
                snprintf(program->log, sizeof(program->log),
                         "unterminated return statement in function: %.64s",
                         function->name);
                return 0;
            }
            if (!strcmp(function->return_type, "void")) {
                if (expression != semicolon) {
                    snprintf(program->log, sizeof(program->log),
                             "void function returns a value: %.64s",
                             function->name);
                    return 0;
                }
            } else if (expression == semicolon) {
                snprintf(program->log, sizeof(program->log),
                         "non-void function returns no value: %.64s",
                         function->name);
                return 0;
            } else if (infer_link_argument_type(source, expression, semicolon,
                                                functions, function_count,
                                                expression_type) &&
                       strcmp(expression_type, function->return_type)) {
                snprintf(program->log, sizeof(program->log),
                         "return type mismatch in %.20s: expected %.20s, got %.20s",
                         function->name, function->return_type, expression_type);
                return 0;
            }
            cursor = semicolon + 1;
        }
        if (strcmp(function->return_type, "void") && !saw_return) {
            snprintf(program->log, sizeof(program->log),
                     "non-void function has no return statement: %.64s",
                     function->name);
            return 0;
        }
    }
    return 1;
}

static int condition_declared_name(const char *start, const char *end,
                                   const char **name, size_t *name_length)
{
    const char *token_end;

    start = control_space(start, end);
    token_end = start;
    while (token_end < end && shader_identifier_character(*token_end))
        ++token_end;
    if (token_end - start == 5 && !strncmp(start, "const", 5)) {
        start = control_space(token_end, end);
        token_end = start;
        while (token_end < end && shader_identifier_character(*token_end))
            ++token_end;
    }
    if (token_end - start != 4 || strncmp(start, "bool", 4))
        return 0;
    start = control_space(token_end, end);
    token_end = start;
    if (token_end >= end ||
        !((*token_end >= 'a' && *token_end <= 'z') ||
          (*token_end >= 'A' && *token_end <= 'Z') || *token_end == '_'))
        return 0;
    while (token_end < end && shader_identifier_character(*token_end))
        ++token_end;
    *name = start;
    *name_length = (size_t)(token_end - start);
    token_end = control_space(token_end, end);
    return token_end < end && *token_end == '=' &&
           (token_end + 1 >= end || token_end[1] != '=');
}

static int condition_declares_variable(const char *start, const char *end)
{
    const char *name;
    size_t name_length;

    return condition_declared_name(start, end, &name, &name_length);
}

static int validate_function_condition_types(Program *program, const char *source,
                                             const LinkFunction *functions,
                                             int function_count)
{
    int function_index;

    for (function_index = 0; function_index < function_count; ++function_index) {
        const LinkFunction *function = &functions[function_index];
        const char *cursor;

        if (!function->definition)
            continue;
        cursor = function->body_start;
        while (cursor < function->body_end) {
            const char *after;
            const char *open;
            const char *close;
            const char *condition_start;
            const char *condition_end;
            const char *kind = NULL;
            const char *for_initializer_end = NULL;
            char condition_type[LINK_TYPE_CAPACITY];

            if (control_word(cursor, function->body_end, "do", &after)) {
                const char *body_end;
                const char *while_after;
                char ignored_log[LOG_SIZE] = {0};

                if (validate_control_statement(after, function->body_end, 1,
                                               &body_end, ignored_log) &&
                    control_word(body_end, function->body_end, "while",
                                 &while_after)) {
                    const char *do_open = control_space(while_after,
                                                        function->body_end);
                    const char *do_close =
                        do_open < function->body_end && *do_open == '('
                            ? control_matching(do_open, function->body_end,
                                               '(', ')')
                            : NULL;
                    if (do_close && condition_declares_variable(do_open + 1,
                                                                do_close)) {
                        snprintf(program->log, sizeof(program->log),
                                 "do-while condition cannot declare a variable in function %.31s",
                                 function->name);
                        return 0;
                    }
                }
            }

            if (control_word(cursor, function->body_end, "if", &after))
                kind = "if";
            else if (control_word(cursor, function->body_end, "while", &after))
                kind = "while";
            else if (control_word(cursor, function->body_end, "for", &after))
                kind = "for";
            else {
                ++cursor;
                continue;
            }
            open = control_space(after, function->body_end);
            if (open >= function->body_end || *open != '(' ||
                !(close = control_matching(open, function->body_end, '(', ')'))) {
                ++cursor;
                continue;
            }
            condition_start = open + 1;
            condition_end = close;
            if (!strcmp(kind, "for")) {
                const char *first = condition_start;
                const char *second;

                while (first < close && *first != ';')
                    ++first;
                for_initializer_end = first;
                second = first < close ? first + 1 : close;
                while (second < close && *second != ';')
                    ++second;
                condition_start = first < close ? first + 1 : close;
                condition_end = second;
            }
            condition_start = control_space(condition_start, condition_end);
            while (condition_end > condition_start &&
                   (condition_end[-1] == ' ' || condition_end[-1] == '\t' ||
                    condition_end[-1] == '\r' || condition_end[-1] == '\n'))
                --condition_end;
            if (!strcmp(kind, "for") && for_initializer_end &&
                condition_start < condition_end) {
                const char *condition_name;
                size_t condition_name_length;

                if (condition_declared_name(condition_start, condition_end,
                                            &condition_name,
                                            &condition_name_length)) {
                    const char *match = open + 1;

                    while (match < for_initializer_end) {
                        char declared_type[LINK_TYPE_CAPACITY];

                        while (match < for_initializer_end &&
                               (strncmp(match, condition_name,
                                        condition_name_length) ||
                                (match > open + 1 &&
                                 shader_identifier_character(match[-1])) ||
                                (match + condition_name_length <
                                     for_initializer_end &&
                                 shader_identifier_character(
                                     match[condition_name_length]))))
                            ++match;
                        if (match >= for_initializer_end)
                            break;
                        if (infer_link_declarator_list_type(
                                source, match, condition_name_length,
                                declared_type)) {
                            snprintf(program->log, sizeof(program->log),
                                     "duplicate for-loop variable %.*s in %.31s",
                                     (int)condition_name_length,
                                     condition_name, function->name);
                            return 0;
                        }
                        match += condition_name_length;
                    }
                }
            }
            if (!strcmp(kind, "if") && condition_start < condition_end) {
                if (condition_declares_variable(condition_start,
                                                condition_end)) {
                    snprintf(program->log, sizeof(program->log),
                             "if condition cannot declare a variable in function %.31s",
                             function->name);
                    return 0;
                }
            }
            if (condition_start < condition_end &&
                infer_link_argument_type(source, condition_start, condition_end,
                                         functions, function_count, condition_type) &&
                strcmp(condition_type, "bool")) {
                snprintf(program->log, sizeof(program->log),
                         "%.8s condition must have bool type in function %.31s",
                         kind, function->name);
                return 0;
            }
            cursor = close + 1;
        }
    }
    return 1;
}

static const char *assignment_left_start(const char *body_start, const char *assignment)
{
    const char *cursor = assignment;
    const char *control_suffix = NULL;
    int parentheses = 0;
    int brackets = 0;

    while (cursor > body_start) {
        char character = cursor[-1];

        if (character == ')') {
            if (!parentheses && !brackets)
                control_suffix = cursor;
            ++parentheses;
        } else if (character == '(') {
            if (!parentheses && !brackets)
                break;
            if (parentheses == 1 && !brackets && control_suffix) {
                const char *word_end = cursor - 1;
                const char *word_start;

                while (word_end > body_start &&
                       (word_end[-1] == ' ' || word_end[-1] == '\t' ||
                        word_end[-1] == '\r' || word_end[-1] == '\n'))
                    --word_end;
                word_start = word_end;
                while (word_start > body_start &&
                       shader_identifier_character(word_start[-1]))
                    --word_start;
                if ((word_end - word_start == 2 &&
                     !strncmp(word_start, "if", 2)) ||
                    (word_end - word_start == 3 &&
                     !strncmp(word_start, "for", 3)) ||
                    (word_end - word_start == 5 &&
                     !strncmp(word_start, "while", 5)))
                    return control_space(control_suffix, assignment);
            }
            --parentheses;
        } else if (character == ']')
            ++brackets;
        else if (character == '[')
            --brackets;
        else if (!parentheses && !brackets &&
                 (character == ';' || character == '{' || character == '}' ||
                  character == ','))
            break;
        --cursor;
    }
    cursor = control_space(cursor, assignment);
    {
        const char *after;

        if (control_word(cursor, assignment, "else", &after) ||
            control_word(cursor, assignment, "do", &after))
            cursor = control_space(after, assignment);
    }
    return cursor;
}

static const char *conditional_condition_start(const char *body_start,
                                               const char *question)
{
    const char *cursor = question;
    int parentheses = 0;
    int brackets = 0;

    while (cursor > body_start) {
        char character = cursor[-1];

        if (character == ')')
            ++parentheses;
        else if (character == '(') {
            if (!parentheses && !brackets)
                break;
            --parentheses;
        } else if (character == ']')
            ++brackets;
        else if (character == '[') {
            if (!parentheses && !brackets)
                break;
            --brackets;
        } else if (!parentheses && !brackets && character == '=' &&
                 (cursor < question && *cursor != '=') &&
                 (cursor - 1 == body_start ||
                  (cursor[-2] != '=' && cursor[-2] != '!' && cursor[-2] != '<' &&
                   cursor[-2] != '>')))
            break;
        else if (!parentheses && !brackets &&
                 (character == ';' || character == '{' ||
                  character == '}' || character == ',' || character == '?' ||
                  character == ':'))
            break;
        --cursor;
    }
    cursor = control_space(cursor, question);
    {
        const char *after;

        if (control_word(cursor, question, "return", &after))
            cursor = control_space(after, question);
    }
    return cursor;
}

static const char *conditional_colon(const char *question, const char *body_end)
{
    const char *cursor = question + 1;
    int parentheses = 0;
    int brackets = 0;
    int nested = 0;

    while (cursor < body_end) {
        if (*cursor == '(')
            ++parentheses;
        else if (*cursor == ')')
            --parentheses;
        else if (*cursor == '[')
            ++brackets;
        else if (*cursor == ']')
            --brackets;
        else if (!parentheses && !brackets && *cursor == '?')
            ++nested;
        else if (!parentheses && !brackets && *cursor == ':' && nested)
            --nested;
        else if (!parentheses && !brackets && *cursor == ':')
            return cursor;
        ++cursor;
    }
    return NULL;
}

static const char *conditional_false_end(const char *start, const char *body_end)
{
    const char *cursor = start;
    int parentheses = 0;
    int brackets = 0;
    int nested = 0;

    while (cursor < body_end) {
        if (*cursor == '(')
            ++parentheses;
        else if (*cursor == ')') {
            if (!parentheses)
                break;
            --parentheses;
        } else if (*cursor == '[')
            ++brackets;
        else if (*cursor == ']') {
            if (!brackets)
                break;
            --brackets;
        } else if (!parentheses && !brackets && *cursor == '?')
            ++nested;
        else if (!parentheses && !brackets && *cursor == ':' && nested)
            --nested;
        else if (!parentheses && !brackets && !nested &&
                 (*cursor == ':' || *cursor == ';' || *cursor == ','))
            break;
        ++cursor;
    }
    return cursor;
}

static int shader_type_contains_sampler(const char *source, const char *type)
{
    return !strcmp(type, "sampler2D") || !strcmp(type, "samplerCube") ||
           shader_struct_contains_sampler(source, type, strlen(type), 0);
}

static int shader_type_contains_array(const char *source, const char *type)
{
    return strchr(type, '[') != NULL ||
           shader_struct_contains_array(source, type, strlen(type), 0);
}

static int conditional_branch_type_allowed(const char *source, const char *type)
{
    return !shader_type_contains_array(source, type) &&
           !shader_type_contains_sampler(source, type);
}

static int validate_function_conditional_types(Program *program, const char *source,
                                               const LinkFunction *functions,
                                               int function_count)
{
    int function_index;

    for (function_index = 0; function_index < function_count; ++function_index) {
        const LinkFunction *function = &functions[function_index];
        const char *question;

        if (!function->definition)
            continue;
        for (question = function->body_start; question < function->body_end; ++question) {
            const char *condition_start;
            const char *colon;
            const char *false_end;
            char condition_type[LINK_TYPE_CAPACITY];
            char true_type[LINK_TYPE_CAPACITY];
            char false_type[LINK_TYPE_CAPACITY];

            if (*question != '?')
                continue;
            condition_start = conditional_condition_start(function->body_start, question);
            colon = conditional_colon(question, function->body_end);
            if (!colon) {
                snprintf(program->log, sizeof(program->log),
                         "conditional expression is missing ':' in %.31s", function->name);
                return 0;
            }
            false_end = conditional_false_end(colon + 1, function->body_end);
            if (infer_link_argument_type(source, condition_start, question, functions,
                                         function_count, condition_type) &&
                strcmp(condition_type, "bool")) {
                snprintf(program->log, sizeof(program->log),
                         "conditional expression requires bool in %.31s (got %.20s near %.20s)",
                         function->name, condition_type, condition_start);
                return 0;
            }
            if (infer_link_argument_type(source, question + 1, colon, functions,
                                         function_count, true_type) &&
                infer_link_argument_type(source, colon + 1, false_end, functions,
                                         function_count, false_type)) {
                if (strcmp(true_type, false_type) ||
                    !conditional_branch_type_allowed(source, true_type)) {
                    snprintf(program->log, sizeof(program->log),
                             "conditional branch type mismatch in %.31s: %.20s and %.20s",
                             function->name, true_type, false_type);
                    return 0;
                }
            }
        }
    }
    return 1;
}

static const char *assignment_right_end(const char *start, const char *body_end)
{
    const char *cursor = start;
    int parentheses = 0;
    int brackets = 0;
    int conditional = 0;

    while (cursor < body_end) {
        if (*cursor == '(')
            ++parentheses;
        else if (*cursor == ')') {
            if (!parentheses)
                break;
            --parentheses;
        } else if (*cursor == '[')
            ++brackets;
        else if (*cursor == ']')
            --brackets;
        else if (!parentheses && !brackets && *cursor == '?')
            ++conditional;
        else if (!parentheses && !brackets && *cursor == ':' && conditional)
            --conditional;
        else if (!parentheses && !brackets && !conditional &&
                 (*cursor == ';' || *cursor == ','))
            break;
        ++cursor;
    }
    return cursor;
}

static int shader_struct_contains_sampler_at(const char *source, const char *type,
                                             size_t type_length,
                                             const char *position, int depth)
{
    const char *cursor;

    if (depth >= 16)
        return 0;
    cursor = find_link_struct_definition(source, type, type_length, position);
    if (cursor) {
        const char *name_end;
        const char *open;
        const char *close;
        const char *member;
        int braces;

        name_end = skip_shader_space(cursor + 6);
        while (shader_identifier_character(*name_end))
            ++name_end;
        open = skip_shader_space(name_end);
        close = open + 1;
        braces = 1;
        while (*close && braces) {
            if (*close == '{')
                ++braces;
            else if (*close == '}')
                --braces;
            ++close;
        }
        if (braces)
            return 0;
        if (range_has_keyword(open + 1, close - 1, "sampler2D") ||
            range_has_keyword(open + 1, close - 1, "samplerCube"))
            return 1;
        member = open + 1;
        while (member < close - 1) {
            const char *member_end;

            if (!shader_identifier_character(*member)) {
                ++member;
                continue;
            }
            member_end = member;
            while (member_end < close - 1 &&
                   shader_identifier_character(*member_end))
                ++member_end;
            if (shader_struct_contains_sampler_at(
                    source, member, (size_t)(member_end - member), member,
                    depth + 1))
                return 1;
            member = member_end;
        }
        return 0;
    }
    return 0;
}

static int shader_struct_contains_sampler(const char *source, const char *type,
                                          size_t type_length, int depth)
{
    return shader_struct_contains_sampler_at(source, type, type_length,
                                             source + strlen(source), depth);
}

static int shader_struct_contains_array_at(const char *source, const char *type,
                                           size_t type_length,
                                           const char *position, int depth)
{
    const char *cursor;

    if (depth >= 16)
        return 0;
    cursor = find_link_struct_definition(source, type, type_length, position);
    if (cursor) {
        const char *name_end;
        const char *open;
        const char *close;
        const char *member;

        name_end = skip_shader_space(cursor + 6);
        while (shader_identifier_character(*name_end))
            ++name_end;
        open = skip_shader_space(name_end);
        close = control_matching(open, source + strlen(source), '{', '}');
        if (!close)
            return 0;
        if (memchr(open + 1, '[', (size_t)(close - open - 1)))
            return 1;
        member = open + 1;
        while (member < close) {
            const char *member_end;

            if (!shader_identifier_character(*member)) {
                ++member;
                continue;
            }
            member_end = member;
            while (member_end < close && shader_identifier_character(*member_end))
                ++member_end;
            if (shader_struct_contains_array_at(
                    source, member, (size_t)(member_end - member), member,
                    depth + 1))
                return 1;
            member = member_end;
        }
        return 0;
    }
    return 0;
}

static int shader_struct_contains_array(const char *source, const char *type,
                                        size_t type_length, int depth)
{
    return shader_struct_contains_array_at(source, type, type_length,
                                           source + strlen(source), depth);
}

static int equality_operand_type(const char *source, const char *type)
{
    return !shader_type_contains_array(source, type) &&
           !shader_type_contains_sampler(source, type);
}

static const char *comparison_right_end(const char *start, const char *body_end)
{
    const char *cursor = start;
    int parentheses = 0;
    int brackets = 0;

    while (cursor < body_end) {
        if (*cursor == '(')
            ++parentheses;
        else if (*cursor == ')') {
            if (!parentheses)
                break;
            --parentheses;
        } else if (*cursor == '[')
            ++brackets;
        else if (*cursor == ']') {
            if (!brackets)
                break;
            --brackets;
        } else if (!parentheses && !brackets &&
                 (*cursor == '?' || *cursor == ':' || *cursor == ';' ||
                  *cursor == ','))
            break;
        ++cursor;
    }
    return cursor;
}

static const char *unary_operand_end(const char *start, const char *body_end)
{
    const char *cursor = control_space(start, body_end);

    if (cursor < body_end && *cursor == '(') {
        const char *close = control_matching(cursor, body_end, '(', ')');

        return close ? close + 1 : body_end;
    }
    while (cursor < body_end) {
        if (shader_identifier_character(*cursor) || *cursor == '.') {
            ++cursor;
            continue;
        }
        if (*cursor == '[' || *cursor == '(') {
            char opening = *cursor;
            char closing = opening == '[' ? ']' : ')';
            const char *close = control_matching(cursor, body_end, opening, closing);

            if (!close)
                return body_end;
            cursor = close + 1;
            continue;
        }
        break;
    }
    return cursor;
}

static int increment_operand_type(const char *type)
{
    return !strcmp(type, "float") || !strcmp(type, "int") ||
           !strncmp(type, "vec", 3) || !strncmp(type, "ivec", 4) ||
           !strncmp(type, "mat", 3);
}

static int infer_assignment_left_type(const char *source, const char *start,
                                      const char *assignment,
                                      const LinkFunction *functions,
                                      int function_count, char output[LINK_TYPE_CAPACITY]);

static int repeated_swizzle_components(const char *start, const char *end)
{
    const char *dot = end;
    unsigned int seen = 0;

    while (dot > start && dot[-1] != '.')
        --dot;
    if (dot == start)
        return 0;
    while (dot < end && shader_identifier_character(*dot)) {
        int component = *dot == 'x' || *dot == 'r' || *dot == 's' ? 0
                        : *dot == 'y' || *dot == 'g' || *dot == 't' ? 1
                        : *dot == 'z' || *dot == 'b' || *dot == 'p' ? 2
                        : *dot == 'w' || *dot == 'a' || *dot == 'q' ? 3
                                                                    : -1;

        if (component < 0)
            return 0;
        if (seen & (1u << component))
            return 1;
        seen |= 1u << component;
        ++dot;
    }
    return 0;
}

static int operand_is_function_call(const char *start, const char *end)
{
    const char *cursor = start;
    const char *name_start = cursor;

    while (cursor < end && shader_identifier_character(*cursor))
        ++cursor;
    cursor = control_space(cursor, end);
    if (cursor > name_start && cursor < end && *cursor == '(')
        return 1;
    if (end > start && end[-1] == ')') {
        for (cursor = start; cursor < end; ++cursor) {
            const char *close;
            const char *name_end;

            if (*cursor != '(' ||
                (close = control_matching(cursor, end, '(', ')')) != end - 1)
                continue;
            name_end = cursor;
            while (name_end > start &&
                   (name_end[-1] == ' ' || name_end[-1] == '\t' ||
                    name_end[-1] == '\r' || name_end[-1] == '\n'))
                --name_end;
            if (name_end > start && shader_identifier_character(name_end[-1]))
                return 1;
        }
    }
    return 0;
}

static int validate_function_unary_types(Program *program, const char *source,
                                         const LinkFunction *functions,
                                         int function_count)
{
    int function_index;

    for (function_index = 0; function_index < function_count; ++function_index) {
        const LinkFunction *function = &functions[function_index];
        const char *cursor;

        if (!function->definition)
            continue;
        for (cursor = function->body_start; cursor < function->body_end; ++cursor) {
            if (*cursor == '!' && cursor[1] != '=') {
                const char *operand = control_space(cursor + 1, function->body_end);
                const char *operand_end = unary_operand_end(operand, function->body_end);
                char type[LINK_TYPE_CAPACITY];

                if (infer_link_argument_type(source, operand, operand_end, functions,
                                             function_count, type) &&
                    strcmp(type, "bool")) {
                    snprintf(program->log, sizeof(program->log),
                             "logical-not operand must be bool in %.31s", function->name);
                    return 0;
                }
            } else if ((*cursor == '+' || *cursor == '-') && cursor[1] == *cursor) {
                const char *operand;
                const char *operand_end;
                const char *previous = cursor;
                char type[LINK_TYPE_CAPACITY];
                int prefix;

                while (previous > function->body_start &&
                       (previous[-1] == ' ' || previous[-1] == '\t' ||
                        previous[-1] == '\r' || previous[-1] == '\n'))
                    --previous;
                prefix = previous == function->body_start ||
                         (!shader_identifier_character(previous[-1]) &&
                          previous[-1] != ']' && previous[-1] != ')');

                if (prefix) {
                    operand = control_space(cursor + 2, function->body_end);
                    operand_end = unary_operand_end(operand, function->body_end);
                    if (operand_is_function_call(operand, operand_end) ||
                        lvalue_range_has_conditional(operand, operand_end)) {
                        snprintf(program->log, sizeof(program->log),
                                 "increment requires a writable lvalue in %.31s",
                                 function->name);
                        return 0;
                    }
                    if (!infer_link_argument_type(source, operand, operand_end, functions,
                                                  function_count, type))
                        continue;
                } else {
                    operand_end = cursor;
                    operand = assignment_left_start(function->body_start, cursor);
                    if (operand_is_function_call(operand, operand_end) ||
                        lvalue_range_has_conditional(operand, operand_end)) {
                        snprintf(program->log, sizeof(program->log),
                                 "increment requires a writable lvalue in %.31s",
                                 function->name);
                        return 0;
                    }
                    if (!infer_assignment_left_type(source, operand, cursor, functions,
                                                    function_count, type))
                        continue;
                }
                if (operand_is_function_call(operand, operand_end) ||
                    lvalue_range_has_conditional(operand, operand_end) ||
                    !increment_operand_type(type) ||
                    repeated_swizzle_components(operand, operand_end)) {
                    snprintf(program->log, sizeof(program->log),
                             "invalid increment operand in %.31s", function->name);
                    return 0;
                }
                ++cursor;
            } else if (*cursor == '+' || *cursor == '-') {
                const char *previous = cursor;
                const char *operand;
                const char *operand_end;
                char type[LINK_TYPE_CAPACITY];
                int unary;

                while (previous > function->body_start &&
                       (previous[-1] == ' ' || previous[-1] == '\t' ||
                        previous[-1] == '\r' || previous[-1] == '\n'))
                    --previous;
                unary = previous == function->body_start ||
                        strchr("(=,;{?:+-*/!<>", previous[-1]) != NULL;
                if (!unary)
                    continue;
                operand = control_space(cursor + 1, function->body_end);
                operand_end = unary_operand_end(operand, function->body_end);
                if (infer_link_argument_type(source, operand, operand_end, functions,
                                             function_count, type) &&
                    !increment_operand_type(type)) {
                    snprintf(program->log, sizeof(program->log),
                             "unary numeric operand has invalid type in %.31s",
                             function->name);
                    return 0;
                }
            }
        }
    }
    return 1;
}

static const char *subscript_base_start(const char *body_start, const char *open)
{
    const char *cursor = open;
    int parentheses = 0;
    int brackets = 0;

    while (cursor > body_start) {
        char character = cursor[-1];

        if (character == ')')
            ++parentheses;
        else if (character == '(') {
            if (!parentheses)
                break;
            --parentheses;
        } else if (character == ']') {
            ++brackets;
        } else if (character == '[') {
            if (!brackets)
                break;
            --brackets;
        } else if (!parentheses && !brackets &&
                   strchr("=;{},+-*/!<>?:", character)) {
            break;
        } else if (!parentheses && !brackets &&
                   (character == ' ' || character == '\t' || character == '\r' ||
                    character == '\n')) {
            const char *before = cursor - 1;

            while (before > body_start &&
                   (before[-1] == ' ' || before[-1] == '\t' || before[-1] == '\r' ||
                    before[-1] == '\n'))
                --before;
            if (before > body_start && shader_identifier_character(before[-1]) &&
                cursor < open && shader_identifier_character(*cursor))
                break;
        }
        --cursor;
    }
    return control_space(cursor, open);
}

static int link_subscript_bound(const char *type)
{
    const char *array = strchr(type, '[');
    char *end;
    long size;

    if (array) {
        size = strtol(array + 1, &end, 10);
        return end > array + 1 && *end == ']' && size > 0 && size <= INT_MAX
                   ? (int)size
                   : 0;
    }
    if (!strncmp(type, "vec", 3) && type[3] >= '2' && type[3] <= '4' && !type[4])
        return type[3] - '0';
    if ((!strncmp(type, "ivec", 4) || !strncmp(type, "bvec", 4)) &&
        type[4] >= '2' && type[4] <= '4' && !type[5])
        return type[4] - '0';
    if (!strncmp(type, "mat", 3) && type[3] >= '2' && type[3] <= '4' && !type[4])
        return type[3] - '0';
    return -1;
}

static int subscript_is_array_declaration(const char *source,
                                          const char *base_start)
{
    const char *type_end = base_start;
    const char *type_start;
    char constructor[LINK_TYPE_CAPACITY];
    char declarator_type[LINK_TYPE_CAPACITY];
    const char *name_end = base_start;

    while (shader_identifier_character(*name_end))
        ++name_end;
    if (name_end > base_start &&
        infer_link_declarator_list_type(source, base_start,
                                        (size_t)(name_end - base_start),
                                        declarator_type))
        return 1;

    while (type_end > source && (type_end[-1] == ' ' || type_end[-1] == '\t' ||
                                 type_end[-1] == '\r' || type_end[-1] == '\n'))
        --type_end;
    type_start = type_end;
    while (type_start > source && shader_identifier_character(type_start[-1]))
        --type_start;
    if (type_start == type_end)
        return 0;
    return link_constructor_type(type_start, (size_t)(type_end - type_start),
                                 constructor) ||
           stage_has_struct_type_at(source, type_start,
                                    (size_t)(type_end - type_start),
                                    base_start);
}

static int link_integer_constant(const char *source, const char *start,
                                 const char *end, GLint *value)
{
#if MESAGL_GLES2_PROFILE == MESAGL_GLES2_PROFILE_FULL
    return shader_integer_constant(source, start, end, value);
#else
    (void)source;
    (void)start;
    (void)end;
    (void)value;
    return 0;
#endif
}

static int validate_function_subscript_types(Program *program, const char *source,
                                             const LinkFunction *functions,
                                             int function_count)
{
    int function_index;

    for (function_index = 0; function_index < function_count; ++function_index) {
        const LinkFunction *function = &functions[function_index];
        const char *open;

        if (!function->definition)
            continue;
        for (open = function->body_start; open < function->body_end; ++open) {
            const char *close;
            const char *expression;
            const char *base_start;
            char type[LINK_TYPE_CAPACITY];
            char base_type[LINK_TYPE_CAPACITY];
            GLint constant_index;
            int bound;

            if (*open != '[')
                continue;
            close = control_matching(open, function->body_end, '[', ']');
            if (!close) {
                snprintf(program->log, sizeof(program->log),
                         "unterminated subscript in %.31s", function->name);
                return 0;
            }
            expression = control_space(open + 1, close);
            if (expression < close &&
                infer_link_argument_type(source, expression, close, functions,
                                         function_count, type) &&
                strcmp(type, "int")) {
                snprintf(program->log, sizeof(program->log),
                         "subscript must have scalar int type in %.31s", function->name);
                return 0;
            }
            base_start = subscript_base_start(function->body_start, open);
            if (base_start >= open)
                continue;
            if (subscript_is_array_declaration(source, base_start))
                continue;
            if (!infer_link_argument_type(source, base_start, open, functions,
                                          function_count, base_type)) {
                const char *name_end = open;
                const char *name_start;

                while (name_end > base_start &&
                       (name_end[-1] == ' ' || name_end[-1] == '\t' ||
                        name_end[-1] == '\r' || name_end[-1] == '\n'))
                    --name_end;
                name_start = name_end;
                while (name_start > base_start &&
                       shader_identifier_character(name_start[-1]))
                    --name_start;
                if (name_start == name_end || name_start != base_start ||
                    !infer_link_declared_type(source, open, name_start,
                                             (size_t)(name_end - name_start),
                                             base_type))
                    continue;
            }
            bound = link_subscript_bound(base_type);
            if (bound < 0) {
                snprintf(program->log, sizeof(program->log),
                         "type %.24s is not subscriptable in %.31s", base_type,
                         function->name);
                return 0;
            }
            if (bound && link_integer_constant(source, expression, close,
                                               &constant_index) &&
                (constant_index < 0 || constant_index >= bound)) {
                snprintf(program->log, sizeof(program->log),
                         "constant subscript %d exceeds %.24s in %.31s",
                         constant_index, base_type, function->name);
                return 0;
            }
        }
    }
    return 1;
}

static int validate_function_swizzles(Program *program, const char *source,
                                      const LinkFunction *functions,
                                      int function_count)
{
    static const char *const sets[] = {"xyzw", "rgba", "stpq"};
    int function_index;

    for (function_index = 0; function_index < function_count; ++function_index) {
        const LinkFunction *function = &functions[function_index];
        const char *dot;

        if (!function->definition)
            continue;
        for (dot = function->body_start; dot < function->body_end; ++dot) {
            const char *base_start;
            const char *name;
            const char *name_end;
            const char *after;
            const char *set = NULL;
            char base_type[LINK_TYPE_CAPACITY];
            size_t length;
            int dimension;
            size_t component;
            unsigned int written = 0;

            if (*dot != '.' || dot + 1 >= function->body_end ||
                !(dot[1] == '_' || (dot[1] >= 'a' && dot[1] <= 'z') ||
                  (dot[1] >= 'A' && dot[1] <= 'Z')))
                continue;
            base_start = conditional_condition_start(function->body_start, dot);
            if (!infer_link_argument_type(source, base_start, dot, functions,
                                          function_count, base_type)) {
                const char *base_name_end = dot;
                const char *base_name_start;

                while (base_name_end > function->body_start &&
                       (base_name_end[-1] == ' ' || base_name_end[-1] == '\t' ||
                        base_name_end[-1] == '\r' || base_name_end[-1] == '\n'))
                    --base_name_end;
                base_name_start = base_name_end;
                while (base_name_start > function->body_start &&
                       shader_identifier_character(base_name_start[-1]))
                    --base_name_start;
                if (base_name_start == base_name_end ||
                    !infer_link_declared_type(source, dot, base_name_start,
                                             (size_t)(base_name_end - base_name_start),
                                             base_type))
                    continue;
            }
            name = dot + 1;
            name_end = name;
            while (name_end < function->body_end &&
                   shader_identifier_character(*name_end))
                ++name_end;
            length = (size_t)(name_end - name);
            dimension = !strncmp(base_type, "vec", 3) && base_type[3] >= '2' &&
                                base_type[3] <= '4' && !base_type[4]
                            ? base_type[3] - '0'
                        : !strncmp(base_type, "ivec", 4) && base_type[4] >= '2' &&
                                  base_type[4] <= '4' && !base_type[5]
                            ? base_type[4] - '0'
                        : !strncmp(base_type, "bvec", 4) && base_type[4] >= '2' &&
                                  base_type[4] <= '4' && !base_type[5]
                            ? base_type[4] - '0'
                            : 0;
            if (!dimension) {
                if (stage_has_struct_type_at(source, base_type,
                                             strlen(base_type), dot)) {
                    char member_type[LINK_TYPE_CAPACITY];

                    if (!length ||
                        !infer_link_struct_member_type(source, base_type, dot,
                                                       name, length,
                                                       member_type)) {
                        snprintf(program->log, sizeof(program->log),
                                 "unknown structure member in %.31s",
                                 function->name);
                        return 0;
                    }
                } else {
                    snprintf(program->log, sizeof(program->log),
                             "member selection requires a vector or structure in %.31s",
                             function->name);
                    return 0;
                }
                continue;
            }
            if (!length || length > 4) {
                snprintf(program->log, sizeof(program->log),
                         "invalid vector swizzle in %.31s", function->name);
                return 0;
            }
            for (component = 0; component < length; ++component) {
                const char *selected = strchr(sets[0], name[component]) ? sets[0]
                                       : strchr(sets[1], name[component]) ? sets[1]
                                       : strchr(sets[2], name[component]) ? sets[2]
                                                                          : NULL;
                int channel;

                if (!selected || (set && set != selected)) {
                    snprintf(program->log, sizeof(program->log),
                             "mixed or invalid vector swizzle in %.31s", function->name);
                    return 0;
                }
                set = selected;
                channel = (int)(strchr(selected, name[component]) - selected);
                if (channel >= dimension) {
                    snprintf(program->log, sizeof(program->log),
                             "vector swizzle exceeds its dimension in %.31s",
                             function->name);
                    return 0;
                }
                if (written & (1u << channel))
                    written |= 1u << 8;
                written |= 1u << channel;
            }
            after = control_space(name_end, function->body_end);
            if ((written & (1u << 8)) &&
                ((*after == '=' && after[1] != '=') ||
                 ((*after == '+' || *after == '-') && after[1] == *after))) {
                snprintf(program->log, sizeof(program->log),
                         "writable swizzle has repeated components in %.31s",
                         function->name);
                return 0;
            }
        }
    }
    return 1;
}

static int validate_function_comparison_types(Program *program, const char *source,
                                              const LinkFunction *functions,
                                              int function_count)
{
    int function_index;

    for (function_index = 0; function_index < function_count; ++function_index) {
        const LinkFunction *function = &functions[function_index];
        const char *operator_cursor;

        if (!function->definition)
            continue;
        for (operator_cursor = function->body_start;
             operator_cursor < function->body_end; ++operator_cursor) {
            const char *left_start;
            const char *right_start;
            const char *right_end;
            int operator_length = 0;
            int equality = 0;
            int logical = 0;
            char left_type[LINK_TYPE_CAPACITY];
            char right_type[LINK_TYPE_CAPACITY];

            if (operator_cursor + 1 < function->body_end &&
                (!strncmp(operator_cursor, "==", 2) ||
                 !strncmp(operator_cursor, "!=", 2))) {
                operator_length = 2;
                equality = 1;
            } else if (operator_cursor + 1 < function->body_end &&
                       (!strncmp(operator_cursor, "<=", 2) ||
                        !strncmp(operator_cursor, ">=", 2))) {
                operator_length = 2;
            } else if (operator_cursor + 1 < function->body_end &&
                       (!strncmp(operator_cursor, "&&", 2) ||
                        !strncmp(operator_cursor, "||", 2) ||
                        !strncmp(operator_cursor, "^^", 2))) {
                operator_length = 2;
                logical = 1;
            } else if ((*operator_cursor == '<' || *operator_cursor == '>') &&
                       (operator_cursor == function->body_start ||
                        operator_cursor[-1] != *operator_cursor)) {
                operator_length = 1;
            }
            if (!operator_length)
                continue;
            left_start = conditional_condition_start(function->body_start,
                                                      operator_cursor);
            right_start = control_space(operator_cursor + operator_length,
                                        function->body_end);
            right_end = comparison_right_end(right_start, function->body_end);
            if (!infer_link_argument_type(source, left_start, operator_cursor, functions,
                                          function_count, left_type) ||
                !infer_link_argument_type(source, right_start, right_end, functions,
                                          function_count, right_type)) {
                operator_cursor += operator_length - 1;
                continue;
            }
            if ((logical && !strcmp(left_type, "bool") &&
                 !strcmp(right_type, "bool")) ||
                (!logical && equality && !strcmp(left_type, right_type) &&
                 equality_operand_type(source, left_type)) ||
                (!logical && !equality && !strcmp(left_type, right_type) &&
                 (!strcmp(left_type, "float") || !strcmp(left_type, "int")))) {
                operator_cursor += operator_length - 1;
                continue;
            }
            snprintf(program->log, sizeof(program->log),
                     "comparison/logical mismatch in %.20s: %.20s and %.20s",
                     function->name, left_type, right_type);
            return 0;
        }
    }
    return 1;
}

static int validate_function_arithmetic_types(Program *program, const char *source,
                                              const LinkFunction *functions,
                                              int function_count)
{
    int function_index;

    for (function_index = 0; function_index < function_count; ++function_index) {
        const LinkFunction *function = &functions[function_index];
        const char *operator_cursor;

        if (!function->definition)
            continue;
        for (operator_cursor = function->body_start;
             operator_cursor < function->body_end; ++operator_cursor) {
            const char *previous;
            const char *left_start;
            const char *right_start;
            const char *right_end;
            char left_type[LINK_TYPE_CAPACITY];
            char right_type[LINK_TYPE_CAPACITY];
            char result_type[LINK_TYPE_CAPACITY];

            if ((*operator_cursor != '+' && *operator_cursor != '-' &&
                 *operator_cursor != '*' && *operator_cursor != '/') ||
                operator_cursor[1] == *operator_cursor || operator_cursor[1] == '=')
                continue;
            if (link_operator_is_exponent_sign(function->body_start,
                                               operator_cursor))
                continue;
            previous = operator_cursor;
            while (previous > function->body_start &&
                   (previous[-1] == ' ' || previous[-1] == '\t' ||
                    previous[-1] == '\r' || previous[-1] == '\n'))
                --previous;
            if (previous == function->body_start ||
                strchr("(=,;{?:+-*/!<>", previous[-1]))
                continue;
            left_start = conditional_condition_start(function->body_start,
                                                      operator_cursor);
            right_start = control_space(operator_cursor + 1, function->body_end);
            right_end = comparison_right_end(right_start, function->body_end);
            if (!infer_link_argument_type(source, left_start, operator_cursor, functions,
                                          function_count, left_type) ||
                !infer_link_argument_type(source, right_start, right_end, functions,
                                          function_count, right_type))
                continue;
            if (combine_link_arithmetic_types(left_type, right_type, *operator_cursor,
                                              result_type))
                continue;
            snprintf(program->log, sizeof(program->log),
                     "arithmetic operand type mismatch in %.20s: %.20s and %.20s",
                     function->name, left_type, right_type);
            return 0;
        }
    }
    return 1;
}

static int infer_assignment_left_type(const char *source, const char *start,
                                      const char *assignment,
                                      const LinkFunction *functions,
                                      int function_count, char output[LINK_TYPE_CAPACITY])
{
    const char *end = assignment;
    const char *name_end;
    const char *name_start;

    while (end > start && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' ||
                           end[-1] == '\n' || end[-1] == '+' || end[-1] == '-' ||
                           end[-1] == '*' || end[-1] == '/'))
        --end;
    strip_link_parentheses(&start, &end);
    if (infer_link_argument_type(source, start, end, functions, function_count, output))
        return 1;
    {
        const char *cursor;

        for (cursor = start; cursor < end; ++cursor)
            if (*cursor == '.' || *cursor == '[' || *cursor == ']')
                return 0;
    }

    name_end = end;
    while (name_end > start && !shader_identifier_character(name_end[-1]))
        --name_end;
    name_start = name_end;
    while (name_start > start && shader_identifier_character(name_start[-1]))
        --name_start;
    if (name_start < name_end) {
        const char *type_end = name_start;
        const char *type_start;

        if (infer_link_declarator_list_type(
                source, name_start, (size_t)(name_end - name_start), output))
            return 1;

        while (type_end > source &&
               (type_end[-1] == ' ' || type_end[-1] == '\t' ||
                type_end[-1] == '\r' || type_end[-1] == '\n'))
            --type_end;
        type_start = type_end;
        while (type_start > source &&
               shader_identifier_character(type_start[-1]))
            --type_start;
        if (type_start < type_end &&
            copy_known_link_type(source, type_start, type_end, output))
            return 1;
    }
    return name_start < name_end &&
           infer_link_declared_type(source, assignment + 1, name_start,
                                    (size_t)(name_end - name_start), output);
}

static int validate_function_assignment_types(Program *program, const char *source,
                                              const LinkFunction *functions,
                                              int function_count,
                                              int fragment_stage)
{
    int function_index;

    for (function_index = 0; function_index < function_count; ++function_index) {
        const LinkFunction *function = &functions[function_index];
        const char *cursor;

        if (!function->definition)
            continue;
        for (cursor = function->body_start; cursor < function->body_end; ++cursor) {
            const char *left_start;
            const char *left_end;
            const char *right_start;
            const char *right_end;
            char left_type[LINK_TYPE_CAPACITY];
            char right_type[LINK_TYPE_CAPACITY];
            char operation = '=';

            if (*cursor != '=' || (cursor + 1 < function->body_end && cursor[1] == '=') ||
                (cursor > function->body_start &&
                 (cursor[-1] == '=' || cursor[-1] == '!' || cursor[-1] == '<' ||
                  cursor[-1] == '>')))
                continue;
            if (cursor > function->body_start &&
                (cursor[-1] == '+' || cursor[-1] == '-' || cursor[-1] == '*' ||
                 cursor[-1] == '/'))
                operation = cursor[-1];
            left_start = assignment_left_start(function->body_start, cursor);
            left_end = operation == '=' ? cursor : cursor - 1;
            right_start = control_space(cursor + 1, function->body_end);
            right_end = assignment_right_end(right_start, function->body_end);
            while (right_end > right_start &&
                   (right_end[-1] == ' ' || right_end[-1] == '\t' ||
                    right_end[-1] == '\r' || right_end[-1] == '\n'))
                --right_end;
            if (!link_argument_is_writable(left_start, left_end) &&
                !assignment_left_is_declaration(source, left_start, left_end)) {
                snprintf(program->log, sizeof(program->log),
                         "assignment requires a writable lvalue in %.24s near %.*s",
                         function->name, (int)(left_end - left_start), left_start);
                return 0;
            }
            if (right_start >= right_end ||
                !infer_assignment_left_type(source, left_start, cursor, functions,
                                            function_count, left_type) ||
                !infer_link_argument_type(source, right_start, right_end, functions,
                                          function_count, right_type))
                continue;
            if (link_argument_base_is_read_only(source, cursor, left_start,
                                                fragment_stage)) {
                snprintf(program->log, sizeof(program->log),
                         "assignment target is read-only in %.20s", function->name);
                return 0;
            }
            if (operation == '=') {
                if (shader_type_contains_array(source, left_type) ||
                    shader_type_contains_array(source, right_type)) {
                    snprintf(program->log, sizeof(program->log),
                             "array assignment is not supported in %.20s",
                             function->name);
                    return 0;
                }
                if (shader_type_contains_sampler(source, left_type) ||
                    shader_type_contains_sampler(source, right_type)) {
                    snprintf(program->log, sizeof(program->log),
                             "sampler assignment is not allowed in %.20s",
                             function->name);
                    return 0;
                }
                if (!strcmp(left_type, right_type))
                    continue;
            } else {
                char result_type[LINK_TYPE_CAPACITY];

                if (combine_link_arithmetic_types(left_type, right_type, operation,
                                                  result_type) &&
                    !strcmp(left_type, result_type))
                    continue;
            }
            snprintf(program->log, sizeof(program->log),
                     "assignment type mismatch in %.20s: %.20s and %.20s",
                     function->name, left_type, right_type);
            return 0;
        }
    }
    return 1;
}

typedef struct LinkLocalName {
    char name[MESAGL_MAX_SHADER_IDENTIFIER_LENGTH];
    int depth;
    int is_const;
    const char *scope_end;
} LinkLocalName;

static int add_link_local_name(Program *program, LinkLocalName *names,
                               int *name_count, const char *start,
                               const char *end, int depth, int is_const,
                               const LinkFunction *function)
{
    int existing;

    for (existing = 0; existing < *name_count; ++existing)
        if (names[existing].depth == depth &&
            strlen(names[existing].name) == (size_t)(end - start) &&
            !strncmp(names[existing].name, start, (size_t)(end - start))) {
            snprintf(program->log, sizeof(program->log),
                     "duplicate local or parameter name %.*s in %.31s",
                     (int)(end - start), start, function->name);
            return 0;
        }
    if (*name_count >= MESAGL_MAX_SHADER_LOCALS ||
        !copy_link_token(names[*name_count].name,
                         sizeof(names[*name_count].name), start, end)) {
        snprintf(program->log, sizeof(program->log),
                 "local declaration limit exceeded in %.31s", function->name);
        return 0;
    }
    names[*name_count].depth = depth;
    names[*name_count].is_const = is_const;
    names[*name_count].scope_end = NULL;
    ++*name_count;
    return 1;
}

static int link_builtin_constant_name(const char *name, size_t length)
{
    static const char *const names[] = {
        "gl_MaxVertexAttribs", "gl_MaxVertexUniformVectors",
        "gl_MaxVaryingVectors", "gl_MaxVertexTextureImageUnits",
        "gl_MaxCombinedTextureImageUnits", "gl_MaxTextureImageUnits",
        "gl_MaxFragmentUniformVectors", "gl_MaxDrawBuffers",
    };
    size_t index;

    for (index = 0; index < sizeof(names) / sizeof(names[0]); ++index)
        if (strlen(names[index]) == length && !strncmp(names[index], name, length))
            return 1;
    return 0;
}

static int global_name_is_const(const char *source, const char *limit,
                                const char *name, size_t length)
{
    const char *cursor = source;

    while (cursor < limit) {
        const char *match = cursor;
        const char *statement;
        const char *token;
        const char *token_end;
        const char *type_end;
        const char *type_start;
        const char *after;
        char type[LINK_TYPE_CAPACITY];

        while (match < limit &&
               (strncmp(match, name, length) ||
                (match > source && shader_identifier_character(match[-1])) ||
                shader_identifier_character(match[length])))
            ++match;
        if (match >= limit)
            return 0;
        type_end = match;
        while (type_end > source &&
               (type_end[-1] == ' ' || type_end[-1] == '\t' ||
                type_end[-1] == '\r' || type_end[-1] == '\n'))
            --type_end;
        type_start = type_end;
        while (type_start > source &&
               shader_identifier_character(type_start[-1]))
            --type_start;
        after = skip_shader_space(match + length);
        if (!((type_start < type_end &&
               copy_known_link_type(source, type_start, type_end, type) &&
               (*after == '=' || *after == ';' || *after == ',' ||
                *after == '[')) ||
              infer_link_declarator_list_type(source, match, length, type))) {
            cursor = match + length;
            continue;
        }
        statement = match;
        while (statement > source && statement[-1] != ';' && statement[-1] != '}' &&
               statement[-1] != '{')
            --statement;
        token = skip_shader_space(statement);
        token_end = token;
        while (shader_identifier_character(*token_end))
            ++token_end;
        return token_end - token == 5 && !strncmp(token, "const", 5);
    }
    return 0;
}

static int constant_call_selects_user_function(
    const char *source, const char *name, const char *name_end,
    const char *open, const char *close, const LinkFunction *functions,
    int function_count)
{
    char argument_types[MAX_LINK_PARAMETERS][LINK_TYPE_CAPACITY] = {{0}};
    int argument_known[MAX_LINK_PARAMETERS] = {0};
    int argument_count = collect_link_argument_types(
        source, open, close, functions, function_count, argument_types,
        argument_known);
    int function;

    for (function = 0; function < function_count; ++function) {
        int argument;
        int matches = 1;

        if (functions[function].declaration_start >= name ||
            strlen(functions[function].name) !=
                (size_t)(name_end - name) ||
            strncmp(functions[function].name, name,
                    (size_t)(name_end - name)) ||
            functions[function].parameter_count != argument_count)
            continue;
        for (argument = 0; argument < argument_count; ++argument)
            if (!argument_known[argument] ||
                strcmp(argument_types[argument],
                       functions[function].parameters[argument])) {
                matches = 0;
                break;
            }
        if (matches)
            return 1;
    }
    return 0;
}

static int validate_constant_initializer(Program *program, const char *source,
                                         const char *start, const char *end,
                                         const LinkLocalName *names, int name_count,
                                         const char *declared_name,
                                         const char *declared_name_end,
                                         const LinkFunction *function,
                                         const LinkFunction *functions,
                                         int function_count)
{
    const char *cursor = start;

    while (cursor < end) {
        const char *name;
        const char *name_end;
        const char *after;
        size_t length;
        int local;

        if (!shader_identifier_character(*cursor) ||
            (*cursor >= '0' && *cursor <= '9')) {
            ++cursor;
            continue;
        }
        name = cursor;
        name_end = name;
        while (name_end < end && shader_identifier_character(*name_end))
            ++name_end;
        length = (size_t)(name_end - name);
        after = control_space(name_end, end);
        if ((size_t)(declared_name_end - declared_name) == length &&
            !strncmp(declared_name, name, length)) {
            snprintf(program->log, sizeof(program->log),
                     "constant initializer references itself in %.31s",
                     function->name);
            return 0;
        }
        if ((name > start && name[-1] == '.') ||
            (length == 4 && !strncmp(name, "true", 4)) ||
            (length == 5 && !strncmp(name, "false", 5)) ||
            link_builtin_constant_name(name, length) ||
            link_constructor_type(name, length, (char[LINK_TYPE_CAPACITY]){0}) ||
            stage_has_struct_type_at(source, name, length, name)) {
            cursor = name_end;
            continue;
        }
        if (after < end && *after == '(') {
            const char *close = matching_parenthesis(after);

            if (!link_builtin_name(name, length) || !close || close >= end ||
                constant_call_selects_user_function(
                    source, name, name_end, after, close, functions,
                    function_count)) {
                snprintf(program->log, sizeof(program->log),
                         "constant initializer calls non-built-in function %.*s in %.20s",
                         (int)length, name, function->name);
                return 0;
            }
            cursor = name_end;
            continue;
        }
        for (local = name_count - 1; local >= 0; --local)
            if (strlen(names[local].name) == length &&
                !strncmp(names[local].name, name, length))
                break;
        if ((local >= 0 && !names[local].is_const) ||
            (local < 0 && global_variable_named_until(source,
                                                       function->declaration_start,
                                                       name, length) &&
             !global_name_is_const(source, function->declaration_start,
                                   name, length))) {
            snprintf(program->log, sizeof(program->log),
                     "constant initializer references mutable value %.*s in %.20s",
                     (int)length, name, function->name);
            return 0;
        }
        cursor = name_end;
    }
    return 1;
}

static int initializer_has_unresolved_self_reference(
    const char *source, const char *start, const char *end,
    const char *name, const char *name_end, const LinkLocalName *names,
    int name_count, const LinkFunction *functions, int function_count)
{
    size_t length = (size_t)(name_end - name);
    const char *cursor = start;
    int index;

    for (index = name_count - 1; index >= 0; --index)
        if (strlen(names[index].name) == length &&
            !strncmp(names[index].name, name, length))
            return 0;
    if (global_variable_named_until(source, name, name, length))
        return 0;
    while (cursor < end) {
        const char *match = cursor;
        const char *after;

        while (match < end &&
               ((size_t)(end - match) < length ||
                strncmp(match, name, length) ||
                (match > start && shader_identifier_character(match[-1])) ||
                (match + length < end &&
                 shader_identifier_character(match[length]))))
            ++match;
        if (match >= end)
            return 0;
        after = control_space(match + length, end);
        if (*after == '(') {
            for (index = 0; index < function_count; ++index)
                if (strlen(functions[index].name) == length &&
                    !strncmp(functions[index].name, name, length))
                    break;
            if (index < function_count) {
                cursor = match + length;
                continue;
            }
        }
        {
            const char *before = match;

            while (before > start &&
                   (before[-1] == ' ' || before[-1] == '\t' ||
                    before[-1] == '\r' || before[-1] == '\n'))
                --before;
            if (before == start || before[-1] != '.')
                return 1;
        }
        cursor = match + length;
    }
    return 0;
}

static int link_for_initializer_depth(const char *body_start, const char *token)
{
    const char *open = token;
    const char *word_end;
    const char *word_start;

    while (open > body_start &&
           (open[-1] == ' ' || open[-1] == '\t' || open[-1] == '\r' ||
            open[-1] == '\n'))
        --open;
    if (open == body_start || open[-1] != '(')
        return -1;
    word_end = open - 1;
    while (word_end > body_start &&
           (word_end[-1] == ' ' || word_end[-1] == '\t' || word_end[-1] == '\r' ||
            word_end[-1] == '\n'))
        --word_end;
    word_start = word_end;
    while (word_start > body_start && shader_identifier_character(word_start[-1]))
        --word_start;
    if (word_end - word_start != 3 || strncmp(word_start, "for", 3))
        return -1;
    return 1000 + (int)(open - body_start);
}

static int link_loop_condition_depth(const char *body_start, const char *token)
{
    const char *open = token;
    const char *word_end;
    const char *word_start;
    int nested = 0;

    while (open > body_start) {
        --open;
        if (*open == ')')
            ++nested;
        else if (*open == '(') {
            if (!nested)
                break;
            --nested;
        } else if (!nested && (*open == '{' || *open == '}'))
            return -1;
    }
    if (*open != '(')
        return -1;
    word_end = open;
    while (word_end > body_start &&
           (word_end[-1] == ' ' || word_end[-1] == '\t' ||
            word_end[-1] == '\r' || word_end[-1] == '\n'))
        --word_end;
    word_start = word_end;
    while (word_start > body_start &&
           shader_identifier_character(word_start[-1]))
        --word_start;
    if (word_end - word_start == 3 && !strncmp(word_start, "for", 3))
        return 1000 + (int)(open - body_start);
    if (word_end - word_start == 5 && !strncmp(word_start, "while", 5))
        return 2000 + (int)(open - body_start);
    return -1;
}

static int validate_function_local_names(Program *program, const char *source,
                                         const LinkFunction *functions,
                                         int function_count)
{
    int function_index;

    for (function_index = 0; function_index < function_count; ++function_index) {
        const LinkFunction *function = &functions[function_index];
        LinkLocalName names[MESAGL_MAX_SHADER_LOCALS];
        const char *cursor;
        int name_count = 0;
        int depth = 0;
        int parameter;

        if (!function->definition)
            continue;
        memset(names, 0, sizeof(names));
        for (parameter = 0; parameter < function->parameter_count; ++parameter) {
            const char *name = function->parameter_names[parameter];

            if (name[0] &&
                !add_link_local_name(program, names, &name_count, name,
                                     name + strlen(name), 0, 0, function))
                return 0;
        }
        cursor = function->body_start;
        while (cursor < function->body_end) {
            const char *token;
            const char *token_end;
            const char *after;
            char constructor[LINK_TYPE_CAPACITY];
            int declaration_depth = depth;
            int declaration_const = 0;
            int local;

            cursor = control_space(cursor, function->body_end);
            for (local = 0; local < name_count;) {
                if (names[local].scope_end && cursor >= names[local].scope_end) {
                    memmove(&names[local], &names[local + 1],
                            (size_t)(name_count - local - 1) * sizeof(names[0]));
                    --name_count;
                } else {
                    ++local;
                }
            }
            if (cursor >= function->body_end)
                break;
            if (*cursor == '{') {
                ++depth;
                ++cursor;
                continue;
            }
            if (*cursor == '}') {
                while (name_count > 0 && names[name_count - 1].depth >= depth)
                    --name_count;
                if (depth > 0)
                    --depth;
                ++cursor;
                continue;
            }
            if (!shader_identifier_character(*cursor)) {
                ++cursor;
                continue;
            }
            token = cursor;
            token_end = token;
            while (token_end < function->body_end &&
                   shader_identifier_character(*token_end))
                ++token_end;
            after = control_space(token_end, function->body_end);
            if (token_end - token == 6 && !strncmp(token, "struct", 6)) {
                const char *type_name = after;
                const char *type_end = type_name;
                const char *open;
                const char *close;
                const char *scope_end;
                int structure_depth = depth;

                while (type_end < function->body_end &&
                       shader_identifier_character(*type_end))
                    ++type_end;
                open = control_space(type_end, function->body_end);
                close = open < function->body_end && *open == '{'
                            ? control_matching(open, function->body_end, '{', '}')
                            : NULL;
                scope_end = close ? control_space(close + 1, function->body_end)
                                  : NULL;
                if (precision_is_unbraced_control_body(source, cursor))
                    structure_depth = 3000 +
                                      (int)(cursor - function->body_start);
                if (type_name == type_end || !close ||
                    !add_link_local_name(program, names, &name_count, type_name,
                                         type_end, structure_depth, 0, function))
                    return 0;
                if (structure_depth >= 3000 && scope_end && *scope_end == ';')
                    names[name_count - 1].scope_end = scope_end + 1;
                cursor = close + 1;
                continue;
            }
            while (link_qualifier(token, (size_t)(token_end - token))) {
                if (token_end - token == 5 && !strncmp(token, "const", 5))
                    declaration_const = 1;
                token = after;
                token_end = token;
                while (token_end < function->body_end &&
                       shader_identifier_character(*token_end))
                    ++token_end;
                after = control_space(token_end, function->body_end);
            }
            if ((!link_constructor_type(token, (size_t)(token_end - token),
                                        constructor) &&
                 !stage_has_struct_type_at(source, token,
                                           (size_t)(token_end - token), token)) ||
                after >= function->body_end ||
                !shader_identifier_character(*after)) {
                cursor = token_end;
                continue;
            }
            {
                int for_depth = link_for_initializer_depth(function->body_start,
                                                           cursor);
                int condition_depth = link_loop_condition_depth(
                    function->body_start, cursor);

                if (for_depth >= 0)
                    declaration_depth = for_depth;
                else if (condition_depth >= 0)
                    declaration_depth = condition_depth;
                else if (precision_is_unbraced_control_body(source, cursor))
                    declaration_depth = 3000 +
                                        (int)(cursor - function->body_start);
            }
            for (;;) {
                const char *variable = after;
                const char *variable_end = variable;
                const char *scan;
                const char *initializer = NULL;
                const char *initializer_end = NULL;
                int parentheses = 0;
                int brackets = 0;

                while (variable_end < function->body_end &&
                       shader_identifier_character(*variable_end))
                    ++variable_end;
                scan = control_space(variable_end, function->body_end);
                if (scan >= function->body_end ||
                    (*scan != '[' && *scan != '=' && *scan != ',' && *scan != ';')) {
                    cursor = token_end;
                    break;
                }
                if (*scan == '[') {
                    const char *array_end =
                        control_matching(scan, function->body_end, '[', ']');
                    const char *size_start = control_space(scan + 1,
                                                           function->body_end);
                    char size_type[LINK_TYPE_CAPACITY];

                    if (!array_end || size_start == array_end ||
                        !validate_constant_initializer(program, source,
                                                       size_start, array_end,
                                                       names, name_count,
                                                       variable, variable_end,
                                                       function, functions,
                                                       function_count)) {
                        if (!program->log[0])
                            snprintf(program->log, sizeof(program->log),
                                     "array size must be a constant integer expression in %.20s",
                                     function->name);
                        return 0;
                    }
                    if (infer_link_argument_type(source, size_start, array_end,
                                                 functions, function_count,
                                                 size_type) &&
                        strcmp(size_type, "int")) {
                        snprintf(program->log, sizeof(program->log),
                                 "array size must have integer type in %.20s",
                                 function->name);
                        return 0;
                    }
                }
                while (scan < function->body_end) {
                    if (*scan == '(')
                        ++parentheses;
                    else if (*scan == ')') {
                        if (!parentheses)
                            break;
                        --parentheses;
                    } else if (*scan == '[')
                        ++brackets;
                    else if (*scan == ']')
                        --brackets;
                    else if (!parentheses && !brackets && *scan == '=' &&
                             !initializer)
                        initializer = control_space(scan + 1, function->body_end);
                    else if (!parentheses && !brackets &&
                             (*scan == ',' || *scan == ';'))
                        break;
                    ++scan;
                }
                initializer_end = scan;
                if (initializer && initializer_has_unresolved_self_reference(
                                       source, initializer, initializer_end,
                                       variable, variable_end, names, name_count,
                                       functions, function_count)) {
                    snprintf(program->log, sizeof(program->log),
                             "initializer references its undeclared variable %.*s in %.31s",
                             (int)(variable_end - variable), variable,
                             function->name);
                    return 0;
                }
                if (declaration_const &&
                    (!initializer ||
                     !validate_constant_initializer(program, source, initializer,
                                                    initializer_end, names, name_count,
                                                    variable, variable_end,
                                                    function, functions,
                                                    function_count))) {
                    if (!initializer)
                        snprintf(program->log, sizeof(program->log),
                                 "const declaration requires an initializer in %.31s",
                                 function->name);
                    return 0;
                }
                if (!add_link_local_name(program, names, &name_count, variable,
                                         variable_end, declaration_depth,
                                         declaration_const, function))
                    return 0;
                link_loop_condition_declaration_end(
                    source, variable, &names[name_count - 1].scope_end);
                if (declaration_depth >= 3000)
                    names[name_count - 1].scope_end =
                        scan < function->body_end ? scan + 1 : scan;
                if (scan >= function->body_end || *scan != ',') {
                    cursor = scan < function->body_end ? scan + 1 : scan;
                    break;
                }
                after = control_space(scan + 1, function->body_end);
                if (after >= function->body_end ||
                    !shader_identifier_character(*after)) {
                    cursor = scan + 1;
                    break;
                }
            }
        }
    }
    return 1;
}

static int local_function_return_type(const char *source, const char *name)
{
    const char *type_end = name;
    const char *type_start;
    char type[LINK_TYPE_CAPACITY];

    while (type_end > source &&
           (type_end[-1] == ' ' || type_end[-1] == '\t' ||
            type_end[-1] == '\r' || type_end[-1] == '\n'))
        --type_end;
    type_start = type_end;
    while (type_start > source && shader_identifier_character(type_start[-1]))
        --type_start;
    if (type_start == type_end)
        return 0;
    if (type_end - type_start == 4 && !strncmp(type_start, "void", 4))
        return 1;
    return copy_known_link_type(source, type_start, type_end, type);
}

static int validate_no_local_function_declarations(
    Program *program, const char *source, const LinkFunction *functions,
    int function_count)
{
    int function_index;

    for (function_index = 0; function_index < function_count;
         ++function_index) {
        const LinkFunction *function = &functions[function_index];
        const char *cursor;

        if (!function->definition)
            continue;
        cursor = function->body_start;
        while (cursor < function->body_end) {
            const char *name_end;
            const char *open;
            const char *close;
            const char *after;

            if (!shader_identifier_character(*cursor) ||
                (*cursor >= '0' && *cursor <= '9') ||
                (cursor > source && shader_identifier_character(cursor[-1]))) {
                ++cursor;
                continue;
            }
            name_end = cursor;
            while (name_end < function->body_end &&
                   shader_identifier_character(*name_end))
                ++name_end;
            open = control_space(name_end, function->body_end);
            if (open >= function->body_end || *open != '(' ||
                !local_function_return_type(source, cursor)) {
                cursor = name_end;
                continue;
            }
            close = control_matching(open, function->body_end, '(', ')');
            if (!close) {
                cursor = name_end;
                continue;
            }
            after = control_space(close + 1, function->body_end);
            if (after < function->body_end &&
                (*after == ';' || *after == '{')) {
                snprintf(program->log, sizeof(program->log),
                         "function declarations are not allowed inside %.31s",
                         function->name);
                return 0;
            }
            cursor = name_end;
        }
    }
    return 1;
}

static int link_language_word(const char *name, size_t length)
{
    static const char *const words[] = {
        "attribute", "bool",      "break",    "bvec2",    "bvec3",
        "bvec4",     "const",     "continue", "discard",  "do",
        "else",      "false",     "float",    "for",      "highp",
        "if",        "in",        "inout",    "int",      "invariant",
        "ivec2",     "ivec3",     "ivec4",    "lowp",     "mat2",
        "mat3",      "mat4",      "mediump",  "out",      "precision",
        "return",    "sampler2D", "samplerCube", "struct", "true",
        "uniform",   "varying",   "vec2",     "vec3",     "vec4",
        "void",      "while",
    };
    size_t index;

    for (index = 0; index < sizeof(words) / sizeof(words[0]); ++index)
        if (strlen(words[index]) == length &&
            !strncmp(words[index], name, length))
            return 1;
    return 0;
}

static int link_is_structure_declaration_name(const char *start,
                                              const char *name)
{
    const char *end = name;
    const char *token;

    while (end > start && (end[-1] == ' ' || end[-1] == '\t' ||
                           end[-1] == '\r' || end[-1] == '\n'))
        --end;
    token = end;
    while (token > start && shader_identifier_character(token[-1]))
        --token;
    return end - token == 6 && !strncmp(token, "struct", 6);
}

static int link_identifier_is_declarator(const char *source, const char *name)
{
    const char *type_end = name;
    const char *type_start;
    char type[LINK_TYPE_CAPACITY];

    while (type_end > source &&
           (type_end[-1] == ' ' || type_end[-1] == '\t' ||
            type_end[-1] == '\r' || type_end[-1] == '\n'))
        --type_end;
    type_start = type_end;
    while (type_start > source &&
           shader_identifier_character(type_start[-1]))
        --type_start;
    return type_start < type_end &&
           copy_known_link_type(source, type_start, type_end, type);
}

static int link_identifier_is_inline_struct_declarator(const char *source,
                                                       const char *name)
{
    const char *close = name;
    const char *open;
    const char *word_end;
    const char *word;
    int depth = 1;

    while (close > source && (close[-1] == ' ' || close[-1] == '\t' ||
                              close[-1] == '\r' || close[-1] == '\n'))
        --close;
    if (close == source || close[-1] != '}')
        return 0;
    open = close - 1;
    while (open > source && depth) {
        --open;
        if (*open == '}')
            ++depth;
        else if (*open == '{')
            --depth;
    }
    if (depth)
        return 0;
    word_end = open;
    while (word_end > source && (word_end[-1] == ' ' || word_end[-1] == '\t' ||
                                 word_end[-1] == '\r' || word_end[-1] == '\n'))
        --word_end;
    word = word_end;
    while (word > source && shader_identifier_character(word[-1]))
        --word;
    if (word_end - word != 6 || strncmp(word, "struct", 6)) {
        word_end = word;
        while (word_end > source &&
               (word_end[-1] == ' ' || word_end[-1] == '\t' ||
                word_end[-1] == '\r' || word_end[-1] == '\n'))
            --word_end;
        word = word_end;
        while (word > source && shader_identifier_character(word[-1]))
            --word;
    }
    return word_end - word == 6 && !strncmp(word, "struct", 6);
}

static int validate_function_identifiers(Program *program, const char *source,
                                         const LinkFunction *functions,
                                         int function_count)
{
    int function_index;

    for (function_index = 0; function_index < function_count; ++function_index) {
        const LinkFunction *function = &functions[function_index];
        const char *cursor;

        if (!function->definition)
            continue;
        cursor = function->body_start;
        while (cursor < function->body_end) {
            const char *name;
            const char *end;
            const char *before;
            const char *after;
            size_t length;
            char type[LINK_TYPE_CAPACITY];

            if (cursor + 1 < function->body_end && cursor[0] == '/' &&
                cursor[1] == '/') {
                cursor += 2;
                while (cursor < function->body_end && *cursor != '\n')
                    ++cursor;
                continue;
            }
            if (cursor + 1 < function->body_end && cursor[0] == '/' &&
                cursor[1] == '*') {
                cursor += 2;
                while (cursor + 1 < function->body_end &&
                       !(cursor[0] == '*' && cursor[1] == '/'))
                    ++cursor;
                if (cursor + 1 < function->body_end)
                    cursor += 2;
                continue;
            }
            if ((*cursor >= '0' && *cursor <= '9') ||
                (*cursor == '.' && cursor + 1 < function->body_end &&
                 cursor[1] >= '0' && cursor[1] <= '9')) {
                char *number_end;

                (void)strtod(cursor, &number_end);
                cursor = number_end > cursor ? number_end : cursor + 1;
                continue;
            }
            if (!shader_identifier_character(*cursor)) {
                ++cursor;
                continue;
            }
            name = cursor;
            end = name;
            while (end < function->body_end &&
                   shader_identifier_character(*end))
                ++end;
            length = (size_t)(end - name);
            before = name;
            while (before > function->body_start &&
                   (before[-1] == ' ' || before[-1] == '\t' ||
                    before[-1] == '\r' || before[-1] == '\n'))
                --before;
            after = control_space(end, function->body_end);
            if ((before > function->body_start && before[-1] == '.') ||
                link_is_structure_declaration_name(function->body_start, name) ||
                link_identifier_is_declarator(source, name) ||
                link_identifier_is_inline_struct_declarator(source, name) ||
                link_language_word(name, length) || core_gl_identifier(name, length) ||
                stage_has_struct_type_at(source, name, length, name) ||
                global_variable_named_until(source, function->declaration_start,
                                            name, length) ||
                infer_link_declarator_list_type(source, name, length, type) ||
                infer_link_declared_type(source, name, name, length, type) ||
                *after == '(') {
                cursor = end;
                continue;
            }
            snprintf(program->log, sizeof(program->log),
                     "undeclared identifier %.*s in %.31s", (int)length,
                     name, function->name);
            return 0;
        }
    }
    return 1;
}

static int validate_function_prototypes_with_storage(
    Program *program, const char *source, int fragment_stage,
    int require_definitions, LinkFunction *functions)
{
    const char *cursor = source;
    const char *statement = source;
    int function_count = 0;
    int brace_depth = 0;

    if (!validate_global_variable_declarations(program, source) ||
        !validate_struct_type_namespace(program, source))
        return 0;

    while (*cursor) {
        if (cursor[0] == '/' && cursor[1] == '/') {
            cursor += 2;
            while (*cursor && *cursor != '\n')
                ++cursor;
            continue;
        }
        if (cursor[0] == '/' && cursor[1] == '*') {
            cursor += 2;
            while (*cursor && !(cursor[0] == '*' && cursor[1] == '/'))
                ++cursor;
            if (*cursor)
                cursor += 2;
            continue;
        }
        if (*cursor == '{') {
            ++brace_depth;
        } else if (*cursor == '}') {
            if (brace_depth > 0)
                --brace_depth;
            if (!brace_depth)
                statement = cursor + 1;
        } else if (*cursor == ';') {
            if (!brace_depth)
                statement = cursor + 1;
        } else if (*cursor == '(' && !brace_depth) {
            const char *close = matching_parenthesis(cursor);
            const char *after;
            const char *name_end = cursor;
            const char *name_start;
            const char *return_end;
            const char *return_start;
            const char *scan;
            LinkFunction *function;

            if (!close)
                return 1;
            after = skip_shader_space(close + 1);
            if (*after != ';' && *after != '{') {
                cursor = close;
                ++cursor;
                continue;
            }
            scan = statement;
            while (scan < cursor && *scan != '=')
                ++scan;
            if (scan < cursor) {
                cursor = close;
                ++cursor;
                continue;
            }
            while (name_end > statement &&
                   (name_end[-1] == ' ' || name_end[-1] == '\t' || name_end[-1] == '\r' ||
                    name_end[-1] == '\n'))
                --name_end;
            name_start = name_end;
            while (name_start > statement && shader_identifier_character(name_start[-1]))
                --name_start;
            return_end = name_start;
            while (return_end > statement &&
                   (return_end[-1] == ' ' || return_end[-1] == '\t' ||
                    return_end[-1] == '\r' || return_end[-1] == '\n'))
                --return_end;
            return_start = return_end;
            while (return_start > statement && shader_identifier_character(return_start[-1]))
                --return_start;
            if (name_start == name_end || return_start == return_end) {
                cursor = close;
                ++cursor;
                continue;
            }
            if (function_count >= MAX_LINK_FUNCTIONS) {
                strcpy(program->log, "too many shader functions");
                return 0;
            }
            function = &functions[function_count];
            memset(function, 0, sizeof(*function));
            function->declaration_start = statement;
            {
                const char *precision_end = return_start;
                const char *precision_start;
                size_t precision_length;

                while (precision_end > statement &&
                       (precision_end[-1] == ' ' || precision_end[-1] == '\t' ||
                        precision_end[-1] == '\r' || precision_end[-1] == '\n'))
                    --precision_end;
                precision_start = precision_end;
                while (precision_start > statement &&
                       shader_identifier_character(precision_start[-1]))
                    --precision_start;
                precision_length = (size_t)(precision_end - precision_start);
                if (precision_length == 4 &&
                    !strncmp(precision_start, "lowp", 4))
                    function->return_precision = 1;
                else if (precision_length == 7 &&
                         !strncmp(precision_start, "mediump", 7))
                    function->return_precision = 2;
                else if (precision_length == 5 &&
                         !strncmp(precision_start, "highp", 5))
                    function->return_precision = 3;
            }
            if (!copy_link_token(function->name, sizeof(function->name), name_start, name_end) ||
                !copy_link_token(function->return_type, sizeof(function->return_type),
                                 return_start, return_end) ||
                !parse_link_parameters(source, cursor, close, function,
                                       fragment_stage)) {
                strcpy(program->log, "unsupported function signature");
                return 0;
            }
            function->return_precision = link_effective_precision(
                source, function->declaration_start, function->return_type,
                function->return_precision, fragment_stage);
            if (link_builtin_name(function->name, strlen(function->name))) {
                char parameter_types[MAX_LINK_PARAMETERS][LINK_TYPE_CAPACITY] = {{0}};
                int known[MAX_LINK_PARAMETERS];
                int parameter;

                for (parameter = 0; parameter < function->parameter_count; ++parameter) {
                    strcpy(parameter_types[parameter],
                           function->parameters[parameter]);
                    known[parameter] = 1;
                }
                if (link_builtin_accepts_arity(function->name,
                                               strlen(function->name),
                                               function->parameter_count) &&
                    link_builtin_known_types_valid(
                        function->name, strlen(function->name),
                        parameter_types, known,
                        function->parameter_count)) {
                    snprintf(program->log, sizeof(program->log),
                             "built-in function cannot be redeclared: %.47s",
                             function->name);
                    return 0;
                }
            }
            function->definition = *after == '{';
            if (function->definition) {
                function->body_start = after + 1;
                function->body_end = control_matching(
                    after, source + strlen(source), '{', '}');
                if (!function->body_end) {
                    strcpy(program->log, "unterminated function body");
                    return 0;
                }
            }
            if (!strcmp(function->name, "main") &&
                (strcmp(function->return_type, "void") ||
                 function->parameter_count != 0)) {
                strcpy(program->log, "main must return void and take no parameters");
                return 0;
            }
            ++function_count;
        }
        ++cursor;
    }
    {
        int declaration;

        for (declaration = 0; declaration < function_count; ++declaration) {
            size_t name_length = strlen(functions[declaration].name);

            if (stage_has_struct_type(source, functions[declaration].name,
                                      name_length) ||
                global_variable_named(source, functions[declaration].name,
                                      name_length)) {
                snprintf(program->log, sizeof(program->log),
                         "function name conflicts with a global declaration: %.47s",
                         functions[declaration].name);
                return 0;
            }
        }
        for (declaration = 0; declaration < function_count; ++declaration) {
            int candidate;

            for (candidate = declaration + 1; candidate < function_count;
                 ++candidate) {
                int parameter;

                if (!same_link_parameter_signature(&functions[declaration],
                                                   &functions[candidate]))
                    continue;
                if (strcmp(functions[declaration].return_type,
                           functions[candidate].return_type)) {
                    snprintf(program->log, sizeof(program->log),
                             "function overloads cannot differ only by return type: %.63s",
                             functions[declaration].name);
                    return 0;
                }
                if (functions[declaration].return_precision !=
                        functions[candidate].return_precision) {
                    snprintf(program->log, sizeof(program->log),
                             "function return precision qualifiers must match: %.63s",
                             functions[declaration].name);
                    return 0;
                }
                for (parameter = 0;
                     parameter < functions[declaration].parameter_count;
                     ++parameter) {
                    if (functions[declaration].parameter_modes[parameter] !=
                            functions[candidate].parameter_modes[parameter] ||
                        functions[declaration].parameter_consts[parameter] !=
                            functions[candidate].parameter_consts[parameter]) {
                        snprintf(program->log, sizeof(program->log),
                                 "function parameter qualifiers must match: %.63s",
                                 functions[declaration].name);
                        return 0;
                    }
                    if (functions[declaration].parameter_precisions[parameter] !=
                            functions[candidate].parameter_precisions[parameter]) {
                        snprintf(program->log, sizeof(program->log),
                                 "function parameter precision qualifiers must match: %.63s",
                                 functions[declaration].name);
                        return 0;
                    }
                }
            }
        }
        for (declaration = 0; declaration < function_count; ++declaration) {
            int candidate;
            int matching_definitions = 0;
            int matching_prototypes = 0;
            int same_name_definition = 0;

            for (candidate = 0; candidate < function_count; ++candidate) {
                if (functions[candidate].definition &&
                    !strcmp(functions[declaration].name,
                            functions[candidate].name))
                    same_name_definition = 1;
                if (!same_link_signature(&functions[declaration],
                                         &functions[candidate]))
                    continue;
                if (functions[candidate].definition) {
                    ++matching_definitions;
                } else {
                    ++matching_prototypes;
                }
            }
            if (matching_prototypes > 1) {
                snprintf(program->log, sizeof(program->log),
                         "duplicate function prototype: %.63s",
                         functions[declaration].name);
                return 0;
            }
            if (functions[declaration].definition && matching_definitions > 1) {
                snprintf(program->log, sizeof(program->log),
                         "duplicate function definition: %.63s",
                         functions[declaration].name);
                return 0;
            }
            if (require_definitions && !functions[declaration].definition &&
                matching_definitions != 1) {
                snprintf(program->log, sizeof(program->log),
                         same_name_definition ? "function signature mismatch: %.63s"
                                              : "undefined function: %.63s",
                         functions[declaration].name);
                return 0;
            }
        }
    }
    return validate_function_return_types(program, source, functions, function_count) &&
           validate_function_local_names(program, source, functions, function_count) &&
           validate_no_local_function_declarations(program, source, functions,
                                                   function_count) &&
           validate_function_identifiers(program, source, functions, function_count) &&
           validate_function_condition_types(program, source, functions, function_count) &&
           validate_function_conditional_types(program, source, functions, function_count) &&
           validate_function_comparison_types(program, source, functions, function_count) &&
           validate_function_arithmetic_types(program, source, functions, function_count) &&
           validate_function_unary_types(program, source, functions, function_count) &&
           validate_function_subscript_types(program, source, functions, function_count) &&
           validate_function_swizzles(program, source, functions, function_count) &&
           validate_function_assignment_types(program, source, functions, function_count,
                                              fragment_stage) &&
           validate_function_calls(program, source, functions, function_count,
                                   fragment_stage) &&
           validate_nonrecursive_functions(program, source, functions,
                                           function_count);
}

static int validate_function_prototypes(Program *program, const char *source,
                                        int fragment_stage, int require_definitions)
{
    LinkFunction *functions;
    int valid;

    functions = ntglAlloc(MAX_LINK_FUNCTIONS * sizeof(*functions));
    if (!functions) {
        strcpy(program->log, "out of memory while validating shader functions");
        return 0;
    }
    valid = validate_function_prototypes_with_storage(
        program, source, fragment_stage, require_definitions, functions);
    ntglFree(functions);
    return valid;
}

static void build_program_executable(Program *program)
{
    int vertex_count;
    int fragment_count;
    int vertex_compiled;
    int fragment_compiled;
    int writes_frag_color;
    int writes_frag_data;
    int vertex_sampler_count;
    int fragment_sampler_count;
    ShaderReachability vertex_reachability;
    ShaderReachability fragment_reachability;
    int reachability_initialized = 0;

    program->linked = 0;
    program->validated = 0;
    program->log[0] = '\0';
    ntglFree(program->linked_vertex_source);
    ntglFree(program->linked_fragment_source);
    ntglFree(program->vertex_body);
    ntglFree(program->fragment_body);
    program->vertex_body = NULL;
    program->fragment_body = NULL;
    program->linked_vertex_source = combine_stage_sources(program, GL_VERTEX_SHADER,
                                                           &vertex_count, &vertex_compiled);
    program->linked_fragment_source = combine_stage_sources(program, GL_FRAGMENT_SHADER,
                                                             &fragment_count, &fragment_compiled);
    if (!vertex_count || !fragment_count || !vertex_compiled || !fragment_compiled) {
        strcpy(program->log, "compiled vertex and fragment shaders are required");
        return;
    }
    if (!program->linked_vertex_source || !program->linked_fragment_source) {
        ntglFree(program->linked_vertex_source);
        ntglFree(program->linked_fragment_source);
        program->linked_vertex_source = NULL;
        program->linked_fragment_source = NULL;
        strcpy(program->log, "out of memory while linking shader sources");
        return;
    }
    if (main_function_count(program->linked_vertex_source) != 1 ||
        main_function_count(program->linked_fragment_source) != 1) {
        strcpy(program->log, "exactly one main function is required per shader stage");
        return;
    }
#if MESAGL_GLES2_PROFILE == MESAGL_GLES2_PROFILE_FULL
    if (!validate_builtin_invariant_linkage(program))
        return;
#endif
    if (!validate_function_prototypes(program, program->linked_vertex_source, 0, 1) ||
        !validate_function_prototypes(program, program->linked_fragment_source, 1, 1))
        return;
    writes_frag_color = shader_identifier_written(program->linked_fragment_source,
                                                   "gl_FragColor", 12, NULL);
    writes_frag_data = shader_identifier_written(program->linked_fragment_source,
                                                  "gl_FragData", 11, NULL);
    if (shader_has_identifier(program->linked_fragment_source, "gl_FragData") &&
        !validate_fragment_data_indices(program,
                                        program->linked_fragment_source))
        return;
    if (writes_frag_color && writes_frag_data) {
        strcpy(program->log,
               "fragment shader cannot statically write both gl_FragColor and gl_FragData");
        return;
    }
    program->uses_texture = strstr(program->linked_fragment_source, "texture2D") ||
                            strstr(program->linked_fragment_source, "textureCube");
    if (!initialize_shader_reachability(&vertex_reachability,
                                        program->linked_vertex_source) ||
        !initialize_shader_reachability(&fragment_reachability,
                                        program->linked_fragment_source)) {
        destroy_shader_reachability(&vertex_reachability);
        strcpy(program->log, "out of memory while analyzing active shader resources");
        return;
    }
    reachability_initialized = 1;
#if MESAGL_GLES2_PROFILE == MESAGL_GLES2_PROFILE_FULL
    if (shader_has_keyword(program->linked_fragment_source, "attribute")) {
        strcpy(program->log, "attribute declaration is only valid in a vertex shader");
        goto reachability_cleanup;
    }
    if (!validate_uniform_interfaces(program, program->linked_vertex_source,
                                     program->linked_fragment_source))
        goto reachability_cleanup;
    if (!collect_attributes(program, program->linked_vertex_source,
                            &vertex_reachability) ||
        !collect_varyings(program, program->linked_vertex_source,
                          stage_has_invariant_all(program, GL_VERTEX_SHADER)))
        goto reachability_cleanup;
    if (!validate_fragment_varyings(program, program->linked_fragment_source,
                                    &fragment_reachability) ||
        !finalize_active_varyings(program))
        goto reachability_cleanup;
#endif
    free_program_uniforms(program);
    collect_uniforms(program, program->linked_vertex_source,
                     &vertex_reachability, &fragment_reachability,
                     MESAGL_MAX_VERTEX_UNIFORM_VECTORS,
                     &vertex_sampler_count, GL_VERTEX_SHADER);
    collect_uniforms(program, program->linked_fragment_source,
                     &fragment_reachability, &vertex_reachability,
                     MESAGL_MAX_FRAGMENT_UNIFORM_VECTORS,
                     &fragment_sampler_count, GL_FRAGMENT_SHADER);
    if (!program->log[0] &&
        vertex_sampler_count > MESAGL_MAX_VERTEX_TEXTURE_IMAGE_UNITS)
        strcpy(program->log, "vertex sampler limit exceeded");
    if (!program->log[0] &&
        fragment_sampler_count > MESAGL_MAX_FRAGMENT_TEXTURE_IMAGE_UNITS)
        strcpy(program->log, "fragment sampler limit exceeded");
    if (!program->log[0] &&
        vertex_sampler_count + fragment_sampler_count >
            MESAGL_MAX_COMBINED_TEXTURE_IMAGE_UNITS)
        strcpy(program->log, "combined sampler limit exceeded");
    if (program->log[0]) {
        free_program_uniforms(program);
        goto reachability_cleanup;
    }
    if (writes_frag_color)
        find_fragment_output(program, program->linked_fragment_source);
    extract_assignment(program->linked_vertex_source, "gl_Position", program->vertex_position,
                       sizeof(program->vertex_position));
    if (writes_frag_color || writes_frag_data)
        extract_assignment(program->linked_fragment_source,
                           writes_frag_color ? "gl_FragColor" : "gl_FragData[0]",
                           program->fragment_color, sizeof(program->fragment_color));
    extract_discard_condition(program->linked_fragment_source, program->fragment_discard,
                              sizeof(program->fragment_discard));
    program->vertex_body = copy_main_body(program->linked_vertex_source);
    program->fragment_body = copy_main_body(program->linked_fragment_source);
    if (!program->vertex_body || !program->fragment_body) {
        ntglFree(program->vertex_body);
        ntglFree(program->fragment_body);
        program->vertex_body = NULL;
        program->fragment_body = NULL;
        strcpy(program->log, "out of memory while copying shader executable bodies");
        goto reachability_cleanup;
    }
#if MESAGL_GLES2_PROFILE == MESAGL_GLES2_PROFILE_FULL && \
    MESAGL_ENABLE_SHADER_FAST_PATHS
    configure_imgui_fast_path(program);
#endif
    program->linked = 1;
    program->executable = 1;
reachability_cleanup:
    if (reachability_initialized) {
        destroy_shader_reachability(&fragment_reachability);
        destroy_shader_reachability(&vertex_reachability);
    }
}

void glLinkProgram(GLuint name)
{
    Program *program = find_program(name);
    Program candidate;
    int i;

    if (!program) {
        program_name_error(name);
        return;
    }

    memset(&candidate, 0, sizeof(candidate));
    candidate.name = program->name;
    candidate.attached_count = program->attached_count;
    memcpy(candidate.attached, program->attached,
           (size_t)program->attached_count * sizeof(program->attached[0]));
    candidate.delete_pending = program->delete_pending;
    for (i = 0; i < program->binding_count; ++i) {
        if (!program->bindings[i].requested)
            continue;
        candidate.bindings[candidate.binding_count++] = program->bindings[i];
        candidate.bindings[candidate.binding_count - 1].index =
            candidate.bindings[candidate.binding_count - 1].requested_index;
        candidate.bindings[candidate.binding_count - 1].active = 0;
    }

    build_program_executable(&candidate);
    if (!candidate.linked) {
        free_program_uniforms(&candidate);
        ntglFree(candidate.linked_vertex_source);
        ntglFree(candidate.linked_fragment_source);
        ntglFree(candidate.vertex_body);
        ntglFree(candidate.fragment_body);
        program->linked = 0;
        program->validated = 0;
        memcpy(program->log, candidate.log, sizeof(program->log));
        return;
    }

    free_program_uniforms(program);
    ntglFree(program->linked_vertex_source);
    ntglFree(program->linked_fragment_source);
    ntglFree(program->vertex_body);
    ntglFree(program->fragment_body);
    *program = candidate;
    if (current_program == name) {
        ntglSetFragmentFunction(program->fragment_output_uniform >= 0 ? execute_fragment : NULL,
                                program);
        if (program->uses_texture) {
            mesaGLSetGLES2TextureState(1);
        } else {
            mesaGLSetGLES2TextureState(0);
        }
    }
}

void glGetProgramiv(GLuint name, GLenum pname, GLint *params)
{
    Program *program = find_program(name);
    int i;

    if (!params)
        return;
    if (!program) {
        program_name_error(name);
        return;
    }
    if (pname == GL_LINK_STATUS)
        *params = program->linked;
    else if (pname == GL_DELETE_STATUS)
        *params = program->delete_pending;
    else if (pname == GL_VALIDATE_STATUS)
        *params = program->validated;
    else if (pname == GL_ATTACHED_SHADERS)
        *params = program->attached_count;
    else if (pname == GL_ACTIVE_UNIFORMS)
        *params = program->uniform_count;
    else if (pname == GL_ACTIVE_ATTRIBUTES) {
        *params = 0;
        for (i = 0; i < program->binding_count; ++i)
            *params += program->bindings[i].active;
    } else if (pname == GL_ACTIVE_UNIFORM_MAX_LENGTH) {
        *params = 0;
        for (i = 0; i < program->uniform_count; ++i) {
            GLint length = (GLint)strlen(program->uniforms[i].name) + 1;

            if (program->uniforms[i].array_declared &&
                !program->uniforms[i].aggregate_size &&
                !program->uniforms[i].member_size)
                length += 3;
            if (length > *params)
                *params = length;
        }
    } else if (pname == GL_ACTIVE_ATTRIBUTE_MAX_LENGTH) {
        *params = 0;
        for (i = 0; i < program->binding_count; ++i) {
            GLint length = (GLint)strlen(program->bindings[i].name) + 1;

            if (!program->bindings[i].active)
                continue;
            if (length > *params)
                *params = length;
        }
    } else if (pname == GL_INFO_LOG_LENGTH)
        *params = program->log[0] ? (GLint)strlen(program->log) + 1 : 0;
    else
        mesaGLSetError(GL_INVALID_ENUM);
}

void glGetAttachedShaders(GLuint name, GLsizei max_count, GLsizei *count, GLuint *attached)
{
    Program *program = find_program(name);
    GLsizei result = 0;

    if (!program) {
        program_name_error(name);
        return;
    }
    if (max_count < 0) {
        mesaGLSetError(GL_INVALID_VALUE);
        return;
    }
    if (max_count > 0 && !attached) {
        mesaGLSetError(GL_INVALID_VALUE);
        return;
    }
    while (result < max_count && result < program->attached_count) {
        attached[result] = program->attached[result];
        ++result;
    }
    if (count)
        *count = result;
}

static GLenum uniform_gl_type(UniformType type)
{
    switch (type) {
    case UNIFORM_FLOAT:
        return GL_FLOAT;
    case UNIFORM_VEC2:
        return GL_FLOAT_VEC2;
    case UNIFORM_VEC3:
        return GL_FLOAT_VEC3;
    case UNIFORM_VEC4:
        return GL_FLOAT_VEC4;
    case UNIFORM_INT:
        return GL_INT;
    case UNIFORM_IVEC2:
        return GL_INT_VEC2;
    case UNIFORM_IVEC3:
        return GL_INT_VEC3;
    case UNIFORM_IVEC4:
        return GL_INT_VEC4;
    case UNIFORM_BOOL:
        return GL_BOOL;
    case UNIFORM_BVEC2:
        return GL_BOOL_VEC2;
    case UNIFORM_BVEC3:
        return GL_BOOL_VEC3;
    case UNIFORM_BVEC4:
        return GL_BOOL_VEC4;
    case UNIFORM_MAT2:
        return GL_FLOAT_MAT2;
    case UNIFORM_MAT3:
        return GL_FLOAT_MAT3;
    case UNIFORM_MAT4:
        return GL_FLOAT_MAT4;
    case UNIFORM_SAMPLER2D:
        return GL_SAMPLER_2D;
    case UNIFORM_SAMPLERCUBE:
        return GL_SAMPLER_CUBE;
    default:
        return GL_FLOAT;
    }
}

static int uniform_components(UniformType type)
{
    switch (type) {
    case UNIFORM_VEC2:
    case UNIFORM_IVEC2:
    case UNIFORM_BVEC2:
        return 2;
    case UNIFORM_VEC3:
    case UNIFORM_IVEC3:
    case UNIFORM_BVEC3:
        return 3;
    case UNIFORM_VEC4:
    case UNIFORM_IVEC4:
    case UNIFORM_BVEC4:
        return 4;
    case UNIFORM_MAT2:
        return 4;
    case UNIFORM_MAT3:
        return 9;
    case UNIFORM_MAT4:
        return 16;
    default:
        return 1;
    }
}

static int uniform_is_integer(UniformType type)
{
    return type == UNIFORM_INT || type == UNIFORM_IVEC2 || type == UNIFORM_IVEC3 ||
           type == UNIFORM_IVEC4 || type == UNIFORM_BOOL || type == UNIFORM_BVEC2 ||
           type == UNIFORM_BVEC3 || type == UNIFORM_BVEC4 || type == UNIFORM_SAMPLER2D ||
           type == UNIFORM_SAMPLERCUBE;
}

void glGetActiveUniform(GLuint name, GLuint index, GLsizei size, GLsizei *length, GLint *count,
                        GLenum *type, GLchar *uniform_name)
{
    Program *program = find_program(name);
    Uniform *uniform;

    if (!program) {
        program_name_error(name);
        return;
    }
    if (size < 0 || index >= (GLuint)program->uniform_count) {
        mesaGLSetError(GL_INVALID_VALUE);
        return;
    }
    uniform = &program->uniforms[index];
    if (uniform->array_declared && !uniform->aggregate_size &&
        !uniform->member_size) {
        copy_array_name(uniform->name, size, length, uniform_name);
    } else
        copy_log(uniform->name, size, length, uniform_name);
    if (count)
        *count = uniform->size;
    if (type)
        *type = uniform_gl_type(uniform->type);
}

static int aggregate_uniform_index(const Uniform *uniform, const char *name)
{
    const char *zero;
    const char *index_start;
    char *index_end;
    long index;
    size_t prefix_length;

    if (uniform->size <= 1 ||
        (uniform->aggregate_size <= 1 && uniform->member_size <= 1))
        return -1;
    zero = strstr(uniform->name, "[0]");
    if (uniform->member_size > 1) {
        const char *next = zero;

        while (next && (next = strstr(next + 3, "[0]")) != NULL)
            zero = next;
    }
    if (!zero)
        return -1;
    prefix_length = (size_t)(zero - uniform->name);
    if (strncmp(uniform->name, name, prefix_length) || name[prefix_length] != '[')
        return -1;
    index_start = name + prefix_length + 1;
    index = strtol(index_start, &index_end, 10);
    if (index_start == index_end || *index_end != ']' ||
        strcmp(index_end + 1, zero + 3) || index < 0 || index >= uniform->size)
        return -1;
    return (int)index;
}

void glGetActiveAttrib(GLuint name, GLuint index, GLsizei size, GLsizei *length, GLint *count,
                       GLenum *type, GLchar *attribute_name)
{
    Program *program = find_program(name);
    Binding *binding = NULL;
    GLuint active_index = 0;
    int i;

    if (!program) {
        program_name_error(name);
        return;
    }
    if (size < 0) {
        mesaGLSetError(GL_INVALID_VALUE);
        return;
    }
    for (i = 0; i < program->binding_count; ++i) {
        if (!program->bindings[i].active)
            continue;
        if (active_index++ == index) {
            binding = &program->bindings[i];
            break;
        }
    }
    if (!binding) {
        mesaGLSetError(GL_INVALID_VALUE);
        return;
    }
    copy_log(binding->name, size, length, attribute_name);
    if (count)
        *count = 1;
    if (type)
        *type = binding->type;
}

static int program_sampler_state_valid(const Program *program)
{
    unsigned char unit_types[MESAGL_MAX_TEXTURE_UNITS] = {0};
    int uniform_index;

    if (!program || !program->executable)
        return 0;
    for (uniform_index = 0; uniform_index < program->uniform_count; ++uniform_index) {
        const Uniform *uniform = &program->uniforms[uniform_index];
        unsigned char sampler_type;
        int element;

        if (uniform->type != UNIFORM_SAMPLER2D &&
            uniform->type != UNIFORM_SAMPLERCUBE)
            continue;
        sampler_type = uniform->type == UNIFORM_SAMPLER2D ? 1 : 2;
        for (element = 0; element < uniform->size; ++element) {
            const GLint *storage = uniform->array_integer
                                       ? uniform->array_integer + element * 4
                                       : uniform->integer;
            GLint unit = storage[0];

            if (unit < 0 || unit >= MESAGL_MAX_TEXTURE_UNITS)
                return 0;
            if (unit_types[unit] && unit_types[unit] != sampler_type)
                return 0;
            unit_types[unit] = sampler_type;
        }
    }
    return 1;
}

void glValidateProgram(GLuint name)
{
    Program *program = find_program(name);

    if (!program) {
        program_name_error(name);
        return;
    }
    program->validated = program->linked && program->executable &&
                         program_sampler_state_valid(program);
    if (!program->linked)
        strcpy(program->log, "program has not been successfully linked");
    else if (!program->validated)
        strcpy(program->log,
               "sampler uniforms use an invalid unit or conflicting sampler types");
    else
        program->log[0] = '\0';
}

void glGetProgramInfoLog(GLuint name, GLsizei size, GLsizei *length, GLchar *log)
{
    Program *program = find_program(name);

    if (!program) {
        program_name_error(name);
        return;
    }
    if (size < 0) {
        mesaGLSetError(GL_INVALID_VALUE);
        return;
    }
    copy_log(program->log, size, length, log);
}

void glUseProgram(GLuint name)
{
    Program *program = find_program(name);
    Program *previous = find_program(current_program);

    if (!name) {
        current_program = 0;
        ntglSetFragmentFunction(NULL, NULL);
        if (previous && previous->delete_pending)
            destroy_program(previous);
        return;
    }
    if (!program) {
        program_name_error(name);
        return;
    }
    if (!program->linked) {
        mesaGLSetError(GL_INVALID_OPERATION);
        return;
    }
    current_program = name;
    ntglSetFragmentFunction(program->fragment_output_uniform >= 0 ? execute_fragment : NULL,
                            program);
    if (program->uses_texture) {
        mesaGLSetGLES2TextureState(1);
    } else
        mesaGLSetGLES2TextureState(0);
    if (previous && previous != program && previous->delete_pending)
        destroy_program(previous);
}

GLboolean glIsProgram(GLuint name)
{
    return find_program(name) ? GL_TRUE : GL_FALSE;
}

#if MESAGL_GLES2_PROFILE == MESAGL_GLES2_PROFILE_LITE
static GLint conventional_attribute(const char *name)
{
    if (strstr(name, "Pos") || strstr(name, "pos") || strstr(name, "Position"))
        return 0;
    if (strstr(name, "UV") || strstr(name, "uv") || strstr(name, "TexCoord"))
        return 1;
    if (strstr(name, "Color") || strstr(name, "color") || strstr(name, "Colour"))
        return 2;
    return -1;
}
#endif

GLint glGetAttribLocation(GLuint name, const GLchar *attribute)
{
    Program *program = find_program(name);
    int i;

    if (!program) {
        program_name_error(name);
        return -1;
    }
    if (!program->linked) {
        mesaGLSetError(GL_INVALID_OPERATION);
        return -1;
    }
    if (!attribute)
        return -1;
    for (i = 0; i < program->binding_count; ++i) {
        Binding *binding = &program->bindings[i];

        if (!binding->active)
            continue;
        if (!strcmp(binding->name, attribute))
            return (GLint)binding->index;
    }
#if MESAGL_GLES2_PROFILE == MESAGL_GLES2_PROFILE_LITE
    return conventional_attribute(attribute);
#else
    return -1;
#endif
}

GLint glGetUniformLocation(GLuint name, const GLchar *uniform)
{
    Program *program = find_program(name);
    int i;

    if (!program) {
        program_name_error(name);
        return -1;
    }
    if (!program->linked) {
        mesaGLSetError(GL_INVALID_OPERATION);
        return -1;
    }
    if (!uniform)
        return -1;
    for (i = 0; i < program->uniform_count; ++i) {
        Uniform *candidate = &program->uniforms[i];
        size_t base_length = strlen(candidate->name);

        if (!strcmp(candidate->name, uniform))
            return candidate->location;
        {
            int aggregate_index = aggregate_uniform_index(candidate, uniform);

            if (aggregate_index >= 0)
                return candidate->location + aggregate_index;
        }
        if (!strncmp(candidate->name, uniform, base_length) && uniform[base_length] == '[') {
            char *index_end;
            long index = strtol(uniform + base_length + 1, &index_end, 10);

            if (*index_end == ']' && !index_end[1] && index >= 0 && index < candidate->size)
                return candidate->location + (GLint)index;
        }
    }
    return -1;
}

static int uniform_float_width(UniformType type)
{
    return type == UNIFORM_FLOAT ? 1
           : type == UNIFORM_VEC2 ? 2
           : type == UNIFORM_VEC3 ? 3
           : type == UNIFORM_VEC4 ? 4
                                  : 0;
}

static int uniform_integer_width(UniformType type)
{
    if (type == UNIFORM_INT || type == UNIFORM_BOOL || type == UNIFORM_SAMPLER2D ||
        type == UNIFORM_SAMPLERCUBE)
        return 1;
    if (type == UNIFORM_IVEC2 || type == UNIFORM_BVEC2)
        return 2;
    if (type == UNIFORM_IVEC3 || type == UNIFORM_BVEC3)
        return 3;
    if (type == UNIFORM_IVEC4 || type == UNIFORM_BVEC4)
        return 4;
    return 0;
}

static Uniform *validate_uniform_update(GLint location, GLsizei count, int width,
                                        int integer, UniformType matrix_type, const void *value)
{
    Program *program = find_program(current_program);
    Uniform *uniform;
    int first;
    int expected_width;

    if (!program || !program->executable) {
        mesaGLSetError(GL_INVALID_OPERATION);
        return NULL;
    }
    if (count < 0 || (count && !value)) {
        mesaGLSetError(GL_INVALID_VALUE);
        return NULL;
    }
    if (location == -1)
        return NULL;
    uniform = find_uniform(program, location);
    if (!uniform) {
        mesaGLSetError(GL_INVALID_OPERATION);
        return NULL;
    }
    first = location - uniform->location;
    if (count > uniform->size - first) {
        mesaGLSetError(GL_INVALID_OPERATION);
        return NULL;
    }
    expected_width = matrix_type ? (uniform->type == matrix_type ? width : 0)
                                 : integer ? uniform_integer_width(uniform->type)
                                           : uniform_float_width(uniform->type);
    if (expected_width != width) {
        mesaGLSetError(GL_INVALID_OPERATION);
        return NULL;
    }
    return uniform;
}

static int set_uniform_floats(GLint location, GLsizei count, int width,
                              UniformType matrix_type, const GLfloat *value)
{
    Uniform *uniform = validate_uniform_update(location, count, width, 0, matrix_type, value);
    int element;

    if (!uniform)
        return 0;
    for (element = 0; element < count; ++element)
        memcpy(uniform_float_value(uniform, location + element), value + element * width,
               (size_t)width * sizeof(float));
    return 1;
}

static int set_uniform_ints(GLint location, GLsizei count, int width, const GLint *value)
{
    Uniform *uniform = validate_uniform_update(location, count, width, 1, UNIFORM_OTHER, value);
    int element;

    if (!uniform)
        return 0;
    for (element = 0; element < count; ++element) {
        GLint *destination = uniform_integer_value(uniform, location + element);
        const GLint *source = value + element * width;
        int component;

        if (uniform->type == UNIFORM_BOOL || uniform->type == UNIFORM_BVEC2 ||
            uniform->type == UNIFORM_BVEC3 || uniform->type == UNIFORM_BVEC4) {
            for (component = 0; component < width; ++component)
                destination[component] = source[component] ? GL_TRUE : GL_FALSE;
        } else {
            memcpy(destination, source, (size_t)width * sizeof(GLint));
        }
    }
    return 1;
}

void glUniform1i(GLint location, GLint value)
{
    Program *program = find_program(current_program);
    Uniform *uniform = find_uniform(program, location);

    if (set_uniform_ints(location, 1, 1, &value) && uniform &&
        uniform->type == UNIFORM_SAMPLER2D)
        program->sampler = value;
}

void glUniform1f(GLint location, GLfloat value)
{
    set_uniform_floats(location, 1, 1, UNIFORM_OTHER, &value);
}

void glUniform2f(GLint location, GLfloat x, GLfloat y)
{
    const GLfloat value[2] = {x, y};

    set_uniform_floats(location, 1, 2, UNIFORM_OTHER, value);
}

void glUniform4f(GLint location, GLfloat x, GLfloat y, GLfloat z, GLfloat w)
{
    const GLfloat value[4] = {x, y, z, w};

    set_uniform_floats(location, 1, 4, UNIFORM_OTHER, value);
}

void glUniformMatrix4fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value)
{
    Program *program = find_program(current_program);

    if (!program || !program->executable) {
        mesaGLSetError(GL_INVALID_OPERATION);
        return;
    }
    if (transpose) {
        mesaGLSetError(GL_INVALID_VALUE);
        return;
    }
    if (set_uniform_floats(location, count, 16, UNIFORM_MAT4, value) && count > 0) {
        glMatrixMode(GL_PROJECTION);
        glLoadMatrixf(value);
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
    }
}

void glUniform1fv(GLint location, GLsizei count, const GLfloat *value)
{
    set_uniform_floats(location, count, 1, UNIFORM_OTHER, value);
}

void glUniform2fv(GLint location, GLsizei count, const GLfloat *value)
{
    set_uniform_floats(location, count, 2, UNIFORM_OTHER, value);
}

void glUniform3f(GLint location, GLfloat x, GLfloat y, GLfloat z)
{
    const GLfloat value[3] = {x, y, z};

    set_uniform_floats(location, 1, 3, UNIFORM_OTHER, value);
}

void glUniform3fv(GLint location, GLsizei count, const GLfloat *value)
{
    set_uniform_floats(location, count, 3, UNIFORM_OTHER, value);
}

void glUniform4fv(GLint location, GLsizei count, const GLfloat *value)
{
    set_uniform_floats(location, count, 4, UNIFORM_OTHER, value);
}

void glUniform2i(GLint location, GLint x, GLint y)
{
    const GLint value[2] = {x, y};

    set_uniform_ints(location, 1, 2, value);
}

void glUniform3i(GLint location, GLint x, GLint y, GLint z)
{
    const GLint value[3] = {x, y, z};

    set_uniform_ints(location, 1, 3, value);
}

void glUniform4i(GLint location, GLint x, GLint y, GLint z, GLint w)
{
    const GLint value[4] = {x, y, z, w};

    set_uniform_ints(location, 1, 4, value);
}

void glUniform1iv(GLint location, GLsizei count, const GLint *value)
{
    set_uniform_ints(location, count, 1, value);
}

void glUniform2iv(GLint location, GLsizei count, const GLint *value)
{
    set_uniform_ints(location, count, 2, value);
}

void glUniform3iv(GLint location, GLsizei count, const GLint *value)
{
    set_uniform_ints(location, count, 3, value);
}

void glUniform4iv(GLint location, GLsizei count, const GLint *value)
{
    set_uniform_ints(location, count, 4, value);
}

void glUniformMatrix2fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value)
{
    Program *program = find_program(current_program);

    if (!program || !program->executable) {
        mesaGLSetError(GL_INVALID_OPERATION);
        return;
    }
    if (transpose) {
        mesaGLSetError(GL_INVALID_VALUE);
        return;
    }
    set_uniform_floats(location, count, 4, UNIFORM_MAT2, value);
}

void glUniformMatrix3fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value)
{
    Program *program = find_program(current_program);

    if (!program || !program->executable) {
        mesaGLSetError(GL_INVALID_OPERATION);
        return;
    }
    if (transpose) {
        mesaGLSetError(GL_INVALID_VALUE);
        return;
    }
    set_uniform_floats(location, count, 9, UNIFORM_MAT3, value);
}

void glGetUniformfv(GLuint name, GLint location, GLfloat *params)
{
    Program *program = find_program(name);
    Uniform *uniform;
    float *storage;
    GLint *integer_storage;

    if (!program) {
        program_name_error(name);
        return;
    }
    if (!program->linked) {
        mesaGLSetError(GL_INVALID_OPERATION);
        return;
    }
    uniform = find_uniform(program, location);
    if (!uniform) {
        mesaGLSetError(GL_INVALID_OPERATION);
        return;
    }
    if (!params)
        return;
    storage = uniform_float_value(uniform, location);
    integer_storage = uniform_integer_value(uniform, location);

    if (storage && params) {
        int count = uniform_components(uniform->type);
        int i;

        if (uniform_is_integer(uniform->type))
            for (i = 0; i < count; ++i)
                params[i] = (float)integer_storage[i];
        else
            memcpy(params, storage, (size_t)count * sizeof(*params));
    }
}

void glGetUniformiv(GLuint name, GLint location, GLint *params)
{
    Program *program = find_program(name);
    Uniform *uniform;
    GLint *storage;
    float *float_storage;

    if (!program) {
        program_name_error(name);
        return;
    }
    if (!program->linked) {
        mesaGLSetError(GL_INVALID_OPERATION);
        return;
    }
    uniform = find_uniform(program, location);
    if (!uniform) {
        mesaGLSetError(GL_INVALID_OPERATION);
        return;
    }
    if (!params)
        return;
    storage = uniform_integer_value(uniform, location);
    float_storage = uniform_float_value(uniform, location);

    if (storage && params) {
        int count = uniform_components(uniform->type);
        int i;

        if (uniform_is_integer(uniform->type))
            memcpy(params, storage, (size_t)count * sizeof(*params));
        else
            for (i = 0; i < count; ++i)
                params[i] = (GLint)lroundf(float_storage[i]);
    }
}

void glGenBuffers(GLsizei n, GLuint *names)
{
    int i;
    int slot;
    if (n < 0 || (n && !names)) {
        mesaGLSetError(GL_INVALID_VALUE);
        return;
    }
    for (i = 0; i < n; ++i) {
        for (slot = 0; slot < MAX_BUFFERS && buffers[slot].name; ++slot) {
        }
        if (slot == MAX_BUFFERS) {
            names[i] = 0;
            mesaGLSetError(GL_OUT_OF_MEMORY);
            continue;
        }
        while (find_buffer(next_buffer))
            ++next_buffer;
        buffers[slot].name = next_buffer++;
        names[i] = buffers[slot].name;
    }
}

void glDeleteBuffers(GLsizei n, const GLuint *names)
{
    int i;

    if (n < 0 || (n && !names)) {
        mesaGLSetError(GL_INVALID_VALUE);
        return;
    }
    for (i = 0; i < n; ++i) {
        Buffer *buffer = find_buffer(names[i]);
        int attribute;

        if (!buffer)
            continue;
        ntglFree(buffer->data);
        memset(buffer, 0, sizeof(*buffer));
        if (array_buffer == names[i])
            array_buffer = 0;
        if (element_buffer == names[i])
            element_buffer = 0;
        for (attribute = 0; attribute < MAX_ATTRIBUTES; ++attribute)
            if (attributes[attribute].buffer == names[i])
                attributes[attribute].buffer = 0;
    }
}

void glBindBuffer(GLenum target, GLuint name)
{
    Buffer *buffer;

    if (target != GL_ARRAY_BUFFER && target != GL_ELEMENT_ARRAY_BUFFER) {
        mesaGLSetError(GL_INVALID_ENUM);
        return;
    }
    buffer = get_or_create_buffer(name);
    if (name && !buffer)
        return;
    if (buffer)
        buffer->created = 1;
    if (target == GL_ARRAY_BUFFER)
        array_buffer = name;
    else if (target == GL_ELEMENT_ARRAY_BUFFER)
        element_buffer = name;
}

static Buffer *bound_buffer(GLenum target)
{
    if (target == GL_ARRAY_BUFFER)
        return find_buffer(array_buffer);
    if (target == GL_ELEMENT_ARRAY_BUFFER)
        return find_buffer(element_buffer);
    return NULL;
}

void glBufferData(GLenum target, GLsizeiptr size, const void *data, GLenum usage)
{
    Buffer *buffer;
    unsigned char *storage;

    if (target != GL_ARRAY_BUFFER && target != GL_ELEMENT_ARRAY_BUFFER) {
        mesaGLSetError(GL_INVALID_ENUM);
        return;
    }
    buffer = bound_buffer(target);
    if (!buffer) {
        mesaGLSetError(GL_INVALID_OPERATION);
        return;
    }
    if (size < 0) {
        mesaGLSetError(GL_INVALID_VALUE);
        return;
    }
    if (usage != GL_STREAM_DRAW && usage != GL_STATIC_DRAW && usage != GL_DYNAMIC_DRAW) {
        mesaGLSetError(GL_INVALID_ENUM);
        return;
    }
    storage = size ? (unsigned char *)ntglAlloc((size_t)size) : NULL;
    if (size && !storage) {
        mesaGLSetError(GL_OUT_OF_MEMORY);
        return;
    }
    if (data && size)
        memcpy(storage, data, (size_t)size);
    else if (size)
        memset(storage, 0, (size_t)size);
    ntglFree(buffer->data);
    buffer->data = storage;
    buffer->size = (size_t)size;
    buffer->usage = usage;
}

void glBufferSubData(GLenum target, GLintptr offset, GLsizeiptr size, const void *data)
{
    Buffer *buffer;

    if (target != GL_ARRAY_BUFFER && target != GL_ELEMENT_ARRAY_BUFFER) {
        mesaGLSetError(GL_INVALID_ENUM);
        return;
    }
    buffer = bound_buffer(target);
    if (!buffer) {
        mesaGLSetError(GL_INVALID_OPERATION);
        return;
    }
    if (offset < 0 || size < 0 || (size && !data)) {
        mesaGLSetError(GL_INVALID_VALUE);
        return;
    }
    if ((size_t)offset > buffer->size || (size_t)size > buffer->size - (size_t)offset) {
        mesaGLSetError(GL_INVALID_VALUE);
        return;
    }
    if (size)
        memcpy(buffer->data + offset, data, (size_t)size);
}

void glEnableVertexAttribArray(GLuint index)
{
    if (index >= MAX_ATTRIBUTES) {
        mesaGLSetError(GL_INVALID_VALUE);
        return;
    }
    attributes[index].enabled = 1;
    if (index == 0)
        glEnableClientState(GL_VERTEX_ARRAY);
    else if (index == 1)
        glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    else if (index == 2)
        glEnableClientState(GL_COLOR_ARRAY);
}

void glDisableVertexAttribArray(GLuint index)
{
    if (index >= MAX_ATTRIBUTES) {
        mesaGLSetError(GL_INVALID_VALUE);
        return;
    }
    attributes[index].enabled = 0;
    if (index == 0)
        glDisableClientState(GL_VERTEX_ARRAY);
    else if (index == 1)
        glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    else if (index == 2)
        glDisableClientState(GL_COLOR_ARRAY);
}

void glVertexAttribPointer(GLuint index, GLint size, GLenum type, GLboolean normalized,
                           GLsizei stride, const void *pointer)
{
    Buffer *buffer = find_buffer(array_buffer);
    const unsigned char *resolved = (const unsigned char *)pointer;
    if (index >= MAX_ATTRIBUTES || size < 1 || size > 4 || stride < 0) {
        mesaGLSetError(GL_INVALID_VALUE);
        return;
    }
    if (type != GL_BYTE && type != GL_UNSIGNED_BYTE && type != GL_SHORT &&
        type != GL_UNSIGNED_SHORT && type != GL_FIXED && type != GL_FLOAT) {
        mesaGLSetError(GL_INVALID_ENUM);
        return;
    }
    if (buffer) {
        size_t offset = (size_t)pointer;

        resolved = buffer->data && offset <= buffer->size ? buffer->data + offset : NULL;
    }
    attributes[index].size = size;
    attributes[index].type = type;
    attributes[index].normalized = normalized;
    attributes[index].stride = stride;
    attributes[index].pointer = pointer;
    attributes[index].buffer = array_buffer;
    if (index == 0)
        glVertexPointer(size, type, stride, resolved);
    else if (index == 1)
        glTexCoordPointer(size, type, stride, resolved);
    else if (index == 2)
        glColorPointer(size, type, stride, resolved);
}

void mesaGLPrepareGLES2Draw(void)
{
    GLuint index;
    for (index = 0; index < 3; ++index) {
        AttribState *attribute = &attributes[index];
        Buffer *buffer;
        const unsigned char *pointer;
        size_t offset;
        if (!attribute->enabled)
            continue;
        buffer = find_buffer(attribute->buffer);
        if (!buffer)
            continue;
        offset = (size_t)attribute->pointer;
        if (offset >= buffer->size)
            continue;
        pointer = buffer->data + offset;
        if (index == 0)
            glVertexPointer(attribute->size, attribute->type, attribute->stride, pointer);
        else if (index == 1)
            glTexCoordPointer(attribute->size, attribute->type, attribute->stride, pointer);
        else
            glColorPointer(attribute->size, attribute->type, attribute->stride, pointer);
    }
}

void glActiveTexture(GLenum texture)
{
    int unit = (int)(texture - GL_TEXTURE0);

    if (texture < GL_TEXTURE0 || unit >= MESAGL_MAX_TEXTURE_UNITS ||
        !mesaGLSetActiveTextureUnit(unit)) {
        mesaGLSetError(GL_INVALID_ENUM);
        return;
    }
    active_texture = texture;
}

void glGetVertexAttribiv(GLuint index, GLenum pname, GLint *params)
{
    AttribState *attribute;
    if (index >= MAX_ATTRIBUTES) {
        mesaGLSetError(GL_INVALID_VALUE);
        return;
    }
    if (!params)
        return;
    attribute = &attributes[index];
    if (pname == GL_CURRENT_VERTEX_ATTRIB) {
        int component;

        for (component = 0; component < 4; ++component)
            params[component] = (GLint)lroundf(attribute->current[component]);
    } else if (pname == GL_VERTEX_ATTRIB_ARRAY_ENABLED)
        *params = attribute->enabled;
    else if (pname == GL_VERTEX_ATTRIB_ARRAY_SIZE)
        *params = attribute->size;
    else if (pname == GL_VERTEX_ATTRIB_ARRAY_TYPE)
        *params = (GLint)attribute->type;
    else if (pname == GL_VERTEX_ATTRIB_ARRAY_NORMALIZED)
        *params = attribute->normalized;
    else if (pname == GL_VERTEX_ATTRIB_ARRAY_STRIDE)
        *params = attribute->stride;
    else if (pname == GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING)
        *params = (GLint)attribute->buffer;
    else
        mesaGLSetError(GL_INVALID_ENUM);
}

void glGetVertexAttribPointerv(GLuint index, GLenum pname, void **pointer)
{
    if (index >= MAX_ATTRIBUTES) {
        mesaGLSetError(GL_INVALID_VALUE);
        return;
    }
    if (pname != GL_VERTEX_ATTRIB_ARRAY_POINTER) {
        mesaGLSetError(GL_INVALID_ENUM);
        return;
    }
    if (!pointer)
        return;
    *pointer = (void *)attributes[index].pointer;
}

void glGetVertexAttribfv(GLuint index, GLenum pname, GLfloat *params)
{
    GLint integer = 0;
    int component;

    if (index >= MAX_ATTRIBUTES) {
        mesaGLSetError(GL_INVALID_VALUE);
        return;
    }
    if (!params)
        return;
    if (pname == GL_CURRENT_VERTEX_ATTRIB) {
        memcpy(params, attributes[index].current, sizeof(attributes[index].current));
        return;
    }
    if (pname != GL_VERTEX_ATTRIB_ARRAY_ENABLED && pname != GL_VERTEX_ATTRIB_ARRAY_SIZE &&
        pname != GL_VERTEX_ATTRIB_ARRAY_TYPE && pname != GL_VERTEX_ATTRIB_ARRAY_NORMALIZED &&
        pname != GL_VERTEX_ATTRIB_ARRAY_STRIDE &&
        pname != GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING) {
        mesaGLSetError(GL_INVALID_ENUM);
        return;
    }
    glGetVertexAttribiv(index, pname, &integer);
    for (component = 0; component < (pname == GL_CURRENT_VERTEX_ATTRIB ? 4 : 1); ++component)
        params[component] = (GLfloat)integer;
}

static void vertex_attrib(GLuint index, int count, const GLfloat *values)
{
    int component;

    if (index >= MAX_ATTRIBUTES) {
        mesaGLSetError(GL_INVALID_VALUE);
        return;
    }
    if (!values)
        return;
    attributes[index].current[0] = 0.0f;
    attributes[index].current[1] = 0.0f;
    attributes[index].current[2] = 0.0f;
    attributes[index].current[3] = 1.0f;
    for (component = 0; component < count; ++component)
        attributes[index].current[component] = values[component];
}

void glVertexAttrib1f(GLuint index, GLfloat x)
{
    vertex_attrib(index, 1, &x);
}

void glVertexAttrib1fv(GLuint index, const GLfloat *values)
{
    vertex_attrib(index, 1, values);
}

void glVertexAttrib2f(GLuint index, GLfloat x, GLfloat y)
{
    const GLfloat values[2] = {x, y};

    vertex_attrib(index, 2, values);
}

void glVertexAttrib2fv(GLuint index, const GLfloat *values)
{
    vertex_attrib(index, 2, values);
}

void glVertexAttrib3f(GLuint index, GLfloat x, GLfloat y, GLfloat z)
{
    const GLfloat values[3] = {x, y, z};

    vertex_attrib(index, 3, values);
}

void glVertexAttrib3fv(GLuint index, const GLfloat *values)
{
    vertex_attrib(index, 3, values);
}

void glVertexAttrib4f(GLuint index, GLfloat x, GLfloat y, GLfloat z, GLfloat w)
{
    const GLfloat values[4] = {x, y, z, w};

    vertex_attrib(index, 4, values);
}

void glVertexAttrib4fv(GLuint index, const GLfloat *values)
{
    vertex_attrib(index, 4, values);
}

void glBlendEquation(GLenum mode)
{
    glBlendEquationSeparate(mode, mode);
}

void glBlendEquationSeparate(GLenum mode_rgb, GLenum mode_alpha)
{
    if ((mode_rgb != GL_FUNC_ADD && mode_rgb != GL_FUNC_SUBTRACT &&
         mode_rgb != GL_FUNC_REVERSE_SUBTRACT) ||
        (mode_alpha != GL_FUNC_ADD && mode_alpha != GL_FUNC_SUBTRACT &&
         mode_alpha != GL_FUNC_REVERSE_SUBTRACT)) {
        mesaGLSetError(GL_INVALID_ENUM);
        return;
    }
    NTGLblendEquation rgb = mode_rgb == GL_FUNC_SUBTRACT           ? NTGL_FUNC_SUBTRACT
                            : mode_rgb == GL_FUNC_REVERSE_SUBTRACT ? NTGL_FUNC_REVERSE_SUBTRACT
                                                                   : NTGL_FUNC_ADD;
    NTGLblendEquation alpha = mode_alpha == GL_FUNC_SUBTRACT           ? NTGL_FUNC_SUBTRACT
                              : mode_alpha == GL_FUNC_REVERSE_SUBTRACT ? NTGL_FUNC_REVERSE_SUBTRACT
                                                                       : NTGL_FUNC_ADD;
    mesaGLSetBlendEquationState(mode_rgb, mode_alpha);
    ntglBlendEquationSeparate(rgb, alpha);
}

GLboolean glIsBuffer(GLuint name)
{
    Buffer *buffer = find_buffer(name);

    return buffer && buffer->created ? GL_TRUE : GL_FALSE;
}

void glGetBufferParameteriv(GLenum target, GLenum pname, GLint *params)
{
    Buffer *buffer;

    if (!params)
        return;
    if (target != GL_ARRAY_BUFFER && target != GL_ELEMENT_ARRAY_BUFFER) {
        mesaGLSetError(GL_INVALID_ENUM);
        return;
    }
    if (pname != GL_BUFFER_SIZE && pname != GL_BUFFER_USAGE) {
        mesaGLSetError(GL_INVALID_ENUM);
        return;
    }
    buffer = bound_buffer(target);
    if (!buffer) {
        mesaGLSetError(GL_INVALID_OPERATION);
        return;
    }
    if (pname == GL_BUFFER_SIZE)
        *params = (GLint)buffer->size;
    else
        *params = (GLint)buffer->usage;
}

const void *mesaGLResolveElementPointer(const void *pointer)
{
    Buffer *buffer = find_buffer(element_buffer);
    size_t offset;
    if (!buffer)
        return pointer;
    offset = (size_t)pointer;
    if (offset >= buffer->size)
        return NULL;
    return buffer->data + offset;
}

static int attribute_type_size(GLenum type)
{
    switch (type) {
    case GL_BYTE:
    case GL_UNSIGNED_BYTE:
        return 1;
    case GL_SHORT:
    case GL_UNSIGNED_SHORT:
        return 2;
    case GL_FIXED:
    case GL_FLOAT:
        return 4;
    default:
        return 0;
    }
}

static int validate_attribute_bounds(GLuint maximum_vertex)
{
    int index;

    for (index = 0; index < MAX_ATTRIBUTES; ++index) {
        AttribState *attribute = &attributes[index];
        Buffer *buffer;
        size_t element_size;
        size_t offset;
        size_t stride;
        size_t required;
        int type_size;

        if (!attribute->enabled)
            continue;
        if (!attribute->buffer) {
            if (!attribute->pointer) {
                mesaGLSetError(GL_INVALID_OPERATION);
                return 0;
            }
            continue;
        }
        buffer = find_buffer(attribute->buffer);
        type_size = attribute_type_size(attribute->type);
        if (!buffer || !type_size || attribute->size < 1 || attribute->size > 4 ||
            attribute->stride < 0) {
            mesaGLSetError(GL_INVALID_OPERATION);
            return 0;
        }
        offset = (size_t)attribute->pointer;
        element_size = (size_t)attribute->size * (size_t)type_size;
        stride = attribute->stride ? (size_t)attribute->stride : element_size;
        if (maximum_vertex && stride > (SIZE_MAX - element_size) / maximum_vertex) {
            mesaGLSetError(GL_INVALID_OPERATION);
            return 0;
        }
        required = (size_t)maximum_vertex * stride + element_size;
        if (offset > buffer->size || required > buffer->size - offset) {
            mesaGLSetError(GL_INVALID_OPERATION);
            return 0;
        }
    }
    return 1;
}

static GLuint maximum_index(GLenum type, const void *indices, GLsizei count)
{
    GLuint maximum = 0;
    int i;

    for (i = 0; i < count; ++i) {
        GLuint value = type == GL_UNSIGNED_BYTE    ? ((const GLubyte *)indices)[i]
                       : type == GL_UNSIGNED_SHORT ? ((const GLushort *)indices)[i]
                                                   : ((const GLuint *)indices)[i];

        if (value > maximum)
            maximum = value;
    }
    return maximum;
}

#if MESAGL_GLES2_PROFILE == MESAGL_GLES2_PROFILE_FULL
typedef struct ShaderExecution {
    Program *program;
    float inputs[MAX_ATTRIBUTES][4];
    const float *varyings;
    const float *varying_dfdx;
    const float *varying_dfdy;
    int varying_count;
    float frag_coord[4];
    int front_facing;
    float point_coord[2];
    NTGLprogramVertex *vertex_output;
    float *fragment_output;
    MesaGLSLValue *uniform_array;
    char (*uniform_member_names)[MESAGL_MAX_SHADER_LVALUE_PATH_LENGTH];
    MesaGLSLValue *uniform_struct_members;
    MesaGLSLValue *uniform_struct_array;
    MesaGLSLValue *varying_array;
    char depth_range_names[3][MESAGL_MAX_SHADER_LVALUE_PATH_LENGTH];
    MesaGLSLValue depth_range_members[3];
    MesaGLSLValue fragment_data[1];
    MesaGLPreparedTexture2D imgui_sampler;
    int imgui_sampler_ready;
    int imgui_texture_unit;
    int imgui_sample_cached;
    float imgui_cached_uv[2];
    float imgui_cached_texel[4];
} ShaderExecution;

static void load_uniform_value(Uniform *uniform, GLint location, MesaGLSLValue *item)
{
    float *float_storage = uniform_float_value(uniform, location);
    GLint *integer_storage = uniform_integer_value(uniform, location);

    memset(item, 0, sizeof(*item));
    if (uniform_is_integer(uniform->type)) {
        int component_count = uniform_components(uniform->type);
        int component_index;

        for (component_index = 0; component_index < component_count; ++component_index)
            item->data[component_index] = (float)integer_storage[component_index];
    } else
        memcpy(item->data, float_storage, sizeof(item->data));
    item->columns = uniform->type == UNIFORM_MAT2   ? 2
                    : uniform->type == UNIFORM_MAT3 ? 3
                    : uniform->type == UNIFORM_MAT4 ? 4
                                                   : 1;
    item->rows = uniform->type == UNIFORM_VEC2   ? 2
                 : uniform->type == UNIFORM_IVEC2 || uniform->type == UNIFORM_BVEC2
                     ? 2
                 : uniform->type == UNIFORM_VEC3 || uniform->type == UNIFORM_IVEC3 ||
                       uniform->type == UNIFORM_BVEC3
                     ? 3
                 : uniform->type == UNIFORM_VEC4 || uniform->type == UNIFORM_IVEC4 ||
                       uniform->type == UNIFORM_BVEC4
                     ? 4
                 : item->columns > 1 ? item->columns
                                     : 1;
    item->type = uniform->type == UNIFORM_SAMPLER2D
                     ? MESAGL_GLSL_TYPE_SAMPLER2D
                 : uniform->type == UNIFORM_SAMPLERCUBE
                     ? MESAGL_GLSL_TYPE_SAMPLERCUBE
                 : uniform->type == UNIFORM_BOOL || uniform->type == UNIFORM_BVEC2 ||
                           uniform->type == UNIFORM_BVEC3 || uniform->type == UNIFORM_BVEC4
                     ? MESAGL_GLSL_TYPE_BOOL
                 : uniform_is_integer(uniform->type) ? MESAGL_GLSL_TYPE_INT
                                                    : MESAGL_GLSL_TYPE_FLOAT;
}

static int indexed_struct_member_name(
    char destination[MESAGL_MAX_SHADER_LVALUE_PATH_LENGTH],
    const char *source, int index)
{
    const char *zero = NULL;
    const char *next;
    const char *cursor = source;
    int length;

    while ((next = strstr(cursor, "[0].")) != NULL) {
        zero = next;
        cursor = next + 1;
    }
    if (!zero)
        return 0;
    length = snprintf(destination, MESAGL_MAX_SHADER_LVALUE_PATH_LENGTH, "%.*s[%d]%s", (int)(zero - source), source,
                      index, zero + 3);
    return length >= 0 && length < MESAGL_MAX_SHADER_LVALUE_PATH_LENGTH;
}

static int execution_lookup(void *user, const char *name, size_t length, MesaGLSLValue *value)
{
    ShaderExecution *execution = (ShaderExecution *)user;
    Program *program = execution->program;
    int i;

    memset(value, 0, sizeof(*value));
    if (length == 12 && !strncmp(name, "gl_FragCoord", length)) {
        memcpy(value->data, execution->frag_coord, sizeof(execution->frag_coord));
        value->rows = 4;
        value->columns = 1;
        return 1;
    }
    if (length == 14 && !strncmp(name, "gl_FrontFacing", length)) {
        value->data[0] = execution->front_facing != 0;
        value->rows = 1;
        value->columns = 1;
        value->type = MESAGL_GLSL_TYPE_BOOL;
        return 1;
    }
    if (length == 13 && !strncmp(name, "gl_PointCoord", length)) {
        memcpy(value->data, execution->point_coord, sizeof(execution->point_coord));
        value->rows = 2;
        value->columns = 1;
        value->type = MESAGL_GLSL_TYPE_FLOAT;
        return 1;
    }
    if (length == 13 && !strncmp(name, "gl_DepthRange", length)) {
        GLfloat range[2];

        glGetFloatv(GL_DEPTH_RANGE, range);
        memset(execution->depth_range_names, 0,
               sizeof(execution->depth_range_names));
        strcpy(execution->depth_range_names[0], "near");
        strcpy(execution->depth_range_names[1], "far");
        strcpy(execution->depth_range_names[2], "diff");
        memset(execution->depth_range_members, 0,
               sizeof(execution->depth_range_members));
        execution->depth_range_members[0].data[0] = range[0];
        execution->depth_range_members[1].data[0] = range[1];
        execution->depth_range_members[2].data[0] = range[1] - range[0];
        for (i = 0; i < 3; ++i) {
            execution->depth_range_members[i].rows = 1;
            execution->depth_range_members[i].columns = 1;
            execution->depth_range_members[i].type = MESAGL_GLSL_TYPE_FLOAT;
        }
        value->rows = 1;
        value->columns = 1;
        value->type = MESAGL_GLSL_TYPE_STRUCT;
        value->member_names = execution->depth_range_names;
        value->members = execution->depth_range_members;
        value->member_count = 3;
        value->struct_type_name = "gl_DepthRangeParameters";
        value->struct_type_length = strlen(value->struct_type_name);
        return 1;
    }
    if (length == 11 && !strncmp(name, "gl_FragData", length) &&
        execution->fragment_output) {
        memset(execution->fragment_data, 0, sizeof(execution->fragment_data));
        memcpy(execution->fragment_data[0].data, execution->fragment_output,
               4 * sizeof(float));
        execution->fragment_data[0].rows = 4;
        execution->fragment_data[0].columns = 1;
        execution->fragment_data[0].type = MESAGL_GLSL_TYPE_FLOAT;
        value->rows = 4;
        value->columns = 1;
        value->type = MESAGL_GLSL_TYPE_FLOAT;
        value->array = execution->fragment_data;
        value->array_size = 1;
        return 1;
    }
    for (i = 0; i < program->uniform_count; ++i) {
        Uniform *uniform = &program->uniforms[i];
        int element;

        if (strlen(uniform->name) != length || strncmp(uniform->name, name, length))
            continue;
        for (element = 0; element < uniform->size; ++element) {
            MesaGLSLValue *item =
                &execution->uniform_array[uniform->location - 1 + element];

            load_uniform_value(uniform, uniform->location + element, item);
        }
        *value = execution->uniform_array[uniform->location - 1];
        if (uniform->array_declared) {
            value->array = &execution->uniform_array[uniform->location - 1];
            value->array_size = uniform->size;
        }
        return 1;
    }
    for (i = 0; i < program->uniform_count; ++i) {
        Uniform *uniform = &program->uniforms[i];
        int member_count = 0;
        int member;

        if (!uniform->aggregate_name[0] || strlen(uniform->aggregate_name) != length ||
            strncmp(uniform->aggregate_name, name, length))
            continue;
        for (member = 0; member < program->uniform_count; ++member)
            if (!strcmp(program->uniforms[member].aggregate_name,
                        uniform->aggregate_name) &&
                program->uniforms[member].aggregate_element <= 0)
                member_count += strstr(program->uniforms[member].member_name, "[0].")
                                    ? program->uniforms[member].member_size
                                    : 1;
        if (uniform->aggregate_size > 1) {
            int element;

            for (element = 0; element < uniform->aggregate_size; ++element) {
                int member_index = 0;

                for (member = 0; member < program->uniform_count; ++member) {
                    Uniform *candidate = &program->uniforms[member];
                    int storage_index;
                    int array_element;

                    if (strcmp(candidate->aggregate_name, uniform->aggregate_name) ||
                        (candidate->aggregate_element >= 0 &&
                         candidate->aggregate_element != element))
                        continue;
                    storage_index = element * member_count + member_index;
                    if (strstr(candidate->member_name, "[0].")) {
                        for (array_element = 0; array_element < candidate->member_size;
                             ++array_element) {
                            int nested_index = storage_index + array_element;

                            if (!indexed_struct_member_name(
                                    execution->uniform_member_names[nested_index],
                                    candidate->member_name, array_element))
                                return 0;
                            load_uniform_value(candidate,
                                               candidate->location + array_element,
                                               &execution->uniform_struct_members[nested_index]);
                        }
                        member_index += candidate->member_size;
                        continue;
                    }
                    strcpy(execution->uniform_member_names[storage_index],
                           candidate->member_name);
                    if (candidate->member_size > 1) {
                        for (array_element = 0; array_element < candidate->member_size;
                             ++array_element)
                            load_uniform_value(
                                candidate, candidate->location + array_element,
                                &execution->uniform_array[candidate->location - 1 +
                                                          array_element]);
                        execution->uniform_struct_members[storage_index] =
                            execution->uniform_array[candidate->location - 1];
                        execution->uniform_struct_members[storage_index].array =
                            &execution->uniform_array[candidate->location - 1];
                        execution->uniform_struct_members[storage_index].array_size =
                            candidate->member_size;
                    } else
                        load_uniform_value(candidate, candidate->location + element,
                                           &execution->uniform_struct_members[storage_index]);
                    ++member_index;
                }
                memset(&execution->uniform_struct_array[element], 0,
                       sizeof(execution->uniform_struct_array[element]));
                execution->uniform_struct_array[element].rows = 1;
                execution->uniform_struct_array[element].columns = 1;
                execution->uniform_struct_array[element].type = MESAGL_GLSL_TYPE_STRUCT;
                execution->uniform_struct_array[element].member_names =
                    execution->uniform_member_names + element * member_count;
                execution->uniform_struct_array[element].members =
                    execution->uniform_struct_members + element * member_count;
                execution->uniform_struct_array[element].member_count = member_count;
                execution->uniform_struct_array[element].struct_type_name =
                    uniform->aggregate_type;
                execution->uniform_struct_array[element].struct_type_length =
                    strlen(uniform->aggregate_type);
            }
            *value = execution->uniform_struct_array[0];
            value->array = execution->uniform_struct_array;
            value->array_size = uniform->aggregate_size;
        } else {
            int member_index = 0;

            for (member = 0; member < program->uniform_count; ++member) {
                Uniform *candidate = &program->uniforms[member];
                int element;

                if (strcmp(candidate->aggregate_name, uniform->aggregate_name))
                    continue;
                if (strstr(candidate->member_name, "[0].")) {
                    for (element = 0; element < candidate->member_size; ++element) {
                        if (!indexed_struct_member_name(
                                execution->uniform_member_names[member_index],
                                candidate->member_name, element))
                            return 0;
                        load_uniform_value(candidate, candidate->location + element,
                                           &execution->uniform_struct_members[member_index]);
                        ++member_index;
                    }
                    continue;
                }
                strcpy(execution->uniform_member_names[member_index],
                       candidate->member_name);
                for (element = 0; element < candidate->member_size; ++element)
                    load_uniform_value(candidate, candidate->location + element,
                                       &execution->uniform_array[candidate->location - 1 +
                                                                 element]);
                execution->uniform_struct_members[member_index] =
                    execution->uniform_array[candidate->location - 1];
                if (candidate->member_size > 1) {
                    execution->uniform_struct_members[member_index].array =
                        &execution->uniform_array[candidate->location - 1];
                    execution->uniform_struct_members[member_index].array_size =
                        candidate->member_size;
                }
                ++member_index;
            }
            value->rows = 1;
            value->columns = 1;
            value->type = MESAGL_GLSL_TYPE_STRUCT;
            value->member_names = execution->uniform_member_names;
            value->members = execution->uniform_struct_members;
            value->member_count = member_count;
            value->struct_type_name = uniform->aggregate_type;
            value->struct_type_length = strlen(uniform->aggregate_type);
        }
        return 1;
    }
    for (i = 0; i < program->binding_count; ++i) {
        Binding *binding = &program->bindings[i];

        if (binding->active && strlen(binding->name) == length &&
            !strncmp(binding->name, name, length)) {
            int columns = attribute_slots(binding->type);
            int rows = shader_type_rows(binding->type);
            int column;

            for (column = 0; column < columns; ++column)
                memcpy(value->data + column * rows,
                       execution->inputs[binding->index + (GLuint)column],
                       (size_t)rows * sizeof(float));
            value->rows = rows;
            value->columns = columns;
            value->type = MESAGL_GLSL_TYPE_FLOAT;
            return 1;
        }
    }
    for (i = 0; i < program->varying_count; ++i) {
        Varying *varying = &program->varyings[i];
        int element;

        if (varying->active && strlen(varying->name) == length &&
            !strncmp(varying->name, name, length) &&
            (execution->varyings || execution->vertex_output)) {
            int columns = varying_slots(varying->type);
            int rows = shader_type_rows(varying->type);

            for (element = 0; element < varying->size; ++element) {
                MesaGLSLValue *item = &execution->varying_array[varying->slot + element];
                int slot = varying->slot + element * columns;
                int column;

                memset(item, 0, sizeof(*item));
                for (column = 0; column < columns; ++column) {
                    const float *source = execution->varyings
                                              ? execution->varyings +
                                                    (slot + column) * 4
                                              : execution->vertex_output
                                                    ->varying[slot + column];

                    memcpy(item->data + column * rows, source,
                           (size_t)rows * sizeof(float));
                    if (execution->varyings && execution->varying_dfdx &&
                        execution->varying_dfdy) {
                        memcpy(item->dfdx + column * rows,
                               execution->varying_dfdx + (slot + column) * 4,
                               (size_t)rows * sizeof(float));
                        memcpy(item->dfdy + column * rows,
                               execution->varying_dfdy + (slot + column) * 4,
                               (size_t)rows * sizeof(float));
                        item->has_derivatives = 1;
                    }
                }
                item->rows = rows;
                item->columns = columns;
                item->type = MESAGL_GLSL_TYPE_FLOAT;
            }
            *value = execution->varying_array[varying->slot];
            if (varying->size > 1) {
                value->array = &execution->varying_array[varying->slot];
                value->array_size = varying->size;
            }
            return 1;
        }
    }
    return 0;
}

static int sample_texture_2d(ShaderExecution *execution, int unit,
                             const MesaGLSLValue *coordinate, float lod_or_bias,
                             int explicit_lod, MesaGLSLValue *value)
{
    int varying;
    int sampled = 0;

    if (coordinate->rows < 2)
        return 0;
    if (explicit_lod)
        sampled = mesaGLSampleTexture2DLod(unit, coordinate->data[0], coordinate->data[1],
                                           lod_or_bias, value->data);
    else if (coordinate->has_derivatives)
        sampled = mesaGLSampleTexture2DGradBias(
            unit, coordinate->data[0], coordinate->data[1], coordinate->dfdx[0],
            coordinate->dfdx[1], coordinate->dfdy[0], coordinate->dfdy[1], lod_or_bias,
            value->data);

    for (varying = 0; !explicit_lod && !sampled && varying < execution->varying_count;
         ++varying) {
        const float *source = execution->varyings + varying * 4;

        if (fabsf(source[0] - coordinate->data[0]) < 1.0e-6f &&
            fabsf(source[1] - coordinate->data[1]) < 1.0e-6f && execution->varying_dfdx &&
            execution->varying_dfdy) {
            const float *dfdx = execution->varying_dfdx + varying * 4;
            const float *dfdy = execution->varying_dfdy + varying * 4;

            sampled = mesaGLSampleTexture2DGradBias(
                unit, coordinate->data[0], coordinate->data[1], dfdx[0], dfdx[1], dfdy[0],
                dfdy[1], lod_or_bias, value->data);
            break;
        }
    }
    if (!sampled)
        sampled = lod_or_bias != 0.0f || explicit_lod
                      ? mesaGLSampleTexture2DLod(unit, coordinate->data[0], coordinate->data[1],
                                                lod_or_bias, value->data)
                      : mesaGLSampleTexture2D(unit, coordinate->data[0], coordinate->data[1],
                                              value->data);
    if (!sampled)
        return 0;
    value->rows = 4;
    value->columns = 1;
    value->type = MESAGL_GLSL_TYPE_FLOAT;
    return 1;
}

static int project_texture_coordinate(const MesaGLSLValue *source,
                                      MesaGLSLValue *coordinate)
{
    int divisor_index;
    float divisor;
    int component;

    if ((source->rows != 3 && source->rows != 4) || source->columns != 1)
        return 0;
    divisor_index = source->rows - 1;
    divisor = source->data[divisor_index];
    if (fabsf(divisor) < 1.0e-20f)
        return 0;
    memset(coordinate, 0, sizeof(*coordinate));
    coordinate->rows = 2;
    coordinate->columns = 1;
    coordinate->type = MESAGL_GLSL_TYPE_FLOAT;
    coordinate->has_derivatives = source->has_derivatives;
    for (component = 0; component < 2; ++component) {
        coordinate->data[component] = source->data[component] / divisor;
        coordinate->dfdx[component] =
            (source->dfdx[component] * divisor -
             source->data[component] * source->dfdx[divisor_index]) /
            (divisor * divisor);
        coordinate->dfdy[component] =
            (source->dfdy[component] * divisor -
             source->data[component] * source->dfdy[divisor_index]) /
            (divisor * divisor);
    }
    return 1;
}

static int execution_call(void *user, const char *name, size_t length,
                          const MesaGLSLValue *arguments, int argument_count,
                          MesaGLSLValue *value)
{
    ShaderExecution *execution = (ShaderExecution *)user;
    int varying;

    if (length == 9 && !strncmp(name, "texture2D", length) &&
        (argument_count == 2 || argument_count == 3) &&
        arguments[1].rows >= 2)
        return sample_texture_2d(execution, (int)arguments[0].data[0], &arguments[1],
                                 argument_count == 3 ? arguments[2].data[0] : 0.0f, 0, value);
    if (length == 12 && !strncmp(name, "texture2DLod", length) && argument_count == 3 &&
        arguments[1].rows >= 2)
        return sample_texture_2d(execution, (int)arguments[0].data[0], &arguments[1],
                                 arguments[2].data[0], 1, value);
    if ((length == 13 && !strncmp(name, "texture2DProj", length) &&
         (argument_count == 2 || argument_count == 3)) ||
        (length == 16 && !strncmp(name, "texture2DProjLod", length) &&
         argument_count == 3)) {
        MesaGLSLValue coordinate;

        if (!project_texture_coordinate(&arguments[1], &coordinate))
            return 0;
        return sample_texture_2d(execution, (int)arguments[0].data[0], &coordinate,
                                 argument_count == 3 ? arguments[2].data[0] : 0.0f,
                                 length == 16, value);
    }
    if (length == 11 && !strncmp(name, "textureCube", length) &&
        (argument_count == 2 || argument_count == 3) && arguments[1].rows >= 3) {
        int sampled;
        float bias = argument_count == 3 ? arguments[2].data[0] : 0.0f;

        if (arguments[1].has_derivatives)
            sampled = mesaGLSampleTextureCubeGradBias(
                (int)arguments[0].data[0], arguments[1].data[0], arguments[1].data[1],
                arguments[1].data[2], arguments[1].dfdx[0], arguments[1].dfdx[1],
                arguments[1].dfdx[2], arguments[1].dfdy[0], arguments[1].dfdy[1],
                arguments[1].dfdy[2], bias, value->data);
        else
            sampled = mesaGLSampleTextureCubeLod(
                (int)arguments[0].data[0], arguments[1].data[0], arguments[1].data[1],
                arguments[1].data[2], bias, value->data);
        if (!sampled)
            return 0;
        value->rows = 4;
        value->columns = 1;
        value->type = MESAGL_GLSL_TYPE_FLOAT;
        return 1;
    }
    if (length == 14 && !strncmp(name, "textureCubeLod", length) && argument_count == 3 &&
        arguments[1].rows >= 3 &&
        mesaGLSampleTextureCubeLod((int)arguments[0].data[0], arguments[1].data[0],
                                   arguments[1].data[1], arguments[1].data[2],
                                   arguments[2].data[0], value->data)) {
        value->rows = 4;
        value->columns = 1;
        value->type = MESAGL_GLSL_TYPE_FLOAT;
        return 1;
    }
    if (argument_count == 1 &&
        ((length == 4 && !strncmp(name, "dFdx", length)) ||
         (length == 4 && !strncmp(name, "dFdy", length)) ||
         (length == 6 && !strncmp(name, "fwidth", length)))) {
        if (arguments[0].has_derivatives) {
            int component;

            *value = arguments[0];
            for (component = 0; component < arguments[0].rows; ++component)
                value->data[component] =
                    length == 4 && name[3] == 'x'
                        ? arguments[0].dfdx[component]
                    : length == 4 ? arguments[0].dfdy[component]
                                  : fabsf(arguments[0].dfdx[component]) +
                                        fabsf(arguments[0].dfdy[component]);
            value->has_derivatives = 0;
            memset(value->dfdx, 0, sizeof(value->dfdx));
            memset(value->dfdy, 0, sizeof(value->dfdy));
            return 1;
        }
        for (varying = 0; varying < execution->varying_count; ++varying) {
            const float *source = execution->varyings + varying * 4;
            int component;
            int matches = 1;

            for (component = 0; component < arguments[0].rows; ++component)
                if (fabsf(source[component] - arguments[0].data[component]) >= 1.0e-6f)
                    matches = 0;
            if (matches && execution->varying_dfdx && execution->varying_dfdy) {
                const float *dfdx = execution->varying_dfdx + varying * 4;
                const float *dfdy = execution->varying_dfdy + varying * 4;

                *value = arguments[0];
                for (component = 0; component < arguments[0].rows; ++component)
                    value->data[component] =
                        length == 4 && name[3] == 'x' ? dfdx[component]
                        : length == 4                ? dfdy[component]
                                                     : fabsf(dfdx[component]) +
                                                           fabsf(dfdy[component]);
                return 1;
            }
        }
    }
    return 0;
}

static int execution_assign(void *user, const char *name, size_t length, const char *swizzle,
                            size_t swizzle_length, int array_index,
                            const MesaGLSLValue *value)
{
    ShaderExecution *execution = (ShaderExecution *)user;
    int i;
    MesaGLSLType value_type = value->type == MESAGL_GLSL_TYPE_UNKNOWN
                                    ? MESAGL_GLSL_TYPE_FLOAT
                                    : value->type;

    if (length == 11 && !strncmp(name, "gl_Position", length) && execution->vertex_output &&
        !swizzle_length && array_index < 0 && value_type == MESAGL_GLSL_TYPE_FLOAT &&
        value->rows == 4 && value->columns == 1) {
        memcpy(execution->vertex_output->position, value->data, 4 * sizeof(float));
        return 1;
    }
    if (length == 12 && !strncmp(name, "gl_PointSize", length) && execution->vertex_output &&
        !swizzle_length && array_index < 0 && value_type == MESAGL_GLSL_TYPE_FLOAT &&
        value->rows == 1 && value->columns == 1) {
        execution->vertex_output->point_size = value->data[0];
        return 1;
    }
    if (execution->fragment_output &&
        ((length == 12 && !strncmp(name, "gl_FragColor", length) &&
          array_index < 0) ||
         (length == 11 && !strncmp(name, "gl_FragData", length) &&
          array_index == 0))) {
        if (value_type != MESAGL_GLSL_TYPE_FLOAT || value->columns != 1 ||
            value->rows != (swizzle_length ? (int)swizzle_length : 4))
            return 0;
        if (!swizzle_length) {
            memcpy(execution->fragment_output, value->data, 4 * sizeof(float));
        } else {
            for (i = 0; i < (int)swizzle_length; ++i) {
                static const char rgba[] = "rgba";
                static const char xyzw[] = "xyzw";
                const char *channel = strchr(rgba, swizzle[i]);
                int destination;
                int previous;

                if (channel)
                    destination = (int)(channel - rgba);
                else {
                    channel = strchr(xyzw, swizzle[i]);
                    destination = channel ? (int)(channel - xyzw) : -1;
                }
                if (!channel)
                    return 0;
                for (previous = 0; previous < i; ++previous) {
                    const char *previous_channel = strchr(rgba, swizzle[previous]);
                    int previous_destination = previous_channel
                                                   ? (int)(previous_channel - rgba)
                                                   : (int)(strchr(xyzw, swizzle[previous]) - xyzw);

                    if (previous_destination == destination)
                        return 0;
                }
                execution->fragment_output[destination] = value->data[i];
            }
        }
        return 1;
    }
    if (execution->vertex_output)
        for (i = 0; i < execution->program->varying_count; ++i) {
            Varying *varying = &execution->program->varyings[i];

            if (strlen(varying->name) == length && !strncmp(varying->name, name, length)) {
                int element = array_index < 0 ? 0 : array_index;
                int expected_columns = varying_slots(varying->type);
                int expected_rows = shader_type_rows(varying->type);
                int column;

                if (swizzle_length || (varying->size == 1 && array_index >= 0) ||
                    (varying->size > 1 && array_index < 0) || element >= varying->size)
                    return 0;
                if (value_type != MESAGL_GLSL_TYPE_FLOAT ||
                    value->columns != expected_columns ||
                    value->rows != expected_rows)
                    return 0;
                if (!varying->active)
                    return 1;
                for (column = 0; column < expected_columns; ++column)
                    memcpy(execution->vertex_output
                               ->varying[varying->slot + element * expected_columns + column],
                           value->data + column * expected_rows,
                           (size_t)expected_rows * sizeof(float));
                return 1;
            }
        }
    return 0;
}

static float attribute_component(const unsigned char *source, GLenum type, GLboolean normalized)
{
    if (type == GL_FLOAT) {
        float value;

        memcpy(&value, source, sizeof(value));
        return value;
    }
    if (type == GL_FIXED) {
        GLint value;

        memcpy(&value, source, sizeof(value));
        return value / 65536.0f;
    }
    if (type == GL_UNSIGNED_BYTE)
        return normalized ? *source / 255.0f : *source;
    if (type == GL_BYTE) {
        signed char value;

        memcpy(&value, source, sizeof(value));
        return normalized ? fmaxf(value / 127.0f, -1.0f) : value;
    }
    if (type == GL_UNSIGNED_SHORT) {
        GLushort value;

        memcpy(&value, source, sizeof(value));
        return normalized ? value / 65535.0f : value;
    }
    if (type == GL_SHORT) {
        GLshort value;

        memcpy(&value, source, sizeof(value));
        return normalized ? fmaxf(value / 32767.0f, -1.0f) : value;
    }
    return 0.0f;
}

static int load_attributes(GLuint vertex, ShaderExecution *execution)
{
    int index;

    for (index = 0; index < MAX_ATTRIBUTES; ++index) {
        AttribState *attribute = &attributes[index];
        const unsigned char *base;
        Buffer *buffer;
        int type_size;
        int component_index;
        size_t stride;

        memcpy(execution->inputs[index], attribute->current, sizeof(attribute->current));
        if (!attribute->enabled)
            continue;
        type_size = attribute_type_size(attribute->type);
        if (!type_size || attribute->size < 1 || attribute->size > 4)
            return 0;
        buffer = find_buffer(attribute->buffer);
        base = (const unsigned char *)attribute->pointer;
        if (buffer) {
            size_t offset = (size_t)attribute->pointer;

            if (offset >= buffer->size)
                return 0;
            base = buffer->data + offset;
        }
        if (!base)
            return 0;
        stride = attribute->stride ? (size_t)attribute->stride
                                   : (size_t)attribute->size * (size_t)type_size;
        base += (size_t)vertex * stride;
        for (component_index = 0; component_index < attribute->size; ++component_index)
            execution->inputs[index][component_index] =
                attribute_component(base + component_index * type_size, attribute->type,
                                    attribute->normalized);
    }
    return 1;
}

static NTGLprimitive programmable_mode(GLenum mode)
{
    switch (mode) {
    case GL_POINTS:
        return NTGL_POINTS;
    case GL_LINES:
        return NTGL_LINES;
    case GL_LINE_LOOP:
        return NTGL_LINE_LOOP;
    case GL_LINE_STRIP:
        return NTGL_LINE_STRIP;
    case GL_TRIANGLE_STRIP:
        return NTGL_TRIANGLE_STRIP;
    case GL_TRIANGLE_FAN:
        return NTGL_TRIANGLE_FAN;
    default:
        return NTGL_TRIANGLES;
    }
}

static int imgui_texture_index(int index, int size, int wrap)
{
    if (wrap == GL_REPEAT) {
        index %= size;
        return index < 0 ? index + size : index;
    }
    if (wrap == GL_MIRRORED_REPEAT) {
        int period = size * 2;

        index %= period;
        if (index < 0)
            index += period;
        return index < size ? index : period - index - 1;
    }
    if (index < 0)
        return 0;
    return index < size ? index : size - 1;
}

static float imgui_texture_coordinate(float coordinate, int wrap)
{
    if (!isfinite(coordinate))
        return 0.0f;
    if (wrap == GL_REPEAT)
        return coordinate - floorf(coordinate);
    if (wrap == GL_MIRRORED_REPEAT) {
        coordinate = fmodf(coordinate, 2.0f);
        if (coordinate < 0.0f)
            coordinate += 2.0f;
        return coordinate <= 1.0f ? coordinate : 2.0f - coordinate;
    }
    if (coordinate < 0.0f)
        return 0.0f;
    return coordinate > 1.0f ? 1.0f : coordinate;
}

static void sample_imgui_texture(const MesaGLPreparedTexture2D *sampler,
                                 float s, float t, float color[4])
{
    float x = imgui_texture_coordinate(s, sampler->wrap_s) * sampler->width;
    float y = imgui_texture_coordinate(t, sampler->wrap_t) * sampler->height;
    int component;

    if (sampler->nearest) {
        int sample_x = imgui_texture_index((int)floorf(x), sampler->width,
                                           sampler->wrap_s);
        int sample_y = imgui_texture_index((int)floorf(y), sampler->height,
                                           sampler->wrap_t);
        const unsigned char *pixel = sampler->pixels +
                                     ((size_t)sample_y * sampler->width +
                                      (size_t)sample_x) *
                                         4;

        for (component = 0; component < 4; ++component)
            color[component] = pixel[component] / 255.0f;
        return;
    }
    {
        int unwrapped_x0 = (int)floorf(x - 0.5f);
        int unwrapped_y0 = (int)floorf(y - 0.5f);
        int x0 = imgui_texture_index(unwrapped_x0, sampler->width,
                                     sampler->wrap_s);
        int y0 = imgui_texture_index(unwrapped_y0, sampler->height,
                                     sampler->wrap_t);
        int x1 = imgui_texture_index(unwrapped_x0 + 1, sampler->width,
                                     sampler->wrap_s);
        int y1 = imgui_texture_index(unwrapped_y0 + 1, sampler->height,
                                     sampler->wrap_t);
        float alpha_x = x - 0.5f - unwrapped_x0;
        float alpha_y = y - 0.5f - unwrapped_y0;

        for (component = 0; component < 4; ++component) {
            float c00 = sampler->pixels[((size_t)y0 * sampler->width + x0) * 4 +
                                        component] /
                        255.0f;
            float c10 = sampler->pixels[((size_t)y0 * sampler->width + x1) * 4 +
                                        component] /
                        255.0f;
            float c01 = sampler->pixels[((size_t)y1 * sampler->width + x0) * 4 +
                                        component] /
                        255.0f;
            float c11 = sampler->pixels[((size_t)y1 * sampler->width + x1) * 4 +
                                        component] /
                        255.0f;

            color[component] =
                (c00 * (1.0f - alpha_x) + c10 * alpha_x) *
                    (1.0f - alpha_y) +
                (c01 * (1.0f - alpha_x) + c11 * alpha_x) * alpha_y;
        }
    }
}

static float sample_imgui_alpha(const MesaGLPreparedTexture2D *sampler,
                                float s, float t)
{
    float x = imgui_texture_coordinate(s, sampler->wrap_s) * sampler->width;
    float y = imgui_texture_coordinate(t, sampler->wrap_t) * sampler->height;

    if (sampler->nearest) {
        int sample_x = imgui_texture_index((int)floorf(x), sampler->width,
                                           sampler->wrap_s);
        int sample_y = imgui_texture_index((int)floorf(y), sampler->height,
                                           sampler->wrap_t);

        return sampler->pixels[((size_t)sample_y * sampler->width +
                                (size_t)sample_x) *
                                   4 +
                               3] /
               255.0f;
    }
    {
        int unwrapped_x0 = (int)floorf(x - 0.5f);
        int unwrapped_y0 = (int)floorf(y - 0.5f);
        int x0 = imgui_texture_index(unwrapped_x0, sampler->width,
                                     sampler->wrap_s);
        int y0 = imgui_texture_index(unwrapped_y0, sampler->height,
                                     sampler->wrap_t);
        int x1 = imgui_texture_index(unwrapped_x0 + 1, sampler->width,
                                     sampler->wrap_s);
        int y1 = imgui_texture_index(unwrapped_y0 + 1, sampler->height,
                                     sampler->wrap_t);
        float alpha_x = x - 0.5f - unwrapped_x0;
        float alpha_y = y - 0.5f - unwrapped_y0;
        float a00 = sampler->pixels[((size_t)y0 * sampler->width + x0) * 4 + 3] /
                    255.0f;
        float a10 = sampler->pixels[((size_t)y0 * sampler->width + x1) * 4 + 3] /
                    255.0f;
        float a01 = sampler->pixels[((size_t)y1 * sampler->width + x0) * 4 + 3] /
                    255.0f;
        float a11 = sampler->pixels[((size_t)y1 * sampler->width + x1) * 4 + 3] /
                    255.0f;

        return (a00 * (1.0f - alpha_x) + a10 * alpha_x) *
                   (1.0f - alpha_y) +
               (a01 * (1.0f - alpha_x) + a11 * alpha_x) * alpha_y;
    }
}

static int imgui_program_fragment(void *user, const float *varyings,
                                  const float *varying_dfdx,
                                  const float *varying_dfdy, float color[4])
{
    ShaderExecution *execution = (ShaderExecution *)user;
    Program *program = execution->program;
    const float *uv = varyings + program->imgui_uv_varying * 4;
    const float *vertex_color =
        varyings + program->imgui_color_varying * 4;
    float texel[4];
    int component;

    if (MESAGL_LIKELY(execution->imgui_sampler_ready &&
                      execution->imgui_sample_cached &&
                      uv[0] == execution->imgui_cached_uv[0] &&
                      uv[1] == execution->imgui_cached_uv[1])) {
        memcpy(texel, execution->imgui_cached_texel, sizeof(texel));
    } else if (execution->imgui_sampler_ready &&
               execution->imgui_sampler.rgb_white) {
        texel[0] = 1.0f;
        texel[1] = 1.0f;
        texel[2] = 1.0f;
        texel[3] = sample_imgui_alpha(&execution->imgui_sampler,
                                      uv[0], uv[1]);
    } else if (execution->imgui_sampler_ready) {
        sample_imgui_texture(&execution->imgui_sampler, uv[0], uv[1], texel);
    } else if (varying_dfdx && varying_dfdy) {
        const float *dfdx = varying_dfdx + program->imgui_uv_varying * 4;
        const float *dfdy = varying_dfdy + program->imgui_uv_varying * 4;

        if (!mesaGLSampleTexture2DGradBias(execution->imgui_texture_unit,
                                           uv[0], uv[1], dfdx[0],
                                           dfdx[1], dfdy[0], dfdy[1], 0.0f,
                                           texel))
            return 0;
    } else if (!mesaGLSampleTexture2D(execution->imgui_texture_unit, uv[0],
                                      uv[1], texel)) {
        return 0;
    }
    if (execution->imgui_sampler_ready) {
        execution->imgui_cached_uv[0] = uv[0];
        execution->imgui_cached_uv[1] = uv[1];
        memcpy(execution->imgui_cached_texel, texel, sizeof(texel));
        execution->imgui_sample_cached = 1;
    }
    if (execution->imgui_sampler_ready && execution->imgui_sampler.rgb_white) {
        color[0] = vertex_color[0];
        color[1] = vertex_color[1];
        color[2] = vertex_color[2];
        color[3] = vertex_color[3] * texel[3];
        return 1;
    }
    for (component = 0; component < 4; ++component)
        color[component] = vertex_color[component] * texel[component];
    return 1;
}

static int program_fragment(void *user, const float *varyings, int varying_count,
                            const float *varying_dfdx, const float *varying_dfdy,
                            const float frag_coord[4], int front_facing,
                            const float point_coord[2], float color[4])
{
    ShaderExecution *shared = (ShaderExecution *)user;
    ShaderExecution execution;
    MesaGLSLValue value;
    int discarded = 0;

    if (shared->program->imgui_fast_path)
        return imgui_program_fragment(user, varyings, varying_dfdx,
                                      varying_dfdy, color);
    execution = *shared;

    execution.varyings = varyings;
    execution.varying_dfdx = varying_dfdx;
    execution.varying_dfdy = varying_dfdy;
    execution.varying_count = varying_count;
    execution.fragment_output = color;
    execution.front_facing = front_facing;
    memcpy(execution.point_coord, point_coord, sizeof(execution.point_coord));
    memcpy(execution.frag_coord, frag_coord, sizeof(execution.frag_coord));
    memset(color, 0, 4 * sizeof(*color));
    if (execution.program->fragment_body)
        return mesaGLSLExecuteProgram(
                   execution.program->linked_fragment_source,
                   execution.program->fragment_body, execution_lookup,
                   execution_call, execution_assign, &execution, &discarded,
                   NULL) &&
               !discarded;
    if (execution.program->fragment_discard[0]) {
        MesaGLSLValue condition;

        if (!mesaGLSLExpression(execution.program->fragment_discard, NULL, execution_lookup,
                                execution_call, &execution, &condition, NULL) ||
            condition.data[0] != 0.0f)
            return 0;
    }
    if (!mesaGLSLExpression(execution.program->fragment_color, NULL, execution_lookup,
                            execution_call,
                            &execution, &value, NULL) || value.rows != 4 || value.columns != 1)
        return 0;
    memcpy(color, value.data, 4 * sizeof(float));
    return 1;
}

static GLuint programmable_vertex_index(const void *indices, GLenum index_type,
                                        GLint first, int position)
{
    if (!indices)
        return (GLuint)first + (GLuint)position;
    if (index_type == GL_UNSIGNED_BYTE)
        return ((const GLubyte *)indices)[position];
    if (index_type == GL_UNSIGNED_SHORT)
        return ((const GLushort *)indices)[position];
    return ((const GLuint *)indices)[position];
}

static int execute_programmable_batch(Program *program, ShaderExecution *execution,
                                      NTGLprogramVertex *output, GLenum mode,
                                      const GLuint *vertices, int count)
{
    int i;

    for (i = 0; i < count; ++i) {
        int discarded = 0;

        memset(&output[i], 0, sizeof(output[i]));
        output[i].point_size = 1.0f;
        execution->vertex_output = &output[i];
        if (!load_attributes(vertices[i], execution))
            return 0;
        if (program->imgui_fast_path) {
            Uniform *projection = find_uniform(program,
                                               program->imgui_projection);
            const float *matrix = uniform_float_value(
                projection, program->imgui_projection);
            const float *position = execution->inputs[program->imgui_position];
            int row;

            if (!matrix)
                return 0;
            for (row = 0; row < 4; ++row)
                output[i].position[row] = matrix[row] * position[0] +
                                          matrix[4 + row] * position[1] +
                                          matrix[12 + row];
            memcpy(output[i].varying[program->imgui_uv_varying],
                   execution->inputs[program->imgui_uv], 2 * sizeof(float));
            memcpy(output[i].varying[program->imgui_color_varying],
                   execution->inputs[program->imgui_color], 4 * sizeof(float));
        } else if (!mesaGLSLExecuteProgram(
                       program->linked_vertex_source, program->vertex_body,
                       execution_lookup, execution_call, execution_assign,
                       execution, &discarded, NULL) ||
                   discarded) {
            return 0;
        }
    }
    execution->vertex_output = NULL;
    if (program->imgui_fast_path && execution->imgui_sampler_ready)
        ntglDrawProgrammableNoDerivativesClamped(
            programmable_mode(mode), output, count,
            program->varying_slot_count, program_fragment, execution);
    else
        ntglDrawProgrammable(programmable_mode(mode), output, count,
                             program->varying_slot_count, program_fragment,
                             execution);
    return 1;
}

static int draw_programmable_chunks(Program *program, ShaderExecution *execution,
                                    NTGLprogramVertex *output, GLenum mode,
                                    GLsizei count, GLenum index_type,
                                    const void *indices, GLint first)
{
    GLuint vertices[MESAGL_MAX_VERTICES];
    int start = 0;

    if (mode == GL_LINE_LOOP && count > MESAGL_MAX_VERTICES) {
        while (start < count) {
            int batch = count - start;
            int i;

            if (batch > MESAGL_MAX_VERTICES)
                batch = MESAGL_MAX_VERTICES;
            for (i = 0; i < batch; ++i)
                vertices[i] = programmable_vertex_index(indices, index_type,
                                                        first, start + i);
            if (!execute_programmable_batch(program, execution, output,
                                            GL_LINE_STRIP, vertices, batch))
                return 0;
            if (start + batch == count)
                break;
            start += batch - 1;
        }
        vertices[0] = programmable_vertex_index(indices, index_type, first,
                                                count - 1);
        vertices[1] = programmable_vertex_index(indices, index_type, first, 0);
        return execute_programmable_batch(program, execution, output, GL_LINES,
                                          vertices, 2);
    }

    if (mode == GL_TRIANGLE_FAN && count > MESAGL_MAX_VERTICES) {
        int batch = MESAGL_MAX_VERTICES;
        int i;

        for (i = 0; i < batch; ++i)
            vertices[i] = programmable_vertex_index(indices, index_type, first, i);
        if (!execute_programmable_batch(program, execution, output, mode, vertices,
                                        batch))
            return 0;
        start = batch;
        while (start < count) {
            int added = count - start;

            if (added > MESAGL_MAX_VERTICES - 2)
                added = MESAGL_MAX_VERTICES - 2;
            vertices[0] = programmable_vertex_index(indices, index_type, first, 0);
            vertices[1] = programmable_vertex_index(indices, index_type, first,
                                                    start - 1);
            for (i = 0; i < added; ++i)
                vertices[i + 2] = programmable_vertex_index(indices, index_type,
                                                            first, start + i);
            if (!execute_programmable_batch(program, execution, output, mode,
                                            vertices, added + 2))
                return 0;
            start += added;
        }
        return 1;
    }

    while (start < count) {
        int batch = count - start;
        int capacity = MESAGL_MAX_VERTICES;
        int advance;
        int i;

        if (mode == GL_TRIANGLES)
            capacity -= capacity % 3;
        else if (mode == GL_LINES)
            capacity -= capacity % 2;
        if (batch > capacity)
            batch = capacity;
        for (i = 0; i < batch; ++i)
            vertices[i] = programmable_vertex_index(indices, index_type, first,
                                                    start + i);
        if (!execute_programmable_batch(program, execution, output, mode, vertices,
                                        batch))
            return 0;
        if (start + batch == count)
            break;
        advance = batch;
        if (mode == GL_LINE_STRIP)
            advance -= 1;
        else if (mode == GL_TRIANGLE_STRIP)
            advance -= 2;
        if (advance <= 0)
            break;
        start += advance;
    }
    return 1;
}

static int draw_programmable(GLenum mode, GLsizei count, GLenum index_type, const void *indices,
                             GLint first)
{
    Program *program = find_program(current_program);
    NTGLprogramVertex *output;
    ShaderExecution execution;

    if (!program || !program->executable || !program->vertex_body ||
        !program->fragment_body)
        return 0;
    if (!program_sampler_state_valid(program)) {
        mesaGLSetError(GL_INVALID_OPERATION);
        return 1;
    }
    if (!count)
        return 1;
    if (!validate_attribute_bounds(
            indices ? maximum_index(index_type, indices, count)
                    : (GLuint)((uint64_t)(GLuint)first + (uint64_t)(GLuint)count - 1u)))
        return 1;
    output = (NTGLprogramVertex *)ntglAlloc(
        (size_t)(count < MESAGL_MAX_VERTICES ? count : MESAGL_MAX_VERTICES) *
        sizeof(*output));
    if (!output) {
        mesaGLSetError(GL_OUT_OF_MEMORY);
        return 1;
    }
    memset(&execution, 0, sizeof(execution));
    execution.program = program;
    if (program->imgui_fast_path) {
        Uniform *sampler = find_uniform(program, program->imgui_texture);
        GLint *unit = uniform_integer_value(sampler, program->imgui_texture);

        if (unit) {
            execution.imgui_texture_unit = unit[0];
            execution.imgui_sampler_ready = mesaGLPrepareTexture2D(
                unit[0], &execution.imgui_sampler);
        }
    }
    execution.uniform_array = (MesaGLSLValue *)ntglAlloc(
        MESAGL_MAX_SHADER_UNIFORM_STORAGE * sizeof(*execution.uniform_array));
    execution.uniform_member_names =
        (char (*)[MESAGL_MAX_SHADER_LVALUE_PATH_LENGTH])ntglAlloc(
        MESAGL_MAX_SHADER_UNIFORM_STORAGE * sizeof(*execution.uniform_member_names));
    execution.uniform_struct_members = (MesaGLSLValue *)ntglAlloc(
        MESAGL_MAX_SHADER_UNIFORM_STORAGE * sizeof(*execution.uniform_struct_members));
    execution.uniform_struct_array = (MesaGLSLValue *)ntglAlloc(
        MESAGL_MAX_SHADER_ARRAY_ELEMENTS * sizeof(*execution.uniform_struct_array));
    execution.varying_array = (MesaGLSLValue *)ntglAlloc(
        MESAGL_MAX_VARYING_INTERPOLATORS * sizeof(*execution.varying_array));
    if (!execution.uniform_array || !execution.uniform_member_names ||
        !execution.uniform_struct_members || !execution.uniform_struct_array ||
        !execution.varying_array) {
        ntglFree(execution.varying_array);
        ntglFree(execution.uniform_struct_array);
        ntglFree(execution.uniform_struct_members);
        ntglFree(execution.uniform_member_names);
        ntglFree(execution.uniform_array);
        ntglFree(output);
        mesaGLSetError(GL_OUT_OF_MEMORY);
        return 1;
    }
    memset(execution.uniform_array, 0,
           MESAGL_MAX_SHADER_UNIFORM_STORAGE * sizeof(*execution.uniform_array));
    memset(execution.varying_array, 0,
           MESAGL_MAX_VARYING_INTERPOLATORS * sizeof(*execution.varying_array));
    if (!draw_programmable_chunks(program, &execution, output, mode, count,
                                  index_type, indices, first))
        mesaGLSetError(GL_INVALID_OPERATION);
    ntglFree(execution.varying_array);
    ntglFree(execution.uniform_struct_array);
    ntglFree(execution.uniform_struct_members);
    ntglFree(execution.uniform_member_names);
    ntglFree(execution.uniform_array);
    ntglFree(output);
    return 1;
}
#endif

int mesaGLDrawGLES2Arrays(unsigned int mode, int first, int count)
{
#if MESAGL_GLES2_PROFILE == MESAGL_GLES2_PROFILE_FULL
    return draw_programmable(mode, count, 0, NULL, first);
#else
    Program *program = find_program(current_program);

    (void)mode;
    if (program && !program_sampler_state_valid(program)) {
        mesaGLSetError(GL_INVALID_OPERATION);
        return 1;
    }
    if (program && !count)
        return 1;
    if (program &&
        !validate_attribute_bounds(
            (GLuint)((uint64_t)(GLuint)first + (uint64_t)(GLuint)count - 1u)))
        return 1;
    return 0;
#endif
}

int mesaGLDrawGLES2Elements(unsigned int mode, int count, unsigned int type, const void *indices)
{
#if MESAGL_GLES2_PROFILE == MESAGL_GLES2_PROFILE_FULL
    const void *resolved;
    Buffer *buffer = find_buffer(element_buffer);
    size_t index_size = type == GL_UNSIGNED_BYTE    ? 1
                        : type == GL_UNSIGNED_SHORT ? 2
                                                    : 4;

    if (type != GL_UNSIGNED_BYTE && type != GL_UNSIGNED_SHORT &&
        (!MESAGL_ENABLE_UINT_ELEMENT_INDICES || type != GL_UNSIGNED_INT))
        return 0;
    if (!count)
        return draw_programmable(mode, count, type, NULL, 0);
    if (buffer) {
        size_t offset = (size_t)indices;
        size_t bytes;

        if ((size_t)count > SIZE_MAX / index_size) {
            mesaGLSetError(GL_INVALID_OPERATION);
            return 1;
        }
        bytes = (size_t)count * index_size;

        if (offset > buffer->size || bytes > buffer->size - offset) {
            mesaGLSetError(GL_INVALID_OPERATION);
            return 1;
        }
        resolved = buffer->data + offset;
    } else {
        if (!indices) {
            mesaGLSetError(GL_INVALID_OPERATION);
            return 1;
        }
        resolved = indices;
    }
    return draw_programmable(mode, count, type, resolved, 0);
#else
    const void *resolved;
    Program *program = find_program(current_program);
    Buffer *buffer = find_buffer(element_buffer);
    size_t index_size = type == GL_UNSIGNED_BYTE    ? 1
                        : type == GL_UNSIGNED_SHORT ? 2
                                                    : 4;

    (void)mode;
    if (!program)
        return 0;
    if (!program_sampler_state_valid(program)) {
        mesaGLSetError(GL_INVALID_OPERATION);
        return 1;
    }
    if (!count)
        return 1;
    if (buffer) {
        size_t offset = (size_t)indices;
        size_t bytes;

        if ((size_t)count > SIZE_MAX / index_size) {
            mesaGLSetError(GL_INVALID_OPERATION);
            return 1;
        }
        bytes = (size_t)count * index_size;
        if (offset > buffer->size || bytes > buffer->size - offset) {
            mesaGLSetError(GL_INVALID_OPERATION);
            return 1;
        }
        resolved = buffer->data + offset;
    } else {
        if (!indices) {
            mesaGLSetError(GL_INVALID_OPERATION);
            return 1;
        }
        resolved = indices;
    }
    if (!validate_attribute_bounds(maximum_index(type, resolved, count)))
        return 1;
    return 0;
#endif
}

void mesaGLGLES2GetIntegerv(unsigned int pname, int *value, int *handled)
{
    *handled = 1;
    if (pname == GL_CURRENT_PROGRAM)
        *value = (int)current_program;
    else if (pname == GL_ARRAY_BUFFER_BINDING)
        *value = (int)array_buffer;
    else if (pname == GL_ELEMENT_ARRAY_BUFFER_BINDING)
        *value = (int)element_buffer;
    else if (pname == GL_ACTIVE_TEXTURE)
        *value = (int)active_texture;
    else if (pname == GL_MAX_TEXTURE_IMAGE_UNITS)
        *value = MESAGL_MAX_FRAGMENT_TEXTURE_IMAGE_UNITS;
    else if (pname == GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS)
        *value = MESAGL_MAX_VERTEX_TEXTURE_IMAGE_UNITS;
    else if (pname == GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS)
        *value = MESAGL_MAX_COMBINED_TEXTURE_IMAGE_UNITS;
    else if (pname == GL_MAX_VERTEX_ATTRIBS)
        *value = MAX_ATTRIBUTES;
    else if (pname == GL_MAX_VARYING_VECTORS)
        *value = MESAGL_MAX_VARYING_VECTORS;
    else if (pname == GL_MAX_VERTEX_UNIFORM_VECTORS)
        *value = MESAGL_MAX_VERTEX_UNIFORM_VECTORS;
    else if (pname == GL_MAX_FRAGMENT_UNIFORM_VECTORS)
        *value = MESAGL_MAX_FRAGMENT_UNIFORM_VECTORS;
    else
        *handled = 0;
}
