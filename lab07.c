#include "common.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

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
