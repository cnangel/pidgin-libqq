/**
 * @file buddy_memo.c
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
#include "debug.h"
#include "notify.h"
#include "request.h"

#include "buddy_memo.h"
#include "utils.h"
#include "packet_parse.h"
#include "buddy_list.h"
#include "buddy_info.h"
#include "char_conv.h"
#include "im.h"
#include "qq_define.h"
#include "qq_base.h"
#include "qq_network.h"
#include "qq.h"


#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include <stdlib.h>
#include <stdio.h>

/* memo index */
enum {
	QQ_MEMO_ALIAS = 0,
	QQ_MEMO_MOBILD,
	QQ_MEMO_TELEPHONE,
	QQ_MEMO_ADDRESS,
	QQ_MEMO_EMAIL,
	QQ_MEMO_ZIPCODE,
	QQ_MEMO_NOTE,
	QQ_MEMO_SIZE
};

/* memo id */
static const gchar *memo_id[] = {
	"mm_alias",
	"mm_mobile",
	"mm_telephone",
	"mm_address",
	"mm_email",
	"mm_zipcode",
	"mm_note"
};

/* memo text */
static const gchar *memo_txt[] = {
	N_("Alias"),
	N_("Mobile"),
	N_("Telephone"),
	N_("Address"),
	N_("Email"),
	N_("Postal Code"),
	N_("Note")
};

typedef struct _modify_memo_request {
	PurpleConnection *gc;
	guint32 bd_uid;
	gchar **segments;
} modify_memo_request;


static void memo_debug(gchar **segments)
{
	gint index;
	g_return_if_fail(NULL != segments);
	for (index = 0;  index < QQ_MEMO_SIZE; index++) {
		purple_debug_info("QQ","memo[%i]=%s\n", index, segments[index]);
	}
}

static void memo_free(gchar **segments)
{
	gint index;
	g_return_if_fail(NULL != segments);
	for (index = 0; index < QQ_MEMO_SIZE; index++) {
		g_free(segments[index]);
	}
	purple_debug_info("QQ", "memo freed\n");
}

static void update_buddy_alias(PurpleConnection *gc, guint32 bd_uid, gchar *alias)
{
	PurpleAccount *account;
	PurpleBuddy *buddy;
	gchar *who;
	g_return_if_fail(NULL != gc && NULL != alias);

	account = (PurpleAccount *)gc->account;
	g_return_if_fail(NULL != account);

	who = uid_to_purple_name(bd_uid);
	buddy = purple_find_buddy(account, who);
	if (buddy == NULL || purple_buddy_get_protocol_data(buddy) == NULL) {
		g_free(who);
		purple_debug_info("QQ", "Error...Can NOT find %d!\n", bd_uid);
		return;
	}
	purple_blist_alias_buddy(buddy, (const char*)alias);
}

static void request_change_memo(PurpleConnection *gc, guint32 bd_uid, gchar **segments)
{
	gint bytes;
	/* Attention, length of each segment must be guint8(0~255),
	 * so length of memo string is limited.
	 * convert it to guint8 first before putting data */
	guint seg_len;
	gint index;
	guint8 raw_data[MAX_PACKET_SIZE - 16] = {0};

	purple_debug_info( "QQ", "request_change_memo\n" );
	g_return_if_fail(NULL != gc && NULL != segments);

	bytes = 0;
	bytes += qq_put8(raw_data+bytes, QQ_BUDDY_MEMO_MODIFY);
	bytes += qq_put8(raw_data+bytes, 0x00);
	bytes += qq_put32(raw_data+bytes, (guint32)bd_uid);
	bytes += qq_put8(raw_data+bytes, 0x00);
	for (index = 0; index < QQ_MEMO_SIZE; index++) {
		seg_len = strlen(segments[index]);
		seg_len = seg_len & 0xff;
		bytes += qq_put8(raw_data+bytes, (guint8)seg_len);
		bytes += qq_putdata(raw_data+bytes, (const guint8 *)segments[index], (guint8)seg_len);
	}

	/* debug */
	/*
	   qq_show_packet("MEMO MODIFY", raw_data, bytes);
	   */

	qq_send_cmd(gc, QQ_CMD_BUDDY_MEMO, raw_data, bytes);
}

static void memo_modify_cancle_cb(modify_memo_request *memo_request, PurpleRequestFields *fields)
{
	memo_free(memo_request->segments);
	g_free(memo_request);
}

