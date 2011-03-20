/**
 * @file qq_base.c
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
#include "server.h"
#include "cipher.h"
#include "request.h"

#include "buddy_info.h"
#include "buddy_list.h"
#include "char_conv.h"
#include "qq_crypt.h"
#include "group.h"
#include "qq_define.h"
#include "qq_network.h"
#include "qq_base.h"
#include "packet_parse.h"
#include "qq.h"
#include "qq_network.h"
#include "utils.h"
#include "group_internal.h"

/* generate a md5 key using uid and session_key */
static void get_session_md5(guint8 *session_md5, guint32 uid, guint8 *session_key)
{
	guint8 src[QQ_KEY_LENGTH + QQ_KEY_LENGTH];
	gint bytes = 0;

	bytes += qq_put32(src + bytes, uid);
	bytes += qq_putdata(src + bytes, session_key, QQ_KEY_LENGTH);

	qq_get_md5(session_md5, QQ_KEY_LENGTH, src, bytes);
}

/* send logout packets to QQ server */
void qq_request_logout(PurpleConnection *gc)
{
	gint i;
	qq_data *qd;
	guint8 *logout_fill;

	qd = (qq_data *) gc->proto_data;
	logout_fill = (guint8 *) g_alloca(16);
	memset(logout_fill, 0x00,16);

	for (i = 0; i < 4; i++)
		qq_send_cmd(gc, QQ_CMD_LOGOUT, logout_fill, QQ_KEY_LENGTH);

	qd->is_login = FALSE;	/* update login status AFTER sending logout packets */
}


/* send keep-alive packet to QQ server (it is a heart-beat) */
void qq_request_keep_alive(PurpleConnection *gc)
{
	qq_data *qd;
	guint8 raw_data[16] = {0};
	gint bytes= 0;
	gchar qq[11];
	gint len=0;

	qd = (qq_data *) gc->proto_data;

	/* In fact, we can send whatever we like to server
	 * with this command, server return the same result including
	 * the amount of online QQ users, my ip and port */
	len = g_snprintf(qq, 11, "%u", qd->uid);
	bytes += qq_putdata(raw_data + bytes, (guint8 *)qq, len);
	qq_send_cmd(gc, QQ_CMD_KEEP_ALIVE, raw_data, bytes);
}

gboolean qq_process_keep_alive(guint8 *data, gint data_len, PurpleConnection *gc)
{
	qq_data *qd;
	gint bytes= 0;
	guint8 ret;
	time_t server_time;
	struct tm *tm_local;

	g_return_val_if_fail(data != NULL && data_len != 0, FALSE);

	qd = (qq_data *) gc->proto_data;

	/* qq_show_packet("Keep alive reply packet", data, len); */

	bytes = 0;
	bytes += qq_get8(&ret, data + bytes);
	bytes += qq_get32(&qd->online_total, data + bytes);
	if(0 == qd->online_total) {
		purple_connection_error_reason(gc,
				PURPLE_CONNECTION_ERROR_NETWORK_ERROR,
				_("Lost connection with server"));
	} else {
		purple_debug_info("QQ", "Online QQ Account Number : %d", qd->online_total);
	}

	bytes += qq_getIP(&qd->my_ip, data + bytes);
	bytes += qq_get16(&qd->my_port, data + bytes);
	/* skip 2 bytes, 0x(00 3c) */
	bytes += 2;
	bytes += qq_gettime(&server_time, data + bytes);
	/* skip 5 bytes, all are 0 */

	purple_debug_info("QQ", "keep alive, %s:%d\n",
		inet_ntoa(qd->my_ip), qd->my_port);

	tm_local = localtime(&server_time);

	if (tm_local != NULL)
		purple_debug_info("QQ", "Server time: %d-%d-%d, %d:%d:%d\n",
				(1900 +tm_local->tm_year), (1 + tm_local->tm_mon), tm_local->tm_mday,
				tm_local->tm_hour, tm_local->tm_min, tm_local->tm_sec);
	else
		purple_debug_error("QQ", "Server time could not be parsed\n");

	return TRUE;
}

/* For QQ2010 */
void qq_request_touch_server(PurpleConnection *gc)
{
	qq_data *qd;
	guint8 *buf, *raw_data;
	gint bytes;
	guint8 *encrypted;
	gint encrypted_len;

	g_return_if_fail(gc != NULL && gc->proto_data != NULL);
	qd = (qq_data *) gc->proto_data;

	raw_data = g_newa(guint8, 1024);
	memset(raw_data, 0, 1024);

	encrypted = g_newa(guint8, 1024);

	bytes = qq_putdata(raw_data,touch_fill,sizeof(touch_fill));
	if (qd->redirect == NULL) {
		/* first packet to get server */
		qd->redirect_len = 15;
		qd->redirect = g_realloc(qd->redirect, qd->redirect_len);
		memset(qd->redirect, 0, qd->redirect_len);
	}
	bytes += qq_putdata(raw_data + bytes, qd->redirect, qd->redirect_len);

	encrypted_len = qq_encrypt(encrypted, raw_data, bytes, qd->ld.random_key);

	buf = g_newa(guint8, 1024);
	memset(buf, 0, 1024);
	bytes = 0;
	bytes += qq_putdata(buf + bytes, qd->ld.random_key, QQ_KEY_LENGTH);
	bytes += qq_putdata(buf + bytes, encrypted, encrypted_len);

	qd->send_seq++;
	qq_send_cmd_encrypted(gc, QQ_CMD_TOUCH_SERVER, qd->send_seq, buf, bytes, TRUE);
}

