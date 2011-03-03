/**
 * @file im.h
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

#ifndef _QQ_IM_H_
#define _QQ_IM_H_

#include <glib.h>
#include "connection.h"

enum {
	QQ_MSG_BUDDY_09 = 0x0009,
	QQ_MSG_TO_UNKNOWN = 0x000a,
	QQ_MSG_SMS = 0x0014,	/* not sure */
	QQ_MSG_NEWS = 0x0018,
	QQ_MSG_ROOM_IM_UNKNOWN = 0x0020,
	QQ_MSG_ADD_TO_ROOM = 0x0021,
	QQ_MSG_DEL_FROM_ROOM = 0x0022,
	QQ_MSG_APPLY_ADD_TO_ROOM = 0x0023,
	QQ_MSG_APPROVE_APPLY_ADD_TO_ROOM = 0x0024,
	QQ_MSG_REJCT_APPLY_ADD_TO_ROOM = 0x0025,
	QQ_MSG_CREATE_ROOM = 0x0026,
	QQ_MSG_TEMP_ROOM_IM = 0x002A,
	QQ_MSG_ROOM_IM = 0x002B,
	QQ_MSG_ROOM_IM_52 = 0x0052,
	QQ_MSG_SYS = 0x0030,
	QQ_MSG_BUDDY_78 = 0x0078,
	QQ_MSG_BUDDY_84 = 0x0084,
	QQ_MSG_BUDDY_A6 = 0x00A6,
	QQ_MSG_BUDDY_A7 = 0x00A7,
	QQ_MSG_BUDDY_85 = 0x0085,

	QQ_MSG_ROOM_MEMBER_IM = 0x008C,
	QQ_MSG_SOMEBODY = 0x008D,
	QQ_MSG_WRITING = 0x0079,
	QQ_MSG_STRANGER = 0x0031

};

typedef struct {
	guint8 font_size;
	guint8 attr;
	guint8 rgb[3];
	guint16 charset;
	gchar *font;		/* Attension: font may NULL. old font name is in QQ charset */
	guint8 font_len;
} qq_im_format;

gint qq_get_im_tail(qq_im_format *fmt, guint8 *data, gint data_len);

qq_im_format *qq_im_fmt_new_default(void);
void qq_im_fmt_free(qq_im_format *fmt);
qq_im_format *qq_im_fmt_new_by_purple(const gchar *msg);
gchar *qq_im_fmt_to_purple(qq_im_format *fmt, GString *text);
gboolean qq_im_smiley_none(const gchar *msg);
GSList *qq_im_get_segments(gchar *msg_stripped, gboolean is_smiley_none);

void qq_got_message(PurpleConnection *gc, const gchar *msg);
gint qq_send_im(PurpleConnection *gc, const gchar *who, const gchar *message, PurpleMessageFlags flags);

void qq_process_im(PurpleConnection *gc, guint8 *data, gint len, guint16 msg_type);
void qq_process_extend_im(PurpleConnection *gc, guint8 *data, gint len);

gchar *qq_emoticon_to_purple(gchar *text);
gchar *emoticon_get(guint8 symbol);
#endif
