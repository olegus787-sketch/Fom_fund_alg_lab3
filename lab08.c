#include <ctype.h>
#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
    OK, END, INVALID, RANGE, MEMORY, IO, NOT_FOUND, DUPLICATE, DOMAIN
} Status;

typedef struct {
    char *data;
    size_t len, cap;
} Buffer;

/* Ввод, проверка и обработка ошибок. */

static Status reserve(void **out, void *old, size_t *cap, size_t needed, size_t elem_size)
{
    if (!out || !cap || !elem_size) return INVALID;
    if (needed <= *cap) {
        *out = old;
        return OK;
    }
    size_t max_count = SIZE_MAX / elem_size;
    if (needed > max_count) return RANGE;

    /* Увеличиваем запас вдвое, когда это позволяет размер. */
    size_t new_cap = needed;
    if (*cap && *cap <= max_count / 2 && *cap * 2 > needed) {
        new_cap = *cap * 2;
    }
    void *new_memory = realloc(old, new_cap * elem_size);
    if (!new_memory) return MEMORY;
    *out = new_memory;
    *cap = new_cap;
    return OK;
}

static Status append_char(Buffer *b, int c)
{
    if (!b || c < 0 || c > 255) return INVALID;
    if (b->len > SIZE_MAX - 2) return RANGE;
    void *p;
    Status st = reserve(&p, b->data, &b->cap, b->len + 2, 1);
    if (st != OK) return st;
    b->data = p;
    ((unsigned char *)b->data)[b->len++] = (unsigned char)c;
    b->data[b->len] = '\0';
    return OK;
}

static Status arg_count(int argc, char **argv, int min, int max)
{
    if (!argv || argc < min || argc > max) return INVALID;
    for (int i = 1; i < argc; ++i) if (!argv[i] || !*argv[i]) return INVALID;
    return OK;
}

static Status unsigned_number(const char *s, uintmax_t max, uintmax_t *out)
{
    if (!s || !*s || !out) return INVALID;
    uintmax_t value = 0;
    for (; *s; ++s) {
        if (*s < '0' || *s > '9') return INVALID;
        unsigned digit = (unsigned)(*s - '0');
        if (digit > max || value > (max - digit) / 10) return RANGE;
        value = value * 10 + digit;
    }
    *out = value;
    return OK;
}

static Status signed_number(const char *s, intmax_t *out)
{
    if (!s || !out) return INVALID;
    bool negative = *s == '-';
    if (*s == '+' || negative) ++s;
    uintmax_t limit = (uintmax_t)INTMAX_MAX + (negative ? 1u : 0u);
    uintmax_t n;
    Status st = unsigned_number(s, limit, &n);
    if (st != OK) return st;
    if (negative && n != 0) {
        /* Сначала уменьшаем модуль, чтобы он поместился в знаковый тип. */
        *out = -(intmax_t)(n - 1) - 1;
    } else {
        *out = (intmax_t)n;
    }
    return OK;
}

static Status open_file(const char *path, const char *mode, FILE **out)
{
    if (!path || !*path || !mode || !out) return INVALID;
    *out = fopen(path, mode);
    return *out ? OK : IO;
}

static Status close_file(FILE *file, Status status)
{
    if (file && fclose(file) == EOF && status == OK) return IO;
    return status;
}

static void report(Status status)
{
    const char *messages[] = {"ok", "unexpected end of input", "invalid input",
        "value out of range", "out of memory", "I/O error", "not found",
        "duplicate value", "operation outside its domain"};
    if (status != OK && status >= END && status <= DOMAIN)
        fprintf(stderr, "%s\n", messages[status]);
}

static int result(Status status)
{
    if (status == OK && fflush(stdout) == EOF) status = IO;
    report(status);
    return status == OK ? EXIT_SUCCESS : EXIT_FAILURE;
}

/* Решение задачи. */
typedef struct Term {
    intmax_t coefficient;
    unsigned power;
    struct Term *next;
} Term;

typedef enum { ADD, SUB, MULT, DIV, MOD, EVAL, DIFF, CMPS } Operation;

static void clear(Term *p)
{
    while (p) {
        Term *next = p->next;
        free(p);
        p = next;
    }
}

static Status arithmetic(intmax_t a, intmax_t b, Operation op, intmax_t *out)
{
    if (!out) return INVALID;
    if (op == ADD) {
        if ((b > 0 && a > INTMAX_MAX - b) || (b < 0 && a < INTMAX_MIN - b)) return RANGE;
        *out = a + b;
    } else if (op == SUB) {
        if ((b > 0 && a < INTMAX_MIN + b) || (b < 0 && a > INTMAX_MAX + b)) return RANGE;
        *out = a - b;
    } else if (op == MULT) {
        if (a > 0 && ((b > 0 && a > INTMAX_MAX / b) || (b < 0 && b < INTMAX_MIN / a)))
            return RANGE;
        if (a < 0 && ((b > 0 && a < INTMAX_MIN / b) || (b < 0 && a < INTMAX_MAX / b)))
            return RANGE;
        *out = a * b;
    } else return INVALID;
    return OK;
}

