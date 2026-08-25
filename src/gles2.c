#include "GLES2/gl2.h"
#include "gles2_internal.h"
#include "mesaGL/ntgl.h"

#include <stdlib.h>
#include <string.h>

#define MAX_SHADERS 32
#define MAX_PROGRAMS 16
#define MAX_BUFFERS 64
#define MAX_BINDINGS 8
#define MAX_ATTRIBUTES 8
#define LOG_SIZE 128
#define MAX_CONTEXTS 8

typedef struct Shader {
    GLuint name;
    GLenum type;
    char *source;
    int compiled;
    char log[LOG_SIZE];
} Shader;

typedef struct Binding {
    char name[48];
    GLuint index;
} Binding;

typedef struct Program {
    GLuint name;
    GLuint vertex_shader;
    GLuint fragment_shader;
    int linked;
    int uses_texture;
    GLint sampler;
    Binding bindings[MAX_BINDINGS];
    int binding_count;
    char log[LOG_SIZE];
} Program;

typedef struct Buffer {
    GLuint name;
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
} AttribState;

typedef struct GLESContextState {
    NTGLcontext *context;
    Shader shaders[MAX_SHADERS];
    Program programs[MAX_PROGRAMS];
    Buffer buffers[MAX_BUFFERS];
    GLuint next_shader, next_program, next_buffer;
    GLuint current_program, array_buffer, element_buffer;
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
    context_states[free_slot].next_shader = 1;
    context_states[free_slot].next_program = 1;
    context_states[free_slot].next_buffer = 1;
    return &context_states[free_slot];
}

void mesaGLGLES2ReleaseCurrentContext(void)
{
    NTGLcontext *context = ntglGetCurrent();
    int i, slot;
    for (slot = 0; slot < MAX_CONTEXTS; ++slot)
        if (context_states[slot].context == context) {
            for (i = 0; i < MAX_SHADERS; ++i)
                ntglFree(context_states[slot].shaders[i].source);
            for (i = 0; i < MAX_BUFFERS; ++i)
                ntglFree(context_states[slot].buffers[i].data);
            memset(&context_states[slot], 0, sizeof(context_states[slot]));
            return;
        }
}

#define shaders (current_state()->shaders)
#define programs (current_state()->programs)
#define buffers (current_state()->buffers)
#define next_shader (current_state()->next_shader)
#define next_program (current_state()->next_program)
#define next_buffer (current_state()->next_buffer)
#define current_program (current_state()->current_program)
#define array_buffer (current_state()->array_buffer)
#define element_buffer (current_state()->element_buffer)
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

static void copy_log(const char *source, GLsizei size, GLsizei *length, GLchar *destination)
{
    size_t count = source ? strlen(source) : 0;
    if (size > 0 && destination) {
        if (count >= (size_t)size)
            count = (size_t)size - 1;
        memcpy(destination, source, count);
        destination[count] = '\0';
    }
    if (length)
        *length = (GLsizei)count;
}

static int source_has(const Shader *shader, const char *text)
{
    return shader && shader->source && strstr(shader->source, text) != NULL;
}

GLuint glCreateShader(GLenum type)
{
    int i;
    if (type != GL_VERTEX_SHADER && type != GL_FRAGMENT_SHADER)
        return 0;
    for (i = 0; i < MAX_SHADERS; ++i) {
        if (!shaders[i].name) {
            shaders[i].name = next_shader++;
            shaders[i].type = type;
            return shaders[i].name;
        }
    }
    return 0;
}

void glDeleteShader(GLuint name)
{
    Shader *shader = find_shader(name);
    if (!shader)
        return;
    ntglFree(shader->source);
    memset(shader, 0, sizeof(*shader));
}

