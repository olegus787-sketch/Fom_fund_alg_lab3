#include "common.h"

#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

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
