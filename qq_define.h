/**
 * @file qq_define.h
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

#ifndef _QQ_HEADER_INFO_H_
#define _QQ_HEADER_INFO_H_

#include <glib.h>

#define QQ_UDP_HEADER_LENGTH    7
#define QQ_TCP_HEADER_LENGTH    9

#define QQ_PACKET_TAG			0x02	/* all QQ text packets starts with it */
#define QQ_PACKET_TAIL			0x03	/* all QQ text packets end with it */

#define QQ_CLIENT_1E0D 0x1E0D   /* QQ2010 Intl */
const gchar *qq_get_ver_desc(gint source);

/* list of known QQ commands */
enum {
	QQ_CMD_LOGOUT = 0x0062,				/* log out */
				/* get onlines from tencent */
	QQ_CMD_UPDATE_INFO = 0x0004,			/* update information */
	QQ_CMD_GET_BUDDY_INFO = 0x003C,			/* get user information */
	QQ_CMD_GET_BUDDIES_SIGN = 	0x0067,			/* get buddy signature */
	QQ_CMD_REMOVE_BUDDY = 0x000a,			/* delete a buddy  */
	QQ_CMD_CHANGE_STATUS = 0x000d,		/* change my online status */
	QQ_CMD_GET_GROUP_LIST = 0x0001,		/* get buddy group label */
	QQ_CMD_ACK_SYS_MSG = 0x0012,			/* ack system message */
	QQ_CMD_SEND_TYPING = 0x00D5,			/* send now typing */
	QQ_CMD_SEND_IM = 0x00CD,			/* send message */
	QQ_CMD_RECV_IM = 0x0017,			/* receive message */
	QQ_CMD_RECV_IM_CE = 0x00CE,
	QQ_CMD_REMOVE_ME = 0x001c,			/* remove self */
	QQ_CMD_REQUEST_KEY = 0x001d,			/* request key for file transfer */
	QQ_CMD_CELL_PHONE_1 = 0x0021,			/* cell phone 1 */
	QQ_CMD_GET_BUDDIES_LIST = 0x0126,		/* get buddies list */
	QQ_CMD_GET_BUDDIES_ONLINE = 0x0027,		/* get online buddies list */
	QQ_CMD_CELL_PHONE_2 = 0x0029,			/* cell phone 2 */
	QQ_CMD_ROOM = 0x0002,			/* room command */
	QQ_CMD_GET_LEVEL = 0x005C,			/* get level for one or more buddies */
	QQ_CMD_RECV_MSG_SYS = 0x0080,			/* receive a system message */
	QQ_CMD_BUDDY_CHANGE_STATUS = 0x0081,	/* buddy change status */
	QQ_CMD_BUDDY_MEMO = 0x003E,    /* the message about buddy memo */

    /* for QQ2010*/
	QQ_CMD_TOUCH_SERVER = 0x0091,					/* touch login server */
	QQ_CMD_CAPTCHA = 0x00BA,						/* verify LOGIN captcha */
	QQ_CMD_AUTH = 0x00DD,				/* Password verify */
	QQ_CMD_VERIFY_DE = 0x00DE,
	QQ_CMD_VERIFY_E5 = 0x00E5,
	QQ_CMD_VERIFY_E3 = 0x00E3,
	QQ_CMD_LOGIN = 0x0030,
	QQ_CMD_LOGIN_E9 = 0x00E9,
	QQ_CMD_LOGIN_EA = 0x00EA,
	QQ_CMD_LOGIN_GETLIST = 0x00EB,
	QQ_CMD_LOGIN_EC = 0x00EC,
	QQ_CMD_LOGIN_ED = 0x00ED,
	QQ_CMD_KEEP_ALIVE = 0x0058,
	QQ_CMD_SEARCH_UID = 0x0115,
	QQ_CMD_AUTH_TOKEN = 0x00AE,				/* the request verification of information */
	QQ_CMD_ADD_BUDDY_TOUCH = 0x00A7,			/* add friend without auth */
	QQ_CMD_ADD_BUDDY_POST = 0x00A8, 				/* add buddy with auth */
	QQ_CMD_BUDDY_CHECK_CODE =  0x00B5,
	QQ_CMD_BUDDY_QUESTION =  0x00B7
};

const gchar *qq_get_cmd_desc(gint type);

enum {
	QQ_ROOM_CMD_GET_QUN_LIST = 0x20,
	QQ_ROOM_CMD_GET_INFO = 0x72,
	QQ_ROOM_CMD_CREATE = 0x04,	//Deprecated
	QQ_ROOM_CMD_MEMBER_OPT = 0x02,
	QQ_ROOM_CMD_CHANGE_INFO = 0x03,
	QQ_ROOM_CMD_ACTIVATE = 0x05,
	QQ_ROOM_CMD_SEARCH = 0x06,
	QQ_ROOM_CMD_JOIN = 0x19,
	QQ_ROOM_CMD_AUTH = 0x08,
	QQ_ROOM_CMD_QUIT = 0x09,
	QQ_ROOM_CMD_SEND_IM = 0x2a,
	QQ_ROOM_CMD_GET_ONLINES = 0x0b,
	QQ_ROOM_CMD_GET_MEMBERS_INFO = 0x0c,
	QQ_ROOM_CMD_GET_GROUP_CARD = 0x90,
	QQ_ROOM_CMD_CHANGE_CARD = 0x0E,
	QQ_ROOM_CMD_GET_REALNAMES = 0x0F,
	QQ_ROOM_CMD_GET_CARD = 0x10,
	QQ_ROOM_CMD_ADMIN = 0x1B,
	QQ_ROOM_CMD_TRANSFER = 0x1C,
	QQ_ROOM_CMD_TEMP_CREATE = 0x30,
	QQ_ROOM_CMD_TEMP_CHANGE_MEMBER = 0x31,
	QQ_ROOM_CMD_TEMP_QUIT = 0x32,
	QQ_ROOM_CMD_TEMP_GET_INFO = 0x33,
	QQ_ROOM_CMD_TEMP_SEND_IM = 0x35,
	QQ_ROOM_CMD_TEMP_GET_MEMBERS = 0x37
};

const gchar *qq_get_room_cmd_desc(gint room_cmd);

enum {
	QQ_SERVER_BUDDY_ADDED_ME = 3,
	QQ_SERVER_BUDDY_REJECTED_ME = 4,
	QQ_SERVER_NOTICE= 6,
	QQ_SERVER_NEW_CLIENT = 9,
	QQ_SERVER_BUDDY_ADDING_EX = 40,
	QQ_SERVER_BUDDY_ADD_REQUEST = 41,
	QQ_SERVER_BUDDY_ADDED_ANSWER = 42,
	QQ_SERVER_BUDDY_ACCEPTED = 43
};

enum {
	QQ_BUDDY_OFFLINE = 0x00,
	QQ_BUDDY_ONLINE_NORMAL = 10,
	QQ_BUDDY_CHANGE_TO_OFFLINE = 20,
	QQ_BUDDY_ONLINE_AWAY = 30,
	QQ_BUDDY_ONLINE_INVISIBLE = 40,
	QQ_BUDDY_ONLINE_BUSY = 50
};


gboolean is_online(guint8 status);

#endif
