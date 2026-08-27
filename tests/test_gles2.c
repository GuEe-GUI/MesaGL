#include "GLES2/gl2.h"
#include "mesaGL/ntgl.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct Vertex {
    float position[2];
    float uv[2];
    uint8_t color[4];
    float transform[4];
} Vertex;

static const char vertex_source[] = "#version 100\n"
                                    "#pragma STDGL invariant(all)\n"
                                    "#define POSITION_OUTPUT gl_Position\n"
                                    "#ifdef GL_ES\n"
                                    "attribute vec2 Position;\n"
                                    "#else\n"
                                    "this branch must not compile\n"
                                    "#endif\n"
                                    "attribute vec2 UV;\n"
                                    "attribute vec4 Color;\n"
                                    "attribute mat2 InputMatrix;\n"
                                    "attribute vec4 "
                                    "mesaGL_attribute_identifier_that_is_intentionally_longer_than_the_old_forty_seven_limit;\n"
                                    "uniform mat4 oddly_named_transform;\n"
                                    "uniform vec4 oddly_named_tint;\n"
                                    "invariant varying vec2 Frag_UV;\n"
                                    "varying vec4 Frag_Color;\n"
                                    "varying vec4 custom_varying;\n"
                                    "varying vec4 custom_array[2];\n"
                                    "varying mat3 custom_matrix;\n"
                                    "void main() {\n"
                                    "  Frag_UV = UV;\n"
                                    "  Frag_Color = Color;\n"
                                    "  gl_PointSize = 8.0;\n"
                                    "  custom_varying = oddly_named_tint;\n"
                                    "  custom_array[0] = oddly_named_tint;\n"
                                    "  custom_array[1] = oddly_named_tint.bgra;\n"
                                    "  custom_matrix = mat3(1.0);\n"
                                    "  mat4 combined = oddly_named_transform * mat4(mat3(1.0));\n"
                                    "  vec2 transformed = InputMatrix * Position;\n"
                                    "  vec4 row_position = vec4(transformed, 0.0, 1.0) * mat4(1.0);\n"
                                    "  POSITION_OUTPUT = combined * row_position * "
                                    "mesaGL_attribute_identifier_that_is_intentionally_longer_than_the_old_forty_seven_limit.w;\n"
                                    "}\n";

