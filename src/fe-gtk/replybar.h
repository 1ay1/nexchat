#ifndef NEXCHAT_REPLYBAR_H
#define NEXCHAT_REPLYBAR_H

#include <gtk/gtk.h>

/* the "Replying to alice: ..." bar over the input box */
GtkWidget *replybar_new (struct session_gui *gui);
/* reply to msgid in sess (NULL: not replying any more) */
void replybar_set (struct session *sess, const char *msgid);
gboolean replybar_active (struct session *sess);
/* the bar shows sess's target (tab switched to it) */
void replybar_update (struct session *sess);
/* Enter: send text as the reply; TRUE if it went that way */
gboolean replybar_send (struct session *sess, const char *text);
/* choose the message to answer from the recent ones */
void replybar_pick (struct session *sess);
void replybar_test_init (void);

#endif
