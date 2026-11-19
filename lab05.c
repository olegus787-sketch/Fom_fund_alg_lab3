#include "common.h"

#include <ctype.h>
#include <inttypes.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

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
