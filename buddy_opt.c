/**
 * @file buddy_opt.c
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
#include "privacy.h"

#include "buddy_info.h"
#include "buddy_list.h"
#include "buddy_opt.h"
#include "char_conv.h"
#include "qq_define.h"
#include "im.h"
#include "qq_base.h"
#include "packet_parse.h"
#include "qq_network.h"
#include "utils.h"

#define PURPLE_GROUP_QQ_FORMAT          "QQ (%s)"

#define QQ_REMOVE_SELF_REPLY_OK       0x00

static void buddy_opt_req_free(qq_buddy_opt_req *opt_req)
{
	g_return_if_fail(opt_req != NULL);
	if (opt_req->no_auth)	g_free(opt_req->no_auth);
	if (opt_req->auth) g_free(opt_req->auth);
	if (opt_req->session) g_free(opt_req->session);
	if (opt_req->captcha_input) g_free(opt_req->captcha_input);
	g_free(opt_req);
}

static void buddy_req_cancel_cb(qq_buddy_opt_req *opt_req, const gchar *msg)
{
	g_return_if_fail(opt_req != NULL);
	buddy_opt_req_free(opt_req);
}

PurpleGroup *qq_group_find_or_new(const gchar *group_name)
{
	PurpleGroup *g;

	g_return_val_if_fail(group_name != NULL, NULL);

	g = purple_find_group(group_name);
	if (g == NULL) {
		g = purple_group_new(group_name);
		purple_blist_add_group(g, NULL);
		purple_debug_warning("QQ", "Add new group: %s\n", group_name);
	}

	return g;
}

static qq_buddy_data *qq_buddy_data_new(guint32 uid)
{
	qq_buddy_data *bd = g_new0(qq_buddy_data, 1);
	memset(bd, 0, sizeof(qq_buddy_data));
	bd->uid = uid;
	bd->status = QQ_BUDDY_OFFLINE;
	return bd;
}

qq_buddy_data *qq_buddy_data_find(PurpleConnection *gc, guint32 uid)
{
	gchar *who;
	PurpleBuddy *buddy;
	qq_buddy_data *bd;

	g_return_val_if_fail(gc != NULL, NULL);

	who = uid_to_purple_name(uid);
	if (who == NULL)	return NULL;
	buddy = purple_find_buddy(purple_connection_get_account(gc), who);
	g_free(who);

	if (buddy == NULL) {
		purple_debug_error("QQ", "Can not find purple buddy of %u\n", uid);
		return NULL;
	}

	if ((bd = purple_buddy_get_protocol_data(buddy)) == NULL) {
		purple_debug_error("QQ", "Can not find buddy data of %u\n", uid);
		return NULL;
	}
	return bd;
}

void qq_buddy_data_free(qq_buddy_data *bd)
{
	g_return_if_fail(bd != NULL);

	if (bd->nickname) g_free(bd->nickname);
	g_free(bd);
}

/* create purple buddy without data and display with no-auth icon */
PurpleBuddy * qq_buddy_new( PurpleConnection *gc, guint32 uid, PurpleGroup * group )
{
	PurpleBuddy *buddy;
	gchar *who;
	gchar * group_name;

	g_return_val_if_fail(gc->account != NULL && uid != 0, NULL);
	/* deprecated when fix qq_process_add_buddy_touch */
	if (!group)
	{
		group_name = g_strdup_printf(PURPLE_GROUP_QQ_FORMAT,
			purple_account_get_username(gc->account));
		group = qq_group_find_or_new(group_name);
		g_free(group_name);
		if (group == NULL) {
			purple_debug_error("QQ", "Failed creating group\n");
			return NULL;
		}
	}

	group_name = purple_group_get_name(group);
	purple_debug_info("QQ", "Add new purple buddy: [%u], at Group [%s]\n", uid, group_name);
	who = uid_to_purple_name(uid);
	buddy = purple_buddy_new(gc->account, who, NULL);	/* alias is NULL */
	purple_buddy_set_protocol_data(buddy, NULL);

	g_free(who);
	
	purple_blist_add_buddy(buddy, NULL, group, NULL);

	return buddy;
}

void qq_buddy_free(PurpleBuddy *buddy)
{
	qq_buddy_data *bd;

	g_return_if_fail(buddy);

	if ((bd = purple_buddy_get_protocol_data(buddy)) != NULL) {
		qq_buddy_data_free(bd);
	}
	purple_buddy_set_protocol_data(buddy, NULL);
	purple_blist_remove_buddy(buddy);
}

PurpleBuddy *qq_buddy_find(PurpleConnection *gc, guint32 uid)
{
	PurpleBuddy *buddy;
	gchar *who;

	g_return_val_if_fail(gc->account != NULL && uid != 0, NULL);

	who = uid_to_purple_name(uid);
	buddy = purple_find_buddy(gc->account, who);
	g_free(who);
	return buddy;
}

PurpleBuddy * qq_buddy_find_or_new( PurpleConnection *gc, guint32 uid, guint8 group_id)
{
	PurpleBuddy *buddy;
	qq_buddy_data *bd;
	qq_data *qd;
	GSList *l;
	qq_group * g;
	PurpleGroup * old_group;
	g_return_val_if_fail(gc->account != NULL && uid != 0, NULL);

	qd = (qq_data *)gc->proto_data;

	buddy = qq_buddy_find(gc, uid);
	/* group_id==0xFF only when add an unknown stranger */
	if (group_id==0xFF) {
		if (buddy)	goto buddy_data_check;
		else group_id=0;		//add stranger to group 0
	}

	/* find input group_id */
	for (l=qd->group_list; l; l=l->next)
	{
		if (((qq_group *)(l->data))->group_id == group_id) break;
	}

	/* if group_id found */
	if (l)	{
		if (buddy)		//if buddy already exist, we need check if he is in new group
		{	
			old_group = purple_buddy_get_group(buddy);
			g = (qq_group *)l->data;
			if (old_group != purple_find_group(g->group_name))
			{
				qq_buddy_free(buddy);
			} else	goto buddy_data_check;
		}
		old_group = purple_find_group(((qq_group *)(l->data))->group_name);
		buddy = qq_buddy_new(gc, uid, old_group);
	} else {
		if (group_id==0)
		{
			if (!buddy)
				buddy = qq_buddy_new(gc, uid, NULL);
			goto buddy_data_check;
		}
		purple_debug_error("QQ","cannot find group id: %u", group_id);
		return NULL;
	}

buddy_data_check:
	if (purple_buddy_get_protocol_data(buddy) != NULL)
		return buddy;

	bd = qq_buddy_data_new(uid);
	purple_buddy_set_protocol_data(buddy, bd);
	return buddy;
}

