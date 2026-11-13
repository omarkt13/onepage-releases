#include "testing.h"

int t_failures;
int t_checks;

void t_dump(const unsigned char *p, size_t n)
{
	fprintf(stderr, "    ");
	for (size_t i = 0; i < n; i++) {
		fprintf(stderr, "%02x", p[i]);
	}
	fprintf(stderr, "\n");
}

int t_run(const struct test_case *cases, int n)
{
	int failed_cases = 0;

	for (int i = 0; i < n; i++) {
		int before = t_failures;

		cases[i].fn();
		int ok = t_failures == before;

		printf("[%s] %s\n", ok ? " OK " : "FAIL", cases[i].name);
		failed_cases += !ok;
	}
	printf("%d cases, %d failed, %d checks, %d failed checks\n", n, failed_cases, t_checks,
	       t_failures);
	return t_failures ? 1 : 0;
}
