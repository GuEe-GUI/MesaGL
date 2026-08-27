#include "glsl_vm.h"
#include "mesaGL/config.h"
#include "mesaGL/ntgl.h"

#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct Parser {
    const char *cursor;
    const char *end;
    MesaGLSLLookupFn lookup;
    MesaGLSLCallFn call;
    void *user;
    void (*suppress_side_effects)(void *user, int enable);
    int (*increment)(void *user, const char *start, const char *end,
                     int delta, int postfix, MesaGLSLValue *value);
    int (*assign_expression)(void *user, const char *start, const char *end,
                             const MesaGLSLValue *value);
    int failed;
} Parser;

static int safe_integer_value(float value)
{
    if (!isfinite(value) || value < -2147483648.0f || value >= 2147483648.0f)
        return 0;
    return (int)value;
}

static void skip_space(Parser *parser)
{
    for (;;) {
        while (parser->cursor < parser->end &&
               (*parser->cursor == ' ' || *parser->cursor == '\t' || *parser->cursor == '\r' ||
                *parser->cursor == '\n'))
            ++parser->cursor;
        if (parser->end - parser->cursor >= 2 && parser->cursor[0] == '/' &&
            parser->cursor[1] == '/') {
            parser->cursor += 2;
            while (parser->cursor < parser->end && *parser->cursor != '\n')
                ++parser->cursor;
            continue;
        }
        if (parser->end - parser->cursor >= 2 && parser->cursor[0] == '/' &&
            parser->cursor[1] == '*') {
            parser->cursor += 2;
            while (parser->end - parser->cursor >= 2 &&
                   !(parser->cursor[0] == '*' && parser->cursor[1] == '/'))
                ++parser->cursor;
            if (parser->end - parser->cursor >= 2)
                parser->cursor += 2;
            continue;
        }
        break;
    }
}

static int accept(Parser *parser, char token)
{
    skip_space(parser);
    if (parser->cursor < parser->end && *parser->cursor == token) {
        ++parser->cursor;
        return 1;
    }
    return 0;
}

static int accept_text(Parser *parser, const char *text)
{
    size_t length = strlen(text);

    skip_space(parser);
    if ((size_t)(parser->end - parser->cursor) >= length &&
        !strncmp(parser->cursor, text, length)) {
        parser->cursor += length;
        return 1;
    }
    return 0;
}

static int identifier(Parser *parser, const char **start, size_t *length)
{
    const char *cursor;

    skip_space(parser);
    cursor = parser->cursor;
    if (cursor >= parser->end || !((*cursor >= 'a' && *cursor <= 'z') ||
                                   (*cursor >= 'A' && *cursor <= 'Z') || *cursor == '_'))
        return 0;
    ++cursor;
    while (cursor < parser->end && ((*cursor >= 'a' && *cursor <= 'z') ||
                                    (*cursor >= 'A' && *cursor <= 'Z') ||
                                    (*cursor >= '0' && *cursor <= '9') || *cursor == '_'))
        ++cursor;
    *start = parser->cursor;
    *length = (size_t)(cursor - parser->cursor);
    parser->cursor = cursor;
    return 1;
}

static MesaGLSLValue scalar(float value)
{
    MesaGLSLValue result = {
        {0}, 1, 1, NULL, 0, NULL, NULL, 0, {0}, {0}, 0, MESAGL_GLSL_TYPE_FLOAT, NULL, 0};

    result.data[0] = value;
    return result;
}

static MesaGLSLValue boolean(float value)
{
    MesaGLSLValue result = scalar(value != 0.0f);

    result.type = MESAGL_GLSL_TYPE_BOOL;
    return result;
}

static int components(const MesaGLSLValue *value)
{
    return value->rows * value->columns;
}

static float component(const MesaGLSLValue *value, int index)
{
    return value->data[components(value) == 1 ? 0 : index];
}

static MesaGLSLValue assignment_expression(Parser *parser);
static MesaGLSLValue expression(Parser *parser);
static int same_struct_type(const char *left_name, size_t left_length,
                            const char *right_name, size_t right_length);

static int name_is(const char *name, size_t length, const char *expected)
{
    return strlen(expected) == length && !strncmp(name, expected, length);
}

static MesaGLSLType normalized_type(const MesaGLSLValue *value)
{
    return value->type == MESAGL_GLSL_TYPE_UNKNOWN ? MESAGL_GLSL_TYPE_FLOAT : value->type;
}

static int is_float_gen_type(const MesaGLSLValue *value)
{
    return !value->member_count && !value->array_size && value->columns == 1 &&
           normalized_type(value) == MESAGL_GLSL_TYPE_FLOAT;
}

static int is_bool_gen_type(const MesaGLSLValue *value)
{
    return !value->member_count && !value->array_size && value->columns == 1 &&
           normalized_type(value) == MESAGL_GLSL_TYPE_BOOL;
}

static int is_numeric_gen_type(const MesaGLSLValue *value)
{
    MesaGLSLType type = normalized_type(value);

    return !value->member_count && !value->array_size && value->columns == 1 &&
           (type == MESAGL_GLSL_TYPE_FLOAT || type == MESAGL_GLSL_TYPE_INT);
}

static int same_gen_type(const MesaGLSLValue *left, const MesaGLSLValue *right)
{
    return left->rows == right->rows && left->columns == right->columns &&
           normalized_type(left) == normalized_type(right) && !left->member_count &&
           !right->member_count && !left->array_size && !right->array_size;
}

static int builtin(const char *name, size_t length, const MesaGLSLValue *arguments,
                   int argument_count, MesaGLSLValue *result)
{
    int count;
    int i;

    if (argument_count < 1)
        return 0;
    count = components(&arguments[0]);
    *result = arguments[0];
    if (name_is(name, length, "dot") && argument_count == 2 &&
        is_float_gen_type(&arguments[0]) && same_gen_type(&arguments[0], &arguments[1])) {
        *result = scalar(0.0f);
        for (i = 0; i < count; ++i) {
            result->data[0] += arguments[0].data[i] * arguments[1].data[i];
            result->dfdx[0] += arguments[0].dfdx[i] * arguments[1].data[i] +
                               arguments[0].data[i] * arguments[1].dfdx[i];
            result->dfdy[0] += arguments[0].dfdy[i] * arguments[1].data[i] +
                               arguments[0].data[i] * arguments[1].dfdy[i];
        }
        result->has_derivatives =
            arguments[0].has_derivatives || arguments[1].has_derivatives;
        return 1;
    }
    if (name_is(name, length, "distance") && argument_count == 2 &&
        is_float_gen_type(&arguments[0]) && same_gen_type(&arguments[0], &arguments[1])) {
        float distance = 0.0f;
        float derivative_x = 0.0f;
        float derivative_y = 0.0f;

        for (i = 0; i < count; ++i) {
            float difference = arguments[0].data[i] - arguments[1].data[i];

            distance += difference * difference;
            derivative_x += difference *
                            (arguments[0].dfdx[i] - arguments[1].dfdx[i]);
            derivative_y += difference *
                            (arguments[0].dfdy[i] - arguments[1].dfdy[i]);
        }
        distance = sqrtf(distance);
        *result = scalar(distance);
        if (distance > 0.0f) {
            result->dfdx[0] = derivative_x / distance;
            result->dfdy[0] = derivative_y / distance;
        }
        result->has_derivatives = arguments[0].has_derivatives ||
                                  arguments[1].has_derivatives;
        return 1;
    }
    if (name_is(name, length, "cross") && argument_count == 2 && count == 3 &&
        is_float_gen_type(&arguments[0]) && same_gen_type(&arguments[0], &arguments[1])) {
        result->data[0] = arguments[0].data[1] * arguments[1].data[2] -
                          arguments[0].data[2] * arguments[1].data[1];
        result->data[1] = arguments[0].data[2] * arguments[1].data[0] -
                          arguments[0].data[0] * arguments[1].data[2];
        result->data[2] = arguments[0].data[0] * arguments[1].data[1] -
                          arguments[0].data[1] * arguments[1].data[0];
        result->dfdx[0] = arguments[0].dfdx[1] * arguments[1].data[2] +
                          arguments[0].data[1] * arguments[1].dfdx[2] -
                          arguments[0].dfdx[2] * arguments[1].data[1] -
                          arguments[0].data[2] * arguments[1].dfdx[1];
        result->dfdx[1] = arguments[0].dfdx[2] * arguments[1].data[0] +
                          arguments[0].data[2] * arguments[1].dfdx[0] -
                          arguments[0].dfdx[0] * arguments[1].data[2] -
                          arguments[0].data[0] * arguments[1].dfdx[2];
        result->dfdx[2] = arguments[0].dfdx[0] * arguments[1].data[1] +
                          arguments[0].data[0] * arguments[1].dfdx[1] -
                          arguments[0].dfdx[1] * arguments[1].data[0] -
                          arguments[0].data[1] * arguments[1].dfdx[0];
        result->dfdy[0] = arguments[0].dfdy[1] * arguments[1].data[2] +
                          arguments[0].data[1] * arguments[1].dfdy[2] -
                          arguments[0].dfdy[2] * arguments[1].data[1] -
                          arguments[0].data[2] * arguments[1].dfdy[1];
        result->dfdy[1] = arguments[0].dfdy[2] * arguments[1].data[0] +
                          arguments[0].data[2] * arguments[1].dfdy[0] -
                          arguments[0].dfdy[0] * arguments[1].data[2] -
                          arguments[0].data[0] * arguments[1].dfdy[2];
        result->dfdy[2] = arguments[0].dfdy[0] * arguments[1].data[1] +
                          arguments[0].data[0] * arguments[1].dfdy[1] -
                          arguments[0].dfdy[1] * arguments[1].data[0] -
                          arguments[0].data[1] * arguments[1].dfdy[0];
        result->has_derivatives = arguments[0].has_derivatives ||
                                  arguments[1].has_derivatives;
        return 1;
    }
    if ((name_is(name, length, "length") || name_is(name, length, "normalize")) &&
        argument_count == 1 && is_float_gen_type(&arguments[0])) {
        float length_value = 0.0f;
        float length_dfdx = 0.0f;
        float length_dfdy = 0.0f;

        for (i = 0; i < count; ++i) {
            length_value += arguments[0].data[i] * arguments[0].data[i];
            length_dfdx += arguments[0].data[i] * arguments[0].dfdx[i];
            length_dfdy += arguments[0].data[i] * arguments[0].dfdy[i];
        }
        length_value = sqrtf(length_value);
        if (length_value > 0.0f) {
            length_dfdx /= length_value;
            length_dfdy /= length_value;
        }
        if (name_is(name, length, "length")) {
            *result = scalar(length_value);
            result->dfdx[0] = length_dfdx;
            result->dfdy[0] = length_dfdy;
        } else if (length_value > 0.0f)
            for (i = 0; i < count; ++i) {
                result->data[i] /= length_value;
                result->dfdx[i] = arguments[0].dfdx[i] / length_value -
                                  arguments[0].data[i] * length_dfdx /
                                      (length_value * length_value);
                result->dfdy[i] = arguments[0].dfdy[i] / length_value -
                                  arguments[0].data[i] * length_dfdy /
                                      (length_value * length_value);
            }
        result->has_derivatives = arguments[0].has_derivatives;
        return 1;
    }
    if (argument_count == 1 &&
        (name_is(name, length, "abs") || name_is(name, length, "sign")) &&
        is_float_gen_type(&arguments[0])) {
        for (i = 0; i < count; ++i) {
            float value = arguments[0].data[i];

            result->data[i] = name_is(name, length, "abs")
                                  ? fabsf(value)
                                  : value > 0.0f ? 1.0f : value < 0.0f ? -1.0f : 0.0f;
            if (name_is(name, length, "abs")) {
                float factor = value > 0.0f ? 1.0f : value < 0.0f ? -1.0f : 0.0f;

                result->dfdx[i] = factor * arguments[0].dfdx[i];
                result->dfdy[i] = factor * arguments[0].dfdy[i];
            } else {
                result->dfdx[i] = 0.0f;
                result->dfdy[i] = 0.0f;
            }
        }
        result->has_derivatives = name_is(name, length, "abs") &&
                                  arguments[0].has_derivatives;
        return 1;
    }
    if (argument_count == 1 &&
        (name_is(name, length, "floor") ||
         name_is(name, length, "ceil") || name_is(name, length, "fract") ||
         name_is(name, length, "sqrt") || name_is(name, length, "sin") ||
         name_is(name, length, "cos") || name_is(name, length, "tan") ||
         name_is(name, length, "asin") || name_is(name, length, "acos") ||
         name_is(name, length, "atan") || name_is(name, length, "exp") ||
         name_is(name, length, "exp2") || name_is(name, length, "log") ||
         name_is(name, length, "log2") || name_is(name, length, "inversesqrt") ||
         name_is(name, length, "radians") ||
         name_is(name, length, "degrees")) && is_float_gen_type(&arguments[0])) {
        for (i = 0; i < count; ++i) {
            float value = arguments[0].data[i];
            float factor = 0.0f;

            if (name_is(name, length, "floor"))
                result->data[i] = floorf(value);
            else if (name_is(name, length, "ceil"))
                result->data[i] = ceilf(value);
            else if (name_is(name, length, "fract"))
                result->data[i] = value - floorf(value);
            else if (name_is(name, length, "sqrt"))
                result->data[i] = sqrtf(value);
            else if (name_is(name, length, "sin"))
                result->data[i] = sinf(value);
            else if (name_is(name, length, "cos"))
                result->data[i] = cosf(value);
            else if (name_is(name, length, "tan"))
                result->data[i] = tanf(value);
            else if (name_is(name, length, "asin"))
                result->data[i] = asinf(value);
            else if (name_is(name, length, "acos"))
                result->data[i] = acosf(value);
            else if (name_is(name, length, "atan"))
                result->data[i] = atanf(value);
            else if (name_is(name, length, "exp"))
                result->data[i] = expf(value);
            else if (name_is(name, length, "exp2"))
                result->data[i] = exp2f(value);
            else if (name_is(name, length, "log"))
                result->data[i] = logf(value);
            else if (name_is(name, length, "log2"))
                result->data[i] = log2f(value);
            else if (name_is(name, length, "inversesqrt"))
                result->data[i] = 1.0f / sqrtf(value);
            else if (name_is(name, length, "radians"))
                result->data[i] = value * 0.01745329251994329577f;
            else
                result->data[i] = value * 57.295779513082320876f;
            if (name_is(name, length, "fract"))
                factor = 1.0f;
            else if (name_is(name, length, "sqrt"))
                factor = value > 0.0f ? 0.5f / sqrtf(value) : 0.0f;
            else if (name_is(name, length, "sin"))
                factor = cosf(value);
            else if (name_is(name, length, "cos"))
                factor = -sinf(value);
            else if (name_is(name, length, "tan")) {
                float cosine = cosf(value);

                factor = 1.0f / (cosine * cosine);
            } else if (name_is(name, length, "asin"))
                factor = 1.0f / sqrtf(1.0f - value * value);
            else if (name_is(name, length, "acos"))
                factor = -1.0f / sqrtf(1.0f - value * value);
            else if (name_is(name, length, "atan"))
                factor = 1.0f / (1.0f + value * value);
            else if (name_is(name, length, "exp"))
                factor = expf(value);
            else if (name_is(name, length, "exp2"))
                factor = 0.6931471805599453f * exp2f(value);
            else if (name_is(name, length, "log"))
                factor = 1.0f / value;
            else if (name_is(name, length, "log2"))
                factor = 1.4426950408889634f / value;
            else if (name_is(name, length, "inversesqrt"))
                factor = -0.5f / (value * sqrtf(value));
            else if (name_is(name, length, "radians"))
                factor = 0.01745329251994329577f;
            else if (name_is(name, length, "degrees"))
                factor = 57.295779513082320876f;
            result->dfdx[i] = factor * arguments[0].dfdx[i];
            result->dfdy[i] = factor * arguments[0].dfdy[i];
        }
        result->has_derivatives = arguments[0].has_derivatives;
        return 1;
    }
    if (argument_count == 2 &&
        (name_is(name, length, "min") || name_is(name, length, "max") ||
         name_is(name, length, "pow") || name_is(name, length, "mod") ||
         name_is(name, length, "step") || name_is(name, length, "atan"))) {
        int right_count = components(&arguments[1]);

        if (name_is(name, length, "min") || name_is(name, length, "max")) {
            if (!is_float_gen_type(&arguments[0]) ||
                !is_float_gen_type(&arguments[1]))
                return 0;
        } else if (!is_float_gen_type(&arguments[0]) ||
                   !is_float_gen_type(&arguments[1])) {
            return 0;
        }
        if (name_is(name, length, "step")) {
            if (count != 1 && !same_gen_type(&arguments[0], &arguments[1]))
                return 0;
        } else if ((name_is(name, length, "pow") || name_is(name, length, "atan")) ?
                       !same_gen_type(&arguments[0], &arguments[1]) :
                       (right_count != 1 &&
                        !same_gen_type(&arguments[0], &arguments[1]))) {
            return 0;
        }
        if (name_is(name, length, "step") && count == 1 && right_count > 1) {
            *result = arguments[1];
            count = right_count;
        }
        for (i = 0; i < count; ++i) {
            float right = component(&arguments[1], i);
            int right_index = components(&arguments[1]) == 1 ? 0 : i;
            int left_index = components(&arguments[0]) == 1 ? 0 : i;
            float left = arguments[0].data[left_index];
            float left_dfdx = arguments[0].dfdx[left_index];
            float left_dfdy = arguments[0].dfdy[left_index];
            float right_dfdx = arguments[1].dfdx[right_index];
            float right_dfdy = arguments[1].dfdy[right_index];
            float factor;

            if (name_is(name, length, "min")) {
                result->data[i] = fminf(result->data[i], right);
                if (right < left) {
                    result->dfdx[i] = right_dfdx;
                    result->dfdy[i] = right_dfdy;
                }
            } else if (name_is(name, length, "max")) {
                result->data[i] = fmaxf(result->data[i], right);
                if (right > left) {
                    result->dfdx[i] = right_dfdx;
                    result->dfdy[i] = right_dfdy;
                }
            } else if (name_is(name, length, "pow")) {
                result->data[i] = powf(result->data[i], right);
                factor = left > 0.0f ? result->data[i] : 0.0f;
                result->dfdx[i] = left > 0.0f
                                      ? factor * (right_dfdx * logf(left) +
                                                  right * left_dfdx / left)
                                      : 0.0f;
                result->dfdy[i] = left > 0.0f
                                      ? factor * (right_dfdy * logf(left) +
                                                  right * left_dfdy / left)
                                      : 0.0f;
            } else if (name_is(name, length, "mod")) {
                factor = floorf(left / right);
                result->data[i] -= right * factor;
                result->dfdx[i] = left_dfdx - factor * right_dfdx;
                result->dfdy[i] = left_dfdy - factor * right_dfdy;
            } else if (name_is(name, length, "step")) {
                result->data[i] = right < left ? 0.0f : 1.0f;
                result->dfdx[i] = 0.0f;
                result->dfdy[i] = 0.0f;
            } else {
                result->data[i] = atan2f(result->data[i], right);
                factor = left * left + right * right;
                result->dfdx[i] =
                    factor > 0.0f ? (right * left_dfdx - left * right_dfdx) / factor : 0.0f;
                result->dfdy[i] =
                    factor > 0.0f ? (right * left_dfdy - left * right_dfdy) / factor : 0.0f;
            }
        }
        result->has_derivatives =
            arguments[0].has_derivatives || arguments[1].has_derivatives;
        return 1;
    }
    if (argument_count == 3 &&
        (name_is(name, length, "clamp") || name_is(name, length, "mix") ||
         name_is(name, length, "smoothstep"))) {
        int second_count = components(&arguments[1]);
        int third_count = components(&arguments[2]);

        if (name_is(name, length, "clamp")) {
            if (!is_float_gen_type(&arguments[0]) ||
                !is_float_gen_type(&arguments[1]) ||
                !is_float_gen_type(&arguments[2]))
                return 0;
        } else if (!is_float_gen_type(&arguments[0]) ||
                   !is_float_gen_type(&arguments[1]) ||
                   !is_float_gen_type(&arguments[2])) {
            return 0;
        }
        if (name_is(name, length, "smoothstep")) {
            if ((components(&arguments[0]) != 1 || second_count != 1) &&
                (!same_gen_type(&arguments[0], &arguments[2]) ||
                 !same_gen_type(&arguments[1], &arguments[2])))
                return 0;
            *result = arguments[2];
            count = third_count;
        } else if (name_is(name, length, "clamp")) {
            if ((!same_gen_type(&arguments[0], &arguments[1]) ||
                 !same_gen_type(&arguments[0], &arguments[2])) &&
                !(second_count == 1 && third_count == 1))
                return 0;
        } else if (!same_gen_type(&arguments[0], &arguments[1]) ||
                   (third_count != 1 && !same_gen_type(&arguments[0], &arguments[2]))) {
            return 0;
        }
        for (i = 0; i < count; ++i) {
            float b = component(&arguments[1], i);
            float c = component(&arguments[2], i);
            int a_index = components(&arguments[0]) == 1 ? 0 : i;
            int b_index = components(&arguments[1]) == 1 ? 0 : i;
            int c_index = components(&arguments[2]) == 1 ? 0 : i;
            float a = arguments[0].data[a_index];
            float adx = arguments[0].dfdx[a_index], ady = arguments[0].dfdy[a_index];
            float bdx = arguments[1].dfdx[b_index], bdy = arguments[1].dfdy[b_index];
            float cdx = arguments[2].dfdx[c_index], cdy = arguments[2].dfdy[c_index];

            if (name_is(name, length, "clamp")) {
                result->data[i] = fminf(fmaxf(a, b), c);
                if (a < b) {
                    result->dfdx[i] = bdx;
                    result->dfdy[i] = bdy;
                } else if (a > c) {
                    result->dfdx[i] = cdx;
                    result->dfdy[i] = cdy;
                }
            } else if (name_is(name, length, "mix")) {
                result->data[i] = a * (1.0f - c) + b * c;
                result->dfdx[i] = adx * (1.0f - c) + bdx * c + (b - a) * cdx;
                result->dfdy[i] = ady * (1.0f - c) + bdy * c + (b - a) * cdy;
            } else {
                float denominator = b - a;
                float raw = denominator != 0.0f ? (c - a) / denominator : 0.0f;
                float interpolation = fminf(fmaxf(raw, 0.0f), 1.0f);
                float raw_dfdx = denominator != 0.0f
                                      ? ((cdx - adx) * denominator -
                                         (c - a) * (bdx - adx)) /
                                            (denominator * denominator)
                                      : 0.0f;
                float raw_dfdy = denominator != 0.0f
                                      ? ((cdy - ady) * denominator -
                                         (c - a) * (bdy - ady)) /
                                            (denominator * denominator)
                                      : 0.0f;
                float curve_derivative =
                    raw > 0.0f && raw < 1.0f ? 6.0f * interpolation * (1.0f - interpolation)
                                             : 0.0f;

                result->data[i] = interpolation * interpolation * (3.0f - 2.0f * interpolation);
                result->dfdx[i] = curve_derivative * raw_dfdx;
                result->dfdy[i] = curve_derivative * raw_dfdy;
            }
        }
        result->has_derivatives = arguments[0].has_derivatives ||
                                  arguments[1].has_derivatives ||
                                  arguments[2].has_derivatives;
        return 1;
    }
    if ((name_is(name, length, "reflect") || name_is(name, length, "faceforward")) &&
        argument_count == (name_is(name, length, "reflect") ? 2 : 3)) {
        float product = 0.0f;
        float product_dfdx = 0.0f;
        float product_dfdy = 0.0f;

        if (!is_float_gen_type(&arguments[0]) ||
            !same_gen_type(&arguments[0], &arguments[1]) ||
            (argument_count == 3 && !same_gen_type(&arguments[0], &arguments[2])))
            return 0;
        for (i = 0; i < count; ++i) {
            int product_argument = argument_count == 2 ? 0 : 2;

            product += arguments[argument_count == 2 ? 0 : 2].data[i] * arguments[1].data[i];
            product_dfdx += arguments[product_argument].dfdx[i] * arguments[1].data[i] +
                            arguments[product_argument].data[i] * arguments[1].dfdx[i];
            product_dfdy += arguments[product_argument].dfdy[i] * arguments[1].data[i] +
                            arguments[product_argument].data[i] * arguments[1].dfdy[i];
        }
        if (argument_count == 2) {
            for (i = 0; i < count; ++i) {
                result->data[i] -= 2.0f * product * arguments[1].data[i];
                result->dfdx[i] = arguments[0].dfdx[i] -
                                  2.0f * (product_dfdx * arguments[1].data[i] +
                                          product * arguments[1].dfdx[i]);
                result->dfdy[i] = arguments[0].dfdy[i] -
                                  2.0f * (product_dfdy * arguments[1].data[i] +
                                          product * arguments[1].dfdy[i]);
            }
        } else if (product >= 0.0f) {
            for (i = 0; i < count; ++i) {
                result->data[i] = -result->data[i];
                result->dfdx[i] = -result->dfdx[i];
                result->dfdy[i] = -result->dfdy[i];
            }
        }
        result->has_derivatives = arguments[0].has_derivatives ||
                                  arguments[1].has_derivatives ||
                                  (argument_count == 3 && arguments[2].has_derivatives);
        return 1;
    }
    if (name_is(name, length, "refract") && argument_count == 3 &&
        is_float_gen_type(&arguments[0]) && same_gen_type(&arguments[0], &arguments[1]) &&
        is_float_gen_type(&arguments[2]) && components(&arguments[2]) == 1) {
        float product = 0.0f;
        float product_dfdx = 0.0f;
        float product_dfdy = 0.0f;
        float eta = arguments[2].data[0];
        float eta_dfdx = arguments[2].dfdx[0];
        float eta_dfdy = arguments[2].dfdy[0];
        float k;

        for (i = 0; i < count; ++i) {
            product += arguments[1].data[i] * arguments[0].data[i];
            product_dfdx += arguments[1].dfdx[i] * arguments[0].data[i] +
                            arguments[1].data[i] * arguments[0].dfdx[i];
            product_dfdy += arguments[1].dfdy[i] * arguments[0].data[i] +
                            arguments[1].data[i] * arguments[0].dfdy[i];
        }
        k = 1.0f - eta * eta * (1.0f - product * product);
        if (k < 0.0f) {
            memset(result->data, 0, (size_t)count * sizeof(float));
            memset(result->dfdx, 0, (size_t)count * sizeof(float));
            memset(result->dfdy, 0, (size_t)count * sizeof(float));
        } else {
            float root = sqrtf(k);
            float k_dfdx = -2.0f * eta * eta_dfdx * (1.0f - product * product) +
                           2.0f * eta * eta * product * product_dfdx;
            float k_dfdy = -2.0f * eta * eta_dfdy * (1.0f - product * product) +
                           2.0f * eta * eta * product * product_dfdy;
            float root_dfdx = root > 0.0f ? k_dfdx / (2.0f * root) : 0.0f;
            float root_dfdy = root > 0.0f ? k_dfdy / (2.0f * root) : 0.0f;
            float factor = eta * product + root;
            float factor_dfdx = eta_dfdx * product + eta * product_dfdx + root_dfdx;
            float factor_dfdy = eta_dfdy * product + eta * product_dfdy + root_dfdy;

            for (i = 0; i < count; ++i) {
                result->data[i] = eta * arguments[0].data[i] -
                                  factor * arguments[1].data[i];
                result->dfdx[i] = eta_dfdx * arguments[0].data[i] +
                                  eta * arguments[0].dfdx[i] -
                                  factor_dfdx * arguments[1].data[i] -
                                  factor * arguments[1].dfdx[i];
                result->dfdy[i] = eta_dfdy * arguments[0].data[i] +
                                  eta * arguments[0].dfdy[i] -
                                  factor_dfdy * arguments[1].data[i] -
                                  factor * arguments[1].dfdy[i];
            }
        }
        result->has_derivatives = arguments[0].has_derivatives ||
                                  arguments[1].has_derivatives ||
                                  arguments[2].has_derivatives;
        return 1;
    }
    if (name_is(name, length, "matrixCompMult") && argument_count == 2 &&
        arguments[0].columns > 1 && normalized_type(&arguments[0]) == MESAGL_GLSL_TYPE_FLOAT &&
        same_gen_type(&arguments[0], &arguments[1])) {
        for (i = 0; i < count; ++i) {
            result->data[i] *= arguments[1].data[i];
            result->dfdx[i] = arguments[0].dfdx[i] * arguments[1].data[i] +
                              arguments[0].data[i] * arguments[1].dfdx[i];
            result->dfdy[i] = arguments[0].dfdy[i] * arguments[1].data[i] +
                              arguments[0].data[i] * arguments[1].dfdy[i];
        }
        result->has_derivatives =
            arguments[0].has_derivatives || arguments[1].has_derivatives;
        return 1;
    }
    if ((name_is(name, length, "any") || name_is(name, length, "all")) &&
        argument_count == 1 && is_bool_gen_type(&arguments[0]) && count > 1) {
        int truth = name_is(name, length, "all");

        for (i = 0; i < count; ++i)
            if (name_is(name, length, "all"))
                truth = truth && arguments[0].data[i] != 0.0f;
            else
                truth = truth || arguments[0].data[i] != 0.0f;
        *result = boolean((float)truth);
        return 1;
    }
    if (name_is(name, length, "not") && argument_count == 1 &&
        is_bool_gen_type(&arguments[0]) && count > 1) {
        for (i = 0; i < count; ++i)
            result->data[i] = arguments[0].data[i] == 0.0f;
        result->type = MESAGL_GLSL_TYPE_BOOL;
        return 1;
    }
    if (argument_count == 2 && components(&arguments[1]) == count &&
        (name_is(name, length, "lessThan") || name_is(name, length, "lessThanEqual") ||
         name_is(name, length, "greaterThan") || name_is(name, length, "greaterThanEqual") ||
         name_is(name, length, "equal") || name_is(name, length, "notEqual")) &&
        count > 1 && same_gen_type(&arguments[0], &arguments[1]) &&
        (is_numeric_gen_type(&arguments[0]) ||
         ((name_is(name, length, "equal") || name_is(name, length, "notEqual")) &&
          is_bool_gen_type(&arguments[0])))) {
        for (i = 0; i < count; ++i) {
            float left = arguments[0].data[i];
            float right = arguments[1].data[i];

            result->data[i] = name_is(name, length, "lessThan")          ? left < right
                              : name_is(name, length, "lessThanEqual")   ? left <= right
                              : name_is(name, length, "greaterThan")     ? left > right
                              : name_is(name, length, "greaterThanEqual") ? left >= right
                              : name_is(name, length, "equal")           ? left == right
                                                                           : left != right;
        }
        result->type = MESAGL_GLSL_TYPE_BOOL;
        return 1;
    }
    return 0;
}

