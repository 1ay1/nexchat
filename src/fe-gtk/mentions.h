#ifndef NEXCHAT_MENTIONS_H
#define NEXCHAT_MENTIONS_H

/* Window > Mentions: highlights and private messages from everywhere */
void mentions_open (void);
/* a tab closed: its mentions can't be jumped to any more */
void mentions_session_gone (struct session *sess);

#endif