/* send packet to remove a buddy from my buddy list */
static void qq_request_remove_buddy(PurpleConnection *gc, qq_buddy_opt_req *opt_req)
{
	gint bytes;
	guint8 *raw_data;
	gchar * uid_str;

	g_return_if_fail(opt_req && opt_req->uid != 0);
	g_return_if_fail(opt_req->auth != NULL && opt_req->auth_len > 0);

	raw_data = g_newa(guint8, opt_req->auth_len + sizeof(uid_str));
	bytes = 0;
	bytes += qq_put8(raw_data + bytes, opt_req->auth_len);
	bytes += qq_putdata(raw_data + bytes, opt_req->auth, opt_req->auth_len);

	uid_str = uid_to_purple_name(opt_req->uid);
	bytes += qq_putdata(raw_data + bytes, (guint8 *)uid_str, strlen(uid_str));
	g_free(uid_str);

	qq_send_cmd_mess(gc, QQ_CMD_REMOVE_BUDDY, raw_data, bytes, 0, opt_req->uid);
	
	buddy_opt_req_free(opt_req);
}

void qq_request_auth_token( PurpleConnection *gc, guint8 cmd, guint16 sub_cmd, guint32 dataptr2ship, qq_buddy_opt_req *opt_req )
{
	guint8 raw_data[128];
	gint bytes;

	g_return_if_fail(opt_req && opt_req->uid > 0);
	bytes = 0;
	bytes += qq_put8(raw_data + bytes, cmd);
	bytes += qq_put16(raw_data + bytes, sub_cmd);
	bytes += qq_put32(raw_data + bytes, opt_req->uid);

	if (opt_req->captcha_input && opt_req->session)
	{	
		bytes += qq_put_vstr(raw_data+bytes, opt_req->captcha_input, sizeof(guint16), NULL);
		bytes += qq_put16(raw_data+bytes, opt_req->session_len);
		bytes += qq_putdata(raw_data+bytes, opt_req->session, opt_req->session_len);
	}
	
	qq_send_cmd_mess(gc, QQ_CMD_AUTH_TOKEN, raw_data, bytes, dataptr2ship, (guintptr)opt_req);
}

void qq_process_auth_token( PurpleConnection *gc, guint8 *data, gint data_len, guint32 dataptr, qq_buddy_opt_req *opt_req )
{
	gint bytes;
	guint8 cmd, reply;
	guint16 sub_cmd;
	guint8 *code = NULL;
	guint16 code_len = 0;

	g_return_if_fail(data != NULL && data_len != 0);
	g_return_if_fail(opt_req && opt_req->uid != 0);

	//qq_show_packet("qq_process_auth_token", data, data_len);
	bytes = 0;
	bytes += qq_get8(&cmd, data + bytes);
	bytes += qq_get16(&sub_cmd, data + bytes);
	bytes += qq_get8(&reply, data + bytes);

	/* if reply == 0x01, we need request captcha */
	if (reply)
	{
		/* if this is end, means you have submitted the wrong captcha  */
		if (bytes>=data_len)
		{
			qq_request_auth_token(gc, QQ_AUTH_INFO_BUDDY, QQ_AUTH_INFO_ADD_BUDDY, 0, opt_req);
			return;
		}

		bytes += qq_get_vstr(&code, NULL, sizeof(guint16), data + bytes);
		purple_util_fetch_url_request(
			(gchar *)code, TRUE, NULL, TRUE,  NULL, TRUE, auth_token_captcha_input_cb, opt_req);
		return;
	}
	
	bytes += qq_get16(&opt_req->auth_len, data + bytes);
	g_return_if_fail(opt_req->auth_len > 0);
	g_return_if_fail(bytes + opt_req->auth_len <= data_len);
	opt_req->auth = g_new0(guint8, opt_req->auth_len);
	bytes += qq_getdata(opt_req->auth, opt_req->auth_len, data + bytes);

	if (cmd == QQ_AUTH_INFO_BUDDY && sub_cmd == QQ_AUTH_INFO_REMOVE_BUDDY) {
		qq_request_remove_buddy(gc, opt_req);
		return;
	}
	if (sub_cmd == QQ_AUTH_INFO_ADD_BUDDY) {
		if (opt_req->auth_type == 0x01)
			add_buddy_authorize_input(gc, opt_req);
		else if (opt_req->auth_type == 0x00)
			qq_request_search_uid(gc, opt_req);
		return;
	}
	if (cmd == QQ_AUTH_INFO_BUDDY && sub_cmd == QQ_AUTH_INFO_UPDATE_BUDDY_INFO) {
		request_change_info(gc, (guint8 *)dataptr, code, code_len);
		return;
	}
	purple_debug_info("QQ", "Got auth info cmd 0x%x, sub 0x%x, reply 0x%x\n",
			cmd, sub_cmd, reply);
}

static void auth_token_captcha_input_cancel_cb(qq_buddy_opt_req *opt_req,
	PurpleRequestFields *fields)
{
}

static void auth_token_captcha_input_ok_cb(qq_buddy_opt_req *opt_req,
	PurpleRequestFields *fields)
{
	g_return_if_fail(opt_req != NULL && opt_req->gc != NULL && opt_req->uid != 0);

	opt_req->captcha_input = purple_request_fields_get_string(fields, "captcha_code");

	if (! strlen(opt_req->captcha_input) > 0) {
		auth_token_captcha_input_cancel_cb(opt_req, fields);
		return;
	}

	qq_request_auth_token(opt_req->gc, 0x02, QQ_AUTH_INFO_ADD_BUDDY, 0, opt_req);
}

