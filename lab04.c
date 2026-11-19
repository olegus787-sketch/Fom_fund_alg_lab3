#include "string_type.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct {
    String city, street, building, index;
    uintmax_t house, apartment;
} Address;

typedef struct {
    Address recipient;
    double weight;
    String id, created, due;
    int64_t created_at, due_at;
    bool delivered;
} Mail;

typedef struct {
    Address *office;
    Mail *items;
    size_t count, cap;
} Post;

static void clear_address(Address *a)
{
    string_clear(&a->city);
    string_clear(&a->street);
    string_clear(&a->building);
    string_clear(&a->index);
}

static void clear_mail(Mail *mail)
{
    clear_address(&mail->recipient);
    string_clear(&mail->id);
    string_clear(&mail->created);
    string_clear(&mail->due);
}

static Status parse_address(char **f, Address *a)
{
    if (!f || !a || !*f[0] || !*f[1] || digits(f[5], 6) != OK) return INVALID;
    Status st = unsigned_number(f[2], UINTMAX_MAX, &a->house);
    if (st == OK) st = unsigned_number(f[4], UINTMAX_MAX, &a->apartment);
    if (st == OK && (!a->house || !a->apartment)) st = INVALID;
    if (st == OK) st = string_set(&a->city, f[0]);
    if (st == OK) st = string_set(&a->street, f[1]);
    if (st == OK) st = string_set(&a->building, f[3]);
    if (st == OK) st = string_set(&a->index, f[5]);
    return st;
}

static Status pipe_fields(char *line, char **fields, size_t count)
{
    if (!line || !fields || !count) return INVALID;
    for (size_t i = 0; i < count; ++i) {
        fields[i] = line;
        char *sep = strchr(line, '|');
        if (i + 1 == count) return sep ? INVALID : OK;
        if (!sep) return INVALID;
        *sep = '\0';
        line = sep + 1;
    }
    return INVALID;
}

static Status parse_mail(char *line, Mail *m, double eps)
{
    if (!line || !m) return INVALID;
    char *fields[10];
    Status st = pipe_fields(line, fields, 10);
    if (st == OK) st = parse_address(fields, &m->recipient);
    if (st == OK) st = real_number(fields[6], &m->weight);
    if (st == OK && real_order(m->weight, 0, eps) < 0) st = INVALID;
    if (st == OK) st = digits(fields[7], 14);
    Date created, due;
    if (st == OK) st = timestamp(fields[8], ':', &created);
    if (st == OK) st = timestamp(fields[9], ':', &due);
    if (st == OK) {
        m->created_at = date_seconds(&created);
        m->due_at = date_seconds(&due);
        if (m->due_at < m->created_at) st = INVALID;
    }
    if (st == OK) st = string_set(&m->id, fields[7]);
    if (st == OK) st = string_set(&m->created, fields[8]);
    if (st == OK) st = string_set(&m->due, fields[9]);
    return st;
}

static Status locate(const Post *post, const char *id, size_t *index)
{
    if (!post || !index || digits(id, 14) != OK) return INVALID;
    for (size_t i = 0; i < post->count; ++i) {
        if (strcmp(id, post->items[i].id.data) == 0) {
            *index = i;
            return OK;
        }
    }
    return NOT_FOUND;
}

static int mail_order(const Mail *a, const Mail *b)
{
    int order = strcmp(a->recipient.index.data, b->recipient.index.data);
    return order ? order : strcmp(a->id.data, b->id.data);
}

static Status add(Post *post, Mail *m)
{
    if (!post || !m || !m->id.data) return INVALID;
    size_t index;
    Status st = locate(post, m->id.data, &index);
    if (st == OK) return DUPLICATE;
    if (st != NOT_FOUND) return st;
    void *memory;
    st = reserve(&memory, post->items, &post->cap, post->count + 1, sizeof *post->items);
    if (st != OK) return st;
    post->items = memory;
    index = post->count;
    while (index && mail_order(m, &post->items[index - 1]) < 0) {
        post->items[index] = post->items[index - 1];
        --index;
    }
    post->items[index] = *m;
    *m = (Mail){0};
    ++post->count;
    return OK;
}

static Status remove_mail(Post *post, const char *id)
{
    size_t i;
    Status st = locate(post, id, &i);
    if (st != OK) return st;
    clear_mail(&post->items[i]);
    --post->count;
    memmove(post->items + i, post->items + i + 1, (post->count - i) * sizeof *post->items);
    return OK;
}

static Status print_mail(const Mail *m)
{
    const Address *a = &m->recipient;
    return printf("%s | %s, %s, %" PRIuMAX "%s%s, apt %" PRIuMAX
                  " | %s | %.10g | %s | %s | %s\n",
                  m->id.data, a->city.data, a->street.data, a->house,
                  a->building.length ? "/" : "", a->building.data, a->apartment,
                  a->index.data, m->weight, m->created.data, m->due.data,
                  m->delivered ? "delivered" : "pending") < 0 ? IO : OK;
}

