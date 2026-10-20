#ifndef NEXCHAT_NXIMAGE_H
#define NEXCHAT_NXIMAGE_H

#include <gtk/gtk.h>

/* an image from file bytes: PNG/JPEG/TIFF through GTK, GIF/WebP/BMP/SVG...
   through gdk-pixbuf. An animated GIF comes back as a paintable that
   plays itself (*animated TRUE). NULL: not an image anything here reads. */
GdkPaintable *nx_image_from_bytes (GBytes *bytes, int *width, int *height, gboolean *animated);

#endif