void auth_token_captcha_input_cb(PurpleUtilFetchUrlData *url_data, 
	gpointer user_data, const gchar *url_text, gsize len, const gchar *error_message)
{
	PurpleAccount *account;
	PurpleRequestFields *fields;
	PurpleRequestFieldGroup *group;
	PurpleRequestField *field;
	qq_buddy_opt_req *opt_req = (qq_buddy_opt_req *)user_data;
	gchar *end_of_headers, *p;
	guint header_len, content_len;

	g_return_if_fail(opt_req && opt_req->gc && opt_req->uid>0 );
	g_return_if_fail(url_text && len>0);

	account = purple_connection_get_account(opt_req->gc);

	end_of_headers = strstr(url_text, "\r\n\r\n");
	if (end_of_headers) {
		header_len = (end_of_headers + 4 - url_text);
	}

	p = find_header_content(url_text, header_len, "\nContent-Length: ", sizeof("\nContent-Length: ") - 1);
	if (p) {
		sscanf(p, "%" G_GSIZE_FORMAT, &content_len);
	} else {
		purple_debug_error("QQ", "can not parse http header, maybe it's chunked!");
	}

	p = find_header_content(url_text, header_len, "\ngetqqsession: ", sizeof("\ngetqqsession: ") - 1);
	if (!p) purple_debug_error("QQ", "can not find qqsession in http header!");
	
	opt_req->session_len = strstr(p, "\r\n")-p;
	opt_req->session = g_new0(guint8, opt_req->session_len);
	g_memmove(opt_req->session, p, opt_req->session_len);

	fields = purple_request_fields_new();
	group = purple_request_field_group_new(NULL);
	purple_request_fields_add_group(fields, group);

	field = purple_request_field_image_new("captcha_img",
		_("Captcha Image"), url_text+header_len, content_len);
	purple_request_field_group_add_field(group, field);

	field = purple_request_field_string_new("captcha_code",
		_("Enter code"), "", FALSE);
	purple_request_field_string_set_masked(field, FALSE);
	purple_request_field_group_add_field(group, field);

	purple_request_fields(account,
		_("QQ Captcha Verification"),
		_("QQ Captcha Verification"),
		_("Enter the text from the image"),
		fields,
		_("OK"), G_CALLBACK(auth_token_captcha_input_ok_cb),
		_("Cancel"), G_CALLBACK(auth_token_captcha_input_cancel_cb),
		account, NULL, NULL,
		opt_req);
}

static void add_buddy_question_cb(qq_buddy_opt_req *opt_req, const gchar *text)
{
	g_return_if_fail(opt_req != NULL);
	if (opt_req->gc == NULL || opt_req->uid == 0) {
		buddy_opt_req_free(opt_req);
		return;
	}

	qq_request_question(opt_req->gc, QQ_QUESTION_ANSWER, opt_req->uid, NULL, text);
	buddy_opt_req_free(opt_req);
}

static void add_buddy_question_input(PurpleConnection *gc, guint32 uid, gchar *question)
{
	gchar *who, *msg;
	qq_buddy_opt_req *opt_req;
	g_return_if_fail(uid != 0);

	opt_req = g_new0(qq_buddy_opt_req, 1);
	opt_req->gc = gc;
	opt_req->uid = uid;
	opt_req->auth = NULL;
	opt_req->auth_len = 0;

	who = uid_to_purple_name(uid);
	msg = g_strdup_printf(_("%u requires verification: %s"), uid, question);
	purple_request_input(gc, _("Add buddy question"), msg,
			_("Enter answer here"),
			NULL,
			TRUE, FALSE, NULL,
			_("Send"), G_CALLBACK(add_buddy_question_cb),
			_("Cancel"), G_CALLBACK(buddy_req_cancel_cb),
			purple_connection_get_account(gc), who, NULL,
			opt_req);

	g_free(msg);
	g_free(who);
}

void qq_request_question(PurpleConnection *gc,
		guint8 cmd, guint32 uid, const gchar *question_utf8, const gchar *answer_utf8)
{
	guint8 raw_data[MAX_PACKET_SIZE - 16];
	gint bytes;

	g_return_if_fail(uid > 0);
	bytes = 0;
	bytes += qq_put8(raw_data + bytes, cmd);
	if (cmd == QQ_QUESTION_GET) {
		bytes += qq_put8(raw_data + bytes, 0);
		qq_send_cmd_mess(gc, QQ_CMD_BUDDY_QUESTION, raw_data, bytes, 0, uid);
		return;
	}
	if (cmd == QQ_QUESTION_SET) {
		bytes += qq_put_vstr(raw_data + bytes, question_utf8, sizeof(guint8), QQ_CHARSET_DEFAULT);
		bytes += qq_put_vstr(raw_data + bytes, answer_utf8, sizeof(guint8), QQ_CHARSET_DEFAULT);
		bytes += qq_put8(raw_data + bytes, 0);
		qq_send_cmd_mess(gc, QQ_CMD_BUDDY_QUESTION, raw_data, bytes, 0, uid);
		return;
	}
	/* Unknow 2 bytes, 0x(00 01) */
	bytes += qq_put8(raw_data + bytes, 0x00);
	bytes += qq_put8(raw_data + bytes, 0x01);
	g_return_if_fail(uid != 0);
	bytes += qq_put32(raw_data + bytes, uid);
	if (cmd == QQ_QUESTION_REQUEST) {
		qq_send_cmd_mess(gc, QQ_CMD_BUDDY_QUESTION, raw_data, bytes, 0, uid);
		return;
	}
	bytes += qq_put_vstr(raw_data + bytes, answer_utf8, sizeof(guint8), QQ_CHARSET_DEFAULT);
	bytes += qq_put8(raw_data + bytes, 0);
	qq_send_cmd_mess(gc, QQ_CMD_BUDDY_QUESTION, raw_data, bytes, 0, uid);
	return;
}

