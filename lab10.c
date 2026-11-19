#include "common.h"

#include <ctype.h>
#include <stdlib.h>

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
