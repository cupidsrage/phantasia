/*-
 * Copyright (c) 1989, 1993
 *      The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by the University of
 *      California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *      @(#)pathnames.h 8.2 (Berkeley) 4/2/94
 */

#ifndef PHANTASIA_DIR
#define PHANTASIA_DIR		"/app/phantasia"
#endif

/* Setting Files */
#define CHARSTATS_FILE		PHANTASIA_DIR "/data/charstats"
#define MONSTER_FILE		PHANTASIA_DIR "/data/monsters"
#define MOTD_FILE		PHANTASIA_DIR "/data/motd"
#define PROTECTED_FILE		PHANTASIA_DIR "/data/protected"
#define SHOPITEMS_FILE		PHANTASIA_DIR "/data/shopitems"
#define WIZARD_FILE		PHANTASIA_DIR "/data/wizards"

/* Data Files */
#define ACCOUNT_FILE		PHANTASIA_DIR "/data/account"
#define TEMP_ACCOUNT_FILE	PHANTASIA_DIR "/data/accounts.tmp"
#define NETWORK_FILE		PHANTASIA_DIR "/data/networks"
#define TEMP_NETWORK_FILE	PHANTASIA_DIR "/data/networks.tmp"
#define BACKUP_FILE		PHANTASIA_DIR "/data/backup"
#define TEMP_BACKUP_FILE	PHANTASIA_DIR "/data/backup.tmp"
#define CHARACTER_FILE		PHANTASIA_DIR "/data/characters"
#define TEMP_CHARACTER_FILE	PHANTASIA_DIR "/data/characters.tmp"
#define SECONDARY_CHAR_FILE	PHANTASIA_DIR "/data/characters.2"
#define DATA_FILE		PHANTASIA_DIR "/data/realmdata"
#define SCOREBOARD_FILE		PHANTASIA_DIR "/data/scoreboard"
#define TEMP_SCOREBOARD_FILE	PHANTASIA_DIR "/data/scoreboard.tmp"
#define OLD_SCOREBOARD_FILE	PHANTASIA_DIR "/data/scoreboard.old"
#define TAG_FILE		PHANTASIA_DIR "/data/tags"
#define TEMP_TAG_FILE		PHANTASIA_DIR "/data/tags.tmp"
#define TAGGED_FILE		PHANTASIA_DIR "/data/tagged"
#define TEMP_TAGGED_FILE	PHANTASIA_DIR "/data/tagged.tmp"
#define HISTORY_FILE		PHANTASIA_DIR "/data/history"
#define QUEST_FILE		PHANTASIA_DIR "/data/quests"
#define TEMP_QUEST_FILE		PHANTASIA_DIR "/data/quests.tmp"
#define EXTRA_MONSTER_FILE	PHANTASIA_DIR "/data/extramonsters"
#define GEAR_FILE		PHANTASIA_DIR "/data/gear"
#define TEMP_GEAR_FILE		PHANTASIA_DIR "/data/gear.tmp"
#define WORLD_EVENT_FILE	PHANTASIA_DIR "/data/worldevent"
#define ARTIFACT_FILE		PHANTASIA_DIR "/data/artifacts"
#define TEMP_ARTIFACT_FILE	PHANTASIA_DIR "/data/artifacts.tmp"
#define DEEDS_FILE		PHANTASIA_DIR "/data/deeds"
#define TEMP_DEEDS_FILE		PHANTASIA_DIR "/data/deeds.tmp"

/* Log Files */
#define BATTLE_LOG		PHANTASIA_DIR "/log/battles"
#define CANTRIP_LOG		PHANTASIA_DIR "/log/cantrip"
#define CHAT_LOG		PHANTASIA_DIR "/log/chatlog"
#define COMBAT_LOG		PHANTASIA_DIR "/log/combat"
#define CONNECTION_LOG		PHANTASIA_DIR "/log/connections"
#define DEBUG_LOG		PHANTASIA_DIR "/log/debug"
#define ERROR_LOG		PHANTASIA_DIR "/log/errorlog"
#define EVENT_LOG		PHANTASIA_DIR "/log/events"
#define GAME_LOG		PHANTASIA_DIR "/log/games"
#define HACK_LOG		PHANTASIA_DIR "/log/hacks"
#define LEVEL_LOG		PHANTASIA_DIR "/log/levels"
#define PURCHASE_LOG		PHANTASIA_DIR "/log/purchase"
#define SERVER_LOG		PHANTASIA_DIR "/log/server"

/* Support Files */
#define NEW_ACCOUNT_SCRIPT PHANTASIA_DIR "/bin/new_account_mail.pl"
#define	ACCOUNT_PASSWORD_RESET_SCRIPT PHANTASIA_DIR "/bin/account_reset_mail.pl"
#define	CHARACTER_PASSWORD_RESET_SCRIPT PHANTASIA_DIR "/bin/character_reset_mail.pl"