static void qq_request_add_buddy_by_question(PurpleConnection *gc, guint32 uid,
	guint8 *code, guint16 code_len)
{
	guint8 raw_data[MAX_PACKET_SIZE - 16];
	gint bytes = 0;

	g_return_if_fail(uid != 0 && code_len > 0);

	bytes = 0;
	bytes += qq_put8(raw_data + bytes, 0x10);
	bytes += qq_put32(raw_data + bytes, uid);
	bytes += qq_put16(raw_data + bytes, 0);

	bytes += qq_put8(raw_data + bytes, 0);
	bytes += qq_put8(raw_data + bytes, 0);	/* no auth code */

	bytes += qq_put16(raw_data + bytes, code_len);
	bytes += qq_putdata(raw_data + bytes, code, code_len);

	bytes += qq_put8(raw_data + bytes, 1);	/* ALLOW ADD ME FLAG */
	bytes += qq_put8(raw_data + bytes, 0);	/* group number? */
	qq_send_cmd(gc, QQ_CMD_ADD_BUDDY_POST, raw_data, bytes);
}

void qq_process_question(PurpleConnection *gc, guint8 *data, gint data_len, guint32 uid)
{
	gint bytes;
	guint8 cmd, reply;
	gchar *question, *answer;
	guint16 code_len;
	guint8 *code;

	g_return_if_fail(data != NULL && data_len != 0);

	qq_show_packet("qq_process_question", data, data_len);
	bytes = 0;
	bytes += qq_get8(&cmd, data + bytes);
	if (cmd == QQ_QUESTION_GET) {
		bytes += qq_get_vstr(&question, QQ_CHARSET_DEFAULT, sizeof(guint8), data + bytes);
		bytes += qq_get_vstr(&answer, QQ_CHARSET_DEFAULT, sizeof(guint8), data + bytes);
		purple_debug_info("QQ", "Get buddy adding Q&A:\n%s\n%s\n", question, answer);
		g_free(question);
		g_free(answer);
		return;
	}
	if (cmd == QQ_QUESTION_SET) {
		bytes += qq_get8(&reply, data + bytes);
		if (reply == 0) {
			purple_debug_info("QQ", "Successed setting Q&A\n");
		} else {
			purple_debug_warning("QQ", "Failed setting Q&A, reply %d\n", reply);
		}
		return;
	}

	g_return_if_fail(uid != 0);
	bytes += 2; /* skip 2 bytes, 0x(00 01)*/
	if (cmd == QQ_QUESTION_REQUEST) {
		bytes += qq_get8(&reply, data + bytes);
		if (reply == 0x01) {
			purple_debug_warning("QQ", "Failed getting question, reply %d\n", reply);
			return;
		}
		bytes += qq_get_vstr(&question, QQ_CHARSET_DEFAULT, sizeof(guint8), data + bytes);
		purple_debug_info("QQ", "Get buddy question:\n%s\n", question);
		add_buddy_question_input(gc, uid, question);
		g_free(question);
		return;
	}

	if (cmd == QQ_QUESTION_ANSWER) {
		bytes += qq_get8(&reply, data + bytes);
		if (reply == 0x01) {
			purple_notify_error(gc, _("Add Buddy"), _("Invalid answer."), NULL);
			return;
		}
		bytes += qq_get16(&code_len, data + bytes);
		g_return_if_fail(code_len > 0);
		g_return_if_fail(bytes + code_len <= data_len);

		code = g_newa(guint8, code_len);
		bytes += qq_getdata(code, code_len, data + bytes);
		qq_request_add_buddy_by_question(gc, uid, code, code_len);
		return;
	}

	g_return_if_reached();
}

/* try to add a buddy without authentication */
static void qq_request_add_buddy_touch(PurpleConnection *gc, qq_buddy_opt_req *opt_req)
{
	guint bytes;
	guint8 raw_data[16];

	g_return_if_fail(opt_req && opt_req->uid > 0);

	bytes = 0;
	bytes += qq_put32(raw_data + bytes, opt_req->uid);
	qq_send_cmd_mess(gc, QQ_CMD_ADD_BUDDY_TOUCH, raw_data, bytes, 0, (guintptr)opt_req);
}

void qq_request_add_buddy_post(PurpleConnection *gc, qq_buddy_opt_req *opt_req, const gchar *text)
{
	static guint8 fill1[] = {
		0x00, 0x0A, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
	};
	static guint8 fill2[] = {
		0x00, 0x0C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
	};
	guint8 raw_data[256];
	gint bytes = 0;
	guint8 cmd;

	g_return_if_fail(opt_req && opt_req->uid!= 0);

	switch (opt_req->auth_type)
	{
	case 0x01:	//auth
		cmd = 0x02;
		break;
	case 0x02:	//question
		cmd = 0x10;
		break;
	case 0x00:	//no_auth
	case 0x03:	//approve and add
	case 0x04:	//approve
	case 0x05:	//reject
		cmd = opt_req->auth_type;
		break;
	}
	
	bytes = 0;
	bytes += qq_put8(raw_data + bytes, cmd);
	bytes += qq_put32(raw_data + bytes, opt_req->uid);

	if (cmd == 0x03 || cmd == 0x04 || cmd == 0x05)
	{
		bytes += qq_put16(raw_data + bytes, 0);
	} else {
		if (opt_req->no_auth && opt_req->no_auth_len > 0) {
			bytes += qq_put16(raw_data + bytes, opt_req->no_auth_len);
			bytes += qq_putdata(raw_data + bytes, opt_req->no_auth, opt_req->no_auth_len);
		} else	bytes += qq_put16(raw_data + bytes, 0);

		if (opt_req->auth == NULL || opt_req->auth_len <= 0) {
			bytes += qq_put16(raw_data + bytes, 0);
		} else {
			bytes += qq_put16(raw_data + bytes, opt_req->auth_len);
			bytes += qq_putdata(raw_data + bytes, opt_req->auth, opt_req->auth_len);
		}
		bytes += qq_put8(raw_data + bytes, 1);	/* ALLOW ADD ME FLAG */
	}

	bytes += qq_put8(raw_data + bytes, opt_req->group_id);	/* group number */

	if (text) {
		bytes += qq_put8(raw_data + bytes, strlen(text));
		bytes += qq_putdata(raw_data + bytes, (guint8 *)text, strlen(text));
	}

	if (cmd == 0x03 || cmd == 0x04 || cmd == 0x05)
		bytes += qq_putdata(raw_data + bytes, fill2, sizeof(fill2));
	else
		bytes += qq_putdata(raw_data + bytes, fill1, sizeof(fill1));

	qq_send_cmd_mess(gc, QQ_CMD_ADD_BUDDY_POST, raw_data, bytes, 0, opt_req->auth_type);

	buddy_opt_req_free(opt_req);
}