static MesaGLSLValue construct(Parser *parser, const char *name, size_t length)
{
    MesaGLSLValue arguments[16];
    MesaGLSLValue result = {
        {0}, 1, 1, NULL, 0, NULL, NULL, 0, {0}, {0}, 0, MESAGL_GLSL_TYPE_UNKNOWN, NULL, 0};
    int argument_count = 0;
    int wanted = 0;
    int matrix = 0;
    int output = 0;
    int supplied = 0;
    int i;

    if (!accept(parser, '(')) {
        parser->failed = 1;
        return result;
    }
    if (!accept(parser, ')')) {
        do {
            if (argument_count >= 16) {
                parser->failed = 1;
                return result;
            }
            arguments[argument_count++] = assignment_expression(parser);
        } while (accept(parser, ','));
        if (!accept(parser, ')'))
            parser->failed = 1;
    }
    if (name_is(name, length, "float") || name_is(name, length, "int") ||
        name_is(name, length, "bool"))
        wanted = 1;
    else if (name_is(name, length, "vec2") || name_is(name, length, "ivec2") ||
             name_is(name, length, "bvec2"))
        wanted = 2;
    else if (name_is(name, length, "vec3") || name_is(name, length, "ivec3") ||
             name_is(name, length, "bvec3"))
        wanted = 3;
    else if (name_is(name, length, "vec4") || name_is(name, length, "ivec4") ||
             name_is(name, length, "bvec4"))
        wanted = 4;
    else if (name_is(name, length, "mat2")) {
        wanted = 4;
        matrix = 2;
    } else if (name_is(name, length, "mat3")) {
        wanted = 9;
        matrix = 3;
    } else if (name_is(name, length, "mat4")) {
        wanted = 16;
        matrix = 4;
    } else if (builtin(name, length, arguments, argument_count, &result))
        return result;
    else if (parser->call &&
               parser->call(parser->user, name, length, arguments, argument_count, &result))
        return result;
    else {
        parser->failed = 1;
        return result;
    }
    for (i = 0; i < argument_count; ++i) {
        if (arguments[i].member_count || arguments[i].array_size ||
            normalized_type(&arguments[i]) == MESAGL_GLSL_TYPE_SAMPLER2D ||
            normalized_type(&arguments[i]) == MESAGL_GLSL_TYPE_SAMPLERCUBE) {
            parser->failed = 1;
            return result;
        }
        if (arguments[i].columns > 1 && argument_count != 1) {
            parser->failed = 1;
            return result;
        }
        supplied += components(&arguments[i]);
    }
    if (!argument_count ||
        (wanted == 1 && (argument_count != 1 || supplied < 1)) ||
        (wanted > 1 && !(argument_count == 1 && supplied == 1) &&
         !(!matrix && argument_count == 1 && supplied >= wanted) &&
         !(matrix && argument_count == 1 && arguments[0].columns > 1) &&
         supplied != wanted)) {
        parser->failed = 1;
        return result;
    }
    if (matrix && argument_count == 1 && arguments[0].columns > 1) {
        int column;
        int row;

        for (i = 0; i < matrix; ++i)
            result.data[i * matrix + i] = 1.0f;
        for (column = 0; column < matrix && column < arguments[0].columns; ++column)
            for (row = 0; row < matrix && row < arguments[0].rows; ++row) {
                int destination = column * matrix + row;
                int source = column * arguments[0].rows + row;

                result.data[destination] = arguments[0].data[source];
                result.dfdx[destination] = arguments[0].dfdx[source];
                result.dfdy[destination] = arguments[0].dfdy[source];
            }
        result.has_derivatives = arguments[0].has_derivatives;
    } else if (argument_count == 1 && components(&arguments[0]) == 1 && wanted > 1) {
        if (matrix) {
            for (i = 0; i < matrix; ++i) {
                result.data[i * matrix + i] = arguments[0].data[0];
                result.dfdx[i * matrix + i] = arguments[0].dfdx[0];
                result.dfdy[i * matrix + i] = arguments[0].dfdy[0];
            }
        } else
            for (i = 0; i < wanted; ++i) {
                result.data[i] = arguments[0].data[0];
                result.dfdx[i] = arguments[0].dfdx[0];
                result.dfdy[i] = arguments[0].dfdy[0];
            }
        result.has_derivatives = arguments[0].has_derivatives;
    } else
        for (i = 0; i < argument_count && output < wanted; ++i) {
            int item;

            for (item = 0; item < components(&arguments[i]) && output < wanted; ++item) {
                result.data[output] = arguments[i].data[item];
                result.dfdx[output] = arguments[i].dfdx[item];
                result.dfdy[output] = arguments[i].dfdy[item];
                ++output;
            }
            result.has_derivatives =
                result.has_derivatives || arguments[i].has_derivatives;
        }
    result.rows = matrix ? matrix : wanted;
    result.columns = matrix ? matrix : 1;
    result.type = name[0] == 'i' ? MESAGL_GLSL_TYPE_INT
                  : name[0] == 'b' ? MESAGL_GLSL_TYPE_BOOL
                                   : MESAGL_GLSL_TYPE_FLOAT;
    if ((output != wanted &&
         !(argument_count == 1 && components(&arguments[0]) == 1) &&
         !(matrix && argument_count == 1 && arguments[0].columns > 1)) ||
        parser->failed)
        parser->failed = 1;
    if (name[0] == 'i')
        for (i = 0; i < wanted; ++i)
            result.data[i] = (float)safe_integer_value(result.data[i]);
    else if (name[0] == 'b')
        for (i = 0; i < wanted; ++i)
            result.data[i] = result.data[i] != 0.0f;
    return result;
}

static MesaGLSLValue swizzle(Parser *parser, MesaGLSLValue value)
{
    const char *name;
    size_t length;
    MesaGLSLValue result = {
        {0}, 1, 1, NULL, 0, NULL, NULL, 0, {0}, {0}, 0, MESAGL_GLSL_TYPE_UNKNOWN, NULL, 0};
    size_t i;
    const char *component_set = NULL;

    if (!accept(parser, '.'))
        return value;
    if (!identifier(parser, &name, &length) || length > 4) {
        parser->failed = 1;
        return result;
    }
    for (i = 0; i < length; ++i) {
        const char *channels = "xyzw";
        const char *rgba = "rgba";
        const char *stpq = "stpq";
        const char *found = strchr(channels, name[i]);
        const char *selected = channels;
        int index;

        if (!found) {
            found = strchr(rgba, name[i]);
            selected = rgba;
        }
        if (!found) {
            found = strchr(stpq, name[i]);
            selected = stpq;
        }
        if (!found) {
            parser->failed = 1;
            return result;
        }
        if (component_set && component_set != selected) {
            parser->failed = 1;
            return result;
        }
        component_set = selected;
        index = (int)(found - selected);
        if (index >= components(&value)) {
            parser->failed = 1;
            return result;
        }
        result.data[i] = value.data[index];
        result.dfdx[i] = value.dfdx[index];
        result.dfdy[i] = value.dfdy[index];
    }
    result.rows = (int)length;
    result.columns = 1;
    result.type = value.type;
    result.has_derivatives = value.has_derivatives;
    return result;
}

static MesaGLSLValue postfix(Parser *parser, MesaGLSLValue value)
{
    char struct_path[MESAGL_MAX_SHADER_LVALUE_PATH_LENGTH] = {0};
    size_t struct_path_length = 0;

    for (;;) {
        skip_space(parser);
        if (parser->cursor < parser->end && *parser->cursor == '.') {
            if (value.member_count > 0) {
                const char *member_name;
                size_t member_length;
                char requested[MESAGL_MAX_SHADER_LVALUE_PATH_LENGTH];
                size_t requested_length;
                int member;

                ++parser->cursor;
                if (!identifier(parser, &member_name, &member_length)) {
                    parser->failed = 1;
                    return value;
                }
                requested_length = struct_path_length + (struct_path_length ? 1 : 0) +
                                   member_length;
                if (requested_length >= sizeof(requested)) {
                    parser->failed = 1;
                    return value;
                }
                if (struct_path_length) {
                    memcpy(requested, struct_path, struct_path_length);
                    requested[struct_path_length] = '.';
                }
                memcpy(requested + struct_path_length + (struct_path_length ? 1 : 0),
                       member_name, member_length);
                requested[requested_length] = '\0';
                for (member = 0; member < value.member_count; ++member)
                    if (strlen(value.member_names[member]) == requested_length &&
                        !strncmp(value.member_names[member], requested, requested_length))
                        break;
                if (member == value.member_count) {
                    for (member = 0; member < value.member_count; ++member)
                        if (!strncmp(value.member_names[member], requested,
                                     requested_length) &&
                            (value.member_names[member][requested_length] == '.' ||
                             value.member_names[member][requested_length] == '['))
                            break;
                    if (member == value.member_count) {
                        parser->failed = 1;
                        return value;
                    }
                    memcpy(struct_path, requested, requested_length + 1);
                    struct_path_length = requested_length;
                    continue;
                }
                value = value.members[member];
                struct_path[0] = '\0';
                struct_path_length = 0;
            } else
                value = swizzle(parser, value);
        } else if (accept(parser, '[')) {
            MesaGLSLValue subscript = expression(parser);
            MesaGLSLValue selected = {
                {0}, 1, 1, NULL, 0, NULL, NULL, 0, {0}, {0}, 0,
                MESAGL_GLSL_TYPE_UNKNOWN, NULL, 0};
            int index = safe_integer_value(subscript.data[0]);

            if (struct_path_length && value.member_count) {
                int written;

                if (!accept(parser, ']') || subscript.member_count || subscript.array_size ||
                    normalized_type(&subscript) != MESAGL_GLSL_TYPE_INT ||
                    subscript.rows != 1 || subscript.columns != 1 || index < 0) {
                    parser->failed = 1;
                    return selected;
                }
                written = snprintf(struct_path + struct_path_length,
                                   sizeof(struct_path) - struct_path_length,
                                   "[%d]", index);
                if (written < 0 ||
                    written >= (int)(sizeof(struct_path) - struct_path_length)) {
                    parser->failed = 1;
                    return selected;
                }
                struct_path_length += (size_t)written;
                continue;
            }

            if (!accept(parser, ']') || subscript.member_count || subscript.array_size ||
                normalized_type(&subscript) != MESAGL_GLSL_TYPE_INT ||
                subscript.rows != 1 || subscript.columns != 1 || index < 0 ||
                (!value.array_size && value.rows <= 1 && value.columns <= 1) ||
                index >= (value.array_size ? value.array_size
                                           : value.columns > 1 ? value.columns : value.rows)) {
                parser->failed = 1;
                return selected;
            }
            if (value.array_size) {
                selected = value.array[index];
            } else if (value.columns > 1) {
                int row;

                selected.rows = value.rows;
                selected.columns = 1;
                selected.type = value.type;
                for (row = 0; row < value.rows; ++row) {
                    selected.data[row] = value.data[index * value.rows + row];
                    selected.dfdx[row] = value.dfdx[index * value.rows + row];
                    selected.dfdy[row] = value.dfdy[index * value.rows + row];
                }
                selected.has_derivatives = value.has_derivatives;
            } else {
                selected.data[0] = value.data[index];
                selected.type = value.type;
                selected.dfdx[0] = value.dfdx[index];
                selected.dfdy[0] = value.dfdy[index];
                selected.has_derivatives = value.has_derivatives;
            }
            value = selected;
        } else
            return value;
    }
}

