/**
 * @file group_im.c
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
#include "notify.h"
#include "prefs.h"
#include "request.h"
#include "util.h"

#include "char_conv.h"
#include "group_internal.h"
#include "group_info.h"
#include "group_join.h"
#include "group_im.h"
#include "group_opt.h"
#include "im.h"
#include "qq_define.h"
#include "packet_parse.h"
#include "qq_network.h"
#include "qq_process.h"
#include "utils.h"

/* show group conversation window */
PurpleConversation *qq_room_conv_open(PurpleConnection *gc, qq_room_data *rmd)
{
	PurpleConversation *conv;
	gchar *topic_utf8;

	g_return_val_if_fail(rmd != NULL, NULL);
	g_return_val_if_fail(rmd->name, NULL);

	conv = purple_find_conversation_with_account(PURPLE_CONV_TYPE_CHAT,
			rmd->name, purple_connection_get_account(gc));
	if (conv != NULL)	{
		/* show only one conversation per room */
		return conv;
	}

	serv_got_joined_chat(gc, rmd->id, rmd->name);
	conv = purple_find_conversation_with_account(PURPLE_CONV_TYPE_CHAT, rmd->name, purple_connection_get_account(gc));
	if (conv != NULL) {
		if (rmd->bulletin != NULL) {
			topic_utf8 = g_strdup_printf("%u %s", rmd->qun_id, rmd->bulletin);
		} else {
			topic_utf8 = g_strdup_printf("%u", rmd->qun_id);
		}
		purple_debug_info("QQ", "Chat topic = %s\n", topic_utf8);
		purple_conv_chat_set_topic(PURPLE_CONV_CHAT(conv), NULL, topic_utf8);
		g_free(topic_utf8);

		if (rmd->has_got_members_info)
			qq_send_room_cmd_only(gc, QQ_ROOM_CMD_GET_ONLINES, rmd->id);
		else
			qq_update_room(gc, 0, rmd->id);
		return conv;
	}
	return NULL;
}

/* refresh online member in group conversation window */
void qq_room_conv_set_onlines(PurpleConnection *gc, qq_room_data *rmd)
{
	GList *names, *list, *flags;
	qq_buddy_data *bd;
	gchar *member_name, *member_uid;
	PurpleConversation *conv;
	gint flag;
	gboolean is_find;

	g_return_if_fail(rmd != NULL);

	conv = purple_find_conversation_with_account(PURPLE_CONV_TYPE_CHAT,
			rmd->name, purple_connection_get_account(gc));
	if (conv == NULL) {
		purple_debug_warning("QQ", "Conversation \"%s\" is not opened\n", rmd->name);
		return;
	}
	g_return_if_fail(rmd->members != NULL);

	names = NULL;
	flags = NULL;

	list = rmd->members;
	while (list != NULL) {
		bd = (qq_buddy_data *) list->data;

		/* we need unique identifiers for everyone in the chat or else we'll
		 * run into problems with functions like get_cb_real_name from qq.c */
		member_name =   (bd->nickname != NULL && *(bd->nickname) != '\0') ?
				g_strdup_printf("%s (%u)", bd->nickname, bd->uid) :
				g_strdup_printf("(%u)", bd->uid);
		member_uid = g_strdup_printf("(%u)", bd->uid);

		flag = 0;
		/* TYPING to put online above OP and FOUNDER */
		if (is_online(bd->status)) flag |= (PURPLE_CBFLAGS_TYPING | PURPLE_CBFLAGS_VOICE);
		if(1 == (bd->role & 1)) flag |= PURPLE_CBFLAGS_OP;
		if(bd->uid == rmd->creator_uid) flag |= PURPLE_CBFLAGS_FOUNDER;

		is_find = TRUE;
		if (purple_conv_chat_find_user(PURPLE_CONV_CHAT(conv), member_name))
		{
			purple_conv_chat_user_set_flags(PURPLE_CONV_CHAT(conv),
					member_name,
					flag);
		} else if (purple_conv_chat_find_user(PURPLE_CONV_CHAT(conv), member_uid))
		{
			purple_conv_chat_user_set_flags(PURPLE_CONV_CHAT(conv),
					member_uid,
					flag);
			purple_conv_chat_rename_user(PURPLE_CONV_CHAT(conv), member_uid, member_name);
		} else {
			is_find = FALSE;
		}
		if (!is_find) {
			/* always put it even offline */
			names = g_list_append(names, member_name);
			flags = g_list_append(flags, GINT_TO_POINTER(flag));
		} else {
			g_free(member_name);
		}
		g_free(member_uid);
		list = list->next;
	}

	if (names != NULL && flags != NULL) {
		purple_conv_chat_add_users(PURPLE_CONV_CHAT(conv), names, NULL, flags, FALSE);
	}

	/* clean up names */
	while (names != NULL) {
		member_name = (gchar *) names->data;
		names = g_list_remove(names, member_name);
		g_free(member_name);
	}
	g_list_free(flags);
}