static const char fragment_source_0[] =
    "#version 100\n"
    "#define RESULT_SCALE vec3(0.25, 1.0, 0.25)\n"
    "#if 1 || (1 / 0)\n"
    "#define PREPROCESSOR_SCALE 1.0\n"
    "#else\n"
    "#define PREPROCESSOR_SCALE 0.0\n"
    "#endif\n"
    "#define SCALE_RESULT(value) ((value) * RESULT_SCALE)\n"
    "#define CURRENT_SOURCE_FILE __FILE__\n"
    "#define CURRENT_SOURCE_LINE __LINE__\n"
    "#define mesaGL_macro_name_longer_than_the_old_forty_seven_character_limit "
    "(0.0 + 0.0 + 0.0 + 0.0 + 0.0 + 0.0 + 0.0 + 0.0 + 0.0 + 0.0 + "
    "0.0 + 0.0 + 0.0 + 0.0 + 0.0 + 0.0 + 0.0 + 0.0 + 0.0 + 1.0)\n"
    "#define mesaGL_apply_long_parameter("
    "mesaGL_parameter_name_longer_than_the_old_forty_seven_character_limit) "
    "(mesaGL_parameter_name_longer_than_the_old_forty_seven_character_limit)\n"
    "#if defined(GL_ES)\n"
    "precision mediump float;\n"
    "#endif\n"
    "invariant gl_FragCoord, gl_PointCoord;\n"
    "#define EXPECTED_LOGICAL_LINE 60\n"
    "#line EXPECTED_LOGICAL_LINE 9\n"
    "const int LOGICAL_LINE = __LINE__;\n"
    "const int LOGICAL_FILE = __FILE__;\n"
    "invariant varying mediump vec2 Frag_UV;\n"
    "invariant varying vec4 Frag_Color;\n"
    "invariant varying vec4 custom_varying;\n"
    "invariant varying vec4 custom_array[PALETTE_SIZE];\n"
    "invariant varying mat3 custom_matrix;\n"
    "const int PALETTE_SIZE = 05 / 0x2;\n"
    "const highp vec3 COLOR_SCALE = vec3(1.0);\n"
    "const int IMPLEMENTATION_UNITS = gl_MaxTextureImageUnits;\n"
    "vec3 global_scale = vec3(1.0);\n"
    "struct AnonymousNested { vec3 scale; };\n"
    "struct { AnonymousNested nested; } anonymous_global;\n"
    "uniform highp vec4 palette[PALETTE_SIZE];\n"
    "uniform int selected_index;\n"
    "uniform float "
    "mesaGL_uniform_identifier_that_is_intentionally_longer_than_the_old_forty_seven_limit[2];\n"
    "struct PaletteResult { vec4 color; float alpha_scale; };\n"
    "struct Wrapper { PaletteResult result; };\n"
    "struct Deep1 { vec3 scale; };\n"
    "struct Deep2 { Deep1 value; };\n"
    "struct Deep3 { Deep2 value; };\n"
    "struct Deep4 { Deep3 value; };\n"
    "struct ColorArray { vec3 unused, values[2], tail; mat3 transform; };\n"
    "struct ColorArray2 { ColorArray groups[2]; };\n"
    "vec4 select_palette(vec4 values[PALETTE_SIZE], int index) { return values[index]; }\n"
    "void apply_palette(inout vec4 color, out float alpha_scale) { "
    "color *= select_palette(palette, selected_index); alpha_scale = 1.0; }\n"
    "void scale_result(inout PaletteResult result) { "
    "result.color.rgb = SCALE_RESULT(result.color.rgb); }\n"
    "void swap_pair(inout vec2 pair) { pair = pair.yx; }\n"
    "bool poison(inout vec3 value) { value = vec3(0.0); return true; }\n"
    "bool poison_global() { global_scale = vec3(0.0); return true; }\n"
    "vec3 poison_value(inout vec3 value) { value = vec3(0.0); return value; }\n"
    "vec3 overload_identity(vec3 value) { return value; }\n"
    "vec4 overload_identity(vec4 value) { "
    "return vec4(overload_identity(value.rgb), value.a); }\n"
    "float overload_identity(float value) { return value; }\n"
    "int overload_identity(int value) { return value; }\n"
    "float mesaGL_function_identifier_that_is_intentionally_longer_than_the_old_forty_seven_limit("
    "float value) { return value; }\n";

