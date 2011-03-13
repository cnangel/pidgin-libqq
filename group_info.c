/**
 * @file group_info.c
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

#include "internal.h"

#include "conversation.h"
#include "debug.h"

#include "char_conv.h"
#include "group_im.h"
#include "group_internal.h"
#include "group_info.h"
#include "buddy_list.h"
#include "qq_define.h"
#include "packet_parse.h"
#include "qq_network.h"
#include "utils.h"

/* we check who needs to update member info every minutes
 * this interval determines if their member info is outdated */
#define QQ_GROUP_CHAT_REFRESH_NICKNAME_INTERNAL  180

static gboolean check_update_interval(qq_buddy_data *member)
{
	g_return_val_if_fail(member != NULL, FALSE);
	return (member->nickname == NULL) ||
		(time(NULL) - member->last_update) > QQ_GROUP_CHAT_REFRESH_NICKNAME_INTERNAL;
}

/* this is done when we receive the reply to get_online_members sub_cmd
 * all member are set offline, and then only those in reply packets are online */
static void set_all_offline(qq_room_data *rmd)
{
	GList *list;
	qq_buddy_data *bd;
	g_return_if_fail(rmd != NULL);

	list = rmd->members;
	while (list != NULL) {
		bd = (qq_buddy_data *) list->data;
		bd->status = QQ_BUDDY_CHANGE_TO_OFFLINE;
		list = list->next;
	}
}

/* send packet to get info for each group member */
gint qq_request_room_get_members_info( PurpleConnection *gc, guint32 room_id, guint32 update_class, guint32 index )
{
	guint8 *raw_data;
	gint bytes, num;
	GList *list;
	qq_room_data *rmd;
	qq_buddy_data *bd;
	guint32 i = 0;

	g_return_val_if_fail(room_id > 0, 0);

	rmd  = qq_room_data_find(gc, room_id);
	g_return_val_if_fail(rmd != NULL, 0);

	for (num = 0, list = rmd->members; list != NULL; list = list->next) {
		bd = (qq_buddy_data *) list->data;
		if (check_update_interval(bd))
			num++;
	}

	if (num <= 0) {
		purple_debug_info("QQ", "No group member info needs to be updated now.\n");
		return 0;
	}

	raw_data = g_newa(guint8, 4 * num);

	bytes = 0;

	list = rmd->members;
	/* index shipped from last request 
		send 30 uids one time	*/
	while (list != NULL) {
		if (i>=index)
		{
			bd = (qq_buddy_data *) list->data;
			if (check_update_interval(bd))
				bytes += qq_put32(raw_data + bytes, bd->uid);
		}
		i++;
		if (i==index+30) break;
		list = list->next;
	}
	/* if reach the end */
	if (list == NULL)	i=0;

	qq_send_room_cmd_mess(gc, QQ_ROOM_CMD_GET_MEMBERS_INFO, rmd->id, raw_data, bytes,
			update_class, i);
	return num;
}

static gchar *get_role_desc(qq_room_role role)
{
	const char *role_desc;
	switch (role) {
	case QQ_ROOM_ROLE_NO:
		role_desc = _("Not member");
		break;
	case QQ_ROOM_ROLE_YES:
		role_desc = _("Member");
		break;
	case QQ_ROOM_ROLE_REQUESTING:
		role_desc = _("Requesting");
		break;
	case QQ_ROOM_ROLE_ADMIN:
		role_desc = _("Admin");
		break;
	default:
		role_desc = _("Unknown");
	}

	return g_strdup(role_desc);
}

static void room_info_display(PurpleConnection *gc, qq_room_data *rmd)
{
	PurpleNotifyUserInfo *room_info;
	gchar *utf8_value;

	g_return_if_fail(rmd != NULL && rmd->id > 0);

	room_info = purple_notify_user_info_new();

	/* XXX: Should this be "Topic"? */
	purple_notify_user_info_add_pair(room_info, _("Room Title"), rmd->name);
	purple_notify_user_info_add_pair(room_info, _("Notice"), rmd->bulletin);
	purple_notify_user_info_add_pair(room_info, _("Detail"), rmd->intro);

	purple_notify_user_info_add_section_break(room_info);

	utf8_value = g_strdup_printf(("%u"), rmd->creator_uid);
	purple_notify_user_info_add_pair(room_info, _("Creator"), utf8_value);
	g_free(utf8_value);

	utf8_value = get_role_desc(rmd->my_role);
	purple_notify_user_info_add_pair(room_info, _("About me"), utf8_value);
	g_free(utf8_value);

	utf8_value = g_strdup_printf(("%d"), rmd->category);
	purple_notify_user_info_add_pair(room_info, _("Category"), utf8_value);
	g_free(utf8_value);

	utf8_value = g_strdup_printf(("%d"), rmd->auth_type);
	purple_notify_user_info_add_pair(room_info, _("Authorize"), utf8_value);
	g_free(utf8_value);

	utf8_value = g_strdup_printf(("%u"), rmd->qun_id);
	purple_notify_userinfo(gc, utf8_value, room_info, NULL, NULL);
	g_free(utf8_value);

	purple_notify_user_info_destroy(room_info);
}

