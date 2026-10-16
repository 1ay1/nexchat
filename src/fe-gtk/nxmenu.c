/* nexchat
 * Copyright (C) 2026 nexchat contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <string.h>
#ifndef WIN32
#include <unistd.h>
#else
#include <io.h>
#define access _access
#define R_OK 4
#endif

#include "nxmenu.h"
#include "gtkcompat.h"

/* from gtkutil.c (not its header, which drags in the whole client) */
const char *gtkutil_stock_icon_name (const char *stock_id);
#if !GTK_CHECK_VERSION(4, 0, 0)
GtkWidget *gtkutil_image_new_from_stock (const char *stock_id, GtkIconSize size);
GtkWidget *gtkutil_menu_item_new_with_image (const char *label, gboolean is_markup, GtkWidget *image);
GtkWidget *gtkutil_menu_item_get_label (GtkWidget *item);
#endif

/* an image file we can read, rather than a stock / themed icon name */
static gboolean
icon_is_file (const char *icon)
{
	return icon && access (icon, R_OK) == 0;
}

#if GTK_CHECK_VERSION(4, 0, 0)

/* ================= GTK4: GMenu + actions ================= */

typedef enum { E_ITEM, E_CHECK, E_RADIO, E_SEPARATOR, E_SUBMENU } entry_kind;

typedef struct
{
	int refs;
	GSimpleAction *action;	/* string state: the active member's target */
	GList *members;			/* nx_item *, in creation order */
	int next;					/* for member targets */
} radio_group;

struct _nx_item
{
	entry_kind kind;
	nx_menu *menu;			/* the menu it's in */
	char *label;			/* as given */
	int flags;
	char *icon;
	nx_item_cb cb;
	gpointer userdata;
	gboolean sensitive;
	gboolean active;		/* check / radio */
	radio_group *group;	/* radio */
	char *target;			/* radio: this item's value in the group's action */
	nx_menu *submenu;
	GSimpleAction *action;	/* item / check: its own; radio: the group's (ref) */
	GData *data;
};

struct _nx_menu
{
	GList *entries;			/* nx_item * */
	nx_item *parent;			/* the submenu entry we hang off, NULL at the top */
	GMenu *model;
	GSimpleActionGroup *actions;	/* used at the root only */
	GtkWidget *popover;
	GDestroyNotify done;
	gpointer done_data;
	int next_id;
	gboolean frozen;			/* model rebuild pending (root only) */
};

static nx_menu *
menu_root (nx_menu *m)
{
	while (m->parent)
		m = m->parent->menu;
	return m;
}

nx_menu *
nx_menu_new (void)
{
	nx_menu *m = g_new0 (nx_menu, 1);
	m->model = g_menu_new ();
	m->actions = g_simple_action_group_new ();
	return m;
}

static void
item_free (nx_item *it)
{
	nx_menu *root = menu_root (it->menu);

	if (it->submenu)
	{
		it->submenu->parent = NULL;
		nx_menu_free (it->submenu);
	}
	if (it->group)
	{
		it->group->members = g_list_remove (it->group->members, it);
		if (--it->group->refs == 0)
		{
			g_action_map_remove_action (G_ACTION_MAP (root->actions),
												 g_action_get_name (G_ACTION (it->group->action)));
			g_object_unref (it->group->action);
			g_free (it->group);
		}
	}
	else if (it->action)
		g_action_map_remove_action (G_ACTION_MAP (root->actions),
											 g_action_get_name (G_ACTION (it->action)));
	if (it->action)
		g_object_unref (it->action);
	g_datalist_clear (&it->data);
	g_free (it->label);
	g_free (it->icon);
	g_free (it->target);
	g_free (it);
}

void
nx_menu_free (nx_menu *m)
{
	GList *l;

	/* submenus' items keep actions in the root map: free them while the
	   root (m, if it is one) still exists */
	for (l = m->entries; l; l = l->next)
		item_free (l->data);
	g_list_free (m->entries);
	g_object_unref (m->model);
	g_object_unref (m->actions);
	if (m->popover)
		gtk_widget_unparent (m->popover);
	g_free (m);
}

