#ifndef NEXCHAT_URLPREVIEW_H
#define NEXCHAT_URLPREVIEW_H

#include <gtk/gtk.h>

/* image links in this chat box pop up a preview when hovered */
void urlpreview_attach (GtkWidget *xtext);
void urlpreview_test_hover (GtkWidget *xtext, const char *url);

#endif
