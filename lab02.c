#include <ctype.h>
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

static Status digits(const char *s, size_t n)
{
    if (!s || strlen(s) != n) return INVALID;
    for (size_t i = 0; i < n; ++i) if (s[i] < '0' || s[i] > '9') return INVALID;
    return OK;
}

static int int_order(uintmax_t lhs, uintmax_t rhs)
{
    if (lhs < rhs) return -1;
    if (lhs > rhs) return 1;
    return 0;
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
enum { MINUTES = 1440 };
typedef struct { uintmax_t id; unsigned start, end; } Talk;

static int by_id(const void *a, const void *b)
{
    return int_order(((const Talk *)a)->id, ((const Talk *)b)->id);
}

static int by_end(const void *a, const void *b)
{
    return int_order(((const Talk *)a)->end, ((const Talk *)b)->end);
}

static Status schedule(const Talk *talks, size_t n, size_t halls, size_t *answer)
{
    if (!talks || !n || !halls || !answer) return INVALID;
    if (n > SIZE_MAX / sizeof(Talk)) return RANGE;
    Talk *ordered = malloc(n * sizeof *ordered);
    if (!ordered) return MEMORY;
    for (size_t i = 0; i < n; ++i) {
        if (!talks[i].id || talks[i].start >= talks[i].end || talks[i].end >= MINUTES) {
            free(ordered);
            return INVALID;
        }
        ordered[i] = talks[i];
    }
    qsort(ordered, n, sizeof *ordered, by_id);
    for (size_t i = 1; i < n; ++i) {
        if (ordered[i-1].id == ordered[i].id) {
            free(ordered);
            return DUPLICATE;
        }
    }
    qsort(ordered, n, sizeof *ordered, by_end);

    /* free_at[t] — сколько залов освободилось в минуту t. */
    size_t free_at[MINUTES] = {0};
    free_at[0] = halls;
    size_t selected = 0;
    for (size_t i = 0; i < n; ++i) {
        int minute = (int)ordered[i].start;
        while (minute >= 0 && free_at[minute] == 0) --minute;
        if (minute < 0) continue;

        /* Берём самый поздний подходящий зал, оставляя более ранние свободными. */
        --free_at[minute];
        ++free_at[ordered[i].end];
        ++selected;
    }
    free(ordered);
    *answer = selected;
    return OK;
}

static Status clock_time(const char *s, unsigned *minutes)
{
    if (!s || !minutes) return INVALID;
    if (!s[0] || !s[1] || s[2] != ':' || digits(s + 3, 2) != OK) return INVALID;
    char hours[] = {s[0], s[1], '\0'};
    uintmax_t h, m;
    Status st = unsigned_number(hours, 23, &h);
    if (st == OK) st = unsigned_number(s + 3, 59, &m);
    if (st == OK) *minutes = (unsigned)(h * 60 + m);
    return st;
}

static Status read_fields(Buffer *line, char **fields, size_t n)
{
    Status st = read_line(stdin, line);
    size_t count;
    if (st == OK) st = split(line->data, fields, n, &count);
    if (st == OK && count != n) st = INVALID;
    return st;
}

int main(int argc, char **argv)
{
    Status st = arg_count(argc, argv, 1, 1);
    Buffer line = {0};
    char *fields[3];
    uintmax_t n = 0, k = 0;
    if (st == OK) st = read_fields(&line, fields, 2);
    if (st == OK) st = unsigned_number(fields[0], SIZE_MAX / sizeof(Talk), &n);
    if (st == OK) st = unsigned_number(fields[1], SIZE_MAX, &k);
    if (st == OK && (!n || !k)) st = INVALID;
    Talk *talks = NULL;
    if (st == OK) {
        talks = malloc((size_t)n * sizeof *talks);
        if (!talks) st = MEMORY;
    }
    for (size_t i = 0; st == OK && i < n; ++i) {
        st = read_fields(&line, fields, 3);
        if (st == OK) st = unsigned_number(fields[0], UINTMAX_MAX, &talks[i].id);
        if (st == OK) st = clock_time(fields[1], &talks[i].start);
        if (st == OK) st = clock_time(fields[2], &talks[i].end);
    }
    size_t answer;
    if (st == OK) st = schedule(talks, (size_t)n, (size_t)k, &answer);
    if (st == OK && printf("%zu\n", answer) < 0) st = IO;
    free(talks);
    free(line.data);
    return result(st);
}