static void
run_cb (nx_item *it)
{
	if (it->cb)
		it->cb (it, it->userdata);
}

static void
on_activate (GSimpleAction *a, GVariant *param, nx_item *it)
{
	if (it->kind == E_CHECK)
	{
		it->active = !it->active;
		g_simple_action_set_state (a, g_variant_new_boolean (it->active));
		run_cb (it);
	}
	else if (it->kind == E_RADIO)
	{
		nx_item *on = NULL, *off = NULL;
		const char *t = g_variant_get_string (param, NULL);
		GList *l;

		for (l = it->group->members; l; l = l->next)
		{
			nx_item *o = l->data;
			if (!strcmp (o->target, t))
				on = o;
			else if (o->active)
				off = o;
		}
		/* GTK3: choosing the active radio again keeps it on and runs its
		   handler once more */
		if (!on)
			return;
		if (on->active)
		{
			run_cb (on);
			return;
		}
		if (off)
			off->active = FALSE;
		on->active = TRUE;
		g_simple_action_set_state (a, g_variant_new_string (t));
		/* GTK3's order: the one going off, then the one going on */
		if (off)
			run_cb (off);
		run_cb (on);
	}
	else
		run_cb (it);
}

/* actions live in the root menu's group as nx.i<id> */
static GSimpleAction *
action_new (nx_menu *m, const GVariantType *param, GVariant *state, nx_item *it)
{
	nx_menu *root = menu_root (m);
	char name[32];
	GSimpleAction *a;

	g_snprintf (name, sizeof name, "i%d", root->next_id++);
	a = state ? g_simple_action_new_stateful (name, param, state)
				 : g_simple_action_new (name, param);
	g_signal_connect (a, "activate", G_CALLBACK (on_activate), it);
	g_action_map_add_action (G_ACTION_MAP (root->actions), G_ACTION (a));
	return a;
}

/* GMenu labels are always mnemonic; escape underscores of plain labels.
   Markup labels have their tags stripped (GtkPopoverMenu reads the
   use-markup attribute, which keeps them). */
static char *
model_label (nx_item *it)
{
	GString *s;
	const char *p;

	if (it->flags & NX_ITEM_MNEMONIC || it->icon)
		return g_strdup (it->label);
	s = g_string_new (NULL);
	for (p = it->label; *p; p++)
	{
		if (*p == '_')
			g_string_append_c (s, '_');
		g_string_append_c (s, *p);
	}
	return g_string_free (s, FALSE);
}

static GMenuItem *
model_item (nx_item *it)
{
	char *label = model_label (it);
	char name[48];
	GMenuItem *mi;

	if (it->kind == E_SUBMENU)
		mi = g_menu_item_new_submenu (label, G_MENU_MODEL (it->submenu->model));
	else
	{
		g_snprintf (name, sizeof name, "nx.%s", g_action_get_name (G_ACTION (it->action)));
		mi = g_menu_item_new (label, NULL);
		if (it->kind == E_RADIO)
			g_menu_item_set_action_and_target_value (mi, name, g_variant_new_string (it->target));
		else
			g_menu_item_set_action_and_target_value (mi, name, NULL);
	}
	if (it->flags & NX_ITEM_MARKUP)
		g_menu_item_set_attribute (mi, "use-markup", "b", TRUE);
	if (it->icon)
	{
		GIcon *icon;
		if (icon_is_file (it->icon))
		{
			GFile *f = g_file_new_for_path (it->icon);
			icon = g_file_icon_new (f);
			g_object_unref (f);
		}
		else
			icon = g_themed_icon_new (gtkutil_stock_icon_name (it->icon));
		g_menu_item_set_icon (mi, icon);
		g_object_unref (icon);
	}
	g_free (label);
	return mi;
}

/* separators split the menu into sections, which GtkPopoverMenu draws with
   a line between them. Unlike GTK3, an empty section draws nothing, so a
   leading or doubled separator disappears. */
