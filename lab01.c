#include <inttypes.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

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

static Status signed_number(const char *s, intmax_t *out)
{
    if (!s || !out) return INVALID;
    bool negative = *s == '-';
    if (*s == '+' || negative) ++s;
    uintmax_t limit = (uintmax_t)INTMAX_MAX + (negative ? 1u : 0u);
    uintmax_t n;
    Status st = unsigned_number(s, limit, &n);
    if (st != OK) return st;
    if (negative && n != 0) {
        /* Сначала уменьшаем модуль, чтобы он поместился в знаковый тип. */
        *out = -(intmax_t)(n - 1) - 1;
    } else {
        *out = (intmax_t)n;
    }
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
/* XOR складывает биты без переноса. AND находит переносы. */
static uintmax_t bit_add(uintmax_t a, uintmax_t b)
{
    while (b) {
        uintmax_t carry = (a & b) << 1;
        a ^= b;
        b = carry;
    }
    return a;
}

enum { TEXT_CAP = sizeof(uintmax_t) * CHAR_BIT + 2 };

static Status convert(intmax_t number, unsigned r, char *out, size_t capacity)
{
    if (!out || capacity < TEXT_CAP || r < 1 || r > 5) return INVALID;
    const char alphabet[] = "0123456789ABCDEFGHIJKLMNOPQRSTUV";
    /* Младшие r бит маски равны единице. Они дают одну цифру. */
    uintmax_t mask = bit_add((uintmax_t)1 << r, UINTMAX_MAX);
    uintmax_t magnitude = (uintmax_t)number;
    if (number < 0) magnitude = bit_add(~magnitude, 1);
    char reverse[TEXT_CAP];
    size_t length = 0;
    do {
        reverse[length] = alphabet[magnitude & mask];
        length = (size_t)bit_add(length, 1);
        magnitude >>= r;
    } while (magnitude);
    if (number < 0) {
        reverse[length] = '-';
        length = (size_t)bit_add(length, 1);
    }
    size_t position = 0;
    while (length) {
        length = (size_t)bit_add(length, UINTMAX_MAX);
        out[position] = reverse[length];
        position = (size_t)bit_add(position, 1);
    }
    out[position] = '\0';
    return OK;
}

int main(int argc, char **argv)
{
    if (arg_count(argc, argv, 3, 3) != OK) {
        fputs("Usage: lab01 NUMBER R\n", stderr);
        return result(INVALID);
    }
    intmax_t number;
    uintmax_t r;
    char output[TEXT_CAP];
    Status st = signed_number(argv[1], &number);
    if (st == OK) st = unsigned_number(argv[2], 5, &r);
    if (st == OK) st = convert(number, (unsigned)r, output, sizeof output);
    if (st == OK && puts(output) == EOF) st = IO;
    return result(st);
}
