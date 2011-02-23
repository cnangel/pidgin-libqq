/**
 * @file im.c
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
#include "internal.h"
#include "notify.h"
#include "server.h"
#include "util.h"

#include "buddy_info.h"
#include "buddy_list.h"
#include "buddy_opt.h"
#include "char_conv.h"
#include "qq_define.h"
#include "im.h"
#include "packet_parse.h"
#include "qq_network.h"
#include "send_file.h"
#include "utils.h"

#define QQ_MSG_IM_MAX               513	/* max length of IM */

enum {
	QQ_IM_TEXT = 0x01,
	QQ_IM_AUTO_REPLY = 0x02
};

enum
{
	QQ_NORMAL_IM_TEXT = 0x000B,
	QQ_NORMAL_IM_FILE_REQUEST_TCP = 0x0001,
	QQ_NORMAL_IM_FILE_APPROVE_TCP = 0x0003,
	QQ_NORMAL_IM_FILE_REJECT_TCP = 0x0005,
	QQ_NORMAL_IM_FILE_REQUEST_UDP = 0x0035,
	QQ_NORMAL_IM_FILE_APPROVE_UDP = 0x0037,
	QQ_NORMAL_IM_FILE_REJECT_UDP = 0x0039,
	QQ_NORMAL_IM_FILE_NOTIFY = 0x003b,
	QQ_NORMAL_IM_FILE_PASV = 0x003f,			/* are you behind a firewall? */
	QQ_NORMAL_IM_FILE_CANCEL = 0x0049,
	QQ_NORMAL_IM_FILE_EX_REQUEST_UDP = 0x81,
	QQ_NORMAL_IM_FILE_EX_REQUEST_ACCEPT = 0x83,
	QQ_NORMAL_IM_FILE_EX_REQUEST_CANCEL = 0x85,
	QQ_NORMAL_IM_FILE_EX_NOTIFY_IP = 0x87
};

typedef struct _qq_im_header qq_im_header;
struct _qq_im_header {
	/* this is the common part of normal_text */
	guint16 version_from;
	guint32 uid_from;
	guint32 uid_to;
	guint8 session_md5[QQ_KEY_LENGTH];
	guint16 im_type;
};

/* read the common parts of the normal_im,
 * returns the bytes read if succeed, or -1 if there is any error */
static gint get_im_header(qq_im_header *im_header, guint8 *data, gint len)
{
	gint bytes;
	g_return_val_if_fail(data != NULL && len > 0, -1);

	bytes = 0;
	bytes += qq_get16(&(im_header->version_from), data + bytes);
	bytes += qq_get32(&(im_header->uid_from), data + bytes);
	bytes += qq_get32(&(im_header->uid_to), data + bytes);
	bytes += qq_getdata(im_header->session_md5, QQ_KEY_LENGTH, data + bytes);
	bytes += qq_get16(&(im_header->im_type), data + bytes);
	return bytes;
}

typedef struct _qq_emoticon qq_emoticon;
struct _qq_emoticon {
	guint8 symbol;
	gchar *name;
};

