/*
 * @file libsexy/sexy-icon-entry.c Entry widget
 *
 * @Copyright (C) 2004-2006 Christian Hammond.
 * Some of this code is from gtkspell, Copyright (C) 2002 Evan Martin.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gtk/gtk.h>
#include "gtkcompat.h"
#include "nxmenu.h"
#include "gtkevents.h"
#include "sexy-spell-entry.h"
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#include <glib/gi18n.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "sexy-iso-codes.h"

#ifdef WIN32
#include "marshal.h"
#else
#include "../common/marshal.h"
#endif

#ifdef WIN32
#include "../common/typedef.h"
#include <io.h>
#else
#include <unistd.h>
#endif

#include "../common/cfgfiles.h"
#include "../common/hexchatc.h"
#include "palette.h"
#include "xtext.h"

/* GTK3's stock table maps these ids to these icon names (gtkiconfactory.c) */
#define SEXY_ICON_ADD "list-add"
#define SEXY_ICON_REMOVE "list-remove"
#define SEXY_ICON_SPELL_CHECK "tools-check-spelling"

/*
 * Bunch of poop to make enchant into a runtime dependency rather than a
 * compile-time dependency.  This makes it so I don't have to hear the
 * complaints from people with binary distributions who don't get spell
 * checking because they didn't check their configure output.
 */
struct EnchantDict;
struct EnchantBroker;

typedef void (*EnchantDictDescribeFn) (const char * const lang_tag,
                                       const char * const provider_name,
                                       const char * const provider_desc,
                                       const char * const provider_file,
                                       void * user_data);

static struct EnchantBroker * (*enchant_broker_init) (void);
static void (*enchant_broker_free) (struct EnchantBroker * broker);
static void (*enchant_broker_free_dict) (struct EnchantBroker * broker, struct EnchantDict * dict);
static void (*enchant_broker_list_dicts) (struct EnchantBroker * broker, EnchantDictDescribeFn fn, void * user_data);
static struct EnchantDict * (*enchant_broker_request_dict) (struct EnchantBroker * broker, const char *const tag);

static void (*enchant_dict_add_to_personal) (struct EnchantDict * dict, const char *const word, ssize_t len);
static void (*enchant_dict_add_to_session) (struct EnchantDict * dict, const char *const word, ssize_t len);
static int (*enchant_dict_check) (struct EnchantDict * dict, const char *const word, ssize_t len);
static void (*enchant_dict_describe) (struct EnchantDict * dict, EnchantDictDescribeFn fn, void * user_data);
static void (*enchant_dict_free_suggestions) (struct EnchantDict * dict, char **suggestions);
static void (*enchant_dict_store_replacement) (struct EnchantDict * dict, const char *const mis, ssize_t mis_len, const char *const cor, ssize_t cor_len);
static char ** (*enchant_dict_suggest) (struct EnchantDict * dict, const char *const word, ssize_t len, size_t * out_n_suggs);
static gboolean have_enchant = FALSE;

struct _SexySpellEntryPriv
{
	struct EnchantBroker *broker;
	PangoAttrList        *attr_list;
	gint                  mark_character;
	GHashTable           *dict_hash;
	GSList               *dict_list;
	gchar               **words;
	gint                 *word_starts;
	gint                 *word_ends;
	gboolean              checked;
	gboolean              parseattr;
	gint                  preedit_length;	/* chars of IM preedit; GtkEntry hides its own in GTK3 */
	SexySpellEntryMenuHook menu_hook;
	gpointer              menu_hook_data;
	int                   cursor_style;	/* 0 GTK's own, else SexyCursorStyle */
	gboolean              cursor_blink;
	gboolean              cursor_off;	/* blinked out */
	guint                 blink_id;
	gint64                blink_start;
};

static void sexy_spell_entry_class_init(SexySpellEntryClass *klass);
static void sexy_spell_entry_editable_init (GtkEditableInterface *iface);
static void sexy_spell_entry_init(SexySpellEntry *entry);
static void sexy_spell_entry_finalize(GObject *obj);
static void sexy_spell_entry_destroy(GObject *obj);
static void sexy_spell_entry_menu_init (SexySpellEntry *entry);
static void sexy_spell_entry_snapshot (GtkWidget *widget, GtkSnapshot *snapshot);
static void cursor_notify_cb (GObject *o, GParamSpec *ps, gpointer entry);
static void cursor_focus_cb (GtkEventControllerFocus *c, gpointer entry);

/* GtkEditable handlers */
static void sexy_spell_entry_changed(GtkEditable *editable, gpointer data);

/* Internal utility functions */
static gboolean   word_misspelled                             (SexySpellEntry       *entry,
                                                               int                   start,
                                                               int                   end);
static gboolean   default_word_check                          (SexySpellEntry       *entry,
                                                               const gchar          *word);
static gboolean   sexy_spell_entry_activate_language_internal (SexySpellEntry       *entry,
                                                               const gchar          *lang,
                                                               GError              **error);
static gchar     *get_lang_from_dict                          (struct EnchantDict   *dict);
static void       sexy_spell_entry_recheck_all                (SexySpellEntry       *entry);
static void       entry_strsplit_utf8                         (GtkEntry             *entry,
                                                               gchar              ***set,
                                                               gint                **starts,
                                                               gint                **ends);

static GtkEntryClass *parent_class = NULL;

#ifdef HAVE_ISO_CODES
static int codetable_ref = 0;
#endif

G_DEFINE_TYPE_EXTENDED(SexySpellEntry, sexy_spell_entry, GTK_TYPE_ENTRY, 0, G_IMPLEMENT_INTERFACE(GTK_TYPE_EDITABLE, sexy_spell_entry_editable_init));

enum
{
	WORD_CHECK,
	LAST_SIGNAL
};
static guint signals[LAST_SIGNAL] = {0};

static PangoAttrList *empty_attrs_list = NULL;

static gboolean
spell_accumulator(GSignalInvocationHint *hint, GValue *return_accu, const GValue *handler_return, gpointer data)
{
	gboolean ret = g_value_get_boolean(handler_return);
	/* Handlers return TRUE if the word is misspelled.  In this
	 * case, it means that we want to stop if the word is checked
	 * as correct */
	g_value_set_boolean (return_accu, ret);
	return ret;
}

/* enchant warns about every backend plugin whose library isn't installed
   (hspell, voikko, ...); that's normal, the others still work */
static void
enchant_log_cb (const gchar *domain, GLogLevelFlags level, const gchar *msg, gpointer data)
{
	if (msg && strstr (msg, "Error loading plugin"))
		return;
	g_log_default_handler (domain, level, msg, data);
}

static void
initialize_enchant (void)
{
	GModule *enchant;
	gpointer funcptr;
    gsize i;
    const char * const libnames[] = {
#ifdef G_OS_WIN32
        "libenchant.dll",
#endif
#ifdef G_OS_UNIX
        "libenchant.so.1",
        "libenchant.so.2",
        "libenchant-2.so.2",
#endif
#ifdef __APPLE__
        "libenchant-2.dylib",	/* Homebrew / MacPorts enchant 2 */
        "libenchant.dylib",
#endif
    };

    for (i = 0; i < G_N_ELEMENTS(libnames); ++i)
    {
        enchant = g_module_open(libnames[i], 0);
        if (enchant)
        {
            g_info ("Loaded %s", libnames[i]);
            have_enchant = TRUE;
            break;
        }
    }

  if (!have_enchant)
    return;

	g_log_set_handler ("libenchant", G_LOG_LEVEL_WARNING, enchant_log_cb, NULL);

#define MODULE_SYMBOL(name, func, alt_name) G_STMT_START { \
    const char *funcname = name; \
    gboolean ret = g_module_symbol(enchant, funcname, &funcptr); \
    if (alt_name) { \
        funcname = alt_name; \
        ret = g_module_symbol(enchant, funcname, &funcptr); \
    } \
    if (ret == FALSE) { \
        g_warning ("Failed to find enchant symbol %s", funcname); \
        have_enchant = FALSE; \
        return; \
    } \
    (func) = funcptr; \
} G_STMT_END;

	MODULE_SYMBOL("enchant_broker_init", enchant_broker_init, NULL)
	MODULE_SYMBOL("enchant_broker_free", enchant_broker_free, NULL)
	MODULE_SYMBOL("enchant_broker_free_dict", enchant_broker_free_dict, NULL)
	MODULE_SYMBOL("enchant_broker_list_dicts", enchant_broker_list_dicts, NULL)
	MODULE_SYMBOL("enchant_broker_request_dict", enchant_broker_request_dict, NULL)

	MODULE_SYMBOL("enchant_dict_add_to_personal", enchant_dict_add_to_personal,
                  "enchant_dict_add")
	MODULE_SYMBOL("enchant_dict_add_to_session", enchant_dict_add_to_session, NULL)
	MODULE_SYMBOL("enchant_dict_check", enchant_dict_check, NULL)
	MODULE_SYMBOL("enchant_dict_describe", enchant_dict_describe, NULL)
	MODULE_SYMBOL("enchant_dict_free_suggestions",
				  enchant_dict_free_suggestions, "enchant_dict_free_string_list")
	MODULE_SYMBOL("enchant_dict_store_replacement",
				  enchant_dict_store_replacement, NULL)
	MODULE_SYMBOL("enchant_dict_suggest", enchant_dict_suggest, NULL)
}

