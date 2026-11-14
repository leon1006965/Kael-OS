#include "net.h"

/* Small, safe interpreter for an educational C subset. It does not execute
 * machine code; it supports main(), integers, char arrays/string variables,
 * basic expressions, printf/puts/putchar, and common string functions. */

#define C_RUN_VARS 16
#define C_RUN_ARGS 12
#define C_STRING_CAP 128

typedef struct {
    char name[16];
    int value;
    int is_string;
    char string[C_STRING_CAP];
} cvar_t;

typedef struct {
    int is_string;
    int number;
    char string[128];
} c_arg_t;

typedef struct {
    const char* source;
    const char* p;
    const char* end;
    cvar_t vars[C_RUN_VARS];
    int var_count;
    char (*lines)[72];
    int max_lines;
    int line_count;
    int column;
    char* error;
    int error_cap;
    int* output_count;
    int waiting_input;
    int pending_count;
    int pending_var[C_RUN_ARGS];
    char pending_type[C_RUN_ARGS];
} runner_t;

static runner_t r;

static void int_to_string(int value, char* out) {
    char reverse[12];
    uint32_t magnitude = value < 0 ? 0u - (uint32_t)value : (uint32_t)value;
    int n = 0, pos = 0;
    do { reverse[n++] = (char)('0' + magnitude % 10); magnitude /= 10; } while (magnitude);
    if (value < 0) out[pos++] = '-';
    while (n) out[pos++] = reverse[--n];
    out[pos] = 0;
}

static int ci_eq(char a, char b) {
    if (a >= 'A' && a <= 'Z') a += 'a' - 'A';
    if (b >= 'A' && b <= 'Z') b += 'a' - 'A';
    return a == b;
}

static int word_eq(const char* a, uint32_t n, const char* b) {
    if (kstrlen(b) != n) return 0;
    for (uint32_t i = 0; i < n; i++) if (!ci_eq(a[i], b[i])) return 0;
    return 1;
}

static void skip_space(runner_t* r) {
    for (;;) {
        while (r->p < r->end && (*r->p == ' ' || *r->p == '\t' ||
               *r->p == '\r' || *r->p == '\n')) r->p++;
        if (r->p + 1 < r->end && r->p[0] == '/' && r->p[1] == '/') {
            while (r->p < r->end && *r->p != '\n') r->p++;
            continue;
        }
        if (r->p + 1 < r->end && r->p[0] == '/' && r->p[1] == '*') {
            r->p += 2;
            while (r->p + 1 < r->end && !(r->p[0] == '*' && r->p[1] == '/')) r->p++;
            if (r->p + 1 < r->end) r->p += 2;
            continue;
        }
        break;
    }
}

static int identifier_start(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}
static int identifier_char(char c) {
    return identifier_start(c) || (c >= '0' && c <= '9');
}

static uint32_t read_identifier(runner_t* r, char* out, uint32_t cap) {
    skip_space(r);
    uint32_t n = 0;
    while (r->p < r->end && identifier_char(*r->p)) {
        if (n + 1 < cap) out[n++] = *r->p;
        r->p++;
    }
    if (cap) out[n] = 0;
    return n;
}

static int expect(runner_t* r, char c) {
    skip_space(r);
    if (r->p >= r->end || *r->p != c) return 0;
    r->p++;
    return 1;
}

static int read_string(runner_t* r, char* out, uint32_t cap) {
    skip_space(r);
    if (r->p >= r->end || *r->p != '"') return 0;
    r->p++;
    uint32_t n = 0;
    while (r->p < r->end && *r->p != '"') {
        char c = *r->p++;
        if (c == '\\') {
            if (r->p >= r->end) return 0;
            c = *r->p++;
            if (c == 'n') c = '\n';
            else if (c == 'r') c = '\r';
            else if (c == 't') c = '\t';
            else if (c != '\\' && c != '"' && c != '\'') return 0;
        }
        if (n + 1 < cap) out[n++] = c;
    }
    if (r->p >= r->end) return 0;
    r->p++;
    if (cap) out[n] = 0;
    return 1;
}

static int find_var(runner_t* r, const char* name) {
    for (int i = 0; i < r->var_count; i++)
        if (kstrcmp(r->vars[i].name, name) == 0) return i;
    return -1;
}