static void
rebuild_one (nx_menu *m)
{
	GMenu *section = g_menu_new ();
	GList *l;

	g_menu_remove_all (m->model);
	for (l = m->entries; l; l = l->next)
	{
		nx_item *it = l->data;
		GMenuItem *mi;

		if (it->kind == E_SEPARATOR)
		{
			g_menu_append_section (m->model, NULL, G_MENU_MODEL (section));
			g_object_unref (section);
			section = g_menu_new ();
			continue;
		}
		mi = model_item (it);
		g_menu_append_item (section, mi);
		g_object_unref (mi);
	}
	g_menu_append_section (m->model, NULL, G_MENU_MODEL (section));
	g_object_unref (section);
}

static void
changed (nx_menu *m)
{
	rebuild_one (m);
}

static nx_item *
entry_new (nx_menu *m, int pos, entry_kind kind, const char *label, int flags)
{
	nx_item *it = g_new0 (nx_item, 1);
	int n = g_list_length (m->entries);

	it->kind = kind;
	it->menu = m;
	it->label = g_strdup (label ? label : "");
	it->flags = flags;
	it->sensitive = !(flags & NX_ITEM_INSENSITIVE);
	g_datalist_init (&it->data);
	if (pos < 0 || pos > n)
		pos = n;
	m->entries = g_list_insert (m->entries, it, pos);
	return it;
}

nx_item *
nx_menu_add_item (nx_menu *m, int pos, const char *label, int flags,
						const char *icon, nx_item_cb cb, gpointer userdata)
{
	nx_item *it = entry_new (m, pos, E_ITEM, label, flags);

	it->icon = g_strdup (icon);
	it->cb = cb;
	it->userdata = userdata;
	it->action = action_new (m, NULL, NULL, it);
	g_simple_action_set_enabled (it->action, it->sensitive);
	changed (m);
	return it;
}

nx_item *
nx_menu_add_check (nx_menu *m, int pos, const char *label, int flags,
						 gboolean active, nx_item_cb cb, gpointer userdata)
{
	nx_item *it = entry_new (m, pos, E_CHECK, label, flags);

	it->cb = cb;
	it->userdata = userdata;
	it->active = active;
	it->action = action_new (m, NULL, g_variant_new_boolean (active), it);
	g_simple_action_set_enabled (it->action, it->sensitive);
	changed (m);
	return it;
}

nx_item *
nx_menu_add_radio (nx_menu *m, int pos, const char *label, int flags,
						 nx_item *group, gboolean active, nx_item_cb cb, gpointer userdata)
{
	nx_item *it = entry_new (m, pos, E_RADIO, label, flags);
	radio_group *g = group ? group->group : NULL;
	char t[16];

	it->cb = cb;
	it->userdata = userdata;
	if (!g)
	{
		g = g_new0 (radio_group, 1);
		/* a fresh group: its first member is active, as GTK3 made it */
		active = TRUE;
	}
	g->refs++;
	g_snprintf (t, sizeof t, "%d", g->next++);
	it->target = g_strdup (t);
	it->group = g;
	g->members = g_list_append (g->members, it);
	if (!g->action)
		g->action = action_new (m, G_VARIANT_TYPE_STRING, g_variant_new_string (t), it);
	it->action = g_object_ref (g->action);
	/* the group's one action can't know which item: its handler looks the
	   target up in the group, any member will do */
	if (active)
	{
		GList *l;
		for (l = g->members; l; l = l->next)
			((nx_item *) l->data)->active = FALSE;
		it->active = TRUE;
		g_simple_action_set_state (g->action, g_variant_new_string (t));
	}
	changed (m);
	return it;
}

nx_item *
nx_menu_add_separator (nx_menu *m, int pos)
{
	nx_item *it = entry_new (m, pos, E_SEPARATOR, NULL, 0);
	changed (m);
	return it;
}