static void
sexy_spell_entry_class_init(SexySpellEntryClass *klass)
{
	GObjectClass *gobject_class;
	GObjectClass *object_class;
	GtkWidgetClass *widget_class;

	initialize_enchant();

	parent_class = g_type_class_peek_parent(klass);

	gobject_class = G_OBJECT_CLASS(klass);
	object_class  = G_OBJECT_CLASS(klass);
	widget_class  = GTK_WIDGET_CLASS(klass);

	if (have_enchant)
		klass->word_check = default_word_check;

	gobject_class->finalize = sexy_spell_entry_finalize;

	object_class->dispose = sexy_spell_entry_destroy;

	widget_class->snapshot = sexy_spell_entry_snapshot;

	/**
	 * SexySpellEntry::word-check:
	 * @entry: The entry on which the signal is emitted.
	 * @word: The word to check.
	 *
	 * The ::word-check signal is emitted whenever the entry has to check
	 * a word.  This allows the application to mark words as correct even
	 * if none of the active dictionaries contain it, such as nicknames in
	 * a chat client.
	 *
	 * Returns: %FALSE to indicate that the word should be marked as
	 * correct.
	 */
	signals[WORD_CHECK] = g_signal_new("word_check",
					   G_TYPE_FROM_CLASS(object_class),
					   G_SIGNAL_RUN_LAST,
					   G_STRUCT_OFFSET(SexySpellEntryClass, word_check),
					   (GSignalAccumulator) spell_accumulator, NULL,
					   _hexchat_marshal_BOOLEAN__STRING,
					   G_TYPE_BOOLEAN,
					   1, G_TYPE_STRING);

	if (empty_attrs_list == NULL)
	{
		empty_attrs_list = pango_attr_list_new ();
	}
}

static void
sexy_spell_entry_editable_init (GtkEditableInterface *iface)
{
}


static void
insert_hiddenchar (SexySpellEntry *entry, guint start, guint end)
{
	/* FIXME: Pango does not properly reflect the new widths after a char
	 * is 'hidden' */
#if 0
	PangoAttribute *hattr;
	PangoRectangle *rect = g_new (PangoRectangle, 1);

	rect->x = 0;
	rect->y = 0;
	rect->width = 0;
	rect->height = 0;

	hattr = pango_attr_shape_new (rect, rect);
	hattr->start_index = start;
	hattr->end_index = end;
	pango_attr_list_insert (entry->priv->attr_list, hattr);

	g_free (rect);
#endif
}

static void
insert_underline_error (SexySpellEntry *entry, guint start, guint end)
{
	PangoAttribute *ucolor;
	PangoAttribute *unline;

	ucolor = pango_attr_underline_color_new (C16R (colors[COL_SPELL]), C16G (colors[COL_SPELL]), C16B (colors[COL_SPELL]));
	unline = pango_attr_underline_new (PANGO_UNDERLINE_ERROR);

	ucolor->start_index = start;
	unline->start_index = start;

	ucolor->end_index = end;
	unline->end_index = end;

	pango_attr_list_insert (entry->priv->attr_list, ucolor);
	pango_attr_list_insert (entry->priv->attr_list, unline);
}

static void
insert_underline (SexySpellEntry *entry, guint start, gboolean toggle)
{
	PangoAttribute *uattr;

	uattr = pango_attr_underline_new (toggle ? PANGO_UNDERLINE_NONE : PANGO_UNDERLINE_SINGLE);
	uattr->start_index = start;
	uattr->end_index = PANGO_ATTR_INDEX_TO_TEXT_END;
	pango_attr_list_change (entry->priv->attr_list, uattr);
}

static void
insert_bold (SexySpellEntry *entry, guint start, gboolean toggle)
{
	PangoAttribute *battr;

	battr = pango_attr_weight_new (toggle ? PANGO_WEIGHT_NORMAL : PANGO_WEIGHT_BOLD);
	battr->start_index = start;
	battr->end_index = PANGO_ATTR_INDEX_TO_TEXT_END;
	pango_attr_list_change (entry->priv->attr_list, battr);
}

static void
insert_italic (SexySpellEntry *entry, guint start, gboolean toggle)
{
	PangoAttribute *iattr;

	iattr  = pango_attr_style_new (toggle ? PANGO_STYLE_NORMAL : PANGO_STYLE_ITALIC); 
	iattr->start_index = start;
	iattr->end_index = PANGO_ATTR_INDEX_TO_TEXT_END;
	pango_attr_list_change (entry->priv->attr_list, iattr);
}

static void
insert_strikethrough (SexySpellEntry *entry, guint start, gboolean toggle)
{
	PangoAttribute *sattr;

	sattr  = pango_attr_strikethrough_new (!toggle);
	sattr->start_index = start;
	sattr->end_index = PANGO_ATTR_INDEX_TO_TEXT_END;
	pango_attr_list_change (entry->priv->attr_list, sattr);
}

static void
insert_color (SexySpellEntry *entry, guint start, int fgcolor, int bgcolor)
{
	PangoAttribute *fgattr;
	PangoAttribute *ulattr;
	PangoAttribute *bgattr;

	if (fgcolor < 0 || fgcolor > MAX_COL)
	{
		fgattr = pango_attr_foreground_new (C16R (colors[COL_FG]), C16G (colors[COL_FG]), C16B (colors[COL_FG]));
		ulattr = pango_attr_underline_color_new (C16R (colors[COL_FG]), C16G (colors[COL_FG]), C16B (colors[COL_FG]));
	}
	else
	{
		fgattr = pango_attr_foreground_new (C16R (colors[fgcolor]), C16G (colors[fgcolor]), C16B (colors[fgcolor]));
		ulattr = pango_attr_underline_color_new (C16R (colors[fgcolor]), C16G (colors[fgcolor]), C16B (colors[fgcolor]));
	}

	if (bgcolor < 0 || bgcolor > MAX_COL)
		bgattr = pango_attr_background_new (C16R (colors[COL_BG]), C16G (colors[COL_BG]), C16B (colors[COL_BG]));
	else
		bgattr = pango_attr_background_new (C16R (colors[bgcolor]), C16G (colors[bgcolor]), C16B (colors[bgcolor]));

	fgattr->start_index = start;
	fgattr->end_index = PANGO_ATTR_INDEX_TO_TEXT_END;
	pango_attr_list_change (entry->priv->attr_list, fgattr);
	ulattr->start_index = start;
	ulattr->end_index = PANGO_ATTR_INDEX_TO_TEXT_END;
	pango_attr_list_change (entry->priv->attr_list, ulattr);
	bgattr->start_index = start;
	bgattr->end_index = PANGO_ATTR_INDEX_TO_TEXT_END;
	pango_attr_list_change (entry->priv->attr_list, bgattr);
}

static void
insert_reset (SexySpellEntry *entry, guint start)
{
	insert_bold (entry, start, TRUE);
	insert_underline (entry, start, TRUE);
	insert_italic (entry, start, TRUE);
	insert_strikethrough (entry, start, TRUE);
	insert_color (entry, start, -1, -1);
}