static int read_string_value(runner_t* r, char* out, uint32_t cap) {
    skip_space(r);
    if (r->p < r->end && *r->p == '"')
        return read_string(r, out, cap);
    char name[16];
    if (!read_identifier(r, name, sizeof name)) return 0;
    int idx = find_var(r, name);
    if (idx < 0 || !r->vars[idx].is_string) return 0;
    kstrcpy(out, r->vars[idx].string);
    if (kstrlen(out) >= cap) out[cap - 1] = 0;
    return 1;
}

static int parse_expression(runner_t* r, int* value);

static int parse_factor(runner_t* r, int* value) {
    skip_space(r);
    if (r->p >= r->end) return 0;
    if (*r->p == '-') {
        r->p++;
        if (!parse_factor(r, value)) return 0;
        *value = -*value;
        return 1;
    }
    if (*r->p == '(') {
        r->p++;
        if (!parse_expression(r, value) || !expect(r, ')')) return 0;
        return 1;
    }
    if (*r->p >= '0' && *r->p <= '9') {
        int v = 0;
        while (r->p < r->end && *r->p >= '0' && *r->p <= '9') {
            v = v * 10 + (*r->p++ - '0');
        }
        *value = v;
        return 1;
    }
    if (*r->p == '\'') {
        r->p++;
        if (r->p >= r->end) return 0;
        char c = *r->p++;
        if (c == '\\') {
            if (r->p >= r->end) return 0;
            c = *r->p++;
            if (c == 'n') c = '\n';
            else if (c == 't') c = '\t';
            else if (c == 'r') c = '\r';
        }
        if (r->p >= r->end || *r->p++ != '\'') return 0;
        *value = (unsigned char)c;
        return 1;
    }
    char name[16];
    uint32_t n = read_identifier(r, name, sizeof name);
    if (!n) return 0;
    if (kstrcmp(name, "strlen") == 0) {
        char text[C_STRING_CAP];
        if (!expect(r, '(') || !read_string_value(r, text, sizeof text) ||
            !expect(r, ')')) return 0;
        *value = (int)kstrlen(text);
        return 1;
    }
    if (kstrcmp(name, "strcmp") == 0) {
        char left[C_STRING_CAP], right[C_STRING_CAP];
        if (!expect(r, '(') || !read_string_value(r, left, sizeof left) ||
            !expect(r, ',') || !read_string_value(r, right, sizeof right) ||
            !expect(r, ')')) return 0;
        *value = kstrcmp(left, right);
        return 1;
    }
    int idx = find_var(r, name);
    if (idx < 0 || r->vars[idx].is_string) return 0;
    *value = r->vars[idx].value;
    return 1;
}

static int parse_term(runner_t* r, int* value) {
    if (!parse_factor(r, value)) return 0;
    for (;;) {
        skip_space(r);
        if (r->p >= r->end || (*r->p != '*' && *r->p != '/')) return 1;
        char op = *r->p++;
        int rhs;
        if (!parse_factor(r, &rhs) || (op == '/' && rhs == 0)) return 0;
        if (op == '*') *value *= rhs;
        else *value /= rhs;
    }
}

static int parse_expression(runner_t* r, int* value) {
    if (!parse_term(r, value)) return 0;
    for (;;) {
        skip_space(r);
        if (r->p >= r->end || (*r->p != '+' && *r->p != '-')) return 1;
        char op = *r->p++;
        int rhs;
        if (!parse_term(r, &rhs)) return 0;
        if (op == '+') *value += rhs;
        else *value -= rhs;
    }
}

static void output_char(runner_t* r, char c) {
    if (r->line_count <= 0) r->line_count = 1;
    if (c == '\r') return;
    if (c == '\n' || r->column >= 71) {
        if (r->line_count < r->max_lines) {
            r->line_count++;
            r->column = 0;
            r->lines[r->line_count - 1][0] = 0;
        }
        if (c == '\n') return;
    }
    if (r->line_count >= r->max_lines) return;
    r->lines[r->line_count - 1][r->column++] = c;
    r->lines[r->line_count - 1][r->column] = 0;
}

static void output_text(runner_t* r, const char* text) {
    while (*text) output_char(r, *text++);
}

static void output_int(runner_t* r, int value, int hex) {
    char buf[16];
    if (!hex) {
        int_to_string(value, buf);
        output_text(r, buf);
        return;
    }
    static const char digits[] = "0123456789abcdef";
    unsigned v = (unsigned)value;
    int n = 0;
    do { buf[n++] = digits[v & 0xF]; v >>= 4; } while (v && n < 8);
    while (n) output_char(r, buf[--n]);
}

