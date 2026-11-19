#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
    OK, END, INVALID, RANGE, MEMORY, IO, NOT_FOUND, DUPLICATE, DOMAIN
} Status;

/* Ввод, проверка и обработка ошибок. */

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
enum { BYTES = 256, NODES = 511 };
typedef struct { uintmax_t weight; int left, right, byte; } Node;
typedef struct { int ids[BYTES]; size_t count; } Heap;
typedef struct {
    uintmax_t count[BYTES], total, bits, packed;
    char codes[BYTES][BYTES];
} Estimate;

static void heap_push(Heap *heap, const Node *nodes, int id)
{
    size_t i = heap->count++;
    while (i) {
        size_t parent = (i - 1) / 2;
        if (nodes[heap->ids[parent]].weight <= nodes[id].weight) break;
        heap->ids[i] = heap->ids[parent];
        i = parent;
    }
    heap->ids[i] = id;
}

static int heap_pop(Heap *heap, const Node *nodes)
{
    int first = heap->ids[0];
    int last = heap->ids[--heap->count];
    if (!heap->count) return first;
    size_t i = 0;
    while (2 * i + 1 < heap->count) {
        size_t child = 2 * i + 1;
        if (child + 1 < heap->count &&
            nodes[heap->ids[child + 1]].weight < nodes[heap->ids[child]].weight)
            ++child;
        if (nodes[last].weight <= nodes[heap->ids[child]].weight) break;
        heap->ids[i] = heap->ids[child];
        i = child;
    }
    heap->ids[i] = last;
    return first;
}

static Status read_frequencies(FILE *file, Estimate *est)
{
    if (!file || !est) return INVALID;
    unsigned char buffer[8192];
    size_t count;
    while ((count = fread(buffer, 1, sizeof buffer, file)) != 0) {
        if (count > UINTMAX_MAX - est->total) return RANGE;
        est->total += count;
        for (size_t i = 0; i < count; ++i) ++est->count[buffer[i]];
    }
    return ferror(file) ? IO : OK;
}

static Status fill_codes(const Node *nodes, int id, char *path, size_t depth,
                         Estimate *est)
{
    Node node = nodes[id];
    if (node.byte >= 0) {
        if (!depth) path[depth++] = '0';
        memcpy(est->codes[node.byte], path, depth);
        est->codes[node.byte][depth] = '\0';
        if (node.weight > (UINTMAX_MAX - est->bits) / depth) return RANGE;
        est->bits += node.weight * depth;
        return OK;
    }
    path[depth] = '0';
    Status st = fill_codes(nodes, node.left, path, depth + 1, est);
    path[depth] = '1';
    if (st == OK) st = fill_codes(nodes, node.right, path, depth + 1, est);
    return st;
}

static Status estimate(Estimate *est)
{
    if (!est) return INVALID;
    Node nodes[NODES];
    Heap heap = {0};
    int count = 0;
    for (int b = 0; b < BYTES; ++b) {
        if (est->count[b]) {
            nodes[count] = (Node){est->count[b], -1, -1, b};
            heap_push(&heap, nodes, count++);
        }
    }
    /* Два узла с наименьшими частотами становятся детьми нового узла. */
    while (heap.count > 1) {
        int left = heap_pop(&heap, nodes);
        int right = heap_pop(&heap, nodes);
        nodes[count] = (Node){nodes[left].weight + nodes[right].weight, left, right, -1};
        heap_push(&heap, nodes, count++);
    }
    if (!heap.count) return OK;
    char path[BYTES];
    Status st = fill_codes(nodes, heap.ids[0], path, 0, est);
    if (st == OK) est->packed = est->bits / 8 + (est->bits % 8 != 0);
    return st;
}

static Status print_estimate(const Estimate *est)
{
    for (int b = 0; b < BYTES; ++b) {
        if (est->count[b] && printf("%d %" PRIuMAX " %s\n",
             b, est->count[b], est->codes[b]) < 0) return IO;
    }
    long double savings = est->total ? 100 * (1 - (long double)est->packed / est->total) : 0;
    return printf("original_bytes %" PRIuMAX "\nencoded_bits %" PRIuMAX
                  "\nencoded_bytes %" PRIuMAX "\nsavings_percent %.6Lf\n",
                  est->total, est->bits, est->packed, savings) < 0 ? IO : OK;
}

int main(int argc, char **argv)
{
    Status st = arg_count(argc, argv, 2, 2);
    FILE *file = NULL;
    Estimate est = {0};
    if (st == OK) st = open_file(argv[1], "rb", &file);
    if (st == OK) st = read_frequencies(file, &est);
    st = close_file(file, st);
    if (st == OK) st = estimate(&est);
    if (st == OK) st = print_estimate(&est);
    return result(st);
}
