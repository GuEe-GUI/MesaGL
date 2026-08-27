#include "glsl_preprocessor.h"

#include "mesaGL/config.h"
#include "mesaGL/ntgl.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct Macro {
    const char *name;
    size_t name_length;
    const char *value;
    size_t value_length;
    const char *parameters[MESAGL_MAX_SHADER_MACRO_PARAMETERS];
    size_t parameter_lengths[MESAGL_MAX_SHADER_MACRO_PARAMETERS];
    int parameter_count;
} Macro;

typedef struct Conditional {
    int parent_active;
    int branch_active;
    int branch_taken;
    int else_seen;
} Conditional;

typedef struct SourceMap {
    const char *source;
    const size_t *boundaries;
    int boundary_count;
    size_t anchor_offset;
    long anchor_line;
    long anchor_file;
} SourceMap;

static void logical_position(const SourceMap *map, const char *position,
                             long *line, long *file)
{
    size_t offset = (size_t)(position - map->source);
    size_t start = map->anchor_offset;
    int boundary;

    *file = map->anchor_file;
    for (boundary = 0; boundary < map->boundary_count; ++boundary) {
        size_t current = map->boundaries[boundary];

        if (current < map->anchor_offset || current > offset)
            continue;
        ++*file;
        start = current;
    }
    *line = start == map->anchor_offset ? map->anchor_line : 1;
    while (start < offset) {
        if (map->source[start] == '\n')
            ++*line;
        ++start;
    }
}

static int identifier_start(char character)
{
    return (character >= 'a' && character <= 'z') ||
           (character >= 'A' && character <= 'Z') || character == '_';
}

static int identifier_character(char character)
{
    return identifier_start(character) || (character >= '0' && character <= '9');
}

static const char *preprocessing_number_end(const char *cursor, const char *end)
{
    const char *scan = cursor;

    if (scan >= end ||
        (!(*scan >= '0' && *scan <= '9') &&
         !(*scan == '.' && scan + 1 < end && scan[1] >= '0' && scan[1] <= '9')))
        return cursor;

    ++scan;
    while (scan < end) {
        if (identifier_character(*scan) || *scan == '.') {
            ++scan;
            continue;
        }
        if ((*scan == '+' || *scan == '-') && scan > cursor &&
            (scan[-1] == 'e' || scan[-1] == 'E' || scan[-1] == 'p' || scan[-1] == 'P')) {
            ++scan;
            continue;
        }
        break;
    }
    return scan;
}

static const char *skip_space(const char *cursor, const char *end)
{
    while (cursor < end && (*cursor == ' ' || *cursor == '\t' || *cursor == '\r'))
        ++cursor;
    return cursor;
}

static int line_ended(const char *cursor, const char *end)
{
    return skip_space(cursor, end) == end;
}

static int token(const char **cursor, const char *end, const char **start, size_t *length)
{
    const char *current = skip_space(*cursor, end);

    if (current >= end || !identifier_start(*current))
        return 0;
    *start = current++;
    while (current < end && identifier_character(*current))
        ++current;
    *length = (size_t)(current - *start);
    *cursor = current;
    return 1;
}

static int name_is(const char *name, size_t length, const char *wanted)
{
    return strlen(wanted) == length && !strncmp(name, wanted, length);
}

static int find_macro(const Macro *macros, int macro_count, const char *name, size_t length)
{
    int macro;

    for (macro = macro_count - 1; macro >= 0; --macro)
        if (macros[macro].name_length == length &&
            !strncmp(macros[macro].name, name, length))
            return macro;
    return -1;
}

int mesaGLSLReservedES100Identifier(const char *name, size_t length)
{
    static const char *const words[] = {
        "asm",       "class",     "union",      "enum",       "typedef",
        "template",  "this",      "packed",     "goto",       "switch",
        "default",   "inline",    "noinline",   "volatile",   "public",
        "static",    "extern",    "external",   "interface",  "flat",
        "long",      "short",     "double",     "half",       "fixed",
        "unsigned",  "superp",    "input",      "output",     "hvec2",
        "hvec3",     "hvec4",     "dvec2",      "dvec3",      "dvec4",
        "fvec2",     "fvec3",     "fvec4",      "sampler1D",  "sampler3D",
        "sampler1DShadow", "sampler2DShadow", "sampler2DRect",
        "sampler3DRect", "sampler2DRectShadow", "sizeof", "cast",
        "namespace", "using",
    };
    size_t index;

    for (index = 1; index < length; ++index)
        if (name[index - 1] == '_' && name[index] == '_')
            return 1;
    for (index = 0; index < sizeof(words) / sizeof(words[0]); ++index)
        if (strlen(words[index]) == length && !strncmp(words[index], name, length))
            return 1;
    return 0;
}

static int macro_name_forbidden(const char *name, size_t length)
{
    if ((length >= 3 && !strncmp(name, "GL_", 3)) ||
        name_is(name, length, "defined"))
        return 1;
    return mesaGLSLReservedES100Identifier(name, length);
}

static const char *macro_token_end(const char *cursor)
{
    if (identifier_start(*cursor)) {
        ++cursor;
        while (identifier_character(*cursor))
            ++cursor;
        return cursor;
    }
    if ((*cursor >= '0' && *cursor <= '9') || *cursor == '.') {
        ++cursor;
        while (identifier_character(*cursor) || *cursor == '.')
            ++cursor;
        return cursor;
    }
    if ((cursor[0] == '<' && (cursor[1] == '<' || cursor[1] == '=')) ||
        (cursor[0] == '>' && (cursor[1] == '>' || cursor[1] == '=')) ||
        ((cursor[0] == '=' || cursor[0] == '!' || cursor[0] == '&' || cursor[0] == '|' ||
          cursor[0] == '^' || cursor[0] == '+' || cursor[0] == '-' || cursor[0] == '*' ||
          cursor[0] == '/' || cursor[0] == '%') &&
         (cursor[1] == '=' || cursor[1] == cursor[0])))
        return cursor + 2;
    return cursor + 1;
}

