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

#define QQ_MSG_IM_MAX               512	/* max length of IM */

enum {
	QQ_IM_TEXT = 0x01,
	QQ_IM_AUTO_REPLY = 0x02
};

enum
{
	QQ_NORMAL_IM_VIBRATE = 0x00AF,
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
	guint8 index;
	gchar *name;
};

static gboolean emoticons_is_sorted = FALSE;
/* Map for purple smiley convert to qq, need qsort */
static qq_emoticon emoticons[] = {
	{0x4f, 0x0E, "/:)$"},      {0x4f, 0x0E, "/wx$"},      {0x4f, 0x0E, "/small_smile$"},
	{0x42, 0x01, "/:~$"},      {0x42, 0x01, "/pz$"},      {0x42, 0x01, "/curl_lip$"},
	{0x43, 0x02, "/:*$"},      {0x43, 0x02, "/se$"},      {0x43, 0x02, "/desire$"},
	{0x44, 0x03, "/:|$"},      {0x44, 0x03, "/fd$"},      {0x44, 0x03, "/dazed$"},
	{0x45, 0x04, "/8-)$"},     {0x45, 0x04, "/dy$"},      {0x45, 0x04, "/revel$"},
	{0x46, 0x05, "/:<$"},      {0x46, 0x05, "/ll$"},      {0x46, 0x05, "/cry$"},
	{0x47, 0x06, "/:$$"},      {0x47, 0x06, "/hx$"},      {0x47, 0x06, "/bashful$"},
	{0x48, 0x07, "/:x$"},      {0x48, 0x07, "/bz$"},      {0x48, 0x07, "/shut_mouth$"},
	{0x49, 0x08, "/:z$"},      {0x49, 0x08, "/shui$"},    {0x49, 0x08, "/sleep$"},
	{0x4a, 0x09, "/:'$"},      {0x4a, 0x09, "/dk$"},      {0x4a, 0x09, "/weep$"},
	{0x4b, 0x0A, "/:-|$"},     {0x4b, 0x0A, "/gg$"},      {0x4b, 0x0A, "/embarassed$"},
	{0x4c, 0x0B, "/:@$"},      {0x4c, 0x0B, "/fn$"},      {0x4c, 0x0B, "/pissed_off$"},
	{0x4d, 0x0C, "/:P$"},      {0x4d, 0x0C, "/tp$"},      {0x4d, 0x0C, "/act_up$"},
	{0x4e, 0x0D, "/:D$"},      {0x4e, 0x0D, "/cy$"},      {0x4e, 0x0D, "/toothy_smile$"},
	{0x41, 0x00, "/:O$"},      {0x41, 0x00, "/jy$"},      {0x41, 0x00, "/surprised$"},
	{0x73, 0x0F, "/:($"},      {0x73, 0x0F, "/ng$"},      {0x73, 0x0F, "/sad$"},
	{0x74, 0x10, "/:+$"},      {0x74, 0x10, "/kuk$"},     {0x74, 0x10, "/cool$"},
	{0xa1, 0x60, "/--b$"},     {0xa1, 0x60, "/lengh$"},
	{0x76, 0x12, "/:Q$"},      {0x76, 0x12, "/zk$"},      {0x76, 0x12, "/crazy$"},
	{0x77, 0x13, "/:t$"},      {0x77, 0x13, "/tu$"},      {0x77, 0x13, "/vomit$"},
	{0x8a, 0x13, "/;P$"},      {0x8a, 0x13, "/tx$"},      {0x8a, 0x13, "/titter$"},
	{0x8b, 0x14, "/;-D$"},     {0x8b, 0x14, "/ka$"},      {0x8b, 0x14, "/cute$"},
	{0x8c, 0x16, "/;d$"},      {0x8c, 0x16, "/baiy$"},      {0x8c, 0x16, "/disdain$"},
	{0x8d, 0x17, "/;o$"},      {0x8d, 0x17, "/am$"},      {0x8d, 0x17, "/arrogant$"},
	{0x8e, 0x18, "/:g$"},      {0x8e, 0x18, "/jie$"},     {0x8e, 0x18, "/starving$"},
	{0x8f, 0x19, "/|-)$"},     {0x8f, 0x19, "/kun$"},     {0x8f, 0x19, "/sleepy$"},
	{0x78, 0x1A, "/:!$"},      {0x78, 0x1A, "/jk$"},      {0x78, 0x1A, "/terror$"},
	{0x79, 0x1B, "/:L$"},      {0x79, 0x1B, "/lh$"},      {0x79, 0x1B, "/sweat$"},
	{0x7a, 0x1C, "/:>$"},      {0x7a, 0x1C, "/hanx$"},    {0x7a, 0x1C, "/smirk$"},
	{0x7b, 0x1D, "/:;$"},      {0x7b, 0x1D, "/db$"},      {0x7b, 0x1D, "/soldier$"},
	{0x90, 0x1E, "/;f$"},      {0x90, 0x1E, "/fendou$"},  {0x90, 0x1E, "/struggle$"},
	{0x91, 0x1F, "/:-S$"},     {0x91, 0x1F, "/zhm$"},     {0x91, 0x1F, "/curse$"},
	{0x92, 0x20, "/?$"},       {0x92, 0x20, "/yiw$"},     {0x92, 0x20, "/question$"},
	{0x93, 0x21, "/;x$"},      {0x93, 0x21, "/xu$"},      {0x93, 0x21, "/shh$"},
	{0x94, 0x22, "/;@$"},      {0x94, 0x22, "/yun$"},     {0x94, 0x22, "/dizzy$"},
	{0x95, 0x23, "/:8$"},      {0x95, 0x23, "/zhem$"},    {0x95, 0x23, "/excrutiating$"},
	{0x96, 0x24, "/;!$"},      {0x96, 0x24, "/shuai$"},   {0x96, 0x24, "/freaked_out$"},
	{0x97, 0x25, "/!!!$"},     {0x97, 0x25, "/kl$"},      {0x97, 0x25, "/skeleton$"},
	{0x98, 0x26, "/xx$"},      {0x98, 0x26, "/qiao$"},    {0x98, 0x26, "/hammer$"},
	{0x99, 0x27, "/bye$"},     {0x99, 0x27, "/zj$"}, 
	{0xa2, 0x61, "/wipe$"},    {0xa2, 0x61, "/ch$"},
	{0xa3, 0x62, "/dig$"},     {0xa3, 0x62, "/kb$"},
	{0xa4, 0x63, "/handclap$"},{0xa4, 0x63, "/gz$"},
	{0xa5, 0x64, "/&-($"},     {0xa5, 0x64, "/qd$"},
	{0xa6, 0x65, "/B-)$"},     {0xa6, 0x65, "/huaix$"},
	{0xa7, 0x66, "/<@$"},      {0xa7, 0x66, "/zhh$"},
	{0xa8, 0x67, "/@>$"},      {0xa8, 0x67, "/yhh$"},
	{0xa9, 0x68, "/:-O$"},     {0xa9, 0x68, "/hq$"},
	{0xaa, 0x69, "/>-|$"},     {0xaa, 0x69, "/bs$"},
	{0xab, 0x6A, "/P-($"},     {0xab, 0x6A, "/wq$"},
	{0xac, 0x6B, "/:'|$"},     {0xac, 0x6B, "/kk$"},
	{0xad, 0x6C, "/X-)$"},     {0xad, 0x6C, "/yx$"},
	{0xae, 0x6D, "/:*$"},      {0xae, 0x6D, "/qq$"},
	{0xaf, 0x6E, "/@x$"},      {0xaf, 0x6E, "/xia$"},
	{0xb0, 0x6F, "/8*$"},      {0xb0, 0x6F, "/kel$"},
	{0xb1, 0x70, "/pd$"},      {0xb1, 0x70, "/cd$"},
	{0x61, 0x59, "/<W>$"},     {0x61, 0x59, "/xig$"},     {0x61, 0x59, "/watermelon$"},
	{0xb2, 0x71, "/beer$"},    {0xb2, 0x71, "/pj$"},
	{0xb3, 0x72, "/basketb$"}, {0xb3, 0x72, "/lq$"},
	{0xb4, 0x73, "/oo$"},      {0xb4, 0x73, "/pp$"},
	{0x80, 0x3C, "/coffee$"},  {0x80, 0x3C, "/kf$"},
	{0x81, 0x3D, "/eat$"},     {0x81, 0x3D, "/fan$"},
	{0x7c, 0x2E, "/pig$"},     {0x7c, 0x2E, "/zt$"},	
	{0x62, 0x3F, "/rose$"},    {0x62, 0x3F, "/mg$"},
	{0x63, 0x40, "/fade$"},    {0x63, 0x40, "/dx$"},      {0x63, 0x40, "/wilt$"},
	{0xb5, 0x74, "/showlove$"},{0xb5, 0x74, "/sa$"},	
	{0x65, 0x42, "/heart$"},   {0x65, 0x42, "/xin$"},
	{0x66, 0x43, "/break$"},   {0x66, 0x43, "/xs$"},      {0x66, 0x43, "/broken_heart$"},
	{0x67, 0x35, "/cake$"},    {0x67, 0x35, "/dg$"},
	{0x9c, 0x36, "/li$"},      {0x9c, 0x36, "/shd$"},     {0x9c, 0x36, "/lightning$"},
	{0x9d, 0x37, "/bome$"},    {0x9d, 0x37, "/zhd$"},     {0x9d, 0x37, "/bomb$"},
	{0x9e, 0x38, "/kn$"},      {0x9e, 0x38, "/dao$"},     {0x9e, 0x38, "/knife$"},
	{0x5e, 0x39, "/footb$"},   {0x5e, 0x39, "/zq$"},      {0x5e, 0x39, "/soccer$"},
	{0xb6, 0x75, "/ladybug$"}, {0xb6, 0x75, "/pch$"},
	{0x89, 0x3B, "/shit$"},    {0x89, 0x3B, "/bb$"},
	{0x6e, 0x4B, "/moon$"},    {0x6e, 0x4B, "/yl$"},
	{0x6b, 0x4A, "/sun$"},     {0x6b, 0x4A, "/ty$"},
	{0x68, 0x45, "/gift$"},    {0x68, 0x45, "/lw$"},
	{0x7f, 0x31, "/hug$"},     {0x7f, 0x31, "/yb$"},
	{0x6f, 0x4C, "/strong$"},  {0x6f, 0x4C, "/qiang$"},   {0x6f, 0x4C, "/thumbs_up$"},
	{0x70, 0x4D, "/weak$"},    {0x70, 0x4D, "/ruo$"},     {0x70, 0x4D, "/thumbs_down$"},
	{0x88, 0x4E, "/share$"},   {0x88, 0x4E, "/ws$"},      {0x88, 0x4E, "/handshake$"},
	{0xa0, 0x4F, "/victory$"}, {0xa0, 0x4F, "/shl$"},     {0xa0, 0x4F, "/v$"},
	{0xb7, 0x76, "/@)$"},      {0xb7, 0x76, "/bq$"},
	{0xb8, 0x77, "/jj$"},      {0xb8, 0x77, "/gy$"},
	{0xb9, 0x78, "/@@$"},      {0xb9, 0x78, "/qt$"},
	{0xba, 0x79, "/bad$"},     {0xba, 0x79, "/cj$"},
	{0xbb, 0x7A, "/loveu$"},   {0xbb, 0x7A, "/aini$"},
	{0xbc, 0x7B, "/no$"},      {0xbc, 0x7B, "/bu$"},
	{0xbd, 0x7C, "/ok$"},      {0xbd, 0x7C, "/hd$"},
	{0x5c, 0x2A, "/love$"},    {0x5c, 0x2A, "/aiq$"},	
	{0x56, 0x55, "/<L>$"},     {0x56, 0x55, "/fw$"},      {0x56, 0x55, "/blow_kiss$"},
	{0x58, 0x2B, "/jump$"},    {0x58, 0x2B, "/tiao$"},
	{0x5a, 0x29, "/shake$"},   {0x5a, 0x29, "/fad$"},	
	{0x5b, 0x56, "/<O>$"},     {0x5b, 0x56, "/oh$"},      {0x5b, 0x56, "/angry$"},
	{0xbe, 0x7D, "/circle$"},  {0xbe, 0x7D, "/zhq$"},
	{0xbf, 0x7E, "/kotow$"},   {0xbf, 0x7E, "/kt$"},
	{0xc0, 0x7F, "/turn$"},    {0xc0, 0x7F, "/ht$"},
	{0xc1, 0x80, "/skip$"},    {0xc1, 0x80, "/tsh$"},
	{0xc2, 0x81, "/oY$"},      {0xc2, 0x81, "/hsh$"},
	{0xc3, 0x82, "/#-O$"},     {0xc3, 0x82, "/jd$"},
	{0xc4, 0x83, "/hiphop$"},  {0xc4, 0x83, "/jw$"},
	{0xc5, 0x84, "/kiss$"},    {0xc5, 0x84, "/xw$"},
	{0xc6, 0x85, "/<&$"},      {0xc6, 0x85, "/zuotj$"},
	{0xc7, 0x86, "/&>$"},      {0xc7, 0x86, "/youtj$"},
	/* emotes below deprecated in QQ, but available on pidgin */
	{0x75, 0xA0 , "/:#$"},      {0x75, 0xA0 , "/feid$"},    {0x75, 0xA0 , "/SARS$"},
	{0x59, 0xA1 , "/go$"},      {0x59, 0xA1 , "/shan$"},
	{0x57, 0xA2 , "/find$"},    {0x57, 0xA2 , "/zhao$"},    {0x57, 0xA3 , "/search$"},
	{0x55, 0xA3 , "/&$"},       {0x55, 0xA3 , "/mm$"},      {0x55, 0xA3 , "/beautiful_eyebrows$"},
	{0x7d, 0xA4 , "/cat$"},     {0x7d, 0xA4 , "/maom$"},
	{0x7e, 0xA5 , "/dog$"},     {0x7e, 0xA5 , "/xg$"},
	{0x9a, 0xA6 , "/$$"},       {0x9a, 0xA6 , "/qianc$"},   {0x9a, 0xA6 , "/money$"},
	{0x9b, 0xA7 , "/(!)$"},     {0x9b, 0xA7 , "/dp$"},      {0x9b, 0xA7 , "/lightbulb$"},
	{0x60, 0xA8 , "/cup$"},     {0x60, 0xA8 , "/bei$"},
	{0x9f, 0xA9 , "/music$"},   {0x9f, 0xA9 , "/yy$"},
	{0x82, 0xAA , "/pill$"},    {0x82, 0xAA , "/yw$"},
	{0x64, 0xAB , "/kiss$"},    {0x64, 0xAB , "/wen$"},
	{0x83, 0xAC , "/meeting$"}, {0x83, 0xAC , "/hy$"},
	{0x84, 0xAD , "/phone$"},   {0x84, 0xAD , "/dh$"},
	{0x85, 0xAE , "/time$"},    {0x85, 0xAE , "/sj$"},
	{0x86, 0xAF , "/email$"},   {0x86, 0xAF , "/yj$"},
	{0x87, 0xB0 , "/tv$"},      {0x87, 0xB0 , "/ds$"},
	{0x50, 0xB1 , "/<D>$"},     {0x50, 0xB1 , "/dd$"},
	{0x51, 0xB2 , "/<J>$"},     {0x51, 0xB2 , "/mn$"},     {0x51, 0xB2 , "/beauty$"},
	{0x52, 0xB3 , "/<H>$"},     {0x52, 0xB3 , "/hl$"},
	{0x53, 0xB4 , "/<M>$"},     {0x53, 0xB4 , "/mamao$"},
	{0x54, 0xB5 , "/<QQ>$"},    {0x54, 0xB5 , "/qz$"},     {0x54, 0xB5 , "/qq$"},
	{0x5d, 0xB6 , "/<B>$"},     {0x5d, 0xB6 , "/bj$"},     {0x5d, 0xB6 , "/baijiu$"},
	{0x5f, 0xB7 , "/<U>$"},     {0x5f, 0xB7 , "/qsh$"},    {0x5f, 0xB7 , "/soda$"},
	{0x69, 0xB8 , "/<!!>$"},    {0x69, 0xB8 , "/xy$"},     {0x69, 0xB8 , "/rain$"},
	{0x6a, 0xB9 , "/<~>$"},     {0x6a, 0xB9 , "/duoy$"},   {0x6a, 0xB9 , "/cloudy$"},
	{0x6c, 0xBA , "/<Z>$"},     {0x6c, 0xBA , "/xr$"},     {0x6c, 0xBA , "/snowman$"},
	{0x6d, 0xBB , "/<*>$"},     {0x6d, 0xBB , "/xixing$"}, {0x6d, 0xBB , "/star$"},	
	{0x71, 0xBC , "/<00>$"},    {0x71, 0xBC , "/nv$"},     {0x71, 0xBC , "/woman$"},
	{0x72, 0xBD , "/<11>$"},    {0x72, 0xBD , "/nan$"},    {0x72, 0xBD , "/man$"},
	/* end */
	{0, 0, NULL}
};
gint emoticons_num = sizeof(emoticons) / sizeof(qq_emoticon) - 1;

