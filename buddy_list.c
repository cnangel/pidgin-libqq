/**
 * @file buddy_list.c
 *
 * purple
 *
 * Purple is the legal property of its developers, whose names are too numerous
 * to list here.  Please refer to the COPYRIGHT file distributed with this
 * source distribution.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02111-1301  USA
 */

#include "qq.h"

#include "debug.h"
#include "notify.h"
#include "utils.h"
#include "packet_parse.h"
#include "buddy_info.h"
#include "buddy_memo.h"
#include "buddy_list.h"
#include "buddy_opt.h"
#include "char_conv.h"
#include "qq_define.h"
#include "qq_base.h"
#include "group.h"
#include "group_internal.h"
#include "group_info.h"

#include "qq_network.h"

typedef struct _qq_buddy_online {
	guint16 unknown1;
	guint8 ext_flag;
	guint8 comm_flag;
	guint16 unknown2;
	guint8 ending;		/* 0x00 */
} qq_buddy_online;

/* get a list of online_buddies */
void qq_request_get_buddies_online(PurpleConnection *gc, guint8 position, guint32 update_class)
{
	guint8 *raw_data;
	gint bytes = 0;

	raw_data = g_newa(guint8, 5);

	/* 000-000 get online friends cmd
	 * only 0x02 and 0x03 returns info from server, other valuse all return 0xff
	 * I can also only send the first byte (0x02, or 0x03)
	 * and the result is the same */
	bytes += qq_put8(raw_data + bytes, 0x02);
	/* 001-001 seems it supports 255 online buddies at most */
	bytes += qq_put8(raw_data + bytes, position);
	/* 002-002 */
	bytes += qq_put8(raw_data + bytes, 0x00);
	/* 003-004 */
	bytes += qq_put16(raw_data + bytes, 0x0000);

	qq_send_cmd_mess(gc, QQ_CMD_GET_BUDDIES_ONLINE, raw_data, 5, update_class, 0);
}


void qq_request_get_group_list(PurpleConnection *gc, guint16 position, guint32 update_class)
{
	qq_data *qd;
	guint8 raw_data[16] = {0};
	gint bytes = 0;

	qd = (qq_data *) gc->proto_data;


	bytes += qq_put16(raw_data + bytes, 0x1F01);
	bytes += qq_put32(raw_data + bytes, position);

	qq_send_cmd_mess(gc, QQ_CMD_GET_GROUP_LIST, raw_data, bytes, update_class, 0);
}

/* position starts with 0x0000,
 * server may return a position tag if list is too long for one packet */
void qq_request_get_buddies_list(PurpleConnection *gc, guint16 position, guint32 update_class)
{
	qq_data *qd;
	guint8 raw_data[16] = {0};
	gint bytes = 0;

	qd = (qq_data *) gc->proto_data;

	
	bytes += qq_put16(raw_data + bytes, 0x0100);
	bytes += qq_put32(raw_data + bytes, 0x00000000);
	bytes += qq_put32(raw_data + bytes, 0x00000002);

	/* starting position, can manually specify */
	bytes += qq_put16(raw_data+bytes, position);

	bytes += qq_put16(raw_data+bytes, 0);

	qq_send_cmd_mess(gc, QQ_CMD_GET_BUDDIES_LIST, raw_data, bytes, update_class, 0);
}

/* parse the data into qq_buddy_status */
static gint get_buddy_status(qq_buddy_status *bs, guint8 *data)
{
	gint bytes = 0;

	g_return_val_if_fail(data != NULL && bs != NULL, -1);

	/* 000-003: uid */
	bytes += qq_get32(&bs->uid, data + bytes);
	/* 004-004: 0x01 */
	bytes += qq_get8(&bs->unknown1, data + bytes);
	/* this is no longer the IP, it seems QQ (as of 2006) no longer sends
	 * the buddy's IP in this packet. all 0s */
	/* 005-008: ip */
	bytes += qq_getIP(&bs->ip, data + bytes);
	/* port info is no longer here either */
	/* 009-010: port */
	bytes += qq_get16(&bs->port, data + bytes);
	/* 011-011: 0x00 */
	bytes += qq_get8(&bs->unknown2, data + bytes);
	/* 012-012: status */
	bytes += qq_get8(&bs->status, data + bytes);
	/* 013-014: client tag */
	bytes += qq_get16(&bs->version, data + bytes);
	/* 015-030: unknown key */
	bytes += qq_getdata(bs->key, QQ_KEY_LENGTH, data + bytes);
	/* 031-032: */
	bytes += qq_get16(&bs->unknown3, data + bytes);
	/* 033-033: ext_flag */
	bytes += qq_get8(&bs->ext_flag, data + bytes);
	/* 034-034: comm_flag */
	bytes += qq_get8(&bs->comm_flag, data + bytes);

	purple_debug_info("QQ", "Status: %d, uid: %u, ip: %s:%d, Group id: 0x%X, Flag: 0x%X, U: %d - %d - %d, Ver: %04X\n",
			bs->status, bs->uid, inet_ntoa(bs->ip), bs->port,
			bs->ext_flag, bs->comm_flag, 
			bs->unknown1, bs->unknown2, bs->unknown3, bs->version);

	return bytes;
}