static MesaGLSLValue primary(Parser *parser)
{
    MesaGLSLValue result = {
        {0}, 1, 1, NULL, 0, NULL, NULL, 0, {0}, {0}, 0, MESAGL_GLSL_TYPE_UNKNOWN, NULL, 0};
    const char *name;
    size_t length;

    skip_space(parser);
    if (accept(parser, '(')) {
        const char *lvalue_start = parser->cursor - 1;
        const char *lvalue_end;

        result = expression(parser);
        if (!accept(parser, ')'))
            parser->failed = 1;
        result = postfix(parser, result);
        lvalue_end = parser->cursor;
        if (accept_text(parser, "++") || accept_text(parser, "--")) {
            int delta = parser->cursor[-1] == '+' ? 1 : -1;

            if (!parser->increment ||
                !parser->increment(parser->user, lvalue_start, lvalue_end,
                                   delta, 1, &result))
                parser->failed = 1;
        }
        return result;
    }
    if (parser->cursor < parser->end && ((*parser->cursor >= '0' && *parser->cursor <= '9') ||
                                         *parser->cursor == '.')) {
        const char *number_start = parser->cursor;
        char *number_end;
        const char *scan;
        int hexadecimal;
        int floating = 0;

        result = scalar(strtof(parser->cursor, &number_end));
        if (number_end == parser->cursor || number_end > parser->end)
            parser->failed = 1;
        else {
            parser->cursor = number_end;
            hexadecimal = number_end - number_start >= 2 && number_start[0] == '0' &&
                          (number_start[1] == 'x' || number_start[1] == 'X');
            for (scan = number_start; scan < number_end && !hexadecimal; ++scan)
                if (*scan == '.' || *scan == 'e' || *scan == 'E')
                    floating = 1;
            if (!floating) {
                char *integer_end;
                unsigned long long integer = strtoull(number_start, &integer_end, 0);

                if (integer_end != number_end) {
                    parser->failed = 1;
                    return result;
                }
                result.data[0] = (float)integer;
                result.type = MESAGL_GLSL_TYPE_INT;
            } else {
                result.type = MESAGL_GLSL_TYPE_FLOAT;
            }
        }
        return postfix(parser, result);
    }
    if (!identifier(parser, &name, &length)) {
        parser->failed = 1;
        return result;
    }
    if (name_is(name, length, "true") || name_is(name, length, "false")) {
        result = scalar(name_is(name, length, "true") ? 1.0f : 0.0f);
        result.type = MESAGL_GLSL_TYPE_BOOL;
        return postfix(parser, result);
    }
    skip_space(parser);
    if (parser->cursor < parser->end && *parser->cursor == '(')
        result = construct(parser, name, length);
    else {
        const char *lvalue_start = name;
        const char *lvalue_end;

        if (!parser->lookup || !parser->lookup(parser->user, name, length, &result)) {
            parser->failed = 1;
            return result;
        }
        result = postfix(parser, result);
        lvalue_end = parser->cursor;
        if (accept_text(parser, "++") || accept_text(parser, "--")) {
            int delta = parser->cursor[-1] == '+' ? 1 : -1;

            if (!parser->increment ||
                !parser->increment(parser->user, lvalue_start, lvalue_end,
                                   delta, 1, &result))
                parser->failed = 1;
        }
        return result;
    }
    return postfix(parser, result);
}

static MesaGLSLValue unary(Parser *parser)
{
    MesaGLSLValue result;
    int i;

    if (accept_text(parser, "++") || accept_text(parser, "--")) {
        int delta = parser->cursor[-1] == '+' ? 1 : -1;
        const char *start = parser->cursor;

        result = primary(parser);
        if (!parser->increment ||
            !parser->increment(parser->user, start, parser->cursor,
                               delta, 0, &result))
            parser->failed = 1;
        return result;
    }

    if (accept(parser, '+')) {
        result = unary(parser);
        if ((result.type != MESAGL_GLSL_TYPE_UNKNOWN &&
             result.type != MESAGL_GLSL_TYPE_FLOAT &&
             result.type != MESAGL_GLSL_TYPE_INT) || result.member_count)
            parser->failed = 1;
        return result;
    }
    if (accept(parser, '!')) {
        result = unary(parser);
        if (result.type != MESAGL_GLSL_TYPE_BOOL || result.rows != 1 ||
            result.columns != 1 || result.member_count)
            parser->failed = 1;
        return boolean(result.data[0] == 0.0f ? 1.0f : 0.0f);
    }
    if (!accept(parser, '-'))
        return primary(parser);
    result = unary(parser);
    if ((result.type != MESAGL_GLSL_TYPE_UNKNOWN &&
         result.type != MESAGL_GLSL_TYPE_FLOAT && result.type != MESAGL_GLSL_TYPE_INT) ||
        result.member_count) {
        parser->failed = 1;
        return result;
    }
    for (i = 0; i < components(&result); ++i) {
        result.data[i] = -result.data[i];
        result.dfdx[i] = -result.dfdx[i];
        result.dfdy[i] = -result.dfdy[i];
    }
    return result;
}

static MesaGLSLValue binary(Parser *parser, MesaGLSLValue left, MesaGLSLValue right, char op)
{
    MesaGLSLValue result = {
        {0}, 1, 1, NULL, 0, NULL, NULL, 0, {0}, {0}, 0, MESAGL_GLSL_TYPE_UNKNOWN, NULL, 0};
    int left_count = components(&left), right_count = components(&right);
    int count = left_count > right_count ? left_count : right_count;
    int i;

    if (left.member_count || right.member_count ||
        (left.type != MESAGL_GLSL_TYPE_UNKNOWN && left.type != MESAGL_GLSL_TYPE_FLOAT &&
         left.type != MESAGL_GLSL_TYPE_INT) ||
        (right.type != MESAGL_GLSL_TYPE_UNKNOWN && right.type != MESAGL_GLSL_TYPE_FLOAT &&
         right.type != MESAGL_GLSL_TYPE_INT)) {
        parser->failed = 1;
        return result;
    }

    if (left.type != MESAGL_GLSL_TYPE_UNKNOWN && right.type != MESAGL_GLSL_TYPE_UNKNOWN &&
        left.type != right.type) {
        parser->failed = 1;
        return result;
    }
    result.type = left.type != MESAGL_GLSL_TYPE_UNKNOWN ? left.type : right.type;

    if (op == '*' && left.columns > 1 && right.columns > 1 &&
        left.columns == right.rows) {
        int column;
        int row;

        result.rows = left.rows;
        result.columns = right.columns;
        for (column = 0; column < right.columns; ++column)
            for (row = 0; row < left.rows; ++row) {
                int destination = column * result.rows + row;
                int k;

                for (k = 0; k < left.columns; ++k) {
                    int left_index = k * left.rows + row;
                    int right_index = column * right.rows + k;

                    result.data[destination] +=
                        left.data[left_index] * right.data[right_index];
                    result.dfdx[destination] +=
                        left.dfdx[left_index] * right.data[right_index] +
                        left.data[left_index] * right.dfdx[right_index];
                    result.dfdy[destination] +=
                        left.dfdy[left_index] * right.data[right_index] +
                        left.data[left_index] * right.dfdy[right_index];
                }
            }
        result.has_derivatives = left.has_derivatives || right.has_derivatives;
        return result;
    }
    if (op == '*' && left.columns > 1 && right.columns == 1 && left.columns == right.rows) {
        result.rows = left.rows;
        for (i = 0; i < left.rows; ++i) {
            int k;

            for (k = 0; k < left.columns; ++k) {
                result.data[i] += left.data[k * left.rows + i] * right.data[k];
                result.dfdx[i] += left.dfdx[k * left.rows + i] * right.data[k] +
                                  left.data[k * left.rows + i] * right.dfdx[k];
                result.dfdy[i] += left.dfdy[k * left.rows + i] * right.data[k] +
                                  left.data[k * left.rows + i] * right.dfdy[k];
            }
        }
        result.has_derivatives = left.has_derivatives || right.has_derivatives;
        return result;
    }
    if (op == '*' && left.columns == 1 && right.columns > 1 && left.rows == right.rows) {
        int column;

        result.rows = right.columns;
        for (column = 0; column < right.columns; ++column) {
            int row;

            for (row = 0; row < right.rows; ++row) {
                int right_index = column * right.rows + row;

                result.data[column] += left.data[row] * right.data[right_index];
                result.dfdx[column] += left.dfdx[row] * right.data[right_index] +
                                      left.data[row] * right.dfdx[right_index];
                result.dfdy[column] += left.dfdy[row] * right.data[right_index] +
                                      left.data[row] * right.dfdy[right_index];
            }
        }
        result.has_derivatives = left.has_derivatives || right.has_derivatives;
        return result;
    }
    if (left.columns > 1 || right.columns > 1) {
        int left_scalar = left_count == 1;
        int right_scalar = right_count == 1;

        if ((left.columns > 1 && right.columns > 1 &&
             (left.rows != right.rows || left.columns != right.columns ||
              (op != '+' && op != '-' && op != '/'))) ||
            (!left_scalar && !right_scalar &&
             !(left.columns > 1 && right.columns > 1))) {
            parser->failed = 1;
            return result;
        }
        result.rows = left.columns > 1 ? left.rows : right.rows;
        result.columns = left.columns > 1 ? left.columns : right.columns;
    }
    if (left_count != right_count && left_count != 1 && right_count != 1) {
        parser->failed = 1;
        return result;
    }
    if (result.columns == 1)
        result.rows = count;
    for (i = 0; i < count; ++i) {
        float a = component(&left, i), b = component(&right, i);
        int left_index = left_count == 1 ? 0 : i;
        int right_index = right_count == 1 ? 0 : i;
        float adx = left.dfdx[left_index], ady = left.dfdy[left_index];
        float bdx = right.dfdx[right_index], bdy = right.dfdy[right_index];

        if (op == '/' && result.type == MESAGL_GLSL_TYPE_INT) {
            long divisor = (long)(int)b;

            if (!divisor) {
                parser->failed = 1;
                return result;
            }
            result.data[i] = (float)((long)(int)a / divisor);
            result.dfdx[i] = 0.0f;
            result.dfdy[i] = 0.0f;
            continue;
        }
        result.data[i] = op == '+' ? a + b : op == '-' ? a - b : op == '*' ? a * b : a / b;
        result.dfdx[i] = op == '+'   ? adx + bdx
                           : op == '-' ? adx - bdx
                           : op == '*' ? adx * b + a * bdx
                                       : (adx * b - a * bdx) / (b * b);
        result.dfdy[i] = op == '+'   ? ady + bdy
                           : op == '-' ? ady - bdy
                           : op == '*' ? ady * b + a * bdy
                                       : (ady * b - a * bdy) / (b * b);
    }
    result.has_derivatives = left.has_derivatives || right.has_derivatives;
    return result;
}

static MesaGLSLValue product(Parser *parser)
{
    MesaGLSLValue result = unary(parser);

    for (;;) {
        skip_space(parser);
        if (parser->cursor + 1 < parser->end && parser->cursor[1] == '=' &&
            (*parser->cursor == '*' || *parser->cursor == '/'))
            return result;
        if (accept(parser, '*'))
            result = binary(parser, result, unary(parser), '*');
        else if (accept(parser, '/'))
            result = binary(parser, result, unary(parser), '/');
        else
            return result;
    }
}

static MesaGLSLValue sum(Parser *parser)
{
    MesaGLSLValue result = product(parser);

    for (;;) {
        skip_space(parser);
        if (parser->cursor + 1 < parser->end && parser->cursor[1] == '=' &&
            (*parser->cursor == '+' || *parser->cursor == '-'))
            return result;
        if (accept(parser, '+'))
            result = binary(parser, result, product(parser), '+');
        else if (accept(parser, '-'))
            result = binary(parser, result, product(parser), '-');
        else
            return result;
    }
}

static MesaGLSLValue comparison(Parser *parser)
{
    MesaGLSLValue result = sum(parser);

    for (;;) {
        MesaGLSLValue right;
        int operation = 0;

        if (accept_text(parser, "<="))
            operation = 1;
        else if (accept_text(parser, ">="))
            operation = 2;
        else if (accept(parser, '<'))
            operation = 3;
        else if (accept(parser, '>'))
            operation = 4;
        else
            return result;
        right = sum(parser);
        if (result.member_count || right.member_count || result.rows != 1 ||
            result.columns != 1 || right.rows != 1 || right.columns != 1 ||
            (result.type != MESAGL_GLSL_TYPE_UNKNOWN &&
             result.type != MESAGL_GLSL_TYPE_FLOAT && result.type != MESAGL_GLSL_TYPE_INT) ||
            (right.type != MESAGL_GLSL_TYPE_UNKNOWN &&
             right.type != MESAGL_GLSL_TYPE_FLOAT && right.type != MESAGL_GLSL_TYPE_INT) ||
            (result.type == MESAGL_GLSL_TYPE_UNKNOWN ? MESAGL_GLSL_TYPE_FLOAT
                                                     : result.type) !=
                (right.type == MESAGL_GLSL_TYPE_UNKNOWN ? MESAGL_GLSL_TYPE_FLOAT
                                                        : right.type)) {
            parser->failed = 1;
            return result;
        }
        result = boolean(operation == 1   ? result.data[0] <= right.data[0]
                        : operation == 2 ? result.data[0] >= right.data[0]
                        : operation == 3 ? result.data[0] < right.data[0]
                                         : result.data[0] > right.data[0]);
    }
}

static int equal_values(const MesaGLSLValue *left, const MesaGLSLValue *right,
                        int *matches)
{
    MesaGLSLType left_type = normalized_type(left);
    MesaGLSLType right_type = normalized_type(right);
    int item;

    if (left->array_size || right->array_size || left_type != right_type ||
        left->rows != right->rows || left->columns != right->columns ||
        left->member_count != right->member_count ||
        left_type == MESAGL_GLSL_TYPE_SAMPLER2D ||
        left_type == MESAGL_GLSL_TYPE_SAMPLERCUBE)
        return 0;
    if (left->member_count) {
        if (!same_struct_type(left->struct_type_name, left->struct_type_length,
                              right->struct_type_name, right->struct_type_length))
            return 0;
        for (item = 0; item < left->member_count; ++item)
            if (!equal_values(&left->members[item], &right->members[item], matches))
                return 0;
        return 1;
    }
    for (item = 0; item < components(left); ++item)
        if (left->data[item] != right->data[item])
            *matches = 0;
    return 1;
}

static MesaGLSLValue equality(Parser *parser)
{
    MesaGLSLValue result = comparison(parser);

    for (;;) {
        int equal;
        MesaGLSLValue right;

        if (accept_text(parser, "=="))
            equal = 1;
        else if (accept_text(parser, "!="))
            equal = 0;
        else
            return result;
        right = comparison(parser);
        {
            int matches = 1;

            if (!equal_values(&result, &right, &matches)) {
                parser->failed = 1;
                return result;
            }
            result = boolean(equal ? matches : !matches);
        }
    }
}

static void skip_logical_operand(Parser *parser, int stop_at_and)
{
    const char *cursor = parser->cursor;
    int parentheses = 0;
    int brackets = 0;

    while (cursor < parser->end) {
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
                   (*cursor == ',' || *cursor == '?' || *cursor == ':')) {
            break;
        } else if (!parentheses && !brackets && cursor + 1 < parser->end &&
                   ((!strncmp(cursor, "||", 2)) ||
                    (stop_at_and && (!strncmp(cursor, "&&", 2) ||
                                     !strncmp(cursor, "^^", 2))))) {
            break;
        }
        ++cursor;
    }
    parser->cursor = cursor;
}

static MesaGLSLValue logical_and(Parser *parser)
{
    MesaGLSLValue result = equality(parser);

    while (accept_text(parser, "&&")) {
        MesaGLSLValue right;

        if (!parser->suppress_side_effects && result.type == MESAGL_GLSL_TYPE_BOOL &&
            result.rows == 1 && result.columns == 1 && result.data[0] == 0.0f) {
            skip_logical_operand(parser, 1);
            result = boolean(0.0f);
            continue;
        }
        if (parser->suppress_side_effects && result.type == MESAGL_GLSL_TYPE_BOOL &&
            result.rows == 1 && result.columns == 1 && result.data[0] == 0.0f)
            parser->suppress_side_effects(parser->user, 1);
        right = equality(parser);
        if (parser->suppress_side_effects && result.type == MESAGL_GLSL_TYPE_BOOL &&
            result.rows == 1 && result.columns == 1 && result.data[0] == 0.0f)
            parser->suppress_side_effects(parser->user, 0);

        if (result.type != MESAGL_GLSL_TYPE_BOOL || right.type != MESAGL_GLSL_TYPE_BOOL ||
            result.rows != 1 || result.columns != 1 || right.rows != 1 ||
            right.columns != 1)
            parser->failed = 1;
        result = boolean(result.data[0] != 0.0f && right.data[0] != 0.0f);
    }
    return result;
}

static MesaGLSLValue logical_xor(Parser *parser)
{
    MesaGLSLValue result = logical_and(parser);

    while (accept_text(parser, "^^")) {
        MesaGLSLValue right = logical_and(parser);

        if (result.type != MESAGL_GLSL_TYPE_BOOL ||
            right.type != MESAGL_GLSL_TYPE_BOOL || result.rows != 1 ||
            result.columns != 1 || right.rows != 1 || right.columns != 1)
            parser->failed = 1;
        result = boolean((result.data[0] != 0.0f) != (right.data[0] != 0.0f));
    }
    return result;
}

static MesaGLSLValue logical_or(Parser *parser)
{
    MesaGLSLValue result = logical_xor(parser);

    while (accept_text(parser, "||")) {
        MesaGLSLValue right;

        if (!parser->suppress_side_effects && result.type == MESAGL_GLSL_TYPE_BOOL &&
            result.rows == 1 && result.columns == 1 && result.data[0] != 0.0f) {
            skip_logical_operand(parser, 0);
            result = boolean(1.0f);
            continue;
        }
        if (parser->suppress_side_effects && result.type == MESAGL_GLSL_TYPE_BOOL &&
            result.rows == 1 && result.columns == 1 && result.data[0] != 0.0f)
            parser->suppress_side_effects(parser->user, 1);
        right = logical_xor(parser);
        if (parser->suppress_side_effects && result.type == MESAGL_GLSL_TYPE_BOOL &&
            result.rows == 1 && result.columns == 1 && result.data[0] != 0.0f)
            parser->suppress_side_effects(parser->user, 0);

        if (result.type != MESAGL_GLSL_TYPE_BOOL ||
            right.type != MESAGL_GLSL_TYPE_BOOL || result.rows != 1 ||
            result.columns != 1 || right.rows != 1 ||
            right.columns != 1)
            parser->failed = 1;
        result = boolean(result.data[0] != 0.0f || right.data[0] != 0.0f);
    }
    return result;
}

static MesaGLSLValue conditional_expression(Parser *parser)
{
    MesaGLSLValue condition = logical_or(parser);

    if (accept(parser, '?')) {
        MesaGLSLValue yes;
        MesaGLSLValue no;

        if (condition.type != MESAGL_GLSL_TYPE_BOOL || condition.rows != 1 ||
            condition.columns != 1) {
            parser->failed = 1;
            return condition;
        }
        if (parser->suppress_side_effects && condition.data[0] == 0.0f)
            parser->suppress_side_effects(parser->user, 1);
        yes = expression(parser);
        if (parser->suppress_side_effects && condition.data[0] == 0.0f)
            parser->suppress_side_effects(parser->user, 0);

        if (!accept(parser, ':')) {
            parser->failed = 1;
            return condition;
        }
        if (parser->suppress_side_effects && condition.data[0] != 0.0f)
            parser->suppress_side_effects(parser->user, 1);
        no = assignment_expression(parser);
        if (parser->suppress_side_effects && condition.data[0] != 0.0f)
            parser->suppress_side_effects(parser->user, 0);
        {
            int same_value = 1;

            if (!equal_values(&yes, &no, &same_value)) {
                parser->failed = 1;
                return condition;
            }
        }
        return condition.data[0] != 0.0f ? yes : no;
    }
    return condition;
}

static MesaGLSLValue assignment_expression(Parser *parser)
{
    const char *start = parser->cursor;
    MesaGLSLValue left = conditional_expression(parser);
    const char *lvalue_end = parser->cursor;
    char operation = 0;

    if (accept_text(parser, "+="))
        operation = '+';
    else if (accept_text(parser, "-="))
        operation = '-';
    else if (accept_text(parser, "*="))
        operation = '*';
    else if (accept_text(parser, "/="))
        operation = '/';
    else {
        skip_space(parser);
        if (parser->cursor < parser->end && *parser->cursor == '=' &&
            (parser->cursor + 1 >= parser->end || parser->cursor[1] != '=')) {
            ++parser->cursor;
            operation = '=';
        }
    }
    if (operation) {
        MesaGLSLValue right = assignment_expression(parser);

        if (operation != '=')
            right = binary(parser, left, right, operation);
        if (!parser->assign_expression ||
            !parser->assign_expression(parser->user, start, lvalue_end, &right))
            parser->failed = 1;
        return right;
    }
    return left;
}

static MesaGLSLValue expression(Parser *parser)
{
    MesaGLSLValue result = assignment_expression(parser);

    while (accept(parser, ','))
        result = assignment_expression(parser);
    return result;
}

static int expression_internal(const char *source, const char *end,
                               MesaGLSLLookupFn lookup, MesaGLSLCallFn call,
                               void *user, MesaGLSLValue *result,
                               const char **error_at,
                               void (*suppress_side_effects)(void *, int),
                               int (*increment)(void *, const char *, const char *,
                                                int, int, MesaGLSLValue *),
                               int (*assign_expression)(void *, const char *, const char *,
                                                        const MesaGLSLValue *))
{
    Parser parser;

    if (!source || !result)
        return 0;
    parser.cursor = source;
    parser.end = end ? end : source + strlen(source);
    parser.lookup = lookup;
    parser.call = call;
    parser.user = user;
    parser.suppress_side_effects = suppress_side_effects;
    parser.increment = increment;
    parser.assign_expression = assign_expression;
    parser.failed = 0;
    *result = expression(&parser);
    skip_space(&parser);
    if (error_at)
        *error_at = parser.cursor;
    return !parser.failed && parser.cursor == parser.end;
}

int mesaGLSLExpression(const char *source, const char *end, MesaGLSLLookupFn lookup,
                       MesaGLSLCallFn call, void *user, MesaGLSLValue *result,
                       const char **error_at)
{
    return expression_internal(source, end, lookup, call, user, result, error_at,
                               NULL, NULL, NULL);
}

#define EXEC_MAX_LOCALS MESAGL_MAX_SHADER_LOCALS

typedef struct ExecLocal {
    char name[MESAGL_MAX_SHADER_IDENTIFIER_LENGTH];
    int is_const;
    MesaGLSLValue value;
    MesaGLSLValue *array;
    int array_size;
    char (*member_names)[MESAGL_MAX_SHADER_LVALUE_PATH_LENGTH];
    MesaGLSLValue *members;
    int member_count;
} ExecLocal;