void qq_process_add_buddy_post( PurpleConnection *gc, guint8 *data, gint data_len, guintptr auth_type )
{
	guint32 uid;
	g_return_if_fail(data != NULL && data_len != 0);

	qq_get32(&uid, data+1);
	//qq_show_packet("qq_process_add_buddy_post", data, data_len);

	if (auth_type == 0)
	{
		qq_buddy_find_or_new(gc, uid, 0xFF);
		qq_request_get_buddy_info(gc, uid, 0, 0);
		qq_request_get_buddies_online(gc, 0, 0);
		qq_request_get_level(gc, uid);
	}
}

static void add_buddy_auth_cb(qq_buddy_opt_req *opt_req, const gchar *text)
{
	qq_data *qd;
	g_return_if_fail(opt_req != NULL && opt_req->gc);

	qd = (qq_data *)opt_req->gc->proto_data;
	qq_request_add_buddy_post(opt_req->gc, opt_req, text);
}

/* the real packet to reject and request is sent from here */
static void buddy_add_deny_reason_cb(qq_buddy_opt_req *opt_req, const gchar *reason)
{
	g_return_if_fail(opt_req != NULL);
	if (opt_req->gc == NULL || opt_req->uid == 0) {
		buddy_opt_req_free(opt_req);
		return;
	}

	opt_req->auth_type = 0x05;

	qq_request_add_buddy_post(opt_req->gc, opt_req, reason);
}

static void buddy_add_deny_noreason_cb(qq_buddy_opt_req *opt_req)
{
	buddy_add_deny_reason_cb(opt_req, NULL);
}

/* we approve other's request of adding me as friend */
static void buddy_add_authorize_cb(gpointer data)
{
	qq_buddy_opt_req *opt_req = (qq_buddy_opt_req *)data;

	g_return_if_fail(opt_req != NULL);
	if (opt_req->gc == NULL || opt_req->uid == 0) {
		buddy_opt_req_free(opt_req);
		return;
	}

	opt_req->auth_type = 0x03;

	qq_request_add_buddy_post(opt_req->gc, opt_req, NULL);
}

/* we reject other's request of adding me as friend */
static void buddy_add_deny_cb(gpointer data)
{
	qq_buddy_opt_req *opt_req = (qq_buddy_opt_req *)data;
	gchar *who = uid_to_purple_name(opt_req->uid);
	purple_request_input(opt_req->gc, NULL, _("Authorization denied message:"),
			NULL, _("Sorry, you're not my style."), TRUE, FALSE, NULL,
			_("OK"), G_CALLBACK(buddy_add_deny_reason_cb),
			_("Cancel"), G_CALLBACK(buddy_add_deny_noreason_cb),
			purple_connection_get_account(opt_req->gc), who, NULL,
			opt_req);
	g_free(who);
}

static void add_buddy_no_auth_cb(qq_buddy_opt_req *opt_req)
{
	qq_data *qd;
	g_return_if_fail(opt_req != NULL);
	if (opt_req->gc == NULL || opt_req->uid == 0) {
		buddy_opt_req_free(opt_req);
		return;
	}

	qd = (qq_data *) opt_req->gc->proto_data;
	qq_request_add_buddy_touch(opt_req->gc, opt_req);
}

void add_buddy_authorize_input( PurpleConnection *gc, qq_buddy_opt_req *opt_req )
{
	gchar *who, *msg;
	g_return_if_fail(opt_req && opt_req->uid != 0 && opt_req->auth && opt_req->auth_len > 0);

	who = uid_to_purple_name(opt_req->uid);
	msg = g_strdup_printf(_("%s needs authorization"), who);
	purple_request_input(gc, _("Add buddy authorize"), msg,
			_("Enter request here"),
			_("Would you be my friend?"),
			TRUE, FALSE, NULL,
			_("Send"), G_CALLBACK(add_buddy_auth_cb),
			_("Cancel"), G_CALLBACK(buddy_req_cancel_cb),
			purple_connection_get_account(gc), who, NULL,
			opt_req);

	g_free(msg);
	g_free(who);
}

/* add a buddy and send packet to QQ server
 * note that when purple load local cached buddy list into its blist
 * it also calls this funtion, so we have to
 * define qd->is_login=TRUE AFTER LOGIN */
void qq_add_buddy(PurpleConnection *gc, PurpleBuddy *buddy, PurpleGroup *group)
{
	qq_data *qd;
	gchar * group_name;
	qq_buddy_opt_req *opt_req;

	g_return_if_fail(NULL != gc && NULL != gc->proto_data);
	g_return_if_fail(buddy != NULL);

	qd = (qq_data *) gc->proto_data;
	if (!qd->is_login)
		return;		/* IMPORTANT ! */

	if (purple_buddy_get_protocol_data(buddy))
	{
		purple_notify_error(gc, _("QQ Buddy"), _("Add buddy"), _("Buddy exists"));
		return;
	}

	/* free it in qq_request_add_buddy_touch */
	opt_req = g_new0(qq_buddy_opt_req, 1);
	opt_req->gc = gc;
	opt_req->uid = purple_name_to_uid(purple_buddy_get_name(buddy));

	if (group)
	{
		group_name = purple_group_get_name(group);
		if (!group_name)
		{
			purple_notify_error(gc, _("QQ Buddy"), _("Add buddy"), _("Group not exists"));
			goto free;
		}
		opt_req->group_id = group_name_to_id(gc, group_name);
		if (opt_req->group_id == 0xFF)
		{
			purple_notify_error(gc, _("QQ Buddy"), _("Add buddy"), _("Chosen Group not associated with this account"));
			goto free;
		}
	} else opt_req->group_id = 0;
	

	if (opt_req->uid > 0) {
		qq_request_add_buddy_touch(gc, opt_req);
		return;
	}

	purple_notify_error(gc, _("QQ Buddy"), _("Add buddy"), _("Invalid QQ Number"));

free:
	if (buddy == NULL)
		return;
	qq_buddy_free(buddy);
}