/* Объединяем равные степени, степени храним по убыванию. */
static Status put(Term **poly, intmax_t coefficient, unsigned power, Operation op)
{
    if (!poly || (op != ADD && op != SUB)) return INVALID;
    if (!coefficient) return OK;
    Term **slot = poly;
    while (*slot && (*slot)->power > power) slot = &(*slot)->next;
    if (*slot && (*slot)->power == power) {
        Term *item = *slot;
        intmax_t combined;
        Status st = arithmetic(item->coefficient, coefficient, op, &combined);
        if (st != OK) return st;
        if (combined) item->coefficient = combined;
        else {
            *slot = item->next;
            free(item);
        }
        return OK;
    }
    intmax_t value;
    Status st = arithmetic(0, coefficient, op, &value);
    if (st != OK) return st;
    Term *p = malloc(sizeof *p);
    if (!p) return MEMORY;
    *p = (Term){value, power, *slot};
    *slot = p;
    return OK;
}

static Status copy(const Term *p, Term **out)
{
    if (!out) return INVALID;
    Term **tail = out;
    for (; p; p = p->next) {
        Term *node = malloc(sizeof *node);
        if (!node) return MEMORY;
        *node = (Term){p->coefficient, p->power, NULL};
        *tail = node;
        tail = &node->next;
    }
    return OK;
}

static Status add_sub(const Term *a, const Term *b, Operation op, Term **out)
{
    if (!out) return INVALID;
    Status st = copy(a, out);
    for (; st == OK && b; b = b->next) st = put(out, b->coefficient, b->power, op);
    return st;
}

static Status times(const Term *a, const Term *b, Term **out)
{
    if (!out) return INVALID;
    for (; a; a = a->next) {
        for (const Term *p = b; p; p = p->next) {
            if (a->power > UINT_MAX - p->power) return RANGE;
            intmax_t product;
            Status st = arithmetic(a->coefficient, p->coefficient, MULT, &product);
            if (st == OK) st = put(out, product, a->power + p->power, ADD);
            if (st != OK) return st;
        }
    }
    return OK;
}

/* Work through every quotient degree once, preserving any nondivisible residue. */
static Status divide(const Term *a, const Term *b, Term **quotient, Term **remainder)
{
    if (!quotient || !remainder) return INVALID;
    if (!b) return DOMAIN;
    Term *work = NULL;
    Status st = copy(a, &work);
    Term **tail = remainder;
    while (st == OK && work && work->power >= b->power) {
        const unsigned degree = work->power;
        unsigned shift = degree - b->power;
        if (b->coefficient == -1 && work->coefficient == INTMAX_MIN) {
            st = RANGE;
            break;
        }
        intmax_t coeff = work->coefficient / b->coefficient;
        if (coeff) {
            st = put(quotient, coeff, shift, ADD);
            for (const Term *p = b; st == OK && p; p = p->next) {
                intmax_t product;
                st = arithmetic(coeff, p->coefficient, MULT, &product);
                if (st == OK) st = put(&work, product, shift + p->power, SUB);
            }
        }
        if (st == OK && work && work->power == degree) {
            *tail = work;
            work = work->next;
            tail = &(*tail)->next;
            *tail = NULL;
        }
    }
    if (st == OK) *tail = work;
    else clear(work);
    return st;
}

static Status derivative(const Term *p, Term **out)
{
    if (!out) return INVALID;
    for (; p && p->power; p = p->next) {
        intmax_t coefficient;
#if UINT_MAX > INTMAX_MAX
        if (p->power > (uintmax_t)INTMAX_MAX) return RANGE;
#endif
        Status st = arithmetic(p->coefficient, (intmax_t)p->power, MULT, &coefficient);
        if (st == OK) st = put(out, coefficient, p->power - 1, ADD);
        if (st != OK) return st;
    }
    return OK;
}

static Status int_power(intmax_t base, unsigned power, intmax_t *out)
{
    intmax_t value = 1;
    Status st = OK;
    while (st == OK && power) {
        if (power & 1u) st = arithmetic(value, base, MULT, &value);
        power >>= 1;
        if (st == OK && power) st = arithmetic(base, base, MULT, &base);
    }
    if (st == OK) *out = value;
    return st;
}