void qq_room_got_chat_in(PurpleConnection *gc,
		guint32 room_id, guint32 uid_from, const gchar *msg, time_t in_time)
{
	PurpleConversation *conv;
	qq_data *qd;
	qq_buddy_data *bd;
	qq_room_data *rmd;
	gchar *from;

	g_return_if_fail(gc != NULL && room_id != 0);
	g_return_if_fail(msg != NULL);

	qd = (qq_data *)gc->proto_data;
	conv = purple_find_chat(gc, room_id);
	rmd = qq_room_data_find(gc, room_id);
	g_return_if_fail(rmd != NULL);

	purple_debug_info("QQ", "is_show_chat:%d\n", qd->is_show_chat);
	if (NULL == conv && qd->is_show_chat) {
		conv = qq_room_conv_open(gc, rmd);
	}

	if (NULL == conv) {
		purple_debug_info("QQ", "Conversion of %u is not open, missing from %d:/n%s/v\n",
				room_id, uid_from, msg);
		return;
	}

	if (uid_from != 0) {

		bd = qq_room_buddy_find(rmd, uid_from);
		if (bd == NULL || bd->nickname == NULL)
			from = g_strdup_printf("%u", uid_from);
		else
			from = g_strdup(bd->nickname);
	} else {
		from = g_strdup("");
	}
	serv_got_chat_in(gc, room_id, from, 0, msg, in_time);
	g_free(from);
}

