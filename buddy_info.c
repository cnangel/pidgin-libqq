/**
 * @file buddy_info.c
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

#include "utils.h"
#include "packet_parse.h"
#include "buddy_list.h"
#include "buddy_info.h"
#include "char_conv.h"
#include "im.h"
#include "qq_define.h"
#include "qq_base.h"
#include "qq_network.h"

#define QQ_HOROSCOPE_SIZE 13
static const gchar *horoscope_names[] = {
	"-", N_("Aquarius"), N_("Pisces"), N_("Aries"), N_("Taurus"),
	N_("Gemini"), N_("Cancer"), N_("Leo"), N_("Virgo"), N_("Libra"),
	N_("Scorpio"), N_("Sagittarius"), N_("Capricorn")
};

#define QQ_ZODIAC_SIZE 13
static const gchar *zodiac_names[] = {
	"-", N_("Rat"), N_("Ox"), N_("Tiger"), N_("Rabbit"),
	N_("Dragon"), N_("Snake"), N_("Horse"), N_("Goat"), N_("Monkey"),
	N_("Rooster"), N_("Dog"), N_("Pig")
};

#define QQ_BLOOD_SIZE 6
static const gchar *blood_types[] = {
	"-", "A", "B", "O", "AB", N_("Other")
};

#define QQ_PUBLISH_SIZE 3
static const gchar *publish_types[] = {
	N_("Visible"), N_("Friend Only"), N_("Private")
};

#define QQ_GENDER_SIZE 3
static const gchar *genders[] = {
	N_("Private"),
	N_("Male"),
	N_("Female"),
};

#define QQ_PRIVACY_SETTINGS 3
static const gchar * privacy_sets[] = {
	N_("Open to Public"),
	N_("Friends Only"),
	N_("Private"),
};

#define QQ_FACES	    134
#define QQ_ICON_PREFIX "qq_"
#define QQ_ICON_SUFFIX ".png"

#define 	QQ_INFO_UID
#define 	QQ_INFO_NICK						0x4E22
#define 	QQ_INFO_ZIPCODE				0x4E25
#define 	QQ_INFO_ADDR					0x4E25
#define 	QQ_INFO_TEL						0x4E27
#define 	QQ_INFO_GENDER				0x4E29
#define 	QQ_INFO_NAME					0x4E2A
#define 	QQ_INFO_EMAIL					0x4E2B
#define 	QQ_INFO_OCCUPATION		0x4E2C
#define 	QQ_INFO_HOMEPAGE		0x4E2D
#define 	QQ_INFO_COUNTRY			0x4E2E
#define 	QQ_INFO_FACE						0x4E2F
#define 	QQ_INFO_MOBILE				0x4E30
#define 	QQ_INFO_PRIVACY				0x4E31
#define 	QQ_INFO_INTRO					0x4E33
#define 	QQ_INFO_SCHOOL				0x4E35
#define 	QQ_INFO_HOROSCOPE		0x4E36
#define 	QQ_INFO_ZODIAC				0x4E37
#define 	QQ_INFO_BLOOD					0x4E38
#define 	QQ_INFO_BIRTH					0x4E3F
#define 	QQ_INFO_COUNTRY_PROVINCE_CITY		0x4E40
#define 	QQ_INFO_1STLANG				0x4E41
#define 	QQ_INFO_2NDLANG			0x4E42
#define 	QQ_INFO_3RDLANG			0x4E43
#define 	QQ_INFO_AGE						0x4E45
/*	
#define 	QQ_INFO_VIP						0x520B
#define 	QQ_INFO_CLIENT				0x520F
 */

enum {
	QQ_FIELD_UNUSED = 0, QQ_FIELD_BASE, QQ_FIELD_EXT, QQ_FIELD_CONTACT, QQ_FIELD_ADDR
};

enum {
	QQ_FIELD_LABEL = 0, QQ_FIELD_STRING, QQ_FIELD_MULTI, QQ_FIELD_NUM, QQ_FIELD_BOOL, QQ_FIELD_CHOICE, QQ_FIELD_TIME
};

typedef struct {
	guint iclass;
	guint type;
	char *id;
	char *text;
	const gchar **choice;
	guint choice_size;
} QQ_FIELD_INFO;