guint16 qq_process_touch_server(PurpleConnection *gc, guint8 *data, gint data_len)
{
	qq_data *qd;
	gint bytes;
	guint8 ret;

	g_return_val_if_fail (gc != NULL && gc->proto_data != NULL, QQ_LOGIN_REPLY_ERR);
	qd = (qq_data *) gc->proto_data;

	g_return_val_if_fail (data != NULL, QQ_LOGIN_REPLY_ERR);

	/* qq_show_packet("Get Server", data, data_len); */
	bytes = 0;
	bytes += qq_get8(&ret, data+bytes);
	if (ret != 0) {
		purple_connection_error_reason(gc,
			PURPLE_CONNECTION_ERROR_OTHER_ERROR,
			_("Touch server failed"));
		return QQ_LOGIN_REPLY_ERR;
	}
	bytes += qq_gettime(&qd->login_time,data+bytes);
	bytes += qq_getIP(&qd->my_ip,data+bytes);
	bytes += 8;	/* add 8 bytes fill  */
	
	/* qq_show_packet("Got token", data + bytes, data_len - bytes); */

	if (qd->ld.token_touch != NULL) {
		g_free(qd->ld.token_touch);
		qd->ld.token_touch = NULL;
		qd->ld.token_touch_len = 0;
	}
	bytes += qq_get16(&qd->ld.token_touch_len,data+bytes);

	qd->ld.token_touch = g_new0(guint8, qd->ld.token_touch_len);
	bytes += qq_getdata(qd->ld.token_touch,qd->ld.token_touch_len,data+bytes);

	qq_get8(&ret,data+bytes);	/* redirect flag */
	if (ret == 0) {
		/* Notice: do not clear redirect_data here. It will be used in login*/
		qd->redirect_ip.s_addr = 0;
		return QQ_LOGIN_REPLY_OK;
	}
	qd->redirect_len = 15;
	qd->redirect = g_realloc(qd->redirect, qd->redirect_len);
	qd->redirect[0] = 0;	/* fill first 00 */
	qq_getdata(qd->redirect+1, qd->redirect_len, data+bytes);
	memset(qd->redirect+11,0xFF,4);		/* fill last 4 bytes(redirect_ip) into 0xFF*/
	/* qq_show_packet("Redirect to", qd->redirect, qd->redirect_len); */

	qq_get8(&qd->redirect_times,data+bytes+1);
	qq_getIP(&qd->redirect_ip, data+bytes+10);
	purple_debug_info("QQ", "Get server %s\n", inet_ntoa(qd->redirect_ip));
	return QQ_TOUCH_REPLY_REDIRECT;
}

void qq_request_captcha(PurpleConnection *gc)
{
	qq_data *qd;
	guint8 *buf, *raw_data;
	gint bytes;
	guint8 *encrypted;
	gint encrypted_len;
	static guint8 captcha_fill[] = {
		0x03, 0x00, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
	};

	g_return_if_fail(gc != NULL && gc->proto_data != NULL);
	qd = (qq_data *) gc->proto_data;

	g_return_if_fail(qd->ld.token_touch != NULL && qd->ld.token_touch_len > 0);

	raw_data = g_newa(guint8,  1024);
	memset(raw_data, 0, 1024);

	encrypted = g_newa(guint8, 1024);

	bytes = qq_putdata(raw_data,touch_fill,sizeof(touch_fill));
	bytes += qq_put16(raw_data + bytes, qd->ld.token_touch_len);
	bytes += qq_putdata(raw_data + bytes, qd->ld.token_touch, qd->ld.token_touch_len);
	bytes += qq_putdata(raw_data + bytes, captcha_fill,sizeof(captcha_fill)); 

	encrypted_len = qq_encrypt(encrypted, raw_data, bytes, qd->ld.random_key);

	buf = g_newa(guint8, 1024);
	memset(buf, 0, 1024);
	bytes = 0;
	bytes += qq_putdata(buf + bytes, qd->ld.random_key, QQ_KEY_LENGTH);
	bytes += qq_putdata(buf + bytes, encrypted, encrypted_len);

	qd->send_seq++;
	qq_send_cmd_encrypted(gc, QQ_CMD_CAPTCHA, qd->send_seq, buf, bytes, TRUE);
}

void qq_request_captcha_next(PurpleConnection *gc)
{
	qq_data *qd;
	guint8 *buf, *raw_data;
	gint bytes;
	guint8 *encrypted;
	gint encrypted_len;

	g_return_if_fail(gc != NULL && gc->proto_data != NULL);
	qd = (qq_data *) gc->proto_data;

	g_return_if_fail(qd->ld.token_touch != NULL && qd->ld.token_touch_len > 0);

	raw_data = g_newa(guint8, 1024);
	memset(raw_data, 0, 1024);

	encrypted = g_newa(guint8, 1024);

	bytes = 0;
	bytes = qq_putdata(raw_data,touch_fill,sizeof(touch_fill));
	bytes += qq_put16(raw_data + bytes, qd->ld.token_touch_len);
	bytes += qq_putdata(raw_data + bytes, qd->ld.token_touch, qd->ld.token_touch_len);

	bytes += qq_put8(raw_data + bytes, 3); 		/* Subcommand */
	bytes += qq_put16(raw_data + bytes, 5);
	bytes += qq_put32(raw_data + bytes, 0);
	bytes += qq_put8(raw_data + bytes, qd->captcha.next_index); 		/* fragment index */
	bytes += qq_put16(raw_data + bytes, qd->captcha.token_len); 	/* captcha token */
	bytes += qq_putdata(raw_data + bytes, qd->captcha.token, qd->captcha.token_len);

	encrypted_len = qq_encrypt(encrypted, raw_data, bytes, qd->ld.random_key);

	buf = g_newa(guint8, 1024);
	memset(buf, 0, 1024);
	bytes = 0;
	bytes += qq_putdata(buf + bytes, qd->ld.random_key, QQ_KEY_LENGTH);
	bytes += qq_putdata(buf + bytes, encrypted, encrypted_len);

	qd->send_seq++;
	qq_send_cmd_encrypted(gc, QQ_CMD_CAPTCHA, qd->send_seq, buf, bytes, TRUE);

	purple_connection_update_progress(gc, _("Requesting captcha"), 3, QQ_CONNECT_STEPS);
}