/* process the reply packet for get_buddies_online packet */
guint8 qq_process_get_buddies_online(guint8 *data, gint data_len, PurpleConnection *gc)
{
	qq_data *qd;
	gint bytes, bytes_start;
	gint count;
	guint8  position;
	PurpleBuddy *buddy;
	qq_buddy_data *bd;
	int entry_len = 42;

	qq_buddy_status bs;
	guint16 unknown;
	guint8 ending;		/* 0x00 */

	g_return_val_if_fail(data != NULL && data_len != 0, -1);

	qd = (qq_data *) gc->proto_data;

	/* qq_show_packet("Get buddies online reply packet", data, len); */

	bytes = 0;
	bytes += qq_get8(&position, data + bytes);

	count = 0;
	while (bytes < data_len) {
		if (data_len - bytes < entry_len) {
			purple_debug_error("QQ", "[buddies online] only %d, need %d\n",
					(data_len - bytes), entry_len);
			break;
		}
		memset(&bs, 0 ,sizeof(bs));

		/* set flag */
		bytes_start = bytes;
		/* based on one online buddy entry */
		/* 000-034 qq_buddy_status */
		bytes += get_buddy_status(&bs, data + bytes);
		/* 035-036: */
		bytes += qq_get16(&unknown, data + bytes);
		/* 037-037: */
		bytes += qq_get8(&ending, data + bytes);	/* 0x00 */

		bytes += 4;

		if (bs.uid == 0 || (bytes - bytes_start) != entry_len) {
			purple_debug_error("QQ", "uid=0 or entry complete len(%d) != %d\n",
					(bytes - bytes_start), entry_len);
			continue;
		}	/* check if it is a valid entry */

		if (bs.uid == qd->uid) {
			purple_debug_warning("QQ", "I am in online list %u\n", bs.uid);
		}

		/* update buddy information */
		buddy = qq_buddy_find_or_new(gc, bs.uid, 0);
		bd = (buddy == NULL) ? NULL : (qq_buddy_data *)purple_buddy_get_protocol_data(buddy);
		if (bd == NULL) {
			purple_debug_error("QQ",
					"Got an online buddy %u, but not in my buddy list\n", bs.uid);
			continue;
		}
		/*
		if(0 != fe->s->client_tag)
			q_bud->client_tag = fe->s->client_tag;
		*/
		if (bd->status != bs.status || bd->comm_flag != bs.comm_flag) {
			bd->status = bs.status;
			bd->comm_flag = bs.comm_flag;
			qq_update_buddy_status(gc, bd->uid, bd->status, bd->comm_flag);
		}
		bd->ip.s_addr = bs.ip.s_addr;
		bd->port = bs.port;
		bd->ext_flag = bs.ext_flag;
		bd->last_update = time(NULL);
		count++;
	}

	if(bytes > data_len) {
		purple_debug_error("QQ",
				"qq_process_get_buddies_online: Dangerous error! maybe protocol changed, notify developers!\n");
	}

	purple_debug_info("QQ", "Received %d online buddies, nextposition=%u\n",
			count, (guint) position);
	return position;
}