static void
get_word_extents_from_position(SexySpellEntry *entry, gint *start, gint *end, guint position)
{
	const gchar *text;
	gint i, bytes_pos;

	*start = -1;
	*end = -1;

	if (entry->priv->words == NULL)
		return;

	text = nx_entry_get_text (entry);
	bytes_pos = (gint) (g_utf8_offset_to_pointer(text, position) - text);

	for (i = 0; entry->priv->words[i]; i++) {
		if (bytes_pos >= entry->priv->word_starts[i] &&
		    bytes_pos <= entry->priv->word_ends[i]) {
			*start = entry->priv->word_starts[i];
			*end   = entry->priv->word_ends[i];
			return;
		}
	}
}

static void
add_to_dictionary(nx_item *menuitem, gpointer data)
{
	SexySpellEntry *entry = data;
	char *word;
	gint start, end;
	struct EnchantDict *dict;

	if (!have_enchant)
		return;

	get_word_extents_from_position(entry, &start, &end, entry->priv->mark_character);
	word = gtk_editable_get_chars(GTK_EDITABLE(entry), start, end);

	dict = (struct EnchantDict *) nx_item_get_data(menuitem, "enchant-dict");
	if (dict)
		enchant_dict_add_to_personal(dict, word, -1);

	g_free(word);

	if (entry->priv->words) {
		g_strfreev(entry->priv->words);
		g_free(entry->priv->word_starts);
		g_free(entry->priv->word_ends);
	}
	entry_strsplit_utf8(GTK_ENTRY(entry), &entry->priv->words, &entry->priv->word_starts, &entry->priv->word_ends);
	sexy_spell_entry_recheck_all (entry);
}

static void
ignore_all(nx_item *menuitem, gpointer data)
{
	SexySpellEntry *entry = data;
	char *word;
	gint start, end;
	GSList *li;

	if (!have_enchant)
		return;

	get_word_extents_from_position(entry, &start, &end, entry->priv->mark_character);
	word = gtk_editable_get_chars(GTK_EDITABLE(entry), start, end);

	for (li = entry->priv->dict_list; li; li = g_slist_next (li)) {
		struct EnchantDict *dict = (struct EnchantDict *) li->data;
		enchant_dict_add_to_session(dict, word, -1);
	}

	g_free(word);

	if (entry->priv->words) {
		g_strfreev(entry->priv->words);
		g_free(entry->priv->word_starts);
		g_free(entry->priv->word_ends);
	}
	entry_strsplit_utf8(GTK_ENTRY(entry), &entry->priv->words, &entry->priv->word_starts, &entry->priv->word_ends);
	sexy_spell_entry_recheck_all(entry);
}

static void
replace_word(nx_item *menuitem, gpointer data)
{
	SexySpellEntry *entry = data;
	char *oldword;
	const char *newword;
	gint start, end;
	gint cursor;
	struct EnchantDict *dict;

	if (!have_enchant)
		return;

	get_word_extents_from_position(entry, &start, &end, entry->priv->mark_character);
	oldword = gtk_editable_get_chars(GTK_EDITABLE(entry), start, end);
	newword = nx_item_get_text (menuitem);

	cursor = gtk_editable_get_position(GTK_EDITABLE(entry));
	/* is the cursor at the end? If so, restore it there */
	if (g_utf8_strlen(nx_entry_get_text (entry), -1) == cursor)
		cursor = -1;
	else if(cursor > start && cursor <= end)
		cursor = start;

	gtk_editable_delete_text(GTK_EDITABLE(entry), start, end);
	gtk_editable_set_position(GTK_EDITABLE(entry), start);
	gtk_editable_insert_text(GTK_EDITABLE(entry), newword, strlen(newword),
							 &start);
	gtk_editable_set_position(GTK_EDITABLE(entry), cursor);

	dict = (struct EnchantDict *) nx_item_get_data(menuitem, "enchant-dict");

        if (dict)
		enchant_dict_store_replacement(dict,
					       oldword, -1,
					       newword, -1);

	g_free(oldword);
}

static void
build_suggestion_menu(SexySpellEntry *entry, nx_menu *menu, struct EnchantDict *dict, const gchar *word)
{
	nx_item *mi;
	gchar **suggestions;
	size_t n_suggestions, i;

	if (!have_enchant)
		return;

	suggestions = enchant_dict_suggest(dict, word, -1, &n_suggestions);

	if (suggestions == NULL || n_suggestions == 0) {
		/* no suggestions.  put something in the menu anyway (shaded, as the
		   old label-in-a-separator was) */
		nx_menu_add_item (menu, 0, _("<i>(no suggestions)</i>"),
								NX_ITEM_MARKUP | NX_ITEM_INSENSITIVE, NULL, NULL, NULL);
	} else {
		/* build a set of menus with suggestions */
		for (i = 0; i < n_suggestions; i++) {
			if ((i != 0) && (i % 10 == 0)) {
				nx_menu_add_separator (menu, NX_MENU_APPEND);
				menu = nx_menu_add_submenu (menu, NX_MENU_APPEND, _("More..."), 0, NULL);
			}

			mi = nx_menu_add_item (menu, NX_MENU_APPEND, suggestions[i], 0, NULL,
										  replace_word, entry);
			nx_item_set_data (mi, "enchant-dict", dict);
		}
	}

	enchant_dict_free_suggestions(dict, suggestions);
}

/* the "Spelling Suggestions" submenu's contents, into topmenu */
static void
build_spelling_menu(SexySpellEntry *entry, nx_menu *topmenu, const gchar *word)
{
	struct EnchantDict *dict;
	nx_item *mi;
	gchar *label;

	if (!have_enchant)
		return;

	if (entry->priv->dict_list == NULL)
		return;

	/* Suggestions */
	if (g_slist_length(entry->priv->dict_list) == 1) {
		dict = (struct EnchantDict *) entry->priv->dict_list->data;
		build_suggestion_menu(entry, topmenu, dict, word);
	} else {
		GSList *li;
		nx_menu *menu;
		gchar *lang, *lang_name;

		for (li = entry->priv->dict_list; li; li = g_slist_next (li)) {
			dict = (struct EnchantDict *) li->data;
			lang = get_lang_from_dict(dict);
			lang_name = sexy_spell_entry_get_language_name (entry, lang);
			menu = nx_menu_add_submenu (topmenu, NX_MENU_APPEND, lang_name ? lang_name : lang, 0, NULL);
			g_free (lang_name);
			g_free(lang);
			build_suggestion_menu(entry, menu, dict, word);
		}
	}

	/* Separator */
	nx_menu_add_separator (topmenu, NX_MENU_APPEND);

	/* + Add to Dictionary (an icon item; its label is plain, not a mnemonic:
	   the word may contain underscores) */
	label = g_strdup_printf(_("Add \"%s\" to Dictionary"), word);

	if (g_slist_length(entry->priv->dict_list) == 1) {
		dict = (struct EnchantDict *) entry->priv->dict_list->data;
		mi = nx_menu_add_item (topmenu, NX_MENU_APPEND, label, NX_ITEM_PLAIN_ICON, SEXY_ICON_ADD,
									  add_to_dictionary, entry);
		nx_item_set_data (mi, "enchant-dict", dict);
	} else {
		GSList *li;
		nx_menu *menu;
		nx_item *submi;
		gchar *lang, *lang_name;

		menu = nx_menu_add_submenu_icon_plain (topmenu, NX_MENU_APPEND, label, SEXY_ICON_ADD);

		for (li = entry->priv->dict_list; li; li = g_slist_next(li)) {
			dict = (struct EnchantDict *)li->data;
			lang = get_lang_from_dict(dict);
			lang_name = sexy_spell_entry_get_language_name (entry, lang);
			submi = nx_menu_add_item (menu, NX_MENU_APPEND, lang_name ? lang_name : lang, 0, NULL,
											  add_to_dictionary, entry);
			g_free (lang_name);
			g_free(lang);
			nx_item_set_data (submi, "enchant-dict", dict);
		}
	}
	g_free(label);

	/* - Ignore All */
	nx_menu_add_item (topmenu, NX_MENU_APPEND, _("Ignore All"), NX_ITEM_PLAIN_ICON, SEXY_ICON_REMOVE,
							ignore_all, entry);
}