static int parse_argument(runner_t* r, c_arg_t* arg) {
    skip_space(r);
    if (r->p < r->end && *r->p == '"') {
        arg->is_string = 1;
        return read_string(r, arg->string, sizeof arg->string);
    }
    const char* saved = r->p;
    char name[16];
    if (read_identifier(r, name, sizeof name)) {
        int idx = find_var(r, name);
        if (idx >= 0 && r->vars[idx].is_string) {
            arg->is_string = 1;
            kstrcpy(arg->string, r->vars[idx].string);
            return 1;
        }
    }
    r->p = saved;
    arg->is_string = 0;
    return parse_expression(r, &arg->number);
}

static int parse_printf(runner_t* r) {
    char format[256];
    c_arg_t args[C_RUN_ARGS];
    int argc = 0;
    if (!expect(r, '(') || !read_string(r, format, sizeof format)) return 0;
    skip_space(r);
    while (r->p < r->end && *r->p == ',') {
        if (argc == C_RUN_ARGS) return 0;
        r->p++;
        if (!parse_argument(r, &args[argc++])) return 0;
        skip_space(r);
    }
    if (!expect(r, ')') || !expect(r, ';')) return 0;

    int ai = 0;
    for (const char* p = format; *p; p++) {
        if (*p != '%') { output_char(r, *p); continue; }
        p++;
        if (!*p) return 0;
        if (*p == '%') { output_char(r, '%'); continue; }
        if (ai >= argc) return 0;
        c_arg_t* a = &args[ai++];
        if (*p == 's' && a->is_string) output_text(r, a->string);
        else if ((*p == 'd' || *p == 'i' || *p == 'u') && !a->is_string)
            output_int(r, a->number, 0);
        else if ((*p == 'x' || *p == 'X') && !a->is_string)
            output_int(r, a->number, 1);
        else if (*p == 'c' && !a->is_string) output_char(r, (char)a->number);
        else return 0;
    }
    return 1;
}

static int parse_scanf(runner_t* r) {
    char format[256];
    char types[C_RUN_ARGS];
    int conversions = 0;
    if (!expect(r, '(') || !read_string(r, format, sizeof format)) return 0;
    for (const char* p = format; *p; p++) {
        if (*p != '%') continue;
        p++;
        if (!*p) return 0;
        if (*p == '%') continue;
        if (conversions >= C_RUN_ARGS) return 0;
        if (*p == 'd' || *p == 'i' || *p == 'u') types[conversions++] = 'd';
        else if (*p == 's') types[conversions++] = 's';
        else if (*p == 'c') types[conversions++] = 'c';
        else return 0;
    }
    if (!conversions) return 0;

    for (int i = 0; i < conversions; i++) {
        char name[16];
        if (!expect(r, ',')) return 0;
        skip_space(r);
        if (types[i] != 's' && !expect(r, '&')) return 0;
        if (types[i] == 's' && r->p < r->end && *r->p == '&') r->p++;
        if (!read_identifier(r, name, sizeof name)) return 0;
        int idx = find_var(r, name);
        if (idx < 0 || (types[i] == 's') != r->vars[idx].is_string) return 0;
        r->pending_var[i] = idx;
        r->pending_type[i] = types[i];
    }
    if (!expect(r, ')') || !expect(r, ';')) return 0;
    r->pending_count = conversions;
    return 1;
}

static int find_main_body(runner_t* r) {
    const char* p = r->source;
    while (p < r->end) {
        while (p < r->end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) p++;
        if (p >= r->end) break;
        if (*p == '#') { while (p < r->end && *p != '\n') p++; continue; }
        const char* word = p;
        while (p < r->end && identifier_char(*p)) p++;
        if (word_eq(word, (uint32_t)(p - word), "int") ||
            word_eq(word, (uint32_t)(p - word), "void")) {
            while (p < r->end && (*p == ' ' || *p == '\t')) p++;
            const char* name = p;
            while (p < r->end && identifier_char(*p)) p++;
            if (word_eq(name, (uint32_t)(p - name), "main")) {
                int depth = 0;
                while (p < r->end) {
                    if (*p == '(') depth++;
                    else if (*p == ')') depth--;
                    else if (*p == '{' && depth == 0) { r->p = p + 1; return 1; }
                    p++;
                }
                return 0;
            }
        }
        while (p < r->end && *p != '\n' && *p != ';' && *p != '{') p++;
        if (p < r->end && *p == ';') p++;
        else if (p < r->end && *p == '{') p++;
    }
    return 0;
}