static const QQ_FIELD_INFO field_infos[] = {
	{ QQ_FIELD_BASE, 	QQ_FIELD_NUM, "uid", 	N_("QQ Number"), NULL, 0 },
	{ QQ_FIELD_BASE, 	QQ_FIELD_STRING, "nickname", 	N_("Nickname"), NULL, 0 },
	{ QQ_FIELD_ADDR, 	QQ_FIELD_STRING, "zipcode", 	N_("Zipcode"), NULL, 0 },
	{ QQ_FIELD_ADDR, 	QQ_FIELD_STRING, "address", 	N_("Address"), NULL, 0 },
	{ QQ_FIELD_CONTACT, 	QQ_FIELD_STRING, "tel", 	N_("Phone Number"), NULL, 0 },
	{ QQ_FIELD_BASE, 	QQ_FIELD_CHOICE, "gender", 	N_("Gender"), genders, QQ_GENDER_SIZE },
	{ QQ_FIELD_BASE, 	QQ_FIELD_STRING, "name", 	N_("Name"), NULL, 0 },
	{ QQ_FIELD_CONTACT, 	QQ_FIELD_STRING, "email", 	N_("Email"), NULL, 0 },
	{ QQ_FIELD_BASE, 	QQ_FIELD_STRING, "occupation", 	N_("Occupation"), NULL, 0 },
	{ QQ_FIELD_CONTACT, 	QQ_FIELD_STRING, "homepage", 	N_("Homepage"), NULL, 0 },
	{ QQ_FIELD_ADDR, 	QQ_FIELD_NUM, "country_code", 	N_("Country/Region Code"), NULL, 0 },
	{ QQ_FIELD_UNUSED, 	QQ_FIELD_STRING, "face",	"Face", NULL, 0 },
	{ QQ_FIELD_CONTACT, 	QQ_FIELD_STRING, "mobile",	N_("Cellphone Number"), NULL, 0 },
	{ QQ_FIELD_CONTACT, 	QQ_FIELD_CHOICE, "privacy",	"Privacy Settings", privacy_sets, 0 },
	{ QQ_FIELD_BASE, 	QQ_FIELD_MULTI,  "intro", 	N_("Personal Introduction"), NULL, 0 },
	{ QQ_FIELD_EXT, 	QQ_FIELD_STRING, "college",	N_("College"), NULL, 0 },
	{ QQ_FIELD_EXT, 	QQ_FIELD_CHOICE, "horoscope",	N_("Horoscope"), horoscope_names, QQ_HOROSCOPE_SIZE },
	{ QQ_FIELD_EXT, 	QQ_FIELD_CHOICE, "zodiac",	N_("Zodiac"), zodiac_names, QQ_ZODIAC_SIZE },
	{ QQ_FIELD_EXT, 	QQ_FIELD_CHOICE, "blood",	N_("Blood"), blood_types, QQ_BLOOD_SIZE },
	{ QQ_FIELD_ADDR, 	QQ_FIELD_TIME, "birth", 	N_("Birthday"), NULL, 0 },
	{ QQ_FIELD_UNUSED, 	QQ_FIELD_CHOICE, "country_province_city",	"Country Province City", NULL, 0 },
	{ QQ_FIELD_UNUSED, 	QQ_FIELD_CHOICE, "1st_lang",	"1st Language", NULL, 0 },
	{ QQ_FIELD_UNUSED, 	QQ_FIELD_CHOICE, "2nd_lang",	"2nd Language", NULL, 0 },
	{ QQ_FIELD_UNUSED, 	QQ_FIELD_CHOICE, "3rd_lang",	"3rd Language", NULL, 0 },
	{ QQ_FIELD_BASE, 	QQ_FIELD_NUM, "age", 	N_("Age"), NULL, 0 },
	{ QQ_FIELD_UNUSED, 	QQ_FIELD_CHOICE, "vip",	"VIP", NULL, 0 },
	{ QQ_FIELD_UNUSED, 	QQ_FIELD_STRING, "client",	"Client", NULL, 0 },
};

typedef struct _modify_info_request {
	PurpleConnection *gc;
	int iclass;
	guint8 *data;
	guint16 info_num;
} modify_info_request;

#ifdef DEBUG
static void info_debug(gchar **segments)
{
#if 0
	int index;
	gchar *utf8_str;
	for (index = 0; segments[index] != NULL && index < QQ_INFO_LAST; index++) {
		if (field_infos[index].type == QQ_FIELD_STRING
				|| field_infos[index].type == QQ_FIELD_LABEL
				|| field_infos[index].type == QQ_FIELD_MULTI
				|| index == QQ_INFO_GENDER)  {
			utf8_str = qq_to_utf8(segments[index], QQ_CHARSET_DEFAULT);
			purple_debug_info("QQ_BUDDY_INFO", "%s: %s\n", field_infos[index].text, utf8_str);
			g_free(utf8_str);
			continue;
		}
		purple_debug_info("QQ_BUDDY_INFO", "%s: %s\n", field_infos[index].text, segments[index]);
	}
#endif
}
#endif