/* recv an IM from a group chat */
void qq_process_room_im(guint8 *data, gint data_len, guint32 id, PurpleConnection *gc, guint16 msg_type)
{
	gchar *msg_smiley, *msg_fmt, *msg_utf8;
	gint bytes, tail_len;
	struct {
		guint32 qun_id;
		guint8 type8;
		guint32 member_uid;
		guint16 msg_seq;
		time_t send_time;
		guint32 version;
		guint16 msg_len;
		GString *msg;
	} im_text;
	guint32 temp_id;
	guint8 has_font_attr;
	guint8 frag_count, frag_index;
	guint16 msg_id;
	qq_im_format *fmt = NULL;
	guint8 type;
	guint8 * msg_data;
	gchar * text;
	gchar * emoticon;
	gchar * purple_smiley;

	/* at least include im_text.msg_len */
	g_return_if_fail(data != NULL && data_len > 23);

	/* qq_show_packet("ROOM_IM", data, data_len); */
	memset(&im_text, 0, sizeof(im_text));
	bytes = 0;
	bytes += 4;	//00 00 00 00
	bytes += qq_get32(&(im_text.qun_id), data + bytes);
	bytes += qq_get8(&(im_text.type8), data + bytes);

	if(QQ_MSG_TEMP_ROOM_IM == msg_type) {
		bytes += qq_get32(&temp_id, data + bytes);
	}

	bytes += qq_get32(&(im_text.member_uid), bytes + data);
	bytes += 2;	//00 00
	bytes += qq_get16(&(im_text.msg_seq), data + bytes);
	bytes += qq_gettime(&im_text.send_time, data + bytes);
	bytes += qq_get32(&im_text.version, data + bytes);
	bytes += qq_get16(&(im_text.msg_len), data + bytes);
	purple_debug_info("QQ", "Room IM, qun id %u, seq %u, version 0x%04X, len %u\n",
		im_text.qun_id, im_text.msg_seq, im_text.version, im_text.msg_len);

	if (im_text.msg_len != data_len - bytes) {
		purple_debug_warning("QQ", "Room IM length %d should be %d\n",
			im_text.msg_len, data_len - bytes);
		im_text.msg_len = data_len - bytes;
	}

	g_return_if_fail(im_text.msg_len > 0 && bytes + im_text.msg_len <= data_len);
	if(msg_type != QQ_MSG_ROOM_IM_UNKNOWN) {
		g_return_if_fail(im_text.msg_len >= 10);

		bytes ++;		//Unknown 00
		bytes += qq_get8(&has_font_attr, data + bytes);
		bytes += qq_get8(&frag_count, data + bytes);
		bytes += qq_get8(&frag_index, data + bytes);
		bytes += qq_get16(&msg_id, data + bytes);
		bytes += 4;	/* skip 0x(00 00 00 00) */
		purple_debug_info("QQ", "Room IM, has_font_attr %d, fragment %d-%d, msg id %u\n",
			has_font_attr, frag_count, frag_index, msg_id);
		im_text.msg_len -= 10;
	}
	g_return_if_fail(im_text.msg_len > 0);

	/* qq_show_packet("Message", data + bytes, data_len - bytes); */

	switch (msg_type)
	{
	case QQ_MSG_ROOM_IM_52:
		{
			bytes += 8;		//4d 53 47 00 00 00 00 00		MSG.....
			bytes += qq_gettime(&im_text.send_time, data + bytes);
			bytes += 4;		//random guint32;

			if (has_font_attr)	{
				fmt = g_new0(qq_im_format, 1);
			
				bytes += 1;		//Unknown 0x00
				
				bytes += qq_get8(&fmt->rgb[2], data+bytes);
				bytes += qq_get8(&fmt->rgb[1], data+bytes);
				bytes += qq_get8(&fmt->rgb[0], data+bytes);

				bytes += qq_get8(&fmt->font_size, data+bytes);
				/* font attr guint8 : bold:00000001 XOR italic 00000010 XOR underline 00000100*/
				bytes += qq_get8(&fmt->attr, data+bytes);
				/* encoding, 0x8622=GB, 0x0000=EN */
				bytes += qq_get16(&fmt->charset, data+bytes);

				bytes += qq_get_vstr(&fmt->font, NULL, sizeof(guint16), data+bytes);
			} 

			bytes += 2;
			im_text.msg = g_string_new("");
			while (bytes < data_len) {
				bytes += qq_get8(&type, data+bytes);
				bytes += qq_get_vstr(&msg_data, NULL, sizeof(guint16), data+bytes);
				//bytes += msg_dataseg_len = qq_get_vstr(&msg_data, NULL, sizeof(guint16), data+bytes);
				//msg_dataseg_len -= sizeof(guint16);

				switch (type) {
				case 0x01:	//text
					qq_get_vstr(&text, NULL, sizeof(guint16), msg_data+1);		//+1 bypass msg_dataseg_flag 0x01
					g_free(msg_data);
					g_string_append(im_text.msg, text);
					g_free(text);
					break;
				case 0x02:	//emoticon
					qq_get_vstr(&emoticon, NULL, sizeof(guint16), msg_data+1);		//+1 bypass msg_dataseg_flag 0x01
					/* remained Unknown data is FF 00 02 14 XX ; FF Unknown msg_dataseg_flag */
					g_free(msg_data);
					purple_smiley = emoticon_get(*emoticon);
					if (purple_smiley == NULL) {
						purple_debug_info("QQ", "Not found smiley of 0x%02X\n", *emoticon);
						g_string_append(im_text.msg, "<IMG ID=\"0\">");
					} else {
						purple_debug_info("QQ", "Found 0x%02X smiley is %s\n", *emoticon, purple_smiley);
						g_string_append(im_text.msg, purple_smiley);
					}
					g_free(emoticon);
					break;
				case 03:	//image
					break;
					/*		it's kinda complicated, TOFIX later
					msg_dataseg_pos = 0;
					while (msg_dataseg_pos < msg_dataseg_len)
					{
						msg_dataseg_pos = qq_get8(&msg_dataseg_flag, msg_data);
						switch (msg_dataseg_flag)
						{
						case 0x0:
							break;
						}
					}	*/	
				}
			}

			if (fmt != NULL) {
				msg_utf8 = qq_im_fmt_to_purple(fmt, im_text.msg);
				qq_im_fmt_free(fmt);
			} else {
				msg_utf8 =  im_text.msg->str;
			}
			break;
		}
	case QQ_MSG_ROOM_IM_UNKNOWN:
	case QQ_MSG_ROOM_IM:
		{
			if (frag_count <= 1 || frag_count == frag_index + 1) {
				fmt = qq_im_fmt_new_default();
				tail_len = qq_get_im_tail(fmt, data + bytes, data_len - bytes);
				im_text.msg = g_string_new_len((gchar *)(data + bytes), data_len - tail_len);
			} else {
				im_text.msg = g_string_new_len((gchar *)(data + bytes), data_len - bytes);
			}

			/* group im_group has no flag to indicate whether it has font_attr or not */
			msg_smiley = qq_emoticon_to_purple(im_text.msg->str);
			if (fmt != NULL) {
				msg_fmt = qq_im_fmt_to_purple(fmt, g_string_new(msg_smiley));
				msg_utf8 =  qq_to_utf8(msg_fmt, QQ_CHARSET_DEFAULT);
				g_free(msg_fmt);
				qq_im_fmt_free(fmt);
			} else {
				msg_utf8 =  qq_to_utf8(msg_smiley, QQ_CHARSET_DEFAULT);
			}
			g_free(msg_smiley);
			break;
		}
	}

	purple_debug_info("QQ", "Room (%u) IM from %u: %s\n",
			im_text.qun_id, im_text.member_uid, msg_utf8);
 	qq_room_got_chat_in(gc, id, im_text.member_uid, msg_utf8, im_text.send_time);

	g_free(msg_utf8);
	g_string_free(im_text.msg, FALSE);
}

