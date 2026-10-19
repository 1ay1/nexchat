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

#if defined(__APPLE__)
/* set: the bar goes in the macOS global menu bar (nx_menu_use_global_menubar) */
static GtkApplication *macos_global_app;
void nx_menu_bar_export_macos (nx_menu *m);
#endif

/* from gtkutil.c (not its header, which drags in the whole client) */
const char *gtkutil_stock_icon_name (const char *stock_id);

/* an image file we can read, rather than a stock / themed icon name */
static gboolean
icon_is_file (const char *icon)
{
	return icon && access (icon, R_OK) == 0;
}


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
#if defined(__APPLE__)
		if (macos_global_app)
			nx_menu_bar_export_macos (m);
#endif
	}
	return m->bar_widget;
}

#if defined(__APPLE__)
/* macOS: the menu is the global one at the top of the screen, not a bar
   in the window. GtkApplication shows a GMenuModel there and runs "app."
   actions, so the bar's model goes in with its actions added to the app
   (renamed nx.x -> app.x through a copy of the model). The in-window bar
   is hidden. */
void
nx_menu_use_global_menubar (GtkApplication *app)
{
	macos_global_app = app;
}

/* items start..end-1 of src (end < 0: to the end), actions renamed nx. ->
   app., submenus and sections copied too */
static GMenuModel *
macos_copy_range (GMenuModel *src, int start, int end)
{
	GMenu *dst = g_menu_new ();
	int i, n = g_menu_model_get_n_items (src);

	if (end < 0 || end > n)
		end = n;
	for (i = start; i < end; i++)
	{
		GMenuItem *mi = g_menu_item_new_from_model (src, i);
		GMenuModel *sub;
		char *action = NULL;

		if (g_menu_item_get_attribute (mi, G_MENU_ATTRIBUTE_ACTION, "s", &action) &&
			 g_str_has_prefix (action, "nx."))
		{
			GVariant *target = g_menu_item_get_attribute_value (mi, G_MENU_ATTRIBUTE_TARGET, NULL);
			char *app_action = g_strconcat ("app.", action + 3, NULL);
			g_menu_item_set_action_and_target_value (mi, app_action, target);
			g_free (app_action);
			if (target)
				g_variant_unref (target);
		}
		g_free (action);
		if ((sub = g_menu_item_get_link (mi, G_MENU_LINK_SUBMENU)))
		{
			GMenuModel *c = macos_copy_range (sub, 0, -1);
			g_menu_item_set_submenu (mi, c);
			g_object_unref (c);
			g_object_unref (sub);
		}
		if ((sub = g_menu_item_get_link (mi, G_MENU_LINK_SECTION)))
		{
			GMenuModel *c = macos_copy_range (sub, 0, -1);
			g_menu_item_set_section (mi, c);
			g_object_unref (c);
			g_object_unref (sub);
		}
		g_menu_append_item (dst, mi);
		g_object_unref (mi);
	}
	return G_MENU_MODEL (dst);
}

/* platform-macos.m: put the items of the bar's first menu ("nexchat")
   into the application menu GTK made, and wire its About, Settings and Quit
   to Help > About, Settings > Preferences and nexchat > Quit. bar is the
   whole bar with app. actions. */
void macos_merge_app_menu (GtkApplication *app, GMenuModel *bar);

/* GTK always puts its application menu first in the menu bar, titled with
   the app's name. The bar's own first menu, "nexchat" (HexChat's "HexChat"
   menu), would be a second menu of the same name, so its items are moved
   into the application menu, as HexChat's Mac build (gtk-mac-integration)
   did. */

/* Quit from the Dock icon's menu, a logout, a restart: GTK's app delegate
   activates app.quit instead of letting AppKit kill us; it's the same as
   nexchat > Quit (with its "really quit?" if connected and asked for) */
static void (*app_quit_hook) (void);

void
nx_menu_set_app_quit (void (*cb) (void))
{
	app_quit_hook = cb;
}

static void
nx_menu_app_quit (GSimpleAction *a, GVariant *p, gpointer d)
{
	if (app_quit_hook)
		app_quit_hook ();
	else if (macos_global_app)
		g_application_quit (G_APPLICATION (macos_global_app));
}

void
nx_menu_bar_export_macos (nx_menu *m)
{
	GtkApplication *app = macos_global_app;
	char **names;
	int i;
	GMenuModel *model;

	if (!app)
		return;
	names = g_action_group_list_actions (G_ACTION_GROUP (m->actions));
	for (i = 0; names && names[i]; i++)
	{
		GAction *a = g_action_map_lookup_action (G_ACTION_MAP (m->actions), names[i]);
		if (a)
			g_action_map_add_action (G_ACTION_MAP (app), a);
	}
	g_strfreev (names);
	if (!g_action_map_lookup_action (G_ACTION_MAP (app), "quit"))
	{
		GSimpleAction *q = g_simple_action_new ("quit", NULL);
		g_signal_connect (q, "activate", G_CALLBACK (nx_menu_app_quit), NULL);
		g_action_map_add_action (G_ACTION_MAP (app), G_ACTION (q));
		g_object_unref (q);
	}
	model = macos_copy_range (G_MENU_MODEL (m->model), 0, -1);
	gtk_application_set_menubar (app, model);
	macos_merge_app_menu (app, model);
	g_object_unref (model);
	gtk_widget_set_visible (m->bar_widget, FALSE);
	g_object_set_data (G_OBJECT (m->bar_widget), "nx-macos-global", GINT_TO_POINTER (1));
}
#endif

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

