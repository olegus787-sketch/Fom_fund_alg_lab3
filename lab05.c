#include <ctype.h>
#include <inttypes.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

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

static Status read_line(FILE *in, Buffer *b)
{
    if (!in || !b) return INVALID;
    b->len = 0;
    if (b->data) b->data[0] = '\0';
    int c;
    while ((c = fgetc(in)) != EOF && c != '\n') {
        if (c == 0) {
            while ((c = fgetc(in)) != EOF && c != '\n') {}
            return ferror(in) ? IO : INVALID;
        }
        Status st = append_char(b, c);
        if (st != OK) return st;
    }
    if (ferror(in)) return IO;
    if (c == EOF && !b->len) return END;
    if (!b->data) {
        Status st = append_char(b, 0);
        if (st != OK) return st;
        b->len = 0;
    }
    if (b->len && b->data[b->len - 1] == '\r') b->data[--b->len] = '\0';
    return OK;
}

static Status arg_count(int argc, char **argv, int min, int max)
{
    if (!argv || argc < min || argc > max) return INVALID;
    for (int i = 1; i < argc; ++i) if (!argv[i] || !*argv[i]) return INVALID;
    return OK;
}

static Status split(char *line, char **fields, size_t cap, size_t *count)
{
    if (!line || !fields || !count) return INVALID;
    *count = 0;
    while (*line) {
        while (isspace((unsigned char)*line)) ++line;
        if (!*line) break;
        if (*count == cap) return INVALID;
        fields[(*count)++] = line;
        while (*line && !isspace((unsigned char)*line)) ++line;
        if (*line) *line++ = '\0';
    }
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

static Status latin(const char *s, bool empty_allowed)
{
    if (!s || (!*s && !empty_allowed)) return INVALID;
    for (; *s; ++s) {
        if (!((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z'))) return INVALID;
    }
    return OK;
}

static Status copy_text(const char *s, char **out)
{
    if (!s || !out) return INVALID;
    size_t len = strlen(s);
    char *copy = malloc(len + 1);
    if (!copy) return MEMORY;
    memcpy(copy, s, len + 1);
    *out = copy;
    return OK;
}

static int int_order(uintmax_t lhs, uintmax_t rhs)
{
    if (lhs < rhs) return -1;
    if (lhs > rhs) return 1;
    return 0;
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

static Status distinct_files(const char *input, const char *output)
{
    if (!input || !output || strcmp(input, output) == 0) return INVALID;
    struct stat a, b;
    if (stat(input, &a) == 0 && stat(output, &b) == 0 &&
        a.st_dev == b.st_dev && a.st_ino == b.st_ino) return INVALID;
    return OK;
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
typedef struct {
    uintmax_t id;
    char *name, *surname, *group;
    unsigned char *grades;
} Student;

typedef enum { ID, SURNAME, NAME, GROUP } Field;
typedef struct { Student *items; size_t count, capacity; } Class;

static void clear_student(Student *s)
{
    free(s->name);
    free(s->surname);
    free(s->group);
    free(s->grades);
    *s = (Student){0};
}

static Status parse_student(char **f, Student *s)
{
    if (!f || !s || latin(f[1], false) != OK || latin(f[2], false) != OK ||
        !f[3] || !*f[3]) return INVALID;
    Status st = unsigned_number(f[0], UINTMAX_MAX, &s->id);
    unsigned char grades[5];
    for (size_t i = 0; st == OK && i < 5; ++i) {
        uintmax_t mark;
        st = unsigned_number(f[i + 4], UCHAR_MAX, &mark);
        if (st == OK) grades[i] = (unsigned char)mark;
    }
    if (st == OK) st = copy_text(f[1], &s->name);
    if (st == OK) st = copy_text(f[2], &s->surname);
    if (st == OK) st = copy_text(f[3], &s->group);
    if (st == OK) {
        s->grades = malloc(sizeof grades);
        if (!s->grades) st = MEMORY;
        else memcpy(s->grades, grades, sizeof grades);
    }
    return st;
}

static Status load(FILE *input, Class *class)
{
    if (!input || !class) return INVALID;
    Buffer line = {0};
    Status st;
    while ((st = read_line(input, &line)) == OK) {
        char *fields[9];
        size_t count;
        st = split(line.data, fields, 9, &count);
        if (st != OK) break;
        if (!count) continue;
        if (count != 9) { st = INVALID; break; }
        Student s = {0};
        st = parse_student(fields, &s);
        if (st == OK) {
            void *p;
            st = reserve(&p, class->items, &class->capacity, class->count + 1, sizeof s);
            if (st == OK) {
                class->items = p;
                class->items[class->count++] = s;
            }
        }
        if (st != OK) { clear_student(&s); break; }
    }
    free(line.data);
    return st == END ? OK : st;
}

static const char *field_value(const Student *s, Field field)
{
    if (field == SURNAME) return s->surname;
    if (field == NAME) return s->name;
    return s->group;
}

static int by_id(const void *a, const void *b)
{
    return int_order(((const Student *)a)->id, ((const Student *)b)->id);
}

static int by_surname(const void *a, const void *b)
{
    return strcmp(((const Student *)a)->surname, ((const Student *)b)->surname);
}

static int by_name(const void *a, const void *b)
{
    return strcmp(((const Student *)a)->name, ((const Student *)b)->name);
}

static int by_group(const void *a, const void *b)
{
    return strcmp(((const Student *)a)->group, ((const Student *)b)->group);
}

static unsigned exam_total(const Student *s)
{
    unsigned sum = 0;
    for (size_t i = 0; i < 5; ++i) sum += s->grades[i];
    return sum;
}

static Status find(const Class *c, Field field, const char *key,
                   const Student ***matches, size_t *count)
{
    if (!c || field < ID || field > GROUP || !key || !*key || !matches || !count)
        return INVALID;
    uintmax_t id = 0;
    Status st = OK;
    if (field == ID) st = unsigned_number(key, UINTMAX_MAX, &id);
    if (field == NAME || field == SURNAME) st = latin(key, false);
    if (st != OK) return st;
    const Student **out = c->count ? malloc(c->count * sizeof *out) : NULL;
    if (c->count && !out) return MEMORY;
    size_t used = 0;
    for (size_t i = 0; i < c->count; ++i) {
        const Student *s = &c->items[i];
        if (field == ID ? s->id == id : strcmp(field_value(s, field), key) == 0)
            out[used++] = s;
    }
    *matches = out;
    *count = used;
    return OK;
}

static Status above_average(const Class *c, const Student ***matches, size_t *count)
{
    if (!c || !matches || !count) return INVALID;
    *matches = NULL;
    *count = 0;
    if (!c->count) return OK;
    if (c->count > UINTMAX_MAX / (5u * UCHAR_MAX)) return RANGE;
    uintmax_t total = 0;
    for (size_t i = 0; i < c->count; ++i) total += exam_total(&c->items[i]);
    const Student **out = malloc(c->count * sizeof *out);
    if (!out) return MEMORY;
    for (size_t i = 0; i < c->count; ++i) {
        if ((uintmax_t)exam_total(&c->items[i]) * c->count > total)
            out[(*count)++] = &c->items[i];
    }
    *matches = out;
    return OK;
}

static Status print_student(FILE *out, const Student *s, bool trace)
{
    if (trace) {
        return fprintf(out, "%s %s %s %.2f\n", s->surname, s->name,
                       s->group, exam_total(s) / 5.0) < 0 ? IO : OK;
    }
    if (fprintf(out, "%" PRIuMAX " %s %s %s", s->id, s->name, s->surname, s->group) < 0)
        return IO;
    for (size_t i = 0; i < 5; ++i)
        if (fprintf(out, " %u", (unsigned)s->grades[i]) < 0) return IO;
    return fputc('\n', out) == EOF ? IO : OK;
}

static Status parse_field(const char *s, Field *field)
{
    const char *names[] = {"id", "surname", "name", "group"};
    for (int i = ID; i <= GROUP; ++i) {
        if (strcmp(names[i], s) == 0) { *field = (Field)i; return OK; }
    }
    return INVALID;
}

static Status command(Class *c, FILE *trace, char **args, size_t n)
{
    int (*comparators[])(const void *, const void *) = {by_id, by_surname, by_name, by_group};
    if (!n) return OK;
    if (n == 1 && strcmp(args[0], "list") == 0) {
        for (size_t i = 0; i < c->count; ++i)
            if (print_student(stdout, &c->items[i], false) != OK) return IO;
        return OK;
    }
    Field field = ID;
    if (n == 2 && strcmp(args[0], "sort") == 0) {
        Status st = parse_field(args[1], &field);
        if (st == OK && c->count > 1)
            qsort(c->items, c->count, sizeof *c->items, comparators[field]);
        return st;
    }
    bool above = n == 1 && strcmp(args[0], "above") == 0;
    bool id_log = n == 2 && strcmp(args[0], "trace") == 0;
    bool search = n == 3 && strcmp(args[0], "find") == 0;
    if (!above && !id_log && !search) return INVALID;
    const Student **matches = NULL;
    size_t count = 0;
    Status st = search ? parse_field(args[1], &field) : OK;
    if (st == OK) {
        st = above ? above_average(c, &matches, &count)
                   : find(c, field, args[search ? 2 : 1], &matches, &count);
    }
    for (size_t i = 0; st == OK && i < count; ++i) {
        if (above) {
            if (fprintf(trace, "%s %s\n", matches[i]->surname, matches[i]->name) < 0) st = IO;
        } else st = print_student(search ? stdout : trace, matches[i], id_log);
    }
    free(matches);
    if (st == OK && !count && puts("No students") == EOF) st = IO;
    if (st == OK && !search && fflush(trace) == EOF) st = IO;
    return st;
}

static Status interact(Class *c, FILE *trace)
{
    if (puts("Commands: find FIELD VALUE; sort FIELD; list; trace ID; above; quit\n"
             "Fields: id surname name group") == EOF) return IO;
    Buffer line = {0};
    Status st;
    while ((st = read_line(stdin, &line)) != END) {
        char *args[3];
        size_t n;
        if (st == OK) st = split(line.data, args, 3, &n);
        if (st == OK && n == 1 && strcmp(args[0], "quit") == 0) break;
        if (st == OK) st = command(c, trace, args, n);
        if (st == IO || st == MEMORY) break;
        report(st);
    }
    free(line.data);
    return st == END ? OK : st;
}

int main(int argc, char **argv)
{
    Status st = arg_count(argc, argv, 3, 3);
    Class class = {0};
    FILE *in = NULL, *trace = NULL;
    if (st == OK) st = distinct_files(argv[1], argv[2]);
    if (st == OK) st = open_file(argv[1], "r", &in);
    if (st == OK) st = load(in, &class);
    st = close_file(in, st);
    if (st == OK) st = open_file(argv[2], "w", &trace);
    if (st == OK) st = interact(&class, trace);
    st = close_file(trace, st);
    for (size_t i = 0; i < class.count; ++i) clear_student(&class.items[i]);
    free(class.items);
    return result(st);
}
