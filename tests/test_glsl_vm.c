#include "glsl_vm.h"
#include "mesaGL/config.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int lookup(void *user, const char *name, size_t length, MesaGLSLValue *value)
{
    (void)user;
    memset(value, 0, sizeof(*value));
    if (length == 3 && !strncmp(name, "mvp", length)) {
        value->rows = 4;
        value->columns = 4;
        value->data[0] = 2.0f;
        value->data[5] = 3.0f;
        value->data[10] = 1.0f;
        value->data[15] = 1.0f;
        return 1;
    }
    if (length == 8 && !strncmp(name, "position", length)) {
        value->rows = 2;
        value->columns = 1;
        value->data[0] = 0.25f;
        value->data[1] = -0.5f;
        return 1;
    }
    if (length == 4 && !strncmp(name, "tint", length)) {
        value->rows = 4;
        value->columns = 1;
        value->data[0] = 0.2f;
        value->data[1] = 0.4f;
        value->data[2] = 0.6f;
        value->data[3] = 0.8f;
        return 1;
    }
    if (length == 2 && !strncmp(name, "uv", length)) {
        value->rows = 2;
        value->columns = 1;
        value->data[0] = 0.25f;
        value->data[1] = 0.5f;
        value->dfdx[0] = 0.125f;
        value->dfdx[1] = 0.0f;
        value->dfdy[0] = 0.0f;
        value->dfdy[1] = 0.25f;
        value->has_derivatives = 1;
        return 1;
    }
    return 0;
}

static int assign(void *user, const char *name, size_t length, const char *swizzle,
                  size_t swizzle_length, int array_index, const MesaGLSLValue *value)
{
    MesaGLSLValue *output = (MesaGLSLValue *)user;

    (void)swizzle;
    (void)swizzle_length;
    if (length != 6 || strncmp(name, "output", length) || array_index >= 0)
        return 0;
    *output = *value;
    return 1;
}