static int cmini_execute(const char* source, char lines[][72], int max_lines,
                         int* count, char* err, int errcap, int resume) {
    if (!resume) {
        memset(&r, 0, sizeof r);
        r.source = source;
        r.p = source;
        r.end = source + kstrlen(source);
        r.lines = lines;
        r.max_lines = max_lines;
        r.output_count = count;
        r.line_count = 1;
        for (int i = 0; i < max_lines; i++) lines[i][0] = 0;
        if (!find_main_body(&r)) {
            if (errcap) kstrcpy(err, "main() not found");
            *count = 0;
            return -1;
        }
    } else {
        if (!r.waiting_input) {
            if (errcap) kstrcpy(err, "program is not waiting for scanf");
            return -1;
        }
        r.waiting_input = 0;
    }
    r.error = err;
    r.error_cap = errcap;
    r.output_count = count;
    if (errcap) err[0] = 0;

    while (r.p < r.end) {
        skip_space(&r);
        if (r.p >= r.end || *r.p == '}') break;
        if (*r.p == '{') { r.p++; continue; }

        char name[16];
        const char* token_start = r.p;
        uint32_t token_len = read_identifier(&r, name, sizeof name);
        if (!token_len) {
            if (errcap) kstrcpy(err, "C subset: unsupported syntax");
            *count = r.line_count;
            return -1;
        }

        if (word_eq(token_start, token_len, "return")) {
            int ignored;
            skip_space(&r);
            if (r.p < r.end && *r.p != ';' && !parse_expression(&r, &ignored)) {
                if (errcap) kstrcpy(err, "C subset: bad return expression");
                *count = r.line_count; return -1;
            }
            (void)expect(&r, ';');
            break;
        }
        if (word_eq(token_start, token_len, "printf")) {
            if (!parse_printf(&r)) {
                if (errcap) kstrcpy(err, "C subset: unsupported printf format");
                *count = r.line_count; return -1;
            }
            continue;
        }
        if (word_eq(token_start, token_len, "puts")) {
            char text[C_STRING_CAP];
            if (!expect(&r, '(') || !read_string_value(&r, text, sizeof text) ||
                !expect(&r, ')') || !expect(&r, ';')) {
                if (errcap) kstrcpy(err, "C subset: puts expects a string");
                *count = r.line_count; return -1;
            }
            output_text(&r, text); output_char(&r, '\n');
            continue;
        }
        if (word_eq(token_start, token_len, "scanf")) {
            if (!parse_scanf(&r)) {
                if (errcap) kstrcpy(err, "Error during compiling: unsupported scanf format");
                *count = r.line_count; return -1;
            }
            r.waiting_input = 1;
            *count = r.line_count;
            return 1;
        }
        if (word_eq(token_start, token_len, "strcpy") ||
            word_eq(token_start, token_len, "strcat")) {
            int append = word_eq(token_start, token_len, "strcat");
            char destination[16], source_text[C_STRING_CAP];
            if (!expect(&r, '(') || !read_identifier(&r, destination, sizeof destination)) {
                if (errcap) kstrcpy(err, "C subset: bad string destination");
                *count = r.line_count; return -1;
            }
            int dst = find_var(&r, destination);
            if (dst < 0 || !r.vars[dst].is_string || !expect(&r, ',') ||
                !read_string_value(&r, source_text, sizeof source_text) ||
                !expect(&r, ')') || !expect(&r, ';')) {
                if (errcap) kstrcpy(err, "C subset: use char[] with strcpy/strcat");
                *count = r.line_count; return -1;
            }
            if (!append) {
                kstrcpy(r.vars[dst].string, source_text);
            } else {
                uint32_t n = (uint32_t)kstrlen(r.vars[dst].string);
                uint32_t j = 0;
                while (source_text[j] && n + 1 < C_STRING_CAP)
                    r.vars[dst].string[n++] = source_text[j++];
                r.vars[dst].string[n] = 0;
            }
            continue;
        }
        if (word_eq(token_start, token_len, "putchar")) {
            int value;
            if (!expect(&r, '(') || !parse_expression(&r, &value) ||
                !expect(&r, ')') || !expect(&r, ';')) {
                if (errcap) kstrcpy(err, "C subset: bad putchar argument");
                *count = r.line_count; return -1;
            }
            output_char(&r, (char)value);
            continue;
        }
        if (word_eq(token_start, token_len, "int") ||
            word_eq(token_start, token_len, "char") ||
            word_eq(token_start, token_len, "string")) {
            int is_string = word_eq(token_start, token_len, "string");
            int is_char = word_eq(token_start, token_len, "char");
            skip_space(&r);
            if (is_char && r.p < r.end && *r.p == '*') {
                is_string = 1;
                r.p++;
            }
            char var[16];
            if (!read_identifier(&r, var, sizeof var) || r.var_count == C_RUN_VARS) {
                if (errcap) kstrcpy(err, "C subset: too many or invalid variables");
                *count = r.line_count; return -1;
            }
            skip_space(&r);
            if (is_char && r.p < r.end && *r.p == '[') {
                is_string = 1;
                r.p++;
                skip_space(&r);
                while (r.p < r.end && *r.p >= '0' && *r.p <= '9') r.p++;
                if (!expect(&r, ']')) {
                    if (errcap) kstrcpy(err, "C subset: bad char array size");
                    *count = r.line_count; return -1;
                }
            }
            int value = 0;
            char text[C_STRING_CAP] = "";
            if (expect(&r, '=')) {
                skip_space(&r);
                if (is_string || (is_char && r.p < r.end && *r.p == '"')) {
                    is_string = 1;
                    if (!read_string_value(&r, text, sizeof text)) {
                        if (errcap) kstrcpy(err, "C subset: bad string initializer");
                        *count = r.line_count; return -1;
                    }
                } else if (!parse_expression(&r, &value)) {
                    if (errcap) kstrcpy(err, "C subset: bad initializer");
                    *count = r.line_count; return -1;
                }
            }
            if (!expect(&r, ';')) {
                if (errcap) kstrcpy(err, "C subset: expected semicolon");
                *count = r.line_count; return -1;
            }
            kstrcpy(r.vars[r.var_count].name, var);
            r.vars[r.var_count].value = value;
            r.vars[r.var_count].is_string = is_string;
            if (is_string) kstrcpy(r.vars[r.var_count].string, text);
            r.var_count++;
            continue;
        }

        /* Assignment to a previously declared integer or string variable. */
        int idx = find_var(&r, name);
        int value;
        if (idx >= 0 && expect(&r, '=')) {
            if (r.vars[idx].is_string) {
                char text[C_STRING_CAP];
                if (!read_string_value(&r, text, sizeof text) || !expect(&r, ';')) {
                    if (errcap) kstrcpy(err, "C subset: bad string assignment");
                    *count = r.line_count; return -1;
                }
                kstrcpy(r.vars[idx].string, text);
            } else {
                if (!parse_expression(&r, &value) || !expect(&r, ';')) {
                    if (errcap) kstrcpy(err, "C subset: bad assignment");
                    *count = r.line_count; return -1;
                }
                r.vars[idx].value = value;
            }
            continue;
        }

        if (errcap) kstrcpy(err, "C subset: unsupported statement");
        *count = r.line_count;
        return -1;
    }
    *count = r.line_count;
    return 0;
}