static const char fragment_source_1[] =
    "const int SOURCE_ONE_FILE = CURRENT_SOURCE_FILE; "
    "const int SOURCE_ONE_LINE = CURRENT_SOURCE_LINE; "
    "vec4 shade(vec4 input_color) { anonymous_global.nested.scale = vec3(1.0); "
    "input_color *= SOURCE_ONE_FILE == 10 && SOURCE_ONE_LINE == 1 ? 1.0 : 0.0; "
    "input_color *= mesaGL_apply_long_parameter("
    "mesaGL_macro_name_longer_than_the_old_forty_seven_character_limit); "
    "float mesaGL_local_identifier_that_is_intentionally_longer_than_the_old_forty_seven_limit = "
    "mesaGL_function_identifier_that_is_intentionally_longer_than_the_old_forty_seven_limit("
    "mesaGL_uniform_identifier_that_is_intentionally_longer_than_the_old_forty_seven_limit[0]); "
    "input_color *= "
    "mesaGL_local_identifier_that_is_intentionally_longer_than_the_old_forty_seven_limit; "
    "vec4 colors[PALETTE_SIZE]; colors[0] = vec4(0.0); "
    "int classifier = int(bvec2(false, true)[1]) * selected_index * (05 / 0x3); "
    "colors[1] = input_color; float alpha_scale; apply_palette(colors[1], alpha_scale); "
    "PaletteResult results[2]; results[0] = PaletteResult(colors[classifier], alpha_scale); "
    "results[1] = results[0]; bool result_copy_equal = results[1] == results[0]; "
    "scale_result(results[1]); "
    "Wrapper wrapper; wrapper.result.color = results[1].color; "
    "wrapper.result.alpha_scale = results[1].alpha_scale; "
    "wrapper.result.color.rgb *= COLOR_SCALE; "
    "Wrapper wrapper_copy = wrapper; bool wrapper_copy_equal = wrapper_copy == wrapper; "
    "bool xor_copy_equal = wrapper_copy_equal ^^ false; "
    "swap_pair(wrapper.result.color.rg); swap_pair(wrapper.result.color.rg); "
    "vec4 shaded = overload_identity(wrapper.result.color); "
    "vec3 short_circuit = vec3(1.0); bool ignored = false && poison(short_circuit); "
    "bool ignored_global = false && poison_global(); "
    "shaded.rgb *= short_circuit * (ignored ? 0.0 : 1.0) * "
    "(result_copy_equal && xor_copy_equal && "
    "((true ? wrapper : wrapper_copy) == wrapper) ? 1.0 : 0.0); "
    "shaded.rgb *= global_scale * anonymous_global.nested.scale * "
    "(ignored_global ? 0.0 : 1.0); "
    "shaded.rgb *= vec3(gl_DepthRange.near * 5.0, "
    "gl_DepthRange.far * 1.25, gl_DepthRange.diff / 0.6); "
    "shaded.rgb *= IMPLEMENTATION_UNITS > 0 ? 1.0 : 0.0; "
    "shaded.rgb *= PREPROCESSOR_SCALE; "
    "shaded.rgb *= LOGICAL_LINE == 60 && LOGICAL_FILE == 9 ? 1.0 : 0.0; "
    "vec3 conditional_state = vec3(1.0);"
    "vec3 conditional_value = true ? vec3(1.0) : poison_value(conditional_state);"
    "shaded.rgb *= conditional_state * conditional_value; ";

