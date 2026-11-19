#include "common.h"

#include <inttypes.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

typedef struct Word {
    char *text;
    uintmax_t count;
    struct Word *left, *right, *parent;
} Word;

typedef struct { char bytes[5]; size_t len; } Symbol;

static void clear_tree(Word *root)
{
    while (root) {
        if (root->left) {
            Word *next = root->left;
            root->left = NULL;
            root = next;
        } else if (root->right) {
            Word *next = root->right;
            root->right = NULL;
            root = next;
        } else {
            Word *parent = root->parent;
            free(root->text);
            free(root);
            root = parent;
        }
    }
}

static Status new_word(const char *s, uintmax_t n, Word *parent, Word **out)
{
    if (!s || !*s || !n || !out) return INVALID;
    Word *node = calloc(1, sizeof *node);
    if (!node) return MEMORY;
    Status st = copy_text(s, &node->text);
    if (st != OK) { free(node); return st; }
    node->count = n;
    node->parent = parent;
    *out = node;
    return OK;
}

static Status insert(Word **root, const char *s)
{
    if (!root || !s || !*s) return INVALID;
    Word *parent = NULL;
    while (*root) {
        parent = *root;
        int order = strcmp(s, parent->text);
        if (!order) {
            if (parent->count == UINTMAX_MAX) return RANGE;
            ++parent->count;
            return OK;
        }
        root = order < 0 ? &parent->left : &parent->right;
    }
    return new_word(s, 1, parent, root);
}

static Status frequency(const Word *p, const char *s, uintmax_t *count)
{
    if (!s || !*s || !count) return INVALID;
    while (p) {
        int order = strcmp(s, p->text);
        if (!order) break;
        p = order < 0 ? p->left : p->right;
    }
    *count = p ? p->count : 0;
    return OK;
}

static const Word *leftmost(const Word *p)
{
    while (p && p->left) p = p->left;
    return p;
}

static const Word *successor(const Word *p)
{
    if (!p) return NULL;
    if (p->right) return leftmost(p->right);
    while (p->parent && p == p->parent->right) p = p->parent;
    return p->parent;
}

static const Word *next_preorder(const Word *p, size_t *depth)
{
    if (p->left || p->right) {
        ++*depth;
        return p->left ? p->left : p->right;
    }
    while (p->parent) {
        if (p == p->parent->left && p->parent->right) return p->parent->right;
        --*depth;
        p = p->parent;
    }
    return NULL;
}

static Status depth_of(const Word *root, size_t *out)
{
    if (!out) return INVALID;
    *out = 0;
    size_t level = 1;
    for (const Word *p = root; p; p = next_preorder(p, &level))
        if (level > *out) *out = level;
    return OK;
}

static size_t codepoints(const char *s);

static Status shortest_longest(const Word *root, const Word **shortest, const Word **longest)
{
    if (!shortest || !longest) return INVALID;
    if (!root) return NOT_FOUND;
    *shortest = *longest = root;
    size_t lo = codepoints(root->text), hi = lo;
    for (const Word *p = leftmost(root); p; p = successor(p)) {
        size_t n = codepoints(p->text);
        if (n < lo) { lo = n; *shortest = p; }
        if (n > hi) { hi = n; *longest = p; }
    }
    return OK;
}

static int by_count(const void *a, const void *b)
{
    const Word *const *x = a;
    const Word *const *y = b;
    int order = int_order((*y)->count, (*x)->count);
    return order ? order : strcmp((*x)->text, (*y)->text);
}

static Status popular(const Word *root, const Word ***out, size_t *count)
{
    if (!out || !count) return INVALID;
    const Word **items = NULL;
    size_t capacity = 0, used = 0;
    for (const Word *p = leftmost(root); p; p = successor(p)) {
        void *memory;
        Status st = reserve(&memory, items, &capacity, used + 1, sizeof *items);
        if (st != OK) { free(items); return st; }
        items = memory;
        items[used++] = p;
    }
    if (used > 1) qsort(items, used, sizeof *items, by_count);
    *out = items;
    *count = used;
    return OK;
}

static size_t utf8_width(int c)
{
    if (c >= 1 && c <= 127) return 1;
    if (c >= 194 && c <= 223) return 2;
    if (c >= 224 && c <= 239) return 3;
    if (c >= 240 && c <= 244) return 4;
    return 0;
}

