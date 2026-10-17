/* nexchat
 * Copyright (C) 2026 nexchat contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

/* Windows notifications without the WinRT C++ DLL: a balloon on the tray
   icon (Shell_NotifyIcon NIF_INFO). Windows 10/11 turn balloons into real
   toasts in the Action Center, with the app's name and icon. Used when
   hcnotifications-winrt.dll isn't there (mingw builds). */

#include <windows.h>
#include <shellapi.h>

#include <glib.h>
#include "notification-balloon.h"

static HWND balloon_hwnd;
static NOTIFYICONDATAW balloon_nid;
static gboolean balloon_added;

static LRESULT CALLBACK
balloon_wndproc (HWND h, UINT m, WPARAM w, LPARAM l)
{
	return DefWindowProcW (h, m, w, l);
}

int
balloon_notification_init (void)
{
	WNDCLASSW wc = { 0 };

	if (balloon_hwnd)
		return 1;
	wc.lpfnWndProc = balloon_wndproc;
	wc.hInstance = GetModuleHandleW (NULL);
	wc.lpszClassName = L"nexchat-notify";
	RegisterClassW (&wc);
	balloon_hwnd = CreateWindowW (L"nexchat-notify", L"nexchat", 0, 0, 0, 0, 0, HWND_MESSAGE,
											NULL, GetModuleHandleW (NULL), NULL);
	if (!balloon_hwnd)
		return 0;

	balloon_nid.cbSize = sizeof balloon_nid;
	balloon_nid.hWnd = balloon_hwnd;
	balloon_nid.uID = 2;
	balloon_nid.uFlags = NIF_ICON | NIF_TIP;
	/* the exe's own icon (from its resources) */
	balloon_nid.hIcon = LoadIconW (GetModuleHandleW (NULL), MAKEINTRESOURCEW (1));
	if (!balloon_nid.hIcon)
		balloon_nid.hIcon = LoadIconW (NULL, (LPCWSTR) IDI_APPLICATION);
	wcscpy (balloon_nid.szTip, L"nexchat");
	return 1;
}

void
balloon_notification_show (const char *title, const char *text)
{
	wchar_t *wt, *wx;

	if (!balloon_hwnd)
		return;
	/* an icon has to exist for a balloon; a hidden one, if the tray icon
	   is off, keeps the notification area clean */
	if (!balloon_added)
	{
		balloon_nid.uFlags = NIF_ICON | NIF_TIP | NIF_STATE;
		balloon_nid.dwState = NIS_HIDDEN;
		balloon_nid.dwStateMask = NIS_HIDDEN;
		balloon_added = Shell_NotifyIconW (NIM_ADD, &balloon_nid);
		balloon_nid.uVersion = NOTIFYICON_VERSION_4;
		Shell_NotifyIconW (NIM_SETVERSION, &balloon_nid);
	}
	wt = g_utf8_to_utf16 (title ? title : "", -1, NULL, NULL, NULL);
	wx = g_utf8_to_utf16 (text ? text : "", -1, NULL, NULL, NULL);
	balloon_nid.uFlags = NIF_INFO;
	wcsncpy (balloon_nid.szInfoTitle, wt ? wt : L"", G_N_ELEMENTS (balloon_nid.szInfoTitle) - 1);
	wcsncpy (balloon_nid.szInfo, wx ? wx : L"", G_N_ELEMENTS (balloon_nid.szInfo) - 1);
	balloon_nid.szInfoTitle[G_N_ELEMENTS (balloon_nid.szInfoTitle) - 1] = 0;
	balloon_nid.szInfo[G_N_ELEMENTS (balloon_nid.szInfo) - 1] = 0;
	balloon_nid.dwInfoFlags = NIIF_USER | NIIF_LARGE_ICON;
	{
		BOOL ok = Shell_NotifyIconW (NIM_MODIFY, &balloon_nid);
		if (g_getenv ("NX_DEBUG_NOTIFY"))
			g_printerr ("nexchat: balloon added=%d shown=%d '%s'\n", balloon_added, ok, title);
	}
	g_free (wt);
	g_free (wx);
}

void
balloon_notification_deinit (void)
{
	if (balloon_added)
		Shell_NotifyIconW (NIM_DELETE, &balloon_nid);
	balloon_added = FALSE;
	if (balloon_hwnd)
		DestroyWindow (balloon_hwnd);
	balloon_hwnd = NULL;
}