static const char fragment_source_2[] =
    "shaded.rgb *= __FILE__ == 11 && __LINE__ == 1 ? 1.0 : 0.0; "
    "Deep4 deep; deep.value.value.value.scale = vec3(1.0); "
    "deep.value.value.value.scale.rgb *= vec3(1.0); "
    "deep.value.value.value.scale *= "
    "vec3(lessThan(ivec3(0, 1, 2), ivec3(1, 2, 3))) * length(2.0) * 0.5; "
    "shaded.rgb *= deep.value.value.value.scale; "
    "ColorArray2 arrays; arrays.groups[1].unused = vec3(0.0); "
    "arrays.groups[1].values[0] = vec3(1.0); arrays.groups[1].tail = vec3(1.0); "
    "arrays.groups[1].values[0].rgb *= vec3(1.0); "
    "int member_channel = 1; arrays.groups[1].tail[member_channel] = 1.0; "
    "arrays.groups[1].transform = mat3(1.0); "
    "arrays.groups[1].transform[1].y *= 1.0; "
    "shaded.rgb *= arrays.groups[1].values[0] * arrays.groups[1].tail; "
    "vec3 indexed = vec3(1.0); int channel = 1; indexed[channel] = 1.0; "
    "float previous_channel = indexed[channel]++; --indexed[channel]; "
    "indexed *= previous_channel; "
    "mat3 indexed_matrix = mat3(1.0); indexed_matrix[1] = vec3(0.0, 1.0, 0.0); "
    "indexed_matrix++; --indexed_matrix; "
    "indexed_matrix = 1.0 + indexed_matrix - 1.0; "
    "indexed_matrix[1].y *= 1.0; shaded.rgb *= indexed * indexed_matrix[1].yyy * "
    "custom_matrix[1].yyy; "
    "shaded.rgb = clamp(shaded.rgb, 0.0, 1.0) * "
    "step(-0.5, vec3(-0.25)) * smoothstep(0.25, 0.75, vec3(1.0)); "
    "shaded.a *= wrapper.result.alpha_scale * overload_identity(1.0) * "
    "float(overload_identity(1)); "
    "int comma_i = 0; int comma_j = 0; "
    "for (; comma_i < 2; comma_i++, comma_j++) { } "
    "shaded.rgb *= overload_identity((0.0, float(comma_j) * 0.5)); "
    "float assignment_scale = 0.0; "
    "shaded.rgb *= overload_identity((assignment_scale = 1.0)); "
    "const float local_constant = 0.5; "
    "const int local_count = int(local_constant * 4.0); "
    "float local_values[local_count]; local_values[1] = 2.0; "
    "shaded.rgb *= local_values[1] * local_constant; "
    "int nested_count = 0; for (int outer = 0; outer < 2; outer++) { "
    "for (int inner = 0; inner < 3; inner++) { "
    "if (inner == 1) continue; nested_count++; if (nested_count == 4) break; }} "
    "shaded.rgb *= float(nested_count) * 0.25; "
    "return shaded; }\n"
    "void main() { vec4 shaded = shade(custom_array[selected_index]); "
    "if (!gl_FrontFacing) { gl_FragData[gl_MaxDrawBuffers - 1] = "
    "vec4(0.0, 1.0, 0.0, 1.0); } "
    "else if (gl_PointCoord.x > 0.0 || gl_PointCoord.y > 0.0) { "
    "gl_FragData[gl_MaxDrawBuffers - 1] = "
    "vec4(gl_PointCoord, 0.0, 1.0); } "
    "else if (shaded.a < 0.5) { discard; } else { "
    "gl_FragData[gl_MaxDrawBuffers - 1] = shaded; } }\n";