guint32 qq_process_get_group_list(guint8 *data, gint data_len, PurpleConnection *gc)
{
	qq_data *qd;
	gint bytes;
	guint8 cmd;
	guint8 position;
	qq_group * g;

	g_return_val_if_fail(data != NULL && data_len != 0, -1);

	qd = (qq_data *) gc->proto_data;

	qd->group_list = NULL;

	bytes = 0;
	bytes += qq_get8(&cmd, data + bytes);
	/* cmd == 0x1F */
	bytes += qq_get8(&position, data+bytes);
	if (position == 0x01)
	{
		/* no group, return */
		return 0;
	}

	bytes += 6;	/* 00 00 00 XX(one byte of Number of Group) 00 00 */

	while (bytes < data_len)
	{
		g = g_new0(qq_group, 1);
		bytes += qq_get8(&g->group_id, data+bytes);
		bytes += 1;  /* one byte : group_id+1 */
		bytes += qq_get_vstr(&g->group_name, NULL, sizeof(guint8), data+bytes);
		purple_debug_info("QQ", "Get a Group: %s\n", g->group_name);
		qq_group_find_or_new(g->group_name);
		qd->group_list = g_slist_append(qd->group_list, g);
	}
	/*	if all groups received, position = 0
		add buddies associated with group */		
	if (!position)
	{
		while (qd->buddy_list)
		{
			qq_buddy_find_or_new(gc, ((qq_buddy_group *)(qd->buddy_list->data))->uid, ((qq_buddy_group *)(qd->buddy_list->data))->group_id);
			g_free(qd->buddy_list->data);
			qd->buddy_list = g_slist_remove(qd->buddy_list, qd->buddy_list->data);
		}

	}
	return position;
}

/* process reply for get_buddies_list */
guint16 qq_process_get_buddies_list(guint8 *data, gint data_len, PurpleConnection *gc)
{
	qq_data *qd;
	qq_buddy_data bd;
	gint bytes_expected, count;
	gint bytes, buddy_bytes;
	gint nickname_len;
	guint16 position, unknown;
	PurpleBuddy *buddy;

	g_return_val_if_fail(data != NULL && data_len != 0, -1);

	qd = (qq_data *) gc->proto_data;

	if (data_len <= 2) {
		purple_debug_error("QQ", "empty buddies list\n");
		return -1;
	}
	/* qq_show_packet("QQ get buddies list", data, data_len); */
	bytes = 10;
	bytes += qq_get16(&position, data + bytes);
	bytes += 5;
	/* the following data is buddy list in this packet */
	count = 0;
	while (bytes < data_len-5) /* end with 04 4D XX XX XX */
	{
		memset(&bd, 0, sizeof(bd));
		/* set flag */
		buddy_bytes = bytes;
		/* 000-003: uid */
		bytes += qq_get32(&bd.uid, data + bytes);
		/* 004-005: icon index (1-255) */
		bytes += qq_get16(&bd.face, data + bytes);
		/* 006-006: age */
		bytes += qq_get8(&bd.age, data + bytes);
		/* 007-007: gender */
		bytes += qq_get8(&bd.gender, data + bytes);

		bytes += nickname_len = qq_get_vstr(&bd.nickname, NULL, sizeof(guint8), data+bytes);

		qq_filter_str(bd.nickname);

		/* Fixme: merge following as 32bit flag */
		bytes += qq_get16(&unknown, data + bytes);
		bytes += qq_get8(&bd.ext_flag, data + bytes);
		bytes += qq_get8(&bd.comm_flag, data + bytes);

		bytes += 32-4;
		bytes_expected = 40 + nickname_len;

		if (bd.uid == 0 || (bytes - buddy_bytes) != bytes_expected) {
			purple_debug_info("QQ",
					"Buddy entry, expect %d bytes, read %d bytes\n",
					bytes_expected, bytes - buddy_bytes);
			g_free(bd.nickname);
			continue;
		} else {
			count++;
		}

#if 1
		purple_debug_info("QQ", "buddy [%09d]: ext_flag=0x%02x, comm_flag=0x%02x, nick=%s\n",
				bd.uid, bd.ext_flag, bd.comm_flag, bd.nickname);
#endif

		buddy = qq_buddy_find_or_new(gc, bd.uid, 0);
		if (buddy == NULL || purple_buddy_get_protocol_data(buddy) == NULL) {
			g_free(bd.nickname);
			continue;
		}
		purple_blist_server_alias_buddy(buddy, bd.nickname);
		bd.last_update = time(NULL);
		qq_update_buddy_status(gc, bd.uid, bd.status, bd.comm_flag);

		g_memmove(purple_buddy_get_protocol_data(buddy), &bd, sizeof(qq_buddy_data));
	}

	if(bytes > data_len) {
		purple_debug_error("QQ",
				"qq_process_get_buddies_list: Dangerous error! maybe protocol changed, notify developers!\n");
	}

	purple_debug_info("QQ", "Received %d buddies, nextposition=%u\n",
		count, (guint) position);
	return position;
}

#define QQ_CHANGE_ONLINE_STATUS_REPLY_OK 	0x30	/* ASCII value of "0" */

/* TODO: figure out what's going on with the IP region. Sometimes I get valid IP addresses,
 * but the port number's weird, other times I get 0s. I get these simultaneously on the same buddy,
 * using different accounts to get info. */