/* prepare segments to be sent, string all convert to qq charset */
static void memo_modify_ok_cb(modify_memo_request *memo_request, PurpleRequestFields *fields)
{
	PurpleConnection *gc;
	guint32 bd_uid;
	gchar **segments;
	const gchar *utf8_str;
	gchar *value = NULL;
	gint index;

	g_return_if_fail(NULL != memo_request);
	gc = (PurpleConnection *)memo_request->gc;
	segments = (gchar **)memo_request->segments;
	g_return_if_fail(NULL != gc && NULL != segments);
	bd_uid = (guint32)memo_request->bd_uid;


	for (index = 0; index < QQ_MEMO_SIZE; index++) {
		utf8_str = purple_request_fields_get_string(fields, memo_id[index]);
		/* update memo */
		if (QQ_MEMO_ALIAS == index) {
			update_buddy_alias(gc, bd_uid, segments[QQ_MEMO_ALIAS]);
		}
		if (NULL == utf8_str) {
			value = g_strdup("");
		}
		else {
			value = utf8_to_qq(utf8_str, QQ_CHARSET_DEFAULT);
			/* Warnning: value will be string "(NULL)" instead of NULL */
			if (!qq_strcmp("(NULL)", value)) {
				value = g_strdup("");
			}
		}
		g_free(segments[index]);
		segments[index] = value;
	}

	memo_debug(segments);
	/* send segments */
	request_change_memo(gc, bd_uid, segments);

	/* free segments */
	memo_free(segments);
	g_free(memo_request);
}

/* memo modify dialogue */
static void memo_modify_dialogue(PurpleConnection *gc, guint32 bd_uid, gchar **segments, guint32 action)
{
	modify_memo_request *memo_request;
	PurpleRequestField *field;
	PurpleRequestFields *fields;
	PurpleRequestFieldGroup *group;
	int index;
	gchar *utf8_title;
	gchar *utf8_primary;

	g_return_if_fail(NULL != gc && NULL != segments);

	switch (action) {
		case QQ_BUDDY_MEMO_MODIFY:
			/* keep one dialog once a time */
			purple_request_close_with_handle(gc);
			/* show dialog */
			fields = purple_request_fields_new();
			group = purple_request_field_group_new(NULL);
			purple_request_fields_add_group(fields, group);

			for(index = 0; index < QQ_MEMO_SIZE; index++) {
				/*
				   purple_debug_info("QQ", "id:%s txt:%s segment:%s\n",
				   memo_id[index], memo_txt[index], segments[index]);
				   */
				field = purple_request_field_string_new(memo_id[index], memo_txt[index],
						segments[index], FALSE);
				purple_request_field_group_add_field(group, field);
			}

			/* for upload cb */
			memo_request = g_new0(modify_memo_request, 1);
			memo_request->gc = gc;
			memo_request->bd_uid = bd_uid;
			memo_request->segments = segments;
			/* callback */
			utf8_title = g_strdup(_("Buddy Memo"));
			utf8_primary = g_strdup(_("Change his/her memo as you like"));

			purple_request_fields(gc, utf8_title, utf8_primary, NULL,
					fields,
					_("_Modify"), G_CALLBACK(memo_modify_ok_cb),
					_("_Cancel"), G_CALLBACK(memo_modify_cancle_cb),
					purple_connection_get_account(gc), NULL, NULL,
					memo_request);

			g_free(utf8_title);
			g_free(utf8_primary);
			break;
		default:
			purple_debug_info("QQ", "Error...unknown memo action, please tell us\n");
			break;
	}
}

static void qq_create_buddy_memo(PurpleConnection *gc, guint32 bd_uid, guint32 action)
{
	gchar **segments;
	gint index;
	g_return_if_fail(NULL != gc);

	segments = g_new0(gchar*, QQ_MEMO_SIZE);
	for (index = 0; index < QQ_MEMO_SIZE; index++) {
		segments[index] = g_strdup("");;
	}
	memo_modify_dialogue(gc, bd_uid, segments, action);
}

