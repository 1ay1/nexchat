/* nexchat
 * Copyright (C) 2026 nexchat contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

/* Menus without GtkMenu.
 *
 * GTK3: an nx_menu IS a GtkMenu and an nx_item IS its GtkMenuItem (the
 * functions here are the GTK3 calls nexchat always made), so converted and
 * not-yet-converted code mix freely and the menu tests keep meaning the
 * same thing.
 *
 * GTK4: nx_menu keeps its entries itself and renders them into a GMenu
 * (separators split sections) plus a GSimpleActionGroup (plain items are
 * stateless actions, checks boolean-state actions, radio groups one
 * string-state action), shown in a nested GtkPopoverMenu. The model is
 * rebuilt when entries are added/removed; sensitivity and check state go
 * straight to the actions.
 */

#ifndef NEXCHAT_NXMENU_H
#define NEXCHAT_NXMENU_H

#include <gtk/gtk.h>

typedef struct _nx_menu nx_menu;
typedef struct _nx_item nx_item;

/* activate: the item that was chosen (for checks/radios the new state is
   already set, read it with nx_item_get_active) */
typedef void (*nx_item_cb) (nx_item *item, gpointer userdata);

enum
{
	NX_ITEM_MNEMONIC = 1 << 0,	/* "_x" marks the mnemonic */
	NX_ITEM_MARKUP = 1 << 1,	/* the label is Pango markup */
	NX_ITEM_INSENSITIVE = 1 << 2,
	/* an icon item whose label is NOT a mnemonic (icon items default to
	   mnemonic labels, like gtk_image_menu_item_new_with_mnemonic) */
	NX_ITEM_PLAIN_ICON = 1 << 3
};

#define NX_MENU_APPEND (-1)

nx_menu *nx_menu_new (void);

/* a plain item; icon (may be NULL) is a readable image file path or else a
   stock name for gtkutil (NX_STOCK_*); an item with an icon always takes
   its label as a mnemonic (GtkImageMenuItem did). pos: index among the
   menu's entries (separators count), NX_MENU_APPEND for the end. */
nx_item *nx_menu_add_item (nx_menu *menu, int pos, const char *label, int flags,
									const char *icon, nx_item_cb cb, gpointer userdata);
/* the same with the icon as a pixbuf (an app resource) */
nx_item *nx_menu_add_item_pixbuf (nx_menu *menu, int pos, const char *label, int flags,
											 GdkPixbuf *icon, nx_item_cb cb, gpointer userdata);
nx_item *nx_menu_add_check (nx_menu *menu, int pos, const char *label, int flags,
									 gboolean active, nx_item_cb cb, gpointer userdata);
/* group: an earlier radio item of the same group, or NULL to start one */
nx_item *nx_menu_add_radio (nx_menu *menu, int pos, const char *label, int flags,
									 nx_item *group, gboolean active, nx_item_cb cb, gpointer userdata);
nx_item *nx_menu_add_separator (nx_menu *menu, int pos);
/* a submenu entry; item_ret (may be NULL) gets its item */
nx_menu *nx_menu_add_submenu (nx_menu *menu, int pos, const char *label, int flags,
										nx_item **item_ret);
/* a submenu entry laid out like an icon item (icon column, icon may be
   NULL for an empty one; the label is a mnemonic) */
nx_menu *nx_menu_add_submenu_icon (nx_menu *menu, int pos, const char *label,
											  const char *icon, nx_item **item_ret);
/* the same with a plain (non-mnemonic) label */
nx_menu *nx_menu_add_submenu_icon_plain (nx_menu *menu, int pos, const char *label,
													  const char *icon);

/* number of entries (separators included) */
int nx_menu_get_n_items (nx_menu *menu);
nx_item *nx_menu_get_item (nx_menu *menu, int index);
/* take the item (and its submenu) out of its menu and free it */
void nx_item_remove (nx_item *item);
/* remove every entry */
void nx_menu_clear (nx_menu *menu);
/* run cb when this (sub)menu is shown. GTK3: each time the submenu opens;
   GTK4 (no per-submenu signal): once, when the popup it's in opens. */
typedef void (*nx_menu_show_cb) (nx_menu *menu, gpointer userdata);
void nx_menu_on_show (nx_menu *menu, nx_menu_show_cb cb, gpointer userdata);
/* drop the on_show hook again */
void nx_menu_on_show_disconnect (nx_menu *menu);

