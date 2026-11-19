#include <ctype.h>
#include <inttypes.h>
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

static int real_order(double lhs, double rhs, double eps)
{
    double delta = lhs - rhs;
    if (fabs(delta) <= eps) return 0;
    return delta < 0 ? -1 : 1;
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
typedef struct {
    char *surname, *name, *patronymic;
    Date birth;
    char sex;
    double income;
} Liver;

typedef struct Person {
    Liver data;
    uintmax_t id;
    struct Person *next;
} Person;

typedef enum { ADDED, REMOVED, CHANGED } Action;
typedef struct Change {
    Action action;
    Person *person;
    Liver previous;
    struct Change *next;
} Change;

typedef struct { Person *head; Change *history; size_t changes; uintmax_t last_id; } Village;

static void clear_liver(Liver *l)
{
    free(l->surname);
    free(l->name);
    free(l->patronymic);
    *l = (Liver){0};
}

static void clear_person(Person *p)
{
    clear_liver(&p->data);
    free(p);
}

static Status parse_record(char **f, size_t n, double eps, Liver *out)
{
    if (!f || !out || (n != 7 && n != 8)) return INVALID;
    const char *patronymic = n == 7 || strcmp(f[2], "-") == 0 ? "" : f[2];
    unsigned offset = n == 8 ? 1 : 0;
    if (latin(f[0], false) != OK || latin(f[1], false) != OK ||
        latin(patronymic, true) != OK ||
        (strcmp(f[5 + offset], "M") != 0 && strcmp(f[5 + offset], "W") != 0))
        return INVALID;
    uintmax_t day, month, year;
    Status st = unsigned_number(f[2 + offset], 31, &day);
    if (st == OK) st = unsigned_number(f[3 + offset], 12, &month);
    if (st == OK) st = unsigned_number(f[4 + offset], 9999, &year);
    if (st == OK) st = date_parts((int)year, (int)month, (int)day, &out->birth);
    if (st == OK) st = real_number(f[6 + offset], &out->income);
    if (st == OK && real_order(out->income, 0, eps) < 0) st = INVALID;
    if (st == OK) st = copy_text(f[0], &out->surname);
    if (st == OK) st = copy_text(f[1], &out->name);
    if (st == OK) st = copy_text(patronymic, &out->patronymic);
    if (st == OK) out->sex = f[5 + offset][0];
    return st;
}

static void insert(Village *v, Person *p)
{
    int64_t date = date_seconds(&p->data.birth);
    Person **pos = &v->head;
    while (*pos) {
        int64_t other = date_seconds(&(*pos)->data.birth);
        if (date > other || (date == other && p->id < (*pos)->id)) break;
        pos = &(*pos)->next;
    }
    p->next = *pos;
    *pos = p;
}

static Status locate(Village *v, uintmax_t id, Person ***slot)
{
    if (!v || !id || !slot) return INVALID;
    for (Person **p = &v->head; *p; p = &(*p)->next) {
        if ((*p)->id == id) {
            *slot = p;
            return OK;
        }
    }
    return NOT_FOUND;
}

static Status new_change(Village *v, Action action, Person *p, Change **record)
{
    if (!v || !p || !record) return INVALID;
    if (v->changes == SIZE_MAX) return RANGE;
    Change *c = malloc(sizeof *c);
    if (!c) return MEMORY;
    *c = (Change){.action = action, .person = p, .next = v->history};
    *record = c;
    return OK;
}

static void commit_change(Village *v, Change *record)
{
    v->history = record;
    ++v->changes;
}

static Status add(Village *v, Liver *l, bool record)
{
    if (!v || !l) return INVALID;
    if (v->last_id == UINTMAX_MAX) return RANGE;
    Person *p = malloc(sizeof *p);
    if (!p) return MEMORY;
    Change *change = NULL;
    Status st = record ? new_change(v, ADDED, p, &change) : OK;
    if (st != OK) { free(p); return st; }
    *p = (Person){*l, ++v->last_id, NULL};
    *l = (Liver){0};
    insert(v, p);
    if (record) commit_change(v, change);
    return OK;
}

static Status modify(Village *v, uintmax_t id, Liver *replacement)
{
    Person **slot;
    Status st = locate(v, id, &slot);
    if (st != OK) return st;
    Person *p = *slot;
    Change *change;
    st = new_change(v, replacement ? CHANGED : REMOVED, p, &change);
    if (st != OK) return st;
    *slot = p->next;
    p->next = NULL;
    if (replacement) {
        change->previous = p->data;
        p->data = *replacement;
        *replacement = (Liver){0};
        insert(v, p);
    }
    commit_change(v, change);
    return OK;
}

static void discard_history(Village *v)
{
    while (v->history) {
        Change *c = v->history;
        v->history = c->next;
        if (c->action == REMOVED) clear_person(c->person);
        if (c->action == CHANGED) clear_liver(&c->previous);
        free(c);
    }
    v->changes = 0;
}

static Status undo(Village *v, size_t *undone)
{
    if (!v || !undone) return INVALID;
    /* Свежая правка лежит в начале журнала, поэтому идём с конца истории. */
    *undone = v->changes / 2;
    for (size_t i = 0; i < *undone; ++i) {
        Change *c = v->history;
        if (c->action == REMOVED) {
            insert(v, c->person);
        } else {
            Person **slot;
            Status st = locate(v, c->person->id, &slot);
            if (st != OK) return st;
            Person *p = *slot;
            *slot = p->next;
            if (c->action == ADDED) clear_person(p);
            else {
                clear_liver(&p->data);
                p->data = c->previous;
                insert(v, p);
            }
        }
        v->history = c->next;
        --v->changes;
        free(c);
    }
    discard_history(v);
    return OK;
}

static Status print_record(FILE *out, const Person *p, bool show_id)
{
    const Liver *l = &p->data;
    if (show_id && fprintf(out, "%" PRIuMAX " ", p->id) < 0) return IO;
    return fprintf(out, "%s %s %s %02d %02d %04d %c %.17g\n", l->surname, l->name,
                   *l->patronymic ? l->patronymic : "-", l->birth.day, l->birth.month,
                   l->birth.year, l->sex, l->income) < 0 ? IO : OK;
}

static Status export(const Village *v, const char *path)
{
    FILE *out = NULL;
    Status st = open_file(path, "w", &out);
    for (const Person *p = v->head; st == OK && p; p = p->next)
        st = print_record(out, p, false);
    return close_file(out, st);
}

static Status load(FILE *file, Village *v)
{
    Buffer line = {0};
    Status st;
    while ((st = read_line(file, &line)) == OK) {
        char *f[8];
        size_t n;
        st = split(line.data, f, 8, &n);
        if (st != OK) break;
        if (!n) continue;
        Liver l = {0};
        st = parse_record(f, n, 0, &l);
        if (st == OK) st = add(v, &l, false);
        clear_liver(&l);
        if (st != OK) break;
    }
    free(line.data);
    return st == END ? OK : st;
}

static Status matches(const Liver *a, const Liver *b, double eps, bool *equal)
{
    if (!a || !b || !equal) return INVALID;
    *equal = strcmp(a->surname, b->surname) == 0 && strcmp(a->name, b->name) == 0 &&
             strcmp(a->patronymic, b->patronymic) == 0 && a->sex == b->sex &&
             date_seconds(&a->birth) == date_seconds(&b->birth) &&
             real_order(a->income, b->income, eps) == 0;
    return OK;
}

static Status show(const Village *v, const Liver *query)
{
    bool found = false;
    for (const Person *p = v->head; p; p = p->next) {
        bool equal = true;
        if (query) {
            Status st = matches(&p->data, query, 1e-9, &equal);
            if (st != OK) return st;
        }
        if (equal) {
            Status st = print_record(stdout, p, true);
            if (st != OK) return st;
            found = true;
        }
    }
    return found || puts("No residents") != EOF ? OK : IO;
}

static Status command(Village *v, Buffer *line)
{
    if (strncmp(line->data, "save ", 5) == 0) return export(v, line->data + 5);
    char *args[10];
    size_t n;
    Status st = split(line->data, args, 10, &n);
    if (st != OK || !n) return st;
    if (n == 1 && strcmp(args[0], "list") == 0) return show(v, NULL);
    if (n == 1 && strcmp(args[0], "undo") == 0) {
        size_t undone;
        st = undo(v, &undone);
        if (st == OK && printf("Undone: %zu\n", undone) < 0) st = IO;
        return st;
    }
    bool remove = n == 2 && strcmp(args[0], "remove") == 0;
    bool change = n >= 3 && strcmp(args[0], "change") == 0;
    bool find = strcmp(args[0], "find") == 0;
    bool adding = strcmp(args[0], "add") == 0;
    if (!remove && !change && !find && !adding) return INVALID;
    uintmax_t id = 0;
    if (remove || change) st = unsigned_number(args[1], UINTMAX_MAX, &id);
    if (st != OK) return st;
    if (remove) return modify(v, id, NULL);
    size_t offset = change ? 2 : 1;
    Liver l = {0};
    st = parse_record(args + offset, n - offset, 0, &l);
    if (st == OK) {
        if (find) st = show(v, &l);
        else if (adding) st = add(v, &l, true);
        else st = modify(v, id, &l);
    }
    clear_liver(&l);
    return st;
}

static Status interact(Village *v)
{
    if (puts("Commands: list; find RECORD; add RECORD; change ID RECORD; remove ID; "
             "undo; save PATH; quit\nRecord: surname name patronymic day month year M|W income")
        == EOF) return IO;
    Buffer line = {0};
    Status st;
    while ((st = read_line(stdin, &line)) != END) {
        if (st == OK && strcmp(line.data, "quit") == 0) break;
        if (st == OK) st = command(v, &line);
        if (st == MEMORY || st == IO) break;
        report(st);
    }
    free(line.data);
    return st == END ? OK : st;
}

int main(int argc, char **argv)
{
    Status st = arg_count(argc, argv, 2, 2);
    Village v = {0};
    FILE *in = NULL;
    if (st == OK) st = open_file(argv[1], "r", &in);
    if (st == OK) st = load(in, &v);
    st = close_file(in, st);
    if (st == OK) st = interact(&v);
    discard_history(&v);
    while (v.head) {
        Person *next = v.head->next;
        clear_person(v.head);
        v.head = next;
    }
    return result(st);
}
