/* nexchat
 * Copyright (C) 2026 nexchat contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

/* An nx_menu shown as a real Win32 popup menu (the tray menu). Same
   items, mnemonics, checks, radios, greyed items and shortcut labels;
   Windows draws it, light or dark like the rest of the system. */

#include <windows.h>
#include <string.h>

#include "win32-menu.h"

#define CMD_BASE 1000

/* GTK's "_File" mnemonic is Win32's "&File"; a literal & doubles */
static char *
win32_label (nx_item *it)
{
	const char *l = nx_item_get_label (it);
	const char *t = nx_item_get_text (it);
	GString *s;

	if (!l || strchr (l, '<'))	/* markup: the plain text */
		l = t ? t : "";
	s = g_string_new (NULL);
	for (; *l; l++)
	{
		if (*l == '&')
			g_string_append (s, "&&");
		else if (*l == '_' && l[1] == '_')
		{
			g_string_append_c (s, '_');
			l++;
		}
		else if (*l == '_')
			g_string_append_c (s, '&');
		else
			g_string_append_c (s, *l);
	}
	return g_string_free (s, FALSE);
}

/* items numbered from CMD_BASE in cmds, for TrackPopupMenu's return */
static HMENU
build (nx_menu *m, GPtrArray *cmds)
{
	HMENU h = CreatePopupMenu ();
	int i, n = nx_menu_get_n_items (m);

	for (i = 0; i < n; i++)
	{
		nx_item *it = nx_menu_get_item (m, i);
		nx_item_kind kind = nx_item_get_kind (it);
		MENUITEMINFOW mi = { sizeof mi };
		char *label;
		wchar_t *wl;
		guint key = 0;
		GdkModifierType mods = 0;
		nx_menu *sub;

		if (kind == NX_KIND_SEPARATOR)
		{
			AppendMenuW (h, MF_SEPARATOR, 0, NULL);
			continue;
		}
		label = win32_label (it);
		/* "Label<tab>Ctrl+F": Windows puts the shortcut on the right */
		nx_item_get_accel (it, &key, &mods);
		if (key)
		{
			char *a = gtk_accelerator_get_label (key, mods);
			char *l2 = g_strdup_printf ("%s\t%s", label, a);
			g_free (a);
			g_free (label);
			label = l2;
		}
		wl = g_utf8_to_utf16 (label, -1, NULL, NULL, NULL);
		g_free (label);

		mi.fMask = MIIM_STRING | MIIM_STATE | MIIM_FTYPE | MIIM_ID;
		mi.fType = MFT_STRING | (kind == NX_KIND_RADIO ? MFT_RADIOCHECK : 0);
		mi.dwTypeData = wl;
		mi.fState = (nx_item_get_sensitive (it) ? 0 : MFS_DISABLED) |
						(((kind == NX_KIND_CHECK || kind == NX_KIND_RADIO) && nx_item_get_active (it))
						 ? MFS_CHECKED : 0);
		sub = nx_item_get_submenu (it);
		if (sub)
		{
			mi.fMask |= MIIM_SUBMENU;
			mi.hSubMenu = build (sub, cmds);
		}
		else
		{
			g_ptr_array_add (cmds, it);
			mi.wID = CMD_BASE + cmds->len - 1;
		}
		InsertMenuItemW (h, GetMenuItemCount (h), TRUE, &mi);
		g_free (wl);
	}
	return h;
}

nx_item *
win32_menu_track (nx_menu *menu, void *owner)
{
	GPtrArray *cmds = g_ptr_array_new ();
	HMENU h = build (menu, cmds);
	nx_item *chosen = NULL;
	POINT pt;
	UINT cmd;

	GetCursorPos (&pt);
	cmd = TrackPopupMenuEx (h, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY |
									(GetSystemMetrics (SM_MENUDROPALIGNMENT) ? TPM_RIGHTALIGN : TPM_LEFTALIGN),
									pt.x, pt.y, (HWND) owner, NULL);
	/* documented TrackPopupMenu quirk: let the owner settle */
	PostMessageW ((HWND) owner, WM_NULL, 0, 0);
	DestroyMenu (h);
	if (cmd >= CMD_BASE && cmd - CMD_BASE < cmds->len)
		chosen = g_ptr_array_index (cmds, cmd - CMD_BASE);
	g_ptr_array_free (cmds, TRUE);
	return chosen;
}

/* Windows 10 1903+ draws popup menus dark only when the app opts in.
   The switch (uxtheme ordinal 135, SetPreferredAppMode) isn't in the SDK
   but it's what Explorer, Notepad++ and others use; without it menus are
   just light. */
void
win32_menu_follow_dark_mode (void)
{
	typedef int (WINAPI *set_mode_fn) (int);
	typedef void (WINAPI *flush_fn) (void);
	HMODULE ux = LoadLibraryExW (L"uxtheme.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
	set_mode_fn set_mode;
	flush_fn flush;

	if (!ux)
		return;
	set_mode = (set_mode_fn) (void *) GetProcAddress (ux, MAKEINTRESOURCEA (135));
	flush = (flush_fn) (void *) GetProcAddress (ux, MAKEINTRESOURCEA (136));
	if (set_mode)
		set_mode (1);	/* AllowDark: follow the system setting */
	if (flush)
		flush ();
}