void qq_process_room_cmd_get_qun_list( guint8 *data, gint data_len, PurpleConnection *gc )
{
	qq_data *qd;
	qq_room_data *rmd;
	guint32 id, qun_id;
	gint bytes;
	guint8 num;
	guint8 i;

	g_return_if_fail(data != NULL && data_len > 0);
	qd = (qq_data *) gc->proto_data;

	/* qq_show_packet("Room List", data, data_len); */

	bytes = 0;
	bytes += qq_get8(&num, data);
	for (i=0; i<num; ++i)
	{	
		bytes += qq_get32(&id, data+bytes);
		bytes += qq_get32(&qun_id, data+bytes);
		bytes ++;

		rmd = qq_room_data_find(gc, id);
		g_return_if_fail(rmd != NULL);
		rmd->qun_id = qun_id;
		rmd->my_role = QQ_ROOM_ROLE_YES;
		purple_debug_info("QQ", "Qun added id: %u qun_id: %u\n",
			rmd->id, rmd->qun_id);
	}
}

void qq_process_room_cmd_get_info(guint8 *data, gint data_len, guint32 action, PurpleConnection *gc)
{
	qq_data *qd;
	qq_room_data *rmd;
	qq_buddy_data *bd;
	PurpleChat *chat;
	PurpleConversation *conv;
	guint8 organization, role;
	guint16 max_members;
	guint32 resend_flag, member_uid, id, qun_id, last_uid;
	gint bytes; 
	guint num;
	guint8 has_more=0;
	gchar *topic;

	g_return_if_fail(data != NULL && data_len > 0);
	qd = (qq_data *) gc->proto_data;

	/* qq_show_packet("Room Info", data, data_len); */

	bytes = 0;
	bytes += qq_get32(&id, data + bytes);
	g_return_if_fail(id > 0);

	bytes += qq_get32(&qun_id, data + bytes);
	g_return_if_fail(qun_id > 0);

	chat = qq_room_find_or_new(gc, id, qun_id);
	g_return_if_fail(chat != NULL);
	rmd = qq_room_data_find(gc, id);
	g_return_if_fail(rmd != NULL);

	bytes += qq_get32(&resend_flag, data + bytes);		//first 00 00 00 03, second 00 00 00 02

	if (resend_flag == 0x00000003)
	{
		bytes += qq_get8(&(rmd->type8), data + bytes);
		bytes += 4;	//maybe vip sign
		bytes += qq_get32(&(rmd->creator_uid), data + bytes);
		if (rmd->creator_uid == qd->uid)
			rmd->my_role = QQ_ROOM_ROLE_ADMIN;
		bytes += qq_get8(&(rmd->auth_type), data + bytes);
		bytes += 4	;	/* oldCategory */
		bytes += 2;	// 00 00
		bytes += qq_get32(&(rmd->category), data + bytes);
		bytes += qq_get16(&max_members, data + bytes);
		bytes += 1; 
		bytes += 8;
		purple_debug_info("QQ", "type: %u creator: %u category: %u max_members: %u\n",
			rmd->type8, rmd->creator_uid, rmd->category, max_members);

		bytes += qq_get_vstr(&(rmd->name), NULL, sizeof(guint8), data + bytes);
		bytes += 2;	/* 0x0000 */
		bytes += qq_get_vstr(&(rmd->bulletin), NULL, sizeof(guint8), data + bytes);
		bytes += qq_get_vstr(&(rmd->intro), NULL, sizeof(guint8), data + bytes);
		bytes += qq_get_vstr(&(rmd->token), NULL, sizeof(guint16), data + bytes);
		purple_debug_info("QQ", "room [%s] bulletin [%s] intro [%s] \n",
			rmd->name, rmd->bulletin, rmd->intro);
		bytes += 2;		//Unknown
		bytes += qq_get32(&last_uid, data + bytes);	/* last_uid of this recv, request more with it */
		bytes += qq_get8(&has_more, data + bytes);	/* if there are more, request again */
		/* now comes the member list separated by 0x00 */
	}

	num = 0;
	while (bytes < data_len) {
		bytes += qq_get32(&member_uid, data + bytes);
		num++;
		bytes += qq_get8(&organization, data + bytes);
		bytes += qq_get8(&role, data + bytes);

#if 0
		if(organization != 0 || role != 0) {
			purple_debug_info("QQ", "%u, organization=%d, role=%d\n", member_uid, organization, role);
		}
#endif

		bd = qq_room_buddy_find_or_new(gc, rmd, member_uid);
		if (bd != NULL)
			bd->role = role;
	}

	purple_debug_info("QQ", "group \"%s\" has received %d members\n", rmd->name, num);

	if (has_more)
	{
		qq_send_room_cmd_mess(gc, QQ_ROOM_CMD_GET_INFO, id, NULL, 0,
			0, last_uid);
	} else {
		qq_room_update_chat_info(chat, rmd);
		if (action == QQ_ROOM_INFO_DISPLAY) {
			room_info_display(gc, rmd);
		}

		conv = purple_find_conversation_with_account(PURPLE_CONV_TYPE_CHAT,
			rmd->name, purple_connection_get_account(gc));
		if(NULL == conv) {
			purple_debug_warning("QQ", "Conversation \"%s\" is not opened\n", rmd->name);
			return;
		}

		topic = g_strdup_printf("%u %s", rmd->qun_id, rmd->bulletin);
		purple_debug_info("QQ", "Set chat topic to %s\n", topic);
		purple_conv_chat_set_topic(PURPLE_CONV_CHAT(conv), NULL, topic);
		g_free(topic);
	}
}

