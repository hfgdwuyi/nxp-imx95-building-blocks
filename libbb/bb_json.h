#ifndef BB_JSON_H
#define BB_JSON_H
#include <stddef.h>

// Minimal JSON writer - builds JSON strings into buffer
typedef struct {
    char *buf;
    size_t size;
    size_t pos;
} bb_json_writer_t;

void bb_json_init(bb_json_writer_t *w, char *buf, size_t size);
void bb_json_start_object(bb_json_writer_t *w);
void bb_json_end_object(bb_json_writer_t *w);
void bb_json_add_string(bb_json_writer_t *w, const char *key, const char *val);
void bb_json_add_int(bb_json_writer_t *w, const char *key, int val);
void bb_json_add_bool(bb_json_writer_t *w, const char *key, int val);
void bb_json_add_double(bb_json_writer_t *w, const char *key, double val);

// Minimal JSON value getter
const char *bb_json_get_string(const char *json, const char *key);
int         bb_json_get_int(const char *json, const char *key, int default_val);
int         bb_json_get_bool(const char *json, const char *key, int default_val);
double      bb_json_get_double(const char *json, const char *key, double default_val);

#endif
