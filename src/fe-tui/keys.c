/* nexchat TUI — keybindings.conf (bridge.h): the GUI's key bindings file
 * (fe-gtk/fkeys.c), read the same way so one file works in both. GTK isn't
 * here, so accelerators are parsed by hand: "<Primary><Shift>Page_Up", the
 * old "C A S" + keyval-name lines too. Only a file the user has counts;
 * with none, the TUI's own keys (which are the GUI's defaults) stand.
 * Check For Replace (replace.conf) and Change Page "auto" (lastact) are
 * here too, the GUI's logic.
 */

#include "config.h"

#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <glib.h>

#include "../common/hexchat.h"
#include "../common/hexchatc.h"
#include "../common/cfgfiles.h"
#include "../common/fe.h"
#include "../common/outbound.h"
#include "../common/util.h"

#include "bridge.h"

void ui_dirty (void);

typedef struct
{
	char *key;
	int mods, action;
	char *d1, *d2;
} keybind;

static GPtrArray *binds;	/* of keybind*; NULL until loaded */

/* fkeys.c's key_actions[] names, in order */
static const char *const action_names[] = {
	"Run Command", "Change Page", "Insert in Buffer", "Scroll Page", "Set Buffer",
	"Last Command", "Next Command", "Complete nick/command", "Change Selected Nick",
	"Check For Replace", "Move front tab left", "Move front tab right",
	"Move tab family left", "Move tab family right", "Push input line into history",
};

static void
keybind_free (keybind *kb)
{
	g_free (kb->key);
	g_free (kb->d1);
	g_free (kb->d2);
	g_free (kb);
}

/* "<Primary><Shift>Page_Up" (gtk_accelerator_parse's syntax): mods, key */
static gboolean
parse_accel (const char *s, int *mods, char **key)
{
	*mods = 0;
	while (*s == '<')
	{
		const char *e = strchr (s, '>');
		char *m;
		if (!e)
			return FALSE;
		m = g_ascii_strdown (s + 1, e - s - 1);
		/* Primary is Control off macOS; in a terminal it's always Ctrl */
		if (!strcmp (m, "primary") || !strcmp (m, "control") || !strcmp (m, "ctrl") || !strcmp (m, "ctl")
			 || !strcmp (m, "meta") || !strcmp (m, "super"))
			*mods |= NX_KMOD_CTRL;
		else if (!strcmp (m, "alt") || !strcmp (m, "mod1"))
			*mods |= NX_KMOD_ALT;
		else if (!strcmp (m, "shift") || !strcmp (m, "shft"))
			*mods |= NX_KMOD_SHIFT;
		/* anything else (Hyper, Release...) a terminal can't send: keep
		   the binding out */
		else
		{
			g_free (m);
			return FALSE;
		}
		g_free (m);
		s = e + 1;
	}
	if (!*s)
		return FALSE;
	*key = g_strdup (s);
	return TRUE;
}

/* the old format's modifier line: "None", or letters C A S */
static gboolean
parse_oldmods (const char *s, int *mods)
{
	*mods = 0;
	if (!strcmp (s, "None"))
		return TRUE;
	for (; *s; s++)
		switch (*s)
		{
		case 'C': *mods |= NX_KMOD_CTRL; break;
		case 'A': *mods |= NX_KMOD_ALT; break;
		case 'S': *mods |= NX_KMOD_SHIFT; break;
		default: return FALSE;
		}
	return TRUE;
}

static int
action_of (const char *name)
{
	guint i;
	for (i = 0; i < G_N_ELEMENTS (action_names); i++)
		if (!strcmp (action_names[i], name))
			return (int) i;
	return -1;
}