int main(void)
{
    uint16_t pixels[64 * 64] = {0};
    NTGLframebuffer framebuffer = {pixels, 64, 64, 64 * 2, NTGL_RGB565, NTGL_ORIGIN_TOP_LEFT};
    const Vertex vertices[] = {
        {{-0.8f, -0.8f}, {0.0f, 0.0f}, {255, 0, 0, 192}, {1, 0, 0, 1}},
        {{0.8f, -0.8f}, {1.0f, 0.0f}, {0, 255, 0, 192}, {1, 0, 0, 1}},
        {{0.0f, 0.8f}, {0.5f, 1.0f}, {0, 0, 255, 192}, {1, 0, 0, 1}},
    };
    const uint16_t indices[] = {0, 1, 2, 2, 1, 0};
    const uint8_t texture_pixels[] = {
        255, 255, 255, 255, 255, 255, 255, 128, 255, 255, 255, 128, 255, 255, 255, 255,
    };
    NTGLcontext *context = ntglCreateContext(&framebuffer, NULL);
    GLuint vertex_shader, fragment_shader, program, vertex_buffer, index_buffer, texture;
    GLint status;
    GLint transform_location, tint_location, palette_location, selected_location;
    GLint long_identifier_location;
    GLint matrix_attribute;
    GLint long_attribute_location;
    GLint active_uniforms, active_attributes, active_size;
    GLenum active_type;
    GLchar active_name[128];
    GLfloat tint_value[4];
    GLfloat matrix_value[16];
    GLfloat rounded_matrix[16];
    GLint matrix_integer[16];
    const GLfloat identity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    const GLfloat zero_matrix[16] = {0};
    const GLfloat zero_palette[8] = {0};
    const GLfloat palette[8] = {0, 0, 0, 0, 1, 1, 1, 1};
    uint8_t center[4];
    const GLchar *source;
    const GLchar *fragment_sources[] = {
        fragment_source_0, fragment_source_1, fragment_source_2};
    int changed = 0;
    int i;
    if (!context)
        return 1;

    vertex_shader = glCreateShader(GL_VERTEX_SHADER);
    source = vertex_source;
    glShaderSource(vertex_shader, 1, &source, NULL);
    glCompileShader(vertex_shader);
    glGetShaderiv(vertex_shader, GL_COMPILE_STATUS, &status);
    if (!status) {
        char log[256];

        glGetShaderInfoLog(vertex_shader, sizeof(log), NULL, log);
        fprintf(stderr, "GLES2 vertex compile failed: %s\n", log);
        return 2;
    }
    glGetShaderiv(vertex_shader, GL_INFO_LOG_LENGTH, &status);
    if (status != 0)
        return 31;

    fragment_shader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragment_shader, 3, fragment_sources, NULL);
    glCompileShader(fragment_shader);
    glGetShaderiv(fragment_shader, GL_COMPILE_STATUS, &status);
    if (!status) {
        char log[256];

        glGetShaderInfoLog(fragment_shader, sizeof(log), NULL, log);
        fprintf(stderr, "GLES2 fragment compile failed: %s\n", log);
        return 3;
    }

    program = glCreateProgram();
    glAttachShader(program, vertex_shader);
    glAttachShader(program, fragment_shader);
    glLinkProgram(program);
    glGetProgramiv(program, GL_LINK_STATUS, &status);
    if (!status) {
        char log[256];

        glGetProgramInfoLog(program, sizeof(log), NULL, log);
        fprintf(stderr, "GLES2 program link failed: %s\n", log);
        return 4;
    }
    glGetProgramiv(program, GL_INFO_LOG_LENGTH, &status);
    if (status != 0)
        return 32;
    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);
    glGetShaderiv(vertex_shader, GL_DELETE_STATUS, &status);
    if (!status || !glIsShader(vertex_shader) || !glIsShader(fragment_shader))
        return 13;
    glUseProgram(program);
    transform_location = glGetUniformLocation(program, "oddly_named_transform");
    tint_location = glGetUniformLocation(program, "oddly_named_tint");
    palette_location = glGetUniformLocation(program, "palette");
    selected_location = glGetUniformLocation(program, "selected_index");
    long_identifier_location = glGetUniformLocation(
        program,
        "mesaGL_uniform_identifier_that_is_intentionally_longer_than_the_old_forty_seven_limit");
    if (transform_location < 0 || tint_location < 0 || palette_location < 0 ||
        selected_location < 0 || long_identifier_location < 0 ||
        glGetUniformLocation(program, "palette[0]") != palette_location ||
        glGetUniformLocation(program, "palette[1]") != palette_location + 1 ||
        glGetUniformLocation(program, "palette[2]") != -1 ||
        glGetUniformLocation(program, "missing_uniform") != -1)
        return 6;
    glGetProgramiv(program, GL_ACTIVE_UNIFORMS, &active_uniforms);
    glGetProgramiv(program, GL_ACTIVE_ATTRIBUTES, &active_attributes);
    if (active_uniforms != 5 || active_attributes != 5)
        return 7;
    glGetProgramiv(program, GL_ACTIVE_UNIFORM_MAX_LENGTH, &status);
    if (status <
        (GLint)sizeof(
            "mesaGL_uniform_identifier_that_is_intentionally_longer_than_the_old_forty_seven_limit[0]"))
        return 17;
    {
        const char expected[] =
            "mesaGL_uniform_identifier_that_is_intentionally_longer_than_the_old_forty_seven_limit[0]";
        GLint uniform_index;
        int found = 0;

        for (uniform_index = 0; uniform_index < active_uniforms; ++uniform_index) {
            GLsizei active_length = -1;

            glGetActiveUniform(program, (GLuint)uniform_index, sizeof(active_name),
                               &active_length, &active_size, &active_type, active_name);
            if (strcmp(active_name, expected))
                continue;
            if (active_length != (GLsizei)strlen(expected) || active_size != 1)
                return 47;
            found = 1;
        }
        if (!found)
            return 48;
    }
    glGetActiveUniform(program, 0, sizeof(active_name), NULL, &active_size, &active_type,
                       active_name);
    if (active_size != 1 || active_name[0] == '\0')
        return 8;
    {
        GLsizei active_length = -1;

        glGetActiveUniform(program, 0, 0, &active_length, &active_size,
                           &active_type, NULL);
        if (active_length != 0 || glGetError() != GL_NO_ERROR)
            return 43;
    }
    glGetActiveUniform(program, 2, sizeof(active_name), NULL, &active_size, &active_type,
                       active_name);
    if (active_size != 2 || strcmp(active_name, "palette[0]"))
        return 16;
    glGetActiveAttrib(program, 3, sizeof(active_name), NULL, &active_size, &active_type,
                      active_name);
    if (active_size != 1 || active_type != GL_FLOAT_MAT2 ||
        strcmp(active_name, "InputMatrix"))
        return 22;
    long_attribute_location = glGetAttribLocation(
        program,
        "mesaGL_attribute_identifier_that_is_intentionally_longer_than_the_old_forty_seven_limit");
    if (long_attribute_location < 0)
        return 46;
    {
        GLsizei active_length = -1;

        glGetActiveAttrib(program, 3, 0, &active_length, &active_size,
                          &active_type, NULL);
        if (active_length != 0 || glGetError() != GL_NO_ERROR)
            return 44;
    }
    glValidateProgram(program);
    glGetProgramiv(program, GL_VALIDATE_STATUS, &status);
    if (!status)
        return 9;
    glUniformMatrix4fv(transform_location, 1, GL_FALSE, identity);
    glUniform4f(tint_location, 0.2f, 0.6f, 0.9f, 1.0f);
    glUniform4fv(palette_location, 2, palette);
    glUniform1i(selected_location, 1);
    glUniform1f(long_identifier_location, 1.0f);
    glGetUniformfv(program, tint_location, tint_value);
    if (tint_value[0] != 0.2f || tint_value[1] != 0.6f || tint_value[2] != 0.9f ||
        tint_value[3] != 1.0f)
        return 10;
    glUniform3f(tint_location, 0.0f, 0.0f, 0.0f);
    if (glGetError() != GL_INVALID_OPERATION)
        return 23;
    glUniformMatrix4fv(transform_location, 1, GL_TRUE, zero_matrix);
    if (glGetError() != GL_INVALID_VALUE)
        return 24;
    glGetUniformfv(program, transform_location, matrix_value);
    if (memcmp(matrix_value, identity, sizeof(identity)))
        return 25;
    for (i = 0; i < 16; ++i) {
        rounded_matrix[i] = (GLfloat)i + 0.75f;
        matrix_integer[i] = -99;
    }
    glUniformMatrix4fv(transform_location, 1, GL_FALSE, rounded_matrix);
    glGetUniformiv(program, transform_location, matrix_integer);
    for (i = 0; i < 16; ++i)
        if (matrix_integer[i] != i + 1)
            return 96;
    glUniformMatrix4fv(transform_location, 1, GL_FALSE, identity);
    glUniform4fv(palette_location + 1, 2, zero_palette);
    if (glGetError() != GL_INVALID_OPERATION)
        return 26;
    glUniform1f(9999, 1.0f);
    if (glGetError() != GL_INVALID_OPERATION)
        return 27;
    glUniform1f(-1, 1.0f);
    if (glGetError() != GL_NO_ERROR)
        return 28;
    glGetUniformfv(program, 9999, tint_value);
    if (glGetError() != GL_INVALID_OPERATION)
        return 30;
    if (glGetUniformLocation(9999, "value") != -1 || glGetError() != GL_INVALID_VALUE)
        return 31;

    glGenBuffers(1, &vertex_buffer);
    glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);
    matrix_attribute = glGetAttribLocation(program, "InputMatrix");
    if (matrix_attribute < 0 || matrix_attribute + 1 >= 8)
        return 21;
    glEnableVertexAttribArray((GLuint)matrix_attribute);
    glEnableVertexAttribArray((GLuint)matrix_attribute + 1);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), 0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          (const void *)offsetof(Vertex, uv));
    glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Vertex),
                          (const void *)offsetof(Vertex, color));
    glVertexAttribPointer((GLuint)matrix_attribute, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          (const void *)offsetof(Vertex, transform));
    glVertexAttribPointer((GLuint)matrix_attribute + 1, 2, GL_FLOAT, GL_FALSE,
                          sizeof(Vertex),
                          (const void *)(offsetof(Vertex, transform) + 2 * sizeof(float)));

    glGenBuffers(1, &index_buffer);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, index_buffer);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, texture_pixels);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthRangef(0.2f, 0.8f);
    glViewport(0, 0, 64, 64);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glDrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_SHORT, 0);
    glReadPixels(32, 32, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, center);
    if (center[0] < 50 || center[0] > 65 || center[1] < 145 || center[1] > 165 ||
        center[2] < 8 || center[2] > 18) {
        fprintf(stderr, "unexpected center pixel: %u %u %u %u\n", center[0], center[1],
                center[2], center[3]);
        return 11;
    }

    for (i = 0; i < 64 * 64; ++i)
        if (pixels[i])
            ++changed;
    glDisable(GL_BLEND);
    glClear(GL_COLOR_BUFFER_BIT);
    glDrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_SHORT,
                   (const void *)(3 * sizeof(uint16_t)));
    glReadPixels(32, 32, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, center);
    if (center[0] != 0 || center[1] < 250 || center[2] != 0)
        return 18;
    glClear(GL_COLOR_BUFFER_BIT);
    glDrawArrays(GL_POINTS, 0, 1);
    glReadPixels(3, 3, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, center);
    if (center[0] < 28 || center[0] > 40 || center[1] < 212 || center[1] > 226 || center[2]) {
        fprintf(stderr, "unexpected first point pixel: %u %u %u %u\n",
                center[0], center[1], center[2], center[3]);
        return 19;
    }
    glReadPixels(8, 8, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, center);
    if (center[0] < 186 || center[0] > 204 || center[1] < 54 || center[1] > 68 || center[2]) {
        fprintf(stderr, "unexpected second point pixel: %u %u %u %u\n",
                center[0], center[1], center[2], center[3]);
        return 20;
    }
    glEnable(GL_BLEND);
    glUniform4f(tint_location, 1.0f, 0.0f, 0.0f, 0.2f);
    glClear(GL_COLOR_BUFFER_BIT);
    glDrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_SHORT, 0);
    glReadPixels(32, 32, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, center);
    if (center[0] || center[1] || center[2])
        return 12;

    glDeleteBuffers(1, &index_buffer);
    glDeleteBuffers(1, &vertex_buffer);
    glDeleteTextures(1, &texture);
    glDeleteProgram(program);
    glGetProgramiv(program, GL_DELETE_STATUS, &status);
    if (!status || !glIsProgram(program))
        return 14;
    glUseProgram(0);
    glUniform1f(tint_location, 1.0f);
    if (glGetError() != GL_INVALID_OPERATION)
        return 29;
    if (glIsProgram(program) || glIsShader(vertex_shader) || glIsShader(fragment_shader))
        return 15;
    ntglDestroyContext(context);
    return changed > 1000 ? 0 : 5;
}
