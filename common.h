#ifndef COMMON_H
#define COMMON_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

typedef enum {
    OK, END, INVALID, RANGE, MEMORY, IO, NOT_FOUND, DUPLICATE, DOMAIN
} Status;

typedef struct { char *data; size_t len, cap; } Buffer;
typedef struct { int year, month, day, hour, minute, second; } Date;

Status reserve(void **result, void *old, size_t *cap, size_t count, size_t unit);
Status append_char(Buffer *b, int c);
Status read_line(FILE *in, Buffer *b);
Status arg_count(int argc, char **argv, int min, int max);
Status split(char *line, char **fields, size_t cap, size_t *count);
Status unsigned_number(const char *s, uintmax_t max, uintmax_t *out);
Status signed_number(const char *s, intmax_t *out);
Status real_number(const char *s, double *out);
Status latin(const char *s, bool empty_allowed);
Status digits(const char *s, size_t n);
Status copy_text(const char *s, char **out);
Status date_parts(int year, int month, int day, Date *out);
Status timestamp(const char *s, char separator, Date *out);
int64_t date_seconds(const Date *date);
int int_order(uintmax_t lhs, uintmax_t rhs);
int real_order(double lhs, double rhs, double eps);
Status open_file(const char *path, const char *mode, FILE **out);
Status close_file(FILE *file, Status status);
Status distinct_files(const char *input, const char *output);
void report(Status status);
int result(Status status);

#endif
