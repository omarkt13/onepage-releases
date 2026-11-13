#include "mini_json.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct ps {
	const char *s;
};

static void ws(struct ps *p)
{
	while (*p->s && isspace((unsigned char)*p->s)) {
		p->s++;
	}
}

static struct mj *node(enum mj_type t)
{
	struct mj *m = calloc(1, sizeof(*m));

	m->type = t;
	return m;
}

static void put_utf8(char **o, unsigned cp)
{
	char *d = *o;

	if (cp < 0x80) {
		*d++ = (char)cp;
	} else if (cp < 0x800) {
		*d++ = (char)(0xC0 | (cp >> 6));
		*d++ = (char)(0x80 | (cp & 0x3F));
	} else if (cp < 0x10000) {
		*d++ = (char)(0xE0 | (cp >> 12));
		*d++ = (char)(0x80 | ((cp >> 6) & 0x3F));
		*d++ = (char)(0x80 | (cp & 0x3F));
	} else {
		*d++ = (char)(0xF0 | (cp >> 18));
		*d++ = (char)(0x80 | ((cp >> 12) & 0x3F));
		*d++ = (char)(0x80 | ((cp >> 6) & 0x3F));
		*d++ = (char)(0x80 | (cp & 0x3F));
	}
	*o = d;
}

static char *parse_string(struct ps *p, size_t *len)
{
	const char *s = ++p->s; /* past the quote */
	char *out = malloc(strlen(s) + 1);
	char *o = out;

	while (*s && *s != '"') {
		if (*s == '\\') {
			s++;
			switch (*s) {
			case 'n': *o++ = '\n'; s++; break;
			case 't': *o++ = '\t'; s++; break;
			case 'r': *o++ = '\r'; s++; break;
			case 'b': *o++ = '\b'; s++; break;
			case 'f': *o++ = '\f'; s++; break;
			case 'u': {
				unsigned cp = (unsigned)strtoul((char[]){s[1], s[2], s[3], s[4], 0}, NULL, 16);

				s += 5;
				if (cp >= 0xD800 && cp < 0xDC00 && s[0] == '\\' && s[1] == 'u') {
					unsigned lo = (unsigned)strtoul((char[]){s[2], s[3], s[4], s[5], 0},
									NULL, 16);

					cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
					s += 6;
				}
				put_utf8(&o, cp);
				break;
			}
			default: *o++ = *s++; break;
			}
		} else {
			*o++ = *s++;
		}
	}
	*o = '\0';
	*len = (size_t)(o - out);
	p->s = *s ? s + 1 : s;
	return out;
}

static struct mj *parse_value(struct ps *p);

static struct mj *parse_container(struct ps *p, int obj)
{
	struct mj *m = node(obj ? MJ_OBJ : MJ_ARR);
	char close = obj ? '}' : ']';

	p->s++;
	ws(p);
	if (*p->s == close) {
		p->s++;
		return m;
	}
	for (;;) {
		char *key = NULL;

		ws(p);
		if (obj) {
			size_t kl;

			key = parse_string(p, &kl);
			ws(p);
			if (*p->s == ':') {
				p->s++;
			}
		}
		struct mj *v = parse_value(p);

		m->items = realloc(m->items, (m->n + 1) * sizeof(*m->items));
		m->keys = realloc(m->keys, (m->n + 1) * sizeof(*m->keys));
		m->items[m->n] = v;
		m->keys[m->n] = key;
		m->n++;
		ws(p);
		if (*p->s == ',') {
			p->s++;
			continue;
		}
		if (*p->s == close) {
			p->s++;
		}
		return m;
	}
}

static struct mj *parse_value(struct ps *p)
{
	ws(p);
	switch (*p->s) {
	case '{': return parse_container(p, 1);
	case '[': return parse_container(p, 0);
	case '"': {
		struct mj *m = node(MJ_STR);

		m->str = parse_string(p, &m->str_len);
		return m;
	}
	case 't': p->s += 4; { struct mj *m = node(MJ_BOOL); m->boolean = 1; return m; }
	case 'f': p->s += 5; return node(MJ_BOOL);
	case 'n': p->s += 4; return node(MJ_NULL);
	default: {
		struct mj *m = node(MJ_NUM);
		char *end;

		m->num = strtoll(p->s, &end, 10);
		p->s = end;
		return m;
	}
	}
}

struct mj *mj_parse(const char *text)
{
	struct ps p = {text};

	return parse_value(&p);
}

struct mj *mj_load_file(const char *path)
{
	FILE *f = fopen(path, "rb");

	if (!f) {
		return NULL;
	}
	fseek(f, 0, SEEK_END);
	long n = ftell(f);

	fseek(f, 0, SEEK_SET);
	char *buf = malloc((size_t)n + 1);

	if (fread(buf, 1, (size_t)n, f) != (size_t)n) {
		fclose(f);
		free(buf);
		return NULL;
	}
	buf[n] = '\0';
	fclose(f);
	struct mj *m = mj_parse(buf);

	free(buf);
	return m;
}

const struct mj *mj_get(const struct mj *m, const char *path)
{
	char part[64];

	while (m && *path) {
		size_t k = strcspn(path, ".");

		if (k >= sizeof(part)) {
			return NULL;
		}
		memcpy(part, path, k);
		part[k] = '\0';
		path += k + (path[k] == '.');
		if (m->type == MJ_ARR) {
			size_t i = (size_t)atoi(part);

			m = i < m->n ? m->items[i] : NULL;
		} else if (m->type == MJ_OBJ) {
			const struct mj *found = NULL;

			for (size_t i = 0; i < m->n; i++) {
				if (strcmp(m->keys[i], part) == 0) {
					found = m->items[i];
					break;
				}
			}
			m = found;
		} else {
			return NULL;
		}
	}
	return m;
}

long long mj_num(const struct mj *root, const char *path)
{
	const struct mj *m = mj_get(root, path);

	if (!m) {
		fprintf(stderr, "missing number %s\n", path);
		exit(2);
	}
	return m->type == MJ_BOOL ? m->boolean : m->num;
}

const char *mj_str(const struct mj *root, const char *path)
{
	const struct mj *m = mj_get(root, path);

	if (!m || m->type != MJ_STR) {
		fprintf(stderr, "missing string %s\n", path);
		exit(2);
	}
	return m->str;
}

uint8_t *mj_hex(const struct mj *root, const char *path, size_t *len)
{
	const char *s = mj_str(root, path);
	size_t n = strlen(s) / 2;
	uint8_t *b = malloc(n ? n : 1);

	for (size_t i = 0; i < n; i++) {
		unsigned v;

		sscanf(s + 2 * i, "%2x", &v);
		b[i] = (uint8_t)v;
	}
	*len = n;
	return b;
}
