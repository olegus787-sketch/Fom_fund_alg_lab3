#ifndef STRING_TYPE_H
#define STRING_TYPE_H
#include "common.h"

typedef struct { char *data; int length; } String;

/* Destinations are zero-initialized or already own a valid String. */
Status string_set(String *dest, const char *text);
Status string_clear(String *s);
Status string_order(const String *a, const String *b, int *order);
Status string_equal(const String *a, const String *b, bool *equal);
Status string_copy(String *dest, const String *source);
Status string_clone(const String *source, String **out);
Status string_concat(String *dest, const String *suffix);

#endif