typedef struct Executor {
    MesaGLSLLookupFn lookup;
    MesaGLSLCallFn call;
    MesaGLSLAssignFn assign;
    void *user;
    ExecLocal locals[EXEC_MAX_LOCALS];
    int local_count;
    int scope_base;
    int global_count;
    struct Executor *global_owner;
    MesaGLSLValue array_storage[MESAGL_MAX_SHADER_ARRAY_STORAGE];
    int array_storage_used;
    char struct_member_names[MESAGL_MAX_SHADER_STRUCT_STORAGE]
                            [MESAGL_MAX_SHADER_LVALUE_PATH_LENGTH];
    MesaGLSLValue struct_member_storage[MESAGL_MAX_SHADER_STRUCT_STORAGE];
    int struct_storage_used;
    int discarded;
    int returned;
    int breaking;
    int continuing;
    int loop_depth;
    int failed;
    const char *error_at;
    const char *function_source;
    int call_depth;
    int suppress_side_effects;
    int constant_expression_only;
    MesaGLSLValue return_value;
    int have_return_value;
} Executor;

static const char *exec_space(const char *cursor, const char *end)
{
    for (;;) {
        while (cursor < end && (*cursor == ' ' || *cursor == '\t' || *cursor == '\r' ||
                                *cursor == '\n'))
            ++cursor;
        if (end - cursor >= 2 && cursor[0] == '/' && cursor[1] == '/') {
            cursor += 2;
            while (cursor < end && *cursor != '\n')
                ++cursor;
        } else if (end - cursor >= 2 && cursor[0] == '/' && cursor[1] == '*') {
            cursor += 2;
            while (end - cursor >= 2 && !(cursor[0] == '*' && cursor[1] == '/'))
                ++cursor;
            if (end - cursor >= 2)
                cursor += 2;
        } else
            return cursor;
    }
}

static int exec_identifier(const char **cursor, const char *end, const char **name, size_t *length)
{
    const char *start = exec_space(*cursor, end);
    const char *current = start;

    if (current >= end || !((*current >= 'a' && *current <= 'z') ||
                            (*current >= 'A' && *current <= 'Z') || *current == '_'))
        return 0;
    ++current;
    while (current < end && ((*current >= 'a' && *current <= 'z') ||
                             (*current >= 'A' && *current <= 'Z') ||
                             (*current >= '0' && *current <= '9') || *current == '_'))
        ++current;
    *cursor = current;
    *name = start;
    *length = (size_t)(current - start);
    return 1;
}

static const char *matching(const char *open, const char *end, char left, char right)
{
    int depth = 0;
    const char *cursor;

    for (cursor = open; cursor < end; ++cursor) {
        if (*cursor == left)
            ++depth;
        else if (*cursor == right && !--depth)
            return cursor;
    }
    return NULL;
}

static const char *statement_end(const char *start, const char *end)
{
    const char *cursor = exec_space(start, end);
    int parentheses = 0;
    int braces = 0;

    {
        const char *after_keyword = cursor;
        const char *keyword;
        size_t keyword_length;

        if (exec_identifier(&after_keyword, end, &keyword, &keyword_length) &&
            name_is(keyword, keyword_length, "if")) {
            const char *open = exec_space(after_keyword, end);
            const char *close = open < end && *open == '(' ? matching(open, end, '(', ')') : NULL;
            const char *then_end;
            const char *after_then;

            if (!close)
                return end;
            then_end = statement_end(close + 1, end);
            after_then = exec_space(then_end, end);
            after_keyword = after_then;
            if (exec_identifier(&after_keyword, end, &keyword, &keyword_length) &&
                name_is(keyword, keyword_length, "else"))
                return statement_end(after_keyword, end);
            return then_end;
        }
    }

    if (cursor < end && *cursor == '{') {
        const char *close = matching(cursor, end, '{', '}');

        return close ? close + 1 : end;
    }
    for (; cursor < end; ++cursor) {
        if (*cursor == '(')
            ++parentheses;
        else if (*cursor == ')')
            --parentheses;
        else if (*cursor == '{')
            ++braces;
        else if (*cursor == '}')
            --braces;
        else if (*cursor == ';' && !parentheses && !braces)
            return cursor + 1;
    }
    return end;
}

static int exec_lookup(void *user, const char *name, size_t length, MesaGLSLValue *value)
{
    Executor *executor = (Executor *)user;
    int i;

    for (i = executor->local_count - 1; i >= 0; --i)
        if (strlen(executor->locals[i].name) == length &&
            !strncmp(executor->locals[i].name, name, length)) {
            if (executor->constant_expression_only &&
                !executor->locals[i].is_const)
                return 0;
            *value = executor->locals[i].value;
            if (executor->locals[i].array_size) {
                value->array = executor->locals[i].array;
                value->array_size = executor->locals[i].array_size;
            }
            if (executor->locals[i].member_count) {
                value->member_names = executor->locals[i].member_names;
                value->members = executor->locals[i].members;
                value->member_count = executor->locals[i].member_count;
            }
            return 1;
        }
    {
        static const struct {
            const char *name;
            int value;
        } constants[] = {
            {"gl_MaxVertexAttribs", MESAGL_MAX_VERTEX_ATTRIBS},
            {"gl_MaxVertexUniformVectors", MESAGL_MAX_VERTEX_UNIFORM_VECTORS},
            {"gl_MaxVaryingVectors", MESAGL_MAX_VARYING_VECTORS},
            {"gl_MaxVertexTextureImageUnits",
             MESAGL_MAX_VERTEX_TEXTURE_IMAGE_UNITS},
            {"gl_MaxCombinedTextureImageUnits",
             MESAGL_MAX_COMBINED_TEXTURE_IMAGE_UNITS},
            {"gl_MaxTextureImageUnits",
             MESAGL_MAX_FRAGMENT_TEXTURE_IMAGE_UNITS},
            {"gl_MaxFragmentUniformVectors", MESAGL_MAX_FRAGMENT_UNIFORM_VECTORS},
            {"gl_MaxDrawBuffers", 1},
        };
        size_t constant;

        for (constant = 0; constant < sizeof(constants) / sizeof(constants[0]);
             ++constant)
            if (strlen(constants[constant].name) == length &&
                !strncmp(constants[constant].name, name, length)) {
                memset(value, 0, sizeof(*value));
                value->data[0] = (float)constants[constant].value;
                value->rows = 1;
                value->columns = 1;
                value->type = MESAGL_GLSL_TYPE_INT;
                return 1;
            }
    }
    if (executor->global_owner && executor->global_owner != executor)
        for (i = executor->global_owner->global_count - 1; i >= 0; --i)
            if (strlen(executor->global_owner->locals[i].name) == length &&
                !strncmp(executor->global_owner->locals[i].name, name, length)) {
                    if (executor->constant_expression_only &&
                        !executor->global_owner->locals[i].is_const)
                        return 0;
                    *value = executor->global_owner->locals[i].value;
                    if (executor->global_owner->locals[i].array_size) {
                        value->array = executor->global_owner->locals[i].array;
                        value->array_size = executor->global_owner->locals[i].array_size;
                    }
                    if (executor->global_owner->locals[i].member_count) {
                        value->member_names = executor->global_owner->locals[i].member_names;
                        value->members = executor->global_owner->locals[i].members;
                        value->member_count = executor->global_owner->locals[i].member_count;
                    }
                    return 1;
                }
    if (executor->constant_expression_only)
        return 0;
    return executor->lookup && executor->lookup(executor->user, name, length, value);
}

static int custom_call(Executor *executor, const char *name, size_t length,
                       const MesaGLSLValue *arguments, int argument_count, MesaGLSLValue *value);

static int range_has_return_statement(const char *start, const char *end)
{
    const char *cursor = start;

    while ((cursor = exec_space(cursor, end)) < end) {
        const char *token;
        size_t token_length;

        if (exec_identifier(&cursor, end, &token, &token_length)) {
            if (name_is(token, token_length, "return"))
                return 1;
            continue;
        }
        ++cursor;
    }
    return 0;
}

static int exec_call(void *user, const char *name, size_t length,
                     const MesaGLSLValue *arguments, int argument_count, MesaGLSLValue *value)
{
    Executor *executor = (Executor *)user;

    if (executor->constant_expression_only)
        return custom_call(executor, name, length, arguments, argument_count,
                           value);
    if (executor->call &&
        executor->call(executor->user, name, length, arguments, argument_count, value))
        return 1;
    return custom_call(executor, name, length, arguments, argument_count, value);
}

static void exec_suppress_side_effects(void *user, int enable)
{
    Executor *executor = (Executor *)user;

    if (enable)
        ++executor->suppress_side_effects;
    else if (executor->suppress_side_effects > 0)
        --executor->suppress_side_effects;
}

static int suppressed_assign(void *user, const char *name, size_t length,
                             const char *swizzle, size_t swizzle_length,
                             int array_index, const MesaGLSLValue *value)
{
    (void)user;
    (void)name;
    (void)length;
    (void)swizzle;
    (void)swizzle_length;
    (void)array_index;
    (void)value;
    return 1;
}

static int exec_increment(void *user, const char *start, const char *end,
                          int delta, int postfix, MesaGLSLValue *value);

static int same_struct_type(const char *left_name, size_t left_length,
                            const char *right_name, size_t right_length)
{
    if (!left_name || !right_name || left_length != right_length)
        return 0;
    if (left_name[0] == '{' || right_name[0] == '{')
        return left_name == right_name;
    return !strncmp(left_name, right_name, left_length);
}

static int swizzle_channel(char channel)
{
    const char *found = strchr("xyzw", channel);

    if (!found)
        found = strchr("rgba", channel);
    if (!found)
        found = strchr("stpq", channel);
    return found ? (int)((found - (strchr("xyzw", channel)   ? "xyzw"
                                      : strchr("rgba", channel) ? "rgba"
                                                                 : "stpq")))
                 : -1;
}

static int swizzle_component_set(char channel)
{
    return strchr("xyzw", channel) ? 0
           : strchr("rgba", channel) ? 1
           : strchr("stpq", channel) ? 2
                                      : -1;
}

static int copy_value(MesaGLSLValue *destination, const MesaGLSLValue *source)
{
    int element;
    int member;

    if (destination->array_size) {
        if (source->array_size != destination->array_size || !destination->array ||
            !source->array)
            return 0;
        for (element = 0; element < destination->array_size; ++element)
            if (!copy_value(&((MesaGLSLValue *)destination->array)[element],
                            &source->array[element]))
                return 0;
        return 1;
    }
    if (source->array_size)
        return 0;
    if (!destination->member_count) {
        MesaGLSLType destination_type = destination->type == MESAGL_GLSL_TYPE_UNKNOWN
                                              ? MESAGL_GLSL_TYPE_FLOAT
                                              : destination->type;
        MesaGLSLType source_type = source->type == MESAGL_GLSL_TYPE_UNKNOWN
                                         ? MESAGL_GLSL_TYPE_FLOAT
                                         : source->type;

        if (source->member_count || destination_type != source_type ||
            destination->rows != source->rows || destination->columns != source->columns)
            return 0;
        *destination = *source;
        return 1;
    }
    if (source->member_count != destination->member_count ||
        !same_struct_type(source->struct_type_name,
                          source->struct_type_length,
                          destination->struct_type_name,
                          destination->struct_type_length))
        return 0;
    for (member = 0; member < destination->member_count; ++member)
        if (!copy_value(&((MesaGLSLValue *)destination->members)[member],
                        &source->members[member]))
            return 0;
    return 1;
}

static int assign_swizzle(MesaGLSLValue *destination, const char *swizzle,
                          size_t swizzle_length, const MesaGLSLValue *value)
{
    MesaGLSLType destination_type = destination->type == MESAGL_GLSL_TYPE_UNKNOWN
                                            ? MESAGL_GLSL_TYPE_FLOAT
                                            : destination->type;
    MesaGLSLType source_type = value->type == MESAGL_GLSL_TYPE_UNKNOWN
                                       ? MESAGL_GLSL_TYPE_FLOAT
                                       : value->type;
    size_t channel;
    int component_set = -1;

    if (value->member_count || destination_type != source_type || value->columns != 1 ||
        value->rows != (int)swizzle_length)
        return 0;
    for (channel = 0; channel < swizzle_length; ++channel) {
        size_t other;
        int destination_channel = swizzle_channel(swizzle[channel]);
        int selected_set = swizzle_component_set(swizzle[channel]);

        if (destination_channel < 0 || destination_channel >= components(destination) ||
            (component_set >= 0 && component_set != selected_set))
            return 0;
        component_set = selected_set;
        for (other = 0; other < channel; ++other)
            if (swizzle_channel(swizzle[other]) == destination_channel)
                return 0;
    }
    for (channel = 0; channel < swizzle_length; ++channel)
        destination->data[swizzle_channel(swizzle[channel])] = value->data[channel];
    return 1;
}

static int select_numeric_index(const MesaGLSLValue *source, int index,
                                MesaGLSLValue *selected)
{
    int row;

    memset(selected, 0, sizeof(*selected));
    selected->columns = 1;
    selected->type = source->type;
    if (source->columns > 1) {
        if (index < 0 || index >= source->columns)
            return 0;
        selected->rows = source->rows;
        for (row = 0; row < source->rows; ++row)
            selected->data[row] = source->data[index * source->rows + row];
        return 1;
    }
    if (index < 0 || index >= source->rows)
        return 0;
    selected->rows = 1;
    selected->data[0] = source->data[index];
    return 1;
}

static int assign_numeric_index(MesaGLSLValue *destination, int index,
                                const char *swizzle, size_t swizzle_length,
                                const MesaGLSLValue *value)
{
    MesaGLSLValue selected;
    int row;

    if (!select_numeric_index(destination, index, &selected))
        return 0;
    if (swizzle_length) {
        if (destination->columns == 1 ||
            !assign_swizzle(&selected, swizzle, swizzle_length, value))
            return 0;
    } else if (!copy_value(&selected, value)) {
        return 0;
    }
    if (destination->columns > 1)
        for (row = 0; row < destination->rows; ++row)
            destination->data[index * destination->rows + row] = selected.data[row];
    else
        destination->data[index] = selected.data[0];
    return 1;
}

static int select_index_chain(const MesaGLSLValue *source, const int *indices,
                              int index_count, MesaGLSLValue *selected)
{
    MesaGLSLValue current;

    if (index_count <= 0) {
        *selected = *source;
        return 1;
    }
    if (source->array_size) {
        if (!source->array || indices[0] < 0 || indices[0] >= source->array_size)
            return 0;
        return select_index_chain(&source->array[indices[0]], indices + 1,
                                  index_count - 1, selected);
    }
    if (!select_numeric_index(source, indices[0], &current))
        return 0;
    return select_index_chain(&current, indices + 1, index_count - 1, selected);
}

static int assign_index_chain(MesaGLSLValue *destination, const int *indices,
                              int index_count, const MesaGLSLValue *value)
{
    MesaGLSLValue selected;

    if (index_count <= 0)
        return copy_value(destination, value);
    if (destination->array_size) {
        if (!destination->array || indices[0] < 0 ||
            indices[0] >= destination->array_size)
            return 0;
        return assign_index_chain(&((MesaGLSLValue *)destination->array)[indices[0]],
                                  indices + 1, index_count - 1, value);
    }
    if (index_count == 1)
        return assign_numeric_index(destination, indices[0], NULL, 0, value);
    if (!select_numeric_index(destination, indices[0], &selected) ||
        !assign_index_chain(&selected, indices + 1, index_count - 1, value))
        return 0;
    return assign_numeric_index(destination, indices[0], NULL, 0, &selected);
}

static int assign_external_value(Executor *executor, const char *name,
                                 size_t name_length,
                                 const MesaGLSLValue *value)
{
    int element;

    if (!executor->assign)
        return 0;
    if (!value->array_size)
        return executor->assign(executor->user, name, name_length, NULL, 0,
                                -1, value);
    if (!value->array)
        return 0;
    for (element = 0; element < value->array_size; ++element)
        if (!executor->assign(executor->user, name, name_length, NULL, 0,
                              element, &value->array[element]))
            return 0;
    return 1;
}

static int exec_assign(Executor *executor, const char *name, size_t length, int array_index,
                       const char *swizzle, size_t swizzle_length, const MesaGLSLValue *value)
{
    int i;

    for (i = executor->local_count - 1; i >= 0; --i)
        if (strlen(executor->locals[i].name) == length &&
            !strncmp(executor->locals[i].name, name, length)) {
            MesaGLSLValue *destination_value = &executor->locals[i].value;

            if (executor->locals[i].is_const)
                return 0;
            if (array_index >= 0) {
                if (executor->locals[i].array_size) {
                    if (array_index >= executor->locals[i].array_size)
                        return 0;
                    destination_value = &executor->locals[i].array[array_index];
                } else if (destination_value->columns > 1) {
                    MesaGLSLValue column = {
                        {0}, destination_value->rows, 1, NULL, 0, NULL, NULL, 0,
                        {0}, {0}, 0, destination_value->type, NULL, 0};
                    int row;

                    if (array_index >= destination_value->columns)
                        return 0;
                    for (row = 0; row < destination_value->rows; ++row)
                        column.data[row] =
                            destination_value->data[array_index * destination_value->rows + row];
                    if (swizzle_length) {
                        if (!assign_swizzle(&column, swizzle, swizzle_length, value))
                            return 0;
                    } else if (!copy_value(&column, value)) {
                        return 0;
                    }
                    for (row = 0; row < destination_value->rows; ++row)
                        destination_value->data[array_index * destination_value->rows + row] =
                            column.data[row];
                    return 1;
                } else {
                    MesaGLSLValue scalar_value = {
                        {0}, 1, 1, NULL, 0, NULL, NULL, 0, {0}, {0}, 0,
                        destination_value->type, NULL, 0};

                    if (array_index >= destination_value->rows || swizzle_length)
                        return 0;
                    scalar_value.data[0] = destination_value->data[array_index];
                    if (!copy_value(&scalar_value, value))
                        return 0;
                    destination_value->data[array_index] = scalar_value.data[0];
                    return 1;
                }
            } else if (executor->locals[i].array_size)
                return 0;
            if (destination_value->member_count) {
                if (!swizzle_length) {
                    return copy_value(destination_value, value);
                }
                {
                    int member;

                    for (member = 0; member < destination_value->member_count; ++member)
                        if (strlen(destination_value->member_names[member]) == swizzle_length &&
                            !strncmp(destination_value->member_names[member], swizzle,
                                     swizzle_length))
                            break;
                    if (member == destination_value->member_count)
                        return 0;
                    return copy_value(&((MesaGLSLValue *)destination_value->members)[member],
                                      value);
                }
            }
            return !swizzle_length ? copy_value(destination_value, value)
                                   : assign_swizzle(destination_value, swizzle,
                                                    swizzle_length, value);
        }
    if (executor->global_owner && executor->global_owner != executor) {
        if (executor->suppress_side_effects)
            return 1;
        return exec_assign(executor->global_owner, name, length, array_index,
                           swizzle, swizzle_length, value);
    }
    return executor->assign &&
           executor->assign(executor->user, name, length, swizzle, swizzle_length,
                            array_index, value);
}

static int exec_assign_struct_member(Executor *executor, const char *name, size_t name_length,
                                     int array_index, const char *member_name, size_t member_length,
                                     const char *nested_name, size_t nested_length,
                                     const char *swizzle, size_t swizzle_length,
                                     const MesaGLSLValue *value)
{
    int local_index;

    for (local_index = executor->local_count - 1; local_index >= 0; --local_index) {
        ExecLocal *local = &executor->locals[local_index];
        MesaGLSLValue *structure;
        int member;

        if (strlen(local->name) != name_length || strncmp(local->name, name, name_length))
            continue;
        if (local->is_const)
            return 0;
        if (array_index >= 0) {
            if (array_index >= local->array_size)
                return 0;
            structure = &local->array[array_index];
        } else
            structure = &local->value;
        if (!structure->member_count)
            return 0;
        for (member = 0; member < structure->member_count; ++member)
            if (strlen(structure->member_names[member]) == member_length &&
                !strncmp(structure->member_names[member], member_name, member_length))
                break;
        if (member == structure->member_count)
            return 0;
        {
            MesaGLSLValue *destination_value = &((MesaGLSLValue *)structure->members)[member];
            if (nested_length) {
                if (!destination_value->member_count) {
                    if (swizzle_length)
                        return 0;
                    swizzle = nested_name;
                    swizzle_length = nested_length;
                    nested_length = 0;
                }
            }
            if (nested_length) {
                int nested_member;

                for (nested_member = 0; nested_member < destination_value->member_count;
                     ++nested_member)
                    if (strlen(destination_value->member_names[nested_member]) ==
                            nested_length &&
                        !strncmp(destination_value->member_names[nested_member], nested_name,
                                 nested_length))
                        break;
                if (nested_member == destination_value->member_count)
                    return 0;
                destination_value = &((MesaGLSLValue *)destination_value->members)[nested_member];
            }
            if (!swizzle_length) {
                return copy_value(destination_value, value);
            }
            if (destination_value->member_count)
                return 0;
            return assign_swizzle(destination_value, swizzle, swizzle_length, value);
        }
    }
    if (executor->global_owner && executor->global_owner != executor) {
        if (executor->suppress_side_effects)
            return 1;
        return exec_assign_struct_member(executor->global_owner, name, name_length,
                                         array_index, member_name, member_length,
                                         nested_name, nested_length, swizzle,
                                         swizzle_length, value);
    }
    return 0;
}