/* process the server reply for my request to remove a buddy */
void qq_process_remove_buddy(PurpleConnection *gc, guint8 *data, gint data_len, guint32 uid)
{
	PurpleBuddy *buddy = NULL;
	gchar *msg;

	g_return_if_fail(data != NULL && data_len != 0);
	g_return_if_fail(uid != 0);

	buddy = qq_buddy_find(gc, uid);
	if (data[0] != 0) {
		msg = g_strdup_printf(_("Failed removing buddy %u"), uid);
		purple_notify_info(gc, _("QQ Buddy"), msg, NULL);
		g_free(msg);
	}

	purple_debug_info("QQ", "Reply OK for removing buddy\n");
	/* remove buddy again */
	if (buddy != NULL) {
		qq_buddy_free(buddy);
	}
}

/* process the server reply for my request to remove myself from a buddy */
void qq_process_buddy_remove_me(PurpleConnection *gc, guint8 *data, gint data_len, guint32 uid)
{
	gchar *msg;

	g_return_if_fail(data != NULL && data_len != 0);

	if (data[0] == 0) {
		purple_debug_info("QQ", "Reply OK for removing me from %u's buddy list\n", uid);
		return;
	}
	msg = g_strdup_printf(_("Failed removing me from %d's buddy list"), uid);
	purple_notify_info(gc, _("QQ Buddy"), msg, NULL);
	g_free(msg);
}

void qq_process_add_buddy_touch( PurpleConnection *gc, guint8 *data, gint data_len, qq_buddy_opt_req *opt_req )
{
	qq_data *qd;
	gint bytes;
	guint32 dest_uid;
	guint8 reply;

	g_return_if_fail(data != NULL && data_len >= 5);
	g_return_if_fail(opt_req && opt_req->uid != 0);

	qd = (qq_data *) gc->proto_data;

	purple_debug_info("QQ", "Process buddy add no auth for id [%u]\n", opt_req->uid);
	qq_show_packet("buddy_add_no_auth_ex", data, data_len);

	bytes = 0;
	bytes += qq_get32(&dest_uid, data + bytes);
	bytes += qq_get8(&reply, data + bytes);

	g_return_if_fail(dest_uid == opt_req->uid);

	if (reply == 0x99) {
		purple_debug_info("QQ", "Successfully added buddy %u\n", opt_req->uid);
		qq_buddy_find_or_new(gc, opt_req->uid, opt_req->group_id);
		qq_request_get_buddy_info(gc, opt_req->uid, 0, 0);
		qq_request_get_level(gc, opt_req->uid);
		qq_request_get_buddies_online(gc, 0, 0);
		return;
	}

	if (reply != 0) {
		purple_debug_info("QQ", "Failed adding buddy %u, Unknown reply 0x%02X\n",
			opt_req->uid, reply);
	}

	/* need auth */
	g_return_if_fail(data_len > bytes);
	bytes += qq_get8(&opt_req->auth_type, data + bytes);
	purple_debug_warning("QQ", "Adding buddy needs authorize 0x%02X\n", opt_req->auth_type);

	switch (opt_req->auth_type) {
		case 0x00:	/* no authorize */
		case 0x01:	/* authorize */
			qq_request_auth_token(gc, QQ_AUTH_INFO_BUDDY, QQ_AUTH_INFO_ADD_BUDDY, 0, opt_req);
			break;
		case 0x02:	/* disable */
			break;
		case 0x03:	/* answer question */
			qq_request_question(gc, QQ_QUESTION_REQUEST, opt_req->uid, NULL, NULL);
			break;
		case 0x04: /* deny! */
			break;
		default:
			g_return_if_reached();
			break;
	}
	return;
}

/* remove a buddy and send packet to QQ server accordingly */
void qq_remove_buddy(PurpleConnection *gc, PurpleBuddy *buddy, PurpleGroup *group)
{
	qq_data *qd;
	qq_buddy_data *bd;
	qq_buddy_opt_req *opt_req;

	g_return_if_fail(gc != NULL && gc->proto_data != NULL);
	g_return_if_fail(buddy != NULL);

	qd = (qq_data *) gc->proto_data;
	if (!qd->is_login)
		return;

	/* free it in qq_request_remove_buddy */
	opt_req = g_new0(qq_buddy_opt_req, 1);
	opt_req->gc = gc;
	opt_req->uid = purple_name_to_uid(purple_buddy_get_name(buddy));
	
	if (opt_req->uid  > 0 && opt_req->uid  != qd->uid) {
			qq_request_auth_token(gc, QQ_AUTH_INFO_BUDDY, QQ_AUTH_INFO_REMOVE_BUDDY, 0, opt_req);
	}

	if ((bd = purple_buddy_get_protocol_data(buddy)) != NULL) {
		qq_buddy_data_free(bd);
		purple_buddy_set_protocol_data(buddy, NULL);
	} else {
		purple_debug_warning("QQ", "Empty buddy data of %s\n", purple_buddy_get_name(buddy));
	}
}