static guint8  get_status_from_purple(PurpleConnection *gc)
{
	qq_data *qd;
	PurpleAccount *account;
	PurplePresence *presence;
	guint8 ret;

	qd = (qq_data *) gc->proto_data;
	account = purple_connection_get_account(gc);
	presence = purple_account_get_presence(account);

	if (purple_presence_is_status_primitive_active(presence, PURPLE_STATUS_INVISIBLE)) {
		ret = QQ_BUDDY_ONLINE_INVISIBLE;
	} else if (purple_presence_is_status_primitive_active(presence, PURPLE_STATUS_UNAVAILABLE))
	{
		if (qd->client_version >= 2010) {
			ret = QQ_BUDDY_ONLINE_BUSY;
		}
	} else if (purple_presence_is_status_primitive_active(presence, PURPLE_STATUS_AWAY)
		|| purple_presence_is_status_primitive_active(presence, PURPLE_STATUS_EXTENDED_AWAY)
		|| purple_presence_is_status_primitive_active(presence, PURPLE_STATUS_UNAVAILABLE)) {
			ret = QQ_BUDDY_ONLINE_AWAY;
	} else {
		ret = QQ_BUDDY_ONLINE_NORMAL;
	}
	return ret;
}

/* send a packet to change my online status */
void qq_request_change_status(PurpleConnection *gc, guint32 update_class)
{
	qq_data *qd;
	guint8 raw_data[16] = {0};
	gint bytes = 0;
	guint8 away_cmd;
	guint16 misc_status;
	gboolean fake_video;

	qd = (qq_data *) gc->proto_data;
	if (!qd->is_login)
		return;

	away_cmd = get_status_from_purple(gc);

	misc_status = 0x0000;
	fake_video = purple_prefs_get_bool("/plugins/prpl/qq/show_fake_video");
	if (fake_video)
		misc_status |= 0x0001;

	if (qd->client_version >= 2010) {
		bytes = 0;
		bytes += qq_put8(raw_data + bytes, away_cmd);
		/* status version */
		bytes += qq_put16(raw_data + bytes, 0);
		bytes += qq_put16(raw_data + bytes, misc_status);
		/* Fixme: custom status message, now is empty */
		bytes += qq_put16(raw_data + bytes, 0);
		bytes += qq_put32(raw_data + bytes, 0);
	}
	qq_send_cmd_mess(gc, QQ_CMD_CHANGE_STATUS, raw_data, bytes, update_class, 0);
}

/* parse the reply packet for change_status */
void qq_process_change_status(guint8 *data, gint data_len, PurpleConnection *gc)
{
	qq_data *qd;
	gint bytes;
	guint8 reply;
	qq_buddy_data *bd;

	g_return_if_fail(data != NULL && data_len != 0);

	qd = (qq_data *) gc->proto_data;

	bytes = 0;
	bytes = qq_get8(&reply, data + bytes);
	if (reply != QQ_CHANGE_ONLINE_STATUS_REPLY_OK) {
		purple_debug_warning("QQ", "Change status fail 0x%02X\n", reply);
		return;
	}

	/* purple_debug_info("QQ", "Change status OK\n"); */
	bd = qq_buddy_data_find(gc, qd->uid);
	if (bd != NULL) {
		bd->status = get_status_from_purple(gc);
		bd->last_update = time(NULL);
		qq_update_buddy_status(gc, bd->uid, bd->status, bd->comm_flag);
	}
}

/* it is a server message indicating that one of my buddies has changed its status */
void qq_process_buddy_change_status(guint8 *data, gint data_len, PurpleConnection *gc)
{
	qq_data *qd;
	gint bytes;
	guint32 my_uid;
	PurpleBuddy *buddy;
	qq_buddy_data *bd;
	qq_buddy_status bs;

	g_return_if_fail(data != NULL && data_len != 0);

	qd = (qq_data *) gc->proto_data;

	if (data_len < 35) {
		purple_debug_error("QQ", "[buddy status change] only %d, need 35 bytes\n", data_len);
		return;
	}

	memset(&bs, 0, sizeof(bs));
	bytes = 0;
	/* 000-030: qq_buddy_status */
	bytes += get_buddy_status(&bs, data + bytes);
	/* 034-037:  my uid */
	/* This has a value of 0 when we've changed our status to
	 * QQ_BUDDY_ONLINE_INVISIBLE */
	bytes += qq_get32(&my_uid, data + bytes);

	/* update buddy information */
	buddy = qq_buddy_find_or_new(gc, bs.uid, 0);
	bd = (buddy == NULL) ? NULL : (qq_buddy_data *)purple_buddy_get_protocol_data(buddy);
	if (bd == NULL) {
		purple_debug_warning("QQ", "Got status of no-auth buddy %u\n", bs.uid);
		return;
	}

	if(bs.ip.s_addr != 0) {
		bd->ip.s_addr = bs.ip.s_addr;
		bd->port = bs.port;
	}
	if (bd->status != bs.status) {
		bd->status = bs.status;
		qq_update_buddy_status(gc, bd->uid, bd->status, bd->comm_flag);
	}
	bd->last_update = time(NULL);

	if (bd->status == QQ_BUDDY_ONLINE_NORMAL && bd->level <= 0) {
			qq_request_get_level(gc, bd->uid);
	}
}