static int resolve_local_lvalue(Executor *executor, const char *name, size_t name_length,
                                int array_index, const char **path,
                                const size_t *path_length, const int *path_array_index,
                                int path_count,
                                MesaGLSLValue **destination, const char **swizzle,
                                size_t *swizzle_length, int *numeric_index)
{
    int local_index;
    int step;

    *swizzle = NULL;
    *swizzle_length = 0;
    *numeric_index = -1;
    for (local_index = executor->local_count - 1; local_index >= 0; --local_index) {
        ExecLocal *local = &executor->locals[local_index];
        MesaGLSLValue *value;

        if (strlen(local->name) != name_length || strncmp(local->name, name, name_length))
            continue;
        if (local->is_const)
            return -1;
        if (array_index >= 0) {
            if (array_index >= local->array_size)
                return -1;
            value = &local->array[array_index];
        } else {
            if (local->array_size)
                return -1;
            value = &local->value;
        }
        for (step = 0; step < path_count; ++step) {
            int member;

            if (!value->member_count) {
                if (step != path_count - 1)
                    return -1;
                *swizzle = path[step];
                *swizzle_length = path_length[step];
                break;
            }
            for (member = 0; member < value->member_count; ++member)
                if (strlen(value->member_names[member]) == path_length[step] &&
                    !strncmp(value->member_names[member], path[step], path_length[step]))
                    break;
            if (member == value->member_count)
                return -1;
            value = &((MesaGLSLValue *)value->members)[member];
            if (path_array_index[step] >= 0) {
                if (value->array) {
                    if (path_array_index[step] >= value->array_size)
                        return -1;
                    value = &((MesaGLSLValue *)value->array)[path_array_index[step]];
                } else {
                    MesaGLSLValue selected;

                    if (*numeric_index >= 0 ||
                        !select_numeric_index(value, path_array_index[step], &selected))
                        return -1;
                    *numeric_index = path_array_index[step];
                    if (value->columns == 1 && step != path_count - 1)
                        return -1;
                }
            } else if (value->array_size && path_array_index[step] != -2) {
                return -1;
            }
        }
        *destination = value;
        return 1;
    }
    if (executor->global_owner && executor->global_owner != executor) {
        return resolve_local_lvalue(executor->global_owner, name, name_length,
                                    array_index, path, path_length,
                                    path_array_index, path_count, destination,
                                    swizzle, swizzle_length, numeric_index);
    }
    return 0;
}

static int resolve_local_index_root(Executor *executor, const char *name,
                                    size_t name_length, MesaGLSLValue *array_root,
                                    MesaGLSLValue **destination)
{
    int local_index;

    for (local_index = executor->local_count - 1; local_index >= 0; --local_index) {
        ExecLocal *local = &executor->locals[local_index];

        if (strlen(local->name) != name_length ||
            strncmp(local->name, name, name_length))
            continue;
        if (local->is_const)
            return -1;
        if (!local->array_size) {
            *destination = &local->value;
            return 1;
        }
        *array_root = local->value;
        array_root->array = local->array;
        array_root->array_size = local->array_size;
        *destination = array_root;
        return 1;
    }
    if (executor->global_owner && executor->global_owner != executor)
        return resolve_local_index_root(executor->global_owner, name, name_length,
                                        array_root, destination);
    return 0;
}

static int resolve_indexed_terminal(Executor *executor, const char *name,
                                    size_t name_length, int array_index,
                                    const char **path, size_t *path_length,
                                    int *path_array_index, int path_count,
                                    int *indices, int *index_count,
                                    MesaGLSLValue **destination)
{
    const char *swizzle;
    size_t swizzle_length;
    int numeric_index;
    int saved_index;
    int resolved;

    if (path_count <= 0 || *index_count <= 0)
        return 0;
    saved_index = path_array_index[path_count - 1];
    path_array_index[path_count - 1] = -2;
    resolved = resolve_local_lvalue(executor, name, name_length, array_index,
                                    path, path_length, path_array_index, path_count,
                                    destination, &swizzle, &swizzle_length,
                                    &numeric_index);
    path_array_index[path_count - 1] = saved_index;
    if (resolved > 0 && !swizzle_length && numeric_index < 0)
        return 1;
    {
        char flattened_name[MESAGL_MAX_SHADER_LVALUE_PATH_LENGTH];
        const char *saved_path = path[path_count - 1];
        size_t saved_length = path_length[path_count - 1];
        int written;

        written = snprintf(flattened_name, sizeof(flattened_name), "%.*s[%d]",
                           (int)saved_length, saved_path, indices[0]);
        if (written < 0 || written >= (int)sizeof(flattened_name))
            return 0;
        path[path_count - 1] = flattened_name;
        path_length[path_count - 1] = (size_t)written;
        resolved = resolve_local_lvalue(executor, name, name_length, array_index,
                                        path, path_length, path_array_index,
                                        path_count, destination, &swizzle,
                                        &swizzle_length, &numeric_index);
        path[path_count - 1] = saved_path;
        path_length[path_count - 1] = saved_length;
        if (resolved <= 0 || swizzle_length || numeric_index >= 0)
            return 0;
    }
    memmove(indices, indices + 1, (size_t)(*index_count - 1) * sizeof(int));
    --*index_count;
    return 1;
}

static int value_type(const char *name, size_t length, MesaGLSLValue *value)
{
    memset(value, 0, sizeof(*value));
    value->columns = 1;
    if (name_is(name, length, "float") || name_is(name, length, "int") ||
        name_is(name, length, "bool"))
        value->rows = 1;
    else if (name_is(name, length, "vec2") || name_is(name, length, "ivec2") ||
             name_is(name, length, "bvec2"))
        value->rows = 2;
    else if (name_is(name, length, "vec3") || name_is(name, length, "ivec3") ||
             name_is(name, length, "bvec3"))
        value->rows = 3;
    else if (name_is(name, length, "vec4") || name_is(name, length, "ivec4") ||
             name_is(name, length, "bvec4"))
        value->rows = 4;
    else if (name_is(name, length, "mat2"))
        value->rows = value->columns = 2;
    else if (name_is(name, length, "mat3"))
        value->rows = value->columns = 3;
    else if (name_is(name, length, "mat4"))
        value->rows = value->columns = 4;
    else if (name_is(name, length, "sampler2D") || name_is(name, length, "samplerCube"))
        value->rows = 1;
    else
        return 0;
    value->type = name_is(name, length, "sampler2D") ? MESAGL_GLSL_TYPE_SAMPLER2D
                  : name_is(name, length, "samplerCube") ? MESAGL_GLSL_TYPE_SAMPLERCUBE
                  : name[0] == 'i' ? MESAGL_GLSL_TYPE_INT
                  : name[0] == 'b' ? MESAGL_GLSL_TYPE_BOOL
                                   : MESAGL_GLSL_TYPE_FLOAT;
    return 1;
}

static int structure_declaration_visible(const char *source,
                                         const char *declaration,
                                         const char *position)
{
    const char *declaration_scopes[64];
    const char *position_scopes[64];
    const char *cursor = source;
    int declaration_depth = 0;
    int position_depth = 0;
    int depth;

    while (cursor < position) {
        if (*cursor == '{') {
            if (position_depth >= 64)
                return 0;
            position_scopes[position_depth++] = cursor;
        } else if (*cursor == '}' && position_depth > 0) {
            --position_depth;
        }
        ++cursor;
    }
    cursor = source;
    while (cursor < declaration) {
        if (*cursor == '{') {
            if (declaration_depth >= 64)
                return 0;
            declaration_scopes[declaration_depth++] = cursor;
        } else if (*cursor == '}' && declaration_depth > 0) {
            --declaration_depth;
        }
        ++cursor;
    }
    if (declaration_depth > position_depth)
        return 0;
    for (depth = 0; depth < declaration_depth; ++depth)
        if (declaration_scopes[depth] != position_scopes[depth])
            return 0;
    return 1;
}

static int struct_definition(const Executor *executor, const char *type_name,
                             size_t type_length, const char **body_start,
                             const char **body_end)
{
    const char *source = executor->function_source;
    const char *end;
    const char *cursor;
    const char *selected_open = NULL;
    const char *selected_close = NULL;
    const char *position;

    if (!source)
        return 0;
    end = source + strlen(source);
    position = type_name >= source && type_name <= end ? type_name : end;
    cursor = source;
    while ((cursor = strstr(cursor, "struct")) != NULL) {
        const char *after = cursor + 6;
        const char *name;
        size_t length;
        const char *open;
        const char *close;

        if ((cursor > source && ((cursor[-1] >= 'a' && cursor[-1] <= 'z') ||
                                 (cursor[-1] >= 'A' && cursor[-1] <= 'Z') ||
                                 (cursor[-1] >= '0' && cursor[-1] <= '9') || cursor[-1] == '_')) ||
            !exec_identifier(&after, end, &name, &length)) {
            cursor += 6;
            continue;
        }
        open = exec_space(after, end);
        if (length != type_length || strncmp(name, type_name, type_length) ||
            open >= end || *open != '{' ||
            !(close = matching(open, end, '{', '}')) || close >= position ||
            !structure_declaration_visible(source, close + 1, position)) {
            cursor += 6;
            continue;
        }
        selected_open = open;
        selected_close = close;
        cursor = close + 1;
    }
    if (!selected_open)
        return 0;
    *body_start = selected_open + 1;
    *body_end = selected_close;
    return 1;
}

static int parameter_qualifier(const char *name, size_t length);
static int eval_range(Executor *executor, const char *start, const char *end,
                      MesaGLSLValue *value);
static int eval_constant_range(Executor *executor, const char *start,
                               const char *end, MesaGLSLValue *value);

static int initialize_struct_local(Executor *executor, const char *type_name,
                                   size_t type_length, ExecLocal *local);

static int inline_struct_specifier(const char **cursor, const char *end,
                                   const char **type_name, size_t *type_length,
                                   const char **body_start, const char **body_end)
{
    const char *scan = exec_space(*cursor, end);
    const char *candidate;
    size_t candidate_length;
    const char *open;
    const char *close;

    if (exec_identifier(&scan, end, &candidate, &candidate_length)) {
        open = exec_space(scan, end);
        *type_name = candidate;
        *type_length = candidate_length;
    } else {
        open = exec_space(*cursor, end);
        *type_name = open;
        *type_length = 0;
    }
    if (open >= end || *open != '{' ||
        !(close = matching(open, end, '{', '}')))
        return 0;
    if (!*type_length) {
        *type_name = open;
        *type_length = (size_t)(close + 1 - open);
    }
    *body_start = open + 1;
    *body_end = close;
    *cursor = exec_space(close + 1, end);
    return 1;
}

static int initialize_struct_local_body(Executor *executor, const char *type_name,
                                        size_t type_length, const char *cursor,
                                        const char *end, ExecLocal *local)
{
    const char *scan;
    int member_count = 0;

    scan = cursor;
    while ((scan = exec_space(scan, end)) < end) {
        const char *field_type;
        size_t field_type_length;
        const char *nested_start = NULL;
        const char *nested_end = NULL;

        if (!exec_identifier(&scan, end, &field_type, &field_type_length))
            return 0;
        while (parameter_qualifier(field_type, field_type_length))
            if (!exec_identifier(&scan, end, &field_type, &field_type_length))
                return 0;
        if (name_is(field_type, field_type_length, "struct") &&
            !inline_struct_specifier(&scan, end, &field_type,
                                     &field_type_length, &nested_start,
                                     &nested_end))
            return 0;
        (void)nested_start;
        (void)nested_end;
        for (;;) {
            const char *field_name;
            size_t field_name_length;

            if (member_count >= MESAGL_MAX_SHADER_STRUCT_MEMBERS ||
                !exec_identifier(&scan, end, &field_name, &field_name_length))
                return 0;
            (void)field_name;
            (void)field_name_length;
            scan = exec_space(scan, end);
            if (scan < end && *scan == '[') {
                const char *close = matching(scan, end, '[', ']');

                if (!close)
                    return 0;
                scan = exec_space(close + 1, end);
            }
            ++member_count;
            if (scan < end && *scan == ',') {
                scan = exec_space(scan + 1, end);
                continue;
            }
            if (scan >= end || *scan != ';')
                return 0;
            ++scan;
            break;
        }
    }
    if (!member_count || executor->struct_storage_used + member_count >
                             MESAGL_MAX_SHADER_STRUCT_STORAGE)
        return 0;
    local->member_names = executor->struct_member_names + executor->struct_storage_used;
    local->members = executor->struct_member_storage + executor->struct_storage_used;
    executor->struct_storage_used += member_count;
    while ((cursor = exec_space(cursor, end)) < end) {
        const char *field_type;
        size_t field_type_length;
        MesaGLSLValue element_type;
        const char *definition_start;
        const char *definition_end;
        const char *nested_start = NULL;
        const char *nested_end = NULL;
        int field_structure;

        if (!exec_identifier(&cursor, end, &field_type, &field_type_length))
            return 0;
        while (parameter_qualifier(field_type, field_type_length))
            if (!exec_identifier(&cursor, end, &field_type, &field_type_length))
                return 0;
        if (name_is(field_type, field_type_length, "struct") &&
            !inline_struct_specifier(&cursor, end, &field_type,
                                     &field_type_length, &nested_start,
                                     &nested_end))
            return 0;
        field_structure = nested_start ||
                          !value_type(field_type, field_type_length, &element_type);
        if (field_structure &&
            !nested_start &&
            !struct_definition(executor, field_type, field_type_length,
                               &definition_start, &definition_end))
            return 0;
        for (;;) {
            const char *field_name;
            size_t field_name_length;
            MesaGLSLValue declared = element_type;
            int field_array_size = 0;
            int member = local->member_count;
            int previous_member;

            if (!exec_identifier(&cursor, end, &field_name, &field_name_length))
                return 0;
            for (previous_member = 0; previous_member < local->member_count;
                 ++previous_member)
                if (strlen(local->member_names[previous_member]) == field_name_length &&
                    !strncmp(local->member_names[previous_member], field_name,
                             field_name_length))
                    return 0;
            cursor = exec_space(cursor, end);
            if (cursor < end && *cursor == '[') {
                const char *close = matching(cursor, end, '[', ']');
                MesaGLSLValue size;

                if (!close || !eval_constant_range(executor, cursor + 1, close, &size) ||
                    size.rows != 1 || size.columns != 1 ||
                    normalized_type(&size) != MESAGL_GLSL_TYPE_INT ||
                    size.data[0] < 1.0f ||
                    size.data[0] > MESAGL_MAX_SHADER_ARRAY_ELEMENTS ||
                    size.data[0] != (float)safe_integer_value(size.data[0]))
                    return 0;
                field_array_size = safe_integer_value(size.data[0]);
                cursor = exec_space(close + 1, end);
            }
            if (field_array_size) {
                MesaGLSLValue *storage;
                int element;

                if (executor->array_storage_used + field_array_size >
                    MESAGL_MAX_SHADER_ARRAY_STORAGE)
                    return 0;
                storage = executor->array_storage + executor->array_storage_used;
                executor->array_storage_used += field_array_size;
                for (element = 0; element < field_array_size; ++element) {
                    if (field_structure) {
                        ExecLocal nested;

                        memset(&nested, 0, sizeof(nested));
                        if (!(nested_start
                                  ? initialize_struct_local_body(
                                        executor, field_type, field_type_length,
                                        nested_start, nested_end, &nested)
                                  : initialize_struct_local(
                                        executor, field_type,
                                        field_type_length, &nested)))
                            return 0;
                        storage[element] = nested.value;
                    } else {
                        storage[element] = element_type;
                    }
                }
                memset(&declared, 0, sizeof(declared));
                declared.array = storage;
                declared.array_size = field_array_size;
                declared.type = field_structure ? MESAGL_GLSL_TYPE_STRUCT
                                                 : storage[0].type;
                declared.rows = storage[0].rows;
                declared.columns = storage[0].columns;
            } else if (field_structure) {
                ExecLocal nested;

                memset(&nested, 0, sizeof(nested));
                if (!(nested_start
                          ? initialize_struct_local_body(
                                executor, field_type, field_type_length,
                                nested_start, nested_end, &nested)
                          : initialize_struct_local(executor, field_type,
                                                    field_type_length, &nested)))
                    return 0;
                declared = nested.value;
            }
            memset(local->member_names[member], 0,
                   sizeof(local->member_names[member]));
            memcpy(local->member_names[member], field_name,
                   field_name_length < sizeof(local->member_names[member]) - 1
                       ? field_name_length
                       : sizeof(local->member_names[member]) - 1);
            local->members[member] = declared;
            ++local->member_count;
            if (cursor < end && *cursor == ',') {
                cursor = exec_space(cursor + 1, end);
                continue;
            }
            if (cursor >= end || *cursor != ';')
                return 0;
            ++cursor;
            break;
        }
    }
    local->value.member_names = local->member_names;
    local->value.members = local->members;
    local->value.member_count = local->member_count;
    local->value.type = MESAGL_GLSL_TYPE_STRUCT;
    local->value.struct_type_name = type_name;
    local->value.struct_type_length = type_length;
    return local->member_count > 0;
}

static int initialize_struct_local(Executor *executor, const char *type_name,
                                   size_t type_length, ExecLocal *local)
{
    const char *cursor;
    const char *end;

    if (!struct_definition(executor, type_name, type_length, &cursor, &end))
        return 0;
    return initialize_struct_local_body(executor, type_name, type_length,
                                        cursor, end, local);
}

static int exec_assign_expression(void *user, const char *start, const char *end,
                                  const MesaGLSLValue *value)
{
    Executor *executor = (Executor *)user;
    const char *cursor = start;
    const char *name;
    size_t name_length;
    const char *path[16];
    size_t path_length[16];
    int path_array_index[16];
    int path_extra_index[16][2];
    int path_extra_index_count[16];
    int path_count = 0;
    int array_index = -1;
    int base_indices[3];
    int base_index_count = 0;
    MesaGLSLValue *destination;
    const char *swizzle;
    size_t swizzle_length;
    int numeric_index;
    int resolved;

    start = exec_space(start, end);
    while (end > start && (end[-1] == ' ' || end[-1] == '\t' ||
                           end[-1] == '\r' || end[-1] == '\n'))
        --end;
    if (start < end && *start == '(') {
        size_t length = (size_t)(end - start);
        char *normalized;
        int result;

        if (length == SIZE_MAX || !(normalized = (char *)ntglAlloc(length + 1)))
            return 0;
        memcpy(normalized, start, length);
        normalized[length] = '\0';
        for (;;) {
            const char *normalized_end = normalized + length;
            const char *open = exec_space(normalized, normalized_end);
            const char *close;
            size_t open_offset;
            size_t close_offset;

            if (open >= normalized_end || *open != '(')
                break;
            close = matching(open, normalized_end, '(', ')');
            if (!close) {
                ntglFree(normalized);
                return 0;
            }
            open_offset = (size_t)(open - normalized);
            close_offset = (size_t)(close - normalized);
            memmove(normalized + close_offset, normalized + close_offset + 1,
                    length - close_offset);
            --length;
            memmove(normalized + open_offset, normalized + open_offset + 1,
                    length - open_offset);
            --length;
        }
        result = exec_assign_expression(executor, normalized,
                                        normalized + length, value);
        ntglFree(normalized);
        return result;
    }
    cursor = start;
    if (!exec_identifier(&cursor, end, &name, &name_length))
        return 0;
    cursor = exec_space(cursor, end);
    while (cursor < end && *cursor == '[') {
        const char *close = matching(cursor, end, '[', ']');
        MesaGLSLValue subscript;

        if (base_index_count >= 3 || !close ||
            !eval_range(executor, cursor + 1, close, &subscript) ||
            subscript.rows != 1 || subscript.columns != 1 ||
            normalized_type(&subscript) != MESAGL_GLSL_TYPE_INT)
            return 0;
        base_indices[base_index_count++] = safe_integer_value(subscript.data[0]);
        cursor = exec_space(close + 1, end);
    }
    if (base_index_count == 1)
        array_index = base_indices[0];
    while (cursor < end && *cursor == '.') {
        const char *component;
        size_t component_length;

        ++cursor;
        if (path_count >= 16 ||
            !exec_identifier(&cursor, end, &component, &component_length))
            return 0;
        path[path_count] = component;
        path_length[path_count] = component_length;
        path_array_index[path_count] = -1;
        path_extra_index_count[path_count] = 0;
        cursor = exec_space(cursor, end);
        while (cursor < end && *cursor == '[') {
            const char *close = matching(cursor, end, '[', ']');
            MesaGLSLValue subscript;

            if (path_extra_index_count[path_count] >= 2 || !close ||
                !eval_range(executor, cursor + 1, close, &subscript) ||
                subscript.rows != 1 || subscript.columns != 1 ||
                normalized_type(&subscript) != MESAGL_GLSL_TYPE_INT)
                return 0;
            if (path_array_index[path_count] < 0)
                path_array_index[path_count] = safe_integer_value(subscript.data[0]);
            else
                path_extra_index[path_count]
                                [path_extra_index_count[path_count]++] =
                    safe_integer_value(subscript.data[0]);
            cursor = exec_space(close + 1, end);
        }
        ++path_count;
    }
    if (exec_space(cursor, end) != end)
        return 0;
    if (path_count > 0 && path_extra_index_count[path_count - 1] > 0) {
        MesaGLSLValue array_root;
        int terminal_indices[3];
        int terminal_index_count = 1 + path_extra_index_count[path_count - 1];

        terminal_indices[0] = path_array_index[path_count - 1];
        memcpy(terminal_indices + 1, path_extra_index[path_count - 1],
               (size_t)path_extra_index_count[path_count - 1] * sizeof(int));
        if (!resolve_indexed_terminal(executor, name, name_length, array_index,
                                      path, path_length, path_array_index,
                                      path_count, terminal_indices,
                                      &terminal_index_count, &destination))
            return 0;
        array_root = *destination;
        if (executor->suppress_side_effects)
            return assign_index_chain(&array_root, terminal_indices,
                                      terminal_index_count, value);
        return assign_index_chain(destination, terminal_indices,
                                  terminal_index_count, value);
    }
    if (base_index_count > 1 || (base_index_count == 1 && path_count == 0)) {
        MesaGLSLValue array_root;

        if (path_count)
            return 0;
        resolved = resolve_local_index_root(executor, name, name_length,
                                            &array_root, &destination);
        if (resolved < 0)
            return 0;
        if (!resolved) {
            MesaGLSLValue external;

            if (!exec_lookup(executor, name, name_length, &external) ||
                !assign_index_chain(&external, base_indices,
                                    base_index_count, value))
                return 0;
            return executor->suppress_side_effects ||
                   assign_external_value(executor, name, name_length,
                                         &external);
        }
        if (executor->suppress_side_effects) {
            MesaGLSLValue temporary = *destination;

            return assign_index_chain(&temporary, base_indices,
                                      base_index_count, value);
        }
        return assign_index_chain(destination, base_indices, base_index_count,
                                  value);
    }
    resolved = resolve_local_lvalue(executor, name, name_length, array_index,
                                    path, path_length, path_array_index, path_count,
                                    &destination, &swizzle, &swizzle_length,
                                    &numeric_index);
    if (resolved < 0)
        return 0;
    if (!resolved) {
        if (path_count > 1 || (path_count == 1 && path_array_index[0] >= 0))
            return 0;
        return executor->suppress_side_effects
                   ? suppressed_assign(executor->user, name, name_length,
                                       path_count ? path[0] : NULL,
                                       path_count ? path_length[0] : 0,
                                       array_index, value)
                   : exec_assign(executor, name, name_length, array_index,
                                 path_count ? path[0] : NULL,
                                 path_count ? path_length[0] : 0, value);
    }
    if (executor->suppress_side_effects) {
        MesaGLSLValue temporary = *destination;

        return numeric_index >= 0
                   ? assign_numeric_index(&temporary, numeric_index, swizzle,
                                          swizzle_length, value)
                   : swizzle_length
                         ? assign_swizzle(&temporary, swizzle, swizzle_length, value)
                         : copy_value(&temporary, value);
    }
    return numeric_index >= 0
               ? assign_numeric_index(destination, numeric_index, swizzle,
                                      swizzle_length, value)
               : swizzle_length
                     ? assign_swizzle(destination, swizzle, swizzle_length, value)
                     : copy_value(destination, value);
}