static int macro_replacements_equal(const char *left, const char *left_end,
                                    const char *right, const char *right_end)
{
    for (;;) {
        const char *left_token_end;
        const char *right_token_end;

        left = skip_space(left, left_end);
        right = skip_space(right, right_end);
        if (left == left_end || right == right_end)
            return left == left_end && right == right_end;
        left_token_end = macro_token_end(left);
        right_token_end = macro_token_end(right);
        if (left_token_end - left != right_token_end - right ||
            strncmp(left, right, (size_t)(left_token_end - left)))
            return 0;
        left = left_token_end;
        right = right_token_end;
    }
}

static int macro_parameters_equal(const Macro *left, const Macro *right)
{
    int parameter;

    if (left->parameter_count != right->parameter_count)
        return 0;
    for (parameter = 0; parameter < left->parameter_count; ++parameter)
        if (left->parameter_lengths[parameter] !=
                right->parameter_lengths[parameter] ||
            strncmp(left->parameters[parameter], right->parameters[parameter],
                    left->parameter_lengths[parameter]))
            return 0;
    return 1;
}

static int append(char *output, size_t capacity, size_t *used, const char *text, size_t length)
{
    if (*used + length >= capacity)
        return 0;
    memcpy(output + *used, text, length);
    *used += length;
    output[*used] = '\0';
    return 1;
}

static int expand_text(const char *start, const char *end, const Macro *macros, int macro_count,
                       long logical_line, long logical_file, char *output, size_t capacity,
                       size_t *used, int depth, const SourceMap *source_map);

static int expand_text_internal(const char *start, const char *end, const Macro *macros,
                                int macro_count, long logical_line, long logical_file,
                                char *output, size_t capacity, size_t *used, int *active_macros,
                                int depth, const SourceMap *source_map);

static int expand_function(const Macro *macro, int macro_index,
                           const char *const *argument_start,
                           const char *const *argument_end, const Macro *macros, int macro_count,
                           long logical_line, long logical_file, char *output, size_t capacity,
                           size_t *used, int *active_macros, int depth)
{
    const char *cursor = macro->value;
    const char *end = cursor + macro->value_length;
    char substituted[MESAGL_MAX_SHADER_MACRO_RESULT_SIZE];
    size_t substituted_used = 0;

    substituted[0] = '\0';

    while (cursor < end) {
        if (identifier_start(*cursor)) {
            const char *name = cursor++;
            size_t length;
            int parameter;

            while (cursor < end && identifier_character(*cursor))
                ++cursor;
            length = (size_t)(cursor - name);
            for (parameter = 0; parameter < macro->parameter_count; ++parameter)
                if (macro->parameter_lengths[parameter] == length &&
                    !strncmp(macro->parameters[parameter], name, length))
                    break;
            if (parameter < macro->parameter_count) {
                if (!append(substituted, sizeof(substituted), &substituted_used,
                            argument_start[parameter],
                            (size_t)(argument_end[parameter] - argument_start[parameter])))
                    return 0;
            } else if (!append(substituted, sizeof(substituted), &substituted_used, name, length))
                return 0;
        } else if (!append(substituted, sizeof(substituted), &substituted_used, cursor++, 1))
            return 0;
    }
    active_macros[depth] = macro_index;
    return expand_text_internal(substituted, substituted + substituted_used, macros, macro_count,
                                logical_line, logical_file, output, capacity, used, active_macros,
                                depth + 1, NULL);
}

static int expand_text_internal(const char *start, const char *end, const Macro *macros,
                                int macro_count, long logical_line, long logical_file,
                                char *output, size_t capacity, size_t *used, int *active_macros,
                                int depth, const SourceMap *source_map)
{
    const char *cursor = start;

    while (cursor < end) {
        const char *number_end = preprocessing_number_end(cursor, end);

        if (number_end != cursor) {
            if (!append(output, capacity, used, cursor, (size_t)(number_end - cursor)))
                return 0;
            cursor = number_end;
        } else if (identifier_start(*cursor)) {
            const char *name = cursor++;
            size_t length;
            int macro;

            while (cursor < end && identifier_character(*cursor))
                ++cursor;
            length = (size_t)(cursor - name);
            if (name_is(name, length, "__LINE__") || name_is(name, length, "__FILE__")) {
                char number[24];
                long token_line = logical_line;
                long token_file = logical_file;

                if (source_map)
                    logical_position(source_map, name, &token_line, &token_file);
                int number_length = snprintf(number, sizeof(number), "%ld",
                                             name_is(name, length, "__LINE__") ? token_line
                                                                                : token_file);

                if (number_length < 0 || (size_t)number_length >= sizeof(number) ||
                    !append(output, capacity, used, number, (size_t)number_length))
                    return 0;
                continue;
            }
            macro = find_macro(macros, macro_count, name, length);
            if (macro >= 0) {
                int active_index;
                long token_line = logical_line;
                long token_file = logical_file;

                if (source_map)
                    logical_position(source_map, name, &token_line, &token_file);

                for (active_index = 0; active_index < depth; ++active_index)
                    if (active_macros[active_index] == macro)
                        break;
                if (active_index < depth) {
                    if (!append(output, capacity, used, name, length))
                        return 0;
                    continue;
                }
                if (depth >= MESAGL_MAX_SHADER_MACRO_EXPANSION_DEPTH)
                    return 0;
                if (macros[macro].parameter_count >= 0) {
                    const char *argument_start[MESAGL_MAX_SHADER_MACRO_PARAMETERS];
                    const char *argument_end[MESAGL_MAX_SHADER_MACRO_PARAMETERS];
                    const char *call = skip_space(cursor, end);
                    const char *argument;
                    int argument_count = 0;
                    int parentheses = 0;

                    if (call >= end || *call != '(') {
                        if (!append(output, capacity, used, name, (size_t)(cursor - name)))
                            return 0;
                        continue;
                    }
                    argument = call + 1;
                    if (skip_space(argument, end) < end && *skip_space(argument, end) != ')') {
                        while (argument < end) {
                            const char *scan = argument;

                            if (argument_count >= MESAGL_MAX_SHADER_MACRO_PARAMETERS)
                                return 0;
                            argument_start[argument_count] = skip_space(argument, end);
                            while (scan < end) {
                                if (*scan == '(')
                                    ++parentheses;
                                else if (*scan == ')' && !parentheses)
                                    break;
                                else if (*scan == ')')
                                    --parentheses;
                                else if (*scan == ',' && !parentheses)
                                    break;
                                ++scan;
                            }
                            argument_end[argument_count] = scan;
                            while (argument_end[argument_count] > argument_start[argument_count] &&
                                   (argument_end[argument_count][-1] == ' ' ||
                                    argument_end[argument_count][-1] == '\t'))
                                --argument_end[argument_count];
                            ++argument_count;
                            if (scan >= end)
                                return 0;
                            if (*scan == ')') {
                                cursor = scan + 1;
                                break;
                            }
                            argument = scan + 1;
                        }
                    } else {
                        const char *close = skip_space(argument, end);

                        if (close >= end || *close != ')')
                            return 0;
                        cursor = close + 1;
                    }
                    if (argument_count != macros[macro].parameter_count ||
                        !expand_function(&macros[macro], macro, argument_start, argument_end,
                                         macros, macro_count, token_line, token_file, output,
                                         capacity, used, active_macros, depth))
                        return 0;
                } else {
                    active_macros[depth] = macro;
                    if (!expand_text_internal(macros[macro].value,
                                              macros[macro].value +
                                                  macros[macro].value_length,
                                              macros, macro_count, token_line, token_file,
                                              output, capacity, used, active_macros, depth + 1,
                                              NULL))
                        return 0;
                }
            } else if (!append(output, capacity, used, name, (size_t)(cursor - name)))
                return 0;
        } else if (!append(output, capacity, used, cursor++, 1))
            return 0;
    }
    return 1;
}