void qq_process_room_cmd_get_onlines(guint8 *data, gint len, PurpleConnection *gc)
{
	guint32 room_id, member_uid;
	guint8 unknown;
	gint bytes, num;
	qq_room_data *rmd;
	qq_buddy_data *bd;

	g_return_if_fail(data != NULL && len > 0);

	if (len <= 3) {
		purple_debug_error("QQ", "Invalid group online member reply, discard it!\n");
		return;
	}

	bytes = 0;
	bytes += qq_get32(&room_id, data + bytes);
	bytes += qq_get8(&unknown, data + bytes);	/* 0x3c ?? */
	g_return_if_fail(room_id > 0);

	rmd = qq_room_data_find(gc, room_id);
	if (rmd == NULL) {
		purple_debug_error("QQ", "Can not info of room id [%u]\n", room_id);
		return;
	}

	/* set all offline first, then update those online */
	set_all_offline(rmd);
	num = 0;
	while (bytes < len) {
		bytes += qq_get32(&member_uid, data + bytes);
		num++;
		bd = qq_room_buddy_find_or_new(gc, rmd, member_uid);
		if (bd != NULL)
			bd->status = QQ_BUDDY_ONLINE_NORMAL;
	}
	if(bytes > len) {
		purple_debug_error("QQ",
			"group_cmd_get_online_members: Dangerous error! maybe protocol changed, notify developers!");
	}

	purple_debug_info("QQ", "Group \"%s\" has %d online members\n", rmd->name, num);
	qq_room_conv_set_onlines(gc, rmd);
}

/* process the reply to get_members_info packet */
void qq_process_room_cmd_get_members_info( guint8 *data, gint len, guint32 index, PurpleConnection *gc )
{
	gint bytes;
	gint num;
	guint32 id, member_uid;
	guint16 unknown;
	qq_room_data *rmd;
	qq_buddy_data *bd;
	gchar *nick;

	g_return_if_fail(data != NULL && len > 0);

	/* qq_show_packet("qq_process_room_cmd_get_members_info", data, len); */

	bytes = 0;
	bytes += qq_get32(&id, data + bytes);
	g_return_if_fail(id > 0);

	rmd = qq_room_data_find(gc, id);
	g_return_if_fail(rmd != NULL);

	num = 0;

	while (bytes < len) {
		bytes += qq_get32(&member_uid, data + bytes);
		g_return_if_fail(member_uid > 0);
		bd = qq_room_buddy_find_or_new(gc, rmd, member_uid);
		g_return_if_fail(bd != NULL);

		num++;
		bytes += qq_get16(&(bd->face), data + bytes);
		bytes += qq_get8(&(bd->age), data + bytes);
		bytes += qq_get8(&(bd->gender), data + bytes);
		/* only here use old charset GB18030 */
		bytes += qq_get_vstr(&nick, QQ_CHARSET_DEFAULT, sizeof(guint8), data + bytes);
		bytes += qq_get16(&unknown, data + bytes);
		bytes += qq_get8(&(bd->ext_flag), data + bytes);
		bytes += qq_get8(&(bd->comm_flag), data + bytes);

		qq_filter_str(nick);
		bd->nickname = g_strdup(nick);
		g_free(nick);

#if 0
		purple_debug_info("QQ",
				"member [%09d]: ext_flag=0x%02x, comm_flag=0x%02x, nick=%s\n",
				member_uid, bd->ext_flag, bd->comm_flag, bd->nickname);
#endif

		bd->last_update = time(NULL);
	}
	if (bytes > len) {
		purple_debug_error("QQ",
				"group_cmd_get_members_info: Dangerous error! maybe protocol changed, notify developers!");
	}
	purple_debug_info("QQ", "Group \"%s\" got %d member info\n", rmd->name, num);

	if (index)
	{
		qq_request_room_get_members_info(gc, id, 0, index);
		return;
	}
	rmd->has_got_members_info = TRUE;
	qq_room_conv_set_onlines(gc, rmd);
}