static void buddy_add_input(PurpleConnection *gc, guint32 uid, gchar *reason)
{
	PurpleAccount *account = purple_connection_get_account(gc);
	qq_buddy_opt_req *opt_req;
	gchar *who;

	g_return_if_fail(uid != 0 && reason != NULL);

	purple_debug_info("QQ", "Buddy %u request adding, msg: %s\n", uid, reason);

	/* free it in qq_process_add_buddy_post */
	opt_req = g_new0(qq_buddy_opt_req, 1);
	opt_req->gc = gc;
	opt_req->uid = uid;

	if (purple_prefs_get_bool("/plugins/prpl/qq/auto_get_authorize_info")) {
		qq_request_get_buddy_info(gc, opt_req->uid, 0, QQ_BUDDY_INFO_DISPLAY);
	}
	who = uid_to_purple_name(opt_req->uid);

	purple_account_request_authorization(account,
	 		who, NULL,
			NULL, reason,
			purple_find_buddy(account, who) != NULL,
			buddy_add_authorize_cb,
			buddy_add_deny_cb,
			opt_req);

	g_free(who);
}

/* no longer needed
void qq_process_buddy_check_code(PurpleConnection *gc, guint8 *data, gint data_len)
{
	gint bytes;
	guint8 cmd;
	guint8 reply;
	guint32 uid;
	guint16 flag1, flag2;

	g_return_if_fail(data != NULL && data_len >= 5);

	qq_show_packet("buddy_check_code", data, data_len);

	bytes = 0;
	bytes += qq_get8(&cmd, data + bytes);		//0x03
	bytes += qq_get8(&reply, data + bytes);

	if (reply == 0) {
		purple_debug_info("QQ", "Failed checking code\n");
		return;
	}

	bytes += qq_get32(&uid, data + bytes);
	g_return_if_fail(uid != 0);
	bytes += qq_get16(&flag1, data + bytes);
	bytes += qq_get16(&flag2, data + bytes);
	purple_debug_info("QQ", "Check code reply Ok, uid %u, flag 0x%04X-0x%04X\n",
			uid, flag1, flag2);
	return;
}

static void request_buddy_check_code(PurpleConnection *gc,
		gchar *from, guint8 *code, gint code_len)
{
	guint8 *raw_data;
	gint bytes;
	guint32 uid;

	g_return_if_fail(code != NULL && code_len > 0 && from != NULL);

	uid = strtoul(from, NULL, 10);
	raw_data = g_newa(guint8, code_len + 16);
	bytes = 0;
	bytes += qq_put8(raw_data + bytes, 0x03);
	bytes += qq_put8(raw_data + bytes, 0x01);
	bytes += qq_put32(raw_data + bytes, uid);
	bytes += qq_put16(raw_data + bytes, code_len);
	bytes += qq_putdata(raw_data + bytes, code, code_len);

	qq_send_cmd(gc, QQ_CMD_BUDDY_CHECK_CODE, raw_data, bytes);
}

static gint server_buddy_check_code(PurpleConnection *gc,
		gchar *from, guint8 *data, gint data_len)
{
	gint bytes;
	guint16 code_len;
	guint8 *code;

	g_return_val_if_fail(data != NULL && data_len > 0, 0);

	bytes = 0;
	bytes += qq_get16(&code_len, data + bytes);
	if (code_len <= 0) {
		purple_debug_info("QQ", "Server msg for buddy has no code\n");
		return bytes;
	}
	if (bytes + code_len < data_len) {
		purple_debug_error("QQ", "Code len error in server msg for buddy\n");
		qq_show_packet("server_buddy_check_code", data, data_len);
		code_len = data_len - bytes;
	}
	code = g_newa(guint8, code_len);
	bytes += qq_getdata(code, code_len, data + bytes);

	request_buddy_check_code(gc, from, code, code_len);
	return bytes;
}
*/

/* someone wants to add you to his buddy list */
static void server_buddy_add_request(PurpleConnection *gc, gchar *from, gchar *to,
		guint8 *data, gint data_len)
{
	gint bytes;
	guint32 uid;
	gchar *msg;
	guint8 allow_reverse;

	g_return_if_fail(from != NULL && to != NULL);
	g_return_if_fail(data != NULL && data_len >= 3);
	uid = strtoul(from, NULL, 10);
	g_return_if_fail(uid != 0);

	/* qq_show_packet("server_buddy_add_request", data, data_len); */

	bytes = 0;
	bytes += qq_get_vstr(&msg, QQ_CHARSET_DEFAULT, sizeof(guint8), data+bytes);
	bytes += qq_get8(&allow_reverse, data + bytes);	/* allow_reverse = 0x01, allowed */
	//server_buddy_check_code(gc, from, data + bytes, data_len - bytes);

	if (strlen(msg) <= 0) {
		g_free(msg);
		msg = g_strdup( _("No reason given") );
	}
	buddy_add_input(gc, uid, msg);
	g_free(msg);
}

/* when you are added by a person, QQ server will send sys message */
static void server_buddy_added(PurpleConnection *gc, gchar *from, gchar *to,
		guint8 *data, gint data_len)
{
	guint32 uid;

	g_return_if_fail(from != NULL && to != NULL);
	g_return_if_fail(data != NULL);

	qq_show_packet("server_buddy_added", data, data_len);

	purple_debug_info("QQ", "Buddy %s added \n", from);

	//server_buddy_check_code(gc, from, data + bytes, data_len - bytes);

	uid = purple_name_to_uid(from);
	qq_buddy_find_or_new(gc, uid, 0xFF);
	qq_request_get_buddy_info(gc, uid, 0, 0);
	qq_request_get_buddies_online(gc, 0, 0);
	qq_request_get_level(gc, uid);
}

static void server_buddy_adding_ex(PurpleConnection *gc, gchar *from, gchar *to,
		guint8 *data, gint data_len)
{
	gint bytes;
	guint8 allow_reverse;

	g_return_if_fail(from != NULL && to != NULL);
	g_return_if_fail(data != NULL && data_len >= 3);

	qq_show_packet("server_buddy_adding_ex", data, data_len);

	bytes = 0;
	bytes += qq_get8(&allow_reverse, data + bytes);	/* allow_reverse = 0x01, allowed */
	//server_buddy_check_code(gc, from, data + bytes, data_len - bytes);
}

