#include "string_type.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

static bool valid(const String *s)
{
    return s && s->length >= 0 && (s->data ? strlen(s->data) == (size_t)s->length
                                         : s->length == 0);
}

static const char *text(const String *s)
{
    return s->data ? s->data : "";
}

Status string_set(String *dest, const char *source)
{
    if (!valid(dest) || !source) return INVALID;
    size_t len = strlen(source);
    if (len > INT_MAX) return RANGE;
    char *data;
    Status st = copy_text(source, &data);
    if (st != OK) return st;
    free(dest->data);
    *dest = (String){data, (int)len};
    return OK;
}

Status string_clear(String *s)
{
    if (!s) return INVALID;
    free(s->data);
    *s = (String){0};
    return OK;
}

Status string_order(const String *a, const String *b, int *order)
{
    if (!valid(a) || !valid(b) || !order) return INVALID;
    *order = a->length == b->length ? strcmp(text(a), text(b))
                                  : int_order((unsigned)a->length, (unsigned)b->length);
    return OK;
}

Status string_equal(const String *a, const String *b, bool *equal)
{
    if (!valid(a) || !valid(b) || !equal) return INVALID;
    *equal = strcmp(text(a), text(b)) == 0;
    return OK;
}

Status string_copy(String *dest, const String *source)
{
    if (!valid(source)) return INVALID;
    return string_set(dest, text(source));
}

Status string_clone(const String *source, String **out)
{
    if (!valid(source) || !out) return INVALID;
    String *s = calloc(1, sizeof *s);
    if (!s) return MEMORY;
    Status st = string_copy(s, source);
    if (st == OK) *out = s;
    else free(s);
    return st;
}

Status string_concat(String *dest, const String *suffix)
{
    if (!valid(dest) || !valid(suffix)) return INVALID;
    if (dest->length > INT_MAX - suffix->length) return RANGE;
    int length = dest->length + suffix->length;
    char *data = malloc((size_t)length + 1);
    if (!data) return MEMORY;
    memcpy(data, text(dest), (size_t)dest->length);
    memcpy(data + dest->length, text(suffix), (size_t)suffix->length + 1);
    free(dest->data);
    *dest = (String){data, length};
    return OK;
}
