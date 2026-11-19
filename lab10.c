#include <ctype.h>
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
typedef struct Node {
    char *label;
    struct Node *parent, *first, *last, *next;
} Node;

static void clear(Node *p)
{
    while (p) {
        if (p->first) {
            Node *child = p->first;
            p->first = NULL;
            p = child;
        } else {
            Node *next = p->next ? p->next : p->parent;
            free(p->label);
            free(p);
            p = next;
        }
    }
}

static void skip_space(const char **s)
{
    while (isspace((unsigned char)**s)) ++*s;
}

static Status read_node(const char **s, Node *parent, Node **out)
{
    skip_space(s);
    const char *begin = *s, *end = begin;
    while (**s && **s != '(' && **s != ')' && **s != ',') {
        if (!isspace((unsigned char)**s)) end = *s + 1;
        ++*s;
    }
    if (end == begin) return INVALID;
    Node *p = calloc(1, sizeof *p);
    if (!p) return MEMORY;
    size_t length = (size_t)(end - begin);
    p->label = malloc(length + 1);
    if (!p->label) { free(p); return MEMORY; }
    for (size_t i = 0; i < length; ++i) p->label[i] = begin[i];
    p->label[length] = '\0';
    p->parent = parent;
    if (parent) {
        if (parent->last) parent->last->next = p;
        else parent->first = p;
        parent->last = p;
    }
    *out = p;
    return OK;
}

static Status parse(const char *s, Node **out)
{
    if (!s || !out) return INVALID;
    Node *current = NULL;
    Status st = read_node(&s, NULL, out);
    if (st != OK) return st;
    current = *out;
    /* У каждого узла может быть только один список детей в скобках. */
    bool can_open = true;
    while (st == OK) {
        skip_space(&s);
        if (*s == '(' && can_open) {
            ++s;
            skip_space(&s);
            if (*s == ')') {
                ++s;
                can_open = false;
            } else st = read_node(&s, current, &current);
        } else if (*s == ',' && current->parent) {
            ++s;
            st = read_node(&s, current->parent, &current);
            can_open = true;
        } else if (*s == ')' && current->parent) {
            ++s;
            current = current->parent;
            can_open = false;
        } else return !*s && !current->parent ? OK : INVALID;
    }
    return st;
}

static Status print_tree(FILE *out, const Node *root)
{
    if (!out || !root) return INVALID;
    size_t depth = 0;
    const Node *p = root;
    while (p) {
        for (size_t i = 0; i < depth; ++i)
            if (fputs("  ", out) == EOF) return IO;
        if (fprintf(out, "%s\n", p->label) < 0) return IO;
        if (p->first) {
            ++depth;
            p = p->first;
        } else {
            while (p != root && !p->next) { p = p->parent; --depth; }
            p = p == root ? NULL : p->next;
        }
    }
    return OK;
}

static Status process(FILE *in, FILE *out)
{
    Buffer line = {0};
    size_t number = 1;
    Status st;
    while ((st = read_line(in, &line)) == OK) {
        Node *root = NULL;
        st = parse(line.data, &root);
        if (st == OK && fprintf(out, "Tree %zu\n", number) < 0) st = IO;
        if (st == OK) st = print_tree(out, root);
        if (st == OK && fputc('\n', out) == EOF) st = IO;
        clear(root);
        if (st != OK) break;
        ++number;
    }
    if (st != END) fprintf(stderr, "Line %zu: ", number);
    free(line.data);
    return st == END ? OK : st;
}

int main(int argc, char **argv)
{
    Status st = arg_count(argc, argv, 3, 3);
    FILE *in = NULL, *out = NULL;
    if (st == OK) st = distinct_files(argv[1], argv[2]);
    if (st == OK) st = open_file(argv[1], "r", &in);
    if (st == OK) st = open_file(argv[2], "w", &out);
    if (st == OK) st = process(in, out);
    st = close_file(in, st);
    return result(close_file(out, st));
}
