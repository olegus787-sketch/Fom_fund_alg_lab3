#include "common.h"

#include <stdlib.h>

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

/* Tree entry i stores the sum over the interval ending at i of length i & -i. */
static void room_update(size_t *tree, unsigned i, bool add)
{
    for (; i <= MINUTES; i += i & (0u - i)) {
        if (add) ++tree[i];
        else --tree[i];
    }
}

static size_t room_count(const size_t *tree, unsigned i)
{
    size_t sum = 0;
    for (; i; i &= i - 1) sum += tree[i];
    return sum;
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

    size_t tree[MINUTES + 1] = {0};
    for (unsigned i = 1; i <= MINUTES; i *= 2) tree[i] = halls;
    size_t selected = 0;
    for (size_t i = 0; i < n; ++i) {
        unsigned limit = ordered[i].start + 1;
        size_t count = room_count(tree, limit);
        if (!count) continue;
        unsigned lo = 1, hi = limit;
        while (lo < hi) {
            unsigned middle = lo + (hi - lo) / 2;
            if (room_count(tree, middle) < count) lo = middle + 1;
            else hi = middle;
        }
        room_update(tree, lo, false);
        room_update(tree, ordered[i].end + 1, true);
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