static int expand_text(const char *start, const char *end, const Macro *macros, int macro_count,
                       long logical_line, long logical_file, char *output, size_t capacity,
                       size_t *used, int depth, const SourceMap *source_map)
{
    int active_macros[MESAGL_MAX_SHADER_MACRO_EXPANSION_DEPTH];

    (void)depth;
    return expand_text_internal(start, end, macros, macro_count, logical_line, logical_file,
                                output, capacity, used, active_macros, 0,
                                source_map);
}

static int parse_line_expression(const char **cursor, const char *end,
                                 const Macro *macros, int macro_count,
                                 long *value);

static int parse_line_directive(const char *cursor, const char *end, const Macro *macros,
                                int macro_count, long current_line, long current_file,
                                long *next_line, long *next_file,
                                const SourceMap *source_map)
{
    char expanded[MESAGL_MAX_SHADER_MACRO_RESULT_SIZE];
    size_t used = 0;
    long line_number;
    long file_number = current_file;

    expanded[0] = '\0';
    if (!expand_text(cursor, end, macros, macro_count, current_line, current_file, expanded,
                     sizeof(expanded), &used, 0, source_map))
        return 0;
    cursor = skip_space(expanded, expanded + used);
    if (!parse_line_expression(&cursor, expanded + used, macros, macro_count,
                               &line_number))
        return 0;
    cursor = skip_space(cursor, expanded + used);
    if (cursor < expanded + used) {
        if (!parse_line_expression(&cursor, expanded + used, macros,
                                   macro_count, &file_number))
            return 0;
        cursor = skip_space(cursor, expanded + used);
    }
    if (cursor != expanded + used)
        return 0;
    *next_line = line_number;
    *next_file = file_number;
    return 1;
}

static int parse_version_directive(const char *cursor, const char *end)
{
    char *number_end;
    long version;

    cursor = skip_space(cursor, end);
    version = strtol(cursor, &number_end, 10);
    return number_end != cursor && version == 100 && skip_space(number_end, end) == end;
}

static int parse_extension_directive(const char *cursor, const char *end,
                                     unsigned int *enabled_extensions,
                                     int *unsupported_required,
                                     int *unsupported_warning)
{
    const char *extension;
    const char *behavior;
    size_t extension_length;
    size_t behavior_length;
    int known;

    if (!token(&cursor, end, &extension, &extension_length))
        return 0;
    cursor = skip_space(cursor, end);
    if (cursor >= end || *cursor != ':')
        return 0;
    ++cursor;
    if (!token(&cursor, end, &behavior, &behavior_length) || !line_ended(cursor, end))
        return 0;
    if (!name_is(behavior, behavior_length, "require") &&
        !name_is(behavior, behavior_length, "enable") &&
        !name_is(behavior, behavior_length, "warn") &&
        !name_is(behavior, behavior_length, "disable"))
        return 0;
    if (name_is(extension, extension_length, "all")) {
        if (!name_is(behavior, behavior_length, "warn") &&
            !name_is(behavior, behavior_length, "disable"))
            return 0;
        if (name_is(behavior, behavior_length, "disable")) {
            *enabled_extensions &=
                ~MESAGL_GLSL_EXTENSION_STANDARD_DERIVATIVES;
            *enabled_extensions &=
                ~MESAGL_GLSL_EXTENSION_STANDARD_DERIVATIVES_WARN;
        }
        else {
            *enabled_extensions |= MESAGL_GLSL_EXTENSION_STANDARD_DERIVATIVES;
            *enabled_extensions |=
                MESAGL_GLSL_EXTENSION_STANDARD_DERIVATIVES_WARN;
        }
        return 1;
    }
    known = name_is(extension, extension_length, "GL_OES_standard_derivatives");
    if (!known) {
        *unsupported_required = name_is(behavior, behavior_length, "require");
        *unsupported_warning = !*unsupported_required;
        return 1;
    }
    if (name_is(behavior, behavior_length, "require") ||
        name_is(behavior, behavior_length, "enable")) {
        *enabled_extensions |= MESAGL_GLSL_EXTENSION_STANDARD_DERIVATIVES;
        *enabled_extensions &=
            ~MESAGL_GLSL_EXTENSION_STANDARD_DERIVATIVES_WARN;
    } else if (name_is(behavior, behavior_length, "warn")) {
        *enabled_extensions |= MESAGL_GLSL_EXTENSION_STANDARD_DERIVATIVES;
        *enabled_extensions |=
            MESAGL_GLSL_EXTENSION_STANDARD_DERIVATIVES_WARN;
    } else {
        *enabled_extensions &= ~MESAGL_GLSL_EXTENSION_STANDARD_DERIVATIVES;
        *enabled_extensions &=
            ~MESAGL_GLSL_EXTENSION_STANDARD_DERIVATIVES_WARN;
    }
    return 1;
}