static Status evaluate(const Term *p, intmax_t x, intmax_t *out)
{
    if (!out) return INVALID;
    if (!p) { *out = 0; return OK; }
    intmax_t value = p->coefficient;
    unsigned previous = p->power;
    for (p = p->next; p; p = p->next) {
        intmax_t scale;
        Status st = int_power(x, previous - p->power, &scale);
        if (st == OK) st = arithmetic(value, scale, MULT, &value);
        if (st == OK) st = arithmetic(value, p->coefficient, ADD, &value);
        if (st != OK) return st;
        previous = p->power;
    }
    intmax_t scale;
    Status st = int_power(x, previous, &scale);
    if (st == OK) st = arithmetic(value, scale, MULT, out);
    return st;
}

static Status evaluate_real(const Term *p, long double x, long double *out)
{
    if (!out || !isfinite(x)) return INVALID;
    if (!p) { *out = 0; return OK; }
    long double value = p->coefficient;
    unsigned previous = p->power;
    for (p = p->next; p; p = p->next) {
        value = value * powl(x, previous - p->power) + p->coefficient;
        if (!isfinite(value)) return RANGE;
        previous = p->power;
    }
    value *= powl(x, previous);
    if (!isfinite(value)) return RANGE;
    *out = value;
    return OK;
}

static Status replace_product(Term **value, const Term *other)
{
    Term *product = NULL;
    Status st = times(*value, other, &product);
    if (st == OK) {
        clear(*value);
        *value = product;
    } else clear(product);
    return st;
}

/* Scale the Horner accumulator by base^power using repeated squaring. */
static Status scale_poly(Term **value, const Term *base, unsigned power)
{
    if (!*value || !power) return OK;
    Term *factor = NULL;
    Status st = copy(base, &factor);
    while (st == OK && power) {
        if (power & 1u) st = replace_product(value, factor);
        power >>= 1;
        if (st == OK && power && *value) st = replace_product(&factor, factor);
        if (!*value) break;
    }
    clear(factor);
    return st;
}

static Status compose(const Term *p, const Term *other, Term **out)
{
    if (!out) return INVALID;
    if (!p) return OK;
    Status st = put(out, p->coefficient, 0, ADD);
    unsigned previous = p->power;
    for (p = p->next; st == OK && p; p = p->next) {
        st = scale_poly(out, other, previous - p->power);
        if (st == OK) st = put(out, p->coefficient, 0, ADD);
        previous = p->power;
    }
    if (st == OK) st = scale_poly(out, other, previous);
    return st;
}

static void skip_space(const char **p)
{
    while (isspace((unsigned char)**p)) ++*p;
}

static Status parse_digits(const char **p, uintmax_t limit, uintmax_t *out)
{
    if (**p < '0' || **p > '9') return INVALID;
    uintmax_t value = 0;
    do {
        unsigned digit = (unsigned)(**p - '0');
        if (digit > limit || value > (limit - digit) / 10) return RANGE;
        value = value * 10 + digit;
        ++*p;
    } while (**p >= '0' && **p <= '9');
    *out = value;
    return OK;
}

static Status parse_poly(const char *p, Term **out)
{
    if (!p || !out) return INVALID;
    skip_space(&p);
    while (true) {
        bool minus = *p == '-';
        if (*p == '+' || minus) ++p;
        skip_space(&p);
        uintmax_t magnitude = 1, degree = 0;
        bool has_number = *p >= '0' && *p <= '9';
        Status st = OK;
        if (has_number) st = parse_digits(&p, (uintmax_t)INTMAX_MAX + minus, &magnitude);
        if (st != OK) return st;
        skip_space(&p);
        if (*p == 'x') {
            degree = 1;
            ++p;
            skip_space(&p);
            if (*p == '^') {
                ++p;
                skip_space(&p);
                st = parse_digits(&p, UINT_MAX, &degree);
                if (st != OK) return st;
            }
        } else if (!has_number) return INVALID;
        intmax_t coefficient = minus && magnitude ? -(intmax_t)(magnitude - 1) - 1
                                                  : (intmax_t)magnitude;
        st = put(out, coefficient, (unsigned)degree, ADD);
        if (st != OK) return st;
        skip_space(&p);
        if (!*p) return OK;
        if (*p != '+' && *p != '-') return INVALID;
    }
}

static Status read_command(FILE *input, Buffer *buf)
{
    buf->len = 0;
    if (buf->data) buf->data[0] = '\0';
    int c;
    while ((c = fgetc(input)) != EOF) {
        if (c == ';') return buf->len ? OK : INVALID;
        if (c == '%') {
            while ((c = fgetc(input)) != EOF && c != '\n') {}
            c = ' ';
        } else if (c == '[') {
            while ((c = fgetc(input)) != EOF && c != ']' && c != '[') {}
            if (c != ']') return ferror(input) ? IO : INVALID;
            c = ' ';
        }
        if (!c || c == ']') return INVALID;
        if (buf->len || !isspace((unsigned char)c)) {
            Status st = append_char(buf, c);
            if (st != OK) return st;
        }
    }
    return ferror(input) ? IO : buf->len ? INVALID : END;
}