/* fkeys.c key_load_kbs's state machine; a corrupt file loads nothing */
static void
keybinds_load (void)
{
	enum { MOD, KEY, ACT, DT1, DT2 } state = MOD;
	char *ibuf, *buf;
	gsize size;
	int pnt = 0;
	keybind *kb = NULL;
	char *path = g_build_filename (get_xdir (), "keybindings.conf", NULL);

	binds = g_ptr_array_new_with_free_func ((GDestroyNotify) keybind_free);
	if (!g_file_get_contents (path, &ibuf, &size, NULL))
	{
		g_free (path);
		return;
	}
	g_free (path);

	while (buf_get_line (ibuf, &buf, &pnt, (int) size))
	{
		g_strstrip (buf);
		if (buf[0] == '#' || buf[0] == 0)
			continue;
		switch (state)
		{
		case MOD:
			kb = g_new0 (keybind, 1);
			if (!strncmp (buf, "ACCEL=", 6))
			{
				if (!parse_accel (buf + 6, &kb->mods, &kb->key))
					kb->action = -2;		/* read its lines, then drop it */
				state = ACT;
				continue;
			}
			if (!parse_oldmods (buf, &kb->mods))
				goto corrupt;
			state = KEY;
			continue;
		case KEY:
			kb->key = g_strdup (buf);
			state = ACT;
			continue;
		case ACT:
			if (kb->action != -2)
				kb->action = action_of (buf);
			state = DT1;
			continue;
		case DT1:
		case DT2:
			if (buf[0] != 'D')
				goto corrupt;
			if (buf[2] == ':' && (buf[1] == '1' || buf[1] == '2'))
			{
				char **d = buf[1] == '1' ? &kb->d1 : &kb->d2;
				g_free (*d);
				*d = buf[3] ? g_strdup (buf + 3) : NULL;
			}
			else if (buf[2] != '!')
				goto corrupt;
			if (state == DT1)
			{
				state = DT2;
				continue;
			}
			if (kb->action >= 0 && kb->key)
				g_ptr_array_add (binds, kb);
			else
				keybind_free (kb);
			kb = NULL;
			state = MOD;
			continue;
		}
	}
	if (kb)
		keybind_free (kb);
	g_free (ibuf);
	return;

corrupt:
	if (kb)
		keybind_free (kb);
	g_ptr_array_set_size (binds, 0);
	g_free (ibuf);
	fe_message (_("There was an error loading key bindings configuration"), FE_MSG_ERROR);
}

int
nx_core_keybind_count (void)
{
	if (!binds)
		keybinds_load ();
	return (int) binds->len;
}

void
nx_core_keybind_get (int i, nx_keybind *out)
{
	keybind *kb;
	memset (out, 0, sizeof *out);
	if (!binds || i < 0 || (guint) i >= binds->len)
		return;
	kb = binds->pdata[i];
	out->key = kb->key;
	out->mods = kb->mods;
	out->action = kb->action;
	out->d1 = kb->d1;
	out->d2 = kb->d2;
}

/* fkeys.c key_action_handle_command: \n splits, \\ is a backslash */
void
nx_core_run_typed (nx_key buf, const char *d1)
{
	session *sess = (session *) buf;
	GString *out;
	const char *p;

	if (!d1 || !sess || !is_session (sess))
		return;
	out = g_string_new (NULL);
	for (p = d1; *p; p++)
	{
		if (*p == '\\' && p[1] == 'n')
		{
			g_string_append_c (out, '\n');
			p++;
		}
		else if (*p == '\\' && p[1] == '\\')
		{
			g_string_append_c (out, '\\');
			p++;
		}
		else
			g_string_append_c (out, *p);
	}
	handle_multiline (sess, out->str, 0, 0);
	g_string_free (out, TRUE);
	ui_dirty ();
}

static int
is_tab (session *sess)
{
	return sess != NULL;
}

nx_key
nx_core_lastact (void)
{
	return (nx_key) lastact_getfirst (is_tab);
}

/* fkeys.c replace_handle: the last word, a "'"-postfix kept */
char *
nx_core_replace (const char *text)
{
	GSList *list;
	const char *start;
	char *word, *postfix = NULL, *res = NULL, *q;
	size_t len = strlen (text);

	if (len < 1)
		return NULL;
	start = strrchr (text, ' ');
	start = start ? start + 1 : text;
	if (!*start || strlen (start) > 240)
		return NULL;
	word = g_strdup (start);
	len = strlen (word);
	if (word[0] == '\'' && word[len - 1] == '\'')
	{
		g_free (word);
		return NULL;
	}
	q = strchr (word, '\'');
	if (q)
	{
		postfix = q + 1;
		*q = 0;
	}
	for (list = replace_list; list; list = list->next)
	{
		struct popup *pop = list->data;
		if (!strcmp (pop->name, word))
		{
			res = g_strdup_printf ("%.*s%s%s", (int) (start - text), text, pop->cmd, postfix ? postfix : "");
			break;
		}
	}
	g_free (word);
	return res;
}