static int by_creation(const void *left, const void *right)
{
    const Mail *const *a = left;
    const Mail *const *b = right;
    int order = int_order((uintmax_t)(*a)->created_at, (uintmax_t)(*b)->created_at);
    return order ? order : mail_order(*a, *b);
}

static Status select_mail(const Post *p, bool delivered, int64_t now,
                          const Mail ***selected, size_t *count)
{
    if (!p || !selected || !count) return INVALID;
    const Mail **items = p->count ? malloc(p->count * sizeof *items) : NULL;
    if (p->count && !items) return MEMORY;
    size_t used = 0;
    for (size_t i = 0; i < p->count; ++i) {
        const Mail *m = &p->items[i];
        if (delivered ? m->delivered : !m->delivered && m->due_at < now)
            items[used++] = m;
    }
    if (used > 1) qsort(items, used, sizeof *items, by_creation);
    *selected = items;
    *count = used;
    return OK;
}

static Status print_selected(const Post *post, bool delivered)
{
    int64_t now = 0;
    if (!delivered) {
        time_t clock = time(NULL);
        if (clock == (time_t)-1) return IO;
        const struct tm *local = localtime(&clock);
        if (!local) return IO;
        Date d = {local->tm_year + 1900, local->tm_mon + 1, local->tm_mday,
                  local->tm_hour, local->tm_min, local->tm_sec};
        now = date_seconds(&d);
    }
    const Mail **items = NULL;
    size_t count = 0;
    Status st = select_mail(post, delivered, now, &items, &count);
    for (size_t i = 0; st == OK && i < count; ++i) st = print_mail(items[i]);
    free(items);
    if (st == OK && !count && puts("No mail") == EOF) st = IO;
    return st;
}

static Status string_demo(void)
{
    String a = {0}, b = {0}, *clone = NULL;
    Status st = string_set(&a, "cat");
    if (st == OK) st = string_set(&b, "fish");
    int order = 0;
    if (st == OK) st = string_order(&a, &b, &order);
    if (st == OK) st = string_copy(&b, &a);
    if (st == OK) st = string_clone(&b, &clone);
    bool equal = false;
    if (st == OK) st = string_equal(&a, clone, &equal);
    if (st == OK) st = string_concat(&a, &b);
    if (st == OK && printf("order=%d equal=%d concat=%s copy=%s clone=%s\n",
                            order, equal, a.data, b.data, clone->data) < 0) st = IO;
    string_clear(&a);
    string_clear(&b);
    if (clone) string_clear(clone);
    free(clone);
    return st;
}

static Status interact(Post *post, Buffer *line)
{
    if (puts("Commands: add city|street|house|building|apt|index|weight|id|created|due; "
             "find ID; remove ID; deliver ID; delivered; expired; list; quit") == EOF)
        return IO;
    while (true) {
        Status st = read_line(stdin, line);
        if (st == END || (st == OK && strcmp(line->data, "quit") == 0)) return OK;
        if (st == OK && line->len == 0) continue;
        if (st == OK && strncmp(line->data, "add ", 4) == 0) {
            Mail m = {0};
            st = parse_mail(line->data + 4, &m, 0);
            if (st == OK) st = add(post, &m);
            clear_mail(&m);
        } else if (st == OK && strncmp(line->data, "remove ", 7) == 0) {
            st = remove_mail(post, line->data + 7);
        } else if (st == OK && strncmp(line->data, "find ", 5) == 0) {
            size_t i;
            st = locate(post, line->data + 5, &i);
            if (st == OK) st = print_mail(&post->items[i]);
        } else if (st == OK && strncmp(line->data, "deliver ", 8) == 0) {
            size_t i;
            st = locate(post, line->data + 8, &i);
            if (st == OK) post->items[i].delivered = true;
        } else if (st == OK && strcmp(line->data, "delivered") == 0) {
            st = print_selected(post, true);
        } else if (st == OK && strcmp(line->data, "expired") == 0) {
            st = print_selected(post, false);
        } else if (st == OK && strcmp(line->data, "list") == 0) {
            for (size_t i = 0; st == OK && i < post->count; ++i) st = print_mail(&post->items[i]);
        } else if (st == OK) st = INVALID;
        if (st == MEMORY || st == IO) return st;
        report(st);
    }
}

int main(int argc, char **argv)
{
    Status st = arg_count(argc, argv, 1, 2);
    if (st != OK) return result(st);
    if (argc == 2) return result(strcmp(argv[1], "string") == 0 ? string_demo() : INVALID);
    Address office = {0};
    Post post = {&office, NULL, 0, 0};
    Buffer line = {0};
    if (puts("Office address: city|street|house|building|apt|index") == EOF) st = IO;
    if (st == OK) st = read_line(stdin, &line);
    char *fields[6];
    if (st == OK) st = pipe_fields(line.data, fields, 6);
    if (st == OK) st = parse_address(fields, post.office);
    if (st == OK) st = interact(&post, &line);
    for (size_t i = 0; i < post.count; ++i) clear_mail(&post.items[i]);
    free(post.items);
    clear_address(&office);
    free(line.data);
    return result(st);
}