int main(void)
{
    MesaGLSLValue value;
    MesaGLSLValue output = {
        {0}, 4, 1, NULL, 0, NULL, NULL, 0, {0}, {0}, 0, MESAGL_GLSL_TYPE_FLOAT, NULL, 0};
    int discarded;
    const char *error_at = NULL;

    if (!mesaGLSLExpression("mvp * vec4(position, 0.0, 1.0)", NULL, lookup, NULL, NULL,
                            &value, NULL))
        return 1;
    if (value.rows != 4 || value.data[0] != 0.5f || value.data[1] != -1.5f ||
        value.data[2] != 0.0f || value.data[3] != 1.0f)
        return 2;
    if (!mesaGLSLExpression("tint.bgra * 0.5 + vec4(0.1)", NULL, lookup, NULL, NULL,
                            &value, &error_at)) {
        fprintf(stderr, "expression error near: %.32s\n", error_at ? error_at : "(unknown)");
        return 3;
    }
    if (value.data[0] != 0.4f || value.data[1] != 0.3f || value.data[2] != 0.2f ||
        value.data[3] != 0.5f)
        return 4;
    if (!mesaGLSLExpression("mix(vec3(0.0), normalize(vec3(3.0, 0.0, 4.0)), 0.5)",
                            NULL, lookup, NULL, NULL, &value, NULL))
        return 5;
    if (value.rows != 3 || value.data[0] != 0.3f || value.data[1] != 0.0f ||
        value.data[2] != 0.4f)
        return 6;
    if (!mesaGLSLExpression("dot(tint.rgb, vec3(1.0)) > 1.0 ? tint : vec4(0.0)", NULL,
                            lookup, NULL, NULL, &value, NULL))
        return 7;
    if (value.rows != 4 || value.data[0] != 0.2f || value.data[3] != 0.8f)
        return 8;
    if (!mesaGLSLExecute("vec4 local = tint; local.rgb *= 0.5;"
                         "if (local.a > 0.5) { output = local; }"
                         "else { discard; }",
                         lookup, NULL, assign, &output, &discarded, NULL))
        return 9;
    if (discarded || output.data[0] != 0.1f || output.data[1] != 0.2f ||
        output.data[2] != 0.3f || output.data[3] != 0.8f)
        return 10;
    if (!mesaGLSLExecute("float sum = 0.0; for (int i = 0; i < 4; i++) {"
                         "if (i == 2) { continue; } sum += 1.0; }"
                         "while (sum < 5.0) { sum += 1.0; } output = vec4(sum);",
                         lookup, NULL, assign, &output, &discarded, NULL))
        return 11;
    if (discarded || output.data[0] != 5.0f || output.data[1] != 5.0f ||
        output.data[2] != 5.0f || output.data[3] != 5.0f)
        return 12;
    if (!mesaGLSLExecuteProgram(
            "float factorial(float n) { if (n <= 1.0) { return 1.0; } "
            "return n * factorial(n - 1.0); }",
            "output = vec4(factorial(4.0));", lookup, NULL, assign, &output, &discarded, NULL))
        return 13;
    if (discarded || output.data[0] != 24.0f || output.data[1] != 24.0f ||
        output.data[2] != 24.0f || output.data[3] != 24.0f)
        return 14;
    if (!mesaGLSLExpression("vec4(ivec2(2.9, -1.8), bvec2(0.0, 3.0))[2]", NULL, lookup,
                            NULL, NULL, &value, NULL) ||
        value.rows != 1 || value.data[0] != 0.0f)
        return 15;
    if (!mesaGLSLExpression("mat3(2.0)[1].y", NULL, lookup, NULL, NULL, &value, NULL) ||
        value.data[0] != 2.0f)
        return 16;
    if (!mesaGLSLExecute("vec3 colors[2]; colors[0] = vec3(1.0, 2.0, 3.0);"
                         "colors[1] = colors[0].zyx; output = vec4(colors[1], 1.0);",
                         lookup, NULL, assign, &output, &discarded, NULL))
        return 17;
    if (output.data[0] != 3.0f || output.data[1] != 2.0f || output.data[2] != 1.0f ||
        output.data[3] != 1.0f)
        return 18;
    if (!mesaGLSLExpression(
            "vec4(cross(vec3(1.0, 0.0, 0.0), vec3(0.0, 1.0, 0.0)), "
            "smoothstep(0.0, 1.0, 0.5))",
            NULL, lookup, NULL, NULL, &value, NULL))
        return 19;
    if (value.data[0] != 0.0f || value.data[1] != 0.0f || value.data[2] != 1.0f ||
        value.data[3] != 0.5f)
        return 20;
    if (!mesaGLSLExecute("float count = 0.0; do { count += 1.0; } while (count < 3.0);"
                         "output = vec4(count);",
                         lookup, NULL, assign, &output, &discarded, NULL) ||
        output.data[0] != 3.0f)
        return 21;
    if (!mesaGLSLExpression(
            "vec4(lessThan(vec3(0.0, 2.0, 1.0), vec3(1.0, 1.0, 2.0)), "
            "all(bvec2(true)))",
            NULL, lookup, NULL, NULL, &value, NULL))
        return 22;
    if (value.data[0] != 1.0f || value.data[1] != 0.0f || value.data[2] != 1.0f ||
        value.data[3] != 1.0f)
        return 23;
    if (!mesaGLSLExecuteProgram(
            "void adjust(inout vec4 color, out float alpha) {"
            "color.rgb *= vec3(0.5, 0.25, 0.125); alpha = 0.75; }",
            "vec4 color = vec4(0.8, 0.8, 0.8, 1.0); float alpha;"
            "adjust(color, alpha); output = vec4(color.rgb, alpha);",
            lookup, NULL, assign, &output, &discarded, NULL))
        return 24;
    if (output.data[0] != 0.4f || output.data[1] != 0.2f || output.data[2] != 0.1f ||
        output.data[3] != 0.75f)
        return 25;
    if (!mesaGLSLExecuteProgram(
            "void flip(inout vec2 pair) { pair = pair.yx; }",
            "vec4 colors[2]; colors[1] = vec4(0.2, 0.6, 0.8, 1.0);"
            "flip(colors[1].rg); output = colors[1];",
            lookup, NULL, assign, &output, &discarded, NULL))
        return 26;
    if (output.data[0] != 0.6f || output.data[1] != 0.2f || output.data[2] != 0.8f ||
        output.data[3] != 1.0f)
        return 27;
    if (!mesaGLSLExecuteProgram(
            "struct Light { highp vec3 color; float intensity; };",
            "Light light; light.color = vec3(0.8, 0.4, 0.2);"
            "light.color.rg = light.color.gr; light.intensity = 1.0; light.intensity *= 0.5;"
            "output = vec4(light.color * light.intensity, 1.0);",
            lookup, NULL, assign, &output, &discarded, &error_at)) {
        fprintf(stderr, "struct error near: %.32s\n", error_at ? error_at : "(unknown)");
        return 28;
    }
    if (output.data[0] != 0.2f || output.data[1] != 0.4f || output.data[2] != 0.1f ||
        output.data[3] != 1.0f)
        return 29;
    if (!mesaGLSLExecuteProgram(
            "struct Light { vec3 color; float intensity; };"
            "void dim(inout Light light) { light.color *= 0.5; light.intensity *= 0.25; }",
            "Light source = Light(vec3(0.8, 0.4, 0.2), 1.0);"
            "Light copy = source; dim(copy);"
            "output = vec4(copy.color, copy.intensity);",
            lookup, NULL, assign, &output, &discarded, NULL))
        return 30;
    if (output.data[0] != 0.4f || output.data[1] != 0.2f || output.data[2] != 0.1f ||
        output.data[3] != 0.25f)
        return 31;
    if (!mesaGLSLExecuteProgram(
            "struct Light { vec3 color; float intensity; };"
            "void dim(inout Light light) { light.color *= 0.5; light.intensity *= 0.5; }",
            "Light lights[2]; lights[0] = Light(vec3(0.8, 0.4, 0.2), 1.0);"
            "lights[1] = lights[0]; lights[1].color.rg = vec2(0.6, 0.2);"
            "lights[1].intensity *= 0.5; dim(lights[1]);"
            "output = vec4(lights[1].color, lights[1].intensity);",
            lookup, NULL, assign, &output, &discarded, NULL))
        return 32;
    if (output.data[0] != 0.3f || output.data[1] != 0.1f || output.data[2] != 0.1f ||
        output.data[3] != 0.25f)
        return 33;
    if (!mesaGLSLExecuteProgram(
            "struct Light { vec3 color; float intensity; };"
            "struct Scene { Light light; float exposure; };"
            "void flip(inout vec2 value) { value = value.yx; }",
            "Scene scene; scene.light = Light(vec3(0.2, 0.4, 0.8), 0.75);"
            "scene.light.color = vec3(0.4, 0.8, 0.2); scene.light.intensity *= 0.5;"
            "scene.light.color.rg = vec2(0.6, 0.4); scene.light.color.b *= 0.5;"
            "flip(scene.light.color.rg);"
            "scene.exposure = 0.5;"
            "output = vec4(scene.light.color * scene.exposure, scene.light.intensity);",
            lookup, NULL, assign, &output, &discarded, NULL))
        return 34;
    if (output.data[0] != 0.2f || output.data[1] != 0.3f || output.data[2] != 0.05f ||
        output.data[3] != 0.375f)
        return 35;
    if (!mesaGLSLExecuteProgram(
            "struct Light { vec3 color; float intensity; };"
            "struct Scene { Light light; float exposure; };"
            "void flip(inout vec2 value) { value = value.yx; }",
            "Scene scenes[2]; scenes[1].light = Light(vec3(0.8, 0.4, 0.2), 1.0);"
            "scenes[1].light.color.gb = vec2(0.6, 0.4);"
            "flip(scenes[1].light.color.gb);"
            "output = vec4(scenes[1].light.color, scenes[1].light.intensity);",
            lookup, NULL, assign, &output, &discarded, NULL))
        return 36;
    if (output.data[0] != 0.8f || output.data[1] != 0.4f || output.data[2] != 0.6f ||
        output.data[3] != 1.0f)
        return 37;
    if (!mesaGLSLExpression("vec4((uv * 2.0 + vec2(0.1)).yx, 0.0, 1.0)", NULL, lookup,
                            NULL, NULL, &value, NULL))
        return 38;
    if (!value.has_derivatives || value.dfdx[0] != 0.0f || value.dfdx[1] != 0.25f ||
        value.dfdy[0] != 0.5f || value.dfdy[1] != 0.0f)
        return 39;
    if (!mesaGLSLExpression(
            "vec4((mat2(1.0, 2.0, 3.0, 4.0) * mat2(5.0, 6.0, 7.0, 8.0))[0], "
            "vec2(1.0, 2.0) * mat2(1.0, 2.0, 3.0, 4.0))",
            NULL, lookup, NULL, NULL, &value, NULL))
        return 40;
    if (value.data[0] != 23.0f || value.data[1] != 34.0f || value.data[2] != 5.0f ||
        value.data[3] != 11.0f)
        return 41;
    if (!mesaGLSLExpression("vec4(mat3(mat2(2.0))[2], 1.0)", NULL, lookup, NULL, NULL,
                            &value, NULL))
        return 42;
    if (value.data[0] != 0.0f || value.data[1] != 0.0f || value.data[2] != 1.0f ||
        value.data[3] != 1.0f)
        return 43;
    if (!mesaGLSLExpression("mat2(1.0) + 1.0", NULL, lookup, NULL, NULL, &value, NULL) ||
        value.rows != 2 || value.columns != 2 || value.data[0] != 2.0f ||
        value.data[1] != 1.0f || value.data[2] != 1.0f || value.data[3] != 2.0f)
        return 44;
    if (mesaGLSLExpression("mat3(vec2(1.0))", NULL, lookup, NULL, NULL, &value, NULL))
        return 45;
    if (!mesaGLSLExpression("mix(uv, uv * 2.0, 0.25)", NULL, lookup, NULL, NULL, &value,
                            NULL))
        return 46;
    if (!value.has_derivatives || value.dfdx[0] != 0.15625f ||
        value.dfdy[1] != 0.3125f)
        return 47;
    if (!mesaGLSLExecuteProgram(
            "vec4 select_value(float value) { return vec4(value, 0.0, 0.0, 1.0); }"
            "vec4 select_value(vec2 value) { return vec4(0.0, value.x, 0.0, 1.0); }"
            "vec4 select_value(vec3 value) { return vec4(0.0, 0.0, value.z, 1.0); }"
            "float scalar_value(float value) { return 0.25; }"
            "float scalar_value(int value) { return 0.75; }",
            "vec4 selected = select_value(vec2(0.25, 0.75));"
            "output = vec4(selected.g, scalar_value(1), scalar_value(1.0), 1.0);",
            lookup, NULL, assign, &output,
            &discarded, NULL))
        return 48;
    if (output.data[0] != 0.25f || output.data[1] != 0.75f || output.data[2] != 0.25f ||
        output.data[3] != 1.0f)
        return 49;
    if (mesaGLSLExecuteProgram(
            "float only_vec2(vec2 value) { return value.x; }",
            "output = vec4(only_vec2(vec3(1.0)));", lookup, NULL, assign, &output,
            &discarded, NULL))
        return 50;
    if (!mesaGLSLExecuteProgram(
            "struct First { float value; }; struct Second { float value; };"
            "float struct_value(First value) { return 0.2; }"
            "float struct_value(Second value) { return 0.8; }",
            "First first = First(1.0); Second second = Second(1.0);"
            "output = vec4(struct_value(first), struct_value(second), 0.0, 1.0);",
            lookup, NULL, assign, &output, &discarded, NULL))
        return 51;
    if (output.data[0] != 0.2f || output.data[1] != 0.8f)
        return 52;
    if (!mesaGLSLExecuteProgram(
            "float array_sum(in float values[3]) { "
            "return values[0] + values[1] + values[2]; }"
            "void array_adjust(inout vec2 values[2], out float result[2]) { "
            "values[0] = values[0].yx; values[1] *= 0.5; "
            "result[0] = values[0].x; result[1] = values[1].y; }",
            "float weights[3]; weights[0] = 0.1; weights[1] = 0.2; weights[2] = 0.3;"
            "vec2 pairs[2]; pairs[0] = vec2(0.25, 0.75); pairs[1] = vec2(0.8, 0.4);"
            "float results[2]; array_adjust(pairs, results);"
            "output = vec4(array_sum(weights), pairs[0].x, results[1], pairs[1].x);",
            lookup, NULL, assign, &output, &discarded, NULL))
        return 53;
    if (output.data[0] < 0.599f || output.data[0] > 0.601f || output.data[1] != 0.75f ||
        output.data[2] != 0.2f || output.data[3] != 0.4f)
        return 54;
    if (mesaGLSLExecuteProgram(
            "float array_sum(float values[3]) { return values[0]; }",
            "float values[2]; output = vec4(array_sum(values));", lookup, NULL, assign,
            &output, &discarded, NULL))
        return 55;
    if (!mesaGLSLExecuteProgram(
            "struct Light { vec3 color; float intensity; };"
            "struct Scene { Light light; float exposure; };"
            "void adjust_scenes(inout Scene scenes[2]) {"
            "scenes[0].light.color *= scenes[0].exposure;"
            "scenes[1].light.intensity *= 0.25; }",
            "Scene scenes[2];"
            "scenes[0].light = Light(vec3(0.8, 0.4, 0.2), 1.0);"
            "scenes[0].exposure = 0.5;"
            "scenes[1].light = Light(vec3(0.1), 0.8); scenes[1].exposure = 1.0;"
            "adjust_scenes(scenes);"
            "output = vec4(scenes[0].light.color, scenes[1].light.intensity);",
            lookup, NULL, assign, &output, &discarded, NULL))
        return 56;
    if (output.data[0] != 0.4f || output.data[1] != 0.2f || output.data[2] != 0.1f ||
        output.data[3] != 0.2f)
        return 57;
    if (!mesaGLSLExecuteProgram(
            "const highp float BASE = 0.125;"
            "const vec3 SCALE = vec3(BASE * 2.0, BASE * 4.0, BASE * 6.0);"
            "vec3 global_scale(vec3 value) { return value * SCALE; }",
            "const float LOCAL = BASE * 2.0;"
            "output = vec4(global_scale(vec3(1.0)), LOCAL);",
            lookup, NULL, assign, &output, &discarded, NULL))
        return 58;
    if (output.data[0] != 0.25f || output.data[1] != 0.5f || output.data[2] != 0.75f ||
        output.data[3] != 0.25f)
        return 59;
    if (mesaGLSLExecuteProgram(
            "const float IMMUTABLE = 0.5;",
            "IMMUTABLE = 1.0; output = vec4(IMMUTABLE);", lookup, NULL, assign, &output,
            &discarded, NULL))
        return 60;
    if (mesaGLSLExecuteProgram(
            "float overwrite(const float value) { value = 1.0; return value; }",
            "output = vec4(overwrite(0.5));", lookup, NULL, assign, &output, &discarded,
            NULL))
        return 61;
    if (!mesaGLSLExpression("distance(uv, vec2(0.0))", NULL, lookup, NULL, NULL, &value,
                            NULL) ||
        !value.has_derivatives || fabsf(value.dfdx[0] - 0.0559017f) > 0.00001f ||
        fabsf(value.dfdy[0] - 0.2236068f) > 0.00001f)
        return 62;
    if (!mesaGLSLExpression("cross(vec3(uv, 1.0), vec3(1.0, 0.0, 0.0))", NULL, lookup,
                            NULL, NULL, &value, NULL) ||
        !value.has_derivatives || value.dfdx[2] != 0.0f || value.dfdy[2] != -0.25f)
        return 63;
    if (!mesaGLSLExpression("reflect(uv, vec2(1.0, 0.0))", NULL, lookup, NULL, NULL,
                            &value, NULL) ||
        !value.has_derivatives || value.dfdx[0] != -0.125f || value.dfdy[1] != 0.25f)
        return 64;
    if (!mesaGLSLExecute(
            "vec2 first = vec2(0.1, 0.2), second = vec2(0.3, 0.4);"
            "float left[2], right[2]; left[1] = first.y; right[0] = second.x;"
            "float sum = left[1], product = right[0] * 2.0;"
            "output = vec4(first.x + second.y, sum, product, 1.0);",
            lookup, NULL, assign, &output, &discarded, NULL))
        return 65;
    if (output.data[0] < 0.499f || output.data[0] > 0.501f ||
        output.data[1] != 0.2f || output.data[2] < 0.599f ||
        output.data[2] > 0.601f || output.data[3] != 1.0f)
        return 66;
    if (!mesaGLSLExecuteProgram(
            "struct Pair { vec2 value; };",
            "Pair first = Pair(vec2(0.2, 0.4)), second = Pair(vec2(0.6, 0.8));"
            "output = vec4(first.value, second.value);",
            lookup, NULL, assign, &output, &discarded, NULL))
        return 67;
    if (output.data[0] != 0.2f || output.data[1] != 0.4f ||
        output.data[2] != 0.6f || output.data[3] != 0.8f)
        return 68;
    if (mesaGLSLExecute("const float missing; output = vec4(missing);", lookup, NULL,
                        assign, &output, &discarded, NULL))
        return 69;
    if (mesaGLSLExecute("float scalar = vec2(1.0); output = vec4(scalar);", lookup, NULL,
                        assign, &output, &discarded, NULL))
        return 70;
    if (mesaGLSLExecuteProgram(
            "struct First { float value; }; struct Second { float value; };",
            "First first = Second(1.0); output = vec4(first.value);",
            lookup, NULL, assign, &output, &discarded, NULL))
        return 71;
    if (!mesaGLSLExecute(
            "float value = 0.2; { float value = 0.8; value += 0.1; }"
            "output = vec4(value);",
            lookup, NULL, assign, &output, &discarded, NULL))
        return 72;
    if (output.data[0] != 0.2f)
        return 73;
    if (mesaGLSLExecute(
            "float value = 0.2; float value = 0.8; output = vec4(value);",
            lookup, NULL, assign, &output, &discarded, NULL))
        return 74;
    if (mesaGLSLExecute(
            "{ float hidden = 0.8; } output = vec4(hidden);",
            lookup, NULL, assign, &output, &discarded, NULL))
        return 75;
    if (!mesaGLSLExecute(
            "float sum = 0.0; for (int i = 0; i < 20; i++) {"
            "float scratch[16]; scratch[0] = float(i); sum += scratch[0]; }"
            "output = vec4(sum);",
            lookup, NULL, assign, &output, &discarded, NULL))
        return 76;
    if (output.data[0] != 190.0f)
        return 77;
    if (mesaGLSLExecute(
            "for (int i = 0; i < 1; i++) { } output = vec4(float(i));",
            lookup, NULL, assign, &output, &discarded, NULL))
        return 78;
    if (mesaGLSLExecute(
            "float scalar; scalar = vec2(1.0); output = vec4(scalar);",
            lookup, NULL, assign, &output, &discarded, NULL))
        return 79;
    if (mesaGLSLExecute(
            "float values[2]; values[0] = vec2(1.0); output = vec4(values[0]);",
            lookup, NULL, assign, &output, &discarded, NULL))
        return 80;
    if (mesaGLSLExecute(
            "vec2 pair = vec2(0.2, 0.4); pair.xy = 1.0; output = vec4(pair, 0.0, 1.0);",
            lookup, NULL, assign, &output, &discarded, NULL))
        return 81;
    if (mesaGLSLExecute(
            "vec2 pair = vec2(0.2, 0.4); pair.xx = vec2(0.6, 0.8);"
            "output = vec4(pair, 0.0, 1.0);",
            lookup, NULL, assign, &output, &discarded, NULL))
        return 82;
    if (mesaGLSLExecute(
            "ivec2 integers = ivec2(1); integers = vec2(2.0);"
            "output = vec4(integers, 0, 1);",
            lookup, NULL, assign, &output, &discarded, NULL))
        return 83;
    if (mesaGLSLExecuteProgram(
            "struct Item { float value; };",
            "Item item; item.value = vec2(1.0); output = vec4(item.value);",
            lookup, NULL, assign, &output, &discarded, NULL))
        return 84;
    if (!mesaGLSLExecute(
            "vec2 pair = vec2(0.2, 0.4); pair.yx = vec2(0.6, 0.8);"
            "output = vec4(pair, 0.0, 1.0);",
            lookup, NULL, assign, &output, &discarded, NULL))
        return 85;
    if (output.data[0] != 0.8f || output.data[1] != 0.6f)
        return 86;
    if (mesaGLSLExecuteProgram(
            "float missing() { float value = 1.0; }",
            "output = vec4(missing());", lookup, NULL, assign, &output, &discarded,
            NULL))
        return 87;
    if (mesaGLSLExecuteProgram(
            "void invalid() { return 1.0; }",
            "invalid(); output = vec4(1.0);", lookup, NULL, assign, &output, &discarded,
            NULL))
        return 88;
    if (mesaGLSLExecuteProgram(
            "float invalid() { return; }",
            "output = vec4(invalid());", lookup, NULL, assign, &output, &discarded,
            NULL))
        return 89;
    if (mesaGLSLExecuteProgram(
            "vec2 invalid() { return vec3(1.0); }",
            "output = vec4(invalid(), 0.0, 1.0);", lookup, NULL, assign, &output,
            &discarded, NULL))
        return 90;
    if (mesaGLSLExecuteProgram(
            "int invalid() { return 1.0; }",
            "output = vec4(float(invalid()));", lookup, NULL, assign, &output,
            &discarded, NULL))
        return 91;
    if (!mesaGLSLExecuteProgram(
            "float conditional(bool selected) { if (selected) return 0.5; }",
            "output = vec4(conditional(false));", lookup, NULL, assign, &output,
            &discarded, NULL) || output.data[0] != 0.0f)
        return 92;
    if (!mesaGLSLExecuteProgram(
            "struct Pair { vec2 value; };"
            "Pair make_pair() { return Pair(vec2(0.25, 0.75)); }",
            "Pair pair = make_pair(); output = vec4(pair.value, 0.0, 1.0);",
            lookup, NULL, assign, &output, &discarded, NULL))
        return 93;
    if (output.data[0] != 0.25f || output.data[1] != 0.75f ||
        output.data[2] != 0.0f || output.data[3] != 1.0f)
        return 94;
    if (!mesaGLSLExecuteProgram(
            "struct Packet { vec2 values[2]; };"
            "Packet make_packet() { Packet p; p.values[0] = vec2(0.2, 0.4);"
            "p.values[1] = vec2(0.6, 0.8); return p; }",
            "Packet packet = make_packet();"
            "output = vec4(packet.values[0].x, packet.values[1].y, 0.0, 1.0);",
            lookup, NULL, assign, &output, &discarded, NULL))
        return 121;
    if (output.data[0] != 0.2f || output.data[1] != 0.8f ||
        output.data[2] != 0.0f || output.data[3] != 1.0f)
        return 122;
    if (mesaGLSLExpression("true + false", NULL, lookup, NULL, NULL, &value, NULL))
        return 95;
    if (mesaGLSLExpression("-true", NULL, lookup, NULL, NULL, &value, NULL))
        return 96;
    if (mesaGLSLExpression("!1.0", NULL, lookup, NULL, NULL, &value, NULL))
        return 97;
    if (mesaGLSLExpression("vec2(1.0) < vec2(2.0)", NULL, lookup, NULL, NULL,
                            &value, NULL))
        return 98;
    if (!mesaGLSLExpression("vec2(1.0, 2.0) == vec2(1.0, 3.0)", NULL, lookup,
                            NULL, NULL, &value, NULL) || value.type != MESAGL_GLSL_TYPE_BOOL ||
        value.data[0] != 0.0f)
        return 99;
    if (mesaGLSLExpression("1.0 && true", NULL, lookup, NULL, NULL, &value, NULL))
        return 100;
    if (mesaGLSLExpression("1.0 ? vec2(1.0) : vec2(0.0)", NULL, lookup, NULL, NULL,
                            &value, NULL))
        return 101;
    if (mesaGLSLExpression("true ? vec2(1.0) : vec3(0.0)", NULL, lookup, NULL, NULL,
                            &value, NULL))
        return 102;
    if (mesaGLSLExpression("true ? 1 : 1.0", NULL, lookup, NULL, NULL, &value, NULL))
        return 103;
    if (mesaGLSLExecute("if (1.0) output = vec4(1.0);", lookup, NULL, assign,
                        &output, &discarded, NULL))
        return 104;
    if (mesaGLSLExecute("while (0.0) { } output = vec4(1.0);", lookup, NULL, assign,
                        &output, &discarded, NULL))
        return 105;
    if (!mesaGLSLExecute(
            "bool selected = true; output = selected ? vec4(0.25) : vec4(0.75);",
            lookup, NULL, assign, &output, &discarded, NULL))
        return 106;
    if (output.data[0] != 0.25f)
        return 107;
    if (mesaGLSLExpression("dot(bvec2(true), bvec2(false))", NULL, lookup, NULL,
                            NULL, &value, NULL))
        return 108;
    if (mesaGLSLExpression("normalize(mat2(1.0))", NULL, lookup, NULL, NULL,
                            &value, NULL))
        return 109;
    if (mesaGLSLExpression("sin(ivec2(1))", NULL, lookup, NULL, NULL, &value, NULL))
        return 110;
    if (mesaGLSLExpression("pow(vec2(2.0), 3.0)", NULL, lookup, NULL, NULL, &value,
                            NULL))
        return 111;
    if (mesaGLSLExpression("mix(vec2(0.0), vec3(1.0), 0.5)", NULL, lookup, NULL,
                            NULL, &value, NULL))
        return 112;
    if (mesaGLSLExpression("any(true)", NULL, lookup, NULL, NULL, &value, NULL))
        return 113;
    if (mesaGLSLExpression("lessThan(bvec2(true), bvec2(false))", NULL, lookup,
                            NULL, NULL, &value, NULL))
        return 114;
    if (!mesaGLSLExpression("step(0.5, vec3(0.25, 0.25, 0.75))", NULL, lookup,
                            NULL, NULL, &value, NULL) || value.rows != 3 ||
        value.data[0] != 0.0f || value.data[1] != 0.0f || value.data[2] != 1.0f)
        return 115;
    if (!mesaGLSLExpression("clamp(vec3(-1.0, 0.5, 2.0), 0.0, 1.0)", NULL,
                            lookup, NULL, NULL, &value, NULL) || value.rows != 3 ||
        value.data[0] != 0.0f || value.data[1] != 0.5f || value.data[2] != 1.0f)
        return 116;
    if (!mesaGLSLExpression("smoothstep(0.25, 0.75, vec3(0.0, 0.5, 1.0))", NULL,
                            lookup, NULL, NULL, &value, NULL) || value.rows != 3 ||
        value.data[0] != 0.0f || value.data[1] != 0.5f || value.data[2] != 1.0f)
        return 123;
    if (!mesaGLSLExecuteProgram(
            "struct Leaf { vec4 color; }; struct Level2 { Leaf leaf; };"
            "struct Level3 { Level2 next; }; struct Root { Level3 next; };",
            "Root root; root.next.next.leaf.color = vec4(0.1, 0.2, 0.3, 1.0);"
            "root.next.next.leaf.color.gb += vec2(0.2, 0.3);"
            "output = root.next.next.leaf.color;",
            lookup, NULL, assign, &output, &discarded, NULL))
        return 117;
    if (output.data[0] != 0.1f || output.data[1] != 0.4f ||
        output.data[2] < 0.599f || output.data[2] > 0.601f || output.data[3] != 1.0f)
        return 118;
    if (mesaGLSLExecuteProgram(
            "struct Leaf { vec4 color; }; struct Level2 { Leaf leaf; };"
            "struct Level3 { Level2 next; }; struct Root { Level3 next; };",
            "Root root; root.next.next.leaf.color.rr = vec2(1.0);"
            "output = root.next.next.leaf.color;",
            lookup, NULL, assign, &output, &discarded, NULL))
        return 119;
    if (!mesaGLSLExecuteProgram(
            "float incomplete(bool selected) { if (selected) return 0.5; }",
            "output = vec4(incomplete(true));", lookup, NULL, assign, &output,
            &discarded, NULL) || output.data[0] != 0.5f)
        return 120;
    if (!mesaGLSLExecuteProgram(
            "float complete(bool selected) { if (selected) { return 0.25; }"
            "else { return 0.75; } }",
            "output = vec4(complete(false));", lookup, NULL, assign, &output,
            &discarded, NULL) || output.data[0] != 0.75f)
        return 121;
    if (!mesaGLSLExecuteProgram(
            "float complete(bool selected) { if (selected) return 0.25; return 0.5; }",
            "output = vec4(complete(false));", lookup, NULL, assign, &output,
            &discarded, NULL) || output.data[0] != 0.5f)
        return 122;
    if (!mesaGLSLExpression(
            "lessThan(ivec3(0, 2, 4), ivec3(1, 1, 5))",
            NULL, lookup, NULL, NULL, &value, NULL) ||
        value.type != MESAGL_GLSL_TYPE_BOOL || value.rows != 3 ||
        value.data[0] != 1.0f || value.data[1] != 0.0f || value.data[2] != 1.0f)
        return 124;
    if (!mesaGLSLExecuteProgram(
            "struct LeafArray { vec4 colors[2]; };"
            "struct LayerArray { LeafArray leaves[2]; };"
            "struct RootArray { LayerArray layers[2]; };",
            "RootArray root; int layer = 1, leaf = 0, color = 1;"
            "root.layers[layer].leaves[leaf].colors[color] = vec4(0.1, 0.2, 0.3, 1.0);"
            "root.layers[layer].leaves[leaf].colors[color].gb += vec2(0.2, 0.3);"
            "output = root.layers[layer].leaves[leaf].colors[color];",
            lookup, NULL, assign, &output, &discarded, NULL))
        return 125;
    if (output.data[0] != 0.1f || output.data[1] != 0.4f ||
        output.data[2] < 0.599f || output.data[2] > 0.601f || output.data[3] != 1.0f)
        return 126;
    if (mesaGLSLExecuteProgram(
            "struct LeafArray { vec4 colors[2]; };",
            "LeafArray leaf; leaf.colors[2] = vec4(1.0); output = leaf.colors[0];",
            lookup, NULL, assign, &output, &discarded, NULL))
        return 127;
    if (!mesaGLSLExecuteProgram(
            "struct CommaLeaf { vec4 first, colors[2], last; };"
            "struct CommaRoot { CommaLeaf single, leaves[2]; float unused, gain; };",
            "CommaRoot root; int index = 1; root.single.first = vec4(0.1);"
            "root.leaves[index].colors[0] = vec4(0.2, 0.4, 0.6, 1.0);"
            "root.leaves[index].last = vec4(1.0); root.gain = 1.0;"
            "output = root.leaves[index].colors[0] * root.gain;",
            lookup, NULL, assign, &output, &discarded, NULL))
        return 128;
    if (output.data[0] != 0.2f || output.data[1] != 0.4f ||
        output.data[2] < 0.599f || output.data[2] > 0.601f || output.data[3] != 1.0f)
        return 129;
    if (mesaGLSLExecuteProgram(
            "struct InvalidMembers { vec4 repeated, repeated; };",
            "InvalidMembers item; output = item.repeated;",
            lookup, NULL, assign, &output, &discarded, NULL))
        return 130;
    if (mesaGLSLExpression("vec2(1.0, 2.0, 3.0)", NULL, lookup, NULL, NULL,
                            &value, NULL))
        return 131;
    if (!mesaGLSLExpression("float(vec2(1.0, 2.0))", NULL, lookup, NULL, NULL,
                             &value, NULL) ||
        value.rows != 1 || value.columns != 1 || value.data[0] != 1.0f)
        return 132;
    if (mesaGLSLExpression("vec4(vec2(1.0), 2.0)", NULL, lookup, NULL, NULL,
                            &value, NULL))
        return 133;
    if (!mesaGLSLExpression("vec4(vec2(1.0), 2.0, 3.0)", NULL, lookup, NULL, NULL,
                             &value, NULL) || value.rows != 4 || value.data[0] != 1.0f ||
        value.data[1] != 1.0f || value.data[2] != 2.0f || value.data[3] != 3.0f)
        return 134;
    if (mesaGLSLExpression("vec3(1.0)[1.0]", NULL, lookup, NULL, NULL, &value, NULL))
        return 135;
    if (mesaGLSLExecute("vec3 value = vec3(1.0); value[1.0] = 0.5;"
                        "output = vec4(value, 1.0);", lookup, NULL, assign, &output,
                        &discarded, NULL))
        return 136;
    if (!mesaGLSLExecute(
            "vec4 vector = vec4(0.1, 0.2, 0.3, 1.0); int index = 1;"
            "vector[index] += 0.2;"
            "mat3 matrix = mat3(1.0); matrix[1] = vec3(0.2, 0.4, 0.6);"
            "matrix[1].gb += vec2(0.1, 0.2);"
            "output = vector + vec4(matrix[1], 0.0);",
            lookup, NULL, assign, &output, &discarded, NULL))
        return 137;
    if (output.data[0] != 0.3f || output.data[1] < 0.899f ||
        output.data[1] > 0.901f || output.data[2] < 1.099f ||
        output.data[2] > 1.101f || output.data[3] != 1.0f)
        return 138;
    if (!mesaGLSLExecuteProgram(
            "struct IndexedState { vec4 color; mat3 transform; };",
            "IndexedState state; int channel = 1, column = 2;"
            "state.color = vec4(0.1, 0.2, 0.3, 1.0); state.color[channel] += 0.2;"
            "state.transform = mat3(1.0); state.transform[column] = vec3(0.2, 0.4, 0.6);"
            "state.transform[column].xy += vec2(0.1, 0.2);"
            "output = state.color + vec4(state.transform[column], 0.0);",
            lookup, NULL, assign, &output, &discarded, NULL))
        return 139;
    if (output.data[0] != 0.4f || output.data[1] != 1.0f ||
        output.data[2] < 0.899f || output.data[2] > 0.901f || output.data[3] != 1.0f)
        return 140;
    if (mesaGLSLExecuteProgram(
            "struct IndexedState { vec4 color; };",
            "IndexedState state; state.color[1].x = 1.0; output = state.color;",
            lookup, NULL, assign, &output, &discarded, NULL))
        return 141;
    if (!mesaGLSLExecuteProgram(
            "bool touch(out float value) { value = 1.0; return true; }",
            "float value = 0.0; bool first = false && touch(value);"
            "bool second = true || touch(value);"
            "output = vec4(value, first ? 1.0 : 0.0, second ? 1.0 : 0.0, 1.0);",
            lookup, NULL, assign, &output, &discarded, NULL))
        return 142;
    if (output.data[0] != 0.0f || output.data[1] != 0.0f ||
        output.data[2] != 1.0f || output.data[3] != 1.0f)
        return 143;
    if (!mesaGLSLExecuteProgram(
            "bool touch(out float value) { value = 1.0; return true; }",
            "float value = 0.0; bool selected = true && touch(value);"
            "output = vec4(value, selected ? 1.0 : 0.0, 0.0, 1.0);",
            lookup, NULL, assign, &output, &discarded, NULL))
        return 144;
    if (output.data[0] != 1.0f || output.data[1] != 1.0f)
        return 145;
    if (mesaGLSLExecute("bool invalid = false && 1.0; output = vec4(1.0);",
                        lookup, NULL, assign, &output, &discarded, NULL))
        return 146;
    if (!mesaGLSLExecuteProgram(
            "float touch(out float value) { value = 1.0; return 0.25; }",
            "float value = 0.0; float selected = true ? 0.75 : touch(value);"
            "output = vec4(value, selected, 0.0, 1.0);",
            lookup, NULL, assign, &output, &discarded, NULL))
        return 147;
    if (output.data[0] != 0.0f || output.data[1] != 0.75f)
        return 148;
    if (!mesaGLSLExecuteProgram(
            "float touch(out float value) { value = 1.0; return 0.25; }",
            "float value = 0.0; float selected = false ? 0.75 : touch(value);"
            "output = vec4(value, selected, 0.0, 1.0);",
            lookup, NULL, assign, &output, &discarded, NULL))
        return 149;
    if (output.data[0] != 1.0f || output.data[1] != 0.25f)
        return 150;
    if (!mesaGLSLExecute(
            "int integer = 1; int post = integer++; int pre = ++integer;"
            "vec2 vector = vec2(0.25, 0.5); vec2 old = vector++; --vector;"
            "vector--; vector++;"
            "output = vec4(float(post), float(pre), old.x, vector.y);",
            lookup, NULL, assign, &output, &discarded, &error_at)) {
        fprintf(stderr, "increment error near: %.32s\n", error_at ? error_at : "(unknown)");
        return 151;
    }
    if (output.data[0] != 1.0f || output.data[1] != 3.0f ||
        output.data[2] != 0.25f || output.data[3] != 0.5f)
        return 152;
    if (!mesaGLSLExecuteProgram(
            "struct IncrementState { vec3 value; };",
            "IncrementState state; state.value = vec3(0.0);"
            "float post = state.value[1]++; float pre = ++state.value[2];"
            "output = vec4(post, pre, state.value.yz);",
            lookup, NULL, assign, &output, &discarded, NULL))
        return 153;
    if (output.data[0] != 0.0f || output.data[1] != 1.0f ||
        output.data[2] != 1.0f || output.data[3] != 1.0f)
        return 154;
    if (mesaGLSLExecute("const int fixed = 1; int invalid = fixed++;"
                        "output = vec4(float(invalid));", lookup, NULL, assign,
                        &output, &discarded, NULL))
        return 155;
    if (!mesaGLSLExecute(
            "float value = 0.0; float selected = true ? 1.0 : value++;"
            "output = vec4(value, selected, 0.0, 1.0);",
            lookup, NULL, assign, &output, &discarded, NULL))
        return 156;
    if (output.data[0] != 0.0f || output.data[1] != 1.0f)
        return 157;
    if (!mesaGLSLExecuteProgram(
            "const float BASE = 0.25; float global_value = BASE; vec2 global_array[2];"
            "struct GlobalState { vec3 color; }; GlobalState global_state;"
            "void mutate_globals() { global_value += 0.25;"
            "global_array[1] = vec2(0.5, 0.75);"
            "global_state.color = vec3(0.2, 0.4, 0.6); }",
            "mutate_globals(); output = vec4(global_value + global_array[1].x,"
            "global_state.color);",
            lookup, NULL, assign, &output, &discarded, NULL))
        return 158;
    if (output.data[0] != 1.0f || output.data[1] != 0.2f ||
        output.data[2] != 0.4f || output.data[3] < 0.599f || output.data[3] > 0.601f)
        return 159;
    if (!mesaGLSLExecuteProgram(
            "float global_value = 1.0;"
            "bool poison_global() { global_value = 0.0; return true; }",
            "bool ignored = false && poison_global();"
            "output = vec4(global_value, ignored ? 0.0 : 1.0, 0.0, 1.0);",
            lookup, NULL, assign, &output, &discarded, NULL))
        return 160;
    if (output.data[0] != 1.0f || output.data[1] != 1.0f)
        return 161;
    if (mesaGLSLExecuteProgram(
            "float repeated; vec2 repeated;",
            "output = vec4(repeated);", lookup, NULL, assign, &output,
            &discarded, NULL))
        return 162;
    if (!mesaGLSLExecuteProgram(
            "struct DeepGlobal { vec3 color; }; DeepGlobal global_state;"
            "bool poison_deep() { global_state.color[1] = 0.0; return true; }",
            "global_state.color = vec3(1.0);"
            "bool ignored = false && poison_deep();"
            "output = vec4(global_state.color, ignored ? 0.0 : 1.0);",
            lookup, NULL, assign, &output, &discarded, NULL))
        return 163;
    if (output.data[0] != 1.0f || output.data[1] != 1.0f ||
        output.data[2] != 1.0f || output.data[3] != 1.0f)
        return 164;
    if (!mesaGLSLExecute(
            "struct { vec3 color; float alpha; } state;"
            "state.color = vec3(0.2, 0.4, 0.6); state.alpha = 0.8;"
            "output = vec4(state.color, state.alpha);",
            lookup, NULL, assign, &output, &discarded, NULL))
        return 165;
    if (output.data[0] != 0.2f || output.data[1] != 0.4f ||
        output.data[2] < 0.599f || output.data[2] > 0.601f ||
        output.data[3] < 0.799f || output.data[3] > 0.801f)
        return 166;
    if (!mesaGLSLExecuteProgram(
            "struct { vec2 values[2]; } global_state;",
            "global_state.values[1] = vec2(0.25, 0.75);"
            "output = vec4(global_state.values[1], 0.0, 1.0);",
            lookup, NULL, assign, &output, &discarded, NULL))
        return 167;
    if (output.data[0] != 0.25f || output.data[1] != 0.75f ||
        output.data[2] != 0.0f || output.data[3] != 1.0f)
        return 168;
    if (!mesaGLSLExecute(
            "struct { float value; } states[2];"
            "states[1].value = 0.5; output = vec4(states[1].value);",
            lookup, NULL, assign, &output, &discarded, NULL))
        return 169;
    if (output.data[0] != 0.5f)
        return 170;
    if (!mesaGLSLExecute(
            "struct Inline { vec2 value; } first, second;"
            "first.value = vec2(0.25, 0.75); second = first;"
            "output = vec4(second.value, 0.0, 1.0);",
            lookup, NULL, assign, &output, &discarded, NULL))
        return 171;
    if (output.data[0] != 0.25f || output.data[1] != 0.75f ||
        output.data[2] != 0.0f || output.data[3] != 1.0f)
        return 172;
    if (!mesaGLSLExecute(
            "struct { struct { vec2 value; } nested[2]; } state;"
            "state.nested[1].value = vec2(0.4, 0.6);"
            "output = vec4(state.nested[1].value, 0.0, 1.0);",
            lookup, NULL, assign, &output, &discarded, NULL))
        return 173;
    if (output.data[0] != 0.4f || output.data[1] < 0.599f ||
        output.data[1] > 0.601f || output.data[2] != 0.0f ||
        output.data[3] != 1.0f)
        return 174;
    if (mesaGLSLExecute(
            "struct { float value; } first;"
            "struct { float value; } second; second = first;"
            "output = vec4(second.value);",
            lookup, NULL, assign, &output, &discarded, NULL))
        return 175;
    if (mesaGLSLExecute(
            "struct { float repeated; vec2 repeated; } invalid;"
            "output = vec4(invalid.repeated);",
            lookup, NULL, assign, &output, &discarded, NULL))
        return 176;
    if (mesaGLSLExecuteProgram(
            "float mutable_value = 1.0; float invalid = mutable_value;",
            "output = vec4(invalid);", lookup, NULL, assign, &output,
            &discarded, NULL))
        return 177;
    if (mesaGLSLExecuteProgram(
            "float helper() { return 1.0; } float invalid = helper();",
            "output = vec4(invalid);", lookup, NULL, assign, &output,
            &discarded, NULL))
        return 178;
    if (!mesaGLSLExecuteProgram(
            "const int FIRST = 1; const int SECOND = FIRST + 1;"
            "float valid = float(SECOND) * 0.25;",
            "output = vec4(valid);", lookup, NULL, assign, &output,
            &discarded, NULL))
        return 179;
    if (output.data[0] != 0.5f)
        return 180;
    if (!mesaGLSLExecuteProgram(
            "const int ATTRIBUTE_COUNT = gl_MaxVertexAttribs;"
            "float values[gl_MaxVertexAttribs];",
            "values[ATTRIBUTE_COUNT - 1] = 0.75;"
            "output = vec4(values[ATTRIBUTE_COUNT - 1], "
            "float(gl_MaxTextureImageUnits > 0), 0.0, 1.0);",
            lookup, NULL, assign, &output, &discarded, NULL))
        return 181;
    if (output.data[0] != 0.75f || output.data[1] != 1.0f ||
        output.data[2] != 0.0f || output.data[3] != 1.0f)
        return 182;
    if (!mesaGLSLExecuteProgram(
            "", "output = vec4(float(gl_MaxVertexUniformVectors),"
                "float(gl_MaxFragmentUniformVectors), 0.0, 1.0);",
            lookup, NULL, assign, &output, &discarded, NULL))
        return 190;
    if (output.data[0] != (float)MESAGL_MAX_VERTEX_UNIFORM_VECTORS ||
        output.data[1] != (float)MESAGL_MAX_FRAGMENT_UNIFORM_VECTORS)
        return 191;
    if (!mesaGLSLExpression("(1, vec2(0.25, 0.75))", NULL, lookup, NULL,
                            NULL, &value, NULL) ||
        value.type != MESAGL_GLSL_TYPE_FLOAT || value.rows != 2 ||
        value.data[0] != 0.25f || value.data[1] != 0.75f)
        return 183;
    if (!mesaGLSLExecute(
            "int first = 0; int second = 0;"
            "for (; first < 3; first++, second++) { }"
            "output = vec4(float(first), float(second), 0.0, 1.0);",
            lookup, NULL, assign, &output, &discarded, NULL))
        return 184;
    if (output.data[0] != 3.0f || output.data[1] != 3.0f ||
        output.data[2] != 0.0f || output.data[3] != 1.0f)
        return 185;
    if (!mesaGLSLExecute(
            "float first = 0.0; float second = 0.0;"
            "output = vec4((first = 0.25), (second = first = 0.5), "
            "first, second);",
            lookup, NULL, assign, &output, &discarded, NULL))
        return 186;
    if (output.data[0] != 0.25f || output.data[1] != 0.5f ||
        output.data[2] != 0.5f || output.data[3] != 0.5f)
        return 187;
    if (!mesaGLSLExecute(
            "vec2 state = vec2(0.25, 0.5);"
            "bool ignored = false && ((state.x = 1.0) > 0.0);"
            "output = vec4(state, float(ignored), 1.0);",
            lookup, NULL, assign, &output, &discarded, &error_at)) {
        fprintf(stderr, "assignment expression error near: %.32s\n",
                error_at ? error_at : "(unknown)");
        return 188;
    }
    if (output.data[0] != 0.25f || output.data[1] != 0.5f ||
        output.data[2] != 0.0f || output.data[3] != 1.0f)
        return 189;
    if (!mesaGLSLExecute(
            "vec2 state = vec2(0.25, 0.5);"
            "output = vec4((state.y += 0.25), state, 0.0);",
            lookup, NULL, assign, &output, &discarded, &error_at)) {
        fprintf(stderr, "compound assignment expression error near: %.32s\n",
                error_at ? error_at : "(unknown)");
        return 190;
    }
    if (output.data[0] != 0.75f || output.data[1] != 0.25f ||
        output.data[2] != 0.75f || output.data[3] != 0.0f)
        return 191;
    if (!mesaGLSLExecute(
            "const float first = 0.25; const vec2 second = vec2(first * 2.0);"
            "float values[int(second.x * 4.0)]; values[1] = second.x;"
            "output = vec4(values[1], first, 0.0, 1.0);",
            lookup, NULL, assign, &output, &discarded, NULL))
        return 192;
    if (output.data[0] != 0.5f || output.data[1] != 0.25f ||
        output.data[2] != 0.0f || output.data[3] != 1.0f)
        return 193;
    if (mesaGLSLExecute(
            "float mutable_value = 0.5; const float invalid = mutable_value;"
            "output = vec4(invalid);",
            lookup, NULL, assign, &output, &discarded, NULL))
        return 194;
    if (mesaGLSLExecuteProgram(
            "float helper() { return 0.5; }",
            "const float invalid = helper(); output = vec4(invalid);",
            lookup, NULL, assign, &output, &discarded, NULL))
        return 195;
    if (mesaGLSLExecute(
            "const float recursive = recursive; output = vec4(recursive);",
            lookup, NULL, assign, &output, &discarded, NULL))
        return 196;
    if (mesaGLSLExecute(
            "int mutable_size = 2; float values[mutable_size];"
            "output = vec4(values[0]);",
            lookup, NULL, assign, &output, &discarded, NULL))
        return 197;
    if (mesaGLSLExecuteProgram(
            "float mutable_global = 0.5;",
            "const float invalid = mutable_global; output = vec4(invalid);",
            lookup, NULL, assign, &output, &discarded, NULL))
        return 198;
    if (!mesaGLSLExecuteProgram(
            "const int COUNT = 2;"
            "float sum_values(float values[COUNT]) { "
            "return values[0] + values[1]; }",
            "float values[COUNT]; values[0] = 0.25; values[1] = 0.75;"
            "output = vec4(sum_values(values));",
            lookup, NULL, assign, &output, &discarded, NULL))
        return 199;
    if (output.data[0] != 1.0f || output.data[1] != 1.0f ||
        output.data[2] != 1.0f || output.data[3] != 1.0f)
        return 200;
    if (mesaGLSLExecuteProgram(
            "int mutable_count = 2;"
            "float invalid(float values[mutable_count]) { return values[0]; }",
            "float values[2]; output = vec4(invalid(values));",
            lookup, NULL, assign, &output, &discarded, NULL))
        return 201;
    if (mesaGLSLExecute("float values[2.5]; output = vec4(values[0]);",
                        lookup, NULL, assign, &output, &discarded, NULL))
        return 202;
    if (mesaGLSLExecute("float values[vec2(2.0)]; output = vec4(values[0]);",
                        lookup, NULL, assign, &output, &discarded, NULL))
        return 203;
    if (mesaGLSLExecute("float values[true]; output = vec4(values[0]);",
                        lookup, NULL, assign, &output, &discarded, NULL))
        return 204;
    if (!mesaGLSLExecuteProgram(
            "struct InnerEqual { vec2 value; };"
            "struct OuterEqual { InnerEqual inner; float weight; };",
            "OuterEqual first; first.inner.value = vec2(0.25, 0.75);"
            "first.weight = 0.5; OuterEqual second = first;"
            "bool copied = first == second; second.inner.value.x = 0.5;"
            "bool changed = first != second;"
            "output = vec4(float(copied), float(changed), 0.0, 1.0);",
            lookup, NULL, assign, &output, &discarded, NULL))
        return 205;
    if (output.data[0] != 1.0f || output.data[1] != 1.0f ||
        output.data[2] != 0.0f || output.data[3] != 1.0f) {
        fprintf(stderr, "structure equality output: %g %g %g %g\n",
                output.data[0], output.data[1], output.data[2], output.data[3]);
        return 206;
    }
    if (mesaGLSLExecuteProgram(
            "struct FirstEqual { float value; };"
            "struct SecondEqual { float value; };",
            "FirstEqual first; SecondEqual second;"
            "output = vec4(float(first == second));",
            lookup, NULL, assign, &output, &discarded, NULL))
        return 207;
    if (mesaGLSLExecute(
            "float first[2]; float second[2];"
            "output = vec4(float(first == second));",
            lookup, NULL, assign, &output, &discarded, NULL))
        return 208;
    if (mesaGLSLExecuteProgram(
            "struct ArrayEqual { float values[2]; };",
            "ArrayEqual first; ArrayEqual second;"
            "output = vec4(float(first == second));",
            lookup, NULL, assign, &output, &discarded, NULL))
        return 209;
    if (!mesaGLSLExecuteProgram(
            "struct InnerValue { float value; };"
            "struct OuterValue { InnerValue inner; };"
            "void mutate_value(OuterValue value) { value.inner.value = 1.0; }",
            "OuterValue original; original.inner.value = 0.25;"
            "mutate_value(original); output = vec4(original.inner.value);",
            lookup, NULL, assign, &output, &discarded, NULL))
        return 210;
    if (output.data[0] != 0.25f || output.data[1] != 0.25f ||
        output.data[2] != 0.25f || output.data[3] != 0.25f)
        return 211;
    if (!mesaGLSLExecute(
            "int count = 0;"
            "bool different = false ^^ ((count = 1) == 1);"
            "bool both = ((count += 1) == 2) ^^ true;"
            "bool precedence = false || true ^^ true;"
            "output = vec4(float(different), float(count), float(both), "
            "float(precedence));",
            lookup, NULL, assign, &output, &discarded, NULL))
        return 212;
    if (output.data[0] != 1.0f || output.data[1] != 2.0f ||
        output.data[2] != 0.0f || output.data[3] != 0.0f)
        return 213;
    if (mesaGLSLExecute(
            "bvec2 left = bvec2(true); bvec2 right = bvec2(false);"
            "output = vec4(float(left ^^ right));",
            lookup, NULL, assign, &output, &discarded, NULL))
        return 214;
    if (!mesaGLSLExecute(
            "int sum = 0; for (int outer = 0; outer < 3; outer++) {"
            "for (int inner = 0; inner < 3; inner++) {"
            "if (inner == 1) continue; if (outer == 2) break; sum += 1; }}"
            "output = vec4(float(sum));",
            lookup, NULL, assign, &output, &discarded, NULL))
        return 215;
    if (output.data[0] != 4.0f || output.data[1] != 4.0f ||
        output.data[2] != 4.0f || output.data[3] != 4.0f)
        return 216;
    if (mesaGLSLExecute("break; output = vec4(1.0);", lookup, NULL, assign,
                        &output, &discarded, NULL))
        return 217;
    if (mesaGLSLExecute("continue; output = vec4(1.0);", lookup, NULL, assign,
                        &output, &discarded, NULL))
        return 218;
    if (mesaGLSLExecuteProgram(
            "void invalid_break() { break; }",
            "for (int i = 0; i < 1; i++) invalid_break(); output = vec4(1.0);",
            lookup, NULL, assign, &output, &discarded, NULL))
        return 219;
    if (!mesaGLSLExecuteProgram(
            "struct InnerChoice { float value; };"
            "struct OuterChoice { InnerChoice inner; };"
            "OuterChoice poison_choice(inout OuterChoice value) {"
            "value.inner.value = 0.0; return value; }",
            "OuterChoice first; first.inner.value = 0.25;"
            "OuterChoice second; second.inner.value = 0.75;"
            "OuterChoice guard = first;"
            "OuterChoice chosen = true ? first : poison_choice(guard);"
            "chosen.inner.value = 0.5;"
            "output = vec4(chosen.inner.value, first.inner.value, "
            "guard.inner.value, float(chosen != first));",
            lookup, NULL, assign, &output, &discarded, NULL))
        return 220;
    if (output.data[0] != 0.5f || output.data[1] != 0.25f ||
        output.data[2] != 0.25f || output.data[3] != 1.0f)
        return 221;
    if (mesaGLSLExecuteProgram(
            "struct FirstChoice { float value; };"
            "struct SecondChoice { float value; };",
            "FirstChoice first; SecondChoice second;"
            "(true ? first : second); output = vec4(1.0);",
            lookup, NULL, assign, &output, &discarded, NULL))
        return 222;
    if (mesaGLSLExecute(
            "float first[2]; float second[2];"
            "(true ? first : second); output = vec4(1.0);",
            lookup, NULL, assign, &output, &discarded, NULL))
        return 223;
    if (!mesaGLSLExecute(
            "mat2 value = mat2(1.0); mat2 before = value++; mat2 after = ++value;"
            "output = vec4(before[0], after[0]);",
            lookup, NULL, assign, &output, &discarded, NULL))
        return 224;
    if (output.data[0] != 1.0f || output.data[1] != 0.0f ||
        output.data[2] != 3.0f || output.data[3] != 2.0f)
        return 225;
    if (!mesaGLSLExecute(
            "mat2 left = mat2(4.0, 2.0, 8.0, 4.0);"
            "mat2 right = mat2(2.0, 1.0, 4.0, 2.0);"
            "mat2 divided = left / right; mat2 shifted = 1.0 + divided - 1.0;"
            "output = vec4(shifted[0], shifted[1]);",
            lookup, NULL, assign, &output, &discarded, NULL))
        return 226;
    if (output.data[0] != 2.0f || output.data[1] != 2.0f ||
        output.data[2] != 2.0f || output.data[3] != 2.0f)
        return 227;
    if (!mesaGLSLExecuteProgram(
            "float ignored(float) { return 0.25; }"
            "float ignored_pair(float, vec2) { return 0.5; }",
            "output = vec4(ignored(9.0), ignored_pair(3.0, vec2(4.0)), 0.75, 1.0);",
            lookup, NULL, assign, &output, &discarded, NULL))
        return 228;
    if (output.data[0] != 0.25f || output.data[1] != 0.5f ||
        output.data[2] != 0.75f || output.data[3] != 1.0f)
        return 229;
    if (mesaGLSLExecute("vec4 value = vec4(1.0); output = vec4(value.xg, 0.0, 1.0);",
                        lookup, NULL, assign, &output, &discarded, NULL))
        return 230;
    if (mesaGLSLExecute("vec4 value = vec4(1.0); value.rs = vec2(0.0); output = value;",
                        lookup, NULL, assign, &output, &discarded, NULL))
        return 231;
    if (mesaGLSLExecute("float value = 1.0; output = vec4(value[0]);",
                        lookup, NULL, assign, &output, &discarded, NULL))
        return 232;
    if (mesaGLSLExecute("bool value = true; output = vec4(float(value[0]));",
                        lookup, NULL, assign, &output, &discarded, NULL))
        return 233;
    if (!mesaGLSLExecute(
            "vec3 value = vec3(0.25, 0.5, 0.75); int index = 2;"
            "output = vec4(value[index]);",
            lookup, NULL, assign, &output, &discarded, NULL))
        return 234;
    if (output.data[0] != 0.75f || output.data[1] != 0.75f ||
        output.data[2] != 0.75f || output.data[3] != 0.75f)
        return 235;
    if (!mesaGLSLExecute(
            "float value = 0.25; { vec2 value = vec2(0.5, 0.75);"
            "value.x += 0.25; } output = vec4(value);",
            lookup, NULL, assign, &output, &discarded, NULL))
        return 236;
    if (output.data[0] != 0.25f || output.data[1] != 0.25f ||
        output.data[2] != 0.25f || output.data[3] != 0.25f)
        return 237;
    if (!mesaGLSLExecute(
            "float result = 0.0; { float value = 0.25; result += value; }"
            "{ vec2 value = vec2(0.5); result += value.x; } output = vec4(result);",
            lookup, NULL, assign, &output, &discarded, NULL))
        return 238;
    if (output.data[0] != 0.75f || output.data[1] != 0.75f ||
        output.data[2] != 0.75f || output.data[3] != 0.75f)
        return 239;
    if (mesaGLSLExecute(
            "for (int index = 0; index < 1; ++index) { }"
            "output = vec4(float(index));",
            lookup, NULL, assign, &output, &discarded, NULL))
        return 240;
    if (mesaGLSLExecuteProgram(
            "float helper(float value) { float value = 1.0; return value; }",
            "output = vec4(helper(0.0));", lookup, NULL, assign, &output,
            &discarded, NULL))
        return 241;
    if (!mesaGLSLExecute(
            "float index = 0.5; for (int index = 0; index < 2; ++index) { }"
            "output = vec4(index);",
            lookup, NULL, assign, &output, &discarded, NULL))
        return 242;
    if (output.data[0] != 0.5f || output.data[1] != 0.5f ||
        output.data[2] != 0.5f || output.data[3] != 0.5f)
        return 243;
    if (!mesaGLSLExecute(
            "float first = 0.25, second = 0.5, third = first + second;"
            "output = vec4(third);",
            lookup, NULL, assign, &output, &discarded, NULL))
        return 244;
    if (output.data[0] != 0.75f || output.data[1] != 0.75f ||
        output.data[2] != 0.75f || output.data[3] != 0.75f)
        return 245;
    if (!mesaGLSLExecute(
            "vec2 first[2], second[3]; second[2] = vec2(0.25, 0.75);"
            "first[1] = second[2]; output = vec4(first[1], second[2]);",
            lookup, NULL, assign, &output, &discarded, NULL))
        return 246;
    if (output.data[0] != 0.25f || output.data[1] != 0.75f ||
        output.data[2] != 0.25f || output.data[3] != 0.75f)
        return 247;
    if (!mesaGLSLExecute(
            "float small = 1.0e-2; float large = 2.5E+1;"
            "output = vec4(small, large, small + large, 1.0);",
            lookup, NULL, assign, &output, &discarded, NULL))
        return 248;
    if (fabsf(output.data[0] - 0.01f) > 0.0001f ||
        fabsf(output.data[1] - 25.0f) > 0.0001f ||
        fabsf(output.data[2] - 25.01f) > 0.0001f || output.data[3] != 1.0f)
        return 249;
    if (!mesaGLSLExecute("output = vec4(mat2(1.0));", lookup, NULL, assign,
                         &output, &discarded, NULL))
        return 250;
    if (output.data[0] != 1.0f || output.data[1] != 0.0f ||
        output.data[2] != 0.0f || output.data[3] != 1.0f)
        return 254;
    if (mesaGLSLExecute(
            "mat3 value = mat3(mat2(1.0), vec4(1.0), 1.0); output = vec4(value[0], 1.0);",
            lookup, NULL, assign, &output, &discarded, NULL))
        return 251;
    if (!mesaGLSLExecute(
            "mat3 value = mat3(mat2(2.0)); output = vec4(value[0], value[2].z);",
            lookup, NULL, assign, &output, &discarded, NULL))
        return 252;
    if (output.data[0] != 2.0f || output.data[1] != 0.0f ||
        output.data[2] != 0.0f || output.data[3] != 1.0f)
        return 253;
    if (!mesaGLSLExecute(
            "int hexadecimal = 0x10; int octal = 010; int decimal = 10;"
            "output = vec4(float(hexadecimal) / 16.0, float(octal) / 8.0, "
            "float(decimal) / 10.0, 1.0);",
            lookup, NULL, assign, &output, &discarded, NULL))
        return 254;
    if (output.data[0] != 1.0f || output.data[1] != 1.0f ||
        output.data[2] != 1.0f || output.data[3] != 1.0f)
        return 255;
    if (mesaGLSLExecute("int invalid = 09; output = vec4(float(invalid));",
                        lookup, NULL, assign, &output, &discarded, NULL))
        return 256;
    if (!mesaGLSLExecute(
            "int positive = 5 / 2; int negative = -5 / 2; int assigned = 7; assigned /= 3;"
            "output = vec4(float(positive) / 2.0, float(negative) / -2.0, "
            "float(assigned) / 2.0, 1.0);",
            lookup, NULL, assign, &output, &discarded, NULL))
        return 257;
    if (output.data[0] != 1.0f || output.data[1] != 1.0f ||
        output.data[2] != 1.0f || output.data[3] != 1.0f)
        return 258;
    if (mesaGLSLExecute("int invalid = 1 / 0; output = vec4(float(invalid));",
                        lookup, NULL, assign, &output, &discarded, NULL))
        return 259;
    return 0;
}