static int eval_range(Executor *executor, const char *start, const char *end, MesaGLSLValue *value)
{
    const char *error_at = start;

    if (!expression_internal(start, end, exec_lookup, exec_call, executor, value,
                             &error_at, exec_suppress_side_effects, exec_increment,
                             exec_assign_expression)) {
        executor->failed = 1;
        executor->error_at = error_at;
        return 0;
    }
    return 1;
}

static int eval_constant_range(Executor *executor, const char *start,
                               const char *end, MesaGLSLValue *value)
{
    int previous = executor->constant_expression_only;
    int result;

    executor->constant_expression_only = 1;
    result = eval_range(executor, start, end, value);
    executor->constant_expression_only = previous;
    return result;
}

static int value_contains_sampler(const MesaGLSLValue *value)
{
    int index;

    if (value->type == MESAGL_GLSL_TYPE_SAMPLER2D ||
        value->type == MESAGL_GLSL_TYPE_SAMPLERCUBE)
        return 1;
    for (index = 0; index < value->array_size; ++index)
        if (value_contains_sampler(&value->array[index]))
            return 1;
    for (index = 0; index < value->member_count; ++index)
        if (value_contains_sampler(&value->members[index]))
            return 1;
    return 0;
}

static int execute_local_declarator(Executor *executor, const char *type_name,
                                    size_t type_length, const MesaGLSLValue *declared,
                                    int structure, int declaration_const,
                                    const char *structure_start,
                                    const char *structure_end,
                                    const char *start, const char *end)
{
    const char *cursor = start;
    const char *variable;
    size_t variable_length;
    ExecLocal *local;
    int initialized = 0;
    int existing;

    if (!structure &&
        (declared->type == MESAGL_GLSL_TYPE_SAMPLER2D ||
         declared->type == MESAGL_GLSL_TYPE_SAMPLERCUBE)) {
        executor->failed = 1;
        executor->error_at = start;
        return 0;
    }

    if (!exec_identifier(&cursor, end, &variable, &variable_length) ||
        executor->local_count >= EXEC_MAX_LOCALS) {
        executor->failed = 1;
        executor->error_at = cursor;
        return 0;
    }
    for (existing = executor->scope_base; existing < executor->local_count; ++existing)
        if (strlen(executor->locals[existing].name) == variable_length &&
            !strncmp(executor->locals[existing].name, variable, variable_length)) {
            executor->failed = 1;
            executor->error_at = variable;
            return 0;
        }
    local = &executor->locals[executor->local_count++];
    memset(local, 0, sizeof(*local));
    local->is_const = declaration_const;
    memcpy(local->name, variable,
           variable_length < sizeof(local->name) - 1 ? variable_length
                                                     : sizeof(local->name) - 1);
    if (!structure)
        local->value = *declared;
    cursor = exec_space(cursor, end);
    if (cursor < end && *cursor == '[') {
        const char *close = matching(cursor, end, '[', ']');
        MesaGLSLValue array_size;
        int element;

        if (!close || !eval_constant_range(executor, cursor + 1, close, &array_size) ||
            array_size.member_count || array_size.array_size ||
            array_size.rows != 1 || array_size.columns != 1 ||
            normalized_type(&array_size) != MESAGL_GLSL_TYPE_INT ||
            array_size.data[0] < 1.0f ||
            array_size.data[0] > MESAGL_MAX_SHADER_ARRAY_ELEMENTS ||
            array_size.data[0] != (float)safe_integer_value(array_size.data[0]) ||
            executor->array_storage_used + safe_integer_value(array_size.data[0]) >
                MESAGL_MAX_SHADER_ARRAY_STORAGE) {
            executor->failed = 1;
            executor->error_at = cursor;
            return 0;
        }
        local->array_size = safe_integer_value(array_size.data[0]);
        local->array = executor->array_storage + executor->array_storage_used;
        executor->array_storage_used += local->array_size;
        for (element = 0; element < local->array_size; ++element) {
            if (structure) {
                ExecLocal item;

                memset(&item, 0, sizeof(item));
                if (!(structure_start
                          ? initialize_struct_local_body(
                                executor, type_name, type_length,
                                structure_start, structure_end, &item)
                          : initialize_struct_local(executor, type_name,
                                                    type_length, &item))) {
                    executor->failed = 1;
                    executor->error_at = start;
                    return 0;
                }
                local->array[element] = item.value;
            } else {
                local->array[element] = *declared;
            }
        }
        cursor = exec_space(close + 1, end);
    } else if (structure &&
               !(structure_start
                     ? initialize_struct_local_body(executor, type_name, type_length,
                                                    structure_start, structure_end, local)
                     : initialize_struct_local(executor, type_name, type_length, local))) {
        executor->failed = 1;
        executor->error_at = start;
        return 0;
    }
    if (structure) {
        int contains_sampler = local->array_size
                                   ? value_contains_sampler(&local->array[0])
                                   : value_contains_sampler(&local->value);

        if (contains_sampler) {
            executor->failed = 1;
            executor->error_at = start;
            return 0;
        }
    }
    if (cursor < end && *cursor == '=') {
        MesaGLSLValue initializer;
        int initializer_ok;
        int declared_local_count = executor->local_count;

        executor->local_count = declared_local_count - 1;
        if (declaration_const)
            local->is_const = 0;
        initializer_ok = declaration_const
                             ? eval_constant_range(executor, cursor + 1, end,
                                                   &initializer)
                             : eval_range(executor, cursor + 1, end, &initializer);
        if (declaration_const)
            local->is_const = 1;
        executor->local_count = declared_local_count;
        if (!initializer_ok)
            return 0;
        if (local->array_size) {
            executor->failed = 1;
            executor->error_at = cursor;
            return 0;
        }
        if (structure) {
            int member;

            if (initializer.type != MESAGL_GLSL_TYPE_STRUCT ||
                !same_struct_type(initializer.struct_type_name,
                                  initializer.struct_type_length,
                                  type_name, type_length) ||
                initializer.member_count != local->member_count) {
                executor->failed = 1;
                executor->error_at = cursor;
                return 0;
            }
            for (member = 0; member < local->member_count; ++member)
                if (!copy_value(&local->members[member],
                                &initializer.members[member])) {
                    executor->failed = 1;
                    executor->error_at = cursor;
                    return 0;
                }
        } else if ((initializer.type == MESAGL_GLSL_TYPE_UNKNOWN
                        ? MESAGL_GLSL_TYPE_FLOAT
                        : initializer.type) != declared->type ||
                   initializer.rows != declared->rows ||
                   initializer.columns != declared->columns) {
            executor->failed = 1;
            executor->error_at = cursor;
            return 0;
        } else {
            local->value = initializer;
        }
        initialized = 1;
        cursor = end;
    }
    cursor = exec_space(cursor, end);
    if (cursor != end) {
        executor->failed = 1;
        executor->error_at = cursor;
        return 0;
    }
    if (declaration_const && !initialized) {
        executor->failed = 1;
        executor->error_at = start;
        return 0;
    }
    return 1;
}

static void execute_range(Executor *executor, const char *start, const char *end);

static void execute_scoped_range(Executor *executor, const char *start, const char *end)
{
    int local_count = executor->local_count;
    int array_storage_used = executor->array_storage_used;
    int struct_storage_used = executor->struct_storage_used;
    int scope_base = executor->scope_base;

    executor->scope_base = local_count;
    execute_range(executor, start, end);
    executor->local_count = local_count;
    executor->array_storage_used = array_storage_used;
    executor->struct_storage_used = struct_storage_used;
    executor->scope_base = scope_base;
}

static void execute_clause(Executor *executor, const char *start, const char *end)
{
    char statement_buffer[512];
    char *statement = statement_buffer;
    const char *trimmed = exec_space(start, end);
    const char *finish = end;
    const char *name;
    size_t name_length;
    const char *name_cursor;
    int delta = 0;

    while (finish > trimmed && (finish[-1] == ' ' || finish[-1] == '\t' ||
                                finish[-1] == '\r' || finish[-1] == '\n'))
        --finish;
    if (finish == trimmed)
        return;
    if (finish - trimmed >= 2 && finish[-1] == '+' && finish[-2] == '+') {
        delta = 1;
        finish -= 2;
    } else if (finish - trimmed >= 2 && finish[-1] == '-' && finish[-2] == '-') {
        delta = -1;
        finish -= 2;
    }
    name_cursor = trimmed;
    if (delta && exec_identifier(&name_cursor, finish, &name, &name_length) &&
        exec_space(name_cursor, finish) == finish) {
        MesaGLSLValue value;

        if (!exec_lookup(executor, name, name_length, &value)) {
            executor->failed = 1;
            return;
        }
        value.data[0] += (float)delta;
        if (!exec_assign(executor, name, name_length, -1, NULL, 0, &value))
            executor->failed = 1;
        return;
    }
    if (delta)
        finish += 2;
    if ((size_t)(finish - trimmed) > SIZE_MAX - 2) {
        executor->failed = 1;
        executor->error_at = trimmed;
        return;
    }
    if ((size_t)(finish - trimmed) + 2 > sizeof(statement_buffer) &&
        !(statement = (char *)ntglAlloc((size_t)(finish - trimmed) + 2))) {
        executor->failed = 1;
        executor->error_at = trimmed;
        return;
    }
    memcpy(statement, trimmed, (size_t)(finish - trimmed));
    statement[finish - trimmed] = ';';
    statement[finish - trimmed + 1] = '\0';
    execute_range(executor, statement, statement + (finish - trimmed) + 1);
    if (statement != statement_buffer)
        ntglFree(statement);
}

static int evaluate_condition(Executor *executor, const char *start,
                              const char *end, MesaGLSLValue *condition,
                              int *declared)
{
    const char *cursor = start;
    const char *type;
    const char *name;
    size_t type_length;
    size_t name_length;

    *declared = 0;
    if (exec_identifier(&cursor, end, &type, &type_length) &&
        name_is(type, type_length, "const") &&
        !exec_identifier(&cursor, end, &type, &type_length))
        return 0;
    if (name_is(type, type_length, "bool") &&
        exec_identifier(&cursor, end, &name, &name_length) &&
        exec_space(cursor, end) < end && *exec_space(cursor, end) == '=') {
        execute_clause(executor, start, end);
        if (executor->failed ||
            !exec_lookup(executor, name, name_length, condition)) {
            executor->failed = 1;
            executor->error_at = start;
            return 0;
        }
        *declared = 1;
        return 1;
    }
    return eval_range(executor, start, end, condition);
}

static int keyword_at(const char *cursor, const char *end, const char *keyword,
                      const char **after)
{
    const char *name;
    size_t length;

    if (!exec_identifier(&cursor, end, &name, &length) || !name_is(name, length, keyword))
        return 0;
    if (after)
        *after = cursor;
    return 1;
}

