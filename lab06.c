#include <ctype.h>
#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
    OK, END, INVALID, RANGE, MEMORY, IO, NOT_FOUND, DUPLICATE, DOMAIN
} Status;

typedef struct {
    char *data;
    size_t len, cap;
} Buffer;

typedef struct {
    int year, month, day, hour, minute, second;
} Date;

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

static Status split(char *line, char **fields, size_t cap, size_t *count)
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

static Status real_number(const char *s, double *out)
{
    if (!s || !*s || !out || isspace((unsigned char)*s)) return INVALID;
    char *end;
    double x = strtod(s, &end);
    if (end == s || *end) return INVALID;
    if (!isfinite(x)) return RANGE;
    *out = x;
    return OK;
}

static Status copy_text(const char *s, char **out)
{
    if (!s || !out) return INVALID;
    size_t len = strlen(s);
    char *copy = malloc(len + 1);
    if (!copy) return MEMORY;
    memcpy(copy, s, len + 1);
    *out = copy;
    return OK;
}

static Status date_parts(int year, int month, int day, Date *out)
{
    const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (!out || year < 1 || year > 9999 || month < 1 || month > 12 || day < 1)
        return INVALID;
    bool leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
    if (day > days[month - 1] + (leap && month == 2)) return INVALID;
    *out = (Date){year, month, day, 0, 0, 0};
    return OK;
}

static Status timestamp(const char *s, char separator, Date *out)
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

static int64_t date_seconds(const Date *date)
{
    const int before_month[] = {0,31,59,90,120,151,181,212,243,273,304,334};
    int y = date->year - 1;
    bool leap = date->year % 4 == 0 && (date->year % 100 != 0 || date->year % 400 == 0);
    int64_t days = y * 365 + y / 4 - y / 100 + y / 400;
    days += before_month[date->month - 1] + date->day - 1;
    if (leap && date->month > 2) ++days;
    return ((days * 24 + date->hour) * 60 + date->minute) * 60 + date->second;
}

static int int_order(uintmax_t lhs, uintmax_t rhs)
{
    if (lhs < rhs) return -1;
    if (lhs > rhs) return 1;
    return 0;
}