/* the entry's right-click menu: when the word under the click is
   misspelled, "Spelling Suggestions" and a separator go on top */
static void
sexy_spell_entry_add_spelling (SexySpellEntry *entry, nx_menu *menu)
{
	nx_menu *sub;
	gint start, end;
	gchar *word;

	if ((have_enchant == FALSE) || (entry->priv->checked == FALSE))
		return;

	if (g_slist_length(entry->priv->dict_list) == 0)
		return;

	get_word_extents_from_position(entry, &start, &end, entry->priv->mark_character);
	if (start == end)
		return;
	if (!word_misspelled(entry, start, end))
		return;

	/* separator, then above it the suggestions menu */
	nx_menu_add_separator (menu, 0);
	sub = nx_menu_add_submenu_icon_plain (menu, 0, _("Spelling Suggestions"), SEXY_ICON_SPELL_CHECK);

	word = gtk_editable_get_chars(GTK_EDITABLE(entry), start, end);
	g_assert(word != NULL);
	build_spelling_menu (entry, sub, word);
	g_free(word);
}

/* GTK4: GtkText's own context menu can only take a GMenuModel appended at
   the end, so the whole menu is ours, laid out as GTK3's was: spelling,
   Cut/Copy/Paste/Delete, Select All, Insert Emoji, then the hook's items.
   The labels are GtkEntry's own, from GTK's gtk30 catalogue (GTK4 ships
   gtk40, which the user may not have) */

/* the character position whose cursor is nearest to x (GtkText
   coordinates, scrolling included): cursor x grows with the position in
   left-to-right text, so a binary search over the positions */
static int
nx_text_position_at (GtkText *text, double x)
{
	int lo = 0, hi = g_utf8_strlen (gtk_editable_get_text (GTK_EDITABLE (text)), -1);
	graphene_rect_t strong;

	while (lo < hi)
	{
		int mid = (lo + hi) / 2;
		double mx, nx;

		gtk_text_compute_cursor_extents (text, mid, &strong, NULL);
		mx = strong.origin.x;
		gtk_text_compute_cursor_extents (text, mid + 1, &strong, NULL);
		nx = strong.origin.x;
		if (x < (mx + nx) / 2)
			hi = mid;
		else
			lo = mid + 1;
	}
	return lo;
}

static void
menu_text_action (nx_item *item, gpointer name)
{
	GtkWidget *w = nx_item_get_widget (item);
	if (w)
		gtk_widget_activate_action (w, name, NULL);
}

static void
sexy_spell_entry_menu_popup (SexySpellEntry *entry, double x, double y)
{
	nx_menu *menu = nx_menu_new ();
	GtkEditable *ed = GTK_EDITABLE (entry);
	gboolean has_sel = gtk_editable_get_selection_bounds (ed, NULL, NULL);
	gboolean editable = gtk_editable_get_editable (ed);

	sexy_spell_entry_add_spelling (entry, menu);
	nx_menu_add_item (menu, NX_MENU_APPEND, g_dgettext ("gtk30", "Cu_t"), NX_ITEM_MNEMONIC |
							(has_sel && editable ? 0 : NX_ITEM_INSENSITIVE), NULL, menu_text_action, "clipboard.cut");
	nx_menu_add_item (menu, NX_MENU_APPEND, g_dgettext ("gtk30", "_Copy"), NX_ITEM_MNEMONIC |
							(has_sel ? 0 : NX_ITEM_INSENSITIVE), NULL, menu_text_action, "clipboard.copy");
	nx_menu_add_item (menu, NX_MENU_APPEND, g_dgettext ("gtk30", "_Paste"), NX_ITEM_MNEMONIC |
							(editable ? 0 : NX_ITEM_INSENSITIVE), NULL, menu_text_action, "clipboard.paste");
	nx_menu_add_item (menu, NX_MENU_APPEND, g_dgettext ("gtk30", "_Delete"), NX_ITEM_MNEMONIC |
							(has_sel && editable ? 0 : NX_ITEM_INSENSITIVE), NULL, menu_text_action, "selection.delete");
	nx_menu_add_separator (menu, NX_MENU_APPEND);
	nx_menu_add_item (menu, NX_MENU_APPEND, g_dgettext ("gtk30", "Select _All"), NX_ITEM_MNEMONIC, NULL,
							menu_text_action, "selection.select-all");
	nx_menu_add_item (menu, NX_MENU_APPEND, g_dgettext ("gtk30", "Insert _Emoji"), NX_ITEM_MNEMONIC, NULL,
							menu_text_action, "misc.insert-emoji");
	if (entry->priv->menu_hook)
		entry->priv->menu_hook (entry, menu, entry->priv->menu_hook_data);
	nx_menu_popup (menu, GTK_WIDGET (entry), nx_current_event (), NULL, NULL);
}

static gboolean
sexy_spell_entry_click (GtkWidget *w, guint button, int n_press, double x, double y,
								GdkModifierType state, gpointer unused)
{
	SexySpellEntry *entry = SEXY_SPELL_ENTRY (w);
	GtkEditable *text = gtk_editable_get_delegate (GTK_EDITABLE (w));
	int pos = -1;

	if (button != 3)
		return FALSE;
	/* the character under the click marks the word (GTK3's
	   gtk_entry_find_position): GtkText puts the cursor there on press */
	if (GTK_IS_TEXT (text))
	{
		graphene_point_t in = GRAPHENE_POINT_INIT ((float) x, (float) y), out;
		if (gtk_widget_compute_point (w, GTK_WIDGET (text), &in, &out))
			pos = nx_text_position_at (GTK_TEXT (text), out.x);
	}
	entry->priv->mark_character = pos >= 0 ? pos : gtk_editable_get_position (GTK_EDITABLE (w));
	sexy_spell_entry_menu_popup (entry, x, y);
	return TRUE;
}

static void
sexy_spell_entry_menu_init (SexySpellEntry *entry)
{
	nx_on_click (GTK_WIDGET (entry), sexy_spell_entry_click, NULL);
}

void
sexy_spell_entry_set_menu_hook (SexySpellEntry *entry, SexySpellEntryMenuHook hook, gpointer data)
{
	entry->priv->menu_hook = hook;
	entry->priv->menu_hook_data = data;
}

static void
sexy_spell_entry_init(SexySpellEntry *entry)
{
	entry->priv = g_new0(SexySpellEntryPriv, 1);

	entry->priv->dict_hash = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);

	if (have_enchant)
	{
#ifdef HAVE_ISO_CODES
		if (codetable_ref == 0)
			codetable_init ();
		codetable_ref++;
#endif
		sexy_spell_entry_activate_default_languages(entry);
	}

	entry->priv->attr_list = pango_attr_list_new();

	entry->priv->checked = TRUE;
	entry->priv->parseattr = TRUE;
	entry->priv->cursor_blink = TRUE;

	sexy_spell_entry_menu_init (entry);
	g_signal_connect(G_OBJECT(entry), "changed", G_CALLBACK(sexy_spell_entry_changed), NULL);
	g_signal_connect (entry, "notify::cursor-position", G_CALLBACK (cursor_notify_cb), entry);
	{
		GtkEventController *fc = gtk_event_controller_focus_new ();
		g_signal_connect (fc, "enter", G_CALLBACK (cursor_focus_cb), entry);
		gtk_widget_add_controller (GTK_WIDGET (entry), fc);
	}
}