static void set_log(char *log, size_t log_size, const char *message);

static int parse_pragma_directive(const char *cursor, const char *end,
                                  unsigned int *preprocessor_state,
                                  int inside_function, char *log,
                                  size_t log_size)
{
    const char *name;
    size_t length;
    int standard;
    int control;

    if (!token(&cursor, end, &name, &length))
        return 1;
    standard = name_is(name, length, "STDGL");
    control = name_is(name, length, "optimize") ||
              name_is(name, length, "debug");
    if (!standard && !control)
        return 1;
    if (inside_function) {
        set_log(log, log_size,
                "recognized shader pragma is only valid outside functions");
        return 0;
    }
    if (standard &&
        (!token(&cursor, end, &name, &length) ||
         !name_is(name, length, "invariant"))) {
        set_log(log, log_size, "invalid #pragma STDGL directive");
        return 0;
    }
    cursor = skip_space(cursor, end);
    if (cursor >= end || *cursor++ != '(' ||
        !token(&cursor, end, &name, &length) ||
        (standard ? !name_is(name, length, "all")
                  : (!name_is(name, length, "on") &&
                     !name_is(name, length, "off")))) {
        set_log(log, log_size, "invalid recognized shader pragma");
        return 0;
    }
    cursor = skip_space(cursor, end);
    if (cursor >= end || *cursor++ != ')' || !line_ended(cursor, end)) {
        set_log(log, log_size, "invalid recognized shader pragma");
        return 0;
    }
    if (standard)
        *preprocessor_state |= MESAGL_GLSL_PRAGMA_INVARIANT_ALL;
    return 1;
}

static void update_function_scope(const char *start, const char *end,
                                  int *brace_depth, int *function_depth,
                                  char *last_top_level_token)
{
    const char *cursor;

    for (cursor = start; cursor < end; ++cursor) {
        char character = *cursor;

        if (character == ' ' || character == '\t' || character == '\r' ||
            character == '\n')
            continue;
        if (character == '{') {
            if (!*brace_depth && *last_top_level_token == ')')
                *function_depth = 1;
            ++*brace_depth;
            continue;
        }
        if (character == '}') {
            if (*brace_depth > 0)
                --*brace_depth;
            if (*function_depth && *brace_depth < *function_depth)
                *function_depth = 0;
            if (!*brace_depth)
                *last_top_level_token = '}';
            continue;
        }
        if (!*brace_depth)
            *last_top_level_token = character;
    }
}

typedef struct ConditionParser {
    const char *cursor;
    const char *end;
    const Macro *macros;
    int macro_count;
    int depth;
    int evaluate;
    int failed;
    long logical_line;
    long logical_file;
} ConditionParser;

static long condition_or(ConditionParser *parser);

static int condition_accept(ConditionParser *parser, const char *text)
{
    size_t length = strlen(text);

    parser->cursor = skip_space(parser->cursor, parser->end);
    if ((size_t)(parser->end - parser->cursor) < length ||
        strncmp(parser->cursor, text, length))
        return 0;
    parser->cursor += length;
    return 1;
}

static long condition_primary(ConditionParser *parser)
{
    const char *name;
    size_t length;
    char *number_end;
    long value;

    parser->cursor = skip_space(parser->cursor, parser->end);
    if (condition_accept(parser, "(")) {
        value = condition_or(parser);
        if (!condition_accept(parser, ")"))
            parser->failed = 1;
        return value;
    }
    if ((size_t)(parser->end - parser->cursor) >= 7 &&
        !strncmp(parser->cursor, "defined", 7) &&
        (parser->cursor + 7 == parser->end || !identifier_character(parser->cursor[7]))) {
        int parentheses;

        parser->cursor = skip_space(parser->cursor + 7, parser->end);
        parentheses = condition_accept(parser, "(");
        if (!token(&parser->cursor, parser->end, &name, &length)) {
            parser->failed = 1;
            return 0;
        }
        if (parentheses && !condition_accept(parser, ")"))
            parser->failed = 1;
        return find_macro(parser->macros, parser->macro_count, name, length) >= 0;
    }
    if (token(&parser->cursor, parser->end, &name, &length)) {
        int macro = find_macro(parser->macros, parser->macro_count, name, length);

        if (name_is(name, length, "__LINE__"))
            return parser->logical_line;
        if (name_is(name, length, "__FILE__"))
            return parser->logical_file;

        if (macro >= 0 && parser->macros[macro].parameter_count < 0 &&
            parser->depth < MESAGL_MAX_SHADER_MACRO_EXPANSION_DEPTH) {
            ConditionParser nested = {parser->macros[macro].value,
                                      parser->macros[macro].value +
                                      parser->macros[macro].value_length,
                                      parser->macros, parser->macro_count, parser->depth + 1,
                                      parser->evaluate, 0, parser->logical_line,
                                      parser->logical_file};

            value = condition_or(&nested);
            nested.cursor = skip_space(nested.cursor, nested.end);
            if (nested.failed || nested.cursor != nested.end)
                parser->failed = 1;
            return value;
        }
        if (parser->evaluate)
            parser->failed = 1;
        return 0;
    }
    value = strtol(parser->cursor, &number_end, 0);
    if (number_end == parser->cursor || number_end > parser->end) {
        parser->failed = 1;
        return 0;
    }
    parser->cursor = number_end;
    while (parser->cursor < parser->end &&
           (*parser->cursor == 'u' || *parser->cursor == 'U' || *parser->cursor == 'l' ||
            *parser->cursor == 'L'))
        ++parser->cursor;
    return value;
}