static Status print_poly(const Term *p)
{
    if (!p) return puts("0") == EOF ? IO : OK;
    bool first = true;
    for (; p; p = p->next) {
        intmax_t value = p->coefficient;
        if (value < 0 || !first) {
            if (putchar(value < 0 ? '-' : '+') == EOF) return IO;
        }
        uintmax_t magnitude = value < 0 ? (uintmax_t)(-(value + 1)) + 1 : (uintmax_t)value;
        if ((!p->power || magnitude != 1) && printf("%" PRIuMAX, magnitude) < 0) return IO;
        if (p->power && putchar('x') == EOF) return IO;
        if (p->power > 1 && printf("^%u", p->power) < 0) return IO;
        first = false;
    }
    return putchar('\n') == EOF ? IO : OK;
}

static void trim_end(char *s)
{
    size_t n = strlen(s);
    while (n && isspace((unsigned char)s[n - 1])) s[--n] = '\0';
}

static Status evaluate_and_print(const Term *p, const char *input)
{
    if (strpbrk(input, ".eE")) {
        char *end;
        long double x = strtold(input, &end);
        if (end == input || *end) return INVALID;
        if (!isfinite(x)) return RANGE;
        long double value;
        Status st = evaluate_real(p, x, &value);
        if (st == OK && printf("%.18Lg\n", value) < 0) st = IO;
        return st;
    }
    intmax_t x, value;
    Status st = signed_number(input, &x);
    if (st == OK) st = evaluate(p, x, &value);
    if (st == OK && printf("%" PRIdMAX "\n", value) < 0) st = IO;
    return st;
}

static Status execute(char *line, Term **acc)
{
    const char *names[] = {"Add", "Sub", "Mult", "Div", "Mod", "Eval", "Diff", "Cmps"};
    char *open = strchr(line, '(');
    if (!open) return INVALID;
    *open = '\0';
    trim_end(line);
    int op = ADD;
    while (op <= CMPS && strcmp(line, names[op]) != 0) ++op;
    if (op > CMPS) return INVALID;
    char *close = strchr(open + 1, ')');
    if (!close) return INVALID;
    const char *trailing = close + 1;
    skip_space(&trailing);
    if (*trailing) return INVALID;
    *close = '\0';
    char *left = open + 1, *right = strchr(left, ',');
    if (right) *right++ = '\0';
    Term *a = NULL, *b = NULL, *output = NULL, *rest = NULL;
    const Term *first = *acc;
    Status st = OK;
    if (op == DIFF) {
        const char *arg = left;
        skip_space(&arg);
        if (right) st = INVALID;
        else if (*arg) {
            st = parse_poly(arg, &a);
            first = a;
        }
        if (st == OK) st = derivative(first, &output);
    } else {
        if (right) {
            st = parse_poly(left, &a);
            first = a;
        } else right = left;
        if (st == OK && op == EVAL) {
            const char *arg = right;
            skip_space(&arg);
            trim_end(right);
            st = evaluate_and_print(first, arg);
        } else if (st == OK) {
            st = parse_poly(right, &b);
            if (st == OK) {
                switch (op) {
                case ADD: case SUB: st = add_sub(first, b, (Operation)op, &output); break;
                case MULT: st = times(first, b, &output); break;
                case DIV: st = divide(first, b, &output, &rest); break;
                case MOD: st = divide(first, b, &rest, &output); break;
                case CMPS: st = compose(first, b, &output); break;
                default: st = INVALID; break;
                }
            }
        }
    }
    if (st == OK && op != EVAL) {
        clear(*acc);
        *acc = output;
        output = NULL;
        st = print_poly(*acc);
    }
    clear(a);
    clear(b);
    clear(output);
    clear(rest);
    return st;
}

static Status run(FILE *in)
{
    Buffer line = {0};
    Term *acc = NULL;
    Status st;
    size_t number = 1;
    while ((st = read_command(in, &line)) == OK) {
        st = execute(line.data, &acc);
        if (st != OK) break;
        ++number;
    }
    if (st != END) fprintf(stderr, "Command %zu: ", number);
    clear(acc);
    free(line.data);
    return st == END ? OK : st;
}

int main(int argc, char **argv)
{
    Status st = arg_count(argc, argv, 2, 2);
    FILE *in = NULL;
    if (st == OK) st = open_file(argv[1], "r", &in);
    if (st == OK) st = run(in);
    return result(close_file(in, st));
}