static const char *execute_statement(Executor *executor, const char *start, const char *end)
{
    const char *cursor = exec_space(start, end);
    const char *name;
    size_t length;

    if (cursor >= end || executor->failed || executor->returned || executor->discarded)
        return end;
    if (*cursor == '{') {
        const char *close = matching(cursor, end, '{', '}');

        if (!close) {
            executor->failed = 1;
            executor->error_at = cursor;
            return end;
        }
        execute_scoped_range(executor, cursor + 1, close);
        return close + 1;
    }
    {
        const char *finish = statement_end(cursor, end);
        const char *expression_end = finish;
        int increment_statement =
            end - cursor >= 2 && ((!strncmp(cursor, "++", 2)) ||
                                  (!strncmp(cursor, "--", 2)));

        if (expression_end > cursor && expression_end[-1] == ';')
            --expression_end;
        while (expression_end > cursor &&
               (expression_end[-1] == ' ' || expression_end[-1] == '\t' ||
                expression_end[-1] == '\r' || expression_end[-1] == '\n'))
            --expression_end;
        if (increment_statement && finish <= end && finish > cursor && finish[-1] == ';') {
            MesaGLSLValue unused;

            if (!eval_range(executor, cursor, expression_end, &unused))
                return finish;
            return finish;
        }
    }
    {
        const char *keyword_cursor = cursor;

        if (exec_identifier(&keyword_cursor, end, &name, &length) && name_is(name, length, "if")) {
            const char *condition_start = exec_space(keyword_cursor, end);
            const char *condition_end;
            const char *then_start;
            const char *then_end;
            const char *after;
            MesaGLSLValue condition;

            if (condition_start >= end || *condition_start != '(' ||
                !(condition_end = matching(condition_start, end, '(', ')'))) {
                executor->failed = 1;
                executor->error_at = condition_start;
                return end;
            }
            if (!eval_range(executor, condition_start + 1, condition_end,
                            &condition))
                return end;
            if (condition.type != MESAGL_GLSL_TYPE_BOOL || condition.rows != 1 ||
                condition.columns != 1) {
                executor->failed = 1;
                executor->error_at = condition_start + 1;
                return end;
            }
            then_start = condition_end + 1;
            then_end = statement_end(then_start, end);
            if (condition.data[0] != 0.0f)
                execute_scoped_range(executor, then_start, then_end);
            after = exec_space(then_end, end);
            keyword_cursor = after;
            if (exec_identifier(&keyword_cursor, end, &name, &length) &&
                name_is(name, length, "else")) {
                const char *else_end = statement_end(keyword_cursor, end);

                if (condition.data[0] == 0.0f)
                    execute_scoped_range(executor, keyword_cursor, else_end);
                return else_end;
            }
            return then_end;
        }
    }
    {
        const char *after_keyword;

        if (keyword_at(cursor, end, "for", &after_keyword)) {
            const char *open = exec_space(after_keyword, end);
            const char *close;
            const char *first_semicolon = NULL;
            const char *second_semicolon = NULL;
            const char *body_start;
            const char *body_end;
            const char *scan;
            int depth = 0;
            int iteration;
            int loop_local_count = executor->local_count;
            int loop_array_storage = executor->array_storage_used;
            int loop_struct_storage = executor->struct_storage_used;
            int loop_scope_base = executor->scope_base;
            int condition_local_count;

            if (open >= end || *open != '(' || !(close = matching(open, end, '(', ')'))) {
                executor->failed = 1;
                executor->error_at = open;
                return end;
            }
            for (scan = open + 1; scan < close; ++scan) {
                if (*scan == '(')
                    ++depth;
                else if (*scan == ')')
                    --depth;
                else if (*scan == ';' && !depth) {
                    if (!first_semicolon)
                        first_semicolon = scan;
                    else {
                        second_semicolon = scan;
                        break;
                    }
                }
            }
            if (!first_semicolon || !second_semicolon) {
                executor->failed = 1;
                executor->error_at = open;
                return end;
            }
            executor->scope_base = loop_local_count;
            execute_clause(executor, open + 1, first_semicolon);
            condition_local_count = executor->local_count;
            body_start = close + 1;
            body_end = statement_end(body_start, end);
            ++executor->loop_depth;
            for (iteration = 0; iteration < MESAGL_MAX_SHADER_LOOP_ITERATIONS && !executor->failed &&
                                !executor->returned && !executor->discarded;
                 ++iteration) {
                MesaGLSLValue condition = boolean(1.0f);
                int condition_declared = 0;

                executor->local_count = condition_local_count;
                if (exec_space(first_semicolon + 1, second_semicolon) < second_semicolon &&
                    !evaluate_condition(executor, first_semicolon + 1,
                                        second_semicolon, &condition,
                                        &condition_declared))
                    break;
                if (condition.type != MESAGL_GLSL_TYPE_BOOL || condition.rows != 1 ||
                    condition.columns != 1) {
                    executor->failed = 1;
                    executor->error_at = first_semicolon + 1;
                    break;
                }
                if (condition.data[0] == 0.0f)
                    break;
                execute_scoped_range(executor, body_start, body_end);
                if (executor->breaking) {
                    executor->breaking = 0;
                    break;
                }
                executor->continuing = 0;
                execute_clause(executor, second_semicolon + 1, close);
            }
            if (iteration == MESAGL_MAX_SHADER_LOOP_ITERATIONS) {
                executor->failed = 1;
                executor->error_at = cursor;
            }
            --executor->loop_depth;
            executor->local_count = loop_local_count;
            executor->array_storage_used = loop_array_storage;
            executor->struct_storage_used = loop_struct_storage;
            executor->scope_base = loop_scope_base;
            return body_end;
        }
        if (keyword_at(cursor, end, "while", &after_keyword)) {
            const char *open = exec_space(after_keyword, end);
            const char *close;
            const char *body_start;
            const char *body_end;
            int iteration;
            int condition_local_count = executor->local_count;
            int condition_scope_base = executor->scope_base;

            if (open >= end || *open != '(' || !(close = matching(open, end, '(', ')'))) {
                executor->failed = 1;
                executor->error_at = open;
                return end;
            }
            body_start = close + 1;
            body_end = statement_end(body_start, end);
            executor->scope_base = condition_local_count;
            ++executor->loop_depth;
            for (iteration = 0; iteration < MESAGL_MAX_SHADER_LOOP_ITERATIONS && !executor->failed &&
                                !executor->returned && !executor->discarded;
                 ++iteration) {
                MesaGLSLValue condition;
                int condition_declared;

                executor->local_count = condition_local_count;
                if (!evaluate_condition(executor, open + 1, close, &condition,
                                        &condition_declared))
                    break;
                if (condition.type != MESAGL_GLSL_TYPE_BOOL || condition.rows != 1 ||
                    condition.columns != 1) {
                    executor->failed = 1;
                    executor->error_at = open + 1;
                    break;
                }
                if (condition.data[0] == 0.0f)
                    break;
                execute_scoped_range(executor, body_start, body_end);
                if (executor->breaking) {
                    executor->breaking = 0;
                    break;
                }
                executor->continuing = 0;
            }
            if (iteration == MESAGL_MAX_SHADER_LOOP_ITERATIONS) {
                executor->failed = 1;
                executor->error_at = cursor;
            }
            --executor->loop_depth;
            executor->local_count = condition_local_count;
            executor->scope_base = condition_scope_base;
            return body_end;
        }
        if (keyword_at(cursor, end, "do", &after_keyword)) {
            const char *body_start = after_keyword;
            const char *body_end = statement_end(body_start, end);
            const char *while_start = exec_space(body_end, end);
            const char *condition_start;
            const char *condition_end;
            const char *after_condition;
            int iteration;

            if (!keyword_at(while_start, end, "while", &condition_start)) {
                executor->failed = 1;
                executor->error_at = while_start;
                return end;
            }
            condition_start = exec_space(condition_start, end);
            if (condition_start >= end || *condition_start != '(' ||
                !(condition_end = matching(condition_start, end, '(', ')'))) {
                executor->failed = 1;
                executor->error_at = condition_start;
                return end;
            }
            after_condition = exec_space(condition_end + 1, end);
            if (after_condition >= end || *after_condition != ';') {
                executor->failed = 1;
                executor->error_at = after_condition;
                return end;
            }
            ++executor->loop_depth;
            for (iteration = 0; iteration < MESAGL_MAX_SHADER_LOOP_ITERATIONS &&
                                !executor->failed && !executor->returned &&
                                !executor->discarded;
                 ++iteration) {
                MesaGLSLValue condition;

                execute_scoped_range(executor, body_start, body_end);
                if (executor->breaking) {
                    executor->breaking = 0;
                    break;
                }
                executor->continuing = 0;
                if (!eval_range(executor, condition_start + 1, condition_end,
                                &condition))
                    break;
                if (condition.type != MESAGL_GLSL_TYPE_BOOL || condition.rows != 1 ||
                    condition.columns != 1) {
                    executor->failed = 1;
                    executor->error_at = condition_start + 1;
                    break;
                }
                if (condition.data[0] == 0.0f)
                    break;
            }
            if (iteration == MESAGL_MAX_SHADER_LOOP_ITERATIONS) {
                executor->failed = 1;
                executor->error_at = cursor;
            }
            --executor->loop_depth;
            return after_condition + 1;
        }
    }
    {
        const char *statement_finish = statement_end(cursor, end);
        const char *semicolon = statement_finish > cursor ? statement_finish - 1 : cursor;
        const char *token_cursor = cursor;
        MesaGLSLValue declared;
        int declaration_const = 0;

        if (semicolon == cursor)
            return statement_finish;
        token_cursor = exec_space(token_cursor, semicolon);
        if (token_cursor < semicolon &&
            !((*token_cursor >= 'a' && *token_cursor <= 'z') ||
              (*token_cursor >= 'A' && *token_cursor <= 'Z') ||
              *token_cursor == '_')) {
            MesaGLSLValue unused;

            eval_range(executor, token_cursor, semicolon, &unused);
            return statement_finish;
        }
        if (!exec_identifier(&token_cursor, semicolon, &name, &length)) {
            executor->failed = 1;
            executor->error_at = cursor;
            return statement_finish;
        }
        while (parameter_qualifier(name, length)) {
            if (name_is(name, length, "const"))
                declaration_const = 1;
            if (!exec_identifier(&token_cursor, semicolon, &name, &length)) {
                executor->failed = 1;
                executor->error_at = token_cursor;
                return statement_finish;
            }
        }
        if (name_is(name, length, "precision"))
            return statement_finish;
        if (name_is(name, length, "discard")) {
            executor->discarded = 1;
            return statement_finish;
        }
        if (name_is(name, length, "return")) {
            token_cursor = exec_space(token_cursor, semicolon);
            if (token_cursor < semicolon) {
                if (!eval_range(executor, token_cursor, semicolon, &executor->return_value))
                    return statement_finish;
                executor->have_return_value = 1;
            }
            executor->returned = 1;
            return statement_finish;
        }
        if (name_is(name, length, "break")) {
            if (!executor->loop_depth) {
                executor->failed = 1;
                executor->error_at = cursor;
                return statement_finish;
            }
            executor->breaking = 1;
            return statement_finish;
        }
        if (name_is(name, length, "continue")) {
            if (!executor->loop_depth) {
                executor->failed = 1;
                executor->error_at = cursor;
                return statement_finish;
            }
            executor->continuing = 1;
            return statement_finish;
        }
        if (name_is(name, length, "struct")) {
            const char *type_name;
            size_t type_length;
            const char *open;
            const char *close;
            const char *after_struct = exec_space(token_cursor, semicolon);
            const char *after_name = after_struct;
            const char *candidate_name = NULL;
            size_t candidate_length = 0;
            MesaGLSLValue structure_value;
            const char *declarator;

            if (exec_identifier(&after_name, semicolon, &candidate_name,
                                &candidate_length)) {
                open = exec_space(after_name, semicolon);
                type_name = candidate_name;
                type_length = candidate_length;
            } else {
                open = after_struct;
                type_name = open;
                type_length = 0;
            }
            if (open >= semicolon || *open != '{' ||
                !(close = matching(open, semicolon, '{', '}'))) {
                executor->failed = 1;
                executor->error_at = open;
                return statement_finish;
            }
            if (!type_length) {
                type_name = open;
                type_length = (size_t)(close + 1 - open);
            }
            declarator = exec_space(close + 1, semicolon);
            if (declarator == semicolon)
                return statement_finish;
            memset(&structure_value, 0, sizeof(structure_value));
            while (declarator < semicolon) {
                const char *declarator_end = declarator;
                int parentheses = 0;
                int brackets = 0;

                while (declarator_end < semicolon) {
                    if (*declarator_end == '(')
                        ++parentheses;
                    else if (*declarator_end == ')')
                        --parentheses;
                    else if (*declarator_end == '[')
                        ++brackets;
                    else if (*declarator_end == ']')
                        --brackets;
                    else if (*declarator_end == ',' && !parentheses && !brackets)
                        break;
                    ++declarator_end;
                }
                if (!execute_local_declarator(
                        executor, type_name, type_length, &structure_value, 1,
                        declaration_const, open + 1, close, declarator,
                        declarator_end))
                    return statement_finish;
                if (declarator_end == semicolon)
                    break;
                declarator = declarator_end + 1;
            }
            return statement_finish;
        }
        {
            const char *struct_start;
            const char *struct_end;
            int scalar_or_vector = value_type(name, length, &declared);
            int structure = !scalar_or_vector &&
                            struct_definition(executor, name, length, &struct_start, &struct_end);

            (void)struct_start;
            (void)struct_end;
            if (scalar_or_vector || structure) {
                const char *declarator = token_cursor;

                while (declarator < semicolon) {
                    const char *declarator_end = declarator;
                    int parentheses = 0;
                    int brackets = 0;

                    while (declarator_end < semicolon) {
                        if (*declarator_end == '(')
                            ++parentheses;
                        else if (*declarator_end == ')')
                            --parentheses;
                        else if (*declarator_end == '[')
                            ++brackets;
                        else if (*declarator_end == ']')
                            --brackets;
                        else if (*declarator_end == ',' && !parentheses && !brackets)
                            break;
                        ++declarator_end;
                    }
                    if (!execute_local_declarator(executor, name, length, &declared,
                                                  structure, declaration_const,
                                                  NULL, NULL,
                                                  declarator, declarator_end))
                        return statement_finish;
                    if (declarator_end == semicolon)
                        break;
                    declarator = declarator_end + 1;
                }
                return statement_finish;
            }
        }
        {
            const char *swizzle = NULL;
            size_t swizzle_length = 0;
            const char *struct_member = NULL;
            size_t struct_member_length = 0;
            const char *nested_member = NULL;
            size_t nested_member_length = 0;
            const char *lvalue_path[16];
            size_t lvalue_path_length[16];
            int lvalue_path_array_index[16];
            int lvalue_path_extra_index[16][2];
            int lvalue_path_extra_index_count[16];
            int lvalue_path_count = 0;
            int complex_lvalue = 0;
            const char *operator_at;
            MesaGLSLValue value;
            MesaGLSLValue previous;
            char operation = '=';
            int array_index = -1;
            int base_indices[3];
            int base_index_count = 0;

            token_cursor = exec_space(token_cursor, semicolon);
            if (token_cursor < semicolon && *token_cursor == '(') {
                MesaGLSLValue unused;

                eval_range(executor, cursor, semicolon, &unused);
                return statement_finish;
            }
            while (token_cursor < semicolon && *token_cursor == '[') {
                const char *close = matching(token_cursor, semicolon, '[', ']');
                MesaGLSLValue subscript;

                if (base_index_count >= 3 || !close ||
                    !eval_range(executor, token_cursor + 1, close, &subscript) ||
                    subscript.member_count || subscript.array_size ||
                    normalized_type(&subscript) != MESAGL_GLSL_TYPE_INT ||
                    subscript.rows != 1 || subscript.columns != 1) {
                    executor->failed = 1;
                    executor->error_at = token_cursor;
                    return statement_finish;
                }
                base_indices[base_index_count++] = safe_integer_value(subscript.data[0]);
                token_cursor = exec_space(close + 1, semicolon);
            }
            if (base_index_count == 1)
                array_index = base_indices[0];
            while (token_cursor < semicolon && *token_cursor == '.') {
                const char *component;
                size_t component_length;

                ++token_cursor;
                if (lvalue_path_count >= 16 ||
                    !exec_identifier(&token_cursor, semicolon, &component,
                                     &component_length)) {
                    executor->failed = 1;
                    return statement_finish;
                }
                lvalue_path[lvalue_path_count] = component;
                lvalue_path_length[lvalue_path_count] = component_length;
                lvalue_path_array_index[lvalue_path_count] = -1;
                lvalue_path_extra_index_count[lvalue_path_count] = 0;
                token_cursor = exec_space(token_cursor, semicolon);
                while (token_cursor < semicolon && *token_cursor == '[') {
                    const char *close = matching(token_cursor, semicolon, '[', ']');
                    MesaGLSLValue subscript;

                    if (lvalue_path_extra_index_count[lvalue_path_count] >= 2 ||
                        !close || !eval_range(executor, token_cursor + 1, close,
                                              &subscript) || subscript.rows != 1 ||
                        subscript.columns != 1 ||
                        normalized_type(&subscript) != MESAGL_GLSL_TYPE_INT) {
                        executor->failed = 1;
                        return statement_finish;
                    }
                    if (lvalue_path_array_index[lvalue_path_count] < 0)
                        lvalue_path_array_index[lvalue_path_count] =
                            safe_integer_value(subscript.data[0]);
                    else
                        lvalue_path_extra_index[lvalue_path_count]
                                                 [lvalue_path_extra_index_count
                                                      [lvalue_path_count]++] =
                            safe_integer_value(subscript.data[0]);
                    complex_lvalue = 1;
                    token_cursor = exec_space(close + 1, semicolon);
                }
                ++lvalue_path_count;
            }
            if (lvalue_path_count > 0) {
                swizzle = lvalue_path[0];
                swizzle_length = lvalue_path_length[0];
            }
            if (lvalue_path_count > 1) {
                struct_member = lvalue_path[0];
                struct_member_length = lvalue_path_length[0];
                nested_member = lvalue_path[1];
                nested_member_length = lvalue_path_length[1];
                swizzle = lvalue_path_count > 2 ? lvalue_path[2] : NULL;
                swizzle_length = lvalue_path_count > 2 ? lvalue_path_length[2] : 0;
            }
            operator_at = exec_space(token_cursor, semicolon);
            if (operator_at + 2 <= semicolon &&
                ((!strncmp(operator_at, "++", 2)) ||
                 (!strncmp(operator_at, "--", 2))) &&
                exec_space(operator_at + 2, semicolon) == semicolon) {
                MesaGLSLValue unused;

                if (!eval_range(executor, cursor, operator_at + 2, &unused))
                    return statement_finish;
                return statement_finish;
            }
            if (operator_at + 1 < semicolon && operator_at[1] == '=' &&
                (operator_at[0] == '+' || operator_at[0] == '-' || operator_at[0] == '*' ||
                 operator_at[0] == '/')) {
                operation = operator_at[0];
                operator_at += 2;
            } else if (operator_at < semicolon && *operator_at == '=' &&
                       (operator_at + 1 >= semicolon || operator_at[1] != '='))
                ++operator_at;
            else {
                MesaGLSLValue unused;

                eval_range(executor, cursor, semicolon, &unused);
                return statement_finish;
            }
            if (!eval_range(executor, operator_at, semicolon, &value))
                return statement_finish;
            if (lvalue_path_count > 0 &&
                lvalue_path_extra_index_count[lvalue_path_count - 1] > 0) {
                MesaGLSLValue *destination;
                MesaGLSLValue previous_indexed;
                int terminal_indices[3];
                int terminal_index_count =
                    1 + lvalue_path_extra_index_count[lvalue_path_count - 1];
                int terminal_path = lvalue_path_count - 1;

                terminal_indices[0] = lvalue_path_array_index[terminal_path];
                memcpy(terminal_indices + 1,
                       lvalue_path_extra_index[terminal_path],
                       (size_t)lvalue_path_extra_index_count[terminal_path] *
                           sizeof(int));
                if (!resolve_indexed_terminal(
                        executor, name, length, array_index, lvalue_path,
                        lvalue_path_length, lvalue_path_array_index,
                        lvalue_path_count, terminal_indices,
                        &terminal_index_count, &destination) ||
                    !select_index_chain(destination, terminal_indices,
                                        terminal_index_count, &previous_indexed)) {
                    executor->failed = 1;
                    executor->error_at = cursor;
                    return statement_finish;
                }
                if (operation != '=') {
                    Parser arithmetic_parser;

                    memset(&arithmetic_parser, 0, sizeof(arithmetic_parser));
                    value = binary(&arithmetic_parser, previous_indexed, value,
                                   operation);
                    if (arithmetic_parser.failed) {
                        executor->failed = 1;
                        executor->error_at = cursor;
                        return statement_finish;
                    }
                }
                if (!executor->suppress_side_effects &&
                    !assign_index_chain(destination, terminal_indices,
                                        terminal_index_count, &value)) {
                    executor->failed = 1;
                    executor->error_at = cursor;
                }
                return statement_finish;
            }
            if (base_index_count > 1) {
                MesaGLSLValue *destination;
                MesaGLSLValue array_root;
                int external = 0;
                int resolved;

                if (lvalue_path_count) {
                    executor->failed = 1;
                    executor->error_at = cursor;
                    return statement_finish;
                }
                resolved = resolve_local_index_root(executor, name, length,
                                                    &array_root, &destination);
                if (resolved < 0) {
                    executor->failed = 1;
                    executor->error_at = cursor;
                    return statement_finish;
                }
                if (!resolved) {
                    if (!exec_lookup(executor, name, length, &array_root)) {
                        executor->failed = 1;
                        executor->error_at = cursor;
                        return statement_finish;
                    }
                    destination = &array_root;
                    external = 1;
                }
                if (!select_index_chain(destination, base_indices,
                                        base_index_count, &previous)) {
                    executor->failed = 1;
                    executor->error_at = cursor;
                    return statement_finish;
                }
                if (operation != '=') {
                    Parser arithmetic_parser;

                    memset(&arithmetic_parser, 0, sizeof(arithmetic_parser));
                    value = binary(&arithmetic_parser, previous, value, operation);
                    if (arithmetic_parser.failed) {
                        executor->failed = 1;
                        executor->error_at = cursor;
                        return statement_finish;
                    }
                }
                if (!executor->suppress_side_effects &&
                    !assign_index_chain(destination, base_indices,
                                        base_index_count, &value)) {
                    executor->failed = 1;
                    executor->error_at = cursor;
                    return statement_finish;
                }
                if (!executor->suppress_side_effects && external &&
                    !assign_external_value(executor, name, length,
                                           destination)) {
                    executor->failed = 1;
                    executor->error_at = cursor;
                }
                return statement_finish;
            }
            if (lvalue_path_count > 3 || complex_lvalue) {
                MesaGLSLValue *destination;
                const char *terminal_swizzle;
                size_t terminal_swizzle_length;
                int numeric_index;
                int resolved = resolve_local_lvalue(executor, name, length, array_index,
                                                    lvalue_path, lvalue_path_length,
                                                    lvalue_path_array_index,
                                                    lvalue_path_count, &destination,
                                                    &terminal_swizzle,
                                                    &terminal_swizzle_length,
                                                    &numeric_index);

                if (resolved <= 0) {
                    executor->failed = 1;
                    executor->error_at = cursor;
                    return statement_finish;
                }
                if (executor->suppress_side_effects) {
                    MesaGLSLValue selected;

                    if (numeric_index >= 0) {
                        if (!select_numeric_index(destination, numeric_index, &selected)) {
                            executor->failed = 1;
                            return statement_finish;
                        }
                    } else {
                        selected = *destination;
                    }
                    if (terminal_swizzle_length) {
                        MesaGLSLValue swizzled = {
                            {0}, (int)terminal_swizzle_length, 1, NULL, 0,
                            NULL, NULL, 0, {0}, {0}, 0, selected.type, NULL, 0};
                        size_t channel;

                        if (terminal_swizzle_length > 4) {
                            executor->failed = 1;
                            return statement_finish;
                        }
                        for (channel = 0; channel < terminal_swizzle_length; ++channel) {
                            int source_channel =
                                swizzle_channel(terminal_swizzle[channel]);

                            if (source_channel < 0 ||
                                source_channel >= components(&selected)) {
                                executor->failed = 1;
                                return statement_finish;
                            }
                            swizzled.data[channel] = selected.data[source_channel];
                        }
                        selected = swizzled;
                    }
                    if (operation != '=') {
                        Parser arithmetic_parser;

                        memset(&arithmetic_parser, 0, sizeof(arithmetic_parser));
                        value = binary(&arithmetic_parser, selected, value, operation);
                        if (arithmetic_parser.failed) {
                            executor->failed = 1;
                            executor->error_at = cursor;
                            return statement_finish;
                        }
                    }
                    if (!copy_value(&selected, &value)) {
                        executor->failed = 1;
                        executor->error_at = cursor;
                    }
                    return statement_finish;
                }
                if (operation != '=') {
                    Parser arithmetic_parser;

                    if (numeric_index >= 0) {
                        if (!select_numeric_index(destination, numeric_index, &previous)) {
                            executor->failed = 1;
                            return statement_finish;
                        }
                    } else {
                        previous = *destination;
                    }
                    if (terminal_swizzle_length) {
                        MesaGLSLValue selected = {
                            {0}, (int)terminal_swizzle_length, 1, NULL, 0, NULL, NULL, 0,
                            {0}, {0}, 0, MESAGL_GLSL_TYPE_UNKNOWN, NULL, 0};
                        size_t channel;

                        if (terminal_swizzle_length > 4) {
                            executor->failed = 1;
                            return statement_finish;
                        }
                        selected.type = previous.type;
                        for (channel = 0; channel < terminal_swizzle_length; ++channel) {
                            int source_channel = swizzle_channel(terminal_swizzle[channel]);

                            if (source_channel < 0 ||
                                source_channel >= components(&previous)) {
                                executor->failed = 1;
                                return statement_finish;
                            }
                            selected.data[channel] = previous.data[source_channel];
                        }
                        previous = selected;
                    }
                    memset(&arithmetic_parser, 0, sizeof(arithmetic_parser));
                    value = binary(&arithmetic_parser, previous, value, operation);
                    if (arithmetic_parser.failed) {
                        executor->failed = 1;
                        return statement_finish;
                    }
                }
                if (numeric_index >= 0
                        ? !assign_numeric_index(destination, numeric_index,
                                                terminal_swizzle,
                                                terminal_swizzle_length, &value)
                        : terminal_swizzle_length
                              ? !assign_swizzle(destination, terminal_swizzle,
                                                terminal_swizzle_length, &value)
                              : !copy_value(destination, &value)) {
                    executor->failed = 1;
                    executor->error_at = cursor;
                }
                return statement_finish;
            }
            if (operation != '=') {
                Parser arithmetic_parser;
                const char *calculation_swizzle = swizzle;
                size_t calculation_swizzle_length = swizzle_length;

                if (!exec_lookup(executor, name, length, &previous)) {
                    executor->failed = 1;
                    return statement_finish;
                }
                if (array_index >= 0) {
                    if (previous.array) {
                        if (array_index >= previous.array_size) {
                            executor->failed = 1;
                            return statement_finish;
                        }
                        previous = previous.array[array_index];
                    } else if (previous.columns > 1) {
                        MesaGLSLValue selected = {
                            {0}, previous.rows, 1, NULL, 0, NULL, NULL, 0,
                            {0}, {0}, 0, previous.type, NULL, 0};
                        int row;

                        if (array_index >= previous.columns) {
                            executor->failed = 1;
                            return statement_finish;
                        }
                        for (row = 0; row < previous.rows; ++row)
                            selected.data[row] =
                                previous.data[array_index * previous.rows + row];
                        previous = selected;
                    } else if (array_index < previous.rows) {
                        previous.data[0] = previous.data[array_index];
                        previous.rows = 1;
                    } else {
                        executor->failed = 1;
                        return statement_finish;
                    }
                }
                if (!struct_member && previous.member_count && swizzle_length) {
                    struct_member = swizzle;
                    struct_member_length = swizzle_length;
                    swizzle = NULL;
                    swizzle_length = 0;
                    calculation_swizzle = NULL;
                    calculation_swizzle_length = 0;
                }
                if (struct_member) {
                    int member;

                    for (member = 0; member < previous.member_count; ++member)
                        if (strlen(previous.member_names[member]) == struct_member_length &&
                            !strncmp(previous.member_names[member], struct_member,
                                     struct_member_length))
                            break;
                    if (member == previous.member_count) {
                        executor->failed = 1;
                        return statement_finish;
                    }
                    previous = previous.members[member];
                }
                if (nested_member_length && previous.member_count) {
                    int member;

                    for (member = 0; member < previous.member_count; ++member)
                        if (strlen(previous.member_names[member]) == nested_member_length &&
                            !strncmp(previous.member_names[member], nested_member,
                                     nested_member_length))
                            break;
                    if (member == previous.member_count) {
                        executor->failed = 1;
                        return statement_finish;
                    }
                    previous = previous.members[member];
                } else if (nested_member_length) {
                    if (calculation_swizzle_length) {
                        executor->failed = 1;
                        return statement_finish;
                    }
                    calculation_swizzle = nested_member;
                    calculation_swizzle_length = nested_member_length;
                }
                if (calculation_swizzle_length) {
                    MesaGLSLValue selected = {
                        {0}, (int)calculation_swizzle_length, 1, NULL, 0, NULL, NULL, 0,
                        {0}, {0}, 0, MESAGL_GLSL_TYPE_UNKNOWN, NULL, 0};
                    size_t channel;

                    for (channel = 0; channel < calculation_swizzle_length; ++channel) {
                        int source_channel = swizzle_channel(calculation_swizzle[channel]);

                        if (source_channel < 0 || source_channel >= components(&previous)) {
                            executor->failed = 1;
                            return statement_finish;
                        }
                        selected.data[channel] = previous.data[source_channel];
                    }
                    previous = selected;
                }
                memset(&arithmetic_parser, 0, sizeof(arithmetic_parser));
                value = binary(&arithmetic_parser, previous, value, operation);
                if (arithmetic_parser.failed) {
                    executor->failed = 1;
                    return statement_finish;
                }
            }
            if (!(struct_member
                      ? exec_assign_struct_member(executor, name, length, array_index,
                                                  struct_member,
                                                  struct_member_length, nested_member,
                                                  nested_member_length, swizzle, swizzle_length,
                                                  &value)
                      : exec_assign(executor, name, length, array_index, swizzle, swizzle_length,
                                    &value))) {
                executor->failed = 1;
                executor->error_at = cursor;
            }
            return statement_finish;
        }
    }
}

