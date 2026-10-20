#ifndef NEXCHAT_UPLOAD_H
#define NEXCHAT_UPLOAD_H

#include <gtk/gtk.h>

/* the server offers a file host (soju.im/FILEHOST) */
gboolean upload_available (struct session *sess);
/* upload; the link goes in sess's input box */
void upload_file (struct session *sess, GFile *file);
void upload_bytes (struct session *sess, GBytes *data, const char *name, const char *type);
/* Cmd/Ctrl+V: an image on the clipboard is uploaded. TRUE if it was */
gboolean upload_paste (struct session *sess, GtkWidget *entry);
void upload_test_init (void);

#endif