static long condition_unary(ConditionParser *parser)
{
    if (condition_accept(parser, "!"))
        return !condition_unary(parser);
    if (condition_accept(parser, "~"))
        return ~condition_unary(parser);
    if (condition_accept(parser, "+"))
        return condition_unary(parser);
    if (condition_accept(parser, "-"))
        return -condition_unary(parser);
    return condition_primary(parser);
}

static long condition_product(ConditionParser *parser)
{
    long value = condition_unary(parser);

    for (;;) {
        if (condition_accept(parser, "*"))
            value *= condition_unary(parser);
        else if (condition_accept(parser, "/")) {
            long divisor = condition_unary(parser);

            if (!divisor && parser->evaluate)
                parser->failed = 1;
            else if (divisor)
                value /= divisor;
        } else if (condition_accept(parser, "%")) {
            long divisor = condition_unary(parser);

            if (!divisor && parser->evaluate)
                parser->failed = 1;
            else if (divisor)
                value %= divisor;
        } else
            return value;
    }
}

static long condition_sum(ConditionParser *parser)
{
    long value = condition_product(parser);

    for (;;) {
        if (condition_accept(parser, "+"))
            value += condition_product(parser);
        else if (condition_accept(parser, "-"))
            value -= condition_product(parser);
        else
            return value;
    }
}

static long condition_shift(ConditionParser *parser)
{
    long value = condition_sum(parser);

    for (;;) {
        if (condition_accept(parser, "<<"))
            value <<= condition_sum(parser);
        else if (condition_accept(parser, ">>"))
            value >>= condition_sum(parser);
        else
            return value;
    }
}

static long condition_relation(ConditionParser *parser)
{
    long value = condition_shift(parser);

    for (;;) {
        if (condition_accept(parser, "<="))
            value = value <= condition_shift(parser);
        else if (condition_accept(parser, ">="))
            value = value >= condition_shift(parser);
        else if (condition_accept(parser, "<"))
            value = value < condition_shift(parser);
        else if (condition_accept(parser, ">"))
            value = value > condition_shift(parser);
        else
            return value;
    }
}

static long condition_equality(ConditionParser *parser)
{
    long value = condition_relation(parser);

    for (;;) {
        if (condition_accept(parser, "=="))
            value = value == condition_relation(parser);
        else if (condition_accept(parser, "!="))
            value = value != condition_relation(parser);
        else
            return value;
    }
}

static long condition_bitwise_and(ConditionParser *parser)
{
    long value = condition_equality(parser);

    while (!strncmp(skip_space(parser->cursor, parser->end), "&", 1) &&
           strncmp(skip_space(parser->cursor, parser->end), "&&", 2)) {
        condition_accept(parser, "&");
        value &= condition_equality(parser);
    }
    return value;
}

static long condition_bitwise_xor(ConditionParser *parser)
{
    long value = condition_bitwise_and(parser);

    while (condition_accept(parser, "^"))
        value ^= condition_bitwise_and(parser);
    return value;
}

static long condition_bitwise_or(ConditionParser *parser)
{
    long value = condition_bitwise_xor(parser);

    while (!strncmp(skip_space(parser->cursor, parser->end), "|", 1) &&
           strncmp(skip_space(parser->cursor, parser->end), "||", 2)) {
        condition_accept(parser, "|");
        value |= condition_bitwise_xor(parser);
    }
    return value;
}

static long condition_and(ConditionParser *parser)
{
    long value = condition_bitwise_or(parser);

    while (condition_accept(parser, "&&")) {
        int evaluate = parser->evaluate;
        long right;

        if (!value)
            parser->evaluate = 0;
        right = condition_bitwise_or(parser);
        parser->evaluate = evaluate;
        value = value && right;
    }
    return value;
}

static long condition_or(ConditionParser *parser)
{
    long value = condition_and(parser);

    while (condition_accept(parser, "||")) {
        int evaluate = parser->evaluate;
        long right;

        if (value)
            parser->evaluate = 0;
        right = condition_and(parser);
        parser->evaluate = evaluate;
        value = value || right;
    }
    return value;
}

static int condition_value(const char *cursor, const char *end, const Macro *macros,
                           int macro_count, long logical_line,
                           long logical_file, int *valid)
{
    ConditionParser parser = {cursor, end, macros, macro_count, 0, 1, 0,
                              logical_line, logical_file};
    long value = condition_or(&parser);

    parser.cursor = skip_space(parser.cursor, parser.end);
    *valid = !parser.failed && parser.cursor == parser.end;
    return value != 0;
}

static int parse_line_expression(const char **cursor, const char *end,
                                 const Macro *macros, int macro_count,
                                 long *value)
{
    ConditionParser parser = {*cursor, end, macros, macro_count, 0, 1, 0,
                              0, 0};

    *value = condition_or(&parser);
    if (parser.failed || parser.cursor == *cursor || *value < 0 ||
        *value > INT_MAX)
        return 0;
    *cursor = parser.cursor;
    return 1;
}

static void set_log(char *log, size_t log_size, const char *message)
{
    size_t length;

    if (!log || !log_size)
        return;
    length = strlen(message);
    if (length >= log_size)
        length = log_size - 1;
    memcpy(log, message, length);
    log[length] = '\0';
}

static void prefix_log_location(char *log, size_t log_size, long file,
                                long line)
{
    char message[128];

    if (!log || !log_size || !log[0])
        return;
    snprintf(message, sizeof(message), "%ld:%ld: %s", file, line, log);
    set_log(log, log_size, message);
}

static void raw_source_position(const char *source, size_t source_length,
                                const size_t *boundaries, int boundary_count,
                                size_t offset, long *line, long *file)
{
    size_t start = 0;
    size_t cursor;
    int boundary;

    if (offset > source_length)
        offset = source_length;
    *file = 0;
    for (boundary = 0; boundary < boundary_count; ++boundary) {
        if (boundaries[boundary] > offset)
            break;
        ++*file;
        start = boundaries[boundary];
    }
    *line = 1;
    for (cursor = start; cursor < offset; ++cursor) {
        if (source[cursor] == '\n' || source[cursor] == '\r') {
            char first = source[cursor];

            ++*line;
            if (cursor + 1 < offset &&
                ((first == '\r' && source[cursor + 1] == '\n') ||
                 (first == '\n' && source[cursor + 1] == '\r')))
                ++cursor;
        }
    }
}