static void execute_range(Executor *executor, const char *start, const char *end)
{
    const char *cursor = start;

    while ((cursor = exec_space(cursor, end)) < end && !executor->failed && !executor->returned &&
           !executor->discarded && !executor->breaking && !executor->continuing)
        cursor = execute_statement(executor, cursor, end);
}

static int exec_increment(void *user, const char *start, const char *end,
                          int delta, int postfix, MesaGLSLValue *value)
{
    Executor *executor = (Executor *)user;
    MesaGLSLValue before;
    MesaGLSLValue after;
    const char *error_at;
    int component_index;

    if (!expression_internal(start, end, exec_lookup, exec_call, executor, &before,
                             &error_at, exec_suppress_side_effects, exec_increment,
                             exec_assign_expression))
        return 0;
    if (before.member_count || before.array_size ||
        (normalized_type(&before) != MESAGL_GLSL_TYPE_FLOAT &&
         normalized_type(&before) != MESAGL_GLSL_TYPE_INT))
        return 0;
    after = before;
    for (component_index = 0; component_index < components(&after);
         ++component_index)
        after.data[component_index] += (float)delta;
    if (!executor->suppress_side_effects &&
        !exec_assign_expression(executor, start, end, &after))
        return 0;
    *value = postfix ? before : after;
    return 1;
}

static int parameter_qualifier(const char *name, size_t length)
{
    return name_is(name, length, "const") || name_is(name, length, "in") ||
           name_is(name, length, "out") || name_is(name, length, "inout") ||
           name_is(name, length, "lowp") || name_is(name, length, "mediump") ||
           name_is(name, length, "highp");
}

static int struct_member_count(const Executor *executor, const char *type_name,
                               size_t type_length)
{
    const char *cursor;
    const char *end;
    int count = 0;

    if (!struct_definition(executor, type_name, type_length, &cursor, &end))
        return 0;
    while ((cursor = exec_space(cursor, end)) < end) {
        const char *semicolon = strchr(cursor, ';');

        if (!semicolon || semicolon >= end)
            return 0;
        ++count;
        cursor = semicolon + 1;
    }
    return count;
}

static int parameter_array_size(Executor *executor, const char **cursor,
                                const char *end, int *array_size)
{
    const char *open = exec_space(*cursor, end);
    const char *close;
    MesaGLSLValue size;

    *array_size = 0;
    if (open >= end || *open != '[')
        return 1;
    close = matching(open, end, '[', ']');
    if (!close || open + 1 == close ||
        !eval_constant_range(executor, open + 1, close, &size) ||
        size.rows != 1 || size.columns != 1 || size.type != MESAGL_GLSL_TYPE_INT ||
        size.data[0] < 1.0f ||
        size.data[0] > MESAGL_MAX_SHADER_ARRAY_ELEMENTS ||
        size.data[0] != (float)safe_integer_value(size.data[0]))
        return 0;
    *array_size = safe_integer_value(size.data[0]);
    *cursor = close + 1;
    return 1;
}

static int function_parameters_match(Executor *executor, const char *start,
                                     const char *end, const MesaGLSLValue *arguments,
                                     int argument_count)
{
    const char *cursor = start;
    int argument = 0;

    while ((cursor = exec_space(cursor, end)) < end) {
        const char *type_name;
        const char *parameter_name;
        size_t type_length;
        size_t parameter_length;
        MesaGLSLValue declared;
        int members;
        int array_size;

        if (!exec_identifier(&cursor, end, &type_name, &type_length))
            return 0;
        while (parameter_qualifier(type_name, type_length))
            if (!exec_identifier(&cursor, end, &type_name, &type_length))
                return 0;
        members = struct_member_count(executor, type_name, type_length);
        if ((!value_type(type_name, type_length, &declared) && !members) ||
            argument >= argument_count)
            return 0;
        exec_identifier(&cursor, end, &parameter_name, &parameter_length);
        if (!parameter_array_size(executor, &cursor, end, &array_size) ||
            (!!array_size != !!arguments[argument].array_size) ||
            (array_size && arguments[argument].array_size != array_size))
            return 0;
        if (members) {
            const MesaGLSLValue *argument_value =
                array_size ? &arguments[argument].array[0] : &arguments[argument];

            if (argument_value->member_count != members ||
                argument_value->struct_type_length != type_length ||
                !argument_value->struct_type_name ||
                strncmp(argument_value->struct_type_name, type_name, type_length))
                return 0;
        } else {
            const MesaGLSLValue *argument_value =
                array_size ? &arguments[argument].array[0] : &arguments[argument];

            if (argument_value->member_count || argument_value->rows != declared.rows ||
                argument_value->columns != declared.columns ||
                (argument_value->type != MESAGL_GLSL_TYPE_UNKNOWN &&
                 argument_value->type != declared.type))
                return 0;
        }
        ++argument;
        cursor = exec_space(cursor, end);
        if (cursor < end && *cursor == ',')
            ++cursor;
        else if (cursor < end)
            return 0;
    }
    return argument == argument_count;
}

static int custom_call(Executor *executor, const char *name, size_t length,
                       const MesaGLSLValue *arguments, int argument_count, MesaGLSLValue *value)
{
    char function_name[MESAGL_MAX_SHADER_IDENTIFIER_LENGTH];
    const char *argument_start[16];
    const char *argument_end[16];
    const char *call_source_end = name + strlen(name);
    const char *call_open = exec_space(name + length, call_source_end);
    const char *call_close;
    const char *source = executor->function_source;
    const char *source_end;
    const char *match;

    {
        ExecLocal structure;
        int member;

        memset(&structure, 0, sizeof(structure));
        if (struct_definition(executor, name, length, &match, &source_end)) {
            if (!initialize_struct_local(executor, name, length, &structure) ||
                structure.member_count != argument_count)
                return 0;
            for (member = 0; member < structure.member_count; ++member)
                structure.members[member] = arguments[member];
            *value = structure.value;
            return 1;
        }
    }

    if (executor->constant_expression_only)
        return 0;

    if (!source || !length || length >= sizeof(function_name) || argument_count > 16 ||
        executor->call_depth >= MESAGL_MAX_SHADER_CALL_DEPTH)
        return 0;
    if (call_open >= call_source_end || *call_open != '(' ||
        !(call_close = matching(call_open, call_source_end, '(', ')')))
        return 0;
    {
        const char *cursor = call_open + 1;
        int argument = 0;

        while (cursor < call_close && argument < argument_count) {
            const char *start = exec_space(cursor, call_close);
            const char *scan = start;
            int parentheses = 0;
            int brackets = 0;

            while (scan < call_close) {
                if (*scan == '(')
                    ++parentheses;
                else if (*scan == ')')
                    --parentheses;
                else if (*scan == '[')
                    ++brackets;
                else if (*scan == ']')
                    --brackets;
                else if (*scan == ',' && !parentheses && !brackets)
                    break;
                ++scan;
            }
            argument_start[argument] = start;
            argument_end[argument] = scan;
            while (argument_end[argument] > start &&
                   (argument_end[argument][-1] == ' ' || argument_end[argument][-1] == '\t' ||
                    argument_end[argument][-1] == '\r' || argument_end[argument][-1] == '\n'))
                --argument_end[argument];
            ++argument;
            cursor = scan < call_close ? scan + 1 : scan;
        }
        if (argument != argument_count)
            return 0;
    }
    memcpy(function_name, name, length);
    function_name[length] = '\0';
    source_end = source + strlen(source);
    match = source;
    while ((match = strstr(match, function_name)) != NULL) {
        const char *after_name;
        const char *parameters_end;
        const char *body_start;
        const char *body_end;
        const char *parameter;
        const char *return_start;
        const char *return_end;
        const char *return_definition_start;
        const char *return_definition_end;
        Executor child;
        MesaGLSLValue declared_return;
        MesaGLSLValue call_result;
        int parameter_mode[16] = {0};
        int parameter_array[16] = {0};
        int parameter_count = 0;
        int returns_void;

        if ((match > source && ((match[-1] >= 'a' && match[-1] <= 'z') ||
                                (match[-1] >= 'A' && match[-1] <= 'Z') ||
                                (match[-1] >= '0' && match[-1] <= '9') || match[-1] == '_')) ||
            strncmp(match, name, length)) {
            match += length;
            continue;
        }
        after_name = exec_space(match + length, source_end);
        if (after_name >= source_end || *after_name != '(' ||
            !(parameters_end = matching(after_name, source_end, '(', ')'))) {
            match += length;
            continue;
        }
        body_start = exec_space(parameters_end + 1, source_end);
        if (body_start >= source_end || *body_start != '{' ||
            !(body_end = matching(body_start, source_end, '{', '}'))) {
            match += length;
            continue;
        }
        if (!function_parameters_match(executor, after_name + 1, parameters_end, arguments,
                                       argument_count)) {
            match += length;
            continue;
        }
        return_end = match;
        while (return_end > source && (return_end[-1] == ' ' || return_end[-1] == '\t' ||
                                       return_end[-1] == '\r' || return_end[-1] == '\n'))
            --return_end;
        return_start = return_end;
        while (return_start > source &&
               ((return_start[-1] >= 'a' && return_start[-1] <= 'z') ||
                (return_start[-1] >= 'A' && return_start[-1] <= 'Z') ||
                (return_start[-1] >= '0' && return_start[-1] <= '9') ||
                return_start[-1] == '_'))
            --return_start;
        if (return_start == return_end)
            return 0;
        returns_void = name_is(return_start, (size_t)(return_end - return_start), "void");
        memset(&declared_return, 0, sizeof(declared_return));
        memset(&call_result, 0, sizeof(call_result));
        if (!returns_void &&
            !value_type(return_start, (size_t)(return_end - return_start),
                        &declared_return) &&
            !struct_definition(executor, return_start,
                               (size_t)(return_end - return_start),
                               &return_definition_start, &return_definition_end))
            return 0;
        if (!returns_void &&
            !range_has_return_statement(body_start + 1, body_end))
            return 0;
        memset(&child, 0, sizeof(child));
        child.lookup = executor->lookup;
        child.call = executor->call;
        child.assign = executor->suppress_side_effects ? suppressed_assign
                                                       : executor->assign;
        child.user = executor->user;
        child.function_source = executor->function_source;
        child.global_owner = executor->global_owner;
        child.call_depth = executor->call_depth + 1;
        child.suppress_side_effects = executor->suppress_side_effects;
        parameter = after_name + 1;
        while ((parameter = exec_space(parameter, parameters_end)) < parameters_end) {
            const char *token;
            size_t token_length;
            MesaGLSLValue declared;
            const char *parameter_name;
            size_t parameter_name_length;
            ExecLocal *local;
            int mode = 0;
            int array_size;
            int parameter_const = 0;

            if (!exec_identifier(&parameter, parameters_end, &token, &token_length))
                return 0;
            while (parameter_qualifier(token, token_length)) {
                if (name_is(token, token_length, "const"))
                    parameter_const = 1;
                else if (name_is(token, token_length, "out"))
                    mode = 1;
                else if (name_is(token, token_length, "inout"))
                    mode = 2;
                if (!exec_identifier(&parameter, parameters_end, &token, &token_length))
                    return 0;
            }
            {
                const char *definition_start;
                const char *definition_end;
                int scalar_or_vector = value_type(token, token_length, &declared);
                int structure = !scalar_or_vector &&
                                struct_definition(&child, token, token_length,
                                                  &definition_start, &definition_end);

                if ((!scalar_or_vector && !structure) ||
                    parameter_count >= argument_count ||
                    child.local_count >= EXEC_MAX_LOCALS)
                    return 0;
                local = &child.locals[child.local_count++];
                memset(local, 0, sizeof(*local));
                local->is_const = parameter_const;
                if (exec_identifier(&parameter, parameters_end, &parameter_name,
                                    &parameter_name_length))
                    memcpy(local->name, parameter_name,
                           parameter_name_length < sizeof(local->name) - 1
                               ? parameter_name_length
                               : sizeof(local->name) - 1);
                parameter_mode[parameter_count] = mode;
                if (!parameter_array_size(executor, &parameter, parameters_end,
                                          &array_size))
                    return 0;
                parameter_array[parameter_count] = array_size;
                if (array_size) {
                    int element;

                    if (arguments[parameter_count].array_size != array_size ||
                        child.array_storage_used + array_size >
                            MESAGL_MAX_SHADER_ARRAY_STORAGE)
                        return 0;
                    local->array_size = array_size;
                    local->array = child.array_storage + child.array_storage_used;
                    child.array_storage_used += array_size;
                    for (element = 0; element < array_size; ++element) {
                        if (structure) {
                            ExecLocal item;

                            memset(&item, 0, sizeof(item));
                            if (!initialize_struct_local(&child, token, token_length, &item))
                                return 0;
                            local->array[element] = item.value;
                            if (mode != 1 &&
                                !copy_value(&local->array[element],
                                            &arguments[parameter_count].array[element]))
                                return 0;
                        } else
                            local->array[element] =
                                mode == 1 ? declared : arguments[parameter_count].array[element];
                    }
                    local->value = structure ? local->array[0] : declared;
                } else if (structure) {
                    int member;

                    if (!initialize_struct_local(&child, token, token_length, local) ||
                        arguments[parameter_count].member_count != local->member_count)
                        return 0;
                    if (mode != 1)
                        for (member = 0; member < local->member_count; ++member)
                            if (!copy_value(
                                    &local->members[member],
                                    &arguments[parameter_count].members[member]))
                                return 0;
                } else
                    local->value = mode == 1 ? declared : arguments[parameter_count];
            }
            ++parameter_count;
            parameter = exec_space(parameter, parameters_end);
            if (parameter < parameters_end && *parameter == ',')
                ++parameter;
            else if (parameter < parameters_end)
                return 0;
        }
        if (parameter_count != argument_count)
            return 0;
        execute_range(&child, body_start + 1, body_end);
        if (child.failed)
            return 0;
        if (name_is(return_start, (size_t)(return_end - return_start), "void")) {
            if (child.have_return_value)
                return 0;
            call_result = scalar(0.0f);
        } else {
            if (child.returned && !child.have_return_value)
                return 0;
            if (!child.have_return_value && declared_return.rows) {
                call_result = declared_return;
            } else if (!child.have_return_value) {
                ExecLocal returned;

                memset(&returned, 0, sizeof(returned));
                if (!initialize_struct_local(
                        executor, return_start,
                        (size_t)(return_end - return_start), &returned))
                    return 0;
                call_result = returned.value;
            } else if (declared_return.rows) {
                if (!copy_value(&declared_return, &child.return_value))
                    return 0;
                call_result = declared_return;
            } else if (child.return_value.type != MESAGL_GLSL_TYPE_STRUCT ||
                       child.return_value.struct_type_length !=
                           (size_t)(return_end - return_start) ||
                       !child.return_value.struct_type_name ||
                       strncmp(child.return_value.struct_type_name, return_start,
                               (size_t)(return_end - return_start))) {
                return 0;
            } else {
                ExecLocal returned;

                memset(&returned, 0, sizeof(returned));
                if (!initialize_struct_local(executor, return_start,
                                             (size_t)(return_end - return_start), &returned) ||
                    !copy_value(&returned.value, &child.return_value))
                    return 0;
                call_result = returned.value;
            }
        }
        for (parameter_count = 0;
             !executor->suppress_side_effects && parameter_count < argument_count;
            ++parameter_count)
            if (parameter_mode[parameter_count]) {
                const char *cursor = argument_start[parameter_count];
                const char *argument_name;
                size_t argument_name_length;

                if (parameter_array[parameter_count]) {
                    const char *array_start = argument_start[parameter_count];
                    const char *array_end = argument_end[parameter_count];
                    int element;

                    for (;;) {
                        const char *close;

                        array_start = exec_space(array_start, array_end);
                        while (array_end > array_start &&
                               (array_end[-1] == ' ' || array_end[-1] == '\t' ||
                                array_end[-1] == '\r' || array_end[-1] == '\n'))
                            --array_end;
                        if (array_start >= array_end || *array_start != '(')
                            break;
                        close = matching(array_start, array_end, '(', ')');
                        if (!close || close != array_end - 1)
                            return 0;
                        ++array_start;
                        array_end = close;
                    }
                    cursor = array_start;
                    if (!exec_identifier(&cursor, array_end,
                                         &argument_name,
                                         &argument_name_length))
                        return 0;
                    if (exec_space(cursor, array_end) != array_end)
                        return 0;
                    for (element = 0; element < parameter_array[parameter_count]; ++element)
                        if (!exec_assign(executor, argument_name, argument_name_length, element,
                                         NULL, 0,
                                         &child.locals[parameter_count].array[element]))
                            return 0;
                    continue;
                }
                if (!exec_assign_expression(
                        executor, argument_start[parameter_count],
                        argument_end[parameter_count],
                        &child.locals[parameter_count].value))
                    return 0;
            }
        *value = call_result;
        return 1;
    }
    return 0;
}

int mesaGLSLExecute(const char *source, MesaGLSLLookupFn lookup, MesaGLSLCallFn call,
                    MesaGLSLAssignFn assign, void *user, int *discarded, const char **error_at)
{
    return mesaGLSLExecuteProgram(NULL, source, lookup, call, assign, user, discarded, error_at);
}

static void execute_global_declarations(Executor *executor, const char *source)
{
    const char *end;
    const char *cursor;
    int brace_depth = 0;
    int parentheses_depth = 0;

    if (!source)
        return;
    end = source + strlen(source);
    cursor = source;
    while ((cursor = exec_space(cursor, end)) < end && !executor->failed) {
        const char *after = cursor;
        const char *name;
        size_t length;

        if (*cursor == '{') {
            ++brace_depth;
            ++cursor;
            continue;
        }
        if (*cursor == '}') {
            if (brace_depth)
                --brace_depth;
            ++cursor;
            continue;
        }
        if (*cursor == '(') {
            ++parentheses_depth;
            ++cursor;
            continue;
        }
        if (*cursor == ')') {
            if (parentheses_depth)
                --parentheses_depth;
            ++cursor;
            continue;
        }
        if (!brace_depth && !parentheses_depth &&
            exec_identifier(&after, end, &name, &length)) {
            const char *type_name = name;
            size_t type_length = length;
            const char *variable;
            size_t variable_length;
            MesaGLSLValue declared;
            const char *definition_start;
            const char *definition_end;
            const char *finish;

            if (name_is(name, length, "struct")) {
                const char *open = exec_space(after, end);
                const char *candidate;
                size_t candidate_length;
                const char *close;

                if (exec_identifier(&open, end, &candidate, &candidate_length)) {
                    (void)candidate;
                    (void)candidate_length;
                    open = exec_space(open, end);
                }
                if (open >= end || *open != '{' ||
                    !(close = matching(open, end, '{', '}'))) {
                    executor->failed = 1;
                    executor->error_at = cursor;
                    return;
                }
                finish = statement_end(close + 1, end);
                if (finish <= close + 1 || finish[-1] != ';') {
                    executor->failed = 1;
                    executor->error_at = cursor;
                    return;
                }
                executor->constant_expression_only = 1;
                execute_range(executor, cursor, finish);
                executor->constant_expression_only = 0;
                cursor = finish;
                continue;
            }
            if (name_is(name, length, "uniform") ||
                name_is(name, length, "varying") ||
                name_is(name, length, "attribute") ||
                name_is(name, length, "precision")) {
                finish = statement_end(cursor, end);
                if (finish <= cursor || finish[-1] != ';') {
                    executor->failed = 1;
                    executor->error_at = cursor;
                    return;
                }
                cursor = finish;
                continue;
            }

            while (parameter_qualifier(type_name, type_length))
                if (!exec_identifier(&after, end, &type_name, &type_length))
                    break;
            if ((!value_type(type_name, type_length, &declared) &&
                 !struct_definition(executor, type_name, type_length,
                                    &definition_start, &definition_end)) ||
                !exec_identifier(&after, end, &variable, &variable_length)) {
                ++cursor;
                continue;
            }
            (void)variable;
            (void)variable_length;
            after = exec_space(after, end);
            if (after < end && *after == '(') {
                ++cursor;
                continue;
            }
            finish = statement_end(cursor, end);

            if (finish <= cursor || finish[-1] != ';') {
                executor->failed = 1;
                executor->error_at = cursor;
                return;
            }
            executor->constant_expression_only = 1;
            execute_range(executor, cursor, finish);
            executor->constant_expression_only = 0;
            cursor = finish;
            continue;
        }
        ++cursor;
    }
    executor->global_count = executor->local_count;
}

int mesaGLSLExecuteProgram(const char *program_source, const char *body, MesaGLSLLookupFn lookup,
                           MesaGLSLCallFn call, MesaGLSLAssignFn assign, void *user,
                           int *discarded, const char **error_at)
{
    Executor executor;
    const char *end;

    if (!body)
        return 0;
    memset(&executor, 0, sizeof(executor));
    executor.lookup = lookup;
    executor.call = call;
    executor.assign = assign;
    executor.user = user;
    executor.function_source = program_source;
    executor.global_owner = &executor;
    execute_global_declarations(&executor, program_source);
    end = body + strlen(body);
    if (!executor.failed)
        execute_range(&executor, body, end);
    if (discarded)
        *discarded = executor.discarded;
    if (error_at)
        *error_at = executor.error_at;
    return !executor.failed;
}