static void
sexy_spell_entry_finalize(GObject *obj)
{
	SexySpellEntry *entry;

	g_return_if_fail(obj != NULL);
	g_return_if_fail(SEXY_IS_SPELL_ENTRY(obj));

	entry = SEXY_SPELL_ENTRY(obj);

	if (entry->priv->attr_list)
		pango_attr_list_unref(entry->priv->attr_list);
	if (entry->priv->dict_hash)
		g_hash_table_destroy(entry->priv->dict_hash);
	g_strfreev(entry->priv->words);
	g_free(entry->priv->word_starts);
	g_free(entry->priv->word_ends);

	if (have_enchant) {
		if (entry->priv->broker) {
			GSList *li;
			for (li = entry->priv->dict_list; li; li = g_slist_next(li)) {
				struct EnchantDict *dict = (struct EnchantDict*) li->data;
				enchant_broker_free_dict (entry->priv->broker, dict);
			}
			g_slist_free (entry->priv->dict_list);

			enchant_broker_free(entry->priv->broker);
		}
	}

	g_free(entry->priv);
#ifdef HAVE_ISO_CODES
	codetable_ref--;
	if (codetable_ref == 0)
		codetable_free ();
#endif

	if (G_OBJECT_CLASS(parent_class)->finalize)
		G_OBJECT_CLASS(parent_class)->finalize(obj);
}

static void
sexy_spell_entry_destroy(GObject *obj)
{
	SexySpellEntry *entry = SEXY_SPELL_ENTRY (obj);

	if (entry->priv && entry->priv->blink_id)
	{
		g_source_remove (entry->priv->blink_id);
		entry->priv->blink_id = 0;
	}
	if (G_OBJECT_CLASS(parent_class)->dispose)
		G_OBJECT_CLASS(parent_class)->dispose(obj);
}

/* the cursor: GTK's own unless another style or no blinking is picked;
   then GtkText's caret is made transparent and ours is drawn on top */

static GtkWidget *
cursor_text (SexySpellEntry *entry)
{
	GtkEditable *d = gtk_editable_get_delegate (GTK_EDITABLE (entry));
	return GTK_IS_TEXT (d) ? GTK_WIDGET (d) : NULL;
}

static gboolean
cursor_own (SexySpellEntry *entry)
{
	return entry->priv->cursor_style != 0 || !entry->priv->cursor_blink;
}

static gboolean
cursor_blink_cb (gpointer data)
{
	SexySpellEntry *entry = data;
	SexySpellEntryPriv *p = entry->priv;
	GtkWidget *text = cursor_text (entry);
	int timeout = 10;

	g_object_get (gtk_widget_get_settings (GTK_WIDGET (entry)), "gtk-cursor-blink-timeout", &timeout, NULL);
	/* like GTK: stop blinking (cursor on) when not focused or idle a while */
	if (!text || !gtk_widget_has_focus (text) ||
		 g_get_monotonic_time () - p->blink_start > (gint64) timeout * G_USEC_PER_SEC)
	{
		p->cursor_off = FALSE;
		p->blink_id = 0;
		gtk_widget_queue_draw (GTK_WIDGET (entry));
		return G_SOURCE_REMOVE;
	}
	p->cursor_off = !p->cursor_off;
	gtk_widget_queue_draw (GTK_WIDGET (entry));
	return G_SOURCE_CONTINUE;
}

/* typing or moving shows the cursor and starts the blinking over */
static void
cursor_wake (SexySpellEntry *entry)
{
	SexySpellEntryPriv *p = entry->priv;
	gboolean blink = TRUE;
	int time = 1200;

	if (!cursor_own (entry))
		return;
	p->cursor_off = FALSE;
	p->blink_start = g_get_monotonic_time ();
	g_object_get (gtk_widget_get_settings (GTK_WIDGET (entry)),
					  "gtk-cursor-blink", &blink, "gtk-cursor-blink-time", &time, NULL);
	if (p->cursor_blink && blink && !p->blink_id)
		p->blink_id = g_timeout_add (MAX (time / 2, 50), cursor_blink_cb, entry);
	gtk_widget_queue_draw (GTK_WIDGET (entry));
}

static void
cursor_notify_cb (GObject *o, GParamSpec *ps, gpointer entry)
{
	cursor_wake (entry);
}

static void
cursor_focus_cb (GtkEventControllerFocus *c, gpointer entry)
{
	cursor_wake (entry);
}

static void
sexy_spell_entry_snapshot (GtkWidget *widget, GtkSnapshot *snapshot)
{
	SexySpellEntry *entry = SEXY_SPELL_ENTRY (widget);
	SexySpellEntryPriv *p = entry->priv;
	GtkWidget *text;
	graphene_rect_t cur, next, clip;
	graphene_point_t at;
	GdkRGBA fg;
	const char *str;
	int pos, len, start, end;
	float w, h;

	GTK_WIDGET_CLASS (parent_class)->snapshot (widget, snapshot);

	text = cursor_text (entry);
	if (!cursor_own (entry) || p->cursor_off || !text || !gtk_widget_has_focus (text) ||
		 gtk_editable_get_selection_bounds (GTK_EDITABLE (entry), &start, &end))
		return;

	str = gtk_editable_get_text (GTK_EDITABLE (entry));
	len = g_utf8_strlen (str, -1);
	pos = gtk_editable_get_position (GTK_EDITABLE (entry));
	gtk_text_compute_cursor_extents (GTK_TEXT (text), pos, &cur, NULL);
	h = cur.size.height;

	/* the character under the cursor; past the end, a digit's width */
	w = 0;
	if (pos < len)
	{
		gtk_text_compute_cursor_extents (GTK_TEXT (text), pos + 1, &next, NULL);
		w = next.origin.x - cur.origin.x;
	}
	if (w <= 0)
	{
		PangoLayout *l = gtk_widget_create_pango_layout (text, "0");
		int lw;
		pango_layout_get_pixel_size (l, &lw, NULL);
		g_object_unref (l);
		w = lw;
	}

	if (!gtk_widget_compute_point (text, widget, &GRAPHENE_POINT_INIT (cur.origin.x, cur.origin.y), &at) ||
		 !gtk_widget_compute_bounds (text, widget, &clip))
		return;
	gtk_widget_get_color (text, &fg);

	gtk_snapshot_push_clip (snapshot, &clip);
	switch (p->cursor_style)
	{
	case 1:	/* thick bar */
		gtk_snapshot_append_color (snapshot, &fg, &GRAPHENE_RECT_INIT (at.x, at.y, MAX (2, h / 8), h));
		break;
	case 2:	/* block, the character drawn over it in the background colour */
	{
		GdkRGBA bg = colors[COL_BG];
		if (!prefs.hex_gui_input_style)
			bg = (0.299 * fg.red + 0.587 * fg.green + 0.114 * fg.blue) < 0.5 ?
				(GdkRGBA) { 1, 1, 1, 1 } : (GdkRGBA) { 0, 0, 0, 1 };
		gtk_snapshot_append_color (snapshot, &fg, &GRAPHENE_RECT_INIT (at.x, at.y, w, h));
		if (pos < len)
		{
			const char *c = g_utf8_offset_to_pointer (str, pos);
			char *ch = g_strndup (c, g_utf8_next_char (c) - c);
			PangoLayout *l = gtk_widget_create_pango_layout (text, ch);
			gtk_snapshot_save (snapshot);
			gtk_snapshot_translate (snapshot, &at);
			gtk_snapshot_append_layout (snapshot, l, &bg);
			gtk_snapshot_restore (snapshot);
			g_object_unref (l);
			g_free (ch);
		}
		break;
	}
	case 3:	/* underline */
		gtk_snapshot_append_color (snapshot, &fg, &GRAPHENE_RECT_INIT (at.x, at.y + h - 2, w, 2));
		break;
	case 4:	/* hollow box */
	{
		GskRoundedRect r;
		float bw[4] = { 1, 1, 1, 1 };
		GdkRGBA bc[4] = { fg, fg, fg, fg };
		gsk_rounded_rect_init_from_rect (&r, &GRAPHENE_RECT_INIT (at.x, at.y, w, h), 0);
		gtk_snapshot_append_border (snapshot, &r, bw, bc);
		break;
	}
	default:	/* GTK's thin line, just not blinking */
		gtk_snapshot_append_color (snapshot, &fg, &GRAPHENE_RECT_INIT (at.x, at.y, MAX (1, h * 0.04f), h));
		break;
	}
	gtk_snapshot_pop (snapshot);
}