static void set_error_directive_log(char *log, size_t log_size,
                                    const char *cursor, const char *end)
{
    size_t prefix_length = sizeof("#error: ") - 1;
    size_t token_length;

    if (!log || !log_size)
        return;
    cursor = skip_space(cursor, end);
    token_length = (size_t)(end - cursor);
    if (prefix_length >= log_size) {
        set_log(log, log_size, "#error");
        return;
    }
    if (token_length > log_size - prefix_length - 1)
        token_length = log_size - prefix_length - 1;
    memcpy(log, "#error: ", prefix_length);
    memcpy(log + prefix_length, cursor, token_length);
    log[prefix_length + token_length] = '\0';
}

static char *remove_comments(const char *source, int *unterminated)
{
    size_t length = strlen(source);
    char *cleaned = (char *)ntglAlloc(length + 1);
    size_t index;
    int block = 0;

    if (!cleaned)
        return NULL;
    for (index = 0; index < length; ++index) {
        if (!block && source[index] == '/' && index + 1 < length &&
            source[index + 1] == '/') {
            cleaned[index++] = ' ';
            cleaned[index] = ' ';
            while (index + 1 < length && source[index + 1] != '\n' &&
                   source[index + 1] != '\r')
                cleaned[++index] = ' ';
        } else if (!block && source[index] == '/' && index + 1 < length &&
                   source[index + 1] == '*') {
            block = 1;
            cleaned[index++] = ' ';
            cleaned[index] = ' ';
        } else if (block && source[index] == '*' && index + 1 < length &&
                   source[index + 1] == '/') {
            cleaned[index++] = ' ';
            cleaned[index] = ' ';
            block = 0;
        } else if (block && source[index] != '\n' && source[index] != '\r') {
            cleaned[index] = ' ';
        } else {
            cleaned[index] = source[index];
        }
    }
    cleaned[length] = '\0';
    *unterminated = block;
    return cleaned;
}

static char *normalize_newlines(const char *source, size_t length,
                                const size_t *source_boundaries,
                                int source_boundary_count,
                                size_t **normalized_boundaries)
{
    char *joined = (char *)ntglAlloc(length + 1);
    size_t *boundaries = NULL;
    size_t input = 0;
    size_t output = 0;
    int boundary = 0;

    if (!joined)
        return NULL;
    if (source_boundary_count > 0) {
        boundaries = (size_t *)ntglAlloc((size_t)source_boundary_count *
                                         sizeof(*boundaries));
        if (!boundaries) {
            ntglFree(joined);
            return NULL;
        }
    }
    while (input < length) {
        while (boundary < source_boundary_count &&
               source_boundaries[boundary] == input)
            boundaries[boundary++] = output;
        if (source[input] == '\n' || source[input] == '\r') {
            char first = source[input++];

            if (input < length &&
                ((first == '\r' && source[input] == '\n') ||
                 (first == '\n' && source[input] == '\r'))) {
                while (boundary < source_boundary_count &&
                       source_boundaries[boundary] == input)
                    boundaries[boundary++] = output;
                ++input;
            }
            joined[output++] = '\n';
            continue;
        }
        joined[output++] = source[input++];
    }
    while (boundary < source_boundary_count &&
           source_boundaries[boundary] == input)
        boundaries[boundary++] = output;
    if (boundary != source_boundary_count) {
        ntglFree(boundaries);
        ntglFree(joined);
        return NULL;
    }
    joined[output] = '\0';
    *normalized_boundaries = boundaries;
    return joined;
}