void glShaderSource(GLuint name, GLsizei count, const GLchar *const *strings, const GLint *lengths)
{
    Shader *shader = find_shader(name);
    size_t total = 0;
    char *source;
    int i;
    if (!shader || count < 0 || (count && !strings))
        return;
    for (i = 0; i < count; ++i)
        total += lengths && lengths[i] >= 0 ? (size_t)lengths[i] : strlen(strings[i]);
    source = (char *)ntglAlloc(total + 1);
    if (!source)
        return;
    total = 0;
    for (i = 0; i < count; ++i) {
        size_t part = lengths && lengths[i] >= 0 ? (size_t)lengths[i] : strlen(strings[i]);
        memcpy(source + total, strings[i], part);
        total += part;
    }
    source[total] = '\0';
    ntglFree(shader->source);
    shader->source = source;
    shader->compiled = 0;
}

void glCompileShader(GLuint name)
{
    Shader *shader = find_shader(name);
    if (!shader)
        return;
    shader->compiled = 0;
    shader->log[0] = '\0';
    if (!source_has(shader, "void main")) {
        strcpy(shader->log, "missing void main");
        return;
    }
    if (shader->type == GL_VERTEX_SHADER && !source_has(shader, "gl_Position")) {
        strcpy(shader->log, "vertex shader must write gl_Position");
        return;
    }
    if (shader->type == GL_FRAGMENT_SHADER && !source_has(shader, "gl_FragColor")) {
        strcpy(shader->log, "fragment shader must write gl_FragColor");
        return;
    }
    if (source_has(shader, "for(") || source_has(shader, "for (") || source_has(shader, "while(") ||
        source_has(shader, "while (") || source_has(shader, "discard")) {
        strcpy(shader->log, "control flow is outside the UI shader subset");
        return;
    }
    shader->compiled = 1;
}

void glGetShaderiv(GLuint name, GLenum pname, GLint *params)
{
    Shader *shader = find_shader(name);
    if (!params)
        return;
    if (!shader) {
        *params = 0;
        return;
    }
    if (pname == GL_COMPILE_STATUS)
        *params = shader->compiled;
    else if (pname == GL_INFO_LOG_LENGTH)
        *params = (GLint)strlen(shader->log) + 1;
    else if (pname == GL_SHADER_TYPE)
        *params = (GLint)shader->type;
    else
        *params = 0;
}

void glGetShaderInfoLog(GLuint name, GLsizei size, GLsizei *length, GLchar *log)
{
    Shader *shader = find_shader(name);
    copy_log(shader ? shader->log : "invalid shader", size, length, log);
}

GLuint glCreateProgram(void)
{
    int i;
    for (i = 0; i < MAX_PROGRAMS; ++i) {
        if (!programs[i].name) {
            programs[i].name = next_program++;
            return programs[i].name;
        }
    }
    return 0;
}

void glDeleteProgram(GLuint name)
{
    Program *program = find_program(name);
    if (!program)
        return;
    if (current_program == name)
        current_program = 0;
    memset(program, 0, sizeof(*program));
}

void glAttachShader(GLuint program_name, GLuint shader_name)
{
    Program *program = find_program(program_name);
    Shader *shader = find_shader(shader_name);
    if (!program || !shader)
        return;
    if (shader->type == GL_VERTEX_SHADER)
        program->vertex_shader = shader_name;
    else
        program->fragment_shader = shader_name;
}

void glDetachShader(GLuint program_name, GLuint shader_name)
{
    Program *program = find_program(program_name);
    if (!program)
        return;
    if (program->vertex_shader == shader_name)
        program->vertex_shader = 0;
    if (program->fragment_shader == shader_name)
        program->fragment_shader = 0;
}

void glBindAttribLocation(GLuint program_name, GLuint index, const GLchar *name)
{
    Program *program = find_program(program_name);
    Binding *binding;
    if (!program || !name || program->binding_count >= MAX_BINDINGS)
        return;
    binding = &program->bindings[program->binding_count++];
    strncpy(binding->name, name, sizeof(binding->name) - 1);
    binding->name[sizeof(binding->name) - 1] = '\0';
    binding->index = index;
}

void glLinkProgram(GLuint name)
{
    Program *program = find_program(name);
    Shader *vertex;
    Shader *fragment;
    if (!program)
        return;
    vertex = find_shader(program->vertex_shader);
    fragment = find_shader(program->fragment_shader);
    program->linked = 0;
    program->log[0] = '\0';
    if (!vertex || !fragment || !vertex->compiled || !fragment->compiled) {
        strcpy(program->log, "compiled vertex and fragment shaders are required");
        return;
    }
    program->uses_texture = source_has(fragment, "texture2D");
    program->linked = 1;
}