static Status validate_symbol(const char *s, size_t len)
{
    if (!s || !len || utf8_width((unsigned char)s[0]) != len) return INVALID;
    for (size_t i = 1; i < len; ++i) {
        unsigned byte = (unsigned char)s[i];
        if (byte < 128 || byte > 191) return INVALID;
    }
    unsigned first = (unsigned char)s[0];
    if (len >= 3) {
        unsigned second = (unsigned char)s[1];
        if ((first == 224 && second < 160) || (first == 237 && second > 159) ||
            (first == 240 && second < 144) || (first == 244 && second > 143))
            return INVALID;
    }
    return OK;
}

static Status read_symbol(FILE *file, Symbol *s)
{
    int c = fgetc(file);
    if (c == EOF) return ferror(file) ? IO : END;
    s->len = utf8_width(c);
    if (!s->len) return INVALID;
    ((unsigned char *)s->bytes)[0] = (unsigned char)c;
    for (size_t i = 1; i < s->len; ++i) {
        c = fgetc(file);
        if (c == EOF) return ferror(file) ? IO : INVALID;
        ((unsigned char *)s->bytes)[i] = (unsigned char)c;
    }
    s->bytes[s->len] = '\0';
    return validate_symbol(s->bytes, s->len);
}

static size_t codepoints(const char *s)
{
    size_t length = 0;
    for (; *s; ++s)
        if (((unsigned char)*s & 0xc0u) != 0x80u) ++length;
    return length;
}

static Status read_text(FILE *file, int separators, char **tokens, Word **root)
{
    if (!file || separators < 1 || !tokens || !root) return INVALID;
    for (int i = 0; i < separators; ++i)
        if (validate_symbol(tokens[i], strlen(tokens[i])) != OK) return INVALID;
    Buffer word = {0};
    Status st;
    Symbol symbol;
    while ((st = read_symbol(file, &symbol)) == OK) {
        int i = 0;
        while (i < separators && strcmp(tokens[i], symbol.bytes) != 0) ++i;
        if (i < separators) {
            if (word.len) st = insert(root, word.data);
            word.len = 0;
        } else {
            for (size_t j = 0; st == OK && j < symbol.len; ++j)
                st = append_char(&word, (unsigned char)symbol.bytes[j]);
        }
        if (st != OK) break;
    }
    if (st == END) st = word.len ? insert(root, word.data) : OK;
    free(word.data);
    return st;
}

static Status write_shape(const Word *root, FILE *out)
{
    if (!out) return INVALID;
    if (fputs("BST1\n", out) == EOF) return IO;
    const Word *p = root;
    if (!p) return fputs("#\n", out) == EOF ? IO : OK;
    unsigned phase = 0;
    while (p) {
        if (phase == 0) {
            size_t len = strlen(p->text);
            if (fprintf(out, "W %" PRIuMAX " %zu\n", p->count, len) < 0 ||
                fwrite(p->text, 1, len, out) != len || fputc('\n', out) == EOF)
                return IO;
            if (p->left) p = p->left;
            else {
                if (fputs("#\n", out) == EOF) return IO;
                phase = 1;
            }
        } else if (phase == 1) {
            if (p->right) {
                p = p->right;
                phase = 0;
            } else {
                if (fputs("#\n", out) == EOF) return IO;
                phase = 2;
            }
        } else {
            const Word *child = p;
            p = p->parent;
            if (p) phase = child == p->left ? 1 : 2;
        }
    }
    return OK;
}

static Status read_node(FILE *file, Buffer *line, Word *parent, Word **slot)
{
    Status st = read_line(file, line);
    if (st != OK) return st == END ? INVALID : st;
    if (strcmp(line->data, "#") == 0) return OK;
    char *f[3];
    size_t n;
    st = split(line->data, f, 3, &n);
    if (st != OK || n != 3 || strcmp(f[0], "W") != 0) return INVALID;
    uintmax_t frequency, length;
    st = unsigned_number(f[1], UINTMAX_MAX, &frequency);
    if (st == OK) st = unsigned_number(f[2], SIZE_MAX - 1, &length);
    if (st != OK) return st;
    if (!frequency || !length) return INVALID;
    line->len = 0;
    while (st == OK && line->len < length) {
        Symbol s;
        st = read_symbol(file, &s);
        if (st == END) return INVALID;
        if (st != OK) break;
        if (s.len > length - line->len) return INVALID;
        for (size_t i = 0; st == OK && i < s.len; ++i)
            st = append_char(line, (unsigned char)s.bytes[i]);
    }
    if (st != OK) return st;
    if (fgetc(file) != '\n') return ferror(file) ? IO : INVALID;
    return new_word(line->data, frequency, parent, slot);
}

