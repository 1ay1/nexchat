/* tests/ux/macmouse.c: move the real mouse and click on macOS (CoreGraphics),
   for reproducing what a user does. Coordinates are global screen points.
   usage: macmouse move X Y | click X Y | wiggle X Y W H SECS | key CODE */
#include <ApplicationServices/ApplicationServices.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void
post (CGEventType t, double x, double y)
{
	CGEventRef e = CGEventCreateMouseEvent (NULL, t, CGPointMake (x, y), kCGMouseButtonLeft);
	CGEventPost (kCGHIDEventTap, e);
	CFRelease (e);
}

int
main (int argc, char **argv)
{
	if (argc >= 4 && !strcmp (argv[1], "move"))
		post (kCGEventMouseMoved, atof (argv[2]), atof (argv[3]));
	else if (argc >= 4 && !strcmp (argv[1], "click"))
	{
		double x = atof (argv[2]), y = atof (argv[3]);
		post (kCGEventMouseMoved, x, y);
		usleep (50000);
		post (kCGEventLeftMouseDown, x, y);
		usleep (50000);
		post (kCGEventLeftMouseUp, x, y);
	}
	else if (argc >= 6 && !strcmp (argv[1], "drag"))
	{
		/* press at X1 Y1, move to X2 Y2 in steps (as a hand would), release */
		double x1 = atof (argv[2]), y1 = atof (argv[3]), x2 = atof (argv[4]), y2 = atof (argv[5]);
		int i, n = 24;
		post (kCGEventMouseMoved, x1, y1);
		usleep (80000);
		post (kCGEventLeftMouseDown, x1, y1);
		for (i = 1; i <= n; i++)
		{
			usleep (16000);
			post (kCGEventLeftMouseDragged, x1 + (x2 - x1) * i / n, y1 + (y2 - y1) * i / n);
		}
		usleep (50000);
		post (kCGEventLeftMouseUp, x2, y2);
	}
	else if (argc >= 7 && !strcmp (argv[1], "wiggle"))
	{
		/* sweep across an area slowly, resting so tooltips open and close */
		double x0 = atof (argv[2]), y0 = atof (argv[3]), w = atof (argv[4]), h = atof (argv[5]);
		double secs = atof (argv[6]), t = 0;
		int i = 0;
		while (t < secs)
		{
			double fx = (i % 7) / 6.0, fy = ((i / 7) % 5) / 4.0;
			post (kCGEventMouseMoved, x0 + fx * w, y0 + fy * h);
			usleep (700000);
			t += 0.7;
			i++;
		}
	}
	else if (argc >= 3 && !strcmp (argv[1], "key"))
	{
		CGKeyCode k = (CGKeyCode) atoi (argv[2]);
		CGEventRef d = CGEventCreateKeyboardEvent (NULL, k, true), u = CGEventCreateKeyboardEvent (NULL, k, false);
		CGEventPost (kCGHIDEventTap, d);
		usleep (30000);
		CGEventPost (kCGHIDEventTap, u);
		CFRelease (d);
		CFRelease (u);
	}
	else
		return 2;
	return 0;
}