static void qq_request_captcha_submit(PurpleConnection *gc,
		guint8 *token, guint16 token_len, guint8 *code, guint16 code_len)
{
	qq_data *qd;
	guint8 *buf, *raw_data;
	gint bytes;
	guint8 *encrypted;
	gint encrypted_len;

	g_return_if_fail(gc != NULL && gc->proto_data != NULL);
	qd = (qq_data *) gc->proto_data;

	g_return_if_fail(qd->ld.token_touch != NULL && qd->ld.token_touch_len > 0);
	g_return_if_fail(code != NULL && code_len > 0);

	raw_data = g_newa(guint8, 1024);
	memset(raw_data, 0, 1024);

	encrypted = g_newa(guint8, 1024);	

	bytes = 0;
	bytes = qq_putdata(raw_data,touch_fill,sizeof(touch_fill));
	bytes += qq_put16(raw_data + bytes, qd->ld.token_touch_len);
	bytes += qq_putdata(raw_data + bytes, qd->ld.token_touch, qd->ld.token_touch_len);

	bytes += qq_put8(raw_data + bytes, 4); 		/* Subcommand */
	bytes += qq_put16(raw_data + bytes, 5);
	bytes += qq_put32(raw_data + bytes, 0);
	bytes += qq_put16(raw_data + bytes, code_len);
	bytes += qq_putdata(raw_data + bytes, code, code_len);
	bytes += qq_put16(raw_data + bytes, qd->ld.token_captcha_len); 	/* login token ex */
	bytes += qq_putdata(raw_data + bytes, qd->ld.token_captcha, qd->ld.token_captcha_len);

	encrypted_len = qq_encrypt(encrypted, raw_data, bytes, qd->ld.random_key);

	buf = g_newa(guint8, 1024);
	memset(buf, 0, 1024);
	bytes = 0;
	bytes += qq_putdata(buf + bytes, qd->ld.random_key, QQ_KEY_LENGTH);
	bytes += qq_putdata(buf + bytes, encrypted, encrypted_len);

	qd->send_seq++;
	qq_send_cmd_encrypted(gc, QQ_CMD_CAPTCHA, qd->send_seq, buf, bytes, TRUE);

	purple_connection_update_progress(gc, _("Checking captcha"), 3, QQ_CONNECT_STEPS);
}

typedef struct {
	PurpleConnection *gc;
	guint8 *token;
	guint16 token_len;
} qq_captcha_request;

static void captcha_request_destory(qq_captcha_request *captcha_req)
{
	g_return_if_fail(captcha_req != NULL);
	if (captcha_req->token) g_free(captcha_req->token);
	g_free(captcha_req);
}

static void captcha_input_cancel_cb(qq_captcha_request *captcha_req,
		PurpleRequestFields *fields)
{
	purple_connection_error_reason(captcha_req->gc,
			PURPLE_CONNECTION_ERROR_AUTHENTICATION_FAILED,
			_("Failed captcha verification"));

	captcha_request_destory(captcha_req);
}

static void captcha_input_ok_cb(qq_captcha_request *captcha_req,
		PurpleRequestFields *fields)
{
	gchar *code;

	g_return_if_fail(captcha_req != NULL && captcha_req->gc != NULL);

	code = utf8_to_qq(
			purple_request_fields_get_string(fields, "captcha_code"),
			QQ_CHARSET_DEFAULT);

	if (strlen(code) <= 0) {
		captcha_input_cancel_cb(captcha_req, fields);
		return;
	}

	qq_request_captcha_submit(captcha_req->gc,
			captcha_req->token, captcha_req->token_len,
			(guint8 *)code, strlen(code));

	captcha_request_destory(captcha_req);
}

void qq_captcha_input_dialog(PurpleConnection *gc,qq_captcha_data *captcha)
{
	PurpleAccount *account;
	PurpleRequestFields *fields;
	PurpleRequestFieldGroup *group;
	PurpleRequestField *field;
	qq_captcha_request *captcha_req;

	g_return_if_fail(captcha->token != NULL && captcha->token_len > 0);
	g_return_if_fail(captcha->data != NULL && captcha->data_len > 0);

	captcha_req = g_new0(qq_captcha_request, 1);
	captcha_req->gc = gc;
	captcha_req->token = g_new0(guint8, captcha->token_len);
	g_memmove(captcha_req->token, captcha->token, captcha->token_len);
	captcha_req->token_len = captcha->token_len;

	account = purple_connection_get_account(gc);

	fields = purple_request_fields_new();
	group = purple_request_field_group_new(NULL);
	purple_request_fields_add_group(fields, group);

	field = purple_request_field_image_new("captcha_img",
			_("Captcha Image"), (char *)captcha->data, captcha->data_len);
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
		_("OK"), G_CALLBACK(captcha_input_ok_cb),
		_("Cancel"), G_CALLBACK(captcha_input_cancel_cb),
		purple_connection_get_account(gc), NULL, NULL,
		captcha_req);
}

guint8 qq_process_captcha(PurpleConnection *gc, guint8 *data, gint data_len)
{
	qq_data *qd;
	gint bytes;
	guint8 captcha_cmd;
	guint8 need_captcha;
	guint16 png_len;
	guint8 curr_index;

	g_return_val_if_fail(data != NULL && data_len != 0, QQ_LOGIN_REPLY_ERR);

	g_return_val_if_fail(gc != NULL  && gc->proto_data != NULL, QQ_LOGIN_REPLY_ERR);
	qd = (qq_data *) gc->proto_data;

	bytes = 0;
	bytes += qq_get8(&captcha_cmd, data + bytes); 
	bytes += 2;	/* 0x(00 05) */
	bytes += qq_get8(&need_captcha, data + bytes);
	bytes += 4;		/* 00 00 01 23 */

	bytes += qq_get16(&(qd->ld.token_captcha_len), data + bytes);
	qd->ld.token_captcha = g_realloc(qd->ld.token_captcha, qd->ld.token_captcha_len);
	bytes += qq_getdata(qd->ld.token_captcha, qd->ld.token_captcha_len, data + bytes);
	/* qq_show_packet("Get token ex", qd->ld.token_ex, qd->ld.token_ex_len); */

	if(need_captcha==0)
	{
		purple_debug_info("QQ", "Captcha verified, result %d\n", need_captcha);
		return QQ_LOGIN_REPLY_OK;
	}

	bytes += qq_get16(&png_len, data + bytes);

	qd->captcha.data = g_realloc(qd->captcha.data, qd->captcha.data_len + png_len);
	bytes += qq_getdata(qd->captcha.data + qd->captcha.data_len, png_len, data + bytes);
	qd->captcha.data_len += png_len;

	bytes += qq_get8(&curr_index, data + bytes);
	bytes += qq_get8(&qd->captcha.next_index, data + bytes);

	bytes += qq_get16(&qd->captcha.token_len, data + bytes);
	qd->captcha.token = g_realloc(qd->captcha.token, qd->captcha.token_len);
	bytes += qq_getdata(qd->captcha.token, qd->captcha.token_len, data + bytes);
	/* qq_show_packet("Get captcha token", qd->captcha.token, qd->captcha.token_len); */

	purple_debug_info("QQ", "Request next captcha %d, new %d, total %d\n",
			qd->captcha.next_index, png_len, qd->captcha.data_len);
	if(qd->captcha.next_index > 0)
	{
		return QQ_LOGIN_REPLY_NEXT_CAPTCHA;
	}

	return QQ_LOGIN_REPLY_CAPTCHA_DLG;
}