/* Map for qq smiley convert to purple */
static qq_emoticon emoticons_sym[] = {
	{0x41, 0x00, "/jy$"},
	{0x42, 0x01, "/pz$"},
	{0x43, 0x02, "/se$"},
	{0x44, 0x03, "/fd$"},
	{0x45, 0x04, "/dy$"},
	{0x46, 0x05, "/ll$"},
	{0x47, 0x06, "/hx$"},
	{0x48, 0x07, "/bz$"},
	{0x49, 0x08, "/shui$"},
	{0x4a, 0x09, "/dk$"},
	{0x4b, 0x0A, "/gg$"},
	{0x4c, 0x0B, "/fn$"},
	{0x4d, 0x0C, "/tp$"},
	{0x4e, 0x0D, "/cy$"},
	{0x4f, 0x0E, "/wx$"},
	{0x50, 0xB1 , "/dd$"},
	{0x51, 0xB2 , "/mn$"}, 
	{0x52, 0xB3 , "/hl$"},
	{0x53, 0xB4 , "/mamao$"},
	{0x54, 0xB5 , "/qq$"},
	{0x55, 0xA3 , "/mm$"},
	{0x56, 0x55, "/fw$"},
	{0x57, 0xA3 , "/zhao$"},
	{0x58, 0x2B, "/tiao$"},
	{0x59, 0xA1 , "/shan$"},
	{0x5a, 0x29, "/fad$"},	
	{0x5b, 0x56, "/oh$"},
	{0x5c, 0x2A, "/aiq$"},	
	{0x5e, 0x39, "/zq$"},
	{0x5d, 0xB6 , "/bj$"}, 
	{0x5f, 0xB7 , "/qsh$"},
	{0x60, 0xA8 , "/bei$"},
	{0x61, 0x59, "/xig$"},
	{0x62, 0x3F, "/mg$"},
	{0x63, 0x40, "/dx$"},
	{0x64, 0xAB , "/wen$"},
	{0x65, 0x42, "/xin$"},
	{0x66, 0x43, "/xs$"},
	{0x67, 0x35, "/dg$"},
	{0x68, 0x45, "/lw$"},
	{0x69, 0xB8 , "/xy$"}, 
	{0x6a, 0xB9 , "/duoy$"}, 
	{0x6b, 0x4A, "/ty$"},
	{0x6c, 0xBA , "/xr$"}, 
	{0x6d, 0xBB , "/xixing$"}, 	
	{0x6e, 0x4B, "/yl$"},
	{0x6f, 0x4C, "/qiang$"}, 
	{0x70, 0x4D, "/ruo$"}, 
	{0x71, 0xBC , "/nv$"}, 
	{0x72, 0xBD , "/nan$"},
	{0x73, 0x0F, "/ng$"}, 
	{0x74, 0x10, "/kuk$"}, 
	{0x75, 0xA0 , "/feid$"}, 
	{0x76, 0x12, "/zk$"},
	{0x77, 0x13, "/tu$"},
	{0x78, 0x1A, "/jk$"},
	{0x79, 0x1B, "/lh$"},
	{0x7a, 0x1C, "/hanx$"},
	{0x7b, 0x1D, "/db$"},
	{0x7c, 0x2E, "/zt$"},	
	{0x7d, 0xA4 , "/maom$"},
	{0x7e, 0xA5 , "/xg$"},
	{0x7f, 0x31, "/yb$"},
	{0x80, 0x3C, "/kf$"},
	{0x81, 0x3D, "/fan$"},
	{0x82, 0xAA , "/yw$"},
	{0x83, 0xAC , "/hy$"},
	{0x84, 0xAD , "/dh$"},
	{0x85, 0xAE , "/sj$"},
	{0x86, 0xAF , "/yj$"},
	{0x87, 0xB0 , "/ds$"},
	{0x88, 0x4E, "/ws$"},
	{0x89, 0x3B, "/bb$"},
	{0x8a, 0x13, "/tx$"}, 
	{0x8b, 0x14, "/ka$"},
	{0x8c, 0x16, "/baiy$"}, 
	{0x8d, 0x17, "/am$"},
	{0x8e, 0x18, "/jie$"}, 
	{0x8f, 0x19, "/kun$"}, 
	{0x90, 0x1E, "/fendou$"},
	{0x91, 0x1F, "/zhm$"}, 
	{0x92, 0x20, "/yiw$"}, 
	{0x93, 0x21, "/xu$"},
	{0x94, 0x22, "/yun$"}, 
	{0x95, 0x23, "/zhem$"},
	{0x96, 0x24, "/shuai$"}, 
	{0x97, 0x25, "/kl$"},
	{0x98, 0x26, "/qiao$"},
	{0x99, 0x27, "/zj$"},
	{0x9a, 0xA6 , "/qianc$"},
	{0x9b, 0xA7 , "/dp$"},
	{0x9c, 0x36, "/shd$"},
	{0x9d, 0x37, "/zhd$"}, 
	{0x9e, 0x38, "/dao$"}, 
	{0x9f, 0xA9 , "/yy$"},
	{0xa0, 0x4F, "/shl$"}, 
	{0xa1, 0x60, "/lengh$"},
	{0xa2, 0x61, "/ch$"},
	{0xa3, 0x62, "/kb$"},
	{0xa4, 0x63, "/gz$"},
	{0xa5, 0x64, "/qd$"},
	{0xa6, 0x65, "/huaix$"},
	{0xa7, 0x66, "/zhh$"},
	{0xa8, 0x67, "/yhh$"},
	{0xa9, 0x68, "/hq$"},
	{0xaa, 0x69, "/bs$"},
	{0xab, 0x6A, "/wq$"},
	{0xac, 0x6B, "/kk$"},
	{0xad, 0x6C, "/yx$"},
	{0xae, 0x6D, "/qq$"},
	{0xaf, 0x6E, "/xia$"},
	{0xb0, 0x6F, "/kel$"},
	{0xb1, 0x70, "/cd$"},
	{0xb2, 0x71, "/pj$"},
	{0xb3, 0x72, "/lq$"},
	{0xb4, 0x73, "/pp$"},
	{0xb5, 0x74, "/sa$"},	
	{0xb6, 0x75, "/pch$"},
	{0xb7, 0x76, "/bq$"},
	{0xb8, 0x77, "/gy$"},
	{0xb9, 0x78, "/qt$"},
	{0xba, 0x79, "/cj$"},
	{0xbb, 0x7A, "/aini$"},
	{0xbc, 0x7B, "/bu$"},
	{0xbd, 0x7C, "/hd$"},
	{0xbe, 0x7D, "/zhq$"},
	{0xbf, 0x7E, "/kt$"},
	{0xc0, 0x7F, "/ht$"},
	{0xc1, 0x80, "/tsh$"},
	{0xc2, 0x81, "/hsh$"},
	{0xc3, 0x82, "/jd$"},
	{0xc4, 0x83, "/jw$"},
	{0xc5, 0x84, "/xw$"},
	{0xc6, 0x85, "/zuotj$"},
	{0xc7, 0x86, "/youtj$"},
	{0, 0, NULL}
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
	qsort(emoticons, emoticons_num, sizeof(qq_emoticon), emoticon_cmp);
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
	ret = (qq_emoticon *)bsearch(&key, emoticons, emoticons_num,
			sizeof(qq_emoticon), emoticon_cmp);
	return ret;
}