static void info_display_only(PurpleConnection *gc, guint8 *data)
{
	PurpleNotifyUserInfo *user_info;
	gchar *value;
	guint uid;
	gchar *who;
	guint index;
	guint16 num;
	guint8 choice_num;
	guint bytes;
	guint16 size;
	guint8 * info;
	qq_buddy_data *bd;

	user_info = purple_notify_user_info_new();

	bytes = 3;
	bytes += qq_get32(&uid, data+bytes);
	who = uid_to_purple_name(uid);
	value = g_strdup_printf("%d", uid);
	purple_notify_user_info_add_pair(user_info, _(field_infos[0].text), value);
	g_free(value);

	bd = qq_buddy_data_find(gc, uid);
	purple_notify_user_info_add_pair(user_info, _("Signature"), bd->signature);

	bytes += 4;
	bytes += qq_get16(&num, data+bytes);

	for (index=1; index<num; ++index)
	{
		bytes += 2;
		bytes += qq_get16(&size, data+bytes);
		info = (guint8 *)g_alloca(size);
		bytes += qq_getdata(info, size, data+bytes);


		if (field_infos[index].iclass == QQ_FIELD_UNUSED) {
			continue;
		}
		switch (field_infos[index].type) {
		case QQ_FIELD_BOOL:
			purple_notify_user_info_add_pair(user_info, _(field_infos[index].text),
				*(guint8 *)info? _("True") : _("False"));
			break;
		case QQ_FIELD_CHOICE:
			choice_num = *(guint8 *)info;
			if (choice_num < 0 || choice_num >= field_infos[index].choice_size) {
				choice_num = 0;
			}
			purple_notify_user_info_add_pair(user_info, _(field_infos[index].text), field_infos[index].choice[choice_num]);
			break;
		case QQ_FIELD_NUM:
			value = g_strdup_printf("%d", *(guint8 *)info);
			purple_notify_user_info_add_pair(user_info, _(field_infos[index].text), value);
			g_free(value);
			break;
		case QQ_FIELD_TIME:
			value = g_strdup_printf("%04d-%02d-%02d", g_ntohs(*(guint16 *)info), *(guint8 *)info+2, *(guint8 *)info+3);
			purple_notify_user_info_add_pair(user_info, _(field_infos[index].text), value);
			g_free(value);
			break;
		case QQ_FIELD_LABEL:
		case QQ_FIELD_STRING:
		case QQ_FIELD_MULTI:
		default:
			if (size) {
				value = (gchar *)g_malloc0(size+1);
				g_memmove(value, info, size);
				purple_notify_user_info_add_pair(user_info, _(field_infos[index].text), value);
				g_free(value);
			} else {
				purple_notify_user_info_add_pair(user_info, _(field_infos[index].text), "");
			}
			break;
		}
	}
	purple_notify_userinfo(gc, who, user_info, NULL, NULL);

	purple_notify_user_info_destroy(user_info);
}


void qq_request_get_buddy_info(PurpleConnection *gc, guint32 uid,
		guint32 update_class, int action)
{
	guint8 raw_data[1024];
	guint bytes;
	static guint8 info[] = {
		0x00, 0x1A, 0x4E, 0x22, 0x4E, 0x25, 0x4E, 0x26, 
		0x4E, 0x27, 0x4E, 0x29, 0x4E, 0x2A, 0x4E, 0x2B, 
		0x4E, 0x2C, 0x4E, 0x2D, 0x4E, 0x2E, 0x4E, 0x2F, 
		0x4E, 0x30, 0x4E, 0x31, 0x4E, 0x33, 0x4E, 0x35, 
		0x4E, 0x36, 0x4E, 0x37, 0x4E, 0x38, 0x4E, 0x3F, 
		0x4E, 0x40, 0x4E, 0x41, 0x4E, 0x42, 0x4E, 0x43, 
		0x4E, 0x45, 0x52, 0x0B, 0x52, 0x0F
	};

	g_return_if_fail(uid != 0);

	bytes = 0;
	bytes += qq_put16(raw_data+bytes, 0x0001);
	bytes += qq_put32(raw_data+bytes, uid);
	memset(raw_data+bytes, 0, 22);
	bytes += 22;
	bytes += qq_putdata(raw_data+bytes, info, sizeof(info));

	qq_send_cmd_mess(gc, QQ_CMD_GET_BUDDY_INFO, raw_data, bytes,
			update_class, action);
}

/* send packet to modify personal information */
void request_change_info(PurpleConnection *gc, guint8 *data, guint8 *token, guint token_size)
{
	gint bytes = 0;
	guint8 raw_data[MAX_PACKET_SIZE - 128] = {0};
	guint i = 0;
	guint16 size;

	g_return_if_fail(data != NULL);

	if (!token_size || token == NULL)
	{
		qq_request_auth_token(gc, 0x01, 0x0007, (guint32)data, 0);
		return;
	}
	

	bytes += qq_put8(raw_data + bytes, (guint8)token_size);
	bytes += qq_putdata(raw_data + bytes, token, token_size);
	bytes += qq_put16(raw_data + bytes, 0x0001);
	memset(raw_data+bytes, 0, 22);
	bytes += 22;
	bytes += qq_put16(raw_data + bytes, 0x0001);

	while ( *(guint8 *)(data+i) == 0x4E )
	{
		i += 2;		//4E xx
		i += qq_get16(&size, data+i);
		bytes += qq_putdata(raw_data+bytes, data+i-4, size+4);
		i += size;
	}
	/* qq_show_packet("request_modify_info", raw_data, bytes); */
	qq_send_cmd(gc, QQ_CMD_UPDATE_INFO, raw_data, bytes);
		
	g_free(data);
}

static void info_modify_cancel_cb(modify_info_request *info_request)
{
	g_free(info_request->data);
	g_free(info_request);
}