/* source copy from gg's common.c */
static guint32 crc32_table[256];
static int crc32_initialized = 0;

static void crc32_make_table()
{
	guint32 h = 1;
	unsigned int i, j;

	memset(crc32_table, 0, sizeof(crc32_table));

	for (i = 128; i; i >>= 1) {
		h = (h >> 1) ^ ((h & 1) ? 0xedb88320L : 0);

		for (j = 0; j < 256; j += 2 * i)
			crc32_table[i + j] = crc32_table[j] ^ h;
	}

	crc32_initialized = 1;
}

static guint32 crc32(guint32 crc, const guint8 *buf, int len)
{
	if (!crc32_initialized)
		crc32_make_table();

	if (!buf || len < 0)
		return crc;

	crc ^= 0xffffffffL;

	while (len--)
		crc = (crc >> 8) ^ crc32_table[(crc ^ *buf++) & 0xff];

	return crc ^ 0xffffffffL;
}

void qq_request_auth(PurpleConnection *gc)
{
	qq_data *qd;
	guint8 *buf, *raw_data;
	gint bytes;
	guint8 *encrypted;
	gint encrypted_len;
	time_t now = time(NULL);

	g_return_if_fail(gc != NULL && gc->proto_data != NULL);
	qd = (qq_data *) gc->proto_data;

	g_return_if_fail(qd->ld.token_captcha != NULL && qd->ld.token_captcha_len > 0);

	raw_data = g_newa(guint8, 1024);
	memset(raw_data, 0, 1024);

	encrypted = g_newa(guint8, 1024);	

	/* Encrypted password and put in encrypted */
	bytes = 0;
	bytes += qq_put32(raw_data + bytes, 0x1B9BCCD9);

	raw_data[bytes++]=0x00;	raw_data[bytes++]=0x01;
		
	bytes += qq_put32(raw_data + bytes, qd->uid);
	bytes += qq_putdata(raw_data + bytes, touch_fill + 8, 12);	/* touch_fill Data2 */

	raw_data[bytes++]=0x00;	raw_data[bytes++]=0x00; raw_data[bytes++]=0x01;

	bytes += qq_putdata(raw_data + bytes, qd->ld.pwd_md5, sizeof(qd->ld.pwd_md5));
	
	bytes += qq_puttime(raw_data + bytes, &now);
	
	memset(raw_data + bytes, 0x00, 13);
	bytes += 13;

	bytes += qq_putIP(raw_data + bytes, &qd->my_ip);
	
	memset(raw_data + bytes, 0x00, 8);
	bytes += 8;

	bytes += qq_put16(raw_data + bytes, sizeof(auth_key[0]));
	bytes += qq_putdata(raw_data + bytes, auth_key[0], sizeof(auth_key[0]));
	bytes += qq_putdata(raw_data + bytes, auth_key[1], sizeof(auth_key[1]));

	encrypted_len = qq_encrypt(encrypted, raw_data, bytes, qd->ld.pwd_twice_md5);

	/* create packet */
	bytes = 0;
	bytes += qq_put32(raw_data + bytes, 0x00DE0001);
	bytes += qq_putdata(raw_data + bytes, touch_fill+2, sizeof(touch_fill)-2);
	/* token get from qq_request_captcha */
	bytes += qq_put16(raw_data + bytes, qd->ld.token_captcha_len);
	bytes += qq_putdata(raw_data + bytes, qd->ld.token_captcha, qd->ld.token_captcha_len);
	/* add password encrypted  packet */
	bytes += qq_put16(raw_data + bytes, encrypted_len);
	bytes += qq_putdata(raw_data + bytes, encrypted, encrypted_len);
	/* len of random + len of CRC32, wrong */
	bytes += qq_put16(raw_data + bytes, sizeof(qd->ld.random_key) + 4);
	bytes += qq_putdata(raw_data + bytes, qd->ld.random_key, sizeof(qd->ld.random_key));
	bytes += qq_put32(raw_data + bytes, crc32(0xFFFFFFFF, qd->ld.random_key, sizeof(qd->ld.random_key)));

	bytes += qq_put32(raw_data + bytes, 0x01772E01);
	bytes += qq_put32(raw_data + bytes, 0xBCA75E24);

	bytes += qq_put16(raw_data +bytes, sizeof(auth_key[1]));
	bytes += qq_putdata(raw_data +bytes, auth_key[1], sizeof(auth_key[1]));

	raw_data[bytes++]=0x02;
	bytes += qq_put32(raw_data + bytes, 0xAD98B7D2);

	bytes += qq_put16(raw_data + bytes, sizeof(auth_key[2]));
	bytes += qq_putdata(raw_data + bytes, auth_key[2], sizeof(auth_key[2]));

	/* 00 fill */
	memset(raw_data + bytes, 0x00, 328);
	bytes += 328;

	/* Encrypted by random key*/
	encrypted_len = qq_encrypt(encrypted, raw_data, bytes, qd->ld.random_key);

	buf = g_newa(guint8, 1024);
	memset(buf, 0, 1024);
	bytes = 0;
	bytes += qq_putdata(buf + bytes, qd->ld.random_key, QQ_KEY_LENGTH);
	bytes += qq_putdata(buf + bytes, encrypted, encrypted_len);

	qd->send_seq++;
	qq_send_cmd_encrypted(gc, QQ_CMD_AUTH, qd->send_seq, buf, bytes, TRUE);
}