static gboolean emoticons_is_sorted = FALSE;
/* Map for purple smiley convert to qq, need qsort */
static qq_emoticon emoticons_std[] = {
	{0x4f, "/:)"},      {0x4f, "/wx"},      {0x4f, "/small_smile"},
	{0x42, "/:~"},      {0x42, "/pz"},      {0x42, "/curl_lip"},
	{0x43, "/:*"},      {0x43, "/se"},      {0x43, "/desire"},
	{0x44, "/:|"},      {0x44, "/fd"},      {0x44, "/dazed"},
	{0x45, "/8-)"},     {0x45, "/dy"},      {0x45, "/revel"},
	{0x46, "/:<"},      {0x46, "/ll"},      {0x46, "/cry"},
	{0x47, "/:$"},      {0x47, "/hx"},      {0x47, "/bashful"},
	{0x48, "/:x"},      {0x48, "/bz"},      {0x48, "/shut_mouth"},
	{0x8f, "/|-)"},     {0x8f, "/kun"},     {0x8f, "/sleepy"},
	{0x49, "/:z"},      {0x49, "/shui"},    {0x49, "/sleep"},	/* after sleepy */
	{0x4a, "/:'"},      {0x4a, "/dk"},      {0x4a, "/weep"},
	{0x4b, "/:-|"},     {0x4b, "/gg"},      {0x4b, "/embarassed"},
	{0x4c, "/:@"},      {0x4c, "/fn"},      {0x4c, "/pissed_off"},
	{0x4d, "/:P"},      {0x4d, "/tp"},      {0x4d, "/act_up"},
	{0x4e, "/:D"},      {0x4e, "/cy"},      {0x4e, "/toothy_smile"},
	{0x41, "/:O"},      {0x41, "/jy"},      {0x41, "/surprised"},
	{0x73, "/:("},      {0x73, "/ng"},      {0x73, "/sad"},
	{0x74, "/:+"},      {0x74, "/kuk"},     {0x74, "/cool"},
	{0xa1, "/--b"},     {0xa1, "/lengh"},
	{0x76, "/:Q"},      {0x76, "/zk"},      {0x76, "/crazy"},
	{0x8a, "/;P"},      {0x8a, "/tx"},      {0x8a, "/titter"},
	{0x8b, "/;-D"},     {0x8b, "/ka"},      {0x8b, "/cute"},
	{0x8c, "/;d"},      {0x8c, "/by"},      {0x8c, "/disdain"},
	{0x8d, "/;o"},      {0x8d, "/am"},      {0x8d, "/arrogant"},
	{0x8e, "/:g"},      {0x8e, "/jie"},     {0x8e, "/starving"},
	{0x78, "/:!"},      {0x78, "/jk"},      {0x78, "/terror"},
	{0x79, "/:L"},      {0x79, "/lh"},      {0x79, "/sweat"},
	{0x7a, "/:>"},      {0x7a, "/hanx"},    {0x7a, "/smirk"},
	{0x7b, "/:;"},      {0x7b, "/db"},      {0x7b, "/soldier"},
	{0x90, "/;f"},      {0x90, "/fendou"},  {0x90, "/struggle"},
	{0x91, "/:-S"},     {0x91, "/zhm"},     {0x91, "/curse"},
	{0x92, "/?"},       {0x92, "/yiw"},     {0x92, "/question"},
	{0x93, "/;x"},      {0x93, "/xu"},      {0x93, "/shh"},
	{0x94, "/;@"},      {0x94, "/yun"},     {0x94, "/dizzy"},
	{0x95, "/:8"},      {0x95, "/zhem"},    {0x95, "/excrutiating"},
	{0x96, "/;!"},      {0x96, "/shuai"},   {0x96, "/freaked_out"},
	{0x97, "/!!!"},     {0x97, "/kl"},      {0x97, "/skeleton"},
	{0x98, "/xx"},      {0x98, "/qiao"},    {0x98, "/hammer"},
	{0x99, "/bye"},     {0x99, "/zj"},      {0x99, "/bye"},
	{0xa2, "/wipe"},    {0xa2, "/ch"},
	{0xa3, "/dig"},     {0xa3, "/kb"},
	{0xa4, "/handclap"},{0xa4, "/gz"},
	{0xa5, "/&-("},     {0xa5, "/qd"},
	{0xa6, "/B-)"},     {0xa6, "/huaix"},
	{0xa7, "/<@"},      {0xa7, "/zhh"},
	{0xa8, "/@>"},      {0xa8, "/yhh"},
	{0xa9, "/:-O"},     {0xa9, "/hq"},
	{0xaa, "/>-|"},     {0xaa, "/bs"},
	{0xab, "/P-("},     {0xab, "/wq"},
	{0xac, "/:'|"},     {0xac, "/kk"},
	{0xad, "/X-)"},     {0xad, "/yx"},
	{0xae, "/:*"},      {0xae, "/qq"},
	{0xaf, "/@x"},      {0xaf, "/xia"},
	{0xb0, "/8*"},      {0xb0, "/kel"},
	{0xb1, "/pd"},      {0xb1, "/cd"},
	{0x61, "/<W>"},     {0x61, "/xig"},     {0x61, "/watermelon"},
	{0xb2, "/beer"},    {0xb2, "/pj"},
	{0xb3, "/basketb"}, {0xb3, "/lq"},
	{0xb4, "/oo"},      {0xb4, "/pp"},
	{0x80, "/coffee"},  {0x80, "/kf"},
	{0x81, "/eat"},     {0x81, "/fan"},
	{0x62, "/rose"},    {0x62, "/mg"},
	{0x63, "/fade"},    {0x63, "/dx"},      {0x63, "/wilt"},
	{0xb5, "/showlove"},{0xb5, "/sa"},		/* after sad */
	{0x65, "/heart"},   {0x65, "/xin"},
	{0x66, "/break"},   {0x66, "/xs"},      {0x66, "/broken_heart"},
	{0x67, "/cake"},    {0x67, "/dg"},
	{0x9c, "/li"},      {0x9c, "/shd"},     {0x9c, "/lightning"},
	{0x9d, "/bome"},    {0x9d, "/zhd"},     {0x9d, "/bomb"},
	{0x9e, "/kn"},      {0x9e, "/dao"},     {0x9e, "/knife"},
	{0x5e, "/footb"},   {0x5e, "/zq"},      {0x5e, "/soccer"},
	{0xb6, "/ladybug"}, {0xb6, "/pc"},
	{0x89, "/shit"},    {0x89, "/bb"},
	{0x6e, "/moon"},    {0x6e, "/yl"},
	{0x6b, "/sun"},     {0x6b, "/ty"},
	{0x68, "/gift"},    {0x68, "/lw"},
	{0x7f, "/hug"},     {0x7f, "/yb"},
	{0x6f, "/strong"},  {0x6f, "/qiang"},   {0x6f, "/thumbs_up"},
	{0x70, "/weak"},    {0x70, "/ruo"},     {0x70, "/thumbs_down"},
	{0x88, "/share"},   {0x88, "/ws"},      {0x88, "/handshake"},
	{0xb7, "/@)"},      {0xb7, "/bq"},
	{0xb8, "/jj"},      {0xb8, "/gy"},
	{0xb9, "/@@"},      {0xb9, "/qt"},
	{0xba, "/bad"},     {0xba, "/cj"},
	{0xbb, "/loveu"},   {0xbb, "/aini"},
	{0xbc, "/no"},      {0xbc, "/bu"},
	{0xbd, "/ok"},      {0xbd, "/hd"},
	{0x5c, "/love"},    {0x5c, "/aiq"},		/* after loveu */
	{0x56, "/<L>"},     {0x56, "/fw"},      {0x56, "/blow_kiss"},
	{0x58, "/jump"},    {0x58, "/tiao"},
	{0x5a, "/shake"},   {0x5a, "/fad"},		/* after fade */
	{0x5b, "/<O>"},     {0x5b, "/oh"},      {0x5b, "/angry"},
	{0xbe, "/circle"},  {0xbe, "/zhq"},
	{0xbf, "/kotow"},   {0xbf, "/kt"},
	{0xc0, "/turn"},    {0xc0, "/ht"},
	{0x77, "/:t"},      {0x77, "/tu"},      {0x77, "/vomit"},		/* after turn */
	{0xa0, "/victory"}, {0xa0, "/shl"},     {0xa0, "/v"},			/* end of v */
	{0xc1, "/skip"},    {0xc1, "/tsh"},
	{0xc2, "/oY"},      {0xc2, "/hsh"},
	{0xc3, "/#-O"},     {0xc3, "/jd"},
	{0xc4, "/hiphop"},  {0xc4, "/jw"},
	{0xc5, "/kiss"},    {0xc5, "/xw"},
	{0xc6, "/<&"},      {0xc6, "/ztj"},
	{0x7c, "/pig"},     {0x7c, "/zt"},		/* after ztj */
	{0xc7, "/&>"},      {0xc7, "/ytj"},		/* must be end of "&" */
	{0x75, "/:#"},      {0x75, "/feid"},    {0x75, "/SARS"},
	{0x59, "/go"},      {0x59, "/shan"},
	{0x57, "/find"},    {0x57, "/zhao"},    {0x57, "/search"},
	{0x55, "/&"},       {0x55, "/mm"},      {0x55, "/beautiful_eyebrows"},
	{0x7d, "/cat"},     {0x7d, "/maom"},
	{0x7e, "/dog"},     {0x7e, "/xg"},
	{0x9a, "/$"},       {0x9a, "/qianc"},   {0x9a, "/money"},
	{0x9b, "/(!)"},     {0x9b, "/dp"},      {0x9b, "/lightbulb"},
	{0x60, "/cup"},     {0x60, "/bei"},
	{0x9f, "/music"},   {0x9f, "/yy"},
	{0x82, "/pill"},    {0x82, "/yw"},
	{0x64, "/kiss"},    {0x64, "/wen"},
	{0x83, "/meeting"}, {0x83, "/hy"},
	{0x84, "/phone"},   {0x84, "/dh"},
	{0x85, "/time"},    {0x85, "/sj"},
	{0x86, "/email"},   {0x86, "/yj"},
	{0x87, "/tv"},      {0x87, "/ds"},
	{0x50, "/<D>"},     {0x50, "/dd"},
	{0x51, "/<J>"},     {0x51,  "/mn"},     {0x51,  "/beauty"},
	{0x52, "/<H>"},     {0x52,  "/hl"},
	{0x53, "/<M>"},     {0x53,  "/mamao"},
	{0x54, "/<QQ>"},    {0x54,  "/qz"},     {0x54,  "/qq"},
	{0x5d, "/<B>"},     {0x5d,  "/bj"},     {0x5d,  "/baijiu"},
	{0x5f, "/<U>"},     {0x5f,  "/qsh"},    {0x5f,  "/soda"},
	{0x69, "/<!!>"},    {0x69,  "/xy"},     {0x69,  "/rain"},
	{0x6a, "/<~>"},     {0x6a,  "/duoy"},   {0x6a,  "/cloudy"},
	{0x6c, "/<Z>"},     {0x6c,  "/xr"},     {0x6c,  "/snowman"},
	{0x6d, "/<*>"},     {0x6d,  "/xixing"}, {0x6d,  "/star"},		/* after starving */
	{0x71, "/<00>"},    {0x71,  "/nv"},     {0x71,  "/woman"},
	{0x72, "/<11>"},    {0x72,  "/nan"},    {0x72,  "/man"},
	{0, NULL}
};
gint emoticons_std_num = sizeof(emoticons_std) / sizeof(qq_emoticon) - 1;

