/* Minimal test harness: CHECK/CHECK_STR record failures, TEST_DONE exits. */
#ifndef WK_TEST_H
#define WK_TEST_H

#include <stdio.h>
#include <string.h>

static int t_run, t_failed;

#define CHECK(cond) do {                                                     \
	t_run++;                                                             \
	if (!(cond)) {                                                       \
		t_failed++;                                                  \
		fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__,       \
		    __LINE__, #cond);                                        \
	}                                                                    \
} while (0)

#define CHECK_STR(got, want) do {                                            \
	const char *g_ = (got), *w_ = (want);                                \
	t_run++;                                                             \
	if (g_ == NULL || strcmp(g_, w_) != 0) {                             \
		t_failed++;                                                  \
		fprintf(stderr, "%s:%d: got \"%s\", want \"%s\"\n", __FILE__, \
		    __LINE__, g_ ? g_ : "(null)", w_);                       \
	}                                                                    \
} while (0)

#define TEST_DONE() do {                                                     \
	printf("%d checks, %d failed\n", t_run, t_failed);                   \
	return t_failed != 0;                                                \
} while (0)

#endif
