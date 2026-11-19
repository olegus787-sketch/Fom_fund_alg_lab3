#include <ctype.h>
#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef enum {
    OK, END, INVALID, RANGE, MEMORY, IO, NOT_FOUND, DUPLICATE, DOMAIN
} Status;

typedef struct {
    char *data;
    size_t len, cap;
} Buffer;

typedef struct {
    int year, month, day, hour, minute, second;
} Date;

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

static Status real_number(const char *s, double *out)
{
    if (!s || !*s || !out || isspace((unsigned char)*s)) return INVALID;
    char *end;
    double x = strtod(s, &end);
    if (end == s || *end) return INVALID;
    if (!isfinite(x)) return RANGE;
    *out = x;
    return OK;
}

static Status digits(const char *s, size_t n)
{
    if (!s || strlen(s) != n) return INVALID;
    for (size_t i = 0; i < n; ++i) if (s[i] < '0' || s[i] > '9') return INVALID;
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

static Status date_parts(int year, int month, int day, Date *out)
{
    const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (!out || year < 1 || year > 9999 || month < 1 || month > 12 || day < 1)
        return INVALID;
    bool leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
    if (day > days[month - 1] + (leap && month == 2)) return INVALID;
    *out = (Date){year, month, day, 0, 0, 0};
    return OK;
}

static Status timestamp(const char *s, char separator, Date *out)
{
    if (!s || !out || strlen(s) != 19) return INVALID;
    const char format[] = {'0','0',separator,'0','0',separator,
                           '0','0','0','0',' ','0','0',':','0','0',':','0','0'};
    for (size_t i = 0; i < sizeof format; ++i) {
        if (format[i] == '0' ? (s[i] < '0' || s[i] > '9') : s[i] != format[i])
            return INVALID;
    }
    int day, month, year, hour, minute, second;
    if (sscanf(s + 6, "%4d %2d:%2d:%2d", &year, &hour, &minute, &second) != 4)
        return INVALID;
    day = (s[0] - '0') * 10 + s[1] - '0';
    month = (s[3] - '0') * 10 + s[4] - '0';
    if (hour > 23 || minute > 59 || second > 59) return INVALID;
    Status st = date_parts(year, month, day, out);
    if (st == OK) {
        out->hour = hour;
        out->minute = minute;
        out->second = second;
    }
    return st;
}

static int64_t date_seconds(const Date *date)
{
    const int before_month[] = {0,31,59,90,120,151,181,212,243,273,304,334};
    int y = date->year - 1;
    bool leap = date->year % 4 == 0 && (date->year % 100 != 0 || date->year % 400 == 0);
    int64_t days = y * 365 + y / 4 - y / 100 + y / 400;
    days += before_month[date->month - 1] + date->day - 1;
    if (leap && date->month > 2) ++days;
    return ((days * 24 + date->hour) * 60 + date->minute) * 60 + date->second;
}

static int int_order(uintmax_t lhs, uintmax_t rhs)
{
    if (lhs < rhs) return -1;
    if (lhs > rhs) return 1;
    return 0;
}

static int real_order(double lhs, double rhs, double eps)
{
    double delta = lhs - rhs;
    if (fabs(delta) <= eps) return 0;
    return delta < 0 ? -1 : 1;
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

/* Строки для адресов и отправлений. */
typedef struct { char *data; int length; } String;

static bool valid(const String *s)
{
    return s && s->length >= 0 && (s->data ? strlen(s->data) == (size_t)s->length
                                         : s->length == 0);
}

static const char *text(const String *s)
{
    return s->data ? s->data : "";
}

static Status string_set(String *dest, const char *source)
{
    if (!valid(dest) || !source) return INVALID;
    size_t len = strlen(source);
    if (len > INT_MAX) return RANGE;
    char *data;
    Status st = copy_text(source, &data);
    if (st != OK) return st;
    free(dest->data);
    *dest = (String){data, (int)len};
    return OK;
}

static Status string_clear(String *s)
{
    if (!s) return INVALID;
    free(s->data);
    *s = (String){0};
    return OK;
}

static Status string_order(const String *a, const String *b, int *order)
{
    if (!valid(a) || !valid(b) || !order) return INVALID;
    *order = a->length == b->length ? strcmp(text(a), text(b))
                                  : int_order((unsigned)a->length, (unsigned)b->length);
    return OK;
}

static Status string_equal(const String *a, const String *b, bool *equal)
{
    if (!valid(a) || !valid(b) || !equal) return INVALID;
    *equal = strcmp(text(a), text(b)) == 0;
    return OK;
}

static Status string_copy(String *dest, const String *source)
{
    if (!valid(source)) return INVALID;
    return string_set(dest, text(source));
}

static Status string_clone(const String *source, String **out)
{
    if (!valid(source) || !out) return INVALID;
    String *s = calloc(1, sizeof *s);
    if (!s) return MEMORY;
    Status st = string_copy(s, source);
    if (st == OK) *out = s;
    else free(s);
    return st;
}

static Status string_concat(String *dest, const String *suffix)
{
    if (!valid(dest) || !valid(suffix)) return INVALID;
    if (dest->length > INT_MAX - suffix->length) return RANGE;
    int length = dest->length + suffix->length;
    char *data = malloc((size_t)length + 1);
    if (!data) return MEMORY;
    memcpy(data, text(dest), (size_t)dest->length);
    memcpy(data + dest->length, text(suffix), (size_t)suffix->length + 1);
    free(dest->data);
    *dest = (String){data, length};
    return OK;
}

/* Почтовое отделение. */
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