/* parse fields and send info packet */
static void info_modify_ok_cb(modify_info_request *info_request, PurpleRequestFields *fields)
{
	PurpleConnection *gc;
	guint8 *data;
	guint16 num;
	guint bytes=0;
	guint index;
	guint16 size;
	gchar *str;
	guint32 value;
	guint8 *newdata;
	guint datasize;
	GDate date;
	struct tm tm;

	gc = info_request->gc;
	g_return_if_fail(gc != NULL);
	data = info_request->data;
	g_return_if_fail(data != NULL);
	num = info_request->info_num;

	for (index = 1; index < num; ++index) {
		bytes += 2;
		bytes += qq_get16(&size, data+bytes);

		if (field_infos[index].iclass == QQ_FIELD_UNUSED) {
			continue;
		}
		if (!purple_request_fields_exists(fields, field_infos[index].id)) {
			continue;
		}
		
		switch (field_infos[index].type) {
			case QQ_FIELD_BOOL:
				value = purple_request_fields_get_bool(fields, field_infos[index].id)
						? 0x01 : 0x00;
				if (size==1) {
					bytes += qq_put8(data+bytes, (guint8)value);
					datasize += size+4;
					newdata = (guint8 *)g_realloc(newdata, datasize);
					g_memmove(newdata+datasize-size-4, data+bytes-4-size, 4+size);
				}
				break;
			case QQ_FIELD_CHOICE:
				value = purple_request_fields_get_choice(fields, field_infos[index].id);
				if (value < 0 || value >= field_infos[index].choice_size)	value = 0;
				if (size==1) {
					bytes += qq_put8(data+bytes, (guint8)value);
					datasize += size+4;
					newdata = (guint8 *)g_realloc(newdata, datasize);
					g_memmove(newdata+datasize-size-4, data+bytes-4-size, 4+size);
				}
				break;
			case QQ_FIELD_NUM:
				value = purple_request_fields_get_integer(fields, field_infos[index].id)
					? 0x01 : 0x00;
				if (size==1) {
					bytes += qq_put8(data+bytes, (guint8)value);
					datasize += size+4;
					newdata = (guint8 *)g_realloc(newdata, datasize);
					g_memmove(newdata+datasize-size-4, data+bytes-4-size, 4+size);
				}
				break;
			case QQ_FIELD_TIME:
				str = purple_request_fields_get_string(fields, field_infos[index].id);
				if (str != NULL) {
					g_date_set_parse(&date, str);
					if (g_date_valid (&date))
					{
						memset(&tm, 0, sizeof(tm));
						tm.tm_mon = date.month - 1;
						tm.tm_mday = date.day;
						tm.tm_year = date.year - 1900;
						value = mktime(&tm);
						if (size==4)
						{
							bytes += qq_put32(data+bytes, value);
							datasize += size+4;
							newdata = (guint8 *)g_realloc(newdata, datasize);
							g_memmove(newdata+datasize-size-4, data+bytes-size-4, size+4);
							g_free(str);
							break;
						}				
					}
					g_free(str);
				}
				
				newdata = (guint8 *)g_realloc(newdata, datasize+4+1);
				qq_put16(newdata+datasize, *(guint16 *)(data+bytes-4));
				qq_put16(newdata+datasize+2, 0x0001);
				qq_put8(newdata+datasize+4,0x00);

				datasize += 4+1;
				bytes += size;
				break;
			case QQ_FIELD_LABEL:
			case QQ_FIELD_STRING:
			case QQ_FIELD_MULTI:
			default:
				str = purple_request_fields_get_string(fields, field_infos[index].id);
				if (str == NULL) {
					str = g_strdup("-");
				}

				newdata = (guint8 *)g_realloc(newdata, datasize+strlen(str));
				qq_put16(newdata+datasize, *(guint16 *)(data+bytes-4));
				qq_put16(newdata+datasize+2, strlen(str));
				g_memmove(newdata+datasize+4, str, strlen(str));

				datasize += strlen(str)+4;
				bytes += size;
				g_free(str);;
				break;
		}
	}
	request_change_info(gc, newdata, NULL, 0);

	g_free(data);
	g_free(info_request);
}