/* Map for purple smiley convert to qq, need qsort */
static qq_emoticon emoticons_ext[] = {
	{0xc7, "/&>"},		{0xa5, "/&-("},
	{0xbb, "/loveu"},
	{0x63, "/fade"},
	{0x8f, "/sleepy"},	{0x73, "/sad"},		{0x8e, "/starving"},
	{0xc0, "/turn"},
	{0xa0, "/victory"}, {0x77, "/vomit"},
	{0xc6, "/ztj"},
	{0, NULL}
};
gint emoticons_ext_num = sizeof(emoticons_ext) / sizeof(qq_emoticon) - 1;

/* Map for qq smiley convert to purple */
static qq_emoticon emoticons_sym[] = {
	{0x41, "/jy"},
	{0x42, "/pz"},
	{0x43, "/se"},
	{0x44, "/fd"},
	{0x45, "/dy"},
	{0x46, "/ll"},
	{0x47, "/hx"},
	{0x48, "/bz"},
	{0x49, "/shui"},
	{0x4a, "/dk"},
	{0x4b, "/gg"},
	{0x4c, "/fn"},
	{0x4d, "/tp"},
	{0x4e, "/cy"},
	{0x4f, "/wx"},
	{0x50, "/dd"},
	{0x51, "/mn"},
	{0x52, "/hl"},
	{0x53, "/mamao"},
	{0x54, "/qz"},
	{0x55, "/mm"},
	{0x56, "/fw"},
	{0x57, "/zhao"},
	{0x58, "/tiao"},
	{0x59, "/shan"},
	{0x5a, "/fad"},
	{0x5b, "/oh"},
	{0x5c, "/aiq"},
	{0x5d, "/bj"},
	{0x5e, "/zq"},
	{0x5f, "/qsh"},
	{0x60, "/bei"},
	{0x61, "/xig"},
	{0x62, "/mg"},
	{0x63, "/dx"},
	{0x64, "/wen"},
	{0x65, "/xin"},
	{0x66, "/xs"},
	{0x67, "/dg"},
	{0x68, "/lw"},
	{0x69, "/xy"},
	{0x6a, "/duoy"},
	{0x6b, "/ty"},
	{0x6c, "/xr"},
	{0x6d, "/xixing"},
	{0x6e, "/yl"},
	{0x6f, "/qiang"},
	{0x70, "/ruo"},
	{0x71, "/nv"},
	{0x72, "/nan"},
	{0x73, "/ng"},
	{0x74, "/kuk"},
	{0x75, "/feid"},
	{0x76, "/zk"},
	{0x77, "/tu"},
	{0x78, "/jk"},
	{0x79, "/sweat"},
	{0x7a, "/hanx"},
	{0x7b, "/db"},
	{0x7c, "/zt"},
	{0x7d, "/maom"},
	{0x7e, "/xg"},
	{0x7f, "/yb"},
	{0x80, "/coffee"},
	{0x81, "/fan"},
	{0x82, "/yw"},
	{0x83, "/hy"},
	{0x84, "/dh"},
	{0x85, "/sj"},
	{0x86, "/yj"},
	{0x87, "/ds"},
	{0x88, "/ws"},
	{0x89, "/bb"},
	{0x8a, "/tx"},
	{0x8b, "/ka"},
	{0x8c, "/by"},
	{0x8d, "/am"},
	{0x8e, "/jie"},
	{0x8f, "/kun"},
	{0x90, "/fendou"},
	{0x91, "/zhm"},
	{0x92, "/yiw"},
	{0x93, "/xu"},
	{0x94, "/yun"},
	{0x95, "/zhem"},
	{0x96, "/shuai"},
	{0x97, "/kl"},
	{0x98, "/qiao"},
	{0x99, "/zj"},
	{0x9a, "/qianc"},
	{0x9b, "/dp"},
	{0x9c, "/shd"},
	{0x9d, "/zhd"},
	{0x9e, "/dao"},
	{0x9f, "/yy"},
	{0xa0, "/shl"},
	{0xa1, "/lengh"},
	{0xa2, "/wipe"},
	{0xa3, "/kb"},
	{0xa4, "/gz"},
	{0xa5, "/qd"},
	{0xa6, "/huaix"},
	{0xa7, "/zhh"},
	{0xa8, "/yhh"},
	{0xa9, "/hq"},
	{0xaa, "/bs"},
	{0xab, "/wq"},
	{0xac, "/kk"},
	{0xad, "/yx"},
	{0xae, "/qq"},
	{0xaf, "/xia"},
	{0xb0, "/kel"},
	{0xb1, "/cd"},
	{0xb2, "/pj"},
	{0xb3, "/lq"},
	{0xb4, "/pp"},
	{0xb5, "/sa"},
	{0xb6, "/pc"},
	{0xb7, "/bq"},
	{0xb8, "/gy"},
	{0xb9, "/qt"},
	{0xba, "/cj"},
	{0xbb, "/aini"},
	{0xbc, "/bu"},
	{0xbd, "/hd"},
	{0xbe, "/zhq"},
	{0xbf, "/kt"},
	{0xc0, "/ht"},
	{0xc1, "/tsh"},
	{0xc2, "/hsh"},
	{0xc3, "/jd"},
	{0xc4, "/jw"},
	{0xc5, "/xw"},
	{0xc6, "/ztj"},
	{0xc7, "/ytj"},
	{0, NULL}
};
gint emoticons_sym_num = sizeof(emoticons_sym) / sizeof(qq_emoticon) - 1;;

