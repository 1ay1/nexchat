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
GtkWidget *gtkutil_image_new_from_stock (const char *stock_id, nx_icon_size size);
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
	char *text;				/* displayed text, made on demand */
	int flags;
	char *icon;
	GdkPixbuf *pixbuf;
	nx_item_cb cb;
	gpointer userdata;
	gboolean sensitive;
	gboolean active;		/* check / radio */
	radio_group *group;	/* radio */
	char *target;			/* radio: this item's value in the group's action */
	nx_menu *submenu;
	GSimpleAction *action;	/* item / check: its own; radio: the group's (ref) */
	guint accel_key;
	GdkModifierType accel_mods;
	GtkShortcut *shortcut;	/* in the root's controller */
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
	nx_menu_show_cb show_cb;
	gpointer show_data;
	gboolean is_bar;			/* entries are submenus, no sections */
	GtkWidget *bar_widget;	/* GtkPopoverMenuBar */
	GtkShortcutController *shortcuts;	/* root only */
	gboolean accel_on;		/* root: nx_menu_set_accel_window was called */
	gboolean dirty;			/* model needs a rebuild */
};

static nx_menu *
menu_root (nx_menu *m)
{
	while (m->parent)
		m = m->parent->menu;
	return m;
}

static void
menu_icons_init (void);

nx_menu *
nx_menu_new (void)
{
	nx_menu *m = g_new0 (nx_menu, 1);
	menu_icons_init ();
	m->model = g_menu_new ();
	m->actions = g_simple_action_group_new ();
	{
		/* what greyed submenu entries point at */
		GSimpleAction *off = g_simple_action_new ("nx-off", NULL);
		g_simple_action_set_enabled (off, FALSE);
		g_action_map_add_action (G_ACTION_MAP (m->actions), G_ACTION (off));
		g_object_unref (off);
	}
	return m;
}