char *mesaGLSLPreprocessSource(const char *source, size_t source_length,
                               const size_t *source_boundaries,
                               int source_boundary_count,
                               unsigned int *enabled_extensions, char *log,
                               size_t log_size)
{
    Macro macros[MESAGL_MAX_SHADER_MACROS];
    Conditional conditions[MESAGL_MAX_SHADER_CONDITIONAL_DEPTH];
    char *output;
    size_t used = 0;
    int macro_count = 3;
    int depth = 0;
    int version_allowed = 1;
    int extension_allowed = 1;
    long logical_line = 1;
    long logical_file = 0;
    size_t *normalized_boundaries = NULL;
    SourceMap source_map;
    int brace_depth = 0;
    int function_depth = 0;
    char last_top_level_token = 0;
    char *joined;
    char *cleaned;
    int unterminated_comment = 0;
    const char *line = NULL;

    if (!source || source_boundary_count < 0 ||
        (source_boundary_count && !source_boundaries))
        return NULL;
    if (enabled_extensions)
        *enabled_extensions = 0;
    {
        const char *invalid = (const char *)memchr(source, '\0', source_length);

        if (invalid) {
            raw_source_position(source, source_length, source_boundaries,
                                source_boundary_count,
                                (size_t)(invalid - source), &logical_line,
                                &logical_file);
            set_log(log, log_size,
                    "NUL is not a GLSL ES 1.00 source character");
            prefix_log_location(log, log_size, logical_file, logical_line);
            return NULL;
        }
    }
    {
        const char *invalid =
            (const char *)memchr(source, '\\', source_length);

        if (invalid) {
            raw_source_position(source, source_length, source_boundaries,
                                source_boundary_count,
                                (size_t)(invalid - source), &logical_line,
                                &logical_file);
            set_log(log, log_size,
                    "backslash is not a GLSL ES 1.00 source character");
            prefix_log_location(log, log_size, logical_file, logical_line);
            return NULL;
        }
    }
    joined = normalize_newlines(source, source_length, source_boundaries,
                                source_boundary_count,
                                &normalized_boundaries);
    if (!joined) {
        set_log(log, log_size, "out of memory while joining shader source lines");
        return NULL;
    }
    cleaned = remove_comments(joined, &unterminated_comment);
    ntglFree(joined);
    if (!cleaned) {
        set_log(log, log_size, "out of memory while removing shader comments");
        ntglFree(normalized_boundaries);
        return NULL;
    }
    if (unterminated_comment) {
        set_log(log, log_size, "unterminated shader block comment");
        ntglFree(normalized_boundaries);
        ntglFree(cleaned);
        return NULL;
    }
    memset(macros, 0, sizeof(macros));
    macros[0].name = "GL_ES";
    macros[0].name_length = 5;
    macros[0].value = "1";
    macros[0].value_length = 1;
    macros[0].parameter_count = -1;
    macros[1].name = "__VERSION__";
    macros[1].name_length = 11;
    macros[1].value = "100";
    macros[1].value_length = 3;
    macros[2].name = "GL_OES_standard_derivatives";
    macros[2].name_length = 27;
    macros[2].value = "1";
    macros[2].value_length = 1;
    macros[1].parameter_count = -1;
    macros[2].parameter_count = -1;
    output = (char *)ntglAlloc(MESAGL_MAX_PREPROCESSED_SHADER_SIZE);
    if (!output) {
        set_log(log, log_size, "out of memory while preprocessing shader");
        ntglFree(normalized_boundaries);
        ntglFree(cleaned);
        return NULL;
    }
    source_map.source = cleaned;
    source_map.boundaries = normalized_boundaries;
    source_map.boundary_count = source_boundary_count;
    source_map.anchor_offset = 0;
    source_map.anchor_line = 1;
    source_map.anchor_file = 0;
    output[0] = '\0';
    for (line = cleaned; *line;) {
        const char *end = strchr(line, '\n');
        const char *cursor;
        const char *directive;
        size_t directive_length;
        int active = !depth || conditions[depth - 1].branch_active;

        if (!end)
            end = line + strlen(line);
        logical_position(&source_map, line, &logical_line, &logical_file);
        cursor = skip_space(line, end);
        if (cursor >= end || *cursor != '#') {
            if (active) {
                size_t line_output_start = used;

                if (!expand_text(line, end, macros, macro_count, logical_line,
                                 logical_file, output,
                                 MESAGL_MAX_PREPROCESSED_SHADER_SIZE,
                                 &used, 0, &source_map) ||
                    !append(output, MESAGL_MAX_PREPROCESSED_SHADER_SIZE,
                            &used, "\n", 1))
                    goto expansion_failed;
                update_function_scope(output + line_output_start, output + used,
                                      &brace_depth, &function_depth,
                                      &last_top_level_token);
            }
            if (active && cursor < end)
                version_allowed = 0;
            if (active && cursor < end)
                extension_allowed = 0;
            line = *end ? end + 1 : end;
            continue;
        }
        ++cursor;
        if (line_ended(cursor, end)) {
            version_allowed = 0;
            line = *end ? end + 1 : end;
            continue;
        }
        if (!token(&cursor, end, &directive, &directive_length))
            goto invalid_directive;
        if (name_is(directive, directive_length, "if") ||
            name_is(directive, directive_length, "ifdef") ||
            name_is(directive, directive_length, "ifndef")) {
            int parent_active = active;
            int result;
            int valid = 1;

            if (depth >= MESAGL_MAX_SHADER_CONDITIONAL_DEPTH) {
                set_log(log, log_size, "shader conditional nesting limit exceeded");
                goto failure;
            }
            if (name_is(directive, directive_length, "if"))
                result = condition_value(cursor, end, macros, macro_count,
                                         logical_line, logical_file, &valid);
            else {
                const char *name;
                size_t length;

                if (!token(&cursor, end, &name, &length))
                    goto invalid_directive;
                if (!line_ended(cursor, end))
                    goto invalid_directive;
                result = find_macro(macros, macro_count, name, length) >= 0;
                if (name_is(directive, directive_length, "ifndef"))
                    result = !result;
            }
            if (!valid) {
                set_log(log, log_size, "invalid shader preprocessor expression");
                goto failure;
            }
            conditions[depth].parent_active = parent_active;
            conditions[depth].branch_active = parent_active && result;
            conditions[depth].branch_taken = result;
            conditions[depth].else_seen = 0;
            ++depth;
        } else if (name_is(directive, directive_length, "else")) {
            Conditional *condition;

            if (!depth || conditions[depth - 1].else_seen || !line_ended(cursor, end))
                goto invalid_directive;
            condition = &conditions[depth - 1];
            condition->else_seen = 1;
            condition->branch_active = condition->parent_active && !condition->branch_taken;
            condition->branch_taken = 1;
        } else if (name_is(directive, directive_length, "elif")) {
            Conditional *condition;
            int result;
            int valid;

            if (!depth || conditions[depth - 1].else_seen)
                goto invalid_directive;
            condition = &conditions[depth - 1];
            result = condition_value(cursor, end, macros, macro_count,
                                     logical_line, logical_file, &valid);
            if (!valid) {
                set_log(log, log_size, "invalid shader preprocessor expression");
                goto failure;
            }
            condition->branch_active =
                condition->parent_active && !condition->branch_taken && result;
            condition->branch_taken = condition->branch_taken || result;
        } else if (name_is(directive, directive_length, "endif")) {
            if (!depth || !line_ended(cursor, end))
                goto invalid_directive;
            --depth;
        } else if (active && name_is(directive, directive_length, "define")) {
            const char *name;
            size_t length;
            Macro parsed;
            int existing;

            if (!token(&cursor, end, &name, &length))
                goto invalid_directive;
            if (macro_name_forbidden(name, length)) {
                set_log(log, log_size, "reserved shader macro name cannot be defined");
                goto failure;
            }
            memset(&parsed, 0, sizeof(parsed));
            parsed.name = name;
            parsed.name_length = length;
            parsed.parameter_count = -1;
            if (cursor < end && *cursor == '(') {
                ++cursor;
                parsed.parameter_count = 0;
                cursor = skip_space(cursor, end);
                if (cursor < end && *cursor != ')') {
                    for (;;) {
                        const char *parameter_name;
                        size_t parameter_length;
                        int parameter = parsed.parameter_count;
                        int previous;

                        if (parameter >= MESAGL_MAX_SHADER_MACRO_PARAMETERS ||
                            !token(&cursor, end, &parameter_name, &parameter_length))
                            goto invalid_directive;
                        if (mesaGLSLReservedES100Identifier(parameter_name,
                                                            parameter_length)) {
                            set_log(log, log_size,
                                    "reserved shader macro parameter");
                            goto failure;
                        }
                        for (previous = 0; previous < parameter; ++previous)
                            if (parsed.parameter_lengths[previous] == parameter_length &&
                                !strncmp(parsed.parameters[previous], parameter_name,
                                         parameter_length)) {
                                set_log(log, log_size, "duplicate shader macro parameter");
                                goto failure;
                            }
                        parsed.parameters[parameter] = parameter_name;
                        parsed.parameter_lengths[parameter] = parameter_length;
                        ++parsed.parameter_count;
                        cursor = skip_space(cursor, end);
                        if (cursor < end && *cursor == ',') {
                            cursor = skip_space(cursor + 1, end);
                            continue;
                        }
                        break;
                    }
                }
                if (cursor >= end || *cursor != ')')
                    goto invalid_directive;
                ++cursor;
            }
            cursor = skip_space(cursor, end);
            if (memchr(cursor, '#', (size_t)(end - cursor))) {
                set_log(log, log_size,
                        "macro token pasting and stringification are illegal in GLES");
                goto failure;
            }
            parsed.value = cursor;
            parsed.value_length = (size_t)(end - cursor);
            existing = find_macro(macros, macro_count, name, length);
            if (existing >= 0) {
                if (!macro_parameters_equal(&macros[existing], &parsed) ||
                    !macro_replacements_equal(
                        macros[existing].value,
                        macros[existing].value + macros[existing].value_length,
                        parsed.value, parsed.value + parsed.value_length)) {
                    set_log(log, log_size, "shader macro redefined with different contents");
                    goto failure;
                }
            } else {
                if (macro_count >= MESAGL_MAX_SHADER_MACROS) {
                    set_log(log, log_size, "shader macro limit exceeded");
                    goto failure;
                }
                macros[macro_count++] = parsed;
            }
        } else if (active && name_is(directive, directive_length, "undef")) {
            const char *name;
            size_t length;
            int macro;

            if (!token(&cursor, end, &name, &length))
                goto invalid_directive;
            if (!line_ended(cursor, end))
                goto invalid_directive;
            if (macro_name_forbidden(name, length)) {
                set_log(log, log_size, "reserved shader macro name cannot be undefined");
                goto failure;
            }
            macro = find_macro(macros, macro_count, name, length);
            if (macro >= 0)
                macros[macro] = macros[--macro_count];
        } else if (name_is(directive, directive_length, "version")) {
            if (!active || !version_allowed || !parse_version_directive(cursor, end)) {
                set_log(log, log_size, "only GLSL ES version 100 is supported");
                goto failure;
            }
        } else if (name_is(directive, directive_length, "line")) {
            long next_line = logical_line + 1;
            long next_file = logical_file;

            if (active &&
                !parse_line_directive(cursor, end, macros, macro_count, logical_line,
                                      logical_file, &next_line, &next_file,
                                      &source_map)) {
                set_log(log, log_size, "invalid #line directive");
                goto failure;
            }
            source_map.anchor_offset =
                (size_t)((*end ? end + 1 : end) - cleaned);
            source_map.anchor_line = next_line;
            source_map.anchor_file = next_file;
        } else if (name_is(directive, directive_length, "extension")) {
            int unsupported_required = 0;
            int unsupported_warning = 0;
            unsigned int ignored_extensions = 0;
            unsigned int *extensions = enabled_extensions ? enabled_extensions
                                                          : &ignored_extensions;

            if (active &&
                (!extension_allowed ||
                 !parse_extension_directive(cursor, end, extensions,
                                            &unsupported_required,
                                            &unsupported_warning)))
                goto invalid_directive;
            if (active && unsupported_required) {
                set_log(log, log_size, "required shader extension is not supported");
                goto failure;
            }
            if (active && unsupported_warning)
                set_log(log, log_size,
                        "warning: shader extension is not supported");
            if (active && unsupported_warning)
                prefix_log_location(log, log_size, logical_file,
                                    logical_line);
        } else if (name_is(directive, directive_length, "error")) {
            if (active) {
                set_error_directive_log(log, log_size, cursor, end);
                goto failure;
            }
        } else if (name_is(directive, directive_length, "pragma")) {
            unsigned int ignored_state = 0;
            unsigned int *state = enabled_extensions ? enabled_extensions : &ignored_state;

            if (active &&
                !parse_pragma_directive(cursor, end, state,
                                        function_depth != 0, log, log_size))
                goto failure;
        } else if (active)
            goto invalid_directive;
        version_allowed = 0;
        line = *end ? end + 1 : end;
    }
    if (depth) {
        set_log(log, log_size, "unterminated shader conditional");
        goto failure;
    }
    ntglFree(normalized_boundaries);
    ntglFree(cleaned);
    return output;

expansion_failed:
    set_log(log, log_size, "shader macro expansion failed or exceeded a configured limit");
    goto failure;
invalid_directive:
    set_log(log, log_size, "invalid or unsupported shader preprocessor directive");
failure:
    if (line) {
        logical_position(&source_map, line, &logical_line, &logical_file);
        prefix_log_location(log, log_size, logical_file, logical_line);
    }
    ntglFree(normalized_boundaries);
    ntglFree(cleaned);
    ntglFree(output);
    return NULL;
}

char *mesaGLSLPreprocess(const char *source, unsigned int *enabled_extensions,
                         char *log, size_t log_size)
{
    return source ? mesaGLSLPreprocessSource(
                        source, strlen(source), NULL, 0,
                        enabled_extensions, log, log_size)
                  : NULL;
}
