/* nexchat plugin header.
 *
 * nexchat keeps the HexChat plugin ABI, so this is the same API. New plugins
 * can include this; existing ones that include hexchat-plugin.h keep working.
 */
#ifndef NEXCHAT_PLUGIN_H
#define NEXCHAT_PLUGIN_H

#include "hexchat-plugin.h"

#define NEXCHAT_PRI_HIGHEST HEXCHAT_PRI_HIGHEST
#define NEXCHAT_PRI_HIGH HEXCHAT_PRI_HIGH
#define NEXCHAT_PRI_NORM HEXCHAT_PRI_NORM
#define NEXCHAT_PRI_LOW HEXCHAT_PRI_LOW
#define NEXCHAT_PRI_LOWEST HEXCHAT_PRI_LOWEST

#define NEXCHAT_FD_READ HEXCHAT_FD_READ
#define NEXCHAT_FD_WRITE HEXCHAT_FD_WRITE
#define NEXCHAT_FD_EXCEPTION HEXCHAT_FD_EXCEPTION
#define NEXCHAT_FD_NOTSOCKET HEXCHAT_FD_NOTSOCKET

#define NEXCHAT_EAT_NONE HEXCHAT_EAT_NONE
#define NEXCHAT_EAT_NEXCHAT HEXCHAT_EAT_HEXCHAT
#define NEXCHAT_EAT_PLUGIN HEXCHAT_EAT_PLUGIN
#define NEXCHAT_EAT_ALL HEXCHAT_EAT_ALL

typedef hexchat_plugin nexchat_plugin;
typedef hexchat_list nexchat_list;
typedef hexchat_hook nexchat_hook;
typedef hexchat_context nexchat_context;
typedef hexchat_event_attrs nexchat_event_attrs;

#endif