static void field_request_new(PurpleRequestFieldGroup *group, guint index, guint8 *data)
{
	PurpleRequestField *field;
	gchar *value;
	guint choice_num;
	guint i;
	guint8 * info;
	guint bytes=0;
	guint16 size;

	g_return_if_fail(index > 0);

	for (i=0; i < index; ++i) {
		bytes += 2;
		bytes += qq_get16(&size, data+bytes);
		info = (guint8 *)g_alloca(size);
		bytes += qq_getdata(info, size, data+bytes);
	}
	switch (field_infos[index].type) {
		case QQ_FIELD_BOOL:
			field = purple_request_field_bool_new(
				field_infos[index].id, _(field_infos[index].text),
				*(guint8 *)info);
			purple_request_field_group_add_field(group, field);
			break;
		case QQ_FIELD_CHOICE:
			choice_num = (gint8)*info;
			if (choice_num < 0 || choice_num >= field_infos[index].choice_size) {
				choice_num = 0;
			}

			field = purple_request_field_choice_new(
				field_infos[index].id, _(field_infos[index].text), choice_num);
			for (i = 0; i < field_infos[index].choice_size; i++) {
				purple_request_field_choice_add(field, field_infos[index].choice[i]);
			}
			purple_request_field_group_add_field(group, field);
			break;
		case QQ_FIELD_NUM:
			value = g_strdup_printf("%d", *(guint8 *)info);
			field = purple_request_field_string_new(field_infos[index].id, _(field_infos[index].text), value, FALSE);
			purple_request_field_group_add_field(group, field);
			g_free(value);
			break;
		case QQ_FIELD_TIME:
			value = g_strdup_printf("%04d-%02d-%02d", g_ntohs(*(guint16 *)info), *(guint8 *)info+2, *(guint8 *)info+3);
			field = purple_request_field_string_new(field_infos[index].id, _(field_infos[index].text), value, FALSE);
			purple_request_field_group_add_field(group, field);
			g_free(value);
			break;
		case QQ_FIELD_STRING:
		case QQ_FIELD_MULTI:
		case QQ_FIELD_LABEL:
		default:
			value = (gchar *)g_malloc0(size+1);
			g_memmove(value, info, size);
			if (field_infos[index].type == QQ_FIELD_STRING) {
				field = purple_request_field_string_new(
					field_infos[index].id, _(field_infos[index].text), value, FALSE);
			} else {
				field = purple_request_field_string_new(
					field_infos[index].id, _(field_infos[index].text), value, TRUE);
			}
			purple_request_field_group_add_field(group, field);
			g_free(value);
			break;
	}
}

static void info_modify_dialogue(PurpleConnection *gc, guint8 *data, int iclass)
{
	PurpleRequestFieldGroup *group;
	PurpleRequestFields *fields;
	modify_info_request *info_request;
	gchar *utf8_title, *utf8_prim;
	guint index;
	guint8 bytes;
	guint uid;
	guint16 num;
	guint16 size;

	/* Keep one dialog once a time */
	purple_request_close_with_handle(gc);

	fields = purple_request_fields_new();
	group = purple_request_field_group_new(NULL);
	purple_request_fields_add_group(fields, group);

	bytes = 3;
	bytes += qq_get32(&uid, data+bytes);

	bytes += 4;
	bytes += qq_get16(&num, data+bytes);

	for (index = 1; index < num; ++index) {
		if (field_infos[index].iclass != iclass) {
			continue;
		}
		field_request_new(group, index, data+bytes);
		bytes += 2;
		bytes += qq_get16(&size, data+bytes);
		bytes += size;
	}

	switch (iclass) {
		case QQ_FIELD_CONTACT:
			utf8_title = g_strdup(_("Modify Contact"));
			utf8_prim = g_strdup_printf("%d for %d", _("Modify Contact"), uid);
			break;
		case QQ_FIELD_ADDR:
			utf8_title = g_strdup(_("Modify Address"));
			utf8_prim = g_strdup_printf("%d for %d", _("Modify Address"), uid);
			break;
		case QQ_FIELD_EXT:
			utf8_title = g_strdup(_("Modify Extended Information"));
			utf8_prim = g_strdup_printf("%d for %d", _("Modify Extended Information"), uid);
			break;
		case QQ_FIELD_BASE:
		default:
			utf8_title = g_strdup(_("Modify Information"));
			utf8_prim = g_strdup_printf("%d for %d", _("Modify Information"), uid);
			break;
	}

	info_request = g_new0(modify_info_request, 1);
	info_request->gc = gc;
	info_request->iclass = iclass;
	info_request->data = data+13;
	info_request->info_num = num;

	purple_request_fields(gc,
			utf8_title,
			utf8_prim,
			NULL,
			fields,
			_("Update"), G_CALLBACK(info_modify_ok_cb),
			_("Cancel"), G_CALLBACK(info_modify_cancel_cb),
			purple_connection_get_account(gc), NULL, NULL,
			info_request);

	g_free(utf8_title);
	g_free(utf8_prim);
}

/* process the reply of modify_info packet */
void qq_process_change_info(PurpleConnection *gc, guint8 *data, gint data_len)
{
	qq_data *qd;
	g_return_if_fail(data != NULL && data_len != 0);

	qd = (qq_data *) gc->proto_data;

	if (*(guint8 *)(data+1) != 0x01) {
		purple_debug_info("QQ", "Failed Updating info\n");
		qq_got_message(gc, _("Could not change buddy information."));
	}
}

static void request_set_buddy_icon(PurpleConnection *gc, gint face_num)
{
	PurpleAccount *account = purple_connection_get_account(gc);
	PurplePresence *presence = purple_account_get_presence(account);
	qq_data *qd = (qq_data *) gc->proto_data;
	gint offset;

	if(purple_presence_is_status_primitive_active(presence, PURPLE_STATUS_INVISIBLE)) {
		offset = 2;
	} else if(purple_presence_is_status_primitive_active(presence, PURPLE_STATUS_AWAY)
			|| purple_presence_is_status_primitive_active(presence, PURPLE_STATUS_EXTENDED_AWAY)) {
		offset = 1;
	} else {
		offset = 0;
	}

	qd->my_icon = 3 * (face_num - 1) + offset;
	qq_request_get_buddy_info(gc, qd->uid, 0, QQ_BUDDY_INFO_SET_ICON);
}