void glGetProgramiv(GLuint name, GLenum pname, GLint *params)
{
    Program *program = find_program(name);
    if (!params)
        return;
    if (!program) {
        *params = 0;
        return;
    }
    if (pname == GL_LINK_STATUS)
        *params = program->linked;
    else if (pname == GL_INFO_LOG_LENGTH)
        *params = (GLint)strlen(program->log) + 1;
    else
        *params = 0;
}

void glGetProgramInfoLog(GLuint name, GLsizei size, GLsizei *length, GLchar *log)
{
    Program *program = find_program(name);
    copy_log(program ? program->log : "invalid program", size, length, log);
}

void glUseProgram(GLuint name)
{
    Program *program = find_program(name);
    if (!name) {
        current_program = 0;
        return;
    }
    if (!program || !program->linked)
        return;
    current_program = name;
    if (program->uses_texture) {
        glEnable(GL_TEXTURE_2D);
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    } else
        glDisable(GL_TEXTURE_2D);
}

GLboolean glIsProgram(GLuint name)
{
    return find_program(name) ? GL_TRUE : GL_FALSE;
}

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

GLint glGetAttribLocation(GLuint name, const GLchar *attribute)
{
    Program *program = find_program(name);
    int i;
    if (!program || !program->linked || !attribute)
        return -1;
    for (i = 0; i < program->binding_count; ++i)
        if (!strcmp(program->bindings[i].name, attribute))
            return (GLint)program->bindings[i].index;
    return conventional_attribute(attribute);
}

GLint glGetUniformLocation(GLuint name, const GLchar *uniform)
{
    Program *program = find_program(name);
    if (!program || !program->linked || !uniform)
        return -1;
    if (strstr(uniform, "Proj") || strstr(uniform, "MVP") || strstr(uniform, "Matrix"))
        return 1;
    if (strstr(uniform, "Texture") || strstr(uniform, "Sampler") || strstr(uniform, "Tex"))
        return 2;
    return -1;
}

void glUniform1i(GLint location, GLint value)
{
    Program *program = find_program(current_program);

    if (program && location == 2)
        program->sampler = value;
}

void glUniform1f(GLint location, GLfloat value)
{
    (void)location;
    (void)value;
}

void glUniform2f(GLint location, GLfloat x, GLfloat y)
{
    (void)location;
    (void)x;
    (void)y;
}

void glUniform4f(GLint location, GLfloat x, GLfloat y, GLfloat z, GLfloat w)
{
    (void)location;
    (void)x;
    (void)y;
    (void)z;
    (void)w;
}