guint8 qq_process_auth( PurpleConnection *gc, guint8 *data, gint data_len)
{
	qq_data *qd;
	int bytes;
	guint8 ret;
	gchar *error = NULL;
	guint16 length;
	gchar *msg;
	guint16 msg_len;
	guint8 i;
	PurpleConnectionError reason = PURPLE_CONNECTION_ERROR_AUTHENTICATION_FAILED;

	g_return_val_if_fail(data != NULL && data_len != 0, QQ_LOGIN_REPLY_ERR);

	g_return_val_if_fail(gc != NULL  && gc->proto_data != NULL, QQ_LOGIN_REPLY_ERR);
	qd = (qq_data *) gc->proto_data;

	/* qq_show_packet("Check password reply", data, data_len); */ 

	bytes = 0;
	bytes += qq_get16(&length, data + bytes);	/* total length - header(2) - tail(2) */
	bytes += qq_get8(&ret, data + bytes);

	if (ret == 0) {
		/* get token_auth */
		if (qd->ld.token_auth == NULL) qd->ld.token_auth = g_new0(guint8 *,3);

		for (i=0; i<3; ++i)
		{
			if (qd->ld.token_auth[i] != NULL) g_free(qd->ld.token_auth[i]);

			if (i==1)	 /* length and time*/
			{
				bytes +=4;		/* bypass 00 09 00 02 */
				bytes += qq_gettime(&qd->login_time, data+bytes);
			}
			
			bytes += qq_get16(&qd->ld.token_auth_len[i], data + bytes);
			qd->ld.token_auth[i] = g_new0(guint8, qd->ld.token_auth_len[i]);

			bytes += qq_getdata(qd->ld.token_auth[i], qd->ld.token_auth_len[i], data + bytes);
		}

		/* Key Used in verify_E5 Packet */
		bytes += qq_getdata(qd->ld.keys[0], sizeof(qd->ld.keys[0]), data+bytes);
		bytes += 2;
		qq_getdata(qd->ld.keys[1], sizeof(qd->ld.keys[1]), data+bytes);
		/* qq_show_packet("Get login token", qd->ld.login_token, qd->ld.login_token_len); */

		return QQ_LOGIN_REPLY_OK;
	}

	switch (ret)
	{
		case 0x34:		/* invalid password */
			if (!purple_account_get_remember_password(gc->account)) {
				purple_account_set_password(gc->account, NULL);
			}
			error = g_strdup(_("Incorrect password"));
			reason = PURPLE_CONNECTION_ERROR_AUTHENTICATION_FAILED;
			break;
		case 0x33:		/* need activation */
		case 0x51:		/* need activation */
			error = g_strdup(_("Activation required"));
			reason = PURPLE_CONNECTION_ERROR_AUTHENTICATION_FAILED;
			break;
		case 0xBF:		/* uid is not exist */
			error = g_strdup(_("Username does not exist"));
			reason = PURPLE_CONNECTION_ERROR_INVALID_USERNAME;
			break;
		default:
			qq_hex_dump(PURPLE_DEBUG_WARNING, "QQ", data, data_len,
					">>> [default] decrypt and dump");
			error = g_strdup_printf(
						_("Unknown reply when checking password (0x%02X)"),
						ret );
			reason = PURPLE_CONNECTION_ERROR_OTHER_ERROR;
			break;
	}

	bytes += 8;	/* bypass some fillings */
	bytes += qq_get16(&msg_len, data + bytes);

	msg = g_strndup((gchar *)data + bytes, msg_len);

	purple_debug_error("QQ", "%s: %s\n", error, msg);
	purple_connection_error_reason(gc, reason, msg);

	g_free(error);
	g_free(msg);
	return QQ_LOGIN_REPLY_ERR;
}


void qq_request_verify_E5(PurpleConnection *gc)
{
	qq_data *qd;
	guint8 *buf, *raw_data;
	gint bytes = 0;
	guint8 *encrypted;
	gint encrypted_len;

	g_return_if_fail(gc != NULL && gc->proto_data != NULL);
	qd = (qq_data *) gc->proto_data;

	g_return_if_fail(qd->ld.token_captcha != NULL && qd->ld.token_captcha_len > 0);
	g_return_if_fail(qd->ld.token_auth[0] != NULL && qd->ld.token_auth_len[0] > 0);
	g_return_if_fail(qd->ld.token_auth[1] != NULL && qd->ld.token_auth_len[1] > 0);
	g_return_if_fail(qd->ld.keys[0] != NULL);

	raw_data = g_newa(guint8, 1024);
	memset(raw_data, 0, 1024);
	encrypted = g_newa(guint8, 1024);	

	bytes += qq_put32(raw_data + bytes, 0x010E0001);
	bytes += qq_putdata(raw_data + bytes, touch_fill + 1, sizeof(touch_fill) - 1);

	bytes += qq_put16(raw_data+bytes, qd->ld.token_captcha_len);
	bytes += qq_putdata(raw_data+bytes, qd->ld.token_captcha, qd->ld.token_captcha_len);
	bytes += qq_put16(raw_data+bytes, qd->ld.token_auth_len[0]);
	bytes += qq_putdata(raw_data+bytes, qd->ld.token_auth[0], qd->ld.token_auth_len[0]);

	bytes += qq_put16(raw_data+bytes, 0x0098);
	bytes += qq_put16(raw_data+bytes,0x0002);
	bytes += qq_puttime(raw_data+bytes, &qd->login_time);

	bytes += qq_put16(raw_data+bytes, qd->ld.token_auth_len[1]);
	bytes += qq_putdata(raw_data+bytes, qd->ld.token_auth[1], qd->ld.token_auth_len[1]);

	memset(raw_data+bytes, 0x00, 7);
	*(raw_data+bytes+2) = 0x01;
	bytes += 7;

	encrypted_len = qq_encrypt(encrypted, raw_data, bytes, qd->ld.keys[0]);

	buf = g_newa(guint8, 1024);
	memset(buf, 0, 1024);
	bytes = 0;
	bytes += qq_put16(buf+bytes, qd->ld.token_auth_len[2]);
	bytes += qq_putdata(buf+bytes, qd->ld.token_auth[2], qd->ld.token_auth_len[2]);
	bytes += qq_putdata(buf + bytes, encrypted, encrypted_len);

	qd->send_seq++;
	qq_send_cmd_encrypted(gc, QQ_CMD_VERIFY_E5, qd->send_seq, buf, bytes, TRUE);

}