nx_menu *
nx_menu_add_submenu (nx_menu *m, int pos, const char *label, int flags, nx_item **item_ret)
{
	nx_item *it = entry_new (m, pos, E_SUBMENU, label, flags);

	it->submenu = nx_menu_new ();
	it->submenu->parent = it;
	changed (m);
	if (item_ret)
		*item_ret = it;
	return it->submenu;
}

int
nx_menu_get_n_items (nx_menu *m)
{
	return g_list_length (m->entries);
}

nx_item *
nx_menu_get_item (nx_menu *m, int index)
{
	return g_list_nth_data (m->entries, index);
}

void
nx_menu_remove (nx_menu *m, nx_item *it)
{
	m->entries = g_list_remove (m->entries, it);
	item_free (it);
	changed (m);
}

void
nx_item_set_sensitive (nx_item *it, gboolean sensitive)
{
	it->sensitive = sensitive;
	/* submenus: GtkPopoverMenu has no per-submenu sensitivity; their items
	   stay as they are (GTK3 greyed the entry). Rare: only /MENU -d. */
	if (it->action && it->kind != E_RADIO)
		g_simple_action_set_enabled (it->action, sensitive);
	else if (it->kind == E_RADIO)
	{
		/* one action per group: enabled while any member is */
		GList *l;
		gboolean any = FALSE;
		for (l = it->group->members; l; l = l->next)
			any |= ((nx_item *) l->data)->sensitive;
		g_simple_action_set_enabled (it->action, any);
	}
}

gboolean
nx_item_get_sensitive (nx_item *it)
{
	return it->sensitive;
}

gboolean
nx_item_get_active (nx_item *it)
{
	return it->active;
}

void
nx_item_set_active_quiet (nx_item *it, gboolean active)
{
	if (it->kind == E_CHECK)
	{
		it->active = active;
		g_simple_action_set_state (it->action, g_variant_new_boolean (active));
	}
	else if (it->kind == E_RADIO && active && !it->active)
	{
		GList *l;
		for (l = it->group->members; l; l = l->next)
			((nx_item *) l->data)->active = FALSE;
		it->active = TRUE;
		g_simple_action_set_state (it->action, g_variant_new_string (it->target));
	}
	else if (it->kind == E_RADIO && !active)
		/* GTK3 let a radio go off by itself (the group then had none on) */
		it->active = FALSE;
}

void
nx_item_activate (nx_item *it)
{
	if (it->kind == E_SEPARATOR || it->kind == E_SUBMENU || !it->sensitive)
		return;
	g_action_activate (G_ACTION (it->action),
							 it->kind == E_RADIO ? g_variant_new_string (it->target) : NULL);
}

const char *
nx_item_get_label (nx_item *it)
{
	return it->kind == E_SEPARATOR ? NULL : it->label;
}

nx_menu *
nx_item_get_submenu (nx_item *it)
{
	return it->submenu;
}

gboolean
nx_item_is_separator (nx_item *it)
{
	return it->kind == E_SEPARATOR;
}

GtkWidget *
nx_item_get_widget (nx_item *it)
{
	nx_menu *root = menu_root (it->menu);
	return root->popover ? gtk_widget_get_parent (root->popover) : NULL;
}

void
nx_item_set_data (nx_item *it, const char *key, gpointer data)
{
	g_datalist_set_data (&it->data, key, data);
}

gpointer
nx_item_get_data (nx_item *it, const char *key)
{
	return g_datalist_get_data (&it->data, key);
}

GMenuModel *
nx_menu_get_model (nx_menu *m)
{
	return G_MENU_MODEL (m->model);
}

GActionGroup *
nx_menu_get_actions (nx_menu *m)
{
	return G_ACTION_GROUP (menu_root (m)->actions);
}

static gboolean
popup_free_idle (gpointer data)
{
	nx_menu *m = data;
	GDestroyNotify done = m->done;
	gpointer done_data = m->done_data;

	nx_menu_free (m);
	if (done)
		done (done_data);
	return G_SOURCE_REMOVE;
}

static void
popup_closed (GtkPopover *p, nx_menu *m)
{
	/* the chosen item's action runs after "closed": free afterwards */
	g_idle_add (popup_free_idle, m);
}