static int emoticon_cmp(const void *k1, const void *k2)
{
	const qq_emoticon *e1 = (const qq_emoticon *) k1;
	const qq_emoticon *e2 = (const qq_emoticon *) k2;
	if (e1->symbol == 0) {
		/* purple_debug_info("QQ", "emoticon_cmp len %d\n", strlen(e2->name)); */
		return strncmp(e1->name, e2->name, strlen(e2->name));
	}
	if (e2->symbol == 0) {
		/* purple_debug_info("QQ", "emoticon_cmp len %d\n", strlen(e1->name)); */
		return strncmp(e1->name, e2->name, strlen(e1->name));
	}
	return strcmp(e1->name, e2->name);
}

static void emoticon_try_sort()
{
	if (emoticons_is_sorted)
		return;

	purple_debug_info("QQ", "qsort stand emoticons\n");
	qsort(emoticons_std, emoticons_std_num, sizeof(qq_emoticon), emoticon_cmp);
	purple_debug_info("QQ", "qsort extend emoticons\n");
	qsort(emoticons_ext, emoticons_ext_num, sizeof(qq_emoticon), emoticon_cmp);
	emoticons_is_sorted = TRUE;
}

static qq_emoticon *emoticon_find(gchar *name)
{
	qq_emoticon *ret = NULL;
	qq_emoticon key;

	g_return_val_if_fail(name != NULL, NULL);
	emoticon_try_sort();

	key.name = name;
	key.symbol = 0;

	/* purple_debug_info("QQ", "bsearch emoticon %.20s\n", name); */
	ret = (qq_emoticon *)bsearch(&key, emoticons_ext, emoticons_ext_num,
			sizeof(qq_emoticon), emoticon_cmp);
	if (ret != NULL) {
		return ret;
	}
	ret = (qq_emoticon *)bsearch(&key, emoticons_std, emoticons_std_num,
			sizeof(qq_emoticon), emoticon_cmp);
	return ret;
}

static gchar *emoticon_get(guint8 symbol)
{
	g_return_val_if_fail(symbol >= emoticons_sym[0].symbol, NULL);
	g_return_val_if_fail(symbol <= emoticons_sym[emoticons_sym_num - 2].symbol, NULL);

	return emoticons_sym[symbol - emoticons_sym[0].symbol].name;
}

/* convert qq emote icon to purple sytle
   Notice: text is in qq charset, GB18030 or utf8 */
gchar *qq_emoticon_to_purple(gchar *text)
{
	gchar *ret;
	GString *converted;
	gchar **segments;
	gboolean have_smiley;
	gchar *purple_smiley;
	gchar *cur;
	guint8 symbol;

	/* qq_show_packet("text", (guint8 *)text, strlen(text)); */
	g_return_val_if_fail(text != NULL && strlen(text) != 0, g_strdup(""));

	while ((cur = strchr(text, '\x14')) != NULL)
		*cur = '\x15';

	segments = g_strsplit(text, "\x15", 0);
	if(segments == NULL) {
		return g_strdup("");
	}

	converted = g_string_new("");
	have_smiley = FALSE;
	if (segments[0] != NULL) {
		g_string_append(converted, segments[0]);
	} else {
		purple_debug_info("QQ", "segments[0] is NULL\n");
	}
	while ((*(++segments)) != NULL) {
		have_smiley = TRUE;

		cur = *segments;
		if (cur == NULL) {
			purple_debug_info("QQ", "current segment is NULL\n");
			break;
		}
		if (strlen(cur) == 0) {
			purple_debug_info("QQ", "current segment length is 0\n");
			break;
		}
		symbol = (guint8)cur[0];

		purple_smiley = emoticon_get(symbol);
		if (purple_smiley == NULL) {
			purple_debug_info("QQ", "Not found smiley of 0x%02X\n", symbol);
			g_string_append(converted, "<IMG ID=\"0\">");
		} else {
			purple_debug_info("QQ", "Found 0x%02X smiley is %s\n", symbol, purple_smiley);
			g_string_append(converted, purple_smiley);
			g_string_append(converted, cur + 1);
		}
		/* purple_debug_info("QQ", "next segment\n"); */
	}

	/* purple_debug_info("QQ", "end of convert\n"); */
	if (!have_smiley) {
		g_string_prepend(converted, "<font sml=\"none\">");
		g_string_append(converted, "</font>");
	}
	ret = converted->str;
	g_string_free(converted, FALSE);
	return ret;
}

void qq_im_fmt_free(qq_im_format *fmt)
{
	g_return_if_fail(fmt != NULL);
	if (fmt->font)	g_free(fmt->font);
	g_free(fmt);
}

