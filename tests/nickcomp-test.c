/* common/nickcomp.c: which person a few letters mean, and in what order. */
#include <stdio.h>
#include <string.h>
#include "nickcomp.h"

static int bad;

#define CHECK(c, ...) do { if (!(c)) { printf ("FAIL: " __VA_ARGS__); printf ("\n"); bad++; } } while (0)

static nickcomp_cand people[] = {
	{ "CrazyCat" }, { "CrazyCat`" }, { "aerth" }, { "LoGiQue" }, { "Nemu64" },
	{ "_kiki_" }, { "[Ex]Bob" }, { "bob" }, { "bobby" }, { "dark_star" }, { "yo", .me = 1 },
};

/* the ranked nicks for word, space separated */
static char *
rank (const char *word, gboolean smart)
{
	GPtrArray *c = g_ptr_array_new ();
	GString *s = g_string_new (NULL);
	guint i;

	for (i = 0; i < G_N_ELEMENTS (people); i++)
		g_ptr_array_add (c, &people[i]);
	nickcomp_rank (c, word, NICKCOMP_LETTERS, smart);
	for (i = 0; i < c->len; i++)
		g_string_append_printf (s, "%s%s", i ? " " : "", ((nickcomp_cand *) c->pdata[i])->nick);
	g_ptr_array_unref (c);
	return g_string_free (s, FALSE);
}

static void
want (const char *word, gboolean smart, const char *expect, const char *why)
{
	char *got = rank (word, smart);
	CHECK (!strcmp (got, expect), "%s: \"%s\" -> \"%s\", want \"%s\"", why, word, got, expect);
	g_free (got);
}

static void
first (const char *word, const char *expect, const char *why)
{
	char *got = rank (word, TRUE), *sp = strchr (got, ' ');
	if (sp)
		*sp = 0;
	CHECK (!strcmp (got, expect), "%s: \"%s\" -> first \"%s\", want \"%s\"", why, word, got, expect);
	g_free (got);
}

int
main (void)
{
	time_t now = 1000000;
	char *a;

	/* how the letters fit */
	CHECK (nickcomp_match ("bob", "BOB") == NICKCOMP_EXACT, "exact, any case");
	CHECK (nickcomp_match ("bobby", "bo") == NICKCOMP_PREFIX, "prefix");
	CHECK (nickcomp_match ("[Ex]Bob", "exb") == NICKCOMP_LOOSE, "punctuation skipped");
	CHECK (nickcomp_match ("_kiki_", "kik") == NICKCOMP_LOOSE, "leading _ skipped");
	CHECK (nickcomp_match ("CrazyCat", "cat") == NICKCOMP_WORD, "camelCase word");
	CHECK (nickcomp_match ("Nemu64", "64") == NICKCOMP_WORD, "digits are a word");
	CHECK (nickcomp_match ("dark_star", "star") == NICKCOMP_WORD, "_ splits words");
	CHECK (nickcomp_match ("CrazyCat", "cc") == NICKCOMP_LETTERS, "initials");
	CHECK (nickcomp_match ("LoGiQue", "lgq") == NICKCOMP_LETTERS, "letters in order");
	CHECK (nickcomp_match ("aerth", "rt") == NICKCOMP_NONE, "not from the middle");
	CHECK (nickcomp_match ("bob", "x") == NICKCOMP_NONE, "no fit");
	CHECK (nickcomp_match ("{foo}", "[FOO]") == NICKCOMP_EXACT, "IRC case: [ is {");

	/* nobody has spoken: by fit, then A-Z; never yourself while others fit */
	want ("bo", TRUE, "bob bobby [Ex]Bob", "prefixes, then a word inside a nick");
	want ("b", TRUE, "bob bobby", "one letter: prefixes only");
	want ("cat", TRUE, "CrazyCat CrazyCat`", "a word inside the nick");
	want ("y", TRUE, "yo", "your own nick when nothing else fits");
	want ("ex", TRUE, "[Ex]Bob", "brackets don't get in the way");

	/* the conversation decides between equally good fits */
	people[8].lasttalk = now - 30;	/* bobby talked just now */
	people[7].lasttalk = now - 600;	/* bob ten minutes ago */
	want ("bo", TRUE, "bobby bob [Ex]Bob", "the one who just talked first");
	want ("bo", FALSE, "bob bobby [Ex]Bob", "A-Z order ignores who talked");
	people[7].lastmention = now - 120;	/* bob said your name 2 min ago */
	want ("bo", TRUE, "bob bobby [Ex]Bob", "who said your name beats who just talked");
	people[7].lastmention = 0;
	people[7].lastaddressed = now - 60;	/* you answered bob a minute ago */
	want ("bo", TRUE, "bob bobby [Ex]Bob", "who you're talking to comes first");
	people[7].lastaddressed = 0;
	/* but a better fit still wins: "bob" typed in full means bob */
	people[8].lastmention = now;
	first ("bob", "bob", "an exact name beats a recent prefix");
	people[8].lastmention = 0;
	/* away people after present ones, when neither has talked */
	people[0].away = 1;
	want ("crazy", TRUE, "CrazyCat` CrazyCat", "away after present");
	people[0].away = 0;
	/* a mid-word match never beats a prefix, however recent */
	people[0].lasttalk = now;
	first ("c", "CrazyCat", "prefix");
	people[5].lasttalk = now;
	first ("k", "_kiki_", "the letters of _kiki_");

	/* who a line of yours speaks to */
	CHECK ((a = nickcomp_addressee ("bob: hi", ":")) && !strcmp (a, "bob"), "bob: hi"); g_free (a);
	CHECK ((a = nickcomp_addressee ("bob, hi", ":")) && !strcmp (a, "bob"), "bob, hi"); g_free (a);
	CHECK ((a = nickcomp_addressee ("@bob hi", ":")) && !strcmp (a, "bob"), "@bob hi"); g_free (a);
	CHECK (!(a = nickcomp_addressee ("hello there", ":")), "plain line"); g_free (a);
	CHECK (!(a = nickcomp_addressee ("@", ":")), "lone @"); g_free (a);

	if (bad)
	{
		printf ("FAIL: %d\n", bad);
		return 1;
	}
	printf ("nickcomp: ok\n");
	return 0;
}