guint8 qq_process_verify_E5( PurpleConnection *gc, guint8 *data, gint data_len )
{
	qq_data *qd;
	int bytes;

	g_return_val_if_fail(data != NULL && data_len != 0, QQ_LOGIN_REPLY_ERR);

	g_return_val_if_fail(gc != NULL  && gc->proto_data != NULL, QQ_LOGIN_REPLY_ERR);
	qd = (qq_data *) gc->proto_data;

	bytes = 4;
	bytes += qq_getdata(qd->ld.keys[2], QQ_KEY_LENGTH, data+bytes);
	bytes += 8;
	bytes += qq_get32(&qd->ld.login_fill, data+bytes);
	bytes += qq_gettime(&qd->login_time, data+bytes);
	bytes += qq_getIP(&qd->my_ip, data+bytes);
	bytes += 8;

	if (qd->ld.token_verify == NULL) qd->ld.token_verify = g_new0(guint8 *, 3);

	bytes += qq_get16(&qd->ld.token_verify_len[0], data+bytes);
	if (qd->ld.token_verify[0] != NULL) g_free(qd->ld.token_verify[0]);
	qd->ld.token_verify[0] = g_new0(guint8, qd->ld.token_verify_len[0]);
	bytes += qq_getdata(qd->ld.token_verify[0], qd->ld.token_verify_len[0], data+bytes);

	bytes += qq_getdata(qd->ld.keys[3], QQ_KEY_LENGTH, data+bytes);

	bytes += qq_get16(&qd->ld.token_verify_len[1], data+bytes);
	if (qd->ld.token_verify[1] != NULL) g_free(qd->ld.token_verify[1]);
	qd->ld.token_verify[1] = g_new0(guint8, qd->ld.token_verify_len[1]);
	bytes += qq_getdata(qd->ld.token_verify[1], qd->ld.token_verify_len[1], data+bytes);

	bytes += 41;

	bytes += qq_get16(&qd->ld.token_verify_len[2], data+bytes);
	if (qd->ld.token_verify[2] != NULL) g_free(qd->ld.token_verify[2]);
	qd->ld.token_verify[2] = g_new0(guint8, qd->ld.token_verify_len[2]);
	bytes += qq_getdata(qd->ld.token_verify[2], qd->ld.token_verify_len[2], data+bytes);

	return QQ_LOGIN_REPLY_OK;
}


void qq_request_verify_E3( PurpleConnection *gc )
{
	qq_data *qd;
	guint8 *buf, *raw_data;
	gint bytes = 0;
	guint8 *encrypted;
	gint encrypted_len;

	g_return_if_fail(gc != NULL && gc->proto_data != NULL);
	qd = (qq_data *) gc->proto_data;

	g_return_if_fail(qd->ld.token_captcha != NULL && qd->ld.token_captcha_len > 0);
	g_return_if_fail(qd->ld.token_verify[1] != NULL && qd->ld.token_verify_len[0] > 0);
	g_return_if_fail(qd->ld.token_auth[2] != NULL && qd->ld.token_auth_len[2] > 0);
	g_return_if_fail(qd->ld.keys[0] != NULL);

	raw_data = g_newa(guint8, 1024);
	memset(raw_data, 0, 1024);
	encrypted = g_newa(guint8, 1024);	

	bytes += qq_put16(raw_data + bytes, 0x00C8);
	bytes += qq_putdata(raw_data + bytes, touch_fill, sizeof(touch_fill));

	bytes += qq_put16(raw_data+bytes, qd->ld.token_captcha_len);
	bytes += qq_putdata(raw_data+bytes, qd->ld.token_captcha, qd->ld.token_captcha_len);
	bytes += qq_put16(raw_data+bytes, qd->ld.token_verify_len[1]);
	bytes += qq_putdata(raw_data+bytes, qd->ld.token_verify[1], qd->ld.token_verify_len[1]);

	bytes += qq_put32(raw_data+bytes, 0x00000020);
	memset(raw_data+bytes, 0x00, 32);
	bytes += 32;

	encrypted_len = qq_encrypt(encrypted, raw_data, bytes, qd->ld.keys[0]);

	buf = g_newa(guint8, 1024);
	memset(buf, 0, 1024);

	bytes = 0;
	bytes += qq_put16(buf+bytes, qd->ld.token_auth_len[2]);
	bytes += qq_putdata(buf+bytes, qd->ld.token_auth[2], qd->ld.token_auth_len[2]);
	bytes += qq_putdata(buf + bytes, encrypted, encrypted_len);

	qd->send_seq++;
	qq_send_cmd_encrypted(gc, QQ_CMD_VERIFY_E3, qd->send_seq, buf, bytes, TRUE);
}


guint8 qq_process_verify_E3( PurpleConnection *gc, guint8 *data, gint data_len )
{
	qq_data *qd;
	int bytes;
	guint8 ret;
	guint8 len;

	g_return_val_if_fail(data != NULL && data_len != 0, QQ_LOGIN_REPLY_ERR);

	g_return_val_if_fail(gc != NULL  && gc->proto_data != NULL, QQ_LOGIN_REPLY_ERR);
	qd = (qq_data *) gc->proto_data;

	bytes = 7;
	bytes += qq_get8(&len, data+bytes);
	qd->nickname = g_strndup((gchar *)data + bytes, len);
	bytes += len;

	bytes += qq_get8(&ret, data+bytes);

	return QQ_LOGIN_REPLY_OK;
	//return QQ_LOGIN_REPLY_ERR;
}