qq_im_format *qq_im_fmt_new_default(void)
{
	qq_im_format *fmt;
	const gchar msyh[] = {
		0xE5, 0xBE, 0xAE, 0xE8, 0xBD, 0xAF, 0xE9, 0x9B, 0x85, 0xE9, 0xBB, 0x91, 0x00
	};		/*Microsoft YaHei*/
	fmt = g_new0(qq_im_format, 1);
	memset(fmt, 0, sizeof(qq_im_format));
	fmt->font_len = strlen(msyh);
	fmt->font = g_strdup(msyh);
	fmt->font_size = 14;
	/* encoding, 0x8622=GB, 0x0000=EN */
	fmt->charset = 0x8622;

	return fmt;
}

qq_im_format *qq_im_fmt_new_by_purple(const gchar *msg)
{
	qq_im_format *fmt;
	const gchar *start, *end, *last;
	GData *attribs;
	gchar *tmp;

	g_return_val_if_fail(msg != NULL, NULL);

	fmt = qq_im_fmt_new_default();

	last = msg;
	while (purple_markup_find_tag("font", last, &start, &end, &attribs)) {
		tmp = g_datalist_get_data(&attribs, "face");
		if (tmp && strlen(tmp) > 0) {
			if (fmt->font)	g_free(fmt->font);
			fmt->font_len = strlen(tmp);
			fmt->font = g_strdup(tmp);
		}

		tmp = g_datalist_get_data(&attribs, "size");
		if (tmp) {
			fmt->attr = atoi(tmp) * 3 + 1;
			fmt->attr &= 0x0f;
		}

		tmp = g_datalist_get_data(&attribs, "color");
		if (tmp && strlen(tmp) > 1) {
			unsigned char *rgb;
			gsize rgb_len;
			rgb = purple_base16_decode(tmp + 1, &rgb_len);
			if (rgb != NULL && rgb_len >= 3)
				g_memmove(fmt->rgb, rgb, 3);
			g_free(rgb);
		}

		g_datalist_clear(&attribs);
		last = end + 1;
	}

	if (purple_markup_find_tag("b", msg, &start, &end, &attribs)) {
		fmt->attr ^= 0x01;
		g_datalist_clear(&attribs);
	}

	if (purple_markup_find_tag("i", msg, &start, &end, &attribs)) {
		fmt->attr ^= 0x02;
		g_datalist_clear(&attribs);
	}

	if (purple_markup_find_tag("u", msg, &start, &end, &attribs)) {
		fmt->attr ^= 0x04;
		g_datalist_clear(&attribs);
	}

	return fmt;
}

/* convert qq format to purple */
gchar * qq_im_fmt_to_purple( qq_im_format *fmt, GString *text )
{
	GString *tmp;
	gchar *ret;

	tmp = g_string_new("");
	g_string_append_printf(tmp, "<font color=\"#%02x%02x%02x\">",
		fmt->rgb[0], fmt->rgb[1], fmt->rgb[2]);
	g_string_prepend(text, tmp->str);
	g_string_set_size(tmp, 0);
	g_string_append(text, "</font>");

	if (fmt->font != NULL) {
		g_string_append_printf(tmp, "<font face=\"%s\">", fmt->font);
		g_string_prepend(text, tmp->str);
		g_string_set_size(tmp, 0);
		g_string_append(text, "</font>");
	}
	if (fmt->font_size >= 0) {
		g_string_append_printf(tmp, "<font size=\"%d\">", fmt->font_size);
		g_string_prepend(text, tmp->str);
		g_string_set_size(tmp, 0);
		g_string_append(text, "</font>");
	}
	if (fmt->attr & 0x01) {
		/* bold */
		g_string_prepend(text, "<b>");
		g_string_append(text, "</b>");
	}
	if (fmt->attr & 0x02) {
		/* italic */
		g_string_prepend(text, "<i>");
		g_string_append(text, "</i>");
	}
	if (fmt->attr & 0x04) {
		/* underline */
		g_string_prepend(text, "<u>");
		g_string_append(text, "</u>");
	}

	g_string_free(tmp, TRUE);
	ret = text->str;
	return ret;
}

/* data includes text msg and font attr*/
gint qq_get_im_tail(qq_im_format *fmt, guint8 *data, gint data_len)
{
	gint bytes, text_len;
	guint8 tail_len;
	guint8 font_len;

	g_return_val_if_fail(fmt != NULL && data != NULL, 0);
	g_return_val_if_fail(data_len > 1, 0);
	tail_len = data[data_len - 1];
	g_return_val_if_fail(tail_len > 2, 0);
	text_len = data_len - tail_len;
	g_return_val_if_fail(text_len >= 0, 0);

	bytes = text_len;
	/* qq_show_packet("IM tail", data + bytes, tail_len); */
	bytes += 1;		/* skip 0x00 */
	bytes += qq_get8(&fmt->attr, data + bytes);
	bytes += qq_getdata(fmt->rgb, sizeof(fmt->rgb), data + bytes);	/* red,green,blue */
 	bytes += 1;	/* skip 0x00 */
	bytes += qq_get16(&fmt->charset, data + bytes);

	font_len = data_len - bytes - 1;
	g_return_val_if_fail(font_len > 0, bytes + 1);

	fmt->font_len = font_len;
	if (fmt->font != NULL)	g_free(fmt->font);
	fmt->font = g_strndup((gchar *)data + bytes, fmt->font_len);
	return tail_len;
}

void qq_got_message(PurpleConnection *gc, const gchar *msg)
{
	qq_data *qd;
	gchar *from;
	time_t now = time(NULL);

	g_return_if_fail(gc != NULL  && gc->proto_data != NULL);
	qd = gc->proto_data;

	g_return_if_fail(qd->uid > 0);

	qq_buddy_find_or_new(gc, qd->uid);

	from = uid_to_purple_name(qd->uid);
	serv_got_im(gc, from, msg, PURPLE_MESSAGE_SYSTEM, now);
	g_free(from);
}