void qq_change_icon_cb(PurpleConnection *gc, const char *filepath)
{
	gchar *basename;
	size_t index;
	gint face;

	g_return_if_fail(filepath != NULL);

	purple_debug_info("QQ", "Change my icon to %s\n", filepath);

	basename = g_path_get_basename(filepath);
	index = strcspn(basename, "0123456789");
	face = strtol(basename + index, NULL, 10);
	g_free(basename);
	purple_debug_info("QQ", "Set face to %d\n", face);

	request_set_buddy_icon(gc, face);
}

void qq_set_custom_icon(PurpleConnection *gc, PurpleStoredImage *img)
{
	PurpleAccount *account = purple_connection_get_account(gc);
	const gchar *icon_path = purple_account_get_buddy_icon_path(account);

	g_return_if_fail(icon_path != NULL);

	/* Fixme:
	 *  icon_path is always null
	 *  purple_imgstore_get_filename is always new file
	 *  QQ buddy may set custom icon if level is over 16 */
	purple_debug_info("QQ", "Change my icon to %s\n", icon_path);
}

gchar *qq_get_icon_name(gint face)
{
	gint icon;
	gchar *icon_name;

	icon = face / 3 + 1;
	if (icon < 1 || icon > QQ_FACES) {
		icon = 1;
	}

	icon_name = g_strdup_printf("%s%d%s", QQ_ICON_PREFIX, icon, QQ_ICON_SUFFIX);
	return icon_name;
}

/*
 * This function seems to let people set their buddy icon, but it restricts
 * them to using a small list of stock icons.  Wouldn't it make more sense
 * to use libpurple's normal icon setting stuff?
 *
 * Also it would be nice to unify the icon_dir code for Windows and Linux.
 */
gchar *qq_get_icon_path(gchar *icon_name)
{
	gchar *icon_path;
	const gchar *icon_dir;
#ifdef _WIN32
	static char *dir = NULL;
	if (dir == NULL) {
		dir = g_build_filename(wpurple_install_dir(), "pixmaps",
				"purple", "buddy_icons", "qq", NULL);
	}
#endif

	/*
	 * TODO: The QQ protocol plugin should probably call
	 *       purple_prefs_add_string() at startup to initialize this
	 *       preference.  It is used to allow users or distributions
	 *       to specify this directory.  We don't include these icons
	 *       with libpurple because of possible copyright concerns.
	 */
	icon_dir = purple_prefs_get_string("/plugins/prpl/qq/icon_dir");
	if ( icon_dir == NULL || strlen(icon_dir) == 0) {
#ifdef _WIN32
			icon_dir = dir;
#else
			icon_dir = QQ_BUDDY_ICON_DIR;
#endif
	}
	icon_path = g_strdup_printf("%s%c%s", icon_dir, G_DIR_SEPARATOR, icon_name);

	return icon_path;
}

void qq_update_buddy_icon(PurpleAccount *account, const gchar *who, gint face)
{
	PurpleBuddy *buddy;
	const gchar *icon_name_prev = NULL;
	gchar *icon_name;
	gchar *icon_path;
	gchar *icon_file_content;
	gsize icon_file_size;

	g_return_if_fail(account != NULL && who != NULL);

	/* purple_debug_info("QQ", "Update %s icon to %d\n", who, face); */

	icon_name = qq_get_icon_name(face);
	g_return_if_fail(icon_name != NULL);
	/* purple_debug_info("QQ", "icon file name is %s\n", icon_name); */

	if ((buddy = purple_find_buddy(account, who))) {
		icon_name_prev = purple_buddy_icons_get_checksum_for_user(buddy);
		/*
		purple_debug_info("QQ", "Previous icon is %s\n",
				icon_name_prev != NULL ? icon_name_prev : "(NULL)");
		*/
	}
	if (icon_name_prev != NULL && !strcmp(icon_name, icon_name_prev)) {
		/* purple_debug_info("QQ", "Icon is not changed\n"); */
		g_free(icon_name);
		return;
	}

	icon_path = qq_get_icon_path(icon_name);
	if (icon_path == NULL) {
		g_free(icon_name);
		return;
	}

	if (!g_file_get_contents(icon_path, &icon_file_content, &icon_file_size, NULL)) {
		purple_debug_error("QQ", "Failed reading icon file %s\n", icon_path);
	} else {
		purple_debug_info("QQ", "Update %s icon to %d (%s)\n",
				who, face, icon_path);
		purple_buddy_icons_set_for_user(account, who,
				icon_file_content, icon_file_size, icon_name);
	}
	g_free(icon_name);
	g_free(icon_path);
}