static void
item_free (nx_item *it)
{
	nx_menu *root = menu_root (it->menu);

	if (it->shortcut)
	{
		if (root->shortcuts)
			gtk_shortcut_controller_remove_shortcut (root->shortcuts, it->shortcut);
		g_object_unref (it->shortcut);
	}
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
	g_free (it->text);
	g_free (it->icon);
	if (it->pixbuf)
		g_object_unref (it->pixbuf);
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
	{
		g_object_remove_weak_pointer (G_OBJECT (m->popover), (gpointer *) &m->popover);
		gtk_widget_unparent (m->popover);
	}
	if (m->shortcuts)
		g_object_unref (m->shortcuts);
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

	if (it->flags & NX_ITEM_MNEMONIC || (it->icon && !(it->flags & NX_ITEM_PLAIN_ICON)))
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

/* popups get their own prefix: the bar's shortcuts label "nx.iN" window-wide */
static const char *
menu_prefix (nx_menu *m)
{
	return menu_root (m)->is_bar ? "nx" : "nxp";
}

static GMenuItem *
model_item (nx_item *it)
{
	char *label = model_label (it);
	char name[48];
	GMenuItem *mi;

	if (it->kind == E_SUBMENU)
	{
		mi = g_menu_item_new_submenu (label, G_MENU_MODEL (it->submenu->model));
		/* a greyed submenu entry: the tracker follows an action's enabled
		   state, so point it at a disabled one */
		if (!it->sensitive)
		{
			g_snprintf (name, sizeof name, "%s.nx-off", menu_prefix (it->menu));
			g_menu_item_set_attribute (mi, G_MENU_ATTRIBUTE_ACTION, "s", name);
		}
	}
	else
	{
		g_snprintf (name, sizeof name, "%s.%s", menu_prefix (it->menu),
						g_action_get_name (G_ACTION (it->action)));
		mi = g_menu_item_new (label, NULL);
		if (it->kind == E_RADIO)
			g_menu_item_set_action_and_target_value (mi, name, g_variant_new_string (it->target));
		else
			g_menu_item_set_action_and_target_value (mi, name, NULL);
	}
	if (it->flags & NX_ITEM_MARKUP)
		g_menu_item_set_attribute (mi, "use-markup", "b", TRUE);
	if (it->pixbuf)
	{
		/* GdkPixbuf is a GIcon */
		g_menu_item_set_icon (mi, G_ICON (it->pixbuf));
	}
	else if (it->icon)
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
	if (it->accel_key)
	{
		/* shown next to the label (the shortcut controller does the work) */
		char *accel = gtk_accelerator_name (it->accel_key, it->accel_mods);
		g_menu_item_set_attribute (mi, "accel", "s", accel);
		g_free (accel);
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
	if (m->is_bar)
	{
		/* a bar's model is its submenus, flat */
		g_object_unref (section);
		for (l = m->entries; l; l = l->next)
		{
			GMenuItem *mi = model_item (l->data);
			g_menu_append_item (m->model, mi);
			g_object_unref (mi);
		}
		return;
	}
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

/* models are rebuilt lazily: building an 80-entry menu shouldn't redo the
   model 80 times. Shown menus (popups, bars) rebuild right away. */
static void
changed (nx_menu *m)
{
	nx_menu *root = menu_root (m);

	m->dirty = TRUE;
	if (root->popover || root->bar_widget)
		rebuild_one (m);
}

/* bring m and every submenu's model up to date */
static void
sync_models (nx_menu *m)
{
	GList *l;

	if (m->dirty)
	{
		rebuild_one (m);
		m->dirty = FALSE;
	}
	for (l = m->entries; l; l = l->next)
		if (((nx_item *) l->data)->submenu)
			sync_models (((nx_item *) l->data)->submenu);
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
nx_menu_add_item_pixbuf (nx_menu *m, int pos, const char *label, int flags,
								 GdkPixbuf *icon, nx_item_cb cb, gpointer userdata)
{
	nx_item *it = nx_menu_add_item (m, pos, label, flags | NX_ITEM_MNEMONIC, NULL, cb, userdata);
	it->pixbuf = icon ? g_object_ref (icon) : NULL;
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

nx_menu *
nx_menu_add_submenu_icon (nx_menu *m, int pos, const char *label,
								  const char *icon, nx_item **item_ret)
{
	nx_item *it;
	nx_menu *sub = nx_menu_add_submenu (m, pos, label, NX_ITEM_MNEMONIC, &it);

	it->icon = g_strdup (icon);
	changed (m);
	if (item_ret)
		*item_ret = it;
	return sub;
}

nx_menu *
nx_menu_add_submenu_icon_plain (nx_menu *m, int pos, const char *label, const char *icon)
{
	nx_item *it;
	nx_menu *sub = nx_menu_add_submenu (m, pos, label, NX_ITEM_PLAIN_ICON, &it);

	it->icon = g_strdup (icon);
	changed (m);
	return sub;
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
nx_item_remove (nx_item *it)
{
	nx_menu *m = it->menu;

	m->entries = g_list_remove (m->entries, it);
	item_free (it);
	changed (m);
}

void
nx_menu_clear (nx_menu *m)
{
	while (m->entries)
	{
		nx_item *it = m->entries->data;
		m->entries = g_list_remove (m->entries, it);
		item_free (it);
	}
	changed (m);
}

void
nx_menu_on_show (nx_menu *m, nx_menu_show_cb cb, gpointer userdata)
{
	m->show_cb = cb;
	m->show_data = userdata;
}

void
nx_menu_on_show_disconnect (nx_menu *m)
{
	m->show_cb = NULL;
}

/* find the submenu whose model a nested popover shows */
static nx_menu *
find_submenu (nx_menu *m, GMenuModel *model)
{
	GList *l;

	for (l = m->entries; l; l = l->next)
	{
		nx_item *it = l->data;
		nx_menu *found;

		if (!it->submenu)
			continue;
		if (G_MENU_MODEL (it->submenu->model) == model)
			return it->submenu;
		if ((found = find_submenu (it->submenu, model)))
			return found;
	}
	return NULL;
}

/* a submenu opens: its on_show runs now, as GTK3's "show" did */
static void
popup_submenu_shown (GtkWidget *sub_popover, nx_menu *sub)
{
	if (sub->show_cb)
		sub->show_cb (sub, sub->show_data);
}

/* nested popovers are made from the model, one per submenu, inside ours */
static void
hook_submenus (GtkWidget *w, nx_menu *root)
{
	GtkWidget *c;

	for (c = gtk_widget_get_first_child (w); c; c = gtk_widget_get_next_sibling (c))
	{
		if (GTK_IS_POPOVER_MENU (c))
		{
			nx_menu *sub = find_submenu (root, gtk_popover_menu_get_menu_model (GTK_POPOVER_MENU (c)));
			if (sub && sub->show_cb)
				g_signal_connect (c, "show", G_CALLBACK (popup_submenu_shown), sub);
		}
		hook_submenus (c, root);
	}
}

void
nx_item_set_sensitive (nx_item *it, gboolean sensitive)
{
	it->sensitive = sensitive;
	if (it->kind == E_SUBMENU)
	{
		changed (it->menu);
		return;
	}
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

/* what GtkLabel would show: markup parsed, mnemonic underscore dropped */
const char *
nx_item_get_text (nx_item *it)
{
	char *text = NULL;

	if (it->kind == E_SEPARATOR)
		return NULL;
	if (!it->text)
	{
		gboolean mnemonic = (it->flags & NX_ITEM_MNEMONIC) || (it->icon && !(it->flags & NX_ITEM_PLAIN_ICON));

		if (it->flags & NX_ITEM_MARKUP)
		{
			if (!pango_parse_markup (it->label, -1, mnemonic ? '_' : 0, NULL, &text, NULL, NULL))
				text = g_strdup (it->label);
		}
		else if (mnemonic)
		{
			GString *s = g_string_new (NULL);
			const char *p;

			/* "__" is a literal underscore, a single one marks the key */
			for (p = it->label; *p; p++)
			{
				if (*p == '_')
				{
					if (p[1] != '_')
						continue;
					p++;
				}
				g_string_append_c (s, *p);
			}
			text = g_string_free (s, FALSE);
		}
		else
			text = g_strdup (it->label);
		it->text = text;
	}
	return it->text;
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
	sync_models (m);
	return G_MENU_MODEL (m->model);
}

GActionGroup *
nx_menu_get_actions (nx_menu *m)
{
	return G_ACTION_GROUP (menu_root (m)->actions);
}

GtkWidget *
nx_menu_get_popover (nx_menu *m)
{
	return m->popover;
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

/* GTK4 focuses the first item on popup; GTK3 had nothing selected and the
   first Down picked it. Eat that first Down. */
static gboolean
popup_fresh_key (GtkEventControllerKey *k, guint keyval, guint keycode,
					  GdkModifierType state, gpointer data)
{
	GtkWidget *p = gtk_event_controller_get_widget (GTK_EVENT_CONTROLLER (k));
	gboolean fresh = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (p), "nx-fresh"));

	g_object_set_data (G_OBJECT (p), "nx-fresh", NULL);
	return fresh && (keyval == GDK_KEY_Down || keyval == GDK_KEY_KP_Down);
}

/* GtkModelButton hides its icon when it has a label; GTK3 menus showed
   both, icon on the left. Fix each menu item as it maps. */
static gboolean
model_button_map_hook (GSignalInvocationHint *ih, guint n, const GValue *v, gpointer d)
{
	GtkWidget *b = g_value_get_object (&v[0]);
	GtkWidget *c;

	if (strcmp (G_OBJECT_TYPE_NAME (b), "GtkModelButton") != 0)
		return TRUE;
	for (c = gtk_widget_get_first_child (b); c; c = gtk_widget_get_next_sibling (c))
	{
		if (GTK_IS_IMAGE (c) && gtk_image_get_storage_type (GTK_IMAGE (c)) != GTK_IMAGE_EMPTY)
		{
			gtk_widget_set_visible (c, TRUE);
			gtk_widget_set_hexpand (c, FALSE);
			gtk_widget_add_css_class (c, "nx-menu-icon");
		}
		/* the main label takes the free width: accel and submenu arrow
		   end up at the right edge */
		else if (GTK_IS_LABEL (c) && !gtk_widget_has_css_class (c, "accelerator"))
			gtk_widget_set_hexpand (c, TRUE);
	}
	return TRUE;
}

static void
menu_icons_init (void)
{
	static gboolean done;

	if (done)
		return;
	done = TRUE;
	/* the signal only exists once the class does (a menu can come first) */
	g_type_class_unref (g_type_class_ref (GTK_TYPE_WIDGET));
	g_signal_add_emission_hook (g_signal_lookup ("map", GTK_TYPE_WIDGET), 0,
										model_button_map_hook, NULL, NULL);
}

void
nx_menu_popup (nx_menu *m, GtkWidget *widget, GdkEvent *event,
					GDestroyNotify done, gpointer done_data)
{
	GtkWidget *p, *anchor = widget;
	GtkEventController *k;
	double x = 0, y = 0;
	GdkRectangle r;

	/* a popover added after a tree view's header buttons trips a css node
	   critical; hang it off the parent */
G_GNUC_BEGIN_IGNORE_DEPRECATIONS
	while (GTK_IS_TREE_VIEW (anchor) && gtk_widget_get_parent (anchor))
		anchor = gtk_widget_get_parent (anchor);
G_GNUC_END_IGNORE_DEPRECATIONS

	sync_models (m);
	p = gtk_popover_menu_new_from_model_full (G_MENU_MODEL (m->model), GTK_POPOVER_MENU_NESTED);
	m->popover = p;
	/* it can go away with its parent first (window closed, menu open) */
	g_object_add_weak_pointer (G_OBJECT (p), (gpointer *) &m->popover);
	m->done = done;
	m->done_data = done_data;
	gtk_widget_insert_action_group (p, "nxp", G_ACTION_GROUP (m->actions));
	gtk_widget_set_parent (p, anchor);
	gtk_popover_set_has_arrow (GTK_POPOVER (p), FALSE);
	gtk_widget_set_halign (p, GTK_ALIGN_START);
	/* at the pointer: the event's position in anchor coordinates */
	if (event && gdk_event_get_position (event, &x, &y))
	{
		GtkNative *n = gtk_widget_get_native (anchor);
		graphene_point_t in, out;
		double nx, ny;

		gtk_native_get_surface_transform (n, &nx, &ny);
		in = GRAPHENE_POINT_INIT ((float) (x - nx), (float) (y - ny));
		if (gtk_widget_compute_point (GTK_WIDGET (n), anchor, &in, &out))
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
	if (m->show_cb)
		m->show_cb (m, m->show_data);
	hook_submenus (p, m);
	gtk_popover_popup (GTK_POPOVER (p));
	g_object_set_data (G_OBJECT (p), "nx-fresh", GINT_TO_POINTER (1));
	k = gtk_event_controller_key_new ();
	gtk_event_controller_set_propagation_phase (k, GTK_PHASE_CAPTURE);
	g_signal_connect (k, "key-pressed", G_CALLBACK (popup_fresh_key), NULL);
	gtk_widget_add_controller (p, k);
}

nx_menu *
nx_menu_bar_new (void)
{
	nx_menu *m = nx_menu_new ();
	m->is_bar = TRUE;
	return m;
}

GtkWidget *
nx_menu_bar_get_widget (nx_menu *m)
{
	if (!m->bar_widget)
	{
		sync_models (m);
		m->bar_widget = gtk_popover_menu_bar_new_from_model (G_MENU_MODEL (m->model));
		gtk_widget_insert_action_group (m->bar_widget, "nx", G_ACTION_GROUP (m->actions));
		/* the bar owns the menu: freed with it */
		g_object_set_data_full (G_OBJECT (m->bar_widget), "nx-menu", m,
										(GDestroyNotify) nx_menu_free);
	}
	return m->bar_widget;
}

void
nx_menu_set_accel_window (nx_menu *m, GtkWidget *window)
{
	m = menu_root (m);
	if (m->accel_on)
		return;
	m->accel_on = TRUE;
	if (!window)
		return;
	m->shortcuts = GTK_SHORTCUT_CONTROLLER (gtk_shortcut_controller_new ());
	g_object_ref (m->shortcuts);
	/* anywhere in the window, like a GtkAccelGroup */
	gtk_shortcut_controller_set_scope (m->shortcuts, GTK_SHORTCUT_SCOPE_MANAGED);
	gtk_event_controller_set_propagation_phase (GTK_EVENT_CONTROLLER (m->shortcuts),
															  GTK_PHASE_CAPTURE);
	gtk_widget_add_controller (window, GTK_EVENT_CONTROLLER (m->shortcuts));
	/* the actions are reachable from the window for the shortcuts */
	gtk_widget_insert_action_group (window, "nx", G_ACTION_GROUP (m->actions));
}

void
nx_item_set_accel (nx_item *it, guint key, GdkModifierType mods)
{
	nx_menu *root = menu_root (it->menu);
	char name[48];

	if (!root->accel_on)
		return;
	if (it->shortcut)
	{
		if (root->shortcuts)
			gtk_shortcut_controller_remove_shortcut (root->shortcuts, it->shortcut);
		g_clear_object (&it->shortcut);
	}
	it->accel_key = key;
	it->accel_mods = mods;
	if (key && it->action && root->shortcuts)
	{
		/* disabled actions don't fire: the "only while sensitive" rule */
		g_snprintf (name, sizeof name, "nx.%s", g_action_get_name (G_ACTION (it->action)));
		it->shortcut = gtk_shortcut_new (gtk_keyval_trigger_new (key, mods),
													gtk_named_action_new (name));
		if (it->kind == E_RADIO)
			gtk_shortcut_set_arguments (it->shortcut, g_variant_new_string (it->target));
		g_object_ref (it->shortcut);
		gtk_shortcut_controller_add_shortcut (root->shortcuts, it->shortcut);
	}
	changed (it->menu);
}

void
nx_item_get_accel (nx_item *it, guint *key, GdkModifierType *mods)
{
	*key = it->accel_key;
	*mods = it->accel_mods;
}

nx_item_kind
nx_item_get_kind (nx_item *it)
{
	switch (it->kind)
	{
	case E_CHECK: return NX_KIND_CHECK;
	case E_RADIO: return NX_KIND_RADIO;
	case E_SEPARATOR: return NX_KIND_SEPARATOR;
	case E_SUBMENU: return NX_KIND_SUBMENU;
	default: return NX_KIND_ITEM;
	}
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
			img = gtkutil_image_new_from_stock (icon, NX_ICON_MENU);
		item = gtkutil_menu_item_new_with_image (label, (flags & NX_ITEM_MARKUP) != 0, img);
		if (flags & NX_ITEM_PLAIN_ICON)
			gtk_label_set_text (GTK_LABEL (gtkutil_menu_item_get_label (item)), label);
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
	nx_widget_show (item);
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
	nx_widget_show (item);
	return NX_ITEM (item);
}

nx_item *
nx_menu_add_separator (nx_menu *menu, int pos)
{
	GtkWidget *item = gtk_separator_menu_item_new ();

	place (menu, item, pos);
	nx_widget_show (item);
	return NX_ITEM (item);
}

nx_menu *
nx_menu_add_submenu (nx_menu *menu, int pos, const char *label, int flags, nx_item **item_ret)
{
	GtkWidget *sub = gtk_menu_new ();
	GtkWidget *item = label_item (label, flags);

	place (menu, item, pos);
	nx_widget_show (item);
	gtk_menu_item_set_submenu (GTK_MENU_ITEM (item), sub);
	if (flags & NX_ITEM_INSENSITIVE)
		gtk_widget_set_sensitive (item, FALSE);
	if (item_ret)
		*item_ret = NX_ITEM (item);
	return NX_MENU (sub);
}

nx_menu *
nx_menu_add_submenu_icon (nx_menu *menu, int pos, const char *label,
								  const char *icon, nx_item **item_ret)
{
	GtkWidget *sub = gtk_menu_new ();
	GtkWidget *img = icon ? gtkutil_image_new_from_stock (icon, NX_ICON_MENU) : NULL;
	GtkWidget *item = gtkutil_menu_item_new_with_image (label, FALSE, img);

	place (menu, item, pos);
	nx_widget_show_all (item);
	gtk_menu_item_set_submenu (GTK_MENU_ITEM (item), sub);
	if (item_ret)
		*item_ret = NX_ITEM (item);
	return NX_MENU (sub);
}

nx_menu *
nx_menu_add_submenu_icon_plain (nx_menu *menu, int pos, const char *label, const char *icon)
{
	nx_item *it;
	nx_menu *sub = nx_menu_add_submenu_icon (menu, pos, label, icon, &it);

	gtk_label_set_text (GTK_LABEL (gtkutil_menu_item_get_label (NX_ITEM_WIDGET (it))), label);
	return sub;
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
nx_item_remove (nx_item *item)
{
	gtk_widget_destroy (NX_ITEM_WIDGET (item));
}

void
nx_menu_clear (nx_menu *menu)
{
	GList *items = gtk_container_get_children (GTK_CONTAINER (menu)), *l;

	for (l = items; l; l = l->next)
		gtk_widget_destroy (l->data);
	g_list_free (items);
}

static void
menu_show_sig (GtkWidget *w, gpointer unused)
{
	nx_menu_show_cb cb = g_object_get_data (G_OBJECT (w), "nx-show-cb");

	if (cb)
		cb (NX_MENU (w), g_object_get_data (G_OBJECT (w), "nx-show-ud"));
}

void
nx_menu_on_show (nx_menu *menu, nx_menu_show_cb cb, gpointer userdata)
{
	if (!g_object_get_data (G_OBJECT (menu), "nx-show-cb"))
		g_signal_connect (menu, "show", G_CALLBACK (menu_show_sig), NULL);
	g_object_set_data (G_OBJECT (menu), "nx-show-cb", cb);
	g_object_set_data (G_OBJECT (menu), "nx-show-ud", userdata);
}

void
nx_menu_on_show_disconnect (nx_menu *menu)
{
	g_signal_handlers_disconnect_by_func (menu, menu_show_sig, NULL);
	g_object_set_data (G_OBJECT (menu), "nx-show-cb", NULL);
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

const char *
nx_item_get_text (nx_item *item)
{
	GtkWidget *lbl;

	if (GTK_IS_SEPARATOR_MENU_ITEM (item))
		return NULL;
	lbl = gtkutil_menu_item_get_label (NX_ITEM_WIDGET (item));
	return lbl ? gtk_label_get_text (GTK_LABEL (lbl)) : NULL;
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

nx_item *
nx_menu_add_item_pixbuf (nx_menu *menu, int pos, const char *label, int flags,
								 GdkPixbuf *icon, nx_item_cb cb, gpointer userdata)
{
	GtkWidget *img = gtk_image_new_from_pixbuf (icon);
	GtkWidget *item = gtkutil_menu_item_new_with_image (label, FALSE, img);

	place (menu, item, pos);
	connect_cb (item, cb, userdata);
	if (flags & NX_ITEM_INSENSITIVE)
		gtk_widget_set_sensitive (item, FALSE);
	nx_widget_show_all (item);
	return NX_ITEM (item);
}

nx_menu *
nx_menu_bar_new (void)
{
	return NX_MENU (gtk_menu_bar_new ());
}

GtkWidget *
nx_menu_bar_get_widget (nx_menu *bar)
{
	return NX_MENU_WIDGET (bar);
}

void
nx_menu_set_accel_window (nx_menu *menu, GtkWidget *window)
{
	GtkAccelGroup *ag;

	if (g_object_get_data (G_OBJECT (menu), "nx-accel-on"))
		return;
	g_object_set_data (G_OBJECT (menu), "nx-accel-on", GINT_TO_POINTER (1));
	ag = gtk_accel_group_new ();
	if (window)
		gtk_window_add_accel_group (GTK_WINDOW (window), ag);
	g_object_set_data_full (G_OBJECT (menu), "nx-accel", ag, g_object_unref);
}

/* Override the default GTK2.4 handler, which would make menu
   bindings not work when the menu-bar is hidden. */
static gboolean
can_activate_accel (GtkWidget *widget, guint signal_id, gpointer user_data)
{
	/* GTK2.2 behaviour */
	return gtk_widget_is_sensitive (widget);
}

void
nx_item_set_accel (nx_item *item, guint key, GdkModifierType mods)
{
	GtkWidget *w = NX_ITEM_WIDGET (item);
	GtkAccelGroup *ag = NULL;
	GtkWidget *top;

	/* the accel group lives on the top menu, which an item finds through
	   the parents (a submenu's attach widget, up to the bar / popup) */
	for (top = w; top; )
	{
		ag = g_object_get_data (G_OBJECT (top), "nx-accel");
		if (ag || g_object_get_data (G_OBJECT (top), "nx-accel-on"))
			break;
		if (GTK_IS_MENU (top) && gtk_menu_get_attach_widget (GTK_MENU (top)))
			top = gtk_menu_get_attach_widget (GTK_MENU (top));
		else
			top = gtk_widget_get_parent (top);
	}
	if (!ag)
		return;

	{
		guint oldkey = GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (w), "nx-key"));
		GdkModifierType oldmods = GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (w), "nx-mods"));

		if (oldkey)
			gtk_widget_remove_accelerator (w, ag, oldkey, oldmods);
	}
	g_object_set_data (G_OBJECT (w), "nx-key", GUINT_TO_POINTER (key));
	g_object_set_data (G_OBJECT (w), "nx-mods", GUINT_TO_POINTER (mods));
	if (!key)
		return;
	gtk_widget_add_accelerator (w, "activate", ag, key, mods, GTK_ACCEL_VISIBLE);
	if (!g_object_get_data (G_OBJECT (w), "nx-canaccel"))
	{
		g_signal_connect (w, "can-activate-accel", G_CALLBACK (can_activate_accel), NULL);
		g_object_set_data (G_OBJECT (w), "nx-canaccel", GINT_TO_POINTER (1));
	}
}

void
nx_item_get_accel (nx_item *item, guint *key, GdkModifierType *mods)
{
	*key = GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (item), "nx-key"));
	*mods = GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (item), "nx-mods"));
}

nx_item_kind
nx_item_get_kind (nx_item *item)
{
	if (GTK_IS_SEPARATOR_MENU_ITEM (item))
		return NX_KIND_SEPARATOR;
	if (GTK_IS_RADIO_MENU_ITEM (item))
		return NX_KIND_RADIO;
	if (GTK_IS_CHECK_MENU_ITEM (item))
		return NX_KIND_CHECK;
	if (gtk_menu_item_get_submenu (GTK_MENU_ITEM (item)))
		return NX_KIND_SUBMENU;
	return NX_KIND_ITEM;
}

#endif
