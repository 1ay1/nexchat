/* nxmenu.h: the same calls must behave the same on GTK3 (GtkMenu) and GTK4
 * (GMenu + actions): callbacks and their order (GTK3's radio order: the one
 * going off, then the one going on), quiet state changes, insertion order,
 * removal, sensitivity, labels and per-item data. On GTK4 the generated
 * model is also checked (sections for separators, one action per radio
 * group, disabled actions for insensitive items).
 *
 * Builds against either GTK (see meson.build / CI).
 */
#include <string.h>
#include <stdio.h>
#include "nxmenu.h"

/* nxmenu.c only needs these two from gtkutil.c */
const char *
gtkutil_stock_icon_name (const char *stock_id)
{
	return stock_id;
}

#if !GTK_CHECK_VERSION(4, 0, 0)
GtkWidget *
gtkutil_image_new_from_stock (const char *stock_id, GtkIconSize size)
{
	return gtk_image_new_from_icon_name (stock_id, size);
}

GtkWidget *
gtkutil_menu_item_new_with_image (const char *label, gboolean is_markup, GtkWidget *image)
{
	GtkWidget *item = gtk_menu_item_new_with_mnemonic (label);
	g_object_ref_sink (image);
	g_object_unref (image);
	return item;
}

GtkWidget *
gtkutil_menu_item_get_label (GtkWidget *item)
{
	GtkWidget *c = gtk_bin_get_child (GTK_BIN (item));
	return GTK_IS_LABEL (c) ? c : NULL;
}
#endif

static int fails;
static GString *trace;

#define CHECK(c, ...) do { if (!(c)) { printf ("FAIL: " __VA_ARGS__); printf ("\n"); fails++; } } while (0)

static void
cb (nx_item *it, gpointer ud)
{
	g_string_append_printf (trace, "%s%s;", (char *) ud,
									nx_item_get_active (it) ? "+" : "");
}

static void
check_trace (const char *want, const char *what)
{
	CHECK (!strcmp (trace->str, want), "%s: got \"%s\", want \"%s\"", what, trace->str, want);
	g_string_truncate (trace, 0);
}

#if GTK_CHECK_VERSION(4, 0, 0)
/* the model as text: sections and items with action (+target) */
static void
dump_model (GMenuModel *m, GString *out, int depth)
{
	int i, n = g_menu_model_get_n_items (m);

	for (i = 0; i < n; i++)
	{
		GMenuModel *sec = g_menu_model_get_item_link (m, i, G_MENU_LINK_SECTION);
		GMenuModel *sub = g_menu_model_get_item_link (m, i, G_MENU_LINK_SUBMENU);
		char *label = NULL, *action = NULL;
		GVariant *target;

		if (sec)
		{
			g_string_append_printf (out, "%*s[\n", depth * 2, "");
			dump_model (sec, out, depth + 1);
			g_string_append_printf (out, "%*s]\n", depth * 2, "");
			g_object_unref (sec);
			continue;
		}
		g_menu_model_get_item_attribute (m, i, G_MENU_ATTRIBUTE_LABEL, "s", &label);
		g_menu_model_get_item_attribute (m, i, G_MENU_ATTRIBUTE_ACTION, "s", &action);
		target = g_menu_model_get_item_attribute_value (m, i, G_MENU_ATTRIBUTE_TARGET, NULL);
		g_string_append_printf (out, "%*s%s", depth * 2, "", label ? label : "?");
		if (action)
			g_string_append_printf (out, " -> %s", action);
		if (target)
		{
			char *t = g_variant_print (target, FALSE);
			g_string_append_printf (out, "(%s)", t);
			g_free (t);
			g_variant_unref (target);
		}
		g_string_append_c (out, '\n');
		if (sub)
		{
			dump_model (sub, out, depth + 1);
			g_object_unref (sub);
		}
		g_free (label);
		g_free (action);
	}
}
#endif

