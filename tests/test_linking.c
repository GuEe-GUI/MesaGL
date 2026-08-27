#include "GLES2/gl2.h"
#include "mesaGL/ntgl.h"

#include <string.h>
#include <stdio.h>

#define STRINGIFY_VALUE_(value) #value
#define STRINGIFY_VALUE(value) STRINGIFY_VALUE_(value)

static GLuint compile(GLenum type, const char *source)
{
    GLuint shader = glCreateShader(type);
    GLint status;

    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if (!status) {
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

static GLuint compile_for_link(GLenum type, const char *source, char *log, int log_size)
{
    GLuint shader = glCreateShader(type);
    GLint status = 0;

    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if (!status) {
        if (log)
            glGetShaderInfoLog(shader, log_size, NULL, log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

static int link_status(const char *vertex_source, const char *fragment_source, char *log,
                       int log_size)
{
    GLuint vertex = compile_for_link(GL_VERTEX_SHADER, vertex_source, log, log_size);
    GLuint fragment = vertex ? compile_for_link(GL_FRAGMENT_SHADER, fragment_source,
                                                log, log_size) : 0;
    GLuint program;
    GLint status = 0;

    if (!vertex || !fragment) {
        if (vertex)
            glDeleteShader(vertex);
        if (fragment)
            glDeleteShader(fragment);
        return 0;
    }
    program = glCreateProgram();
    glAttachShader(program, vertex);
    glAttachShader(program, fragment);
    glLinkProgram(program);
    glGetProgramiv(program, GL_LINK_STATUS, &status);
    if (log)
        glGetProgramInfoLog(program, log_size, NULL, log);
    glDeleteProgram(program);
    glDeleteShader(vertex);
    glDeleteShader(fragment);
    return status;
}

static int link_bound_status(const char *vertex_source, const char *fragment_source, char *log,
                             int log_size)
{
    GLuint vertex = compile(GL_VERTEX_SHADER, vertex_source);
    GLuint fragment = compile(GL_FRAGMENT_SHADER, fragment_source);
    GLuint program = glCreateProgram();
    GLint status = 0;

    glBindAttribLocation(program, 1, "matrix_value");
    glBindAttribLocation(program, 1, "color_value");
    glAttachShader(program, vertex);
    glAttachShader(program, fragment);
    glLinkProgram(program);
    glGetProgramiv(program, GL_LINK_STATUS, &status);
    if (log)
        glGetProgramInfoLog(program, log_size, NULL, log);
    glDeleteProgram(program);
    glDeleteShader(fragment);
    glDeleteShader(vertex);
    return status;
}

static int active_attribute_count(const char *vertex_source, const char *fragment_source)
{
    GLuint vertex = compile(GL_VERTEX_SHADER, vertex_source);
    GLuint fragment = compile(GL_FRAGMENT_SHADER, fragment_source);
    GLuint program = glCreateProgram();
    GLint linked = 0;
    GLint count = -1;

    glAttachShader(program, vertex);
    glAttachShader(program, fragment);
    glLinkProgram(program);
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (linked)
        glGetProgramiv(program, GL_ACTIVE_ATTRIBUTES, &count);
    glDeleteProgram(program);
    glDeleteShader(fragment);
    glDeleteShader(vertex);
    return count;
}

static int prebound_inactive_attribute_test(const char *vertex_source,
                                            const char *fragment_source)
{
    GLuint vertex = compile(GL_VERTEX_SHADER, vertex_source);
    GLuint fragment = compile(GL_FRAGMENT_SHADER, fragment_source);
    GLuint program = glCreateProgram();
    GLint linked = 0;
    GLint count = -1;
    GLint location;

    glBindAttribLocation(program, 7, "unused_value");
    glAttachShader(program, vertex);
    glAttachShader(program, fragment);
    glLinkProgram(program);
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    glGetProgramiv(program, GL_ACTIVE_ATTRIBUTES, &count);
    location = glGetAttribLocation(program, "unused_value");
    glDeleteProgram(program);
    glDeleteShader(fragment);
    glDeleteShader(vertex);
    return linked && count == 1 && location == -1;
}

static int unreachable_resource_test(const char *vertex_source,
                                     const char *fragment_source)
{
    static const GLfloat positions[] = {
        -1.0f, -1.0f,
         3.0f, -1.0f,
        -1.0f,  3.0f,
    };
    GLuint vertex = compile(GL_VERTEX_SHADER, vertex_source);
    GLuint fragment = compile(GL_FRAGMENT_SHADER, fragment_source);
    GLuint program = glCreateProgram();
    GLint linked = 0;
    GLint attributes = -1;
    GLint uniforms = -1;
    GLint position;
    GLint scale;
    GLint tint;
    GLint active_values;
    GLint live_after_shadow;
    GLint active_values_size = -1;
    unsigned char pixel[4] = {0};
    GLfloat zero_values[12] = {0};
    GLint uniform_index;
    int valid;

    glAttachShader(program, vertex);
    glAttachShader(program, fragment);
    glLinkProgram(program);
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    glGetProgramiv(program, GL_ACTIVE_ATTRIBUTES, &attributes);
    glGetProgramiv(program, GL_ACTIVE_UNIFORMS, &uniforms);
    position = glGetAttribLocation(program, "position");
    scale = glGetUniformLocation(program, "scale");
    tint = glGetUniformLocation(program, "tint");
    active_values = glGetUniformLocation(program, "active_values[0]");
    live_after_shadow = glGetUniformLocation(program, "live_after_shadow");
    for (uniform_index = 0; uniform_index < uniforms; ++uniform_index) {
        char uniform_name[64];
        GLint uniform_size = 0;

        glGetActiveUniform(program, (GLuint)uniform_index, sizeof(uniform_name),
                           NULL, &uniform_size, NULL, uniform_name);
        if (!strcmp(uniform_name, "active_values[0]"))
            active_values_size = uniform_size;
    }
    valid = linked && attributes == 1 && uniforms == 4 &&
            glGetAttribLocation(program, "dead_position") == -1 &&
            glGetAttribLocation(program, "shadowed_attribute") == -1 &&
            glGetUniformLocation(program, "dead_scale") == -1 &&
            glGetUniformLocation(program, "dead_color") == -1 &&
            glGetUniformLocation(program, "shadowed_uniform") == -1 &&
            glGetUniformLocation(program, "dead_material.color") == -1 &&
            glGetUniformLocation(program, "active_values[2]") >= 0 &&
            glGetUniformLocation(program, "active_values[3]") == -1 &&
            position >= 0 && scale >= 0 && tint >= 0 && active_values >= 0 &&
            live_after_shadow >= 0 &&
            active_values_size == 3;
    if (valid) {
        glUseProgram(program);
        glUniform1f(scale, 1.0f);
        glUniform4f(tint, 0.25f, 0.5f, 0.75f, 1.0f);
        glUniform4fv(active_values, 3, zero_values);
        glUniform4f(live_after_shadow, 0.0f, 0.0f, 0.0f, 0.0f);
        glVertexAttribPointer((GLuint)position, 2, GL_FLOAT, GL_FALSE, 0,
                              positions);
        glEnableVertexAttribArray((GLuint)position);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
        valid = glGetError() == GL_NO_ERROR && pixel[0] >= 63 && pixel[0] <= 65 &&
                pixel[1] >= 127 && pixel[1] <= 129 &&
                pixel[2] >= 190 && pixel[2] <= 192 && pixel[3] == 255;
        glDisableVertexAttribArray((GLuint)position);
        glUseProgram(0);
    }
    glDeleteProgram(program);
    glDeleteShader(fragment);
    glDeleteShader(vertex);
    return valid;
}

int main(void)
{
    unsigned int pixels[4] = {0};
    NTGLframebuffer framebuffer = {
        pixels, 2, 2, 2 * (int)sizeof(*pixels), NTGL_XRGB8888, NTGL_ORIGIN_TOP_LEFT};
    NTGLcontext *context = ntglCreateContext(&framebuffer, NULL);
    const char valid_vertex[] =
        "/* attribute mat4 ignored_attribute; varying mat4 ignored_varying; */"
        "attribute vec2 position; varying vec3 color; uniform mediump vec4 tint;"
        "void main() { color = tint.rgb; gl_Position = vec4(position, 0.0, 1.0); }";
    const char valid_fragment[] =
        "// uniform Unsupported ignored_uniform;\n"
        "precision mediump float; varying vec3 color; uniform vec4 tint;"
        "void main() { gl_FragColor = vec4(color, tint.a); }";
    const char unreachable_resource_vertex[] =
        "attribute vec2 position; attribute vec4 dead_position;"
        "attribute vec4 shadowed_attribute;"
        "uniform float scale; uniform float dead_scale;"
        "uniform highp vec4 shadowed_uniform;"
        "uniform vec4 live_after_shadow;"
        "const int active_index = 1 + 1;"
        "uniform vec4 active_values[8]; varying vec4 dead_varying;"
        "varying vec4 shadowed_varying;"
        "struct Material { vec4 color; }; uniform Material dead_material;"
        "vec4 path(vec4 value) { dead_varying = dead_position;"
        "return value * dead_scale * dead_material.color; }"
        "vec4 shadow_helper(vec4 shadowed_uniform) { return shadowed_uniform; }"
        "vec4 scope_helper() { { vec4 live_after_shadow = vec4(0.0); }"
        "return live_after_shadow; }"
        "vec4 path(vec2 value) { vec4 shadowed_attribute = vec4(0.0);"
        "return vec4(value * scale, 0.0, 1.0) + shadowed_attribute +"
        "shadow_helper(shadowed_attribute) +"
        "active_values[active_index] + scope_helper(); }"
        "void main() { gl_Position = path(position); }";
    const char unreachable_resource_fragment[] =
        "precision mediump float; uniform vec4 tint; uniform vec4 dead_color;"
        "uniform highp vec4 shadowed_uniform;"
        "varying vec4 dead_varying; varying vec4 shadowed_varying;"
        "vec4 dead_path() { return dead_varying * dead_color; }"
        "vec4 live_path() { return tint; }"
        "void main() { vec4 shadowed_uniform = vec4(0.0);"
        "vec4 shadowed_varying = vec4(0.0);"
        "gl_FragColor = live_path() + shadowed_uniform + shadowed_varying; }";
    const char mismatched_fragment[] =
        "precision mediump float; varying vec2 color;"
        "void main() { gl_FragColor = vec4(color, 0.0, 1.0); }";
    const char uniform_vertex[] =
        "uniform vec3 shared_value; void main() { gl_Position = vec4(shared_value, 1.0); }";
    const char uniform_fragment[] =
        "precision mediump float; uniform vec4 shared_value;"
        "void main() { gl_FragColor = shared_value; }";
    const char inactive_uniform_vertex[] =
        "uniform vec4 inactive_shared; void main() { gl_Position = vec4(0.0); }";
    const char inactive_uniform_fragment[] =
        "precision mediump float; uniform vec3 inactive_shared;"
        "void main() { gl_FragColor = vec4(1.0); }";
    const char inactive_uniform_array_vertex[] =
        "uniform vec4 inactive_values[2]; void main() { gl_Position = vec4(0.0); }";
    const char inactive_uniform_array_fragment[] =
        "precision mediump float; uniform vec4 inactive_values[3];"
        "void main() { gl_FragColor = vec4(1.0); }";
    const char precision_uniform_vertex[] =
        "uniform lowp vec4 precise_value; void main() { gl_Position = vec4(0.0); }";
    const char precision_uniform_fragment[] =
        "precision mediump float; uniform highp vec4 precise_value;"
        "void main() { gl_FragColor = vec4(1.0); }";
    const char too_many_varyings[] =
        "varying vec4 v0; varying vec4 v1; varying vec4 v2; varying vec4 v3;"
        "varying vec4 v4; varying vec4 v5; varying vec4 v6; varying vec4 v7;"
        "varying vec4 v8; void main() { gl_Position = vec4(0.0); }";
    const char too_many_active_varyings[] =
        "varying vec4 v0; varying vec4 v1; varying vec4 v2; varying vec4 v3;"
        "varying vec4 v4; varying vec4 v5; varying vec4 v6; varying vec4 v7;"
        "varying vec4 v8; void main() { v0 = vec4(0.0); v1 = v0; v2 = v1; v3 = v2;"
        "v4 = v3; v5 = v4; v6 = v5; v7 = v6; v8 = v7; gl_Position = vec4(0.0); }";
    const char too_many_active_varyings_fragment[] =
        "precision mediump float; varying vec4 v0; varying vec4 v1; varying vec4 v2;"
        "varying vec4 v3; varying vec4 v4; varying vec4 v5; varying vec4 v6;"
        "varying vec4 v7; varying vec4 v8; void main() { gl_FragColor ="
        "v0 + v1 + v2 + v3 + v4 + v5 + v6 + v7 + v8; }";
    const char matrix_varyings_vertex[] =
        "varying mat4 transforms[2];"
        "void main() { transforms[0] = mat4(1.0); transforms[1] = mat4(0.5);"
        "gl_Position = vec4(0.0); }";
    const char matrix_varyings_fragment[] =
        "precision mediump float; varying mat4 transforms[2];"
        "void main() { gl_FragColor = transforms[0][0] + transforms[1][1]; }";
    const char too_many_matrix_varyings[] =
        "varying mat3 transforms[3];"
        "void main() { transforms[0] = mat3(1.0); gl_Position = vec4(0.0); }";
    const char too_many_matrix_varyings_fragment[] =
        "precision mediump float; varying mat3 transforms[3];"
        "void main() { gl_FragColor = vec4(transforms[0][0] + transforms[1][1] + "
        "transforms[2][2], 1.0); }";
    const char simple_fragment[] =
        "precision mediump float; void main() { gl_FragColor = vec4(1.0); }";
    const char structure_uniform[] =
        "struct Material { vec4 color; }; uniform Material material;"
        "void main() { gl_Position = material.color; }";
    const char structure_uniform_fragment_mismatch[] =
        "precision mediump float; struct Material { vec3 color; };"
        "uniform Material material;"
        "void main() { gl_FragColor = vec4(material.color, 1.0); }";
    const char structure_uniform_layout_vertex[] =
        "struct Material { vec4 color; float roughness; };"
        "uniform Material material;"
        "void main() { gl_Position = material.color; }";
    const char structure_uniform_layout_fragment[] =
        "precision mediump float;"
        "struct Material { vec4 color; vec2 roughness; };"
        "uniform Material material;"
        "void main() { gl_FragColor = material.color; }";
    const char structure_precision_vertex[] =
        "struct Material { lowp vec4 color; }; uniform Material shared_material;"
        "void main() { gl_Position = shared_material.color; }";
    const char structure_precision_fragment[] =
        "struct Material { lowp vec4 color; }; uniform Material shared_material;"
        "void main() { gl_FragColor = shared_material.color; }";
    const char structure_precision_mismatch_fragment[] =
        "struct Material { highp vec4 color; }; uniform Material shared_material;"
        "void main() { gl_FragColor = shared_material.color; }";
    const char invalid_main_parameter[] =
        "void main(float value) { gl_Position = vec4(value); }";
    const char valid_void_main_parameter[] =
        "void main(void) { gl_Position = vec4(0.0); }";
    const char vertex_without_position_write[] =
        "void main() { }";
    const char fragment_without_color_write[] =
        "precision mediump float; void main() { }";
    const char return_only_overload[] =
        "float choose(int value) { return float(value); }"
        "vec2 choose(int value) { return vec2(value); }"
        "void main() { gl_Position = vec4(choose(1)); }";
    const char return_only_prototype_mismatch[] =
        "float choose(int value);"
        "vec2 choose(int value) { return vec2(value); }"
        "void main() { gl_Position = vec4(choose(1), 0.0, 1.0); }";
    const char mismatched_array_call[] =
        "vec4 choose(vec4 values[3]) { return values[0]; }"
        "void main() { vec4 values[2]; gl_Position = choose(values); }";
    const char overloaded_array_call[] =
        "vec4 choose(vec4 values[2]) { return values[0]; }"
        "vec4 choose(vec4 values[3]) { return values[1]; }"
        "void main() { vec4 values[2]; values[0] = vec4(1.0);"
        "gl_Position = choose(values); }";
    const char duplicate_direction_qualifier[] =
        "float choose(in out float value) { return value; }"
        "void main() { float value = 1.0; gl_Position = vec4(choose(value)); }";
    const char duplicate_const_qualifier[] =
        "float choose(const const float value) { return value; }"
        "void main() { gl_Position = vec4(choose(1.0)); }";
    const char direct_recursion[] =
        "float recurse(float value) { return recurse(value); }"
        "void main() { gl_Position = vec4(recurse(1.0)); }";
    const char mutual_recursion[] =
        "float second(float value);"
        "float first(float value) { return second(value); }"
        "float second(float value) { return first(value); }"
        "void main() { gl_Position = vec4(first(1.0)); }";
    const char nonrecursive_overload_chain[] =
        "vec3 expand(vec2 value) { return vec3(value, 0.0); }"
        "vec4 expand(vec3 value) { return vec4(value, 1.0); }"
        "void main() { gl_Position = expand(expand(vec2(0.0))); }";
    const char function_global_collision[] =
        "float shade; float shade(float value) { return value; }"
        "void main() { gl_Position = vec4(shade(1.0)); }";
    const char function_uniform_collision[] =
        "uniform float shade; float shade(float value) { return value; }"
        "void main() { gl_Position = vec4(shade(1.0)); }";
    const char function_struct_collision[] =
        "struct Shade { float value; };"
        "float Shade(float value) { return value; }"
        "void main() { gl_Position = vec4(Shade(1.0)); }";
    const char legal_function_local_shadow[] =
        "float shade(float value) { return value; }"
        "void main() { float shade = 0.5; gl_Position = vec4(shade); }";
    const char duplicate_global_variable[] =
        "float shared_value; int shared_value;"
        "void main() { gl_Position = vec4(0.0); }";
    const char constant_uniform_vertex[] =
        "const int BASE = 1; const int COUNT = BASE + 1; uniform vec4 values[COUNT];"
        "void main() { gl_Position = values[1]; }";
    const char non_integer_array_vertex[] =
        "const float COUNT = 2.0; uniform vec4 values[COUNT];"
        "void main() { gl_Position = values[0]; }";
    const char varying_array_vertex[] =
        "const int COUNT = 2; varying vec3 values[COUNT];"
        "void main() { values[0] = vec3(0.0); values[1] = vec3(1.0);"
        "gl_Position = vec4(0.0); }";
    const char mismatched_varying_array_fragment[] =
        "precision mediump float; varying vec3 values[3];"
        "void main() { gl_FragColor = vec4(values[0], 1.0); }";
    const char matrix_attributes_vertex[] =
        "attribute mat3 matrix_value; attribute vec4 color_value;"
        "void main() { gl_Position = color_value + vec4(matrix_value[0], 0.0); }";
    const char too_many_matrix_attributes[] =
        "attribute mat3 first; attribute mat3 second; attribute mat3 third;"
        "void main() { gl_Position = vec4(first[0] + second[0] + third[0], 1.0); }";
    const char unused_attributes[] =
        "attribute vec4 used_value; attribute mat4 unused0; attribute mat4 unused1;"
        "attribute mat4 unused2; void main() { gl_Position = used_value; }";
    const char initialized_attribute[] =
        "attribute vec4 value = vec4(1.0); void main() { gl_Position = value; }";
    const char written_attribute[] =
        "attribute vec4 value; void main() { value *= 0.5; gl_Position = value; }";
    const char local_attribute[] =
        "void main() { attribute vec4 value; gl_Position = value; }";
    const char local_uniform[] =
        "void main() { uniform float value; gl_Position = vec4(value); }";
    const char local_varying[] =
        "void main() { varying vec2 value; gl_Position = vec4(value, 0.0, 1.0); }";
    const char parameter_uniform[] =
        "float helper(uniform float value) { return value; }"
        "void main() { gl_Position = vec4(helper(1.0)); }";
    const char invalid_const_out_parameter[] =
        "void helper(const out float value) { }"
        "void main() { float value; helper(value); gl_Position = vec4(value); }";
    const char invalid_double_precision_parameter[] =
        "void helper(lowp highp float value) { }"
        "void main() { helper(1.0); gl_Position = vec4(0.0); }";
    const char valid_const_in_parameter[] =
        "float helper(const in float value) { return value; }"
        "void main() { gl_Position = vec4(helper(1.0)); }";
    const char fragment_attribute[] =
        "precision mediump float; attribute vec4 value;"
        "void main() { gl_FragColor = value; }";
    const char mismatched_function_signature[] =
        "vec4 helper(vec3 value); vec4 helper(vec4 value) { return value; }"
        "void main() { gl_Position = helper(vec3(1.0)); }";
    const char duplicate_function_definition[] =
        "vec4 helper(vec4 value) { return value; }"
        "vec4 helper(vec4 other) { return other * 0.5; }"
        "void main() { gl_Position = helper(vec4(1.0)); }";
    const char overloaded_functions[] =
        "vec3 helper(vec3 value) { return value; }"
        "vec4 helper(vec4 value) { return vec4(helper(value.xyz), value.w); }"
        "void main() { gl_Position = helper(vec4(1.0)); }";
    const char mismatched_function_array[] =
        "vec4 helper(vec4 value[2]);"
        "vec4 helper(vec4 value[3]) { return value[0]; }"
        "void main() { vec4 values[2]; values[0] = vec4(1.0); values[1] = vec4(0.0);"
        "gl_Position = helper(values); }";
    const char unresolved_function_call[] =
        "void main() { gl_Position = mystery(vec4(1.0)); }";
    const char mismatched_function_arity[] =
        "vec4 helper(vec4 value) { return value; }"
        "void main() { gl_Position = helper(vec4(1.0), vec4(0.0)); }";
    const char mismatched_function_type[] =
        "vec4 helper(vec4 value) { return value; }"
        "void main() { gl_Position = helper(vec3(1.0)); }";
    const char constructor_swizzle_overload[] =
        "vec4 helper(vec3 value) { return vec4(value, 1.0); }"
        "vec4 helper(vec4 value) { return value; }"
        "void main() { gl_Position = helper(vec4(1.0).xyz); }";
    const char mismatched_variable_type[] =
        "vec4 helper(vec4 value) { return value; }"
        "void main() { vec3 value = vec3(1.0); gl_Position = helper(value); }";
    const char variable_swizzle_overload[] =
        "attribute vec4 position;"
        "vec4 helper(vec3 value) { return vec4(value, 1.0); }"
        "void main() { gl_Position = helper(position.xyz); }";
    const char array_element_overload[] =
        "vec4 helper(vec4 value) { return value; }"
        "void main() { vec4 values[2]; values[0] = vec4(1.0);"
        "gl_Position = helper(values[0]); }";
    const char mismatched_compound_type[] =
        "vec4 helper(vec4 value) { return value; }"
        "void main() { vec3 first = vec3(1.0); vec3 second = vec3(0.5);"
        "gl_Position = helper(first + second); }";
    const char matching_compound_type[] =
        "vec4 helper(vec3 value) { return vec4(value, 1.0); }"
        "void main() { vec3 first = vec3(1.0); vec3 second = vec3(0.5);"
        "gl_Position = helper((first + second)); }";
    const char mismatched_nested_call[] =
        "vec3 inner() { return vec3(1.0); }"
        "vec4 outer(vec4 value) { return value; }"
        "void main() { gl_Position = outer(inner()); }";
    const char nested_call_swizzle[] =
        "vec4 inner() { return vec4(1.0); }"
        "vec4 outer(vec3 value) { return vec4(value, 1.0); }"
        "void main() { gl_Position = outer(inner().xyz); }";
    const char scalar_vector_expression[] =
        "vec4 helper(vec3 value) { return vec4(value, 1.0); }"
        "void main() { vec3 value = vec3(0.5); gl_Position = helper(value * 0.5); }";
    const char mismatched_matrix_vector_expression[] =
        "vec4 helper(vec4 value) { return value; }"
        "void main() { mat3 matrix = mat3(1.0); vec3 value = vec3(1.0);"
        "gl_Position = helper(matrix * value); }";
    const char matching_matrix_vector_expression[] =
        "vec4 helper(vec3 value) { return vec4(value, 1.0); }"
        "void main() { mat3 matrix = mat3(1.0); vec3 value = vec3(1.0);"
        "gl_Position = helper(matrix * value); }";
    const char scalar_comparison_expression[] =
        "vec4 helper(bool value) { return value ? vec4(1.0) : vec4(0.0); }"
        "void main() { gl_Position = helper(1.0 < 2.0); }";
    const char mismatched_comparison_expression[] =
        "vec4 helper(vec4 value) { return value; }"
        "void main() { gl_Position = helper(1.0 == 2.0); }";
    const char matching_conditional_expression[] =
        "vec4 helper(vec3 value) { return vec4(value, 1.0); }"
        "void main() { bool selected = true;"
        "gl_Position = helper(selected ? vec3(1.0) : vec3(0.0)); }";
    const char mismatched_conditional_expression[] =
        "vec4 helper(vec4 value) { return value; }"
        "void main() { bool selected = false;"
        "gl_Position = helper(selected ? vec3(1.0) : vec3(0.0)); }";
    const char logical_expression[] =
        "vec4 helper(bool value) { return value ? vec4(1.0) : vec4(0.0); }"
        "void main() { bool first = true; bool second = false;"
        "gl_Position = helper(first && !second); }";
    const char mismatched_return_type[] =
        "vec3 helper() { return vec4(1.0); }"
        "void main() { gl_Position = vec4(helper(), 1.0); }";
    const char void_returning_value[] =
        "void helper() { return 1.0; }"
        "void main() { helper(); gl_Position = vec4(0.0); }";
    const char empty_nonvoid_return[] =
        "float helper() { return; }"
        "void main() { gl_Position = vec4(helper()); }";
    const char matching_nested_return[] =
        "vec3 helper(bool selected) { if (selected) { return vec3(1.0); }"
        "return vec3(0.0); }"
        "void main() { gl_Position = vec4(helper(true), 1.0); }";
    const char invalid_if_condition[] =
        "void main() { if (1.0) gl_Position = vec4(1.0); else gl_Position = vec4(0.0); }";
    const char invalid_while_condition[] =
        "void main() { int value = 0; while (value) { value = value + 1; }"
        "gl_Position = vec4(0.0); }";
    const char invalid_for_condition[] =
        "void main() { for (int value = 0; 1.0; value = value + 1) { break; }"
        "gl_Position = vec4(0.0); }";
    const char valid_empty_for_condition[] =
        "void main() { for (int value = 0; ; value = value + 1) { break; }"
        "gl_Position = vec4(0.0); }";
    const char invalid_initializer_type[] =
        "void main() { vec3 value = vec4(1.0); gl_Position = vec4(value, 1.0); }";
    const char invalid_assignment_type[] =
        "void main() { float value; value = vec2(1.0); gl_Position = vec4(value); }";
    const char invalid_swizzle_assignment[] =
        "void main() { vec4 value = vec4(0.0); value.xy = vec3(1.0);"
        "gl_Position = value; }";
    const char invalid_array_element_assignment[] =
        "void main() { vec3 values[2]; values[0] = vec4(1.0);"
        "gl_Position = vec4(values[0], 1.0); }";
    const char valid_compound_assignments[] =
        "void main() { vec3 value = vec3(1.0); int count = 5;"
        "value *= 0.5; count += 2; gl_Position = vec4(value, float(count)); }";
    const char invalid_compound_assignment[] =
        "void main() { vec3 value = vec3(1.0); value += vec4(1.0);"
        "gl_Position = vec4(value, 1.0); }";
    const char invalid_conditional_condition[] =
        "void main() { vec3 value = 1.0 ? vec3(1.0) : vec3(0.0);"
        "gl_Position = vec4(value, 1.0); }";
    const char invalid_conditional_branches[] =
        "vec3 helper(bool selected) { return selected ? vec3(1.0) : vec4(0.0); }"
        "void main() { gl_Position = vec4(helper(true), 1.0); }";
    const char invalid_argument_conditional[] =
        "vec4 helper(vec3 value) { return vec4(value, 1.0); }"
        "void main() { bool selected = true;"
        "gl_Position = helper(selected ? vec3(1.0) : vec4(0.0)); }";
    const char valid_nested_conditional[] =
        "void main() { bool first = true; bool second = false;"
        "vec3 value = first ? (second ? vec3(0.0) : vec3(1.0)) : vec3(0.5);"
        "gl_Position = vec4(value, 1.0); }";
    const char invalid_nested_conditional[] =
        "void main() { bool first = true;"
        "vec3 value = first ? (1 ? vec3(0.0) : vec3(1.0)) : vec3(0.5);"
        "gl_Position = vec4(value, 1.0); }";
    const char invalid_modulus_operator[] =
        "void main() { int value = 5; value %= 2; gl_Position = vec4(float(value)); }";
    const char invalid_bitwise_operators[] =
        "void main() { int value = (1 << 2) | 1; gl_Position = vec4(float(value)); }";
    const char invalid_logical_operands[] =
        "void main() { bool value = 1.0 && 2.0;"
        "gl_Position = vec4(value ? 1.0 : 0.0); }";
    const char invalid_relational_vectors[] =
        "void main() { bool value = vec2(1.0) < vec2(2.0);"
        "gl_Position = vec4(value ? 1.0 : 0.0); }";
    const char invalid_mixed_equality[] =
        "void main() { bool value = 1.0 == 1;"
        "gl_Position = vec4(value ? 1.0 : 0.0); }";
    const char invalid_sampler_equality[] =
        "uniform sampler2D first; uniform sampler2D second;"
        "void main() { bool value = first == second;"
        "gl_Position = vec4(value ? 1.0 : 0.0); }";
    const char invalid_logical_not_vector[] =
        "void main() { if (false) { bool value = !vec2(1.0); }"
        "gl_Position = vec4(0.0); }";
    const char invalid_boolean_increment[] =
        "void main() { bool value = true; if (false) value++;"
        "gl_Position = vec4(0.0); }";
    const char invalid_constructor_increment[] =
        "void main() { if (false) vec2(1.0)++; gl_Position = vec4(0.0); }";
    const char invalid_repeated_swizzle_increment[] =
        "void main() { vec4 value = vec4(0.0); if (false) value.xx++;"
        "gl_Position = value; }";
    const char valid_numeric_increment[] =
        "void main() { float scalar = 0.0; vec2 vector = vec2(0.0); mat2 matrix = mat2(1.0);"
        "++scalar; vector++; ++matrix; gl_Position = vec4(vector, scalar, matrix[0].x); }";
    const char invalid_boolean_negation[] =
        "void main() { if (false) { bool flag = true; float value = -flag; }"
        "gl_Position = vec4(0.0); }";
    const char invalid_float_array_subscript[] =
        "void main() { vec3 values[2]; if (false) values[1.0] = vec3(0.0);"
        "gl_Position = vec4(0.0); }";
    const char invalid_bool_vector_subscript[] =
        "void main() { vec4 value = vec4(1.0); bool index = false;"
        "if (false) value[index] = 0.0; gl_Position = value; }";
    const char invalid_vector_subscript[] =
        "void main() { mat2 value = mat2(1.0); ivec2 index = ivec2(0);"
        "if (false) value[index] = vec2(0.0); gl_Position = vec4(0.0); }";
    const char valid_integer_subscripts[] =
        "void main() { vec2 values[2]; int index = 0; values[index + 1] = vec2(1.0);"
        "mat2 matrix = mat2(1.0);"
        "gl_Position = vec4(values[1] * matrix[index], 0.0, 1.0); }";
    const char invalid_scalar_base_subscript[] =
        "void main() { float value = 1.0; if (false) value = value[0];"
        "gl_Position = vec4(value); }";
    const char invalid_struct_base_subscript[] =
        "struct Item { float value; }; void main() { Item item;"
        "if (false) item.value = item[0].value; gl_Position = vec4(0.0); }";
    const char invalid_sampler_base_subscript[] =
        "uniform sampler2D image; void main() { if (false) { vec4 other = image[0]; }"
        "gl_Position = vec4(0.0); }";
    const char invalid_negative_vector_subscript[] =
        "void main() { vec3 value = vec3(1.0); if (false) value.x = value[-1];"
        "gl_Position = vec4(value, 1.0); }";
    const char invalid_vector_bound_subscript[] =
        "void main() { vec2 value = vec2(1.0); if (false) value.x = value[2];"
        "gl_Position = vec4(value, 0.0, 1.0); }";
    const char invalid_matrix_bound_subscript[] =
        "void main() { mat3 value = mat3(1.0); if (false) value[0] = value[3];"
        "gl_Position = vec4(value[0], 1.0); }";
    const char invalid_array_bound_subscript[] =
        "void main() { vec2 values[2]; if (false) values[0] = values[2];"
        "gl_Position = vec4(0.0); }";
    const char valid_nested_dynamic_subscripts[] =
        "void main() { vec3 values[2]; int first = 1; int second = 2;"
        "values[first] = vec3(0.0, 0.0, 1.0);"
        "gl_Position = vec4(values[first][second]); }";
    const char invalid_duplicate_parameters[] =
        "float helper(float value, vec2 value) { return value.x; }"
        "void main() { gl_Position = vec4(0.0); }";
    const char invalid_parameter_local_redeclaration[] =
        "float helper(float value) { if (false) { return 0.0; }"
        "float value = 1.0; return value; }"
        "void main() { gl_Position = vec4(helper(0.0)); }";
    const char invalid_duplicate_locals[] =
        "void main() { float value = 0.0; if (false) value += 1.0;"
        "vec2 value = vec2(1.0); gl_Position = vec4(value, 0.0, 1.0); }";
    const char invalid_duplicate_declarators[] =
        "void main() { float value = 0.0, value = 1.0; gl_Position = vec4(value); }";
    const char valid_nested_local_shadow[] =
        "float helper(float value) { { vec2 value = vec2(0.25);"
        "if (value.x < 0.0) return value.x; } return value; }"
        "void main() { float value = helper(0.5); { vec4 value = vec4(0.25);"
        "gl_Position = value; } }";
    const char valid_sibling_local_names[] =
        "void main() { { float value = 0.0; } { vec2 value = vec2(0.5);"
        "gl_Position = vec4(value, 0.0, 1.0); } }";
    const char valid_reused_for_initializer[] =
        "void main() { float sum = 0.0; for (int index = 0; index < 2; ++index) sum += 1.0;"
        "for (int index = 0; index < 2; ++index) sum += 1.0; gl_Position = vec4(sum); }";
    const char invalid_duplicate_for_declarators[] =
        "void main() { for (int index = 0, index = 1; index < 2; ++index) { }"
        "gl_Position = vec4(0.0); }";
    const char valid_shadowed_overload_after_block[] =
        "float select_value(float value) { return value; }"
        "vec2 select_value(vec2 value) { return value; }"
        "void main() { float value = 0.25; { vec2 value = vec2(0.5);"
        "value += vec2(0.25); } float result = select_value(value);"
        "gl_Position = vec4(result); }";
    const char valid_assignment_after_shadow[] =
        "void main() { float value = 0.25; { vec3 value = vec3(0.5); value *= 2.0; }"
        "value = 0.75; gl_Position = vec4(value); }";
    const char valid_sibling_shadow_lookup[] =
        "void main() { float value = 0.25; { vec2 value = vec2(0.5); }"
        "{ float result = value; gl_Position = vec4(result); } }";
    const char valid_for_shadow_lookup[] =
        "float helper(float value) { for (int value = 0; value < 1; ++value) { }"
        "return value; } void main() { gl_Position = vec4(helper(0.5)); }";
    const char invalid_later_declarator_initializer[] =
        "void main() { float first = 0.0, second = 1.0;"
        "vec2 value = second; gl_Position = vec4(value, 0.0, 1.0); }";
    const char invalid_vector_later_declarator_initializer[] =
        "void main() { vec2 first = vec2(0.0), second = vec2(1.0);"
        "float value = second; gl_Position = vec4(value); }";
    const char invalid_later_declarator_overload[] =
        "float select_value(float value) { return value; }"
        "vec2 select_value(vec2 value) { return value; }"
        "void main() { vec2 first = vec2(0.0), second = vec2(1.0);"
        "float value = select_value(second); gl_Position = vec4(value); }";
    const char invalid_later_array_declarator_bound[] =
        "void main() { vec2 first[2], second[3]; if (false) first[0] = second[3];"
        "gl_Position = vec4(0.0); }";
    const char valid_later_array_declarator[] =
        "void main() { vec2 first[2], second[3]; second[2] = vec2(0.5);"
        "first[1] = second[2]; gl_Position = vec4(first[1], 0.0, 1.0); }";
    const char valid_qualified_declarator_list[] =
        "void main() { const highp float first = 0.25, second = 0.5;"
        "gl_Position = vec4(first + second); }";
    const char invalid_dead_branch_identifier[] =
        "void main() { if (false) gl_Position = misspelled_value;"
        "else gl_Position = vec4(0.0); }";
    const char invalid_identifier_after_block[] =
        "void main() { { vec4 temporary = vec4(1.0); }"
        "gl_Position = temporary; }";
    const char invalid_identifier_after_for[] =
        "void main() { for (int index = 0; index < 1; ++index) { }"
        "gl_Position = vec4(float(index)); }";
    const char invalid_identifier_in_initializer[] =
        "void main() { float value = unknown_scalar + 1.0;"
        "gl_Position = vec4(value); }";
    const char invalid_parameter_from_other_function[] =
        "float first(float private_value) { return private_value; }"
        "float second() { return private_value; }"
        "void main() { gl_Position = vec4(second()); }";
    const char valid_identifier_categories[] =
        "struct Item { vec2 field; }; uniform float scale;"
        "float helper(float value) { return value; }"
        "void main() { Item item; item.field = vec2(0.5);"
        "// ignored_identifier\n"
        "float exponent = 1.0e-2; gl_Position = vec4(item.field.xy * scale,"
        "helper(exponent), float(gl_MaxDrawBuffers)); }";
    const char valid_local_structure_identifiers[] =
        "void main() { struct LocalItem { vec2 field; }; LocalItem item;"
        "item.field = vec2(0.5); gl_Position = vec4(item.field, 0.0, 1.0); }";
    const char invalid_mixed_swizzle[] =
        "void main() { vec4 value = vec4(1.0);"
        "if (false) value = vec4(value.xg, 0.0, 1.0); gl_Position = value; }";
    const char invalid_dimension_swizzle[] =
        "void main() { vec2 value = vec2(1.0);"
        "if (false) gl_Position = vec4(value.z); else gl_Position = vec4(0.0); }";
    const char invalid_long_swizzle[] =
        "void main() { vec4 value = vec4(1.0);"
        "if (false) gl_Position = vec4(value.xyzwx); else gl_Position = vec4(0.0); }";
    const char invalid_duplicate_swizzle_write[] =
        "void main() { vec4 value = vec4(1.0); if (false) value.xx = vec2(0.0);"
        "gl_Position = value; }";
    const char valid_duplicate_swizzle_read[] =
        "void main() { vec4 value = vec4(1.0); gl_Position = value.xxxx; }";
    const char invalid_boolean_arithmetic[] =
        "void main() { bool first = true; bool second = false;"
        "if (false) first = first + second; gl_Position = vec4(0.0); }";
    const char invalid_bvec_arithmetic[] =
        "void main() { bvec2 first = bvec2(true); bvec2 second = bvec2(false);"
        "if (false) first = first * second; gl_Position = vec4(0.0); }";
    const char invalid_structure_arithmetic[] =
        "struct Value { float field; };"
        "void main() { Value first; Value second; if (false) first = first + second;"
        "gl_Position = vec4(0.0); }";
    const char invalid_matrix_vector_dimensions[] =
        "void main() { mat2 matrix = mat2(1.0); vec3 value = vec3(1.0);"
        "if (false) value = matrix * value; gl_Position = vec4(value, 1.0); }";
    const char valid_arithmetic_combinations[] =
        "void main() { mat2 matrix = mat2(1.0); vec2 vector = vec2(1.0);"
        "ivec2 integers = ivec2(4) / 2; matrix = 1.0 + matrix; vector = matrix * vector;"
        "gl_Position = vec4(vector, float(integers.x), 1.0); }";
    const char invalid_out_literal[] =
        "void helper(out float value) { value = 1.0; }"
        "void main() { helper(0.0); gl_Position = vec4(0.0); }";
    const char invalid_out_constructor[] =
        "void helper(out vec2 value) { value = vec2(1.0); }"
        "void main() { helper(vec2(0.0)); gl_Position = vec4(0.0); }";
    const char invalid_inout_expression[] =
        "void helper(inout float value) { value += 1.0; }"
        "void main() { float value = 0.0; helper(value + 1.0); gl_Position = vec4(value); }";
    const char invalid_out_repeated_swizzle[] =
        "void helper(out vec2 value) { value = vec2(1.0); }"
        "void main() { vec4 value = vec4(0.0); helper(value.xx); gl_Position = value; }";
    const char invalid_out_const[] =
        "void helper(out float value) { value = 1.0; }"
        "void main() { const float value = 0.0; helper(value); gl_Position = vec4(value); }";
    const char invalid_inout_uniform[] =
        "uniform float value; void helper(inout float target) { target += 1.0; }"
        "void main() { helper(value); gl_Position = vec4(value); }";
    const char invalid_out_read_only_builtin[] =
        "void helper(out vec4 value) { value = vec4(1.0); }"
        "void main() { helper(gl_DepthRange); gl_Position = vec4(0.0); }";
    const char valid_vertex_out_varying[] =
        "varying vec2 value; void helper(out vec2 target) { target = vec2(0.5); }"
        "void main() { helper(value); gl_Position = vec4(value, 0.0, 1.0); }";
    const char invalid_fragment_out_varying[] =
        "precision mediump float; varying vec2 value;"
        "void helper(out vec2 target) { target = vec2(0.5); }"
        "void main() { helper(value); gl_FragColor = vec4(value, 0.0, 1.0); }";
    const char valid_fragment_varying[] =
        "precision mediump float; varying vec2 value;"
        "void main() { gl_FragColor = vec4(value, 0.0, 1.0); }";
    const char valid_out_lvalues[] =
        "struct Item { vec2 field; };"
        "void set_value(out vec2 value) { value = vec2(0.5); }"
        "void main() { Item item; vec2 values[2]; set_value(item.field);"
        "set_value(values[1]); set_value(gl_Position.xy);"
        "gl_Position = vec4(item.field + values[1], gl_Position.xy); }";
    const char mismatched_parameter_modes[] =
        "void helper(out float value); void helper(in float value) { }"
        "void main() { float value = 0.0; helper(value); gl_Position = vec4(value); }";
    const char mismatched_builtin_result[] =
        "vec4 helper(vec4 value) { return value; }"
        "void main() { gl_Position = helper(normalize(vec3(1.0))); }";
    const char matching_builtin_results[] =
        "vec4 vector_result(vec3 value) { return vec4(value, 1.0); }"
        "vec4 scalar_result(float value) { return vec4(value); }"
        "vec4 relation_result(bvec3 value) { return vec4(value.x ? 1.0 : 0.0); }"
        "void main() { vec3 first = vec3(1.0); vec3 second = vec3(0.5);"
        "gl_Position = vector_result(normalize(first)) + scalar_result(dot(first, second)) +"
        "relation_result(lessThan(first, second)); }";
    const char texture_builtin_result[] =
        "uniform sampler2D image;"
        "vec4 helper(vec4 value) { return value; }"
        "void main() { gl_Position = helper(texture2D(image, vec2(0.5))); }";
    const char step_builtin_results[] =
        "vec4 vector_result(vec3 value) { return vec4(value, 1.0); }"
        "void main() { vec3 value = vec3(0.25);"
        "gl_Position = vector_result(step(0.5, value)) +"
        "vector_result(smoothstep(0.0, 1.0, value)); }";
    const char invalid_builtin_arity[] =
        "void main() { gl_Position = vec4(dot(vec3(1.0))); }";
    const char invalid_texture_arity[] =
        "uniform sampler2D image;"
        "void main() { gl_Position = texture2D(image); }";
    const char unresolved_fake_builtin[] =
        "void main() { gl_Position = texture2DBias(vec4(1.0)); }";
    const char invalid_builtin_types[] =
        "void main() { gl_Position = vec4(dot(vec2(1.0), vec3(1.0))); }";
    const char invalid_texture_types[] =
        "uniform samplerCube image;"
        "void main() { gl_Position = texture2D(image, vec2(0.5)); }";
    const char valid_builtin_types[] =
        "void main() { vec3 a = vec3(1.0); vec3 b = vec3(0.0);"
        "gl_Position = vec4(cross(a, b) + normalize(a) * distance(a, b), dot(a, b)); }";
    const char invalid_unary_builtin_type[] =
        "void main() { gl_Position = vec4(sin(bvec4(true))); }";
    const char invalid_pow_builtin_type[] =
        "void main() { gl_Position = vec4(pow(vec4(2.0), 3.0)); }";
    const char invalid_mix_builtin_type[] =
        "void main() { gl_Position = vec4(mix(vec3(0.0), vec2(1.0), 0.5), 1.0); }";
    const char invalid_step_builtin_type[] =
        "void main() { gl_Position = vec4(step(vec3(0.5), 0.75)); }";
    const char valid_scalar_and_integer_builtins[] =
        "void main() { float magnitude = length(2.0);"
        "bvec3 compared = lessThan(ivec3(0, 2, 4), ivec3(1, 1, 5));"
        "gl_Position = vec4(magnitude, compared.x ? 1.0 : 0.0, 0.0, 1.0); }";
    const char invalid_extra_constructor_components[] =
        "void main() { gl_Position = vec4(vec2(1.0), vec3(2.0)); }";
    const char valid_scalar_from_vector_constructor[] =
        "void main() { float value = float(vec2(1.0)); gl_Position = vec4(value); }";
    const char valid_builtin_overload[] =
        "float abs(int value) { return float(value); }"
        "void main() { gl_Position = vec4(abs(2)); }";
    const char valid_builtin_before_overload[] =
        "void main() { gl_Position = vec4(abs(-0.5)); }"
        "float abs(int value) { return float(value); }";
    const char invalid_builtin_redeclaration[] =
        "float abs(float value) { return value; }"
        "void main() { gl_Position = vec4(abs(-0.5)); }";
    const char valid_vector_from_matrix[] =
        "void main() { gl_Position = vec4(mat2(1.0)); }";
    const char invalid_mixed_matrix_constructor[] =
        "void main() { mat3 value = mat3(mat2(1.0), vec4(1.0), 1.0);"
        "gl_Position = vec4(value[0], 1.0); }";
    const char valid_exact_constructor[] =
        "void main() { gl_Position = vec4(vec2(1.0), 2.0, 3.0); }";
    const char invalid_vertex_texture_bias[] =
        "uniform sampler2D image;"
        "void main() { gl_Position = texture2D(image, vec2(0.5), 1.0); }";
    const char valid_vertex_texture_lod[] =
        "uniform sampler2D image;"
        "void main() { gl_Position = texture2DLod(image, vec2(0.5), 1.0); }";
    const char valid_fragment_texture_bias[] =
        "precision mediump float; uniform sampler2D image;"
        "void main() { gl_FragColor = texture2D(image, vec2(0.5), 1.0); }";
    const char invalid_vertex_discard[] =
        "void main() { discard; gl_Position = vec4(0.0); }";
    const char invalid_vertex_fragment_input[] =
        "void main() { gl_Position = gl_FragCoord; }";
    const char invalid_fragment_vertex_output[] =
        "precision mediump float;"
        "void main() { gl_FragColor = gl_Position; }";
    const char invalid_fragment_attribute[] =
        "precision mediump float; attribute vec4 color;"
        "void main() { gl_FragColor = color; }";
    const char invalid_uniform_initializer[] =
        "uniform vec4 position = vec4(0.0);"
        "void main() { gl_Position = position; }";
    const char invalid_varying_initializer[] =
        "varying vec4 color = vec4(1.0);"
        "void main() { gl_Position = color; }";
    const char invalid_mutable_global_initializer[] =
        "float first = 1.0; float second = first;"
        "void main() { gl_Position = vec4(second); }";
    const char invalid_function_global_initializer[] =
        "float helper() { return 1.0; } float value = helper();"
        "void main() { gl_Position = vec4(value); }";
    const char invalid_call_before_declaration[] =
        "void main() { gl_Position = helper(); }"
        "vec4 helper() { return vec4(1.0); }";
    const char valid_call_after_prototype[] =
        "vec4 helper();"
        "void main() { gl_Position = helper(); }"
        "vec4 helper() { return vec4(1.0); }";
    const char invalid_duplicate_function_prototype[] =
        "vec4 helper(); vec4 helper();"
        "void main() { gl_Position = helper(); }"
        "vec4 helper() { return vec4(1.0); }";
    const char invalid_direction_qualifier_overload[] =
        "float helper(in float value) { return value; }"
        "float helper(out float value) { value = 1.0; return value; }"
        "void main() { float value = 0.0; gl_Position = vec4(helper(value)); }";
    const char invalid_function_precision_match[] =
        "highp float helper(lowp float value);"
        "mediump float helper(highp float value) { return value; }"
        "void main() { gl_Position = vec4(helper(1.0)); }";
    const char invalid_default_function_precision_match[] =
        "precision mediump float; float helper(float value);"
        "highp float helper(highp float value) { return value; }"
        "void main() { gl_Position = vec4(helper(1.0)); }";
    const char valid_vertex_default_function_precision[] =
        "float helper(float value);"
        "highp float helper(highp float value) { return value; }"
        "void main() { gl_Position = vec4(helper(1.0)); }";
    const char invalid_const_parameter_match[] =
        "float helper(const float value);"
        "float helper(float value) { return value; }"
        "void main() { gl_Position = vec4(helper(1.0)); }";
    const char invalid_global_after_use[] =
        "void main() { gl_Position = vec4(later); }"
        "float later = 1.0;";
    const char valid_global_before_use[] =
        "float earlier = 1.0;"
        "void main() { gl_Position = vec4(earlier); }";
    const char valid_constant_global_initializer[] =
        "const int first = 1; const int second = first + 1;"
        "float value = float(second) * 0.25;"
        "void main() { gl_Position = vec4(value); }";
    const char invalid_fragment_varying_write[] =
        "precision mediump float; varying vec3 color;"
        "void main() { color = vec3(1.0); gl_FragColor = vec4(color, 1.0); }";
    const char invalid_depth_range_write[] =
        "void main() { gl_DepthRange.near = 0.0; gl_Position = vec4(1.0); }";
    const char invalid_frag_coord_write[] =
        "precision mediump float;"
        "void main() { gl_FragCoord.x = 0.0; gl_FragColor = vec4(1.0); }";
    const char valid_implementation_constant[] =
        "const int count = gl_MaxVertexAttribs; float values[count];"
        "void main() { values[count - 1] = 1.0; gl_Position = vec4(values[count - 1]); }";
    const char invalid_implementation_constant_write[] =
        "void main() { gl_MaxVertexAttribs = 1; gl_Position = vec4(1.0); }";
    const char fragment_data_output[] =
        "precision mediump float;"
        "void main() { gl_FragData[gl_MaxDrawBuffers - 1] = "
        "vec4(0.2, 0.4, 0.6, 1.0); }";
    const char mixed_fragment_outputs[] =
        "precision mediump float;"
        "void main() { gl_FragColor = vec4(1.0); gl_FragData[0] = vec4(1.0); }";
    const char out_of_range_fragment_output[] =
        "precision mediump float;"
        "void main() { gl_FragData[1] = vec4(1.0); }";
    const char dynamic_fragment_output[] =
        "precision mediump float; uniform int output_index;"
        "void main() { gl_FragData[output_index] = vec4(1.0); }";
    const char invariant_vertex[] =
        "invariant gl_Position; invariant varying vec3 stable_color;"
        "void main() { stable_color = vec3(1.0); gl_Position = vec4(1.0); }";
    const char invariant_fragment[] =
        "precision mediump float; invariant gl_FragCoord;"
        "invariant varying vec3 stable_color;"
        "void main() { gl_FragColor = vec4(stable_color, 1.0); }";
    const char noninvariant_fragment[] =
        "precision mediump float; varying vec3 stable_color;"
        "void main() { gl_FragColor = vec4(stable_color, 1.0); }";
    const char inactive_invariant_vertex[] =
        "invariant varying vec3 unused_stable_color;"
        "void main() { gl_Position = vec4(1.0); }";
    const char inactive_noninvariant_fragment[] =
        "precision mediump float; varying vec3 unused_stable_color;"
        "void main() { gl_FragColor = vec4(1.0); }";
    const char pragma_invariant_vertex[] =
        "#pragma STDGL invariant(all)\n"
        "varying vec3 stable_color;"
        "void main() { stable_color = vec3(1.0); gl_Position = vec4(1.0); }";
    const char pragma_invariant_fragment[] =
        "#pragma STDGL invariant(all)\n"
        "precision mediump float;"
        "invariant gl_FragCoord, gl_PointCoord;"
        "invariant varying vec3 stable_color;"
        "void main() { gl_FragColor = vec4(stable_color, 1.0); }";
    const char invalid_local_invariant[] =
        "void main() { invariant float local_value; gl_Position = vec4(1.0); }";
    const char invalid_uniform_invariant[] =
        "invariant uniform vec4 invalid_value;"
        "void main() { gl_Position = invalid_value; }";
    const char reserved_gl_variable[] =
        "float gl_user_value; void main() { gl_Position = vec4(gl_user_value); }";
    const char reserved_gl_function[] =
        "vec4 gl_custom() { return vec4(1.0); }"
        "void main() { gl_Position = gl_custom(); }";
    const char reserved_gl_struct_member[] =
        "struct State { float gl_member; };"
        "void main() { State state; state.gl_member = 1.0;"
        "gl_Position = vec4(state.gl_member); }";
    const char commented_reserved_gl_name[] =
        "// float gl_not_a_declaration;\n"
        "void main() { gl_Position = vec4(1.0); }";
    const char invalid_break_outside_loop[] =
        "void main() { break; gl_Position = vec4(1.0); }";
    const char invalid_continue_in_called_function[] =
        "void invalid_control() { continue; }"
        "void main() { for (int i = 0; i < 1; i++) invalid_control();"
        "gl_Position = vec4(1.0); }";
    const char valid_nested_loop_control[] =
        "void main() { int value = 0; do { value++; if (value < 2) continue;"
        "for (int i = 0; i < 2; i++) if (i == 1) break; } while (value < 2);"
        "gl_Position = vec4(float(value)); }";
    const char commented_loop_control[] =
        "// break; continue;\n"
        "void main() { /* break; */ gl_Position = vec4(1.0); }";
    const char invalid_uniform_write[] =
        "uniform vec4 value;"
        "void main() { value.xyz += vec3(1.0); gl_Position = value; }";
    const char invalid_uniform_array_write[] =
        "uniform vec4 values[2];"
        "void main() { values[1] = vec4(1.0); gl_Position = values[1]; }";
    const char multi_uniform_vertex[] =
        "uniform vec4 first, shared;"
        "void main() { gl_Position = first + shared; }";
    const char conflicting_second_uniform[] =
        "precision mediump float; uniform vec3 shared;"
        "void main() { gl_FragColor = vec4(shared, 1.0); }";
    const char invalid_second_uniform_array[] =
        "uniform vec4 first, values[0];"
        "void main() { gl_Position = first; }";
    const char multi_varying_vertex[] =
        "varying vec3 first, shared[2];"
        "void main() { first = vec3(0.0); shared[0] = vec3(0.0);"
        "shared[1] = vec3(1.0); gl_Position = vec4(0.0); }";
    const char conflicting_second_varying[] =
        "precision mediump float; varying vec3 first, shared[3];"
        "void main() { gl_FragColor = vec4(first + shared[0], 1.0); }";
    const char minimum_uniform_vertex[] =
        "uniform vec4 vectors[" STRINGIFY_VALUE(MESAGL_MAX_VERTEX_UNIFORM_VECTORS) "];"
        "void main() { gl_Position = vectors["
        STRINGIFY_VALUE(MESAGL_MAX_VERTEX_UNIFORM_VECTORS - 1) "]; }";
    const char minimum_uniform_fragment[] =
        "precision mediump float; uniform vec4 colors["
        STRINGIFY_VALUE(MESAGL_MAX_FRAGMENT_UNIFORM_VECTORS) "];"
        "void main() { gl_FragColor = colors["
        STRINGIFY_VALUE(MESAGL_MAX_FRAGMENT_UNIFORM_VECTORS - 1) "]; }";
    const char excessive_vertex_uniforms[] =
        "uniform mat4 matrices["
        STRINGIFY_VALUE(MESAGL_MAX_VERTEX_UNIFORM_VECTORS / 4 + 1) "];"
        "void main() { gl_Position = matrices["
        STRINGIFY_VALUE(MESAGL_MAX_VERTEX_UNIFORM_VECTORS / 4) "] * vec4(1.0); }";
    const char excessive_fragment_uniforms[] =
        "precision mediump float; uniform mat4 matrices["
        STRINGIFY_VALUE(MESAGL_MAX_FRAGMENT_UNIFORM_VECTORS / 4 + 1) "];"
        "void main() { gl_FragColor = matrices["
        STRINGIFY_VALUE(MESAGL_MAX_FRAGMENT_UNIFORM_VECTORS / 4) "] * vec4(1.0); }";
    char log[128];

    if (!context)
        return 1;
    ntglMakeCurrent(context);
    if (link_status(valid_vertex, valid_fragment, log, sizeof(log)) != 1) {
        fprintf(stderr, "valid link: %s\n", log);
        return 2;
    }
    if (!unreachable_resource_test(unreachable_resource_vertex,
                                   unreachable_resource_fragment))
        return 229;
    if (link_status(minimum_uniform_vertex, minimum_uniform_fragment, log, sizeof(log)) != 1) {
        fprintf(stderr, "minimum uniform limits: %s\n", log);
        return 85;
    }
    if (link_status(excessive_vertex_uniforms, simple_fragment, log, sizeof(log)) != 0 ||
        !strstr(log, "uniform vector limit"))
        return 86;
    if (link_status(valid_vertex, excessive_fragment_uniforms, log, sizeof(log)) != 0 ||
        !strstr(log, "uniform vector limit"))
        return 87;
    if (link_status(valid_vertex, mismatched_fragment, log, sizeof(log)) != 0 ||
        !strstr(log, "varying"))
        return 3;
    if (link_status(uniform_vertex, uniform_fragment, log, sizeof(log)) != 0 ||
        !strstr(log, "uniform")) {
        fprintf(stderr, "uniform mismatch link: %s\n", log);
        return 4;
    }
    if (link_status(inactive_uniform_vertex, inactive_uniform_fragment, log,
                    sizeof(log)) != 0 || !strstr(log, "uniform"))
        return 105;
    if (link_status(inactive_uniform_array_vertex, inactive_uniform_array_fragment, log,
                    sizeof(log)) != 0 || !strstr(log, "uniform"))
        return 106;
    if (link_status(precision_uniform_vertex, precision_uniform_fragment, log,
                    sizeof(log)) != 0 || !strstr(log, "precision"))
        return 107;
    if (link_status(too_many_varyings, simple_fragment, log, sizeof(log)) != 1)
        return 5;
    if (link_status(too_many_active_varyings, too_many_active_varyings_fragment, log,
                    sizeof(log)) != 0 || !strstr(log, "varying vector limit"))
        return 102;
    if (link_status(structure_uniform, simple_fragment, log, sizeof(log)) != 1)
        return 6;
    if (link_status(structure_uniform, structure_uniform_fragment_mismatch, log,
                    sizeof(log)) != 0 || !strstr(log, "uniform"))
        return 103;
    if (link_status(structure_uniform_layout_vertex,
                    structure_uniform_layout_fragment, log, sizeof(log)) != 0 ||
        !strstr(log, "uniform"))
        return 104;
    if (link_status(structure_precision_vertex, structure_precision_fragment, log,
                    sizeof(log)) != 1)
        return 108;
    if (link_status(structure_precision_vertex,
                    structure_precision_mismatch_fragment, log, sizeof(log)) != 0 ||
        !strstr(log, "uniform"))
        return 109;
    if (link_status(invalid_main_parameter, simple_fragment, log, sizeof(log)) != 0 ||
        !strstr(log, "main"))
        return 110;
    if (link_status(valid_void_main_parameter, simple_fragment, log,
                    sizeof(log)) != 1)
        return 111;
    if (link_status(vertex_without_position_write, simple_fragment, log,
                    sizeof(log)) != 1)
        return 224;
    if (link_status(valid_vertex, fragment_without_color_write, log,
                    sizeof(log)) != 1)
        return 225;
    if (link_status(return_only_overload, simple_fragment, log, sizeof(log)) != 0 ||
        !strstr(log, "return type"))
        return 112;
    if (link_status(return_only_prototype_mismatch, simple_fragment, log,
                    sizeof(log)) != 0 || !strstr(log, "return type")) {
        fprintf(stderr, "return-only prototype mismatch: %s\n", log);
        return 113;
    }
    if (link_status(mismatched_array_call, simple_fragment, log, sizeof(log)) != 0 ||
        !strstr(log, "overload"))
        return 114;
    if (link_status(overloaded_array_call, simple_fragment, log, sizeof(log)) != 1)
        return 115;
    if (link_status(duplicate_direction_qualifier, simple_fragment, log,
                    sizeof(log)) != 0 || !strstr(log, "signature"))
        return 137;
    if (link_status(duplicate_const_qualifier, simple_fragment, log,
                    sizeof(log)) == 1)
        return 138;
    if (link_status(direct_recursion, simple_fragment, log, sizeof(log)) != 0 ||
        !strstr(log, "recursive"))
        return 116;
    if (link_status(mutual_recursion, simple_fragment, log, sizeof(log)) != 0 ||
        !strstr(log, "recursive"))
        return 117;
    if (link_status(nonrecursive_overload_chain, simple_fragment, log,
                    sizeof(log)) != 1)
        return 118;
    if (link_status(function_global_collision, simple_fragment, log,
                    sizeof(log)) != 0 || !strstr(log, "conflicts"))
        return 119;
    if (link_status(function_uniform_collision, simple_fragment, log,
                    sizeof(log)) != 0 || !strstr(log, "conflicts"))
        return 120;
    if (link_status(function_struct_collision, simple_fragment, log,
                    sizeof(log)) != 0 || !strstr(log, "conflicts"))
        return 121;
    if (link_status(legal_function_local_shadow, simple_fragment, log,
                    sizeof(log)) != 1)
        return 122;
    {
        int duplicate_status = link_status(duplicate_global_variable, simple_fragment,
                                           log, sizeof(log));
        if (duplicate_status != 0)
            return 123;
    }
    if (link_status(constant_uniform_vertex, simple_fragment, log, sizeof(log)) != 1) {
        fprintf(stderr, "constant uniform link: %s\n", log);
        return 7;
    }
    if (link_status(non_integer_array_vertex, simple_fragment, log, sizeof(log)) != 0 ||
        !strstr(log, "array"))
        return 8;
    if (link_status(varying_array_vertex, mismatched_varying_array_fragment, log,
                    sizeof(log)) != 0 ||
        !strstr(log, "varying"))
        return 9;
    if (link_bound_status(matrix_attributes_vertex, simple_fragment, log, sizeof(log)) != 0 ||
        !strstr(log, "attribute"))
        return 10;
    if (link_status(too_many_matrix_attributes, simple_fragment, log, sizeof(log)) != 0 ||
        !strstr(log, "attribute"))
        return 11;
    if (active_attribute_count(unused_attributes, simple_fragment) != 1)
        return 12;
    if (link_status(initialized_attribute, simple_fragment, log, sizeof(log)) != 0)
        return 13;
    if (link_status(written_attribute, simple_fragment, log, sizeof(log)) != 0 ||
        !strstr(log, "read-only"))
        return 14;
    if (link_status(local_attribute, simple_fragment, log, sizeof(log)) != 0)
        return 15;
    if (link_status(local_uniform, simple_fragment, log, sizeof(log)) != 0)
        return 124;
    if (link_status(local_varying, simple_fragment, log, sizeof(log)) != 0)
        return 125;
    if (link_status(parameter_uniform, simple_fragment, log, sizeof(log)) != 0)
        return 126;
    if (link_status(invalid_const_out_parameter, simple_fragment, log,
                    sizeof(log)) != 0 || !strstr(log, "signature"))
        return 127;
    if (link_status(invalid_double_precision_parameter, simple_fragment, log,
                    sizeof(log)) != 0)
        return 128;
    if (link_status(valid_const_in_parameter, simple_fragment, log,
                    sizeof(log)) != 1)
        return 129;
    if (link_status(mismatched_return_type, simple_fragment, log,
                    sizeof(log)) != 0 || !strstr(log, "return type"))
        return 130;
    if (link_status(void_returning_value, simple_fragment, log,
                    sizeof(log)) != 0 || !strstr(log, "returns a value"))
        return 131;
    if (link_status(empty_nonvoid_return, simple_fragment, log,
                    sizeof(log)) != 0 || !strstr(log, "returns no value"))
        return 132;
    if (link_status(matching_nested_return, simple_fragment, log,
                    sizeof(log)) != 1)
        return 133;
    if (link_status(invalid_if_condition, simple_fragment, log,
                    sizeof(log)) != 0 || !strstr(log, "condition"))
        return 134;
    if (link_status(invalid_while_condition, simple_fragment, log,
                    sizeof(log)) != 0 || !strstr(log, "condition"))
        return 135;
    if (link_status(invalid_for_condition, simple_fragment, log,
                    sizeof(log)) != 0 || !strstr(log, "condition"))
        return 136;
    if (link_status(valid_empty_for_condition, simple_fragment, log,
                    sizeof(log)) != 1)
        return 137;
    if (link_status(invalid_initializer_type, simple_fragment, log,
                    sizeof(log)) != 0 || !strstr(log, "assignment type"))
        return 138;
    if (link_status(invalid_assignment_type, simple_fragment, log,
                    sizeof(log)) != 0 || !strstr(log, "assignment type"))
        return 139;
    if (link_status(invalid_swizzle_assignment, simple_fragment, log,
                    sizeof(log)) != 0 || !strstr(log, "assignment type"))
        return 140;
    if (link_status(invalid_array_element_assignment, simple_fragment, log,
                    sizeof(log)) != 0 || !strstr(log, "assignment type"))
        return 141;
    if (link_status(valid_compound_assignments, simple_fragment, log,
                    sizeof(log)) != 1)
        return 142;
    if (link_status(invalid_compound_assignment, simple_fragment, log,
                    sizeof(log)) != 0 || !strstr(log, "assignment type"))
        return 143;
    if (link_status(invalid_conditional_condition, simple_fragment, log,
                    sizeof(log)) != 0 || !strstr(log, "requires bool"))
        return 144;
    if (link_status(invalid_conditional_branches, simple_fragment, log,
                    sizeof(log)) != 0 || !strstr(log, "branch type"))
        return 145;
    if (link_status(invalid_argument_conditional, simple_fragment, log,
                    sizeof(log)) != 0 || !strstr(log, "branch type"))
        return 146;
    if (link_status(valid_nested_conditional, simple_fragment, log,
                    sizeof(log)) != 1)
        return 147;
    if (link_status(invalid_nested_conditional, simple_fragment, log,
                    sizeof(log)) != 0 || !strstr(log, "requires bool"))
        return 148;
    if (link_status(invalid_modulus_operator, simple_fragment, log,
                    sizeof(log)) != 0)
        return 149;
    if (link_status(invalid_bitwise_operators, simple_fragment, log,
                    sizeof(log)) != 0)
        return 150;
    if (link_status(invalid_logical_operands, simple_fragment, log,
                    sizeof(log)) != 0 || !strstr(log, "logical mismatch"))
        return 151;
    if (link_status(invalid_relational_vectors, simple_fragment, log,
                    sizeof(log)) != 0 || !strstr(log, "logical mismatch"))
        return 152;
    if (link_status(invalid_mixed_equality, simple_fragment, log,
                    sizeof(log)) != 0 || !strstr(log, "logical mismatch"))
        return 153;
    if (link_status(invalid_sampler_equality, simple_fragment, log,
                    sizeof(log)) != 0 || !strstr(log, "logical mismatch"))
        return 154;
    if (link_status(invalid_logical_not_vector, simple_fragment, log,
                    sizeof(log)) != 0 || !strstr(log, "logical-not"))
        return 155;
    if (link_status(invalid_boolean_increment, simple_fragment, log,
                    sizeof(log)) != 0 || !strstr(log, "increment"))
        return 156;
    if (link_status(invalid_constructor_increment, simple_fragment, log,
                    sizeof(log)) != 0 || !strstr(log, "increment"))
        return 157;
    if (link_status(invalid_repeated_swizzle_increment, simple_fragment, log,
                    sizeof(log)) != 0 || !strstr(log, "increment"))
        return 158;
    if (link_status(valid_numeric_increment, simple_fragment, log,
                    sizeof(log)) != 1)
        return 159;
    if (link_status(invalid_boolean_negation, simple_fragment, log,
                    sizeof(log)) != 0 || !strstr(log, "unary numeric"))
        return 160;
    if (link_status(invalid_float_array_subscript, simple_fragment, log,
                    sizeof(log)) != 0 || !strstr(log, "subscript"))
        return 161;
    if (link_status(invalid_bool_vector_subscript, simple_fragment, log,
                    sizeof(log)) != 0 || !strstr(log, "subscript"))
        return 162;
    if (link_status(invalid_vector_subscript, simple_fragment, log,
                    sizeof(log)) != 0 || !strstr(log, "subscript"))
        return 163;
    if (link_status(valid_integer_subscripts, simple_fragment, log,
                    sizeof(log)) != 1)
        return 164;
    if (link_status(invalid_mixed_swizzle, simple_fragment, log,
                    sizeof(log)) != 0 || !strstr(log, "swizzle"))
        return 165;
    if (link_status(invalid_dimension_swizzle, simple_fragment, log,
                    sizeof(log)) != 0 || !strstr(log, "swizzle"))
        return 166;
    if (link_status(invalid_long_swizzle, simple_fragment, log,
                    sizeof(log)) != 0 || !strstr(log, "swizzle"))
        return 167;
    if (link_status(invalid_duplicate_swizzle_write, simple_fragment, log,
                    sizeof(log)) != 0 || !strstr(log, "swizzle"))
        return 168;
    if (link_status(valid_duplicate_swizzle_read, simple_fragment, log,
                    sizeof(log)) != 1)
        return 169;
    if (link_status(invalid_boolean_arithmetic, simple_fragment, log,
                    sizeof(log)) != 0 || !strstr(log, "arithmetic"))
        return 170;
    if (link_status(invalid_bvec_arithmetic, simple_fragment, log,
                    sizeof(log)) != 0 || !strstr(log, "arithmetic"))
        return 171;
    if (link_status(invalid_structure_arithmetic, simple_fragment, log,
                    sizeof(log)) != 0 || !strstr(log, "arithmetic"))
        return 172;
    if (link_status(invalid_matrix_vector_dimensions, simple_fragment, log,
                    sizeof(log)) != 0 || !strstr(log, "arithmetic"))
        return 173;
    if (link_status(valid_arithmetic_combinations, simple_fragment, log,
                    sizeof(log)) != 1)
        return 174;
    if (link_status(invalid_out_literal, simple_fragment, log,
                    sizeof(log)) != 0 || !strstr(log, "writable lvalue"))
        return 175;
    if (link_status(invalid_out_constructor, simple_fragment, log,
                    sizeof(log)) != 0 || !strstr(log, "writable lvalue"))
        return 176;
    if (link_status(invalid_inout_expression, simple_fragment, log,
                    sizeof(log)) != 0 || !strstr(log, "writable lvalue"))
        return 177;
    if (link_status(invalid_out_repeated_swizzle, simple_fragment, log,
                    sizeof(log)) != 0 || !strstr(log, "writable lvalue"))
        return 178;
    if (link_status(valid_out_lvalues, simple_fragment, log, sizeof(log)) != 1)
        return 179;
    if (link_status(mismatched_parameter_modes, simple_fragment, log,
                    sizeof(log)) != 0 || !strstr(log, "qualifiers must match"))
        return 180;
    if (link_status(invalid_out_const, simple_fragment, log,
                    sizeof(log)) != 0 || !strstr(log, "read-only"))
        return 181;
    if (link_status(invalid_inout_uniform, simple_fragment, log,
                    sizeof(log)) != 0 || !strstr(log, "read-only"))
        return 182;
    if (link_status(invalid_out_read_only_builtin, simple_fragment, log,
                    sizeof(log)) != 0 || !strstr(log, "read-only"))
        return 183;
    if (link_status(valid_vertex_out_varying, valid_fragment_varying, log,
                    sizeof(log)) != 1)
        return 184;
    if (link_status(valid_vertex_out_varying, invalid_fragment_out_varying, log,
                    sizeof(log)) != 0 || !strstr(log, "read-only"))
        return 185;
    if (link_status(invalid_scalar_base_subscript, simple_fragment, log,
                    sizeof(log)) != 0 || !strstr(log, "not subscriptable"))
        return 186;
    if (link_status(invalid_struct_base_subscript, simple_fragment, log,
                    sizeof(log)) != 0 || !strstr(log, "not subscriptable"))
        return 187;
    if (link_status(invalid_sampler_base_subscript, simple_fragment, log,
                    sizeof(log)) != 0 || !strstr(log, "not subscriptable"))
        return 188;
    if (link_status(invalid_negative_vector_subscript, simple_fragment, log,
                    sizeof(log)) != 0 || !strstr(log, "constant subscript"))
        return 189;
    if (link_status(invalid_vector_bound_subscript, simple_fragment, log,
                    sizeof(log)) != 0 || !strstr(log, "constant subscript"))
        return 190;
    if (link_status(invalid_matrix_bound_subscript, simple_fragment, log,
                    sizeof(log)) != 0 || !strstr(log, "constant subscript"))
        return 191;
    if (link_status(invalid_array_bound_subscript, simple_fragment, log,
                    sizeof(log)) != 0 || !strstr(log, "constant subscript"))
        return 192;
    if (link_status(valid_nested_dynamic_subscripts, simple_fragment, log,
                    sizeof(log)) != 1)
        return 193;
    if (link_status(invalid_duplicate_parameters, simple_fragment, log,
                    sizeof(log)) != 0 || !strstr(log, "signature"))
        return 194;
    if (link_status(invalid_parameter_local_redeclaration, simple_fragment, log,
                    sizeof(log)) != 0 || !strstr(log, "duplicate local"))
        return 195;
    if (link_status(invalid_duplicate_locals, simple_fragment, log,
                    sizeof(log)) != 0 || !strstr(log, "duplicate local"))
        return 196;
    if (link_status(invalid_duplicate_declarators, simple_fragment, log,
                    sizeof(log)) != 0 || !strstr(log, "duplicate local"))
        return 197;
    if (link_status(valid_nested_local_shadow, simple_fragment, log,
                    sizeof(log)) != 1)
        return 198;
    if (link_status(valid_sibling_local_names, simple_fragment, log,
                    sizeof(log)) != 1)
        return 199;
    if (link_status(valid_reused_for_initializer, simple_fragment, log,
                    sizeof(log)) != 1)
        return 200;
    if (link_status(invalid_duplicate_for_declarators, simple_fragment, log,
                    sizeof(log)) != 0 || !strstr(log, "duplicate local"))
        return 201;
    if (link_status(valid_shadowed_overload_after_block, simple_fragment, log,
                    sizeof(log)) != 1)
        return 202;
    if (link_status(valid_assignment_after_shadow, simple_fragment, log,
                    sizeof(log)) != 1)
        return 203;
    if (link_status(valid_sibling_shadow_lookup, simple_fragment, log,
                    sizeof(log)) != 1)
        return 204;
    if (link_status(valid_for_shadow_lookup, simple_fragment, log,
                    sizeof(log)) != 1)
        return 205;
    if (link_status(invalid_later_declarator_initializer, simple_fragment, log,
                    sizeof(log)) != 0 || !strstr(log, "mismatch"))
        return 206;
    if (link_status(invalid_vector_later_declarator_initializer, simple_fragment, log,
                    sizeof(log)) != 0 || !strstr(log, "mismatch"))
        return 207;
    if (link_status(invalid_later_declarator_overload, simple_fragment, log,
                    sizeof(log)) != 0 || !strstr(log, "mismatch"))
        return 208;
    if (link_status(invalid_later_array_declarator_bound, simple_fragment, log,
                    sizeof(log)) != 0 || !strstr(log, "constant subscript"))
        return 209;
    if (link_status(valid_later_array_declarator, simple_fragment, log,
                    sizeof(log)) != 1)
        return 210;
    if (link_status(valid_qualified_declarator_list, simple_fragment, log,
                    sizeof(log)) != 1)
        return 211;
    if (link_status(invalid_dead_branch_identifier, simple_fragment, log,
                    sizeof(log)) != 0 || !strstr(log, "undeclared identifier"))
        return 212;
    if (link_status(invalid_identifier_after_block, simple_fragment, log,
                    sizeof(log)) != 0 || !strstr(log, "undeclared identifier"))
        return 213;
    if (link_status(invalid_identifier_after_for, simple_fragment, log,
                    sizeof(log)) != 0 || !strstr(log, "undeclared identifier"))
        return 214;
    if (link_status(invalid_identifier_in_initializer, simple_fragment, log,
                    sizeof(log)) != 0 || !strstr(log, "undeclared identifier"))
        return 215;
    if (link_status(invalid_parameter_from_other_function, simple_fragment, log,
                    sizeof(log)) != 0 || !strstr(log, "undeclared identifier"))
        return 216;
    if (link_status(valid_identifier_categories, simple_fragment, log,
                    sizeof(log)) != 1)
        return 217;
    if (link_status(valid_local_structure_identifiers, simple_fragment, log,
                    sizeof(log)) != 1)
        return 218;
    if (link_status(valid_vertex, fragment_attribute, log, sizeof(log)) != 0)
        return 16;
    if (!prebound_inactive_attribute_test(unused_attributes, simple_fragment))
        return 17;
    if (link_status(mismatched_function_signature, simple_fragment, log, sizeof(log)) != 0 ||
        !strstr(log, "signature"))
        return 18;
    if (link_status(duplicate_function_definition, simple_fragment, log, sizeof(log)) != 0 ||
        !strstr(log, "duplicate"))
        return 19;
    if (link_status(overloaded_functions, simple_fragment, log, sizeof(log)) != 1)
        return 20;
    if (link_status(mismatched_function_array, simple_fragment, log, sizeof(log)) != 0 ||
        !strstr(log, "signature"))
        return 21;
    if (link_status(unresolved_function_call, simple_fragment, log, sizeof(log)) != 0 ||
        !strstr(log, "unresolved") || !strstr(log, "mystery"))
        return 22;
    if (link_status(mismatched_function_arity, simple_fragment, log, sizeof(log)) != 0 ||
        !strstr(log, "overload") || !strstr(log, "helper"))
        return 23;
    if (link_status(mismatched_function_type, simple_fragment, log, sizeof(log)) != 0 ||
        !strstr(log, "typed overload") || !strstr(log, "helper"))
        return 24;
    if (link_status(constructor_swizzle_overload, simple_fragment, log, sizeof(log)) != 1)
        return 25;
    if (link_status(mismatched_variable_type, simple_fragment, log, sizeof(log)) != 0 ||
        !strstr(log, "typed overload"))
        return 26;
    if (link_status(variable_swizzle_overload, simple_fragment, log, sizeof(log)) != 1)
        return 27;
    if (link_status(array_element_overload, simple_fragment, log, sizeof(log)) != 1)
        return 28;
    if (link_status(mismatched_compound_type, simple_fragment, log, sizeof(log)) != 0 ||
        !strstr(log, "typed overload"))
        return 29;
    if (link_status(matching_compound_type, simple_fragment, log, sizeof(log)) != 1)
        return 30;
    if (link_status(mismatched_nested_call, simple_fragment, log, sizeof(log)) != 0 ||
        !strstr(log, "typed overload"))
        return 31;
    if (link_status(nested_call_swizzle, simple_fragment, log, sizeof(log)) != 1)
        return 32;
    if (link_status(scalar_vector_expression, simple_fragment, log, sizeof(log)) != 1)
        return 33;
    if (link_status(mismatched_matrix_vector_expression, simple_fragment, log,
                    sizeof(log)) != 0 || !strstr(log, "typed overload"))
        return 34;
    if (link_status(matching_matrix_vector_expression, simple_fragment, log,
                    sizeof(log)) != 1)
        return 35;
    if (link_status(scalar_comparison_expression, simple_fragment, log, sizeof(log)) != 1)
        return 36;
    if (link_status(mismatched_comparison_expression, simple_fragment, log,
                    sizeof(log)) != 0 || !strstr(log, "typed overload"))
        return 37;
    if (link_status(matching_conditional_expression, simple_fragment, log,
                    sizeof(log)) != 1)
        return 38;
    if (link_status(mismatched_conditional_expression, simple_fragment, log,
                    sizeof(log)) != 0 || !strstr(log, "typed overload"))
        return 39;
    if (link_status(logical_expression, simple_fragment, log, sizeof(log)) != 1)
        return 40;
    if (link_status(mismatched_builtin_result, simple_fragment, log, sizeof(log)) != 0 ||
        !strstr(log, "typed overload"))
        return 41;
    if (link_status(matching_builtin_results, simple_fragment, log, sizeof(log)) != 1)
        return 42;
    if (link_status(texture_builtin_result, simple_fragment, log, sizeof(log)) != 1)
        return 43;
    if (link_status(step_builtin_results, simple_fragment, log, sizeof(log)) != 1)
        return 44;
    if (link_status(invalid_builtin_arity, simple_fragment, log, sizeof(log)) != 0 ||
        !strstr(log, "built-in argument count"))
        return 45;
    if (link_status(invalid_texture_arity, simple_fragment, log, sizeof(log)) != 0 ||
        !strstr(log, "built-in argument count"))
        return 46;
    if (link_status(unresolved_fake_builtin, simple_fragment, log, sizeof(log)) != 0 ||
        !strstr(log, "unresolved function"))
        return 47;
    if (link_status(invalid_builtin_types, simple_fragment, log, sizeof(log)) != 0 ||
        !strstr(log, "built-in argument types"))
        return 48;
    if (link_status(invalid_texture_types, simple_fragment, log, sizeof(log)) != 0 ||
        !strstr(log, "built-in argument types"))
        return 49;
    if (link_status(valid_builtin_types, simple_fragment, log, sizeof(log)) != 1)
        return 50;
    if (link_status(invalid_vertex_texture_bias, simple_fragment, log, sizeof(log)) != 0)
        return 51;
    if (link_status(valid_vertex_texture_lod, simple_fragment, log, sizeof(log)) != 1)
        return 52;
    if (link_status(valid_vertex, valid_fragment_texture_bias, log, sizeof(log)) != 1)
        return 53;
    if (link_status(invalid_vertex_discard, simple_fragment, log, sizeof(log)) != 0)
        return 54;
    if (link_status(invalid_vertex_fragment_input, simple_fragment, log, sizeof(log)) != 0)
        return 55;
    if (link_status(valid_vertex, invalid_fragment_vertex_output, log, sizeof(log)) != 0)
        return 56;
    if (link_status(valid_vertex, invalid_fragment_attribute, log, sizeof(log)) != 0)
        return 57;
    if (link_status(invalid_uniform_initializer, simple_fragment, log, sizeof(log)) != 0)
        return 58;
    if (link_status(invalid_varying_initializer, simple_fragment, log, sizeof(log)) != 0)
        return 59;
    if (link_status(valid_vertex, invalid_fragment_varying_write, log, sizeof(log)) != 0 ||
        !strstr(log, "read-only"))
        return 60;
    if (link_status(invalid_uniform_write, simple_fragment, log, sizeof(log)) != 0 ||
        !strstr(log, "read-only"))
        return 61;
    if (link_status(invalid_uniform_array_write, simple_fragment, log, sizeof(log)) != 0 ||
        !strstr(log, "read-only"))
        return 62;
    if (link_status(multi_uniform_vertex, conflicting_second_uniform, log, sizeof(log)) != 0 ||
        !strstr(log, "uniform"))
        return 63;
    if (link_status(invalid_second_uniform_array, simple_fragment, log, sizeof(log)) != 0 ||
        !strstr(log, "array"))
        return 64;
    if (link_status(multi_varying_vertex, conflicting_second_varying, log, sizeof(log)) != 0 ||
        !strstr(log, "varying"))
        return 65;
    if (link_status(invalid_unary_builtin_type, simple_fragment, log, sizeof(log)) != 0 ||
        !strstr(log, "built-in argument types"))
        return 66;
    if (link_status(invalid_pow_builtin_type, simple_fragment, log, sizeof(log)) != 0 ||
        !strstr(log, "built-in argument types"))
        return 67;
    if (link_status(invalid_mix_builtin_type, simple_fragment, log, sizeof(log)) != 0 ||
        !strstr(log, "built-in argument types"))
        return 68;
    if (link_status(invalid_step_builtin_type, simple_fragment, log, sizeof(log)) != 0 ||
        !strstr(log, "built-in argument types"))
        return 69;
    if (link_status(valid_scalar_and_integer_builtins, simple_fragment, log,
                    sizeof(log)) != 1)
        return 70;
    if (link_status(invalid_extra_constructor_components, simple_fragment, log,
                    sizeof(log)) != 0 || !strstr(log, "constructor"))
        return 71;
    if (link_status(valid_scalar_from_vector_constructor, simple_fragment, log,
                    sizeof(log)) != 1)
        return 72;
    if (link_status(valid_builtin_overload, simple_fragment, log,
                    sizeof(log)) != 1)
        return 126;
    if (link_status(valid_builtin_before_overload, simple_fragment, log,
                    sizeof(log)) != 1)
        return 127;
    if (link_status(invalid_builtin_redeclaration, simple_fragment, log,
                    sizeof(log)) != 0 || !strstr(log, "built-in"))
        return 128;
    if (link_status(valid_vector_from_matrix, simple_fragment, log,
                    sizeof(log)) != 1)
        return 124;
    if (link_status(invalid_mixed_matrix_constructor, simple_fragment, log,
                    sizeof(log)) != 0 || !strstr(log, "constructor"))
        return 125;
    if (link_status(valid_exact_constructor, simple_fragment, log, sizeof(log)) != 1)
        return 73;
    if (link_status(invalid_mutable_global_initializer, simple_fragment, log,
                    sizeof(log)) != 0)
        return 74;
    if (link_status(invalid_function_global_initializer, simple_fragment, log,
                    sizeof(log)) != 0)
        return 75;
    if (link_status(valid_constant_global_initializer, simple_fragment, log,
                    sizeof(log)) != 1)
        return 76;
    if (link_status(invalid_depth_range_write, simple_fragment, log,
                    sizeof(log)) != 0)
        return 77;
    if (link_status(valid_vertex, invalid_frag_coord_write, log,
                    sizeof(log)) != 0)
        return 78;
    if (link_status(valid_implementation_constant, simple_fragment, log,
                    sizeof(log)) != 1)
        return 79;
    if (link_status(invalid_implementation_constant_write, simple_fragment, log,
                    sizeof(log)) != 0)
        return 80;
    if (link_status(valid_vertex, fragment_data_output, log, sizeof(log)) != 1)
        return 81;
    if (link_status(valid_vertex, mixed_fragment_outputs, log, sizeof(log)) != 0 ||
        !strstr(log, "both"))
        return 82;
    if (link_status(valid_vertex, out_of_range_fragment_output, log,
                    sizeof(log)) != 0 || !strstr(log, "exceeds"))
        return 83;
    if (link_status(valid_vertex, dynamic_fragment_output, log,
                    sizeof(log)) != 0 || !strstr(log, "constant"))
        return 84;
    if (link_status(invariant_vertex, invariant_fragment, log, sizeof(log)) != 1)
        return 85;
    if (link_status(invariant_vertex, noninvariant_fragment, log, sizeof(log)) != 0 ||
        !strstr(log, "invariance"))
        return 86;
    if (link_status(inactive_invariant_vertex,
                    inactive_noninvariant_fragment, log, sizeof(log)) != 0 ||
        !strstr(log, "invariance"))
        return 259;
    if (link_status(invalid_local_invariant, simple_fragment, log,
                    sizeof(log)) != 0)
        return 87;
    if (link_status(invalid_uniform_invariant, simple_fragment, log,
                    sizeof(log)) != 0)
        return 88;
    if (link_status(reserved_gl_variable, simple_fragment, log,
                    sizeof(log)) != 0)
        return 89;
    if (link_status(reserved_gl_function, simple_fragment, log,
                    sizeof(log)) != 0)
        return 90;
    if (link_status(reserved_gl_struct_member, simple_fragment, log,
                    sizeof(log)) != 0)
        return 91;
    if (link_status(commented_reserved_gl_name, simple_fragment, log,
                    sizeof(log)) != 1)
        return 92;
    if (link_status(invalid_break_outside_loop, simple_fragment, log,
                    sizeof(log)) != 0)
        return 93;
    if (link_status(invalid_continue_in_called_function, simple_fragment, log,
                    sizeof(log)) != 0)
        return 94;
    if (link_status(valid_nested_loop_control, simple_fragment, log,
                    sizeof(log)) != 1)
        return 95;
    if (link_status(commented_loop_control, simple_fragment, log,
                    sizeof(log)) != 1)
        return 96;
    if (link_status(pragma_invariant_vertex, pragma_invariant_fragment, log,
                    sizeof(log)) != 1)
        return 97;
    if (link_status(pragma_invariant_vertex, noninvariant_fragment, log,
                    sizeof(log)) != 0 || !strstr(log, "invariance"))
        return 98;
    if (link_status(invariant_vertex, pragma_invariant_fragment, log,
                    sizeof(log)) != 0)
        return 99;
    if (link_status(matrix_varyings_vertex, matrix_varyings_fragment, log,
                    sizeof(log)) != 1) {
        fprintf(stderr, "matrix varying link failure: %s\n", log);
        return 100;
    }
    if (link_status(too_many_matrix_varyings, too_many_matrix_varyings_fragment, log,
                    sizeof(log)) != 0 || !strstr(log, "varying vector limit"))
        return 101;
    if (link_status(invalid_call_before_declaration, simple_fragment, log,
                    sizeof(log)) != 0 || !strstr(log, "before declaration"))
        return 219;
    if (link_status(valid_call_after_prototype, simple_fragment, log,
                    sizeof(log)) != 1)
        return 220;
    if (link_status(invalid_duplicate_function_prototype, simple_fragment, log,
                    sizeof(log)) != 0 || !strstr(log, "duplicate function prototype"))
        return 223;
    if (link_status(invalid_direction_qualifier_overload, simple_fragment, log,
                    sizeof(log)) != 0 || !strstr(log, "qualifiers must match"))
        return 224;
    if (link_status(invalid_function_precision_match, simple_fragment, log,
                    sizeof(log)) != 0 || !strstr(log, "precision qualifiers"))
        return 225;
    if (link_status(invalid_default_function_precision_match, simple_fragment, log,
                    sizeof(log)) != 0 || !strstr(log, "precision qualifiers"))
        return 226;
    if (link_status(valid_vertex_default_function_precision, simple_fragment, log,
                    sizeof(log)) != 1)
        return 227;
    if (link_status(invalid_const_parameter_match, simple_fragment, log,
                    sizeof(log)) != 0 || !strstr(log, "qualifiers must match"))
        return 228;
    if (link_status(invalid_global_after_use, simple_fragment, log,
                    sizeof(log)) != 0 || !strstr(log, "undeclared identifier"))
        return 221;
    if (link_status(valid_global_before_use, simple_fragment, log,
                    sizeof(log)) != 1)
        return 222;
    ntglDestroyContext(context);
    return 0;
}
