/* HexChat
 * Copyright (C) 1998-2010 Peter Zelezny.
 * Copyright (C) 2009-2013 Berke Viktor.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#ifndef HEXCHAT_FKEYS_H
#define HEXCHAT_FKEYS_H

/* These are cp'ed from history.c --AGL */
#define STATE_SHIFT		GDK_SHIFT_MASK
#define STATE_ALT		GDK_MOD1_MASK
/* The "primary" shortcut modifier: Command (GDK_META_MASK) on native macOS
   (the Quartz backend), Control everywhere else. This used to be decided at
   compile time with __APPLE__, which made an X11 build on a Mac register
   every Ctrl shortcut as <Meta>: X11 never reports Meta, so GTK matched
   those accelerators on the bare key and typing 'q' quit the client. */
GdkModifierType key_primary_modifier (void);
#define STATE_CTRL		(key_primary_modifier ())

void key_init (void);
void key_dialog_show (void);
typedef enum
{
	KEY_NOT_HANDLED = 0,	/* pass to the entry (inserts the character) */
	KEY_HANDLED,			/* consumed */
	KEY_HANDLED_STOP		/* consumed, and stop other handlers seeing it */
} key_result;

/* toolkit-independent: keyval + modifier state in, result out */
key_result key_handle (GtkWidget *wid, guint keyval, GdkModifierType state, session *sess);
/* GTK3 key_press_event adapter around key_handle() */
int key_handle_key_press (GtkWidget * wid, GdkEventKey * evt, session *sess);
int key_action_insert (GtkWidget * wid, GdkEventKey * evt, char *d1, char *d2,
						 session *sess);

#endif
