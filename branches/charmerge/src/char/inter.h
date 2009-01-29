// Copyright (c) Athena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#ifndef _INTER_H_
#define _INTER_H_

#include "charserverdb.h"

int inter_init(CharServerDB* db);
void inter_final(void);
int inter_parse_frommap(int fd);
int inter_mapif_init(int fd);
int mapif_disconnectplayer(int fd, int account_id, int char_id, int reason);

extern unsigned int party_share_level;
extern char main_chat_nick[16];

#ifdef TXT_ONLY
int inter_save(void);
#endif

#endif /* _INTER_H_ */