void
sexy_spell_entry_set_cursor (SexySpellEntry *entry, int style, gboolean blink)
{
	static GtkCssProvider *prov;
	SexySpellEntryPriv *p = entry->priv;

	if (!prov)
	{
		prov = gtk_css_provider_new ();
		gtk_css_provider_load_from_string (prov, "entry.nx-own-cursor > text { caret-color: transparent; }\n");
		gtk_style_context_add_provider_for_display (gtk_widget_get_display (GTK_WIDGET (entry)),
				GTK_STYLE_PROVIDER (prov), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 1);
	}
	p->cursor_style = CLAMP (style, 0, 4);
	p->cursor_blink = blink;
	if (cursor_own (entry))
		gtk_widget_add_css_class (GTK_WIDGET (entry), "nx-own-cursor");
	else
		gtk_widget_remove_css_class (GTK_WIDGET (entry), "nx-own-cursor");
	if (p->blink_id && !blink)
	{
		g_source_remove (p->blink_id);
		p->blink_id = 0;
	}
	cursor_wake (entry);
	gtk_widget_queue_draw (GTK_WIDGET (entry));
}

/**
 * sexy_spell_entry_new
 *
 * Creates a new SexySpellEntry widget.
 *
 * Returns: a new #SexySpellEntry.
 */
GtkWidget *
sexy_spell_entry_new(void)
{
	return GTK_WIDGET(g_object_new(SEXY_TYPE_SPELL_ENTRY, NULL));
}

GQuark
sexy_spell_error_quark(void)
{
	static GQuark q = 0;
	if (q == 0)
		q = g_quark_from_static_string("sexy-spell-error-quark");
	return q;
}

static gboolean
default_word_check(SexySpellEntry *entry, const gchar *word)
{
	gboolean result = TRUE;
	GSList *li;

	if (!have_enchant)
		return result;

	if (g_unichar_isalpha(*word) == FALSE) {
		/* We only want to check words */
		return FALSE;
	}
	for (li = entry->priv->dict_list; li; li = g_slist_next (li)) {
		struct EnchantDict *dict = (struct EnchantDict *) li->data;
		if (enchant_dict_check(dict, word, strlen(word)) == 0) {
			result = FALSE;
			break;
		}
	}
	return result;
}

static gboolean
word_misspelled(SexySpellEntry *entry, int start, int end)
{
	const gchar *text;
	gchar *word;
	gboolean ret;

	if (start == end)
		return FALSE;
	text = nx_entry_get_text (entry);
	word = g_new0(gchar, end - start + 2);

	g_strlcpy(word, text + start, end - start + 1);

	g_signal_emit(entry, signals[WORD_CHECK], 0, word, &ret);

	g_free(word);
	return ret;
}

static void
check_word(SexySpellEntry *entry, int start, int end)
{
	PangoAttrIterator *it;

	/* Check to see if we've got any attributes at this position.
	 * If so, free them, since we'll readd it if the word is misspelled */
	it = pango_attr_list_get_iterator(entry->priv->attr_list);
	if (it == NULL)
		return;
	do {
		gint s, e;
		pango_attr_iterator_range(it, &s, &e);
		if (s == start) {
			GSList *attrs = pango_attr_iterator_get_attrs(it);
			g_slist_foreach(attrs, (GFunc) pango_attribute_destroy, NULL);
			g_slist_free(attrs);
		}
	} while (pango_attr_iterator_next(it));
	pango_attr_iterator_destroy(it);

	if (word_misspelled(entry, start, end))
		insert_underline_error(entry, start, end);
}

static void
check_attributes (SexySpellEntry *entry, const char *text, int len)
{
	gboolean bold = FALSE;
	gboolean italic = FALSE;
	gboolean underline = FALSE;
	gboolean strikethrough = FALSE;
	int parsing_color = 0;
	char fg_color[3];
	char bg_color[3];
	int i, offset = 0;

	memset (bg_color, 0, sizeof(bg_color));
	memset (fg_color, 0, sizeof(fg_color));

	for (i = 0; i < len; i++)
	{
		switch (text[i])
		{
		case ATTR_BOLD:
			insert_hiddenchar (entry, i, i + 1);
			insert_bold (entry, i, bold);
			bold = !bold;
			goto check_color;

		case ATTR_ITALICS:
			insert_hiddenchar (entry, i, i + 1);
			insert_italic (entry, i, italic);
			italic = !italic;
			goto check_color;

		case ATTR_STRIKETHROUGH:
			insert_hiddenchar (entry, i, i + 1);
			insert_strikethrough (entry, i, strikethrough);
			strikethrough = !strikethrough;
			goto check_color;

		case ATTR_UNDERLINE:
			insert_hiddenchar (entry, i, i + 1);
			insert_underline (entry, i, underline);
			underline = !underline;
			goto check_color;

		case ATTR_RESET:
			insert_hiddenchar (entry, i, i + 1);
			insert_reset (entry, i);
			bold = FALSE;
			italic = FALSE;
			underline = FALSE;
			strikethrough = FALSE;
			goto check_color;

		case ATTR_HIDDEN:
			insert_hiddenchar (entry, i, i + 1);
			goto check_color;

		case ATTR_REVERSE:
			insert_hiddenchar (entry, i, i + 1);
			insert_color (entry, i, COL_BG, COL_FG);
			goto check_color;

		case '\n':
			insert_reset (entry, i);
			parsing_color = 0;
			break;

		case ATTR_COLOR:
			parsing_color = 1;
			offset = 1;
			break;

		default:
check_color:
			if (!parsing_color)
				continue;

			if (!g_unichar_isdigit (text[i]))
			{
				if (text[i] == ',' && parsing_color <= 3)
				{
					parsing_color = 3;
					offset++;
					continue;
				}
				else
					parsing_color = 5;
			}

			/* don't parse background color without a comma */
			else if (parsing_color == 3 && text[i - 1] != ',')
				parsing_color = 5;

			switch (parsing_color)
			{
			case 1:
				fg_color[0] = text[i];
				parsing_color++;
				offset++;
				continue;
			case 2:
				fg_color[1] = text[i];
				parsing_color++;
				offset++;
				continue;
			case 3:
				bg_color[0] = text[i];
				parsing_color++;
				offset++;
				continue;
			case 4:
				bg_color[1] = text[i];
				parsing_color++;
				offset++;
				continue;
			case 5:
				if (bg_color[0] != 0)
				{
					insert_hiddenchar (entry, i - offset, i);
					insert_color (entry, i, atoi (fg_color), atoi (bg_color));
				}
				else if (fg_color[0] != 0)
				{
					insert_hiddenchar (entry, i - offset, i);
					insert_color (entry, i, atoi (fg_color), -1);
				}
				else
				{
					/* No colors but some commas may have been added */
					insert_hiddenchar (entry, i - offset, i - offset + 1);
					insert_color (entry, i, -1, -1);
				}

				memset (bg_color, 0, sizeof(bg_color));
				memset (fg_color, 0, sizeof(fg_color));
				parsing_color = 0;
				offset = 0;
				continue;
			}
		}
	}
}

static void
sexy_spell_entry_recheck_all(SexySpellEntry *entry)
{
	GdkRectangle rect;
	GtkAllocation allocation;
	GtkWidget *widget = GTK_WIDGET(entry);
	PangoLayout *layout;
	int length, i, text_len;
	const char *text;

	/* Remove all existing pango attributes.  These will get readded as we check */
	pango_attr_list_unref(entry->priv->attr_list);
	entry->priv->attr_list = pango_attr_list_new();

	if (entry->priv->parseattr)
	{
		/* Check for attributes */
		text = nx_entry_get_text (entry);
		text_len = strlen (text);
		check_attributes (entry, text, text_len);
	}

	if (have_enchant && entry->priv->checked
		&& g_slist_length (entry->priv->dict_list) != 0)
	{
		/* Loop through words */
		for (i = 0; entry->priv->words[i]; i++)
		{
			length = strlen (entry->priv->words[i]);
			if (length == 0)
				continue;
			check_word (entry, entry->priv->word_starts[i], entry->priv->word_ends[i]);
		}
	}

	/* GtkText applies them (and leaves them off while there's IM preedit) */
	gtk_entry_set_attributes (GTK_ENTRY (entry), entry->priv->attr_list);
	(void) layout; (void) allocation; (void) rect; (void) widget;
}