/* process reply to get_memo packet */
void qq_process_get_buddy_memo( PurpleConnection *gc, guint8* data, gint data_len, guint32 update_class, guint32 index )
{
	gchar **segments;
	gint bytes;
	guint8 rcv_cmd;
	guint32 rcv_uid;
	guint8 is_that_all;
	gchar * alias;
	qq_data * qd;
	guint i;

	g_return_if_fail(NULL != gc && NULL != data && 0 != data_len);

	//qq_show_packet("MEMO REACH", data, data_len);

	purple_debug_info("QQ", "index=0x%02X\n", index);

	bytes = 0;

	/* TX looks a bit clever than before... :) */
	bytes += qq_get8(&rcv_cmd, data+bytes);
	purple_debug_info("QQ", "rcv_cmd=0x%02X\n", rcv_cmd);

	switch (rcv_cmd) {
		case QQ_BUDDY_MEMO_MODIFY:
		case QQ_BUDDY_MEMO_REMOVE:
			bytes += qq_get8(&is_that_all, data+bytes);
			if (QQ_BUDDY_MEMO_REQUEST_SUCCESS == is_that_all) {
				purple_notify_message(gc, PURPLE_NOTIFY_MSG_INFO,
						_("Memo Modify"), _("Server says:"),
						_("Your request was accepted."),
						NULL, NULL);
				purple_debug_info("QQ", "memo change succeessfully!\n");
			}
			else {
				purple_notify_message(gc, PURPLE_NOTIFY_MSG_INFO,
						_("Memo Modify"), _("Server says:"),
						_("Your request was rejected."),
						NULL, NULL);
				purple_debug_info("QQ", "memo change failed\n");
			}
			break;
		case QQ_BUDDY_MEMO_GET:
			if (bytes == data_len)
			{
				qd = (qq_data *) gc->proto_data;
				qq_create_buddy_memo(gc, qd->uid, QQ_BUDDY_MEMO_MODIFY);
				break;
			}
			bytes += qq_get32(&rcv_uid, data+bytes);
			purple_debug_info("QQ", "rcv_uid=%u\n", rcv_uid);
			bytes += qq_get8(&is_that_all, data+bytes);
			purple_debug_info("QQ", "is_that_all=0x%02X\n", is_that_all);

			segments = g_new0(gchar*, QQ_MEMO_SIZE);

			for (i = 0; i < QQ_MEMO_SIZE; i++) {

				bytes += qq_get_vstr(&segments[i], NULL, sizeof(guint8), data+bytes);
				/*
			   purple_debug_info("QQ", "bytes:%d, seg:%s\n", bytes, segments[i]);
				  */
			}

			/* common action, update buddy memo */
			update_buddy_alias(gc, rcv_uid, segments[QQ_MEMO_ALIAS]);
			/* memo is thing that we regard our buddy as, so we need one more buddy_uid */
			/* action might be QQ_BUDDY_MEMO_MODIFY */
			memo_modify_dialogue(gc, rcv_uid, segments, i);
			g_free(segments);
			break;
		case QQ_BUDDY_MEMO_ALIAS:
			bytes += qq_get8(&is_that_all, data+bytes);
			purple_debug_info("QQ", "is_that_all=0x%02X\n", is_that_all);

			while (bytes < data_len)
			{
				bytes += qq_get32(&rcv_uid, data+bytes);
				purple_debug_info("QQ", "rcv_uid=%u\n", rcv_uid);
				bytes += qq_get_vstr(&alias, NULL, sizeof(guint8),data+bytes);
				update_buddy_alias(gc, rcv_uid, alias);
			}
			if (!is_that_all)
			{
				qq_request_buddy_memo(gc, index+1, 0, QQ_BUDDY_MEMO_ALIAS);
			}
			
			break;
		default:
			purple_debug_info("QQ", "received an UNKNOWN memo cmd!!!\n");
			break;
	}
}

/* request buddy memo */
void qq_request_buddy_memo( PurpleConnection *gc, guint32 index, guint32 update_class, guint8 action )
{
	guint8 raw_data[16] = {0};
	gint bytes;

	purple_debug_info("QQ", "qq_request_buddy_memo, index=%u, action=%u\n",
			index, action);
	g_return_if_fail(NULL != gc);
	/* '0' is ok
	   g_return_if_fail(uid != 0);
	   */
	bytes = 0;
	bytes += qq_put8(raw_data+bytes, action);
	if (action == QQ_BUDDY_MEMO_ALIAS)
	{
		bytes += qq_put8(raw_data+bytes, (guint8)index);
		/* need one byte of bd_index, just make a conversion */
	} else {
		bytes += qq_put32(raw_data+bytes, index);
		memset(raw_data+bytes, 0x00, 4);
		bytes += 4;
	}
	
	//qq_show_packet("MEMO REQUEST", raw_data, bytes);

	qq_send_cmd_mess(gc, QQ_CMD_BUDDY_MEMO, (guint8*)raw_data, bytes, update_class , index);
}


