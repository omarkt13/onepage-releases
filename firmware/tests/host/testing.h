/* Tiny test harness for the host tests. */
#ifndef TESTING_H
#define TESTING_H

#include <stdio.h>
#include <string.h>

extern int t_failures;
extern int t_checks;

#define CHECK(cond)                                                                    \
	do {                                                                           \
		t_checks++;                                                            \
		if (!(cond)) {                                                         \
			t_failures++;                                                  \
			fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, \
				#cond);                                                \
		}                                                                      \
	} while (0)

#define CHECK_EQ(a, b)                                                                  \
	do {                                                                            \
		long long _a = (long long)(a), _b = (long long)(b);                     \
		t_checks++;                                                             \
		if (_a != _b) {                                                         \
			t_failures++;                                                   \
			fprintf(stderr, "%s:%d: %s == %s failed: %lld != %lld\n", __FILE__, \
				__LINE__, #a, #b, _a, _b);                              \
		}                                                                       \
	} while (0)

#define CHECK_MEM(a, alen, b, blen)                                                     \
	do {                                                                            \
		t_checks++;                                                             \
		if ((size_t)(alen) != (size_t)(blen) || memcmp((a), (b), (blen)) != 0) { \
			t_failures++;                                                   \
			fprintf(stderr, "%s:%d: bytes differ: %s (%zu) vs %s (%zu)\n",   \
				__FILE__, __LINE__, #a, (size_t)(alen), #b, (size_t)(blen)); \
			t_dump((const unsigned char *)(a), (size_t)(alen));              \
			t_dump((const unsigned char *)(b), (size_t)(blen));              \
		}                                                                       \
	} while (0)

#define CHECK_STR(a, b) CHECK(strcmp((a), (b)) == 0)

void t_dump(const unsigned char *p, size_t n);

typedef void (*test_fn)(void);
struct test_case {
	const char *name;
	test_fn fn;
};

int t_run(const struct test_case *cases, int n);

#endif