static void
entry_strsplit_utf8(GtkEntry *entry, gchar ***set, gint **starts, gint **ends)
{
	PangoLayout   *layout;
	const PangoLogAttr  *log_attrs;
	const gchar   *text;
	gint           n_attrs, n_strings, i, j;
	PangoLogAttr a;

	/* the word breaks of the text alone (GtkText's own layout is private) */
	PangoContext *ctx = gtk_widget_get_pango_context (GTK_WIDGET (entry));
	layout = pango_layout_new (ctx);
	text = nx_entry_get_text (entry);
	pango_layout_set_text (layout, text, -1);
	log_attrs = pango_layout_get_log_attrs_readonly (layout, &n_attrs);

	/* Find how many words we have */
	for (i = 0, n_strings = 0; i < n_attrs; i++)
	{
		a = log_attrs[i];
		if (a.is_word_start && a.is_word_boundary)
			n_strings++;
	}

	*set    = g_new0(gchar *, n_strings + 1);
	*starts = g_new0(gint, n_strings);
	*ends   = g_new0(gint, n_strings);

	/* Copy out strings */
	for (i = 0, j = 0; i < n_attrs; i++)
	{
		a = log_attrs[i];
		if (a.is_word_start && a.is_word_boundary)
		{
			gint cend, bytes;
			gchar *start;

			/* Find the end of this string */
			for (cend = i; cend < n_attrs; cend++)
			{
				a = log_attrs[cend];
				if (a.is_word_end && a.is_word_boundary)
					break;
			}

			/* Copy sub-string */
			start = g_utf8_offset_to_pointer(text, i);
			bytes = (gint) (g_utf8_offset_to_pointer(text, cend) - start);
			(*set)[j]    = g_new0(gchar, bytes + 1);
			(*starts)[j] = (gint) (start - text);
			(*ends)[j]   = (gint) (start - text + bytes);
			g_utf8_strncpy((*set)[j], start, cend - i);

			/* Move on to the next word */
			j++;
		}
	}
	g_object_unref (layout);
}

static void
sexy_spell_entry_changed(GtkEditable *editable, gpointer data)
{
	SexySpellEntry *entry = SEXY_SPELL_ENTRY(editable);

	if (entry->priv->words)
	{
		g_strfreev(entry->priv->words);
		g_free(entry->priv->word_starts);
		g_free(entry->priv->word_ends);
	}
	entry_strsplit_utf8(GTK_ENTRY(entry), &entry->priv->words, &entry->priv->word_starts, &entry->priv->word_ends);
	sexy_spell_entry_recheck_all(entry);
}

static gboolean
enchant_has_lang(const gchar *lang, GSList *langs) {
	GSList *i;
	for (i = langs; i; i = g_slist_next(i))
	{
		if (strcmp(lang, i->data) == 0)
		{
			return TRUE;
		}
	}
	return FALSE;
}

/**
 * sexy_spell_entry_activate_default_languages:
 * @entry: A #SexySpellEntry.
 *
 * Activate spell checking for languages specified in the 
 * text_spell_langs setting. These languages are
 * activated by default, so this function need only be called
 * if they were previously deactivated.
 */
void
sexy_spell_entry_activate_default_languages(SexySpellEntry *entry)
{
	GSList *enchant_langs;
	char *lang, **i, **langs;

	if (!have_enchant)
		return;

	if (!entry->priv->broker)
		entry->priv->broker = enchant_broker_init();

	enchant_langs = sexy_spell_entry_get_languages(entry);

	langs = g_strsplit_set (prefs.hex_text_spell_langs, ", \t", 0);

	for (i = langs; *i; i++)
	{
		lang = *i;

		if (enchant_has_lang (lang, enchant_langs))
		{
			sexy_spell_entry_activate_language_internal (entry, lang, NULL);
		}
	}

	g_slist_foreach(enchant_langs, (GFunc) g_free, NULL);
	g_slist_free(enchant_langs);
	g_strfreev (langs);

	/* If we don't have any languages activated, use "en" */
	if (entry->priv->dict_list == NULL)
		sexy_spell_entry_activate_language_internal(entry, "en", NULL);

	sexy_spell_entry_recheck_all (entry);
}

static void
get_lang_from_dict_cb(const char * const lang_tag,
		      const char * const provider_name,
		      const char * const provider_desc,
		      const char * const provider_file,
		      void * user_data) {
	gchar **lang = (gchar **)user_data;
	*lang = g_strdup(lang_tag);
}

static gchar *
get_lang_from_dict(struct EnchantDict *dict)
{
	gchar *lang;

	if (!have_enchant)
		return NULL;

	enchant_dict_describe(dict, get_lang_from_dict_cb, &lang);
	return lang;
}

static gboolean
sexy_spell_entry_activate_language_internal(SexySpellEntry *entry, const gchar *lang, GError **error)
{
	struct EnchantDict *dict;

	if (!have_enchant)
		return FALSE;

	if (!entry->priv->broker)
		entry->priv->broker = enchant_broker_init();

	if (g_hash_table_lookup(entry->priv->dict_hash, lang))
		return TRUE;

	dict = enchant_broker_request_dict(entry->priv->broker, lang);

	if (!dict) {
		g_set_error(error, SEXY_SPELL_ERROR, SEXY_SPELL_ERROR_BACKEND, _("enchant error for language: %s"), lang);
		return FALSE;
	}

	enchant_dict_add_to_session (dict, "HexChat", strlen("HexChat"));
	enchant_dict_add_to_session (dict, "nexchat", strlen("nexchat"));
	entry->priv->dict_list = g_slist_append(entry->priv->dict_list, (gpointer) dict);
	g_hash_table_insert(entry->priv->dict_hash, get_lang_from_dict(dict), (gpointer) dict);

	return TRUE;
}

static void
dict_describe_cb(const char * const lang_tag,
		 const char * const provider_name,
		 const char * const provider_desc,
		 const char * const provider_file,
		 void * user_data)
{
	GSList **langs = (GSList **)user_data;

	*langs = g_slist_append(*langs, (gpointer)g_strdup(lang_tag));
}

/**
 * sexy_spell_entry_get_languages:
 * @entry: A #SexySpellEntry.
 *
 * Retrieve a list of language codes for which dictionaries are available.
 *
 * Returns: a new #GList object, or %NULL on error.
 */
GSList *
sexy_spell_entry_get_languages(const SexySpellEntry *entry)
{
	GSList *langs = NULL;

	g_return_val_if_fail(entry != NULL, NULL);
	g_return_val_if_fail(SEXY_IS_SPELL_ENTRY(entry), NULL);

	if (enchant_broker_list_dicts == NULL)
		return NULL;

	if (!entry->priv->broker)
		return NULL;

	enchant_broker_list_dicts(entry->priv->broker, dict_describe_cb, &langs);

	return langs;
}

/**
 * sexy_spell_entry_get_language_name:
 * @entry: A #SexySpellEntry.
 * @lang: The language code to lookup a friendly name for.
 *
 * Get a friendly name for a given locale.
 *
 * Returns: The name of the locale. Should be freed with g_free()
 */
gchar *
sexy_spell_entry_get_language_name(const SexySpellEntry *entry,
								   const gchar *lang)
{
#ifdef HAVE_ISO_CODES
	const gchar *lang_name = "";
	const gchar *country_name = "";

	g_return_val_if_fail (have_enchant, NULL);

	if (codetable_ref == 0)
		codetable_init ();
		
	codetable_lookup (lang, &lang_name, &country_name);

	if (codetable_ref == 0)
		codetable_free ();

	if (strlen (country_name) != 0)
		return g_strdup_printf ("%s (%s)", lang_name, country_name);
	else
		return g_strdup_printf ("%s", lang_name);
#else
	return g_strdup (lang);
#endif
}

/**
 * sexy_spell_entry_language_is_active:
 * @entry: A #SexySpellEntry.
 * @lang: The language to use, in a form enchant understands.
 *
 * Determine if a given language is currently active.
 *
 * Returns: TRUE if the language is active.
 */
gboolean
sexy_spell_entry_language_is_active(const SexySpellEntry *entry,
									const gchar *lang)
{
	return (g_hash_table_lookup(entry->priv->dict_hash, lang) != NULL);
}