/*TODO: maybe this should be qq_update_buddy_status() ?*/
void qq_update_buddy_status(PurpleConnection *gc, guint32 uid, guint8 status, guint8 flag)
{
	gchar *who;
	const gchar *status_id;

	g_return_if_fail(uid != 0);

	/* purple supports signon and idle time
	 * but it is not much use for QQ, I do not use them */
	/* serv_got_update(gc, name, online, 0, q_bud->signon, q_bud->idle, bud->uc); */
	switch(status) {
	case QQ_BUDDY_OFFLINE:
		status_id = "offline";
		break;
	case QQ_BUDDY_ONLINE_NORMAL:
		status_id = "available";
		break;
	case QQ_BUDDY_CHANGE_TO_OFFLINE:
		status_id = "offline";
		break;
	case QQ_BUDDY_ONLINE_AWAY:
		status_id = "away";
		break;
	case QQ_BUDDY_ONLINE_INVISIBLE:
		status_id = "invisible";
		break;
	case QQ_BUDDY_ONLINE_BUSY:
		status_id = "busy";
		break;
	default:
		status_id = "invisible";
		purple_debug_error("QQ", "unknown status: 0x%X\n", status);
		break;
	}

	purple_debug_info("QQ", "buddy %u status = %s\n", uid, status_id);
	who = uid_to_purple_name(uid);
	purple_prpl_got_user_status(gc->account, who, status_id, NULL);

	if (flag & QQ_COMM_FLAG_MOBILE && status != QQ_BUDDY_OFFLINE)
		purple_prpl_got_user_status(gc->account, who, "mobile", NULL);
	else
		purple_prpl_got_user_status_deactive(gc->account, who, "mobile");

	g_free(who);
}

/* refresh all buddies online/offline,
 * after receiving reply for get_buddies_online packet */
void qq_update_buddyies_status(PurpleConnection *gc)
{
	qq_data *qd;
	PurpleBuddy *buddy;
	qq_buddy_data *bd;
	GSList *buddies, *it;
	time_t tm_limit = time(NULL);

	qd = (qq_data *) (gc->proto_data);

	tm_limit -= QQ_UPDATE_ONLINE_INTERVAL;

	buddies = purple_find_buddies(purple_connection_get_account(gc), NULL);
	for (it = buddies; it; it = it->next) {
		buddy = it->data;
		if (buddy == NULL) continue;

		bd = purple_buddy_get_protocol_data(buddy);
		if (bd == NULL) continue;

		if (bd->uid == 0) continue;
		if (bd->uid == qd->uid) continue;	/* my status is always online in my buddy list */
		if (tm_limit < bd->last_update) continue;
		if (bd->status == QQ_BUDDY_ONLINE_INVISIBLE) continue;
		if (bd->status == QQ_BUDDY_CHANGE_TO_OFFLINE) continue;

		bd->status = QQ_BUDDY_CHANGE_TO_OFFLINE;
		bd->last_update = time(NULL);
		qq_update_buddy_status(gc, bd->uid, bd->status, bd->comm_flag);
	}
}

void qq_buddy_data_free_all(PurpleConnection *gc)
{
	PurpleBuddy *buddy;
	GSList *buddies, *it;
	gint count = 0;

	buddies = purple_find_buddies(purple_connection_get_account(gc), NULL);
	for (it = buddies; it; it = it->next) {
		qq_buddy_data *qbd = NULL;

		buddy = it->data;
		if (buddy == NULL) continue;

		qbd = purple_buddy_get_protocol_data(buddy);
		if (qbd == NULL) continue;

		qq_buddy_data_free(qbd);
		purple_buddy_set_protocol_data(buddy, NULL);

		count++;
	}

	if (count > 0) {
		purple_debug_info("QQ", "%d buddies' data are freed\n", count);
	}
}