void
nx_menu_popup (nx_menu *m, GtkWidget *widget, GdkEvent *event,
					GDestroyNotify done, gpointer done_data)
{
	GtkWidget *p = gtk_popover_menu_new_from_model_full (G_MENU_MODEL (m->model),
																		  GTK_POPOVER_MENU_NESTED);
	double x = 0, y = 0;
	GdkRectangle r;

	m->popover = p;
	m->done = done;
	m->done_data = done_data;
	gtk_widget_insert_action_group (p, "nx", G_ACTION_GROUP (m->actions));
	gtk_widget_set_parent (p, widget);
	gtk_popover_set_has_arrow (GTK_POPOVER (p), FALSE);
	gtk_widget_set_halign (p, GTK_ALIGN_START);
	/* at the pointer: the event's position in widget coordinates */
	if (event && gdk_event_get_position (event, &x, &y))
	{
		GtkNative *n = gtk_widget_get_native (widget);
		graphene_point_t in, out;
		double nx, ny;

		gtk_native_get_surface_transform (n, &nx, &ny);
		in = GRAPHENE_POINT_INIT ((float) (x - nx), (float) (y - ny));
		if (gtk_widget_compute_point (GTK_WIDGET (n), widget, &in, &out))
		{
			x = out.x;
			y = out.y;
		}
	}
	r.x = (int) x;
	r.y = (int) y;
	r.width = r.height = 1;
	gtk_popover_set_pointing_to (GTK_POPOVER (p), &r);
	gtk_popover_set_position (GTK_POPOVER (p), GTK_POS_BOTTOM);
	g_signal_connect (p, "closed", G_CALLBACK (popup_closed), m);
	gtk_popover_popup (GTK_POPOVER (p));
}

#else

/* ================= GTK3: GtkMenu, as nexchat always built it ================= */

static void
item_activate_cb (GtkWidget *w, gpointer unused)
{
	nx_item_cb cb = g_object_get_data (G_OBJECT (w), "nx-cb");

	if (cb)
		cb (NX_ITEM (w), g_object_get_data (G_OBJECT (w), "nx-ud"));
}

static void
connect_cb (GtkWidget *item, nx_item_cb cb, gpointer userdata)
{
	if (!cb)
		return;
	g_object_set_data (G_OBJECT (item), "nx-cb", cb);
	g_object_set_data (G_OBJECT (item), "nx-ud", userdata);
	g_signal_connect (item, "activate", G_CALLBACK (item_activate_cb), NULL);
}

static void
place (nx_menu *menu, GtkWidget *item, int pos)
{
	if (pos < 0)
		gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
	else
		gtk_menu_shell_insert (GTK_MENU_SHELL (menu), item, pos);
}

/* a GtkMenuItem whose label is plain / mnemonic / markup */
static GtkWidget *
label_item (const char *label, int flags)
{
	GtkWidget *item;

	if (flags & NX_ITEM_MARKUP)
	{
		item = gtk_menu_item_new_with_label ("");
		if (flags & NX_ITEM_MNEMONIC)
			gtk_label_set_markup_with_mnemonic (GTK_LABEL (gtk_bin_get_child (GTK_BIN (item))), label);
		else
			gtk_label_set_markup (GTK_LABEL (gtk_bin_get_child (GTK_BIN (item))), label);
	}
	else if (flags & NX_ITEM_MNEMONIC)
		item = gtk_menu_item_new_with_mnemonic (label);
	else
		item = gtk_menu_item_new_with_label (label);
	return item;
}

nx_menu *
nx_menu_new (void)
{
	return NX_MENU (gtk_menu_new ());
}

