/* nexchat
 * Copyright (C) 2026 nexchat contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef NEXCHAT_JUMPTO_H
#define NEXCHAT_JUMPTO_H

/* Window > Jump To... */
void jumpto_open (void);
/* how well typed matches name: 0 none, higher better (for the tests) */
int jumpto_score (const char *name, const char *typed);

#endif