int
main (int argc, char **argv)
{
	nx_menu *m, *sub;
	nx_item *plain, *chk, *r1, *r2, *r3, *sep, *subitem, *first, *x;

#if GTK_CHECK_VERSION(4, 0, 0)
	gtk_init ();
#else
	gtk_init (&argc, &argv);
#endif
	trace = g_string_new (NULL);

	m = nx_menu_new ();
#if !GTK_CHECK_VERSION(4, 0, 0)
	g_object_ref_sink (m);
#endif
	plain = nx_menu_add_item (m, NX_MENU_APPEND, "_Plain", NX_ITEM_MNEMONIC, NULL, cb, "plain");
	chk = nx_menu_add_check (m, NX_MENU_APPEND, "Check", 0, FALSE, cb, "chk");
	sep = nx_menu_add_separator (m, NX_MENU_APPEND);
	r1 = nx_menu_add_radio (m, NX_MENU_APPEND, "R1", 0, NULL, TRUE, cb, "r1");
	r2 = nx_menu_add_radio (m, NX_MENU_APPEND, "R2", 0, r1, FALSE, cb, "r2");
	r3 = nx_menu_add_radio (m, NX_MENU_APPEND, "R_3", NX_ITEM_MNEMONIC, r1, FALSE, cb, "r3");
	sub = nx_menu_add_submenu (m, NX_MENU_APPEND, "_Sub", NX_ITEM_MNEMONIC, &subitem);
	nx_menu_add_item (sub, NX_MENU_APPEND, "a_b", 0, NULL, cb, "inner");
	first = nx_menu_add_item (m, 0, "First", NX_ITEM_INSENSITIVE, NULL, cb, "first");

	/* order, labels, kinds */
	CHECK (nx_menu_get_n_items (m) == 8, "8 entries, got %d", nx_menu_get_n_items (m));
	CHECK (nx_menu_get_item (m, 0) == first, "insert at 0");
	CHECK (nx_menu_get_item (m, 1) == plain, "plain second");
	CHECK (nx_menu_get_item (m, 3) == sep && nx_item_is_separator (sep), "separator at 3");
	CHECK (nx_item_get_label (sep) == NULL, "separator has no label");
	CHECK (!strcmp (nx_item_get_label (plain), "_Plain"), "mnemonic label kept: %s", nx_item_get_label (plain));
	CHECK (!strcmp (nx_item_get_label (r3), "R_3"), "radio label: %s", nx_item_get_label (r3));
	CHECK (nx_item_get_submenu (subitem) == sub, "submenu link");
	CHECK (nx_item_get_submenu (plain) == NULL, "no submenu");

	/* displayed text: mnemonic underscore and markup gone */
	{
		nx_item *mk = nx_menu_add_item (sub, NX_MENU_APPEND, "<b>Bo_ld</b> x", NX_ITEM_MARKUP | NX_ITEM_MNEMONIC, NULL, NULL, NULL);
		nx_item *mk2 = nx_menu_add_item (sub, NX_MENU_APPEND, "<i>a_b</i>", NX_ITEM_MARKUP, NULL, NULL, NULL);
		nx_item *dbl = nx_menu_add_item (sub, NX_MENU_APPEND, "a__b_c", NX_ITEM_MNEMONIC, NULL, NULL, NULL);

		CHECK (!strcmp (nx_item_get_text (plain), "Plain"), "text plain: %s", nx_item_get_text (plain));
		CHECK (!strcmp (nx_item_get_text (chk), "Check"), "text check: %s", nx_item_get_text (chk));
		CHECK (!strcmp (nx_item_get_text (nx_menu_get_item (sub, 0)), "a_b"), "text non-mnemonic keeps _: %s",
				 nx_item_get_text (nx_menu_get_item (sub, 0)));
		CHECK (!strcmp (nx_item_get_text (mk), "Bold x"), "text markup+mnemonic: %s", nx_item_get_text (mk));
		CHECK (!strcmp (nx_item_get_text (mk2), "a_b"), "text markup: %s", nx_item_get_text (mk2));
		CHECK (!strcmp (nx_item_get_text (dbl), "a_bc"), "text __: %s", nx_item_get_text (dbl));
		CHECK (nx_item_get_text (sep) == NULL, "separator has no text");
		nx_item_remove (mk);
		nx_item_remove (mk2);
		nx_item_remove (dbl);
	}

	/* plain + check */
	nx_item_activate (plain);
	check_trace ("plain;", "plain activate");
	nx_item_activate (chk);
	check_trace ("chk+;", "check on");
	nx_item_activate (chk);
	check_trace ("chk;", "check off");

	/* insensitive items don't run */
	CHECK (!nx_item_get_sensitive (first), "first insensitive");
	nx_item_activate (first);
	check_trace ("", "insensitive activate");
	nx_item_set_sensitive (first, TRUE);
	nx_item_activate (first);
	check_trace ("first;", "made sensitive");

	/* radios: initial state, GTK3's off-then-on order and re-choose */
	CHECK (nx_item_get_active (r1) && !nx_item_get_active (r2) && !nx_item_get_active (r3),
			 "r1 starts active");
	nx_item_activate (r2);
	check_trace ("r1;r2+;", "r1 -> r2");
	CHECK (nx_item_get_active (r2) && !nx_item_get_active (r1), "r2 active");
	nx_item_activate (r2);
	check_trace ("r2+;", "r2 again (GTK3 runs it, stays on)");

	/* quiet: state changes, no callbacks; radio group follows */
	nx_item_set_active_quiet (r3, TRUE);
	check_trace ("", "quiet radio");
	CHECK (nx_item_get_active (r3) && !nx_item_get_active (r2), "r3 active after quiet");
	nx_item_set_active_quiet (chk, TRUE);
	check_trace ("", "quiet check");
	CHECK (nx_item_get_active (chk), "check on after quiet");
	nx_item_activate (r1);
	check_trace ("r3;r1+;", "r3 -> r1 after quiet");

	/* data */
	nx_item_set_data (plain, "u", "nick");
	CHECK (nx_item_get_data (plain, "u") && !strcmp (nx_item_get_data (plain, "u"), "nick"), "item data");

	/* submenu items */
	nx_item_activate (nx_menu_get_item (sub, 0));
	check_trace ("inner;", "submenu item");

	/* remove */
	nx_item_remove (r2);
	CHECK (nx_menu_get_n_items (m) == 7, "7 after remove");
	nx_item_activate (r3);
	check_trace ("r1;r3+;", "radio after removing a member");

	/* insert into the middle keeps the rest */
	x = nx_menu_add_item (m, 2, "X", 0, NULL, cb, "x");
	CHECK (nx_menu_get_item (m, 2) == x && nx_menu_get_item (m, 3) == chk, "insert at 2");

#if GTK_CHECK_VERSION(4, 0, 0)
	{
		GString *d = g_string_new (NULL);
		const char *want =
			"[\n"
			"  First -> nxp.i4\n"
			"  _Plain -> nxp.i0\n"
			"  X -> nxp.i8\n"
			"  Check -> nxp.i1\n"
			"]\n"
			"[\n"
			"  R1 -> nxp.i2('0')\n"
			"  R_3 -> nxp.i2('2')\n"
			"  _Sub\n"
			"    [\n"
			"      a__b -> nxp.i3\n"
			"    ]\n"
			"]\n";

		dump_model (nx_menu_get_model (m), d, 0);
		CHECK (!strcmp (d->str, want), "model:\n%s\nwant:\n%s", d->str, want);
		g_string_free (d, TRUE);

		/* insensitive -> disabled action */
		nx_item_set_sensitive (x, FALSE);
		CHECK (!g_action_group_get_action_enabled (nx_menu_get_actions (m), "i8"), "x disabled");
		/* check state is the action state */
		{
			GVariant *s = g_action_group_get_action_state (nx_menu_get_actions (m), "i1");
			CHECK (g_variant_get_boolean (s), "check action state on");
			g_variant_unref (s);
		}
	}
	nx_menu_free (m);
#else
	g_object_unref (m);
#endif

	if (fails)
		return 1;
	printf ("nxmenu: ok\n");
	return 0;
}