/* process received normal text IM */
static void process_im_text(PurpleConnection *gc, guint8 *data, gint len, qq_im_header *im_header, guint16 msg_type)
{
	guint16 purple_msg_flag;
	gchar *who;
	gchar *msg_smiley, *msg_fmt, *msg_utf8;
	PurpleBuddy *buddy;
	qq_buddy_data *bd;
	gint bytes, tail_len;
	qq_im_format *fmt = NULL;
	guint8 type;
	guint8 *msg_data;
	//guint msg_dataseg_len;
	//guint8 msg_dataseg_flag;
	//guint msg_dataseg_pos;
	gchar *text;
	gchar *emoticon;
	gchar *purple_smiley;

	struct {
		guint16 msg_seq;
		guint32 send_time;
		guint16 sender_icon;
		guint8 unknown1[3];
		guint8 has_font_attr;
		guint8 fragment_count;
		guint8 fragment_index;
		guint16 msg_id;
		guint8 unknown2;
		guint8 auto_reply;
		GString *msg;		/* no fixed length, ends with 0x00 */
	} im_text;

	g_return_if_fail(data != NULL && len > 0);
	g_return_if_fail(im_header != NULL);

	memset(&im_text, 0, sizeof(im_text));

	/* qq_show_packet("IM text", data, len); */
	bytes = 0;
	bytes += qq_get16(&(im_text.msg_seq), data + bytes);
	bytes += qq_get32(&(im_text.send_time), data + bytes);
	bytes += qq_get16(&(im_text.sender_icon), data + bytes);
	bytes += qq_getdata(im_text.unknown1, sizeof(im_text.unknown1), data + bytes); /* 0x(00 00 00)*/
	bytes += qq_get8(&(im_text.has_font_attr), data + bytes);
	bytes += qq_get8(&(im_text.fragment_count), data + bytes);
	bytes += qq_get8(&(im_text.fragment_index), data + bytes);
	bytes += qq_get16(&(im_text.msg_id), data + bytes);
	bytes += qq_get8(&(im_text.auto_reply), data + bytes);
	purple_debug_info("QQ", "IM Seq %u, id %04X, fragment %d-%d, type %d, %s\n",
			im_text.msg_seq, im_text.msg_id,
			im_text.fragment_count, im_text.fragment_index,
			im_text.auto_reply,
			im_text.has_font_attr ? "exist font attr" : "");

	who = uid_to_purple_name(im_header->uid_from);
	buddy = purple_find_buddy(gc->account, who);
	if (buddy == NULL) {
		/* create no-auth buddy */
		buddy = qq_buddy_new(gc, im_header->uid_from);
	}
	bd = (buddy == NULL) ? NULL : purple_buddy_get_protocol_data(buddy);
	if (bd != NULL) {
		bd->client_tag = im_header->version_from;
		bd->face = im_text.sender_icon;
		qq_update_buddy_icon(gc->account, who, bd->face);
	}
	purple_msg_flag = (im_text.auto_reply == QQ_IM_AUTO_REPLY)
		? PURPLE_MESSAGE_AUTO_RESP : 0;

	if (msg_type == QQ_MSG_BUDDY_A6 || msg_type == QQ_MSG_BUDDY_78)
	{
		bytes += 8;		//4d 53 47 00 00 00 00 00		MSG.....
		bytes += qq_get32(&(im_text.send_time), data + bytes);
		bytes += 4;		//random guint32;

		if (im_text.has_font_attr)
		{
			fmt = g_new0(qq_im_format, 1);
			
			bytes += 1;		//Unknown 0x00

			bytes += qq_get8(&fmt->rgb[0], data+bytes);
			bytes += qq_get8(&fmt->rgb[1], data+bytes);
			bytes += qq_get8(&fmt->rgb[2], data+bytes);

			bytes += qq_get8(&fmt->font_size, data+bytes);
			/* font attr guint8 : bold:00000001 XOR italic 00000010 XOR underline 00000100*/
			bytes += qq_get8(&fmt->attr, data+bytes);
			/* encoding, 0x8622=GB, 0x0000=EN */
			bytes += qq_get16(&fmt->charset, data+bytes);

			bytes += qq_get_vstr(&fmt->font, NULL, sizeof(guint16), data+bytes);
		} 

		bytes += 2;
		im_text.msg = g_string_new("");
		while (bytes < len) {
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
				}
				*/
			}
		}

		if (fmt != NULL) {
			msg_utf8 = qq_im_fmt_to_purple(fmt, im_text.msg);
			qq_im_fmt_free(fmt);
		} else {
			msg_utf8 =  im_text.msg->str;
		}

	} else {

		if (im_text.has_font_attr) {
			fmt = qq_im_fmt_new_default();
			tail_len = qq_get_im_tail(fmt, data + bytes, len - bytes);
			im_text.msg = g_string_new_len((gchar *)(data + bytes), len-tail_len);
		} else	{
			im_text.msg = g_string_new_len((gchar *)(data + bytes), len - bytes);
		}
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

	}

	/* qq_show_packet("IM text", (guint8 *)im_text.msg , strlen(im_text.msg) ); */

	/* send encoded to purple, note that we use im_text.send_time,
	 * not the time we receive the message
	 * as it may have been delayed when I am not online. */
	purple_debug_info("QQ", "IM from %u: %s\n", im_header->uid_from,msg_utf8);
	serv_got_im(gc, who, msg_utf8, purple_msg_flag, (time_t) im_text.send_time);

	g_free(msg_utf8);
	g_free(who);
	g_string_free (im_text.msg, TRUE);
}

