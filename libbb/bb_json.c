#include "bb_json.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

void bb_json_init(bb_json_writer_t *w, char *buf, size_t size) {
    w->buf = buf;
    w->size = size;
    w->pos = 0;
    buf[0] = '\0';
}

void bb_json_start_object(bb_json_writer_t *w) {
    if (w->pos < w->size) w->buf[w->pos++] = '{';
}

void bb_json_end_object(bb_json_writer_t *w) {
    if (w->pos > 1 && w->buf[w->pos - 1] == ',') w->pos--;
    if (w->pos < w->size) w->buf[w->pos++] = '}';
    if (w->pos < w->size) w->buf[w->pos] = '\0';
}

static void add_key(bb_json_writer_t *w, const char *key) {
    int n = snprintf(w->buf + w->pos, w->size - w->pos, "\"%s\":", key);
    if (n > 0) w->pos += n;
}

void bb_json_add_string(bb_json_writer_t *w, const char *key, const char *val) {
    add_key(w, key);
    int n = snprintf(w->buf + w->pos, w->size - w->pos, "\"%s\",", val ? val : "");
    if (n > 0) w->pos += n;
}

void bb_json_add_int(bb_json_writer_t *w, const char *key, int val) {
    add_key(w, key);
    int n = snprintf(w->buf + w->pos, w->size - w->pos, "%d,", val);
    if (n > 0) w->pos += n;
}

void bb_json_add_bool(bb_json_writer_t *w, const char *key, int val) {
    add_key(w, key);
    int n = snprintf(w->buf + w->pos, w->size - w->pos, "%s,", val ? "true" : "false");
    if (n > 0) w->pos += n;
}

void bb_json_add_double(bb_json_writer_t *w, const char *key, double val) {
    add_key(w, key);
    int n = snprintf(w->buf + w->pos, w->size - w->pos, "%.3f,", val);
    if (n > 0) w->pos += n;
}

static const char *find_key(const char *json, const char *key) {
    if (!json || !key) return NULL;
    char search[128];
    snprintf(search, sizeof(search), "\"%s\"", key);
    const char *p = strstr(json, search);
    if (!p) return NULL;
    p += strlen(search);
    while (*p == ' ' || *p == ':') p++;
    return p;
}

const char *bb_json_get_string(const char *json, const char *key) {
    const char *p = find_key(json, key);
    if (!p || *p != '"') return NULL;
    p++;
    static char buf[256];
    int i = 0;
    while (*p && *p != '"' && i < (int)sizeof(buf) - 1) {
        if (*p == '\\' && *(p+1)) {
            p++;
            if (*p == 'n') buf[i++] = '\n';
            else if (*p == 't') buf[i++] = '\t';
            else buf[i++] = *p;
        } else {
            buf[i++] = *p;
        }
        p++;
    }
    buf[i] = '\0';
    return buf;
}

int bb_json_get_int(const char *json, const char *key, int default_val) {
    const char *p = find_key(json, key);
    if (!p) return default_val;
    return atoi(p);
}

int bb_json_get_bool(const char *json, const char *key, int default_val) {
    const char *p = find_key(json, key);
    if (!p) return default_val;
    if (strncmp(p, "true", 4) == 0) return 1;
    if (strncmp(p, "false", 5) == 0) return 0;
    return default_val;
}

double bb_json_get_double(const char *json, const char *key, double default_val) {
    const char *p = find_key(json, key);
    if (!p) return default_val;
    return atof(p);
}