/* process reply to get_info packet */
void qq_process_get_buddy_info(guint8 *data, gint data_len, guint32 action, PurpleConnection *gc)
{
	qq_data *qd;
	gchar *icon_name;
	qq_buddy_data *bd = NULL;
	gint bytes;
	guint32 uid;
	guint8 ret;
	guint16 num;
	guint16 i;
	guint16 flag;
	guint16 size;
	guint8 *info;
	gchar *who;
	gchar *nickname;
	guint16 face;
	guint8 age;
	guint8 gender;
	PurpleBuddy *buddy;
	PurpleAccount *account = purple_connection_get_account(gc);

	g_return_if_fail(data != NULL && data_len != 0);

	qd = (qq_data *) gc->proto_data;

	bytes = 2;
	bytes += qq_get8(&ret, data+bytes);
	if (ret!=0)
	{
		purple_debug_error("QQ", "Get Buddy Info Error!");
		return;
	}

	bytes += qq_get32(&uid, data+bytes);
	who = uid_to_purple_name(uid);

	bytes += 4;
	bytes += qq_get16(&num, data+bytes);

	for (i=0; i<num; ++i)
	{
		bytes += qq_get16(&flag, data+bytes);
		bytes += qq_get16(&size, data+bytes);
		info = (guint8 *)g_alloca(size);
		bytes += qq_getdata(info, size, data+bytes);
		
		switch (flag)
		{
		case QQ_INFO_NICK:
			nickname = (gchar *)g_malloc0(size+1);
			g_memmove(nickname, info, size);
			//qq_filter_str(nickname);
			break;
		case QQ_INFO_FACE:
			face = g_ascii_strtoll((gchar *)info, NULL, 10);
			if (action == QQ_BUDDY_INFO_SET_ICON) {
				if (face != qd->my_icon) {
					icon_name = g_strdup_printf("\x4E\x2F\x00\x04%d\x00", qd->my_icon);
					/* send new face to server */
					request_change_info(gc, (guint8 *)icon_name, NULL, 0);
				}
				g_free(data);
				return;
			}
			break;
		case QQ_INFO_AGE:
			age = *info;
			break;
		case QQ_INFO_GENDER:
			gender = *info;
			if (gender>2) gender = 0;
			break;
		default:
			break;
		}
	}

	if (uid == qd->uid) {	/* it is me */
		purple_debug_info("QQ", "Got my info\n");
		qd->my_icon = face;
		if (nickname != NULL) {
			purple_account_set_alias(account, nickname);
		}
		/* find me in buddy list */
		buddy = qq_buddy_find_or_new(gc, uid, 0xFF);
	} else {
		buddy = purple_find_buddy(gc->account, who);
		/* purple_debug_info("QQ", "buddy=%p\n", (void*)buddy); */
	}

	/* if the buddy is null, the api will catch it and return null here */
	bd = purple_buddy_get_protocol_data(buddy);
	/* purple_debug_info("QQ", "bd=%p\n", (void*)bd); */

	if (bd != NULL && buddy != NULL) {
		/* update buddy list (including myself, if myself is the buddy) */
		bd->age = age;
		bd->gender = gender;
		bd->face = face;

		if (nickname != NULL) {
			if (bd->nickname) g_free(bd->nickname);
			bd->nickname = g_strdup(nickname);
		}
		bd->last_update = time(NULL);

		purple_blist_server_alias_buddy(buddy, bd->nickname);

		/* convert face num from packet (0-299) to local face (1-100) */
		qq_update_buddy_icon(gc->account, who, bd->face);
	}

	g_free(who);
	g_free(nickname);
	
	switch (action) {
		case QQ_BUDDY_INFO_DISPLAY:
			info_display_only(gc, data);
			break;
		case QQ_BUDDY_INFO_SET_ICON:
			g_return_if_reached();
			break;
		case QQ_BUDDY_INFO_MODIFY_BASE:
			info_modify_dialogue(gc, data, QQ_FIELD_BASE);
			break;
		case QQ_BUDDY_INFO_MODIFY_EXT:
			info_modify_dialogue(gc, data, QQ_FIELD_EXT);
			break;
		case QQ_BUDDY_INFO_MODIFY_ADDR:
			info_modify_dialogue(gc, data, QQ_FIELD_ADDR);
			break;
		case QQ_BUDDY_INFO_MODIFY_CONTACT:
			info_modify_dialogue(gc, data, QQ_FIELD_CONTACT);
			break;
		default:
			break;
	}
	return;
}

void qq_request_get_level(PurpleConnection *gc, guint32 uid)
{
	guint8 buf[16] = {0};
	gint bytes = 0;

	bytes += qq_put8(buf + bytes, 0x88);
	bytes += qq_put32(buf + bytes, uid);
	bytes += qq_put8(buf + bytes, 0x00);
	qq_send_cmd(gc, QQ_CMD_GET_LEVEL, buf, bytes);
}