/* it is a normal IM, maybe text or video request */
void qq_process_im( PurpleConnection *gc, guint8 *data, gint len, guint16 msg_type )
{
	gint bytes = 0;
	qq_im_header im_header;

	g_return_if_fail (data != NULL && len > 0);

	bytes = get_im_header(&im_header, data, len);
	if (bytes < 0) {
		purple_debug_error("QQ", "Fail read im header, len %d\n", len);
		qq_show_packet ("IM Header", data, len);
		return;
	}
	purple_debug_info("QQ",
			"Got IM to %u, type: %02X from: %u ver: %s (%04X)\n",
			im_header.uid_to, im_header.im_type, im_header.uid_from,
			qq_get_ver_desc(im_header.version_from), im_header.version_from);

	switch (im_header.im_type) {
		case QQ_NORMAL_IM_TEXT:
			if (bytes >= len - 1) {
				purple_debug_warning("QQ", "Received normal IM text is empty\n");
				return;
			}
			process_im_text(gc, data + bytes, len - bytes, &im_header, msg_type);
			break;
		case QQ_NORMAL_IM_FILE_REJECT_UDP:
			qq_process_recv_file_reject(data + bytes, len - bytes, im_header.uid_from, gc);
			break;
		case QQ_NORMAL_IM_FILE_APPROVE_UDP:
			qq_process_recv_file_accept(data + bytes, len - bytes, im_header.uid_from, gc);
			break;
		case QQ_NORMAL_IM_FILE_REQUEST_UDP:
			qq_process_recv_file_request(data + bytes, len - bytes, im_header.uid_from, gc);
			break;
		case QQ_NORMAL_IM_FILE_CANCEL:
			qq_process_recv_file_cancel(data + bytes, len - bytes, im_header.uid_from, gc);
			break;
		case QQ_NORMAL_IM_FILE_NOTIFY:
			qq_process_recv_file_notify(data + bytes, len - bytes, im_header.uid_from, gc);
			break;
		case QQ_NORMAL_IM_FILE_REQUEST_TCP:
			/* Check ReceivedFileIM::parseContents in eva*/
			/* some client use this function for detect invisable buddy*/
		case QQ_NORMAL_IM_FILE_APPROVE_TCP:
		case QQ_NORMAL_IM_FILE_REJECT_TCP:
		case QQ_NORMAL_IM_FILE_PASV:
		case QQ_NORMAL_IM_FILE_EX_REQUEST_UDP:
		case QQ_NORMAL_IM_FILE_EX_REQUEST_ACCEPT:
		case QQ_NORMAL_IM_FILE_EX_REQUEST_CANCEL:
		case QQ_NORMAL_IM_FILE_EX_NOTIFY_IP:
			qq_show_packet ("Not support", data, len);
			break;
		default:
			/* a simple process here, maybe more later */
			qq_show_packet ("Unknow", data + bytes, len - bytes);
			return;
	}
}

/* send an IM to uid_to */
static void request_send_im(PurpleConnection *gc, guint32 uid_to, guint8 type, qq_im_format *fmt, GString *msg, guint16 msg_id, guint8 frag_count, guint8 frag_index)
{
	qq_data *qd;
	guint8 raw_data[1024];
	gint bytes;
	time_t now;
	static guint8 fill[] = {
		0x00, 0x00, 0x00, 0x0D, 
		0x00, 0x01, 0x00, 0x04,
		0x00, 0x00, 0x00, 0x00, 
		0x00, 0x03, 0x00, 0x01, 0x01
	};
	qd = (qq_data *) gc->proto_data;

	/* purple_debug_info("QQ", "Send IM %d-%d\n", frag_count, frag_index); */
	bytes = 0;
	/* 000-003: receiver uid */
	bytes += qq_put32(raw_data + bytes, qd->uid);
	/* 004-007: sender uid */
	bytes += qq_put32(raw_data + bytes, uid_to);
	/* 008-024: Unknown */
	bytes += qq_putdata(raw_data + bytes, fill, sizeof(fill));
	/* 025-026: sender client version */
	bytes += qq_put16(raw_data + bytes, qd->client_tag);
	/* 027-030: receiver uid */
	bytes += qq_put32(raw_data + bytes, qd->uid);
	/* 031-034: sender uid */
	bytes += qq_put32(raw_data + bytes, uid_to);
	/* 035-040: md5 of (uid+session_key) */
	bytes += qq_putdata(raw_data + bytes, qd->session_md5, 16);
	/* 041-042: message type */
	bytes += qq_put16(raw_data + bytes, QQ_NORMAL_IM_TEXT);
	/* 042-043: sequence number */
	bytes += qq_put16(raw_data + bytes, qd->send_seq);
	/* 044-047: send time */
	now = time(NULL);
	bytes += qq_put32(raw_data + bytes, (guint32) now);
	/* 048-049: sender icon */
	bytes += qq_put16(raw_data + bytes, qd->my_icon);
	/* 050-052: 00 00 00 Unknown */
	bytes += qq_put32(raw_data + bytes ,0);
	bytes --;
	/* 050-050: have_font_attribute 0x01 */
	bytes += qq_put8(raw_data + bytes, 0x01);
	/* 051-051: slice count */
	bytes += qq_put8(raw_data + bytes, frag_count);
	/* 052-052: slice index */
	bytes += qq_put8(raw_data + bytes, frag_index);
	/* 053-053: msg_id */
	bytes += qq_put16(raw_data + bytes, msg_id);
	/* 052-052: text message type (normal/auto-reply) */
	bytes += qq_put8(raw_data + bytes, type);
	/* "MSG" */
	bytes += qq_put32(raw_data + bytes, 0x4D534700);
	bytes += qq_put32(raw_data + bytes, 0x00000000);
	bytes += qq_put32(raw_data + bytes, (guint32) now);
	/* Likely a random int */
	srand((unsigned)now);
	bytes += qq_put32(raw_data + bytes, rand());
	/* font attr set */
	bytes += qq_put8(raw_data + bytes, 0x00);
	bytes += qq_put8(raw_data + bytes, fmt->rgb[0]);
	bytes += qq_put8(raw_data + bytes, fmt->rgb[1]);
	bytes += qq_put8(raw_data + bytes, fmt->rgb[2]);
	bytes += qq_put8(raw_data + bytes, fmt->font_size);
	bytes += qq_put8(raw_data + bytes, fmt->attr);
	bytes += qq_put16(raw_data + bytes, fmt->charset);
	bytes += qq_put16(raw_data + bytes, fmt->font_len);
	bytes += qq_putdata(raw_data + bytes, fmt->font, fmt->font_len);

	bytes += qq_put16(raw_data + bytes, 0x0000);
	/* 053-   : msg does not end with 0x00 */
	bytes += qq_putdata(raw_data + bytes, (guint8 *)msg->str, msg->len);

	/* qq_show_packet("QQ_CMD_SEND_IM", raw_data, bytes); */
	qq_send_cmd(gc, QQ_CMD_SEND_IM, raw_data, bytes);
}