void nx_item_set_sensitive (nx_item *item, gboolean sensitive);
gboolean nx_item_get_sensitive (nx_item *item);
/* check/radio state; _quiet doesn't run the item's callback */
gboolean nx_item_get_active (nx_item *item);
void nx_item_set_active_quiet (nx_item *item, gboolean active);
/* the label as given (mnemonic underscore / markup kept), NULL for
   separators */
const char *nx_item_get_label (nx_item *item);
/* the text shown: markup tags and the mnemonic underscore gone, NULL for
   separators. Returned string lives as long as the item. */
const char *nx_item_get_text (nx_item *item);
nx_menu *nx_item_get_submenu (nx_item *item);
gboolean nx_item_is_separator (nx_item *item);
/* a widget to hang clipboard/toplevel lookups off from an item's
   callback (GTK3: the item; GTK4: the widget the menu popped up on) */
GtkWidget *nx_item_get_widget (nx_item *item);
void nx_item_set_data (nx_item *item, const char *key, gpointer data);
gpointer nx_item_get_data (nx_item *item, const char *key);
/* as if the user chose it (checks toggle, radios switch; callbacks run in
   the order GTK3 ran them: a radio going off, then the one going on) */
void nx_item_activate (nx_item *item);

/* pop the menu up at the pointer of event (from nx_current_event (); NULL:
   where the pointer is now) over widget. The menu is freed once it closes
   and done (may be NULL) runs then with done_data. */
void nx_menu_popup (nx_menu *menu, GtkWidget *widget, GdkEvent *event,
						  GDestroyNotify done, gpointer done_data);

/* ---- menu bars and keyboard shortcuts ---- */

/* a menu bar: its entries are the top-level submenus */
nx_menu *nx_menu_bar_new (void);
/* the widget to put in a window (GTK3: the GtkMenuBar itself) */
GtkWidget *nx_menu_bar_get_widget (nx_menu *bar);

/* shortcuts of items under `menu` (a bar or a popup) work in `window`. Call
   once, before nx_item_set_accel. They work while the item is sensitive,
   even with the menu bar hidden (GTK2's can-activate-accel rule). window
   NULL: shortcuts are only shown (a popup copy of the main menu). Without
   this call nx_item_set_accel does nothing (popups had no accel group). */
void nx_menu_set_accel_window (nx_menu *menu, GtkWidget *window);
/* key + mods activate the item (shown next to its label); key 0 removes */
void nx_item_set_accel (nx_item *item, guint key, GdkModifierType mods);
/* the shortcut, key 0 if none */
void nx_item_get_accel (nx_item *item, guint *key, GdkModifierType *mods);

typedef enum { NX_KIND_ITEM, NX_KIND_CHECK, NX_KIND_RADIO, NX_KIND_SEPARATOR,
					NX_KIND_SUBMENU } nx_item_kind;
nx_item_kind nx_item_get_kind (nx_item *item);

#if GTK_CHECK_VERSION(4, 0, 0)
/* what a popup shows: the model (actions are "nx.<name>") and the actions;
   for tests */
GMenuModel *nx_menu_get_model (nx_menu *menu);
GActionGroup *nx_menu_get_actions (nx_menu *menu);
/* the popover while the popup is up, else NULL; for tests */
GtkWidget *nx_menu_get_popover (nx_menu *menu);
void nx_menu_free (nx_menu *menu);
#ifdef __APPLE__
/* macOS: menu bars made after this go in the global menu bar of app */
void nx_menu_use_global_menubar (GtkApplication *app);
#endif
#ifdef G_OS_WIN32
/* Windows: popups become real Win32 menus; owner_of gives the HWND to own
   one shown from a widget (NULL: fall back to the popover) */
void nx_menu_use_native_popups (gboolean on, void *(*owner_of) (GtkWidget *w));
#endif
#else
/* GTK3 only, during the move: the widgets behind the handles */
#define NX_MENU_WIDGET(m) ((GtkWidget *) (m))
#define NX_ITEM_WIDGET(i) ((GtkWidget *) (i))
#define NX_MENU(w) ((nx_menu *) (w))
#define NX_ITEM(w) ((nx_item *) (w))
#endif

#endif