nx_item *
nx_menu_add_item (nx_menu *menu, int pos, const char *label, int flags,
						const char *icon, nx_item_cb cb, gpointer userdata)
{
	GtkWidget *item;

	if (icon)
	{
		GtkWidget *img;

		if (icon_is_file (icon))
			img = gtk_image_new_from_file (icon);
		else
			img = gtkutil_image_new_from_stock (icon, GTK_ICON_SIZE_MENU);
		item = gtkutil_menu_item_new_with_image (label, (flags & NX_ITEM_MARKUP) != 0, img);
	}
	else
		item = label_item (label, flags);
	place (menu, item, pos);
	connect_cb (item, cb, userdata);
	if (flags & NX_ITEM_INSENSITIVE)
		gtk_widget_set_sensitive (item, FALSE);
	nx_widget_show_all (item);
	return NX_ITEM (item);
}

nx_item *
nx_menu_add_check (nx_menu *menu, int pos, const char *label, int flags,
						 gboolean active, nx_item_cb cb, gpointer userdata)
{
	GtkWidget *item = (flags & NX_ITEM_MNEMONIC) ?
		gtk_check_menu_item_new_with_mnemonic (label) : gtk_check_menu_item_new_with_label (label);

	gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (item), active);
	place (menu, item, pos);
	connect_cb (item, cb, userdata);
	if (flags & NX_ITEM_INSENSITIVE)
		gtk_widget_set_sensitive (item, FALSE);
	gtk_widget_show (item);
	return NX_ITEM (item);
}

nx_item *
nx_menu_add_radio (nx_menu *menu, int pos, const char *label, int flags,
						 nx_item *group, gboolean active, nx_item_cb cb, gpointer userdata)
{
	GSList *g = group ? gtk_radio_menu_item_get_group (GTK_RADIO_MENU_ITEM (group)) : NULL;
	GtkWidget *item = (flags & NX_ITEM_MNEMONIC) ?
		gtk_radio_menu_item_new_with_mnemonic (g, label) : gtk_radio_menu_item_new_with_label (g, label);

	gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (item), active);
	place (menu, item, pos);
	connect_cb (item, cb, userdata);
	if (flags & NX_ITEM_INSENSITIVE)
		gtk_widget_set_sensitive (item, FALSE);
	gtk_widget_show (item);
	return NX_ITEM (item);
}

nx_item *
nx_menu_add_separator (nx_menu *menu, int pos)
{
	GtkWidget *item = gtk_separator_menu_item_new ();

	place (menu, item, pos);
	gtk_widget_show (item);
	return NX_ITEM (item);
}

nx_menu *
nx_menu_add_submenu (nx_menu *menu, int pos, const char *label, int flags, nx_item **item_ret)
{
	GtkWidget *sub = gtk_menu_new ();
	GtkWidget *item = label_item (label, flags);

	place (menu, item, pos);
	gtk_widget_show (item);
	gtk_menu_item_set_submenu (GTK_MENU_ITEM (item), sub);
	if (flags & NX_ITEM_INSENSITIVE)
		gtk_widget_set_sensitive (item, FALSE);
	if (item_ret)
		*item_ret = NX_ITEM (item);
	return NX_MENU (sub);
}

int
nx_menu_get_n_items (nx_menu *menu)
{
	GList *c = gtk_container_get_children (GTK_CONTAINER (menu));
	int n = g_list_length (c);
	g_list_free (c);
	return n;
}

nx_item *
nx_menu_get_item (nx_menu *menu, int index)
{
	GList *c = gtk_container_get_children (GTK_CONTAINER (menu));
	nx_item *it = g_list_nth_data (c, index);
	g_list_free (c);
	return it;
}

void
nx_menu_remove (nx_menu *menu, nx_item *item)
{
	gtk_widget_destroy (NX_ITEM_WIDGET (item));
}

void
nx_item_set_sensitive (nx_item *item, gboolean sensitive)
{
	gtk_widget_set_sensitive (NX_ITEM_WIDGET (item), sensitive);
}

gboolean
nx_item_get_sensitive (nx_item *item)
{
	return gtk_widget_get_sensitive (NX_ITEM_WIDGET (item));
}

gboolean
nx_item_get_active (nx_item *item)
{
	return GTK_IS_CHECK_MENU_ITEM (item) &&
		gtk_check_menu_item_get_active (GTK_CHECK_MENU_ITEM (item));
}