GSList *qq_im_get_segments(gchar *msg_stripped, gboolean is_smiley_none)
{
	GSList *string_list = NULL;
	GString *string_seg;
	gchar *start, *p;
	guint msg_len;
	guint16 string_len;
	guint16 string_ad_len;
	qq_emoticon *emoticon;
	static gchar em_v[] = {
		0x02, 0x00, 0x09, 0x01, 0x00, 0x01, 0x4F, 0xFF, 0x00, 0x02, 0x14, 0xA0
	};

	g_return_val_if_fail(msg_stripped != NULL, NULL);

	//start = msg_stripped;
	msg_len = strlen(msg_stripped);
	string_seg = g_string_new("");

	p = msg_stripped;
	while (p)
	{
		start = p;

		p = g_utf8_strchr(p, -1, '/');
		if (!p)	string_len = msg_stripped + msg_len - start;		//if not find emoticon, append all remained data
		else 	string_len = p - start;

		if (string_len >0)
		{
			if (string_seg->len + string_len + 6 > QQ_MSG_IM_MAX) {
				/* enough chars to send */
				string_list = g_slist_append(string_list, string_seg);
				string_seg = g_string_new("");
			}
			g_string_append_c(string_seg, 0x01);		//TEXT FLAG
			string_ad_len = string_len + 3;		//len of text with prepended data
			string_ad_len = htons(string_ad_len);
			g_string_append_len(string_seg, (gchar *)&string_ad_len, sizeof(guint16));
			g_string_append_c(string_seg, 0x01);		//Unknown FLAG
			string_ad_len = htons(string_len);
			g_string_append_len(string_seg, (gchar *)&string_ad_len, sizeof(guint16));
			g_string_append_len(string_seg, start, string_len);
		}

		/* Append emoticon */
		if ( !is_smiley_none && p )
		{
			if (string_seg->len  + 12 > QQ_MSG_IM_MAX) {
				/* enough chars to send */
				string_list = g_slist_append(string_list, string_seg);
				string_seg = g_string_new("");
			}
			emoticon = emoticon_find(p);
			if (emoticon != NULL) {
				purple_debug_info("QQ", "found emoticon %s as 0x%02X\n",
						emoticon->name, emoticon->symbol);
				/* Until Now, We haven't the new emoticon key database */
				/* So Just Send a Settled V Gesture */
				g_string_append_len(string_seg, em_v, sizeof(em_v));

				//g_string_append_c(string_seg, 0x14);
				//g_string_append_c(string_seg, emoticon->symbol);
				p += strlen(emoticon->name);
				continue;
			} else {
				purple_debug_info("QQ", "Not found emoticon %.20s\n", p);
			}
		}
	}

	if (string_seg->len > 0) {
		string_list = g_slist_append(string_list, string_seg);
	}
	return string_list;
}

gboolean qq_im_smiley_none(const gchar *msg)
{
	const gchar *start, *end, *last;
	GData *attribs;
	gchar *tmp;
	gboolean ret = FALSE;

	g_return_val_if_fail(msg != NULL, TRUE);

	last = msg;
	while (purple_markup_find_tag("font", last, &start, &end, &attribs)) {
		tmp = g_datalist_get_data(&attribs, "sml");
		if (tmp && strlen(tmp) > 0) {
			if (strcmp(tmp, "none") == 0) {
				ret = TRUE;
				break;
			}
		}
		g_datalist_clear(&attribs);
		last = end + 1;
	}
	return ret;
}

/* Grab custom emote icons
static GSList*  qq_grab_emoticons(const char *msg, const char*username)
{
	GSList *list;
	GList *smileys;
	PurpleSmiley *smiley;
	const char *smiley_shortcut;
	char *ptr;
	int length;
	PurpleStoredImage *img;

	smileys = purple_smileys_get_all();
	length = strlen(msg);

	for (; smileys; smileys = g_list_delete_link(smileys, smileys)) {
		smiley = smileys->data;
		smiley_shortcut = purple_smiley_get_shortcut(smiley);
		purple_debug_info("QQ", "Smiley shortcut [%s]\n", smiley_shortcut);

		ptr = g_strstr_len(msg, length, smiley_shortcut);

		if (!ptr)
			continue;

		purple_debug_info("QQ", "Found Smiley shortcut [%s]\n", smiley_shortcut);

		img = purple_smiley_get_stored_image(smiley);

		emoticon = g_new0(MsnEmoticon, 1);
		emoticon->smile = g_strdup(purple_smiley_get_shortcut(smiley));
		emoticon->obj = msn_object_new_from_image(img,
				purple_imgstore_get_filename(img),
				username, MSN_OBJECT_EMOTICON);

 		purple_imgstore_unref(img);
		list = g_slist_prepend(list, emoticon);
	}
	return list;
}
*/

gint qq_send_im(PurpleConnection *gc, const gchar *who, const gchar *what, PurpleMessageFlags flags)
{
	qq_data *qd;
	guint32 uid_to;
	guint8 type;
	qq_im_format *fmt;
	gchar *msg_stripped, *tmp;
	GSList *segments, *it;
	gint msg_len;
	const gchar *start_invalid;
	gboolean is_smiley_none;
	guint8 frag_count, frag_index;
	guint16 msg_id;

	g_return_val_if_fail(NULL != gc && NULL != gc->proto_data, -1);
	g_return_val_if_fail(who != NULL && what != NULL, -1);

	qd = (qq_data *) gc->proto_data;
	purple_debug_info("QQ", "Send IM to %s, msg_len %" G_GSIZE_FORMAT ":\n%s\n", who, strlen(what), what);

	uid_to = purple_name_to_uid(who);
	if (uid_to == qd->uid) {
		/* if msg is to myself, bypass the network */
		serv_got_im(gc, who, what, flags, time(NULL));
		return 1;
	}

	type = (flags == PURPLE_MESSAGE_AUTO_RESP ? QQ_IM_AUTO_REPLY : QQ_IM_TEXT);
	/* qq_show_packet("IM UTF8", (guint8 *)what, strlen(what)); */

	msg_stripped = purple_markup_strip_html(what);
	g_return_val_if_fail(msg_stripped != NULL, -1);
	/* qq_show_packet("IM Stripped", (guint8 *)what, strlen(what)); */

	/* Check and valid utf8 string */
	msg_len = strlen(msg_stripped);
	g_return_val_if_fail(msg_len > 0, -1);
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
	msg_id = qd->send_im_id;
	fmt = qq_im_fmt_new_by_purple(what);
	frag_count = g_slist_length(segments);
	frag_index = 0;
	for (it = segments; it; it = it->next) {
		request_send_im(gc, uid_to, type, fmt, (GString *)it->data, msg_id, frag_count, frag_index);
		g_string_free(it->data, TRUE);
		frag_index++;
	}
	g_slist_free(segments);
	qq_im_fmt_free(fmt);
	return 1;
}
