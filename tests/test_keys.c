#include "keys.h"
#include "test.h"

static char name[32];

static const char *
dec(const char *s)
{
	size_t n = keys_decode((const unsigned char *)s, strlen(s), name,
	    sizeof name);

	return n == 0 ? "(incomplete)" : name;
}

int
main(void)
{
	/* Plain and control bytes. */
	CHECK_STR(dec("c"), "c");
	CHECK_STR(dec("\""), "\"");
	CHECK_STR(dec(" "), "Space");
	CHECK_STR(dec("\x0e"), "C-n");
	CHECK_STR(dec("\x10"), "C-p");
	CHECK_STR(dec("\x02"), "C-b");
	CHECK_STR(dec("\t"), "Tab");
	CHECK_STR(dec("\r"), "Enter");
	CHECK_STR(dec("\x7f"), "BSpace");
	CHECK_STR(dec("\x1b"), "Escape");
	{
		unsigned char nul = 0;

		keys_decode(&nul, 1, name, sizeof name);
		CHECK_STR(name, "C-Space");
	}

	/* Meta. */
	CHECK_STR(dec("\x1bn"), "M-n");
	CHECK_STR(dec("\x1b\x0f"), "C-M-o");
	CHECK_STR(dec("\x1b["), "M-[");
	CHECK_STR(dec("\x1bO"), "M-O");

	/* Arrows and modifiers, as tmux names them. */
	CHECK_STR(dec("\x1b[A"), "Up");
	CHECK_STR(dec("\x1b[D"), "Left");
	CHECK_STR(dec("\x1bOB"), "Down");
	CHECK_STR(dec("\x1b[1;3D"), "M-Left");
	CHECK_STR(dec("\x1b[1;5A"), "C-Up");
	CHECK_STR(dec("\x1b[1;2C"), "S-Right");
	CHECK_STR(dec("\x1b[1;7B"), "C-M-Down");
	CHECK_STR(dec("\x1b[Z"), "BTab");

	/* Tilde keys. */
	CHECK_STR(dec("\x1b[5~"), "PPage");
	CHECK_STR(dec("\x1b[6~"), "NPage");
	CHECK_STR(dec("\x1b[3~"), "DC");
	CHECK_STR(dec("\x1b[15~"), "F5");
	CHECK_STR(dec("\x1b[5;5~"), "C-PPage");

	/* Extended key formats. */
	CHECK_STR(dec("\x1b[27;5;9~"), "C-Tab");
	CHECK_STR(dec("\x1b[97;5u"), "C-a");
	CHECK_STR(dec("\x1b[59;3u"), "M-;");

	/* UTF-8 and incomplete input. */
	CHECK_STR(dec("é"), "é");
	CHECK_STR(dec("\xc3"), "(incomplete)");
	CHECK_STR(dec("\x1b[1;5"), "(incomplete)");

	/* Only the first key is consumed. */
	CHECK(keys_decode((const unsigned char *)"ab", 2, name, sizeof name) == 1);
	CHECK(keys_decode((const unsigned char *)"\x1b[Ax", 4, name,
	    sizeof name) == 3);

	TEST_DONE();
}