/**
 * sexy_spell_entry_activate_language:
 * @entry: A #SexySpellEntry
 * @lang: The language to use in a form Enchant understands. Typically either
 *        a two letter language code or a locale code in the form xx_XX.
 * @error: Return location for error.
 *
 * Activate spell checking for the language specifed.
 *
 * Returns: FALSE if there was an error.
 */
gboolean
sexy_spell_entry_activate_language(SexySpellEntry *entry, const gchar *lang, GError **error)
{
	gboolean ret;

	g_return_val_if_fail(entry != NULL, FALSE);
	g_return_val_if_fail(SEXY_IS_SPELL_ENTRY(entry), FALSE);
	g_return_val_if_fail(lang != NULL && *lang != '\0', FALSE);

	if (!have_enchant)
		return FALSE;

	if (error)
		g_return_val_if_fail(*error == NULL, FALSE);

	ret = sexy_spell_entry_activate_language_internal(entry, lang, error);

	if (ret) {
		if (entry->priv->words) {
			g_strfreev(entry->priv->words);
			g_free(entry->priv->word_starts);
			g_free(entry->priv->word_ends);
		}
		entry_strsplit_utf8(GTK_ENTRY(entry), &entry->priv->words, &entry->priv->word_starts, &entry->priv->word_ends);
		sexy_spell_entry_recheck_all(entry);
	}

	return ret;
}

/**
 * sexy_spell_entry_deactivate_language:
 * @entry: A #SexySpellEntry.
 * @lang: The language in a form Enchant understands. Typically either
 *        a two letter language code or a locale code in the form xx_XX.
 *
 * Deactivate spell checking for the language specifed.
 */
void
sexy_spell_entry_deactivate_language(SexySpellEntry *entry, const gchar *lang)
{
	g_return_if_fail(entry != NULL);
	g_return_if_fail(SEXY_IS_SPELL_ENTRY(entry));

	if (!have_enchant)
		return;

	if (!entry->priv->dict_list)
		return;

	if (lang) {
		struct EnchantDict *dict;

		dict = g_hash_table_lookup(entry->priv->dict_hash, lang);
		if (!dict)
			return;
		enchant_broker_free_dict(entry->priv->broker, dict);
		entry->priv->dict_list = g_slist_remove(entry->priv->dict_list, dict);
		g_hash_table_remove (entry->priv->dict_hash, lang);
	} else {
		/* deactivate all */
		GSList *li;
		struct EnchantDict *dict;

		for (li = entry->priv->dict_list; li; li = g_slist_next(li)) {
			dict = (struct EnchantDict *)li->data;
			enchant_broker_free_dict(entry->priv->broker, dict);
		}

		g_slist_free (entry->priv->dict_list);
		g_hash_table_destroy (entry->priv->dict_hash);
		entry->priv->dict_hash = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
		entry->priv->dict_list = NULL;
	}

	if (entry->priv->words) {
		g_strfreev(entry->priv->words);
		g_free(entry->priv->word_starts);
		g_free(entry->priv->word_ends);
	}
	entry_strsplit_utf8(GTK_ENTRY(entry), &entry->priv->words, &entry->priv->word_starts, &entry->priv->word_ends);
	sexy_spell_entry_recheck_all(entry);
}

/**
 * sexy_spell_entry_set_active_languages:
 * @entry: A #SexySpellEntry
 * @langs: A list of language codes to activate, in a form Enchant understands.
 *         Typically either a two letter language code or a locale code in the
 *         form xx_XX.
 * @error: Return location for error.
 *
 * Activate spell checking for only the languages specified.
 *
 * Returns: FALSE if there was an error.
 */
gboolean
sexy_spell_entry_set_active_languages(SexySpellEntry *entry, GSList *langs, GError **error)
{
	GSList *li;

	g_return_val_if_fail(entry != NULL, FALSE);
	g_return_val_if_fail(SEXY_IS_SPELL_ENTRY(entry), FALSE);
	g_return_val_if_fail(langs != NULL, FALSE);

	if (!have_enchant)
		return FALSE;

	/* deactivate all languages first */
	sexy_spell_entry_deactivate_language(entry, NULL);

	for (li = langs; li; li = g_slist_next(li)) {
		if (sexy_spell_entry_activate_language_internal(entry,
		    (const gchar *) li->data, error) == FALSE)
			return FALSE;
	}
	if (entry->priv->words) {
		g_strfreev(entry->priv->words);
		g_free(entry->priv->word_starts);
		g_free(entry->priv->word_ends);
	}
	entry_strsplit_utf8(GTK_ENTRY(entry), &entry->priv->words, &entry->priv->word_starts, &entry->priv->word_ends);
	sexy_spell_entry_recheck_all(entry);
	return TRUE;
}

/**
 * sexy_spell_entry_get_active_languages:
 * @entry: A #SexySpellEntry
 *
 * Retrieve a list of the currently active languages.
 *
 * Returns: A GSList of char* values with language codes (en, fr, etc).  Both
 *          the data and the list must be freed by the user.
 */
GSList *
sexy_spell_entry_get_active_languages(SexySpellEntry *entry)
{
	GSList *ret = NULL, *li;
	struct EnchantDict *dict;
	gchar *lang;

	g_return_val_if_fail(entry != NULL, NULL);
	g_return_val_if_fail(SEXY_IS_SPELL_ENTRY(entry), NULL);

	if (!have_enchant)
		return NULL;

	for (li = entry->priv->dict_list; li; li = g_slist_next(li)) {
		dict = (struct EnchantDict *) li->data;
		lang = get_lang_from_dict(dict);
		ret = g_slist_append(ret, lang);
	}
	return ret;
}

/**
 * sexy_spell_entry_is_checked:
 * @entry: A #SexySpellEntry.
 *
 * Queries a #SexySpellEntry and returns whether the entry has spell-checking enabled.
 *
 * Returns: TRUE if the entry has spell-checking enabled.
 */
gboolean
sexy_spell_entry_is_checked(SexySpellEntry *entry)
{
	return entry->priv->checked;
}

/**
 * sexy_spell_entry_set_checked:
 * @entry: A #SexySpellEntry.
 * @checked: Whether to enable spell-checking
 *
 * Sets whether the entry has spell-checking enabled.
 */
void
sexy_spell_entry_set_checked(SexySpellEntry *entry, gboolean checked)
{
	GtkWidget *widget;

	if (entry->priv->checked == checked)
		return;

	entry->priv->checked = checked;
	widget = GTK_WIDGET(entry);

	if (checked == FALSE && gtk_widget_get_realized (widget))
	{
		/* This will unmark any existing */
		sexy_spell_entry_recheck_all (entry);
	}
	else
	{
		if (entry->priv->words)
		{
			g_strfreev(entry->priv->words);
			g_free(entry->priv->word_starts);
			g_free(entry->priv->word_ends);
		}
		entry_strsplit_utf8(GTK_ENTRY(entry), &entry->priv->words, &entry->priv->word_starts, &entry->priv->word_ends);
		sexy_spell_entry_recheck_all(entry);
	}
}

/**
* sexy_spell_entry_set_parse_attributes:
* @entry: A #SexySpellEntry.
* @parse: Whether to enable showing attributes
*
* Sets whether to enable showing attributes is enabled.
*/
void
sexy_spell_entry_set_parse_attributes (SexySpellEntry *entry, gboolean parse)
{
	GtkWidget *widget;

	if (entry->priv->parseattr == parse)
		return;

	entry->priv->parseattr = parse;
	widget = GTK_WIDGET (entry);

	if (parse == FALSE && gtk_widget_get_realized (widget))
	{
		/* This will remove current attrs */
		sexy_spell_entry_recheck_all (entry);
	}
	else
	{
		if (entry->priv->words)
		{
			g_strfreev (entry->priv->words);
			g_free (entry->priv->word_starts);
			g_free (entry->priv->word_ends);
		}
		entry_strsplit_utf8 (GTK_ENTRY (entry), &entry->priv->words, &entry->priv->word_starts, &entry->priv->word_ends);
		sexy_spell_entry_recheck_all (entry);
	}
}