void glUniformMatrix4fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value)
{
    if (location != 1 || count != 1 || transpose || !value)
        return;
    glMatrixMode(GL_PROJECTION);
    glLoadMatrixf(value);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

void glGenBuffers(GLsizei n, GLuint *names)
{
    int i;
    int slot;
    if (n < 0 || !names)
        return;
    for (i = 0; i < n; ++i) {
        for (slot = 0; slot < MAX_BUFFERS && buffers[slot].name; ++slot) {
        }
        if (slot == MAX_BUFFERS) {
            names[i] = 0;
            continue;
        }
        buffers[slot].name = next_buffer++;
        names[i] = buffers[slot].name;
    }
}

void glDeleteBuffers(GLsizei n, const GLuint *names)
{
    int i;
    for (i = 0; i < n; ++i) {
        Buffer *buffer = find_buffer(names[i]);
        if (!buffer)
            continue;
        ntglFree(buffer->data);
        memset(buffer, 0, sizeof(*buffer));
        if (array_buffer == names[i])
            array_buffer = 0;
        if (element_buffer == names[i])
            element_buffer = 0;
    }
}

void glBindBuffer(GLenum target, GLuint name)
{
    if (name && !find_buffer(name))
        return;
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
    Buffer *buffer = bound_buffer(target);
    unsigned char *storage;
    if (!buffer || size < 0)
        return;
    storage = size ? (unsigned char *)ntglAlloc((size_t)size) : NULL;
    if (size && !storage)
        return;
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
    Buffer *buffer = bound_buffer(target);
    if (!buffer || !data || offset < 0 || size < 0 || (size_t)offset + (size_t)size > buffer->size)
        return;
    memcpy(buffer->data + offset, data, (size_t)size);
}

void glEnableVertexAttribArray(GLuint index)
{
    if (index < MAX_ATTRIBUTES)
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
    if (index < MAX_ATTRIBUTES)
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
    if (index >= MAX_ATTRIBUTES)
        return;
    attributes[index].size = size;
    attributes[index].type = type;
    attributes[index].normalized = normalized;
    attributes[index].stride = stride;
    attributes[index].pointer = pointer;
    attributes[index].buffer = array_buffer;
    if (buffer) {
        size_t offset = (size_t)pointer;
        if (offset >= buffer->size)
            return;
        resolved = buffer->data + offset;
    }
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
    if (texture != GL_TEXTURE0)
        return;
}

void glGetVertexAttribiv(GLuint index, GLenum pname, GLint *params)
{
    AttribState *attribute;
    if (!params || index >= MAX_ATTRIBUTES)
        return;
    attribute = &attributes[index];
    if (pname == GL_VERTEX_ATTRIB_ARRAY_ENABLED)
        *params = attribute->enabled;
    else if (pname == GL_VERTEX_ATTRIB_ARRAY_SIZE)
        *params = attribute->size;
    else if (pname == GL_VERTEX_ATTRIB_ARRAY_TYPE)
        *params = (GLint)attribute->type;
    else if (pname == GL_VERTEX_ATTRIB_ARRAY_NORMALIZED)
        *params = attribute->normalized;
    else if (pname == GL_VERTEX_ATTRIB_ARRAY_STRIDE)
        *params = attribute->stride;
    else
        *params = 0;
}

void glGetVertexAttribPointerv(GLuint index, GLenum pname, void **pointer)
{
    if (!pointer || index >= MAX_ATTRIBUTES || pname != GL_VERTEX_ATTRIB_ARRAY_POINTER)
        return;
    *pointer = (void *)attributes[index].pointer;
}

void glBlendEquation(GLenum mode)
{
    glBlendEquationSeparate(mode, mode);
}

void glBlendEquationSeparate(GLenum mode_rgb, GLenum mode_alpha)
{
    NTGLblendEquation rgb = mode_rgb == GL_FUNC_SUBTRACT           ? NTGL_FUNC_SUBTRACT
                            : mode_rgb == GL_FUNC_REVERSE_SUBTRACT ? NTGL_FUNC_REVERSE_SUBTRACT
                            : mode_rgb == GL_MIN                   ? NTGL_MIN
                            : mode_rgb == GL_MAX                   ? NTGL_MAX
                                                                   : NTGL_FUNC_ADD;
    NTGLblendEquation alpha = mode_alpha == GL_FUNC_SUBTRACT           ? NTGL_FUNC_SUBTRACT
                              : mode_alpha == GL_FUNC_REVERSE_SUBTRACT ? NTGL_FUNC_REVERSE_SUBTRACT
                              : mode_alpha == GL_MIN                   ? NTGL_MIN
                              : mode_alpha == GL_MAX                   ? NTGL_MAX
                                                                       : NTGL_FUNC_ADD;
    ntglBlendEquationSeparate(rgb, alpha);
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

void mesaGLGLES2GetIntegerv(unsigned int pname, int *value, int *handled)
{
    *handled = 1;
    if (pname == GL_CURRENT_PROGRAM)
        *value = (int)current_program;
    else if (pname == GL_ARRAY_BUFFER_BINDING)
        *value = (int)array_buffer;
    else if (pname == GL_ELEMENT_ARRAY_BUFFER_BINDING)
        *value = (int)element_buffer;
    else
        *handled = 0;
}