void qq_request_login(PurpleConnection *gc)
{
	qq_data *qd;
	guint8 *buf, *raw_data;
	gint bytes;
	guint8 *encrypted;
	gint encrypted_len;

	static const guint8 login_1[] = {
			0xA9, 0x07, 0x23, 0x4B, 0xEB, 0x3A, 0x68, 0xEA, 
			0x66, 0x9A, 0x0D, 0xEB, 0x79, 0x3E, 0xEF, 0x70
	};

	static const guint8 login_2[] = {
			0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00,
			0x08, 0x04, 0x10, 0x00, 0x01, 0x40, 0x01, 0xBC, 
			0xA7, 0x5E, 0x24, 0x00, 0x10, 0x14, 0x82, 0x79, 
			0x84, 0x1A, 0x79, 0xD0, 0xD4, 0xFD, 0x11, 0x26, 
			0x38, 0x39, 0xEC, 0xF6, 0x39
	};
	static const guint8 login_3[] = {
			0x02, 0xAD, 0x98, 0xB7, 0xD2,
			0x00, 0x10,	/* size of data below */
			0x7E, 0xD6, 0xF3, 0x98, 0xB1, 0x03, 0xE9, 0xC8,
			0x96, 0x32, 0x72, 0x4F, 0x7E, 0x2E, 0x7B, 0xD0
	};

	g_return_if_fail(gc != NULL && gc->proto_data != NULL);
	qd = (qq_data *) gc->proto_data;

	g_return_if_fail(qd->ld.token_auth[2] != NULL && qd->ld.token_auth_len[2] > 0);

	raw_data = g_newa(guint8, 1024);
	memset(raw_data, 0, 1024);

	encrypted = g_newa(guint8, 1024);	

	
	bytes = 0;
	bytes += qq_put16(raw_data+bytes, 0x0001);
	bytes += qq_putdata(raw_data+bytes, touch_fill+8, sizeof(touch_fill)-8);

	bytes += qq_put32(raw_data+bytes, qd->ld.login_fill);
	bytes += qq_puttime(raw_data+bytes, &qd->login_time);
	bytes += qq_putIP(raw_data+bytes, &qd->my_ip);
	
	memset(raw_data+bytes, 0x00, 8);
	bytes += 8;
	
	bytes += qq_put16(raw_data+bytes, qd->ld.token_verify_len[0]);
	bytes += qq_putdata(raw_data+bytes, qd->ld.token_verify[0], qd->ld.token_verify_len[0]);

	memset(raw_data+bytes, 0x00, 35);
	bytes += 35;
	bytes += qq_putdata(raw_data+bytes, login_1, sizeof(login_1));

	bytes += qq_put8(raw_data + bytes, 0xCC);	
	bytes += qq_put8(raw_data + bytes, qd->login_mode);

	memset(raw_data + bytes, 0, 25);
	bytes += 25;
	bytes += qq_putdata(raw_data+bytes, touch_fill+2, 6);
	memset(raw_data + bytes, 0, 16);
	bytes += 16;

	/* captcha token get from qq_process_captcha */
	bytes += qq_put16(raw_data + bytes, qd->ld.token_captcha_len);
	bytes += qq_putdata(raw_data + bytes, qd->ld.token_captcha, qd->ld.token_captcha_len);
	
	bytes += qq_putdata(raw_data + bytes, login_2, sizeof(login_2));

	memset(raw_data + bytes, 0, 25);
	bytes += 25;
	
	bytes += qq_putdata(raw_data + bytes, login_3, sizeof(login_3));

	/* 249 bytes zero filled*/
	memset(raw_data + bytes, 0, 249);
	bytes += 249;

	/* qq_show_packet("Login request", raw_data, bytes); */
	encrypted_len = qq_encrypt(encrypted, raw_data, bytes, qd->ld.keys[0]);

	buf = g_newa(guint8, 1024);
	memset(buf, 0, 1024);
	bytes = 0;
	/* login token get from qq_process_auth */
	bytes += qq_put16(buf + bytes, qd->ld.token_auth_len[2]);
	bytes += qq_putdata(buf + bytes, qd->ld.token_auth[2], qd->ld.token_auth_len[2]);
	bytes += qq_putdata(buf + bytes, encrypted, encrypted_len);

	qd->send_seq++;
	qq_send_cmd_encrypted(gc, QQ_CMD_LOGIN, qd->send_seq, buf, bytes, TRUE);
}

/* process the login reply packet */
guint8 qq_process_login( PurpleConnection *gc, guint8 *data, gint data_len)
{
	qq_data *qd;
	gint bytes;
	guint8 ret;
	guint32 uid;

	g_return_val_if_fail(data != NULL && data_len != 0, QQ_LOGIN_REPLY_ERR);

	qd = (qq_data *) gc->proto_data;

	bytes = 0;
	bytes += qq_get8(&ret, data + bytes);
	if (ret == 0) {
			bytes += qq_getdata(qd->session_key, sizeof(qd->session_key), data + bytes);
			purple_debug_info("QQ", "Got session_key\n");
			get_session_md5(qd->session_md5, qd->uid, qd->session_key);

			bytes += qq_get32(&uid, data + bytes);
			if (uid != qd->uid) {
				purple_debug_warning("QQ", "My uid in login reply is %u, not %u\n", uid, qd->uid);
			}
			bytes += qq_getIP(&qd->my_ip, data + bytes);
			bytes += qq_get16(&qd->my_port, data + bytes);
			bytes += qq_gettime(&qd->login_time, data + bytes);
			bytes += qq_getIP(&qd->my_local_ip, data + bytes);
			bytes += 42;
			bytes += qq_get16(&qd->ld.token_login_len, data + bytes);
			qd->ld.token_login = g_realloc(qd->ld.token_login, qd->ld.token_login_len);
			bytes += qq_getdata(qd->ld.token_login, qd->ld.token_login_len, data + bytes);
			/*purple_debug_info("QQ", "Last Login: %s, %s\n",
					inet_ntoa(qd->last_login_ip), ctime(&qd->last_login_time[0]));*/
			return QQ_LOGIN_REPLY_OK;
	}
	return QQ_TOUCH_REPLY_REDIRECT;
}