/* send IM to a group */
static void request_room_send_im(PurpleConnection *gc, guint32 room_id, qq_im_format *fmt, const GString *msg, guint8 frag_count, guint8 frag_index)
{
	guint8 raw_data[1024];
	gint bytes;
	time_t now;

	g_return_if_fail(room_id != 0 && msg != NULL);

	bytes = 0;
	/* type 0x0001, text only; 0x0002, with custom emoticon */
	bytes += qq_put16(raw_data + bytes, 0x0001);
	bytes += qq_put8(raw_data + bytes, frag_count);
	bytes += qq_put8(raw_data + bytes, frag_index);
	bytes += qq_put16(raw_data + bytes, 0x0000);		//maybe msg_id, but always be 0
	bytes += qq_put32(raw_data + bytes, 0);			/* 00 00 00 00 */

	bytes += qq_put32(raw_data + bytes, 0x4D534700);		//MSG...
	bytes += qq_put32(raw_data + bytes, 0);			/* 00 00 00 00 */
	now = time(NULL);
	bytes += qq_puttime(raw_data + bytes, &now);
	/* could be a random int */
	srand((unsigned)now);
	bytes += qq_put32(raw_data + bytes, rand());
	/* font attr set */
	bytes += qq_put8(raw_data + bytes, 0x00);
	bytes += qq_put8(raw_data + bytes, fmt->rgb[2]);
	bytes += qq_put8(raw_data + bytes, fmt->rgb[1]);
	bytes += qq_put8(raw_data + bytes, fmt->rgb[0]);
	bytes += qq_put8(raw_data + bytes, fmt->font_size);
	bytes += qq_put8(raw_data + bytes, fmt->attr);
	bytes += qq_put16(raw_data + bytes, fmt->charset);
	bytes += qq_put16(raw_data + bytes, fmt->font_len);
	bytes += qq_putdata(raw_data + bytes, fmt->font, fmt->font_len);

	bytes += qq_put16(raw_data + bytes, 0x0000);
	bytes += qq_putdata(raw_data + bytes, (guint8 *)msg->str, msg->len);

	qq_send_room_cmd(gc, QQ_ROOM_CMD_SEND_IM, room_id, raw_data, bytes);
}

/* this is the ACK */
void qq_process_room_send_im(PurpleConnection *gc, guint8 *data, gint len)
{
	/* response with the internal group id
	 * ignore it */
	return;
}

/* send a chat msg to a QQ Qun
 * called by purple */
int qq_chat_send(PurpleConnection *gc, int id, const char *what, PurpleMessageFlags flags)
{
	qq_data *qd;
	qq_im_format *fmt;
	gchar *msg_stripped, *tmp;
	GSList *segments, *it;
	gint msg_len;
	const gchar *start_invalid;
	gboolean is_smiley_none;
	guint8 frag_count, frag_index;

	g_return_val_if_fail(NULL != gc && NULL != gc->proto_data, -1);
	g_return_val_if_fail(id != 0 && what != NULL, -1);

	qd = (qq_data *) gc->proto_data;
	purple_debug_info("QQ", "Send chat IM to %u, len %" G_GSIZE_FORMAT ":\n%s\n", id, strlen(what), what);

	/* qq_show_packet("chat IM UTF8", (guint8 *)what, strlen(what)); */

	msg_stripped = purple_markup_strip_html(what);
	g_return_val_if_fail(msg_stripped != NULL, -1);
	/* qq_show_packet("IM Stripped", (guint8 *)what, strlen(what)); */

	/* Check and valid utf8 string */
	msg_len = strlen(msg_stripped);
	if (!g_utf8_validate(msg_stripped, msg_len, &start_invalid)) {
		if (start_invalid > msg_stripped) {
			tmp = g_strndup(msg_stripped, start_invalid - msg_stripped);
			g_free(msg_stripped);
			msg_stripped = g_strconcat(tmp, _("(Invalid UTF-8 string)"), NULL);
			g_free(tmp);
		} else {
			g_free(msg_stripped);
			msg_stripped = g_strdup(_("(Invalid UTF-8 string)"));
		}
	}

	is_smiley_none = qq_im_smiley_none(what);
	segments = qq_im_get_segments(msg_stripped, is_smiley_none);
	g_free(msg_stripped);

	if (segments == NULL) {
		return -1;
	}

	qd->send_im_id++;
	fmt = qq_im_fmt_new_by_purple(what);
	frag_count = g_slist_length(segments);
	frag_index = 0;
	for (it = segments; it; it = g_slist_delete_link(it, it)) {
		request_room_send_im(gc, id, fmt, (GString *)it->data, frag_count, frag_index);
		g_string_free(it->data, TRUE);
		frag_index++;
	}
	qq_im_fmt_free(fmt);
	return 1;
}