static Status restore(FILE *file, Word **root)
{
    if (!file || !root) return INVALID;
    Buffer line = {0};
    Word *tree = NULL;
    Status st = read_line(file, &line);
    if (st == END || (st == OK && strcmp(line.data, "BST1") != 0)) st = INVALID;
    if (st == OK) st = read_node(file, &line, NULL, &tree);
    Word *p = tree;
    unsigned phase = 0;
    while (st == OK && p) {
        if (phase < 2) {
            Word **slot = phase == 0 ? &p->left : &p->right;
            st = read_node(file, &line, p, slot);
            if (*slot) {
                p = *slot;
                phase = 0;
            } else ++phase;
        } else {
            Word *child = p;
            p = p->parent;
            if (p) phase = child == p->left ? 1 : 2;
        }
    }
    if (st == OK && fgetc(file) != EOF) st = INVALID;
    if (st == OK && ferror(file)) st = IO;
    const Word *previous = NULL;
    for (const Word *w = leftmost(tree); st == OK && w; w = successor(w)) {
        if (previous && strcmp(previous->text, w->text) >= 0) st = INVALID;
        previous = w;
    }
    free(line.data);
    if (st == OK) *root = tree;
    else clear_tree(tree);
    return st;
}

static Status disk_command(Word **root, const char *path, bool saving, const char *source)
{
    if (saving) {
        Status st = distinct_files(source, path);
        FILE *out = NULL;
        if (st == OK) st = open_file(path, "wb", &out);
        if (st == OK) st = write_shape(*root, out);
        return close_file(out, st);
    }
    FILE *in = NULL;
    Status st = open_file(path, "rb", &in);
    Word *incoming = NULL;
    if (st == OK) st = restore(in, &incoming);
    st = close_file(in, st);
    if (st == OK) {
        clear_tree(*root);
        *root = incoming;
    } else clear_tree(incoming);
    return st;
}

static Status print_word(const Word *w)
{
    return printf("%s %" PRIuMAX "\n", w->text, w->count) < 0 ? IO : OK;
}

static Status command(Word **root, const char *s, const char *source)
{
    if (strcmp(s, "all") == 0) {
        for (const Word *p = leftmost(*root); p; p = successor(p))
            if (print_word(p) != OK) return IO;
        return OK;
    }
    if (strcmp(s, "depth") == 0) {
        size_t depth;
        Status st = depth_of(*root, &depth);
        if (st == OK && printf("%zu\n", depth) < 0) st = IO;
        return st;
    }
    if (strcmp(s, "shortest") == 0 || strcmp(s, "longest") == 0) {
        const Word *a, *b;
        Status st = shortest_longest(*root, &a, &b);
        return st == OK ? print_word(*s == 's' ? a : b) : st;
    }
    if (strncmp(s, "count ", 6) == 0) {
        uintmax_t n;
        Status st = frequency(*root, s + 6, &n);
        if (st == OK && printf("%" PRIuMAX "\n", n) < 0) st = IO;
        return st;
    }
    if (strncmp(s, "top ", 4) == 0) {
        uintmax_t number;
        Status st = unsigned_number(s + 4, SIZE_MAX, &number);
        if (st != OK) return st;
        const Word **items = NULL;
        size_t count = 0;
        st = popular(*root, &items, &count);
        for (size_t i = 0; st == OK && i < count && i < number; ++i) st = print_word(items[i]);
        free(items);
        return st;
    }
    if (strncmp(s, "save ", 5) == 0) return disk_command(root, s + 5, true, source);
    if (strncmp(s, "load ", 5) == 0) return disk_command(root, s + 5, false, source);
    return INVALID;
}

static Status interact(Word **root, const char *source)
{
    if (puts("Commands: all; count WORD; top N; shortest; longest; depth; "
             "save PATH; load PATH; quit") == EOF) return IO;
    Buffer line = {0};
    Status st;
    while ((st = read_line(stdin, &line)) != END) {
        if (st == OK && strcmp(line.data, "quit") == 0) break;
        if (st == OK && line.len) {
            if (strncmp(line.data, "save ", 5) == 0 || strncmp(line.data, "load ", 5) == 0) {
                st = command(root, line.data, source);
                if (st == IO) { report(st); continue; }
            } else st = command(root, line.data, source);
        }
        if (st == MEMORY || st == IO) break;
        report(st);
    }
    free(line.data);
    return st == END ? OK : st;
}

int main(int argc, char **argv)
{
    Status st = arg_count(argc, argv, 3, INT_MAX);
    Word *root = NULL;
    FILE *in = NULL;
    if (st == OK) st = open_file(argv[1], "rb", &in);
    if (st == OK) st = read_text(in, argc - 2, argv + 2, &root);
    st = close_file(in, st);
    if (st == OK) st = interact(&root, argv[1]);
    clear_tree(root);
    return result(st);
}