int cmini_run(const char* source, char lines[][72], int max_lines,
              int* count, char* err, int errcap) {
    return cmini_execute(source, lines, max_lines, count, err, errcap, 0);
}

int cmini_feed_input(const char* input, char* err, int errcap) {
    if (!r.waiting_input) {
        if (errcap) kstrcpy(err, "program is not waiting for scanf");
        return -1;
    }
    r.error = err;
    r.error_cap = errcap;
    if (errcap) err[0] = 0;

    const char* p = input;
    for (int i = 0; i < r.pending_count; i++) {
        char type = r.pending_type[i];
        int index = r.pending_var[i];
        if (type == 'c') {
            if (!*p) goto invalid_input;
            r.vars[index].value = (unsigned char)*p++;
            continue;
        }
        while (*p == ' ' || *p == '\t') p++;
        if (type == 's') {
            uint32_t n = 0;
            while (*p && *p != ' ' && *p != '\t' && n + 1 < C_STRING_CAP)
                r.vars[index].string[n++] = *p++;
            if (!n) goto invalid_input;
            r.vars[index].string[n] = 0;
        } else {
            int sign = 1, digits = 0;
            int value = 0;
            if (*p == '-') { sign = -1; p++; }
            else if (*p == '+') p++;
            while (*p >= '0' && *p <= '9') {
                value = value * 10 + (*p++ - '0');
                digits++;
            }
            if (!digits) goto invalid_input;
            r.vars[index].value = sign * value;
        }
    }

    output_text(&r, input);
    output_char(&r, '\n');
    return cmini_execute(r.source, r.lines, r.max_lines, r.output_count,
                         err, errcap, 1);

invalid_input:
    if (errcap) kstrcpy(err, "scanf input does not match format");
    return -1;
}