gchar *emoticon_get(guint8 symbol)
{
	g_return_val_if_fail(symbol >= emoticons_sym[0].symbol, NULL);
	g_return_val_if_fail(symbol <= emoticons_sym[emoticons_sym_num - 1].symbol, NULL);

	return emoticons_sym[symbol - emoticons_sym[0].symbol].name;
}

/* convert qq emote icon to purple sytle
   Notice: text is in qq charset, GB18030 or utf8 */
gchar *qq_emoticon_to_purple(gchar *text)
{
	gchar *ret;
	GString *converted;
	gchar **segments;
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
	if (segments[0] != NULL) {
		g_string_append(converted, segments[0]);
	} else {
		purple_debug_info("QQ", "segments[0] is NULL\n");
	}
	while ((*(++segments)) != NULL) {
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
	const gchar Tahoma[] = {
		0x54, 0x61, 0x68, 0x6F, 0x6D, 0x61, 0x00
	};
	fmt = g_new0(qq_im_format, 1);
	memset(fmt, 0, sizeof(qq_im_format));
	fmt->font_len = strlen(Tahoma);
	fmt->font = g_strdup(Tahoma);
	fmt->font_size = 11;
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
			fmt->font_size = atoi(tmp) * 3 + 1;
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
		g_string_append_printf(tmp, "<font size=\"%d\">", ((fmt->font_size-1)&3) ? (fmt->font_size+1)/3 : (fmt->font_size-1)/3);	//fix the difference in size
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

	
	ret = text->str;
	g_string_free(tmp, TRUE);
	g_string_free(text, FALSE);
	return ret;
}

/* data includes text msg and font attr*/
gint qq_get_im_tail(qq_im_format *fmt, guint8 *data, gint data_len)
{
	gint bytes, text_len;
	guint8 tail_len;
	guint8 font_len;
	guint8 font_attr;

	g_return_val_if_fail(fmt != NULL && data != NULL, 0);
	g_return_val_if_fail(data_len > 1, 0);
	tail_len = data[data_len - 1];
	g_return_val_if_fail(tail_len > 2, 0);
	text_len = data_len - tail_len;
	g_return_val_if_fail(text_len >= 0, 0);

	bytes = text_len;
	/* qq_show_packet("IM tail", data + bytes, tail_len); */
	bytes += 1;		/* skip 0x00 */
	bytes += qq_get8(&font_attr, data + bytes);
	fmt->font_size = font_attr & 0x0F;
	fmt->attr = font_attr >> 5;
	bytes += qq_getdata(fmt->rgb, sizeof(fmt->rgb), data + bytes);	/* red,green,blue */
 	bytes += 1;	/* skip 0x00 */
	bytes += qq_get16(&fmt->charset, data + bytes);

	font_len = data_len - bytes - 1;
	g_return_val_if_fail(font_len > 0, bytes + 1);

	fmt->font_len = font_len;
	if (fmt->font != NULL)	g_free(fmt->font);
	fmt->font = g_convert((gchar *)data + bytes, font_len, UTF8, QQ_CHARSET_DEFAULT, NULL, NULL, NULL);
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

	qq_buddy_find_or_new(gc, qd->uid, 0xFF);

	from = uid_to_purple_name(qd->uid);
	serv_got_im(gc, from, msg, PURPLE_MESSAGE_SYSTEM, now);
	g_free(from);
}

static void process_im_vibrate(PurpleConnection *gc, guint8 *data, gint len, qq_im_header *im_header)
{
	gchar *who;
	PurpleBuddy *buddy;
	qq_buddy_data *bd;
	guint bytes;
	qq_im_format *fmt = NULL;
	GString *msg;
	gchar *msg_utf8;

	struct {
		guint16 msg_seq;
		guint32 send_time;
		guint16 sender_icon;
		guint8 unknown1[3];
		guint8 fragment_count;
		guint8 fragment_index;
		guint32 uid_from;
	} im_text;

	g_return_if_fail(data != NULL && len > 0);

	memset(&im_text, 0, sizeof(im_text));

	bytes = 0;
	bytes += qq_get16(&(im_text.msg_seq), data + bytes);
	bytes += qq_get32(&(im_text.send_time), data + bytes);
	bytes += qq_get16(&(im_text.sender_icon), data + bytes);
	bytes += qq_getdata(im_text.unknown1, sizeof(im_text.unknown1), data + bytes);
	bytes += qq_get8(&(im_text.fragment_count), data + bytes);
	bytes += qq_get8(&(im_text.fragment_index), data + bytes);
	bytes += qq_get32(&im_text.uid_from, data + bytes);

	if (im_text.uid_from != im_header->uid_from || im_text.uid_from == 256)
	{
		return;
	}
	
	purple_debug_info("QQ", "Vibrate from uid: %d\n", im_text.uid_from );

	who = uid_to_purple_name(im_text.uid_from);
	buddy = purple_find_buddy(gc->account, who);
	bd = (buddy == NULL) ? NULL : purple_buddy_get_protocol_data(buddy);
	if (bd != NULL) {
		bd->face = im_text.sender_icon;
		qq_update_buddy_icon(gc->account, who, bd->face);
	}

	fmt = qq_im_fmt_new_default();
	msg = g_string_new("a Vibrate!");
	if (fmt != NULL) {
		msg_utf8 = qq_im_fmt_to_purple(fmt, msg);
		qq_im_fmt_free(fmt);
	} 
	serv_got_im(gc, who, msg_utf8, 0, (time_t) im_text.send_time);

	g_free(msg_utf8);
	g_free(who);
	g_string_free (msg, FALSE);

}

/* process received normal text IM */
static void process_im_text(PurpleConnection *gc, guint8 *data, gint len, qq_im_header *im_header, guint16 msg_type)
{
	guint16 purple_msg_flag;
	gchar *who;
	gchar *msg_smiley, *msg_fmt, *msg_utf8, *msg_escaped;
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
	gchar emoticon;
	gchar *purple_smiley;

	struct {
		guint16 msg_seq;
		time_t send_time;
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
	bytes += qq_gettime(&im_text.send_time, data + bytes);
	bytes += qq_get16(&(im_text.sender_icon), data + bytes);
	bytes += qq_getdata(im_text.unknown1, sizeof(im_text.unknown1), data + bytes); /* 0x(00 00 00)*/
	bytes += qq_get8(&(im_text.has_font_attr), data + bytes);
	bytes += qq_get8(&(im_text.fragment_count), data + bytes);
	bytes += qq_get8(&(im_text.fragment_index), data + bytes);
	bytes += qq_get16(&(im_text.msg_id), data + bytes);
	bytes += qq_get8(&(im_text.auto_reply), data + bytes);
	purple_debug_info("QQ", "IM Seq %u, id %04X, fragment %d-%d, type %d, %s\n",
			im_text.msg_seq, im_text.msg_id,
			im_text.fragment_index, im_text.fragment_count,
			im_text.auto_reply,
			im_text.has_font_attr ? "font attr exists" : "");

	who = uid_to_purple_name(im_header->uid_from);
	buddy = purple_find_buddy(gc->account, who);
	bd = (buddy == NULL) ? NULL : purple_buddy_get_protocol_data(buddy);
	if (bd != NULL) {
		bd->client_tag = im_header->version_from;
		bd->face = im_text.sender_icon;
		qq_update_buddy_icon(gc->account, who, bd->face);
	}

	purple_msg_flag = (im_text.auto_reply == QQ_IM_AUTO_REPLY)
		? PURPLE_MESSAGE_AUTO_RESP : 0;

	switch (msg_type)
	{
	case QQ_MSG_BUDDY_A7:
	case QQ_MSG_BUDDY_A6:
	case QQ_MSG_BUDDY_78:
		{
			bytes += 8;		//4d 53 47 00 00 00 00 00		MSG.....
			bytes += qq_gettime(&im_text.send_time, data + bytes);
			bytes += 4;		//random guint32;

			if (im_text.has_font_attr)	{
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
			while (bytes < len) {
				bytes += qq_get8(&type, data+bytes);
				bytes += qq_get_vstr(&msg_data, NULL, sizeof(guint16), data+bytes);
				//bytes += msg_dataseg_len = qq_get_vstr(&msg_data, NULL, sizeof(guint16), data+bytes);
				//msg_dataseg_len -= sizeof(guint16);

				switch (type) {
				case 0x01:	//text
					qq_get_vstr(&text, NULL, sizeof(guint16), msg_data+1);		//+1 bypass msg_dataseg_flag 0x01
					g_string_append(im_text.msg, text);
					g_free(msg_data);
					g_free(text);
					break;
				case 0x02:	//emoticon
					emoticon = *(msg_data+8);
					/* 01 00 01(sizeof INDEX) INDEX(new) FF 00 02(sizeof SYM) 14 SYM(old) */
					g_free(msg_data);
					purple_smiley = emoticon_get(emoticon);
					if (purple_smiley == NULL) {
						purple_debug_info("QQ", "Not found smiley of 0x%02X\n", emoticon);
						g_string_append(im_text.msg, "/v$");
					} else {
						purple_debug_info("QQ", "Found 0x%02X smiley is %s\n", emoticon, purple_smiley);
						g_string_append(im_text.msg, purple_smiley);
					}
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

			msg_escaped = g_markup_escape_text(im_text.msg->str, im_text.msg->len);
			if (fmt != NULL) {
				msg_utf8 = qq_im_fmt_to_purple(fmt, g_string_new(msg_escaped));
				qq_im_fmt_free(fmt);
				g_free(msg_escaped);
			} else {
				msg_utf8 =  msg_escaped;
			}
			break;
		}
	case QQ_MSG_BUDDY_84:
	case QQ_MSG_BUDDY_85:
	case QQ_MSG_TO_UNKNOWN:
	case QQ_MSG_BUDDY_09:	
		{
			if (1) {
				fmt = qq_im_fmt_new_default();
				tail_len = qq_get_im_tail(fmt, data + bytes, len - bytes);
				im_text.msg = g_string_new_len((gchar *)(data + bytes), len- bytes - tail_len-1);	//remove the tail 0x20
			} else	{
				im_text.msg = g_string_new_len((gchar *)(data + bytes), len - bytes-1);		//remove the tail 0x20
			}

			msg_smiley = qq_emoticon_to_purple(im_text.msg->str);
			msg_utf8 = qq_to_utf8(msg_smiley, QQ_CHARSET_DEFAULT);
			msg_escaped = g_markup_escape_text(msg_utf8, -1);

			if (fmt != NULL) {
				msg_fmt = qq_im_fmt_to_purple(fmt, g_string_new(msg_escaped));
				g_free(msg_escaped);
				g_free(msg_utf8);
				msg_utf8 =  msg_fmt;
				qq_im_fmt_free(fmt);
			} else {
				g_free(msg_utf8);
				msg_utf8 =  msg_escaped;
			}
			g_free(msg_smiley);

			break;
		}
	default:
		break;
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
void qq_process_typing( PurpleConnection *gc, guint8 *data, gint len, guint32 uid_from )
{
		gint bytes = 0;
		guint32 uid;
		gchar *who;

		g_return_if_fail (data != NULL && len > 8);

		bytes += 4;	//00 00 00 00
		bytes += qq_get32(&uid, data+bytes);

		if (uid_from==uid)
		{
			purple_debug_info("QQ", "QQ: %d is typing to you\n", uid_from);
			who = uid_to_purple_name(uid_from);
			serv_got_typing(gc, who, 7, PURPLE_TYPING);
		}

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
		case QQ_NORMAL_IM_VIBRATE:
			process_im_vibrate(gc, data+bytes, len-bytes, &im_header);
			break;
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
			qq_show_packet ("Unknown", data + bytes, len - bytes);
			return;
	}
}

/* send an IM to uid_to */
static void request_send_im(PurpleConnection *gc, guint32 uid_to, guint8 type, qq_im_format *fmt, const GString *msg, guint16 msg_id, time_t send_time, guint8 frag_count, guint8 frag_index)
{
	qq_data *qd;
	guint8 raw_data[1024];
	gint bytes;
	static guint8 fill[] = {
		0x00, 0x00, 0x00, 0x0D, 
		0x00, 0x01, 0x00, 0x04,
		0x00, 0x00, 0x00, 0x00, 
		0x00, 0x03, 0x00, 0x01, 0x01
	};
	qd = (qq_data *) gc->proto_data;

	/* purple_debug_info("QQ", "Send IM %d-%d\n", frag_count, frag_index); */
	bytes = 0;
	/* receiver uid */
	bytes += qq_put32(raw_data + bytes, qd->uid);
	/* sender uid */
	bytes += qq_put32(raw_data + bytes, uid_to);
	/* Unknown */
	bytes += qq_putdata(raw_data + bytes, fill, sizeof(fill));
	/* sender client version */
	bytes += qq_put16(raw_data + bytes, qd->client_tag);
	/* receiver uid */
	bytes += qq_put32(raw_data + bytes, qd->uid);
	/* sender uid */
	bytes += qq_put32(raw_data + bytes, uid_to);
	/* md5 of (uid+session_key) */
	bytes += qq_putdata(raw_data + bytes, qd->session_md5, 16);
	/* message type */
	bytes += qq_put16(raw_data + bytes, QQ_NORMAL_IM_TEXT);
	/* sequence number */
	bytes += qq_put16(raw_data + bytes, qd->send_seq);
	/* send time */
	bytes += qq_puttime(raw_data + bytes, &send_time);
	/* sender icon */
	bytes += qq_put16(raw_data + bytes, qd->my_icon);
	/* 00 00 00 Unknown */
	bytes += qq_put32(raw_data + bytes ,0);
	bytes --;
	/* have_font_attribute 0x01 */
	bytes += qq_put8(raw_data + bytes, 0x01);
	/* slice count */
	bytes += qq_put8(raw_data + bytes, frag_count);
	/* slice index */
	bytes += qq_put8(raw_data + bytes, frag_index);
	/* msg_id */
	bytes += qq_put16(raw_data + bytes, msg_id);
	/* text message type (normal/auto-reply) */
	bytes += qq_put8(raw_data + bytes, type);
	/* "MSG" */
	bytes += qq_put32(raw_data + bytes, 0x4D534700);
	bytes += qq_put32(raw_data + bytes, 0x00000000);
	bytes += qq_puttime(raw_data + bytes, &send_time);
	/* Likely a random int */
	srand((unsigned)send_time);
	bytes += qq_put32(raw_data + bytes, (rand() & 0x7fff) | ((rand() & 0x7fff) << 15));
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
	/* msg does not end with 0x00 */
	bytes += qq_putdata(raw_data + bytes, (guint8 *)msg->str, msg->len);

	/* qq_show_packet("QQ_CMD_SEND_IM", raw_data, bytes); */
	qq_send_cmd(gc, QQ_CMD_SEND_IM, raw_data, bytes);
}

GSList *qq_im_get_segments(gchar *msg_stripped, gboolean is_smiley_none)
{
	GSList *string_list = NULL;
	GString *string_seg;
	gchar *start0=NULL, *p0=NULL, *start1=NULL, *p1=NULL, *end1=NULL;
	guint msg_len;
	guint16 string_len=0;
	guint16 string_ad_len;
	qq_emoticon *emoticon;
	static gchar em_prefix[] = {
		0x00, 0x09, 0x01, 0x00, 0x01
	};
	static gchar em_suffix[] = {
		0xFF, 0x00, 0x02, 0x14
	};

	g_return_val_if_fail(msg_stripped != NULL, NULL);

	//start0 = msg_stripped;
	msg_len = strlen(msg_stripped);
	string_seg = g_string_new("");

	p0 = msg_stripped;
	start0 = p0;
	while (p0)
	{
		p0 = g_utf8_strchr(p0, -1, '/');
		
		if (!p0)		//if '/' not found, append proper bytes data
		{
			start1 = start0;
			end1 = msg_stripped + msg_len;
		}
		else {
			if (emoticon = emoticon_find(p0))		//find out if it is a emoticon
			{
				/* if emoticon is found, append proper bytes of text before '/'  */
				start1 = start0;
				end1 = p0;
			} else {		//just a '/'
				p0++;
				continue;
			}
		}

		p1 = start1;
		while (p1 < end1)
		{
			if (end1 - p1 + 6 > QQ_MSG_IM_MAX)
			{
				/* get n utf8 chars with certain size */
				start1 = p1;
				while (p1 < start1 + QQ_MSG_IM_MAX - 6)
					p1 = g_utf8_next_char(p1);
				p1 = g_utf8_prev_char (p1);

				string_len = p1 - start1;
			} else {
				start1 = p1;
				p1 = end1;
				string_len = p1 - start1;
			}

			if (string_len >0)
			{
				if (string_seg->len + string_len + 6 > QQ_MSG_IM_MAX)		//6 is wrapper size of text
				{
					/* enough chars to send */
					string_list = g_slist_append(string_list, string_seg);
					string_seg = g_string_new("");
				}
				g_string_append_c(string_seg, 0x01);		//TEXT FLAG
				string_ad_len = string_len + 3;		//len of text plus prepended data
				string_ad_len = htons(string_ad_len);
				g_string_append_len(string_seg, (gchar *)&string_ad_len, sizeof(guint16));
				g_string_append_c(string_seg, 0x01);		//Unknown FLAG
				string_ad_len = htons(string_len);
				g_string_append_len(string_seg, (gchar *)&string_ad_len, sizeof(guint16));
				g_string_append_len(string_seg, start1, string_len);
			}
		}
		

		/* if '/' is found, first check if it is emoticon, if so, append it */
		if ( !is_smiley_none && p0 )
		{
			if (string_seg->len  + 12 > QQ_MSG_IM_MAX)		//12 is size of a emoticon
			{
				/* enough chars to send */
				string_list = g_slist_append(string_list, string_seg);
				string_seg = g_string_new("");
			}

			if (emoticon != NULL) {
				purple_debug_info("QQ", "found emoticon %s as 0x%02X\n",
					emoticon->name, emoticon->symbol);
				/* Until Now, We haven't the new emoticon key database */
				/* So Just Send a Settled V Gesture */
				//g_string_append_len(string_seg, em_v, sizeof(em_v));
				g_string_append_c(string_seg, 0x02);		//EMOTICON FLAG
				g_string_append_len(string_seg, em_prefix, sizeof(em_prefix));
				g_string_append_c(string_seg, emoticon->index);
				g_string_append_len(string_seg, em_suffix, sizeof(em_suffix));
				g_string_append_c(string_seg, emoticon->symbol);
				p0 += strlen(emoticon->name);
				start0 = p0;
			} else {
				/* DO NOT uncomment it, p0 is not NULL ended */
				//purple_debug_info("QQ", "Not found emoticon %.20s\n", p0);
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

unsigned int qq_send_typing( PurpleConnection *gc, const char *who, PurpleTypingState state )
{
	qq_data *qd;
	guint8 raw_data[16];
	gint bytes;
	guint32 uid_to=0;

	g_return_val_if_fail(NULL != gc && NULL != gc->proto_data, -1);
	g_return_val_if_fail(who != NULL, -1);

	qd = (qq_data *) gc->proto_data;

	if (state != PURPLE_TYPING)
		return 0;

	uid_to = purple_name_to_uid(who);

	if (uid_to==qd->uid)
	{
		/* We'll just fake it, since we're sending to ourself. */
		serv_got_typing(gc, who, 7, PURPLE_TYPING);

		return 7;
	}

	if (!uid_to)
	{
		return 0;
	} else {
		bytes = 0;
		/* receiver uid */
		bytes += qq_put32(raw_data + bytes, qd->uid);
		/* sender uid */
		bytes += qq_put32(raw_data + bytes, uid_to);
		bytes += qq_put8(raw_data + bytes, 0);

		/* qq_show_packet("QQ_CMD_SEND_TYPING", raw_data, bytes); */
		qq_send_cmd(gc, QQ_CMD_SEND_TYPING, raw_data, bytes);
	}

	return 7;
}

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
	time_t send_time;

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
	send_time = time(NULL);
	for (it = segments; it; it = it->next) {
		request_send_im(gc, uid_to, type, fmt, (GString *)it->data, msg_id, send_time, frag_count, frag_index);
		g_string_free(it->data, TRUE);
		frag_index++;
	}
	g_slist_free(segments);
	qq_im_fmt_free(fmt);
	return 1;
}