void qq_request_login_E9( PurpleConnection *gc )
{
	qq_data *qd;
	guint8 raw_data[4] = {0};
	gint bytes= 0;

	qd = (qq_data *) gc->proto_data;

	bytes += qq_put16(raw_data, 0x0101);
	qq_send_cmd(gc, QQ_CMD_LOGIN_E9, raw_data, bytes);
}

void qq_request_login_EA( PurpleConnection *gc )
{
	qq_data *qd;
	guint8 raw_data[2] = {0};
	gint bytes= 0;

	qd = (qq_data *) gc->proto_data;

	bytes += qq_put8(raw_data, 0x01);
	qq_send_cmd(gc, QQ_CMD_LOGIN_EA, raw_data, bytes);
}

void qq_request_login_getlist( PurpleConnection *gc, guint16 index )
{
	qq_data *qd;
	guint8 raw_data[16] = {0};
	gint bytes= 0;

	qd = (qq_data *) gc->proto_data;

	bytes += qq_put32(raw_data+bytes, 0x01000000);
	
	bytes += qq_put8(raw_data+bytes,0x00);
	bytes += qq_put32(raw_data+bytes, 0x00000000);	
	/* first login is all zero, while response a hash like 02 4D 5D CF AE
		02 list entry number, 4D 5D CF AE update time
		next time request with it, to verify if list has changed	*/

	bytes += qq_put16(raw_data+bytes, index);
	qq_send_cmd(gc, QQ_CMD_LOGIN_GETLIST, raw_data, bytes);
}

void qq_request_login_ED( PurpleConnection *gc )
{
	qq_data *qd;
	guint8 raw_data[2] = {0};
	gint bytes= 0;

	qd = (qq_data *) gc->proto_data;

	bytes += qq_put8(raw_data, 0x01);
	qq_send_cmd(gc, QQ_CMD_LOGIN_ED, raw_data, bytes);
}


void qq_request_login_EC( PurpleConnection *gc )
{
	qq_data *qd;
	guint8 raw_data[4] = {0};
	gint bytes= 0;

	qd = (qq_data *) gc->proto_data;

	bytes += qq_put16(raw_data, 0x0100);
	bytes += qq_put8(raw_data+bytes, qd->login_mode);
	qq_send_cmd(gc, QQ_CMD_LOGIN_EC, raw_data, bytes);
}

guint8 qq_process_login_getlist( PurpleConnection *gc, guint8 *data, gint data_len )
{
	qq_data *qd;
	gint bytes;
	guint8 ret;
	guint16 num;
	guint i;
	guint32 uid;
	guint8 type;
	guint8 group_id;
	qq_room_data *rmd;
	qq_buddy_group * bg;
	guint16 index_count;
	guint16 index;

	g_return_val_if_fail(data != NULL && data_len != 0, QQ_LOGIN_REPLY_ERR);

	qd = (qq_data *) gc->proto_data;

	/* now initiate QQ Qun, do it first as it may take longer to finish */
	qq_room_data_initial(gc);
	
	//qq_show_packet("GETLIST", data, data_len);

	bytes = 1;
	qq_get8(&ret, data + bytes);
	if (ret) {
		purple_debug_info("QQ", "No Need to Refresh List");
		return QQ_LOGIN_REPLY_OK;
	}
	bytes = 14;
	bytes += qq_get16(&index_count, data + bytes);
	bytes += qq_get16(&index, data + bytes);
	
	bytes += qq_get16(&num, data+bytes);

	for (i=0; i<num; ++i) {
		bytes += qq_get32(&uid, data+bytes);
		bytes += qq_get8(&type, data+bytes);
		bytes += qq_get8(&group_id, data+bytes);

		if (type == 0x01)		//buddy
		{
			bg = g_new0(qq_buddy_group, 1);
			bg->uid = uid;
			bg->group_id = group_id/4;		//divide by 4!
			/* add buddies after get group_list */
			//buddy = qq_buddy_find_or_new(gc, uid, group_id);
			 qd->buddy_list = g_slist_append(qd->buddy_list, bg);
		} else if (type == 0x04) {
			rmd = qq_room_data_find(gc, uid);
			if(rmd == NULL) {
				rmd = room_data_new(uid, 0, NULL);
				g_return_val_if_fail(rmd != NULL, QQ_LOGIN_REPLY_ERR);
				rmd->my_role = QQ_ROOM_ROLE_YES;
				qd->rooms = g_slist_append(qd->rooms, rmd);
			} else {
				rmd->my_role = QQ_ROOM_ROLE_YES;
			}
		}
	}
	if (index < index_count)	//need request more
	{
		index++;
		qq_request_login_getlist(gc, index);
		return index;
	} else {
		/* clean deleted buddies */
		qq_clean_group_buddy_list(gc);
		return QQ_LOGIN_REPLY_OK;
	}
}

void qq_clean_group_buddy_list( PurpleConnection *gc )
{
	qq_data *qd;
	PurpleBuddy * bd;
	qq_room_data *rmd;
	GSList * list;
	GSList * bl;
	guint32 uid;
	PurpleBlistNode *node;
	PurpleBlistNode *node_next;
	g_return_if_fail(gc != NULL || gc->account != NULL);

	qd = (qq_data *) gc->proto_data;

	node = purple_blist_get_root();
	while (node)
	{
		node_next = node->next;

		if (PURPLE_BLIST_NODE_IS_GROUP(node))
		{
			if (!purple_blist_get_group_size((PurpleGroup *)node, TRUE)) {
				purple_blist_remove_group((PurpleGroup *)node);
			}
		}
		node = node_next;
	}

	for ( list=purple_find_buddies(gc->account, NULL); list; list=list->next )
	{
		bd = (PurpleBuddy *)list->data;
		uid = purple_name_to_uid(bd->name);
		for (bl=qd->buddy_list; bl; bl=bl->next)
		{
			if (uid == ((qq_buddy_group *)(bl->data))->uid)	break;
		}
		/* Buddy Not Found */
		if (!bl)	
		{
			qq_buddy_free(bd);
		}
	}

	for (list=qd->rooms; list; list=list->next)
	{
		rmd = (qq_room_data *)list->data;
		if (rmd->my_role == QQ_ROOM_ROLE_NO)
		{
			qq_room_remove(gc, rmd->id);
		}
	}
	
	g_free(list);


}


