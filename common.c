#include "common.h"

#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

Status reserve(void **result, void *old, size_t *cap, size_t count, size_t unit)
{
    if (!result || !cap || !unit) return INVALID;
    if (count <= *cap) {
        *result = old;
        return OK;
    }
    if (count > SIZE_MAX / unit) return RANGE;
    size_t next = *cap ? *cap : 4;
    if (next > SIZE_MAX / unit) next = count;
    while (next < count) {
        if (next > SIZE_MAX / unit / 2) {
            next = count;
            break;
        }
        next *= 2;
    }
    void *p = realloc(old, next * unit);
    if (!p) return MEMORY;
    *result = p;
    *cap = next;
    return OK;
}

Status append_char(Buffer *b, int c)
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

Status read_line(FILE *in, Buffer *b)
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

Status arg_count(int argc, char **argv, int min, int max)
{
    if (!argv || argc < min || argc > max) return INVALID;
    for (int i = 1; i < argc; ++i) if (!argv[i] || !*argv[i]) return INVALID;
    return OK;
}

Status split(char *line, char **fields, size_t cap, size_t *count)
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

Status unsigned_number(const char *s, uintmax_t max, uintmax_t *out)
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

Status signed_number(const char *s, intmax_t *out)
{
    if (!s || !out) return INVALID;
    bool negative = *s == '-';
    if (*s == '+' || negative) ++s;
    uintmax_t limit = (uintmax_t)INTMAX_MAX + (negative ? 1u : 0u);
    uintmax_t n;
    Status st = unsigned_number(s, limit, &n);
    if (st != OK) return st;
    *out = negative ? (n ? -(intmax_t)(n - 1) - 1 : 0) : (intmax_t)n;
    return OK;
}

Status real_number(const char *s, double *out)
{
    if (!s || !*s || !out || isspace((unsigned char)*s)) return INVALID;
    char *end;
    double x = strtod(s, &end);
    if (end == s || *end) return INVALID;
    if (!isfinite(x)) return RANGE;
    *out = x;
    return OK;
}

Status latin(const char *s, bool empty_allowed)
{
    if (!s || (!*s && !empty_allowed)) return INVALID;
    for (; *s; ++s) {
        if (!((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z'))) return INVALID;
    }
    return OK;
}

Status digits(const char *s, size_t n)
{
    if (!s || strlen(s) != n) return INVALID;
    for (size_t i = 0; i < n; ++i) if (s[i] < '0' || s[i] > '9') return INVALID;
    return OK;
}

Status copy_text(const char *s, char **out)
{
    if (!s || !out) return INVALID;
    size_t len = strlen(s);
    char *copy = malloc(len + 1);
    if (!copy) return MEMORY;
    memcpy(copy, s, len + 1);
    *out = copy;
    return OK;
}

Status date_parts(int year, int month, int day, Date *out)
{
    const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (!out || year < 1 || year > 9999 || month < 1 || month > 12 || day < 1)
        return INVALID;
    bool leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
    if (day > days[month - 1] + (leap && month == 2)) return INVALID;
    *out = (Date){year, month, day, 0, 0, 0};
    return OK;
}

Status timestamp(const char *s, char separator, Date *out)
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

/* Every accepted Date is in 0001–9999. */
int64_t date_seconds(const Date *date)
{
    const int before_month[] = {0,31,59,90,120,151,181,212,243,273,304,334};
    int y = date->year - 1;
    bool leap = date->year % 4 == 0 && (date->year % 100 != 0 || date->year % 400 == 0);
    int64_t days = y * 365 + y / 4 - y / 100 + y / 400
                 + before_month[date->month - 1] + (leap && date->month > 2) + date->day - 1;
    return ((days * 24 + date->hour) * 60 + date->minute) * 60 + date->second;
}

int int_order(uintmax_t lhs, uintmax_t rhs)
{
    return (lhs > rhs) - (lhs < rhs);
}

int real_order(double lhs, double rhs, double eps)
{
    double delta = lhs - rhs;
    if (fabs(delta) <= eps) return 0;
    return delta < 0 ? -1 : 1;
}

Status open_file(const char *path, const char *mode, FILE **out)
{
    if (!path || !*path || !mode || !out) return INVALID;
    *out = fopen(path, mode);
    return *out ? OK : IO;
}

Status close_file(FILE *file, Status status)
{
    if (file && fclose(file) == EOF && status == OK) return IO;
    return status;
}

Status distinct_files(const char *input, const char *output)
{
    if (!input || !output || strcmp(input, output) == 0) return INVALID;
    struct stat a, b;
    if (stat(input, &a) == 0 && stat(output, &b) == 0 &&
        a.st_dev == b.st_dev && a.st_ino == b.st_ino) return INVALID;
    return OK;
}

void report(Status status)
{
    const char *messages[] = {"ok", "unexpected end of input", "invalid input",
        "value out of range", "out of memory", "I/O error", "not found",
        "duplicate value", "operation outside its domain"};
    if (status != OK && status >= END && status <= DOMAIN)
        fprintf(stderr, "%s\n", messages[status]);
}

int result(Status status)
{
    if (status == OK && fflush(stdout) == EOF) status = IO;
    report(status);
    return status == OK ? EXIT_SUCCESS : EXIT_FAILURE;
}