static int real_order(double lhs, double rhs, double eps)
{
    double delta = lhs - rhs;
    if (fabs(delta) <= eps) return 0;
    return delta < 0 ? -1 : 1;
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
typedef enum { START, INTERMEDIATE, FINISH } Marker;
typedef struct Stop {
    double x, y;
    int64_t arrival, departure;
    Marker marker;
    struct Stop *next;
} Stop;

typedef struct Vehicle {
    char *id;
    Stop *stops;
    struct Vehicle *next;
} Vehicle;

typedef enum {
    ROUTES_MIN, ROUTES_MAX, PATH_MIN, PATH_MAX, ROUTE_MIN, ROUTE_MAX,
    STOP_MIN, STOP_MAX, IDLE_MAX, METRIC_COUNT
} Metric;

typedef struct {
    uintmax_t routes, shortest_stop, longest_stop, idle;
    double path, shortest_route, longest_route;
} Stats;

typedef struct { const char *id; Stats stats; } Winner;

static void clear_vehicles(Vehicle *vehicle)
{
    while (vehicle) {
        Vehicle *next = vehicle->next;
        Stop *stop = vehicle->stops;
        while (stop) {
            Stop *after = stop->next;
            free(stop);
            stop = after;
        }
        free(vehicle->id);
        free(vehicle);
        vehicle = next;
    }
}

static Status insert_stop(Vehicle **vehicles, const char *id, const Stop *value)
{
    if (!vehicles || !id || !*id || !value) return INVALID;
    Vehicle **slot = vehicles;
    while (*slot && strcmp((*slot)->id, id) < 0) slot = &(*slot)->next;
    bool new_vehicle = !*slot || strcmp((*slot)->id, id) != 0;
    if (new_vehicle) {
        Vehicle *vehicle = calloc(1, sizeof *vehicle);
        if (!vehicle) return MEMORY;
        Status st = copy_text(id, &vehicle->id);
        if (st != OK) { free(vehicle); return st; }
        vehicle->next = *slot;
        *slot = vehicle;
    }
    Stop *stop = malloc(sizeof *stop);
    if (!stop) return MEMORY;
    *stop = *value;
    Stop **link = &(*slot)->stops;
    while (*link &&
           ((*link)->arrival < stop->arrival ||
            ((*link)->arrival == stop->arrival &&
             ((*link)->departure < stop->departure ||
              ((*link)->departure == stop->departure && (*link)->marker > stop->marker)))))
        link = &(*link)->next;
    stop->next = *link;
    *link = stop;
    return OK;
}

static Status parse_stop(char **fields, double x, double y, Stop *stop)
{
    if (!fields || !stop) return INVALID;
    *stop = (Stop){.x = x, .y = y};
    char a[20], d[20];
    if (strlen(fields[1]) != 10 || strlen(fields[2]) != 8 ||
        strlen(fields[3]) != 10 || strlen(fields[4]) != 8) return INVALID;
    snprintf(a, sizeof a, "%s %s", fields[1], fields[2]);
    snprintf(d, sizeof d, "%s %s", fields[3], fields[4]);
    Date arrival, departure;
    Status st = timestamp(a, '.', &arrival);
    if (st == OK) st = timestamp(d, '.', &departure);
    if (st != OK) return st;
    stop->arrival = date_seconds(&arrival);
    stop->departure = date_seconds(&departure);
    if (stop->departure < stop->arrival) return INVALID;
    if (strcmp(fields[5], "S") == 0) stop->marker = START;
    else if (strcmp(fields[5], "I") == 0) stop->marker = INTERMEDIATE;
    else if (strcmp(fields[5], "F") == 0) stop->marker = FINISH;
    else return INVALID;
    return OK;
}

static Status load(FILE *file, Vehicle **vehicles)
{
    if (!file || !vehicles) return INVALID;
    Buffer line = {0};
    char *fields[6];
    size_t n;
    double x = 0, y = 0;
    Status st = read_line(file, &line);
    if (st == OK) st = split(line.data, fields, 2, &n);
    if (st == OK && n != 2) st = INVALID;
    if (st == OK) st = real_number(fields[0], &x);
    if (st == OK) st = real_number(fields[1], &y);
    if (st == OK) {
        while ((st = read_line(file, &line)) == OK) {
            st = split(line.data, fields, 6, &n);
            if (st != OK) break;
            if (!n) continue;
            Stop stop;
            st = n == 6 ? parse_stop(fields, x, y, &stop) : INVALID;
            if (st == OK) st = insert_stop(vehicles, fields[0], &stop);
            if (st != OK) break;
        }
        if (st == END) st = OK;
    }
    free(line.data);
    return st;
}

static Status summarize(const Vehicle *vehicle, double eps, Stats *out)
{
    if (!vehicle || !out) return INVALID;
    Stats s = {.shortest_stop = UINTMAX_MAX};
    const Stop *previous = NULL;
    /* Между START и FINISH суммируем расстояния между остановками. */
    bool active = false;
    double route = 0;
    for (const Stop *p = vehicle->stops; p; p = p->next) {
        if (previous && previous->departure > p->arrival) return INVALID;
        uintmax_t duration = (uintmax_t)(p->departure - p->arrival);
        if (duration < s.shortest_stop) s.shortest_stop = duration;
        if (duration > s.longest_stop) s.longest_stop = duration;
        if (duration > UINTMAX_MAX - s.idle) return RANGE;
        s.idle += duration;
        if (p->marker == START) {
            if (active) return INVALID;
            active = true;
            route = 0;
        } else {
            if (!active || !previous) return INVALID;
            route += hypot(p->x - previous->x, p->y - previous->y);
            if (!isfinite(route)) return RANGE;
            if (p->marker == FINISH) {
                active = false;
                if (!s.routes || real_order(route, s.shortest_route, eps) < 0)
                    s.shortest_route = route;
                if (!s.routes || real_order(route, s.longest_route, eps) > 0)
                    s.longest_route = route;
                ++s.routes;
                s.path += route;
                if (!isfinite(s.path)) return RANGE;
            }
        }
        previous = p;
    }
    if (active || !s.routes) return INVALID;
    *out = s;
    return OK;
}

static int compare_metric(const Stats *a, const Stats *b, Metric m, double eps)
{
    switch (m) {
    case ROUTES_MIN: return int_order(b->routes, a->routes);
    case ROUTES_MAX: return int_order(a->routes, b->routes);
    case PATH_MIN: return real_order(b->path, a->path, eps);
    case PATH_MAX: return real_order(a->path, b->path, eps);
    case ROUTE_MIN: return real_order(b->shortest_route, a->shortest_route, eps);
    case ROUTE_MAX: return real_order(a->longest_route, b->longest_route, eps);
    case STOP_MIN: return int_order(b->shortest_stop, a->shortest_stop);
    case STOP_MAX: return int_order(a->longest_stop, b->longest_stop);
    case IDLE_MAX: return int_order(a->idle, b->idle);
    default: return 0;
    }
}

static Status extremes(const Vehicle *vehicles, double eps, Winner *winners)
{
    if (!winners) return INVALID;
    for (const Vehicle *v = vehicles; v; v = v->next) {
        Stats stats;
        Status st = summarize(v, eps, &stats);
        if (st != OK) return st;
        for (int i = 0; i < METRIC_COUNT; ++i) {
            if (!winners[i].id || compare_metric(&stats, &winners[i].stats, (Metric)i, eps) > 0)
                winners[i] = (Winner){v->id, stats};
        }
    }
    return OK;
}

static Status print_winner(const Winner *winner, Metric metric)
{
    if (!winner->id) return puts("No vehicles") == EOF ? IO : OK;
    const Stats *s = &winner->stats;
    uintmax_t value;
    switch (metric) {
    case PATH_MIN: case PATH_MAX: case ROUTE_MIN: case ROUTE_MAX: {
        double length = metric == ROUTE_MIN ? s->shortest_route :
                        metric == ROUTE_MAX ? s->longest_route : s->path;
        return printf("%s %.10g\n", winner->id, length) < 0 ? IO : OK;
    }
    case ROUTES_MIN: case ROUTES_MAX: value = s->routes; break;
    case STOP_MIN: value = s->shortest_stop; break;
    case STOP_MAX: value = s->longest_stop; break;
    case IDLE_MAX: value = s->idle; break;
    default: return INVALID;
    }
    return printf("%s %" PRIuMAX "\n", winner->id, value) < 0 ? IO : OK;
}

static Status interact(const Winner *winners)
{
    const char *commands[] = {"routes-min", "routes-max", "path-min", "path-max",
        "route-min", "route-max", "stop-min", "stop-max", "idle-max"};
    if (puts("Commands: routes-min routes-max path-min path-max route-min route-max "
             "stop-min stop-max idle-max quit") == EOF) return IO;
    Buffer line = {0};
    Status st;
    while ((st = read_line(stdin, &line)) != END) {
        if (st == OK && strcmp(line.data, "quit") == 0) break;
        if (st == OK && line.len) {
            int i = 0;
            while (i < METRIC_COUNT && strcmp(commands[i], line.data) != 0) ++i;
            st = i == METRIC_COUNT ? INVALID : print_winner(&winners[i], (Metric)i);
        }
        if (st == MEMORY || st == IO) break;
        report(st);
    }
    free(line.data);
    return st == END ? OK : st;
}

int main(int argc, char **argv)
{
    Status st = arg_count(argc, argv, 2, INT_MAX);
    Vehicle *vehicles = NULL;
    for (int i = 1; st == OK && i < argc; ++i) {
        FILE *file = NULL;
        st = open_file(argv[i], "r", &file);
        if (st == OK) st = load(file, &vehicles);
        st = close_file(file, st);
    }
    Winner winners[METRIC_COUNT] = {0};
    if (st == OK) st = extremes(vehicles, 1e-9, winners);
    if (st == OK) st = interact(winners);
    clear_vehicles(vehicles);
    return result(st);
}