void qq_request_get_buddies_level(PurpleConnection *gc, guint32 update_class)
{
	qq_data *qd = (qq_data *) gc->proto_data;
	PurpleBuddy *buddy;
	qq_buddy_data *bd;
	guint8 *buf;
	GSList *buddies, *it;
	gint bytes;

	/* server only reply levels for online buddies */
	buf = g_newa(guint8, MAX_PACKET_SIZE);

	bytes = 0;
	bytes += qq_put8(buf + bytes, 0x89);
	buddies = purple_find_buddies(purple_connection_get_account(gc), NULL);
	for (it = buddies; it; it = it->next) {
		buddy = it->data;
		if (buddy == NULL) continue;
		if ((bd = purple_buddy_get_protocol_data(buddy)) == NULL) continue;
		if (bd->uid == 0) continue;	/* keep me as end of packet*/
		if (bd->uid == qd->uid) continue;
		bytes += qq_put32(buf + bytes, bd->uid);
	}
	bytes += qq_put32(buf + bytes, qd->uid);
	qq_send_cmd_mess(gc, QQ_CMD_GET_LEVEL, buf, bytes, update_class, 0);
}

void qq_process_get_level_reply(guint8 *data, gint data_len, PurpleConnection *gc)
{
	gint bytes;
	guint8 sub_cmd;
	guint32 uid, onlineTime;
	guint16 level, activeDays;
	qq_buddy_data *bd;
	qq_data * qd = (qq_data *) gc->proto_data;

	bytes = 0;
	bytes += qq_get8(&sub_cmd, data + bytes);
	switch (sub_cmd) {
		case 0x88:
			if (data_len - bytes >= 12) {
				bytes += qq_get32(&uid, data + bytes);
				bytes += qq_get32(&onlineTime, data + bytes);
				bytes += qq_get16(&level, data + bytes);
				bytes += qq_get16(&activeDays, data + bytes);
				
				if (uid == qd->uid)
				{				
					purple_debug_info("QQ", "level: %d, uid %u, tmOnline: %d, tmactiveDays: %d\n",
						level, uid, onlineTime, activeDays);
					qd->onlineTime = onlineTime;
					qd->level = level;
					qd->activeDays = activeDays;
				}
			}
			break;
		case 0x89:
			while (data_len - bytes >= 12) {
				bytes += qq_get32(&uid, data + bytes);
				bytes += qq_get16(&level, data + bytes);
				bytes += 2;
				purple_debug_info("QQ", "level: %d, uid %u \n",
					level, uid);

				bd = qq_buddy_data_find(gc, uid);
				if (bd == NULL) {
					purple_debug_error("QQ", "Got levels of %u not in my buddy list\n", uid);
					continue;
				}

				bd->level = level;
			}
			break;
		default:
			purple_debug_error("QQ",
				"Unknown CMD 0x%X  of Get levels.", sub_cmd);
	}
}

void qq_request_get_buddies_sign( PurpleConnection *gc, guint32 update_class, guint32 count )
{
	qq_data *qd = (qq_data *) gc->proto_data;
	PurpleBuddy *buddy;
	qq_buddy_data *bd;
	guint8 *buf;
	GSList *buddies, *it;
	gint bytes;
	guint16 i;

	buf = g_newa(guint8, MAX_PACKET_SIZE);

	bytes = 0;
	bytes += qq_put8(buf + bytes, 0x83);
	bytes += 2;	//num of buddies, fill it later

	buddies = purple_find_buddies(purple_connection_get_account(gc), NULL);

	for (it = buddies,i=0; it; it = it->next) {
		if (i<count) { i++; continue; }
		if (i>=count+10) break;		//send 10 buddies one time
		buddy = it->data;
		if (buddy == NULL) continue;
		if ((bd = purple_buddy_get_protocol_data(buddy)) == NULL) continue;
		bytes += qq_put32(buf + bytes, bd->uid);
		bytes += qq_put32(buf + bytes, 0x00000000);		//signature modified time, normally null
		i++;
	}
	qq_put16(buf + 1, i-count);	//num of buddies

	qq_send_cmd_mess(gc, QQ_CMD_GET_BUDDY_SIGN, buf, bytes, update_class, it ? i : 0);
}

void qq_process_get_buddy_sign(guint8 *data, gint data_len, PurpleConnection *gc)
{
	gint bytes;
	guint32 uid, last_uid;
	guint8 ret;
	qq_buddy_data *bd;
	gchar *sign, *who;
	qq_data * qd = (qq_data *) gc->proto_data;

	bytes = 1;		//83
	bytes += qq_get8(&ret, data+bytes);
	bytes += qq_get32(&last_uid, data+bytes);

	while (bytes<data_len)
	{
		bytes += qq_get32(&uid, data+bytes);
		bytes += 4;	//signature modified time, no need
		bd = qq_buddy_data_find(gc, uid);
		if (bd)
		{
			bytes += qq_get_vstr(&sign, NULL, sizeof(guint8), data+bytes);
			if (sign)
			{
				purple_debug_info("QQ", "QQ %d Signature: %s\n", uid, sign);
				bd->signature = sign;
				who = uid_to_purple_name(uid);
				purple_prpl_got_user_status(gc->account, who, "mobile", PURPLE_MOOD_NAME, bd->signature, NULL);
			}		
		} else {
			bytes += *(guint8 *)(data+bytes) ;
		}
	}
}