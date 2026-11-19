#include "common.h"

#include <inttypes.h>
#include <limits.h>

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
