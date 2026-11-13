/* Minimal JSON reader for the host tests. Not for production use. */
#ifndef MINI_JSON_H
#define MINI_JSON_H

#include <stddef.h>
#include <stdint.h>

enum mj_type { MJ_NULL, MJ_BOOL, MJ_NUM, MJ_STR, MJ_ARR, MJ_OBJ };

struct mj {
	enum mj_type type;
	long long num;
	int boolean;
	char *str;        /* MJ_STR, UTF-8, terminated */
	size_t str_len;
	struct mj **items; /* MJ_ARR, MJ_OBJ values */
	char **keys;       /* MJ_OBJ */
	size_t n;
};

struct mj *mj_parse(const char *text);
struct mj *mj_load_file(const char *path);
/* Path lookup: "a.b.2.c" (numbers index arrays). NULL if missing. */
const struct mj *mj_get(const struct mj *root, const char *path);
long long mj_num(const struct mj *root, const char *path);
const char *mj_str(const struct mj *root, const char *path);
/* Decodes a hex string at path into a new buffer. */
uint8_t *mj_hex(const struct mj *root, const char *path, size_t *len);

#endif
