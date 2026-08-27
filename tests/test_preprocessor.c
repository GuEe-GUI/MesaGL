#include "GLES2/gl2.h"
#include "mesaGL/ntgl.h"

#include <stdio.h>
#include <string.h>

static int compile_status(GLenum type, const char *source, char *log, int log_size)
{
    GLuint shader = glCreateShader(type);
    GLint status = 0;

    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if (log)
        glGetShaderInfoLog(shader, log_size, NULL, log);
    glDeleteShader(shader);
    return status;
}

int main(void)
{
    unsigned int pixels[4] = {0};
    NTGLframebuffer framebuffer = {
        pixels, 2, 2, 2 * (int)sizeof(*pixels), NTGL_XRGB8888, NTGL_ORIGIN_TOP_LEFT};
    NTGLcontext *context = ntglCreateContext(&framebuffer, NULL);
    const char valid[] =
        "#version 100\n"
        "precision mediump float;\n"
        "#define OUTPUT gl_FragColor\n"
        "#define FINAL_OUTPUT OUTPUT\n"
        "#define MAKE_COLOR(x) vec4(x)\n"
        "#define FORWARD_COLOR(x) MAKE_COLOR(x)\n"
        "#define ENABLED 1\n"
        "#define REQUIRED_VERSION (50 + 50)\n"
        "#if defined(GL_ES) && defined(GL_OES_standard_derivatives) && "
        "(__VERSION__ >= REQUIRED_VERSION)\n"
        "#if ENABLED && ((3 << 2) == 12)\n"
        "float keep(const float value) { return value; }\n"
        "void main() { FINAL_OUTPUT = FORWARD_COLOR(keep(1.0)); }\n"
        "#else\n"
        "invalid inactive text {\n"
        "#endif\n"
        "#endif\n";
    const char bad_version[] = "#version 300\nvoid main() { gl_FragColor = vec4(1.0); }\n";
    const char bad_version_suffix[] =
        "#version 100 desktop\nvoid main() { gl_FragColor = vec4(1.0); }\n";
    const char duplicate_version[] =
        "#version 100\n#version 100\nvoid main() { gl_FragColor = vec4(1.0); }\n";
    const char late_version[] =
        "#define BEFORE_VERSION 1\n#version 100\n"
        "void main() { gl_FragColor = vec4(float(BEFORE_VERSION)); }\n";
    const char bad_arguments[] =
        "#define COLOR(x, y) vec4(x, y)\nvoid main() { gl_FragColor = COLOR(1.0); }\n";
    const char unterminated[] =
        "#ifdef GL_ES\nvoid main() { gl_FragColor = vec4(1.0); }\n";
    const char invalid_expression[] =
        "#if (1 + )\nvoid main() { gl_FragColor = vec4(1.0); }\n#endif\n";
    const char invalid_constant[] =
        "const int COUNT = missing_name + 1;\n"
        "void main() { gl_FragColor = vec4(float(COUNT)); }\n";
    const char invalid_token_paste[] =
        "#define JOIN(a, b) a ## b\n"
        "void main() { gl_FragColor = JOIN(vec, 4)(1.0); }\n";
    const char invalid_stringification[] =
        "#define STRING(value) #value\n"
        "void main() { gl_FragColor = vec4(1.0); }\n";
    const char valid_line_directive[] =
        "#version 100\n"
        "#define TARGET_LINE 40\n"
        "#define CURRENT_LINE __LINE__\n"
        "#if 0\n"
        "#line invalid inactive arguments\n"
        "#endif\n"
        "#line (TARGET_LINE + 2) (3 + 4)\n"
        "const int LINE_NUMBER = CURRENT_LINE;\n"
        "const int FILE_NUMBER = __FILE__;\n"
        "void main() {\n"
        "    gl_FragColor = vec4(float(LINE_NUMBER == 42 && FILE_NUMBER == 7));\n"
        "}\n";
    const char invalid_line_missing[] =
        "#line\nvoid main() { gl_FragColor = vec4(1.0); }\n";
    const char invalid_line_negative[] =
        "#line -1\nvoid main() { gl_FragColor = vec4(1.0); }\n";
    const char invalid_line_extra[] =
        "#line 10 2 3\nvoid main() { gl_FragColor = vec4(1.0); }\n";
    const char valid_comments[] =
        "/* A directive may follow a block comment. */ #define COLOR_VALUE 1.0\n"
        "// #define COLOR_VALUE invalid text\n"
        "/* #line invalid inactive text\n"
        "   #define COLOR_VALUE also_invalid */\n"
        "void main() { gl_FragColor = vec4(COLOR_VALUE); }\n";
    const char unterminated_comment[] =
        "void main() { gl_FragColor = vec4(1.0); } /* missing close\n";
    const char invalid_else_arguments[] =
        "#if 1\n#else unexpected\n#endif\n"
        "void main() { gl_FragColor = vec4(1.0); }\n";
    const char invalid_endif_arguments[] =
        "#if 1\n#endif unexpected\n"
        "void main() { gl_FragColor = vec4(1.0); }\n";
    const char invalid_line_continuation[] =
        "#define TWO (1.0 + \\\n"
        "                     1.0)\n"
        "const int PHYSICAL_LINE = __LINE__;\n"
        "void main() {\n"
        "    gl_FragColor = vec4(TWO * 0.5 * float(PHYSICAL_LINE == 3));\n"
        "}\n";
    const char recursive_object_macro[] =
        "#define FIRST SECOND\n#define SECOND FIRST\n"
        "void main() { gl_FragColor = vec4(float(FIRST)); }\n";
    const char recursive_function_macro[] =
        "#define RECURSE(value) RECURSE(value)\n"
        "void main() { gl_FragColor = vec4(RECURSE(1.0)); }\n";
    const char valid_null_and_inactive_error[] =
        "#\n#if 0\n#error must remain inactive\n#endif\n"
        "void main() { gl_FragColor = vec4(1.0); }\n";
    const char long_macro_tokens[] =
        "#define mesaGL_macro_name_longer_than_the_old_forty_seven_character_limit "
        "(0.0 + 0.0 + 0.0 + 0.0 + 0.0 + 0.0 + 0.0 + 0.0 + 0.0 + 0.0 + "
        "0.0 + 0.0 + 0.0 + 0.0 + 0.0 + 0.0 + 0.0 + 0.0 + 0.0 + 1.0)\n"
        "#define mesaGL_apply_long_parameter("
        "mesaGL_parameter_name_longer_than_the_old_forty_seven_character_limit) "
        "vec4(mesaGL_parameter_name_longer_than_the_old_forty_seven_character_limit)\n"
        "void main() { gl_FragColor = mesaGL_apply_long_parameter("
        "mesaGL_macro_name_longer_than_the_old_forty_seven_character_limit); }\n";
    const char active_error[] =
        "#error selected shader configuration is invalid\n"
        "void main() { gl_FragColor = vec4(1.0); }\n";
    const char unknown_pragma[] =
        "#pragma implementation_specific ignored tokens\n"
        "void main() { gl_FragColor = vec4(1.0); }\n";
    const char valid_control_pragmas[] =
        "#pragma optimize(off)\n#pragma debug(on)\n"
        "#pragma optimize(on)\n#pragma debug(off)\n"
        "void main() { gl_FragColor = vec4(1.0); }\n";
    const char invalid_control_pragma[] =
        "#pragma optimize(maybe)\n"
        "void main() { gl_FragColor = vec4(1.0); }\n";
    const char function_control_pragma[] =
        "void main()\n{\n#pragma debug(on)\n"
        "gl_FragColor = vec4(1.0);\n}\n";
    const char nested_function_control_pragma[] =
        "void main(){if(true){\n#pragma optimize(off)\n}"
        "gl_FragColor=vec4(1.0);}\n";
    const char inactive_fragment_invariant_pragma[] =
        "#if 0\n#pragma STDGL invariant(all)\n#endif\n"
        "void main() { gl_FragColor = vec4(1.0); }\n";
    const char fragment_invariant_pragma[] =
        "#pragma STDGL invariant(all)\n"
        "void main() { gl_FragColor = vec4(1.0); }\n";
    const char fragment_invariant_outputs[] =
        "invariant gl_FragColor, gl_FragData;\n"
        "void main() { gl_FragColor = vec4(1.0); }\n";
    const char fragment_invariant_front_facing[] =
        "invariant gl_FrontFacing;\n"
        "void main() { gl_FragColor = vec4(1.0); }\n";
    const char reserved_gl_macro[] =
        "#define GL_PRIVATE_VALUE 1\n"
        "void main() { gl_FragColor = vec4(1.0); }\n";
    const char reserved_es100_variable[] =
        "precision mediump float; float class;"
        "void main() { gl_FragColor = vec4(1.0); }\n";
    const char reserved_es100_type[] =
        "precision mediump float; sampler2DRect image;"
        "void main() { gl_FragColor = vec4(1.0); }\n";
    const char reserved_es100_sampler[] =
        "precision mediump float; uniform sampler2DShadow shadow_map;"
        "void main() { gl_FragColor = vec4(1.0); }\n";
    const char macro_expanded_reserved_word[] =
        "#define NAME output\nprecision mediump float; float NAME;"
        "void main() { gl_FragColor = vec4(1.0); }\n";
    const char reserved_word_in_comment[] =
        "precision mediump float; /* class output sampler2DRect */"
        "void main() { gl_FragColor = vec4(1.0); }\n";
    const char reserved_word_substring[] =
        "precision mediump float; float classifier;"
        "void main() { classifier = 0.5; gl_FragColor = vec4(classifier); }\n";
    const char invalid_octal_integer[] =
        "precision mediump float; const int value = 09;"
        "void main() { gl_FragColor = vec4(float(value)); }\n";
    const char valid_leading_zero_float[] =
        "precision mediump float; const float value = 09.0;"
        "void main() { gl_FragColor = vec4(value / 9.0); }\n";
    const char invalid_hexadecimal_integer[] =
        "precision mediump float; const int value = 0x;"
        "void main() { gl_FragColor = vec4(float(value)); }\n";
    const char invalid_float_exponent[] =
        "precision mediump float; const float value = 1e+;"
        "void main() { gl_FragColor = vec4(value); }\n";
    const char invalid_numeric_suffix[] =
        "precision mediump float; const int value = 12invalid;"
        "void main() { gl_FragColor = vec4(float(value)); }\n";
    const char invalid_preprocessing_number[] =
        "#define e + 1\n"
        "precision mediump float; const float value = 1e;"
        "void main() { gl_FragColor = vec4(value); }\n";
    const char valid_float_forms[] =
        "precision mediump float; const float first = .5;"
        "const float second = 1.; const float third = 1e-2;"
        "void main() { gl_FragColor = vec4(first, second, third, 1.0); }\n";
    const char wide_decimal_integer[] =
        "precision mediump float; const int value = 2147483648;"
        "void main() { gl_FragColor = vec4(float(value)); }\n";
    const char wide_hexadecimal_integer[] =
        "precision mediump float; const int value = 0x80000000;"
        "void main() { gl_FragColor = vec4(float(value)); }\n";
    const char oversized_decimal_integer[] =
        "precision mediump float; const int value = 4294967296;"
        "void main() { gl_FragColor = vec4(float(value)); }\n";
    const char oversized_hexadecimal_integer[] =
        "precision mediump float; const int value = 0x100000000;"
        "void main() { gl_FragColor = vec4(float(value)); }\n";
    const char maximum_integer_literal[] =
        "precision mediump float; const int value = 2147483647;"
        "void main() { gl_FragColor = vec4(value > 0 ? 1.0 : 0.0); }\n";
    const char reserved_defined_macro[] =
        "#define defined 1\n"
        "void main() { gl_FragColor = vec4(1.0); }\n";
    const char undefine_predefined_macro[] =
        "#undef GL_ES\n"
        "void main() { gl_FragColor = vec4(1.0); }\n";
    const char redefine_version_macro[] =
        "#define __VERSION__ 300\n"
        "void main() { gl_FragColor = vec4(1.0); }\n";
    const char reserved_double_underscore_macro[] =
        "#define user__macro 1\n"
        "void main() { gl_FragColor = vec4(1.0); }\n";
    const char reserved_keyword_macro[] =
        "#define flat 1\n"
        "void main() { gl_FragColor = vec4(1.0); }\n";
    const char reserved_macro_parameter[] =
        "#define PICK(user__value) user__value\n"
        "void main() { gl_FragColor = vec4(PICK(1.0)); }\n";
    const char undefined_condition_identifier[] =
        "#if UNKNOWN_CONDITION\n#endif\n"
        "void main() { gl_FragColor = vec4(1.0); }\n";
    const char short_circuited_undefined_identifier[] =
        "#if 0 && UNKNOWN_CONDITION\n#endif\n"
        "void main() { gl_FragColor = vec4(1.0); }\n";
    const char valid_identical_and_empty_macros[] =
        "#define SCALE (1.0+0.0)\n#define SCALE ( 1.0 + 0.0 )\n#define EMPTY\n"
        "void main() { gl_FragColor = vec4(EMPTY SCALE); }\n";
    const char different_macro_redefinition[] =
        "#define SCALE 1.0\n#define SCALE 0.5\n"
        "void main() { gl_FragColor = vec4(SCALE); }\n";
    const char duplicate_macro_parameter[] =
        "#define PICK(value, value) value\n"
        "void main() { gl_FragColor = vec4(PICK(1.0, 0.0)); }\n";
    const char valid_short_circuit_condition[] =
        "#if (0 && (1 / 0)) || (1 || (1 / 0))\n"
        "#define CONDITIONAL_VALUE (false ? (1.0 / 0.0) : "
        "(true ? 1.0 : (1.0 / 0.0)))\n"
        "#else\n#error short circuit failed\n#endif\n"
        "void main() { gl_FragColor = vec4(CONDITIONAL_VALUE); }\n";
    const char invalid_conditional_expression[] =
        "#if 1 ? 2\n#endif\n"
        "void main() { gl_FragColor = vec4(1.0); }\n";
    const char unsupported_preprocessor_conditional[] =
        "#if 1 ? 1 : 0\n#endif\n"
        "void main() { gl_FragColor = vec4(1.0); }\n";
    const char valid_precision_defaults[] =
        "precision highp float; precision mediump int;"
        "precision lowp sampler2D; precision lowp samplerCube;"
        "void main() { gl_FragColor = vec4(1.0); }\n";
    const char invalid_precision_qualifier[] =
        "precision fastest float;"
        "void main() { gl_FragColor = vec4(1.0); }\n";
    const char invalid_precision_type[] =
        "precision mediump vec4;"
        "void main() { gl_FragColor = vec4(1.0); }\n";
    const char invalid_boolean_precision[] =
        "precision mediump float; void main() { lowp bool value = true;"
        "gl_FragColor = vec4(value ? 1.0 : 0.0); }\n";
    const char invalid_boolean_vector_precision[] =
        "precision mediump float; void main() { mediump bvec2 value = bvec2(true);"
        "gl_FragColor = vec4(value.x ? 1.0 : 0.0); }\n";
    const char invalid_structure_precision[] =
        "precision mediump float; struct State { float value; };"
        "void main() { highp State state; state.value = 1.0;"
        "gl_FragColor = vec4(state.value); }\n";
    const char invalid_void_precision[] =
        "precision mediump float; lowp void helper() { }"
        "void main() { helper(); gl_FragColor = vec4(1.0); }\n";
    const char valid_precision_targets[] =
        "precision mediump float; uniform lowp sampler2D image;"
        "highp float helper(mediump ivec2 value) { lowp mat2 matrix = mat2(1.0);"
        "return matrix[0].x + float(value.x); }"
        "void main() { gl_FragColor = vec4(helper(ivec2(0))); }\n";
    const char local_precision_default[] =
        "void main() { precision mediump float; gl_FragColor = vec4(1.0); }\n";
    const char missing_fragment_float_precision[] =
        "void main() { float value = 1.0; gl_FragColor = vec4(value); }\n";
    const char explicit_fragment_float_precision[] =
        "highp float helper(highp float value) { return value; }\n"
        "void main() { gl_FragColor = vec4(helper(1.0)); }\n";
    const char constructor_without_float_declaration[] =
        "void main() { gl_FragColor = vec4(1.0); }\n";
    const char missing_fragment_parameter_precision[] =
        "highp float helper(float value) { return value; }\n"
        "void main() { gl_FragColor = vec4(helper(1.0)); }\n";
    const char valid_fragment_derivative[] =
        "#extension GL_OES_standard_derivatives : enable\n"
        "void main() { gl_FragColor = vec4(fwidth(gl_FragCoord.x)); }\n";
    const char invalid_fragment_derivative[] =
        "void main() { gl_FragColor = vec4(dFdx(gl_FragCoord.x)); }\n";
    const char commented_fragment_derivative_extension[] =
        "/* #extension GL_OES_standard_derivatives : enable */\n"
        "void main() { gl_FragColor = vec4(dFdx(gl_FragCoord.x)); }\n";
    const char inactive_fragment_derivative_extension[] =
        "#if 0\n#extension GL_OES_standard_derivatives : enable\n#endif\n"
        "void main() { gl_FragColor = vec4(dFdx(gl_FragCoord.x)); }\n";
    const char disabled_fragment_derivative_extension[] =
        "#extension GL_OES_standard_derivatives : enable\n"
        "#extension GL_OES_standard_derivatives : disable\n"
        "void main() { gl_FragColor = vec4(dFdx(gl_FragCoord.x)); }\n";
    const char unsupported_required_extension[] =
        "#extension GL_NOT_A_REAL_extension : require\n"
        "void main() { gl_FragColor = vec4(1.0); }\n";
    const char unsupported_enabled_extension[] =
        "#extension GL_NOT_A_REAL_extension : enable\n"
        "void main() { gl_FragColor = vec4(1.0); }\n";
    const char warned_fragment_derivative[] =
        "#extension GL_OES_standard_derivatives : warn\n"
        "void main() { gl_FragColor = vec4(fwidth(gl_FragCoord.x)); }\n";
    const char all_warned_fragment_derivative[] =
        "#extension all : warn\n"
        "void main() { gl_FragColor = vec4(dFdx(gl_FragCoord.x)); }\n";
    const char invalid_all_extension_behavior[] =
        "#extension all : enable\n"
        "void main() { gl_FragColor = vec4(1.0); }\n";
    const char late_extension_directive[] =
        "precision mediump float;\n"
        "#extension GL_OES_standard_derivatives : enable\n"
        "void main() { gl_FragColor = vec4(1.0); }\n";
    const char invalid_vertex_derivative[] =
        "void main() { float width = fwidth(1.0); gl_Position = vec4(width); }\n";
    const char valid_vertex_lod[] =
        "uniform sampler2D image;\n"
        "void main() { gl_Position = texture2DLod(image, vec2(0.5), 0.0); }\n";
    const char invalid_fragment_lod[] =
        "uniform sampler2D image;\n"
        "void main() { gl_FragColor = texture2DLod(image, vec2(0.5), 0.0); }\n";
    const char local_uniform_scope[] =
        "void main() { uniform float value; gl_FragColor = vec4(value); }\n";
    const char parameter_varying_scope[] =
        "highp float helper(varying highp float value) { return value; }\n"
        "void main() { gl_FragColor = vec4(helper(1.0)); }\n";
    const char precision_before_storage[] =
        "highp uniform float value;\n"
        "void main() { gl_FragColor = vec4(value); }\n";
    const char multiple_storage_qualifiers[] =
        "uniform const highp float value;\n"
        "void main() { gl_FragColor = vec4(value); }\n";
    const char precision_before_varying[] =
        "invariant highp varying vec4 color;\n"
        "void main() { gl_FragColor = color; }\n";
    const char valid_qualified_varying[] =
        "invariant varying highp vec4 color;\n"
        "void main() { gl_FragColor = color; }\n";
    char returned[sizeof(valid)];
    char log[128];
    char oversized_macro_name[1200];
    const char embedded_nul_source[] =
        "void main(){gl_FragColor=vec4(1.0);}\0ignored";
    const GLchar *mapped_condition_sources[] = {
        "#line 20 4\n#define MAPPED_FILE __FILE__\n",
        "#if MAPPED_FILE == 5 && __LINE__ == 1\n"
        "#define MAPPED_VALUE 1.0\n#else\n#error wrong source mapping\n#endif\n",
        "void main(){gl_FragColor=vec4(MAPPED_VALUE);}\n"};
    const GLchar *mapped_error_sources[] = {
        "#line 20 4\n", "\n", "#error mapped source failure\n"};
    char embedded_nul_returned[sizeof(embedded_nul_source)];
    GLuint shader;
    GLint length = 0;

    if (!context)
        return 1;
    ntglMakeCurrent(context);
    if (!compile_status(GL_FRAGMENT_SHADER, valid, log, sizeof(log))) {
        fprintf(stderr, "valid preprocessor shader: %s\n", log);
        return 2;
    }
    if (compile_status(GL_FRAGMENT_SHADER, bad_version, log, sizeof(log)) ||
        !strstr(log, "version 100"))
        return 3;
    if (compile_status(GL_FRAGMENT_SHADER, bad_version_suffix, log, sizeof(log)) ||
        !strstr(log, "version 100"))
        return 24;
    if (compile_status(GL_FRAGMENT_SHADER, duplicate_version, log, sizeof(log)) ||
        !strstr(log, "version 100"))
        return 35;
    if (compile_status(GL_FRAGMENT_SHADER, late_version, log, sizeof(log)) ||
        !strstr(log, "version 100"))
        return 36;
    if (compile_status(GL_FRAGMENT_SHADER, bad_arguments, log, sizeof(log)) ||
        !strstr(log, "macro expansion"))
        return 4;
    if (compile_status(GL_FRAGMENT_SHADER, unterminated, log, sizeof(log)) ||
        !strstr(log, "unterminated"))
        return 5;
    if (compile_status(GL_FRAGMENT_SHADER, invalid_expression, log, sizeof(log)) ||
        !strstr(log, "expression"))
        return 6;
    if (compile_status(GL_FRAGMENT_SHADER, invalid_constant, log, sizeof(log)) ||
        !strstr(log, "constant"))
        return 8;
    if (compile_status(GL_FRAGMENT_SHADER, invalid_token_paste, log, sizeof(log)) ||
        !strstr(log, "illegal in GLES"))
        return 14;
    if (compile_status(GL_FRAGMENT_SHADER, invalid_stringification, log, sizeof(log)) ||
        !strstr(log, "illegal in GLES"))
        return 15;
    if (!compile_status(GL_FRAGMENT_SHADER, valid_line_directive, log, sizeof(log)))
        return 20;
    if (compile_status(GL_FRAGMENT_SHADER, invalid_line_missing, log, sizeof(log)) ||
        !strstr(log, "#line"))
        return 21;
    if (compile_status(GL_FRAGMENT_SHADER, invalid_line_negative, log, sizeof(log)) ||
        !strstr(log, "#line"))
        return 22;
    if (compile_status(GL_FRAGMENT_SHADER, invalid_line_extra, log, sizeof(log)) ||
        !strstr(log, "#line"))
        return 23;
    if (!compile_status(GL_FRAGMENT_SHADER, valid_comments, log, sizeof(log)))
        return 25;
    if (compile_status(GL_FRAGMENT_SHADER, unterminated_comment, log, sizeof(log)) ||
        !strstr(log, "unterminated shader block comment"))
        return 26;
    if (compile_status(GL_FRAGMENT_SHADER, invalid_else_arguments, log, sizeof(log)) ||
        !strstr(log, "directive"))
        return 27;
    if (compile_status(GL_FRAGMENT_SHADER, invalid_endif_arguments, log, sizeof(log)) ||
        !strstr(log, "directive"))
        return 28;
    if (compile_status(GL_FRAGMENT_SHADER, invalid_line_continuation, log,
                       sizeof(log)) ||
        !strstr(log, "backslash"))
        return 34;
    compile_status(GL_FRAGMENT_SHADER, recursive_object_macro, log, sizeof(log));
    if (strstr(log, "macro expansion"))
        return 37;
    compile_status(GL_FRAGMENT_SHADER, recursive_function_macro, log, sizeof(log));
    if (strstr(log, "macro expansion"))
        return 38;
    if (!compile_status(GL_FRAGMENT_SHADER, valid_null_and_inactive_error, log, sizeof(log)))
        return 39;
    if (compile_status(GL_FRAGMENT_SHADER, active_error, log, sizeof(log)) ||
        !strstr(log, "#error") ||
        !strstr(log, "selected shader configuration is invalid"))
        return 40;
    if (!compile_status(GL_FRAGMENT_SHADER, unknown_pragma, log, sizeof(log)))
        return 41;
    if (!compile_status(GL_FRAGMENT_SHADER, valid_control_pragmas, log,
                        sizeof(log)))
        return 90;
    if (compile_status(GL_FRAGMENT_SHADER, invalid_control_pragma, log,
                       sizeof(log)) ||
        !strstr(log, "pragma"))
        return 91;
    if (compile_status(GL_FRAGMENT_SHADER, function_control_pragma, log,
                       sizeof(log)) ||
        !strstr(log, "outside functions"))
        return 92;
    if (compile_status(GL_FRAGMENT_SHADER, nested_function_control_pragma,
                       log, sizeof(log)) ||
        !strstr(log, "outside functions"))
        return 93;
    if (!compile_status(GL_FRAGMENT_SHADER, inactive_fragment_invariant_pragma, log,
                        sizeof(log)))
        return 42;
    if (!compile_status(GL_FRAGMENT_SHADER, fragment_invariant_pragma, log,
                        sizeof(log)))
        return 43;
    if (!compile_status(GL_FRAGMENT_SHADER, fragment_invariant_outputs, log,
                        sizeof(log)))
        return 108;
    if (compile_status(GL_FRAGMENT_SHADER, fragment_invariant_front_facing, log,
                       sizeof(log)))
        return 109;
    if (compile_status(GL_FRAGMENT_SHADER, reserved_gl_macro, log, sizeof(log)) ||
        !strstr(log, "reserved shader macro"))
        return 44;
    if (compile_status(GL_FRAGMENT_SHADER, reserved_defined_macro, log, sizeof(log)) ||
        !strstr(log, "reserved shader macro"))
        return 45;
    if (compile_status(GL_FRAGMENT_SHADER, undefine_predefined_macro, log, sizeof(log)) ||
        !strstr(log, "reserved shader macro"))
        return 46;
    if (compile_status(GL_FRAGMENT_SHADER, redefine_version_macro, log, sizeof(log)) ||
        !strstr(log, "reserved shader macro"))
        return 47;
    if (compile_status(GL_FRAGMENT_SHADER, reserved_double_underscore_macro,
                       log, sizeof(log)) ||
        !strstr(log, "reserved shader macro"))
        return 78;
    if (compile_status(GL_FRAGMENT_SHADER, reserved_keyword_macro,
                       log, sizeof(log)) ||
        !strstr(log, "reserved shader macro"))
        return 81;
    if (compile_status(GL_FRAGMENT_SHADER, reserved_macro_parameter,
                       log, sizeof(log)) ||
        !strstr(log, "reserved shader macro parameter"))
        return 82;
    if (compile_status(GL_FRAGMENT_SHADER, undefined_condition_identifier,
                       log, sizeof(log)) ||
        !strstr(log, "expression"))
        return 79;
    if (!compile_status(GL_FRAGMENT_SHADER,
                        short_circuited_undefined_identifier,
                        log, sizeof(log)))
        return 80;
    if (!compile_status(GL_FRAGMENT_SHADER, valid_identical_and_empty_macros, log,
                        sizeof(log)))
        return 48;
    if (compile_status(GL_FRAGMENT_SHADER, different_macro_redefinition, log, sizeof(log)) ||
        !strstr(log, "redefined"))
        return 49;
    if (compile_status(GL_FRAGMENT_SHADER, duplicate_macro_parameter, log, sizeof(log)) ||
        !strstr(log, "duplicate"))
        return 50;
    if (!compile_status(GL_FRAGMENT_SHADER, valid_short_circuit_condition, log,
                        sizeof(log))) {
        fprintf(stderr, "short-circuit shader: %s\n", log);
        return 51;
    }
    if (compile_status(GL_FRAGMENT_SHADER, invalid_conditional_expression, log, sizeof(log)) ||
        !strstr(log, "expression"))
        return 52;
    if (compile_status(GL_FRAGMENT_SHADER, unsupported_preprocessor_conditional,
                       log, sizeof(log)) ||
        !strstr(log, "expression"))
        return 86;
    if (!compile_status(GL_FRAGMENT_SHADER, valid_precision_defaults, log,
                        sizeof(log)))
        return 16;
    if (compile_status(GL_FRAGMENT_SHADER, invalid_precision_qualifier, log,
                       sizeof(log)) || !strstr(log, "precision"))
        return 17;
    if (compile_status(GL_FRAGMENT_SHADER, invalid_precision_type, log,
                       sizeof(log)) || !strstr(log, "precision"))
        return 18;
    if (compile_status(GL_FRAGMENT_SHADER, invalid_boolean_precision, log,
                       sizeof(log)) || !strstr(log, "cannot have"))
        return 78;
    if (compile_status(GL_FRAGMENT_SHADER, invalid_boolean_vector_precision, log,
                       sizeof(log)) || !strstr(log, "cannot have"))
        return 79;
    if (compile_status(GL_FRAGMENT_SHADER, invalid_structure_precision, log,
                       sizeof(log)) || !strstr(log, "cannot have"))
        return 80;
    if (compile_status(GL_FRAGMENT_SHADER, invalid_void_precision, log,
                       sizeof(log)) || !strstr(log, "cannot have"))
        return 81;
    if (!compile_status(GL_FRAGMENT_SHADER, valid_precision_targets, log,
                        sizeof(log)))
        return 82;
    if (!compile_status(GL_FRAGMENT_SHADER, local_precision_default, log,
                        sizeof(log)))
        return 19;
    if (compile_status(GL_FRAGMENT_SHADER, missing_fragment_float_precision, log,
                       sizeof(log)) || !strstr(log, "precision qualifier"))
        return 53;
    if (!compile_status(GL_FRAGMENT_SHADER, explicit_fragment_float_precision, log,
                        sizeof(log)))
        return 54;
    if (!compile_status(GL_FRAGMENT_SHADER, constructor_without_float_declaration, log,
                        sizeof(log)))
        return 55;
    if (compile_status(GL_FRAGMENT_SHADER, missing_fragment_parameter_precision, log,
                       sizeof(log)) || !strstr(log, "precision qualifier"))
        return 56;
    if (!compile_status(GL_FRAGMENT_SHADER, valid_fragment_derivative, log, sizeof(log)))
        return 9;
    if (compile_status(GL_FRAGMENT_SHADER, invalid_fragment_derivative, log, sizeof(log)) ||
        !strstr(log, "GL_OES_standard_derivatives"))
        return 13;
    if (compile_status(GL_FRAGMENT_SHADER, commented_fragment_derivative_extension, log,
                       sizeof(log)) || !strstr(log, "GL_OES_standard_derivatives"))
        return 29;
    if (compile_status(GL_FRAGMENT_SHADER, inactive_fragment_derivative_extension, log,
                       sizeof(log)) || !strstr(log, "GL_OES_standard_derivatives"))
        return 30;
    if (compile_status(GL_FRAGMENT_SHADER, disabled_fragment_derivative_extension, log,
                       sizeof(log)) || !strstr(log, "GL_OES_standard_derivatives"))
        return 31;
    if (compile_status(GL_FRAGMENT_SHADER, unsupported_required_extension, log, sizeof(log)) ||
        !strstr(log, "not supported"))
        return 32;
    if (!compile_status(GL_FRAGMENT_SHADER, unsupported_enabled_extension, log,
                        sizeof(log)) ||
        !strstr(log, "warning") || !strstr(log, "not supported"))
        return 87;
    if (!compile_status(GL_FRAGMENT_SHADER, warned_fragment_derivative, log,
                        sizeof(log)) ||
        !strstr(log, "warning") ||
        !strstr(log, "GL_OES_standard_derivatives"))
        return 88;
    if (!compile_status(GL_FRAGMENT_SHADER, all_warned_fragment_derivative, log,
                        sizeof(log)) ||
        !strstr(log, "warning") ||
        !strstr(log, "GL_OES_standard_derivatives"))
        return 89;
    if (compile_status(GL_FRAGMENT_SHADER, invalid_all_extension_behavior, log, sizeof(log)) ||
        !strstr(log, "directive"))
        return 33;
    if (compile_status(GL_FRAGMENT_SHADER, late_extension_directive, log,
                       sizeof(log)) ||
        !strstr(log, "directive"))
        return 83;
    if (compile_status(GL_VERTEX_SHADER, invalid_vertex_derivative, log, sizeof(log)) ||
        !strstr(log, "fragment shader"))
        return 10;
    if (!compile_status(GL_VERTEX_SHADER, valid_vertex_lod, log, sizeof(log)))
        return 11;
    if (compile_status(GL_FRAGMENT_SHADER, invalid_fragment_lod, log, sizeof(log)) ||
        !strstr(log, "vertex shader"))
        return 12;
    if (compile_status(GL_FRAGMENT_SHADER, local_uniform_scope, log, sizeof(log)) ||
        !strstr(log, "global scope"))
        return 57;
    if (compile_status(GL_FRAGMENT_SHADER, parameter_varying_scope, log,
                       sizeof(log)) || !strstr(log, "global scope"))
        return 58;
    if (compile_status(GL_FRAGMENT_SHADER, precision_before_storage, log,
                       sizeof(log)) || !strstr(log, "final qualifier"))
        return 59;
    if (compile_status(GL_FRAGMENT_SHADER, multiple_storage_qualifiers, log,
                       sizeof(log)) || !strstr(log, "multiple storage"))
        return 60;
    if (compile_status(GL_FRAGMENT_SHADER, precision_before_varying, log,
                       sizeof(log)) || !strstr(log, "final qualifier"))
        return 61;
    if (!compile_status(GL_FRAGMENT_SHADER, valid_qualified_varying, log,
                        sizeof(log)))
        return 62;
    if (compile_status(GL_FRAGMENT_SHADER, reserved_es100_variable, log,
                       sizeof(log)) || !strstr(log, "reserved GLSL ES 1.00 word"))
        return 63;
    if (compile_status(GL_FRAGMENT_SHADER, reserved_es100_type, log,
                       sizeof(log)) || !strstr(log, "reserved GLSL ES 1.00 word"))
        return 64;
    if (compile_status(GL_FRAGMENT_SHADER, reserved_es100_sampler, log,
                       sizeof(log)) || !strstr(log, "reserved GLSL ES 1.00 word"))
        return 68;
    if (compile_status(GL_FRAGMENT_SHADER, macro_expanded_reserved_word, log,
                       sizeof(log)) || !strstr(log, "reserved GLSL ES 1.00 word"))
        return 65;
    if (!compile_status(GL_FRAGMENT_SHADER, reserved_word_in_comment, log,
                        sizeof(log)))
        return 66;
    if (!compile_status(GL_FRAGMENT_SHADER, reserved_word_substring, log,
                        sizeof(log)))
        return 67;
    if (compile_status(GL_FRAGMENT_SHADER, invalid_octal_integer, log,
                       sizeof(log)) || !strstr(log, "invalid octal"))
        return 69;
    if (!compile_status(GL_FRAGMENT_SHADER, valid_leading_zero_float, log,
                        sizeof(log)))
        return 70;
    if (compile_status(GL_FRAGMENT_SHADER, invalid_hexadecimal_integer, log,
                       sizeof(log)) || !strstr(log, "numeric literal"))
        return 71;
    if (compile_status(GL_FRAGMENT_SHADER, invalid_float_exponent, log,
                       sizeof(log)) || !strstr(log, "numeric literal"))
        return 72;
    if (compile_status(GL_FRAGMENT_SHADER, invalid_numeric_suffix, log,
                       sizeof(log)) || !strstr(log, "numeric literal"))
        return 73;
    if (compile_status(GL_FRAGMENT_SHADER, invalid_preprocessing_number, log,
                       sizeof(log)) || !strstr(log, "numeric literal"))
        return 127;
    if (!compile_status(GL_FRAGMENT_SHADER, valid_float_forms, log, sizeof(log)))
        return 74;
    if (!compile_status(GL_FRAGMENT_SHADER, wide_decimal_integer, log,
                        sizeof(log)))
        return 75;
    if (!compile_status(GL_FRAGMENT_SHADER, wide_hexadecimal_integer, log,
                        sizeof(log)))
        return 76;
    if (compile_status(GL_FRAGMENT_SHADER, oversized_decimal_integer, log,
                       sizeof(log)) || !strstr(log, "32-bit"))
        return 78;
    if (compile_status(GL_FRAGMENT_SHADER, oversized_hexadecimal_integer, log,
                       sizeof(log)) || !strstr(log, "32-bit"))
        return 79;
    if (!compile_status(GL_FRAGMENT_SHADER, maximum_integer_literal, log,
                        sizeof(log)))
        return 77;
    if (!compile_status(GL_FRAGMENT_SHADER, long_macro_tokens, log, sizeof(log))) {
        fprintf(stderr, "long macro shader: %s\n", log);
        return 84;
    }
    memcpy(oversized_macro_name, "#define ", 8);
    memset(oversized_macro_name + 8, 'a', 1025);
    strcpy(oversized_macro_name + 8 + 1025,
           " 1\nvoid main(){gl_FragColor=vec4(1.0);}\n");
    if (!compile_status(GL_FRAGMENT_SHADER, oversized_macro_name, log,
                        sizeof(log)))
        return 85;

    shader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(shader, 1, (const GLchar *const *)&(const char *){valid}, NULL);
    glGetShaderSource(shader, sizeof(returned), &length, returned);
    if (length != (GLint)strlen(valid) || strcmp(returned, valid))
        return 7;
    glDeleteShader(shader);

    shader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(shader, 3, mapped_error_sources, NULL);
    glCompileShader(shader);
    glGetShaderiv(shader, GL_COMPILE_STATUS, &length);
    glGetShaderInfoLog(shader, sizeof(log), NULL, log);
    if (length || !strstr(log, "6:1:") ||
        !strstr(log, "mapped source failure"))
        return 97;
    glDeleteShader(shader);

    shader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(shader, 3, mapped_condition_sources, NULL);
    glCompileShader(shader);
    glGetShaderiv(shader, GL_COMPILE_STATUS, &length);
    if (!length) {
        glGetShaderInfoLog(shader, sizeof(log), NULL, log);
        fprintf(stderr, "mapped preprocessor condition: %s\n", log);
        return 96;
    }
    glDeleteShader(shader);

    shader = glCreateShader(GL_FRAGMENT_SHADER);
    {
        const GLchar *embedded_source = embedded_nul_source;
        GLint embedded_length = (GLint)sizeof(embedded_nul_source) - 1;
        GLint returned_length = -1;
        GLint source_length = 0;

        glShaderSource(shader, 1, &embedded_source, &embedded_length);
        glGetShaderiv(shader, GL_SHADER_SOURCE_LENGTH, &source_length);
        glGetShaderSource(shader, sizeof(embedded_nul_returned),
                          &returned_length, embedded_nul_returned);
        if (source_length != embedded_length + 1 ||
            returned_length != embedded_length ||
            memcmp(embedded_nul_returned, embedded_nul_source,
                   (size_t)embedded_length))
            return 94;
        glCompileShader(shader);
        glGetShaderiv(shader, GL_COMPILE_STATUS, &source_length);
        glGetShaderInfoLog(shader, sizeof(log), NULL, log);
        if (source_length || !strstr(log, "NUL"))
            return 95;
    }
    glDeleteShader(shader);
    ntglDestroyContext(context);
    return 0;
}