/* the buddy approves your request of adding him/her as your friend */
static void server_buddy_added_me(PurpleConnection *gc, gchar *from, gchar *to,
		guint8 *data, gint data_len)
{
	PurpleAccount *account = purple_connection_get_account(gc);
	qq_data *qd;
	guint32 uid;

	g_return_if_fail(from != NULL && to != NULL);

	qd = (qq_data *) gc->proto_data;

	uid = strtoul(from, NULL, 10);
	g_return_if_fail(uid > 0);

	//server_buddy_check_code(gc, from, data, data_len);

	qq_buddy_find_or_new(gc, uid, 0xFF);
	qq_request_get_buddy_info(gc, uid, 0, 0);
	qq_request_get_buddies_online(gc, 0, 0);
	qq_request_get_level(gc, uid);

	purple_account_notify_added(account, to, from, NULL, NULL);
}

/* you are rejected by the person */
static void server_buddy_rejected_me(PurpleConnection *gc, gchar *from, gchar *to,
		guint8 *data, gint data_len)
{
	guint32 uid;
	PurpleBuddy *buddy;
	gchar *msg, *msg_utf8;
	gint bytes;
	gchar **segments;
	gchar *primary, *secondary;
	qq_buddy_data *bd;

	g_return_if_fail(from != NULL && to != NULL);

	qq_show_packet("server_buddy_rejected_me", data, data_len);

	if (data_len <= 0) {
		msg = g_strdup( _("No reason given") );
	} else {
		segments = g_strsplit((gchar *)data, "\x1f", 1);
		if (segments != NULL && segments[0] != NULL) {
			msg = g_strdup(segments[0]);
			g_strfreev(segments);
			bytes = strlen(msg) + 1;
			if (bytes < data_len) {
				//server_buddy_check_code(gc, from, data + bytes, data_len - bytes);
			}
		} else {
			msg = g_strdup( _("No reason given") );
		}
	}
	msg_utf8 = qq_to_utf8(msg, QQ_CHARSET_DEFAULT);
	if (msg_utf8 == NULL) {
		msg_utf8 = g_strdup( _("Unknown reason") );
	}
	g_free(msg);

	primary = g_strdup_printf(_("Rejected by %s"), from);
	secondary = g_strdup_printf(_("Message: %s"), msg_utf8);

	purple_notify_info(gc, _("QQ Buddy"), primary, secondary);

	g_free(msg_utf8);
	g_free(primary);
	g_free(secondary);

	uid = strtoul(from, NULL, 10);
	g_return_if_fail(uid != 0);

	buddy = qq_buddy_find(gc, uid);
	if (buddy != NULL && (bd = purple_buddy_get_protocol_data(buddy)) != NULL) {
		/* Not authorized now, free buddy data */
		qq_buddy_data_free(bd);
		purple_buddy_set_protocol_data(buddy, NULL);
	}
}

void qq_process_buddy_from_server(PurpleConnection *gc, int funct,
		gchar *from, gchar *to, guint8 *data, gint data_len)
{
	switch (funct) {
	case QQ_SERVER_BUDDY_ADD_REQUEST:
		server_buddy_add_request(gc, from, to, data, data_len);
		break;
	case QQ_SERVER_BUDDY_ADDED_ME:
		server_buddy_added_me(gc, from, to, data, data_len);
		break;
	case QQ_SERVER_BUDDY_REJECTED_ME:
		server_buddy_rejected_me(gc, from, to, data, data_len);
		break;
	case QQ_SERVER_BUDDY_ACCEPTED:
		server_buddy_added(gc, from, to, data, data_len);
		break;
	case QQ_SERVER_BUDDY_ADDING_EX:
	case QQ_SERVER_BUDDY_ADDED_ANSWER:
		server_buddy_adding_ex(gc, from, to, data, data_len);
		break;
	default:
		purple_debug_warning("QQ", "Unknow buddy operate (%d) from server\n", funct);
		break;
	}
}

void qq_request_search_uid(PurpleConnection *gc, qq_buddy_opt_req *opt_req)
{
	guint8 raw_data[8];
	gint bytes;

	g_return_if_fail(opt_req && opt_req->uid > 0);
	
	bytes = 0;
	bytes += qq_put8(raw_data + bytes, 0x03);
	bytes += qq_put32(raw_data + bytes, opt_req->uid);

	qq_send_cmd_mess(gc, QQ_CMD_SEARCH_UID, raw_data, bytes, 0, (guintptr)opt_req);
}

void qq_process_search_uid( PurpleConnection *gc, guint8 *data, gint data_len, qq_buddy_opt_req *opt_req )
{
	gint bytes;
	guint32 uid;
	guint8 status;
	gchar * name;
	guint16 icon;

	g_return_if_fail(data != NULL && data_len != 0);
	g_return_if_fail(opt_req && opt_req->uid != 0);

	//qq_show_packet("qq_process_search_uid", data, data_len);
	bytes = 7;
	bytes += qq_get32(&uid, data + bytes);
	g_return_if_fail(uid == opt_req->uid);

	bytes ++;
	bytes += qq_get8(&status, data + bytes);

	bytes += 4;
	bytes += qq_get_vstr(&name, NULL, sizeof(guint8), data + bytes);
	bytes += qq_get16(&icon, data + bytes);

	bytes += 13;
	bytes += qq_get16(&opt_req->no_auth_len, data + bytes);
	if (opt_req->no_auth)
	{
		opt_req->no_auth = g_new0(guint8, opt_req->no_auth_len);
		bytes += qq_getdata(opt_req->no_auth, opt_req->no_auth_len, data + bytes);
	}

	qq_request_add_buddy_post(gc, opt_req, NULL);
}

guint8 group_name_to_id(PurpleConnection *gc, const gchar * group_name)
{
	qq_data * qd;
	GSList *l;
	qq_group *g;
	g_return_val_if_fail(gc && gc->proto_data, 0xFF);

	qd = (qq_data *)gc->proto_data;

	for(l=qd->group_list; l; l=l->next)
	{
		g = l->data;
		if (g_strcmp0(g->group_name, group_name) ==0)
			break;
	}
	if (l)
		return g->group_id;
	else
		return 0xFF;
}