/* GTK2 code assigned GtkCheckMenuItem->active directly, which changed the
   state without emitting "activate"/"toggled". gtk_check_menu_item_set_active
   emits both (and for radio items also toggles the previously active group
   member), so block all connected handlers on the item and its radio group
   while changing the state to keep the old, signal-free semantics. */
void
nx_item_set_active_quiet (nx_item *item, gboolean active)
{
	GSList *group, *l;
	guint sig_activate = g_signal_lookup ("activate", GTK_TYPE_MENU_ITEM);
	guint sig_toggled = g_signal_lookup ("toggled", GTK_TYPE_CHECK_MENU_ITEM);

	if (!GTK_IS_CHECK_MENU_ITEM (item))
		return;
	if (GTK_IS_RADIO_MENU_ITEM (item))
		group = g_slist_copy (gtk_radio_menu_item_get_group (GTK_RADIO_MENU_ITEM (item)));
	else
		group = g_slist_prepend (NULL, item);

	for (l = group; l; l = l->next)
	{
		g_signal_handlers_block_matched (l->data, G_SIGNAL_MATCH_ID, sig_activate, 0, NULL, NULL, NULL);
		g_signal_handlers_block_matched (l->data, G_SIGNAL_MATCH_ID, sig_toggled, 0, NULL, NULL, NULL);
	}

	gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (item), active);

	for (l = group; l; l = l->next)
	{
		g_signal_handlers_unblock_matched (l->data, G_SIGNAL_MATCH_ID, sig_activate, 0, NULL, NULL, NULL);
		g_signal_handlers_unblock_matched (l->data, G_SIGNAL_MATCH_ID, sig_toggled, 0, NULL, NULL, NULL);
	}

	g_slist_free (group);
}

void
nx_item_activate (nx_item *item)
{
	if (gtk_widget_is_sensitive (NX_ITEM_WIDGET (item)))
		gtk_menu_item_activate (GTK_MENU_ITEM (item));
}

const char *
nx_item_get_label (nx_item *item)
{
	GtkWidget *lbl;

	if (GTK_IS_SEPARATOR_MENU_ITEM (item))
		return NULL;
	lbl = gtkutil_menu_item_get_label (NX_ITEM_WIDGET (item));
	return lbl ? gtk_label_get_label (GTK_LABEL (lbl)) : NULL;
}

nx_menu *
nx_item_get_submenu (nx_item *item)
{
	return NX_MENU (gtk_menu_item_get_submenu (GTK_MENU_ITEM (item)));
}

gboolean
nx_item_is_separator (nx_item *item)
{
	return GTK_IS_SEPARATOR_MENU_ITEM (item);
}

GtkWidget *
nx_item_get_widget (nx_item *item)
{
	return NX_ITEM_WIDGET (item);
}

void
nx_item_set_data (nx_item *item, const char *key, gpointer data)
{
	g_object_set_data (G_OBJECT (item), key, data);
}

gpointer
nx_item_get_data (nx_item *item, const char *key)
{
	return g_object_get_data (G_OBJECT (item), key);
}

typedef struct
{
	GDestroyNotify done;
	gpointer done_data;
} popup_done;

static void
popup_selection_done (GtkWidget *menu, popup_done *d)
{
	gtk_widget_destroy (menu);
	g_object_unref (menu);
	if (d->done)
		d->done (d->done_data);
	g_free (d);
}

void
nx_menu_popup (nx_menu *menu, GtkWidget *widget, GdkEvent *event,
					GDestroyNotify done, gpointer done_data)
{
	popup_done *d = g_new (popup_done, 1);

	d->done = done;
	d->done_data = done_data;
	g_object_ref (menu);
	g_object_ref_sink (menu);
	g_object_unref (menu);
	g_signal_connect (menu, "selection-done", G_CALLBACK (popup_selection_done), d);
	nx_menu_popup_at_pointer (NX_MENU_WIDGET (menu), event);
}

#endif
