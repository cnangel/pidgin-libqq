#include <errno.h>
#include <glib.h>
#include <glib/gprintf.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qq_crypt.h"

#define KEY_SIZE 1024
#define MSG_SIZE 1024

static void clean_str(gchar* str) {
	gchar* at_char;
	gsize len = strlen(str);
	while ((at_char = strrchr(str, ' ')) != NULL) {
		gint32 i;
		for (i = 0; at_char[i + 1] != '\0'; i++) {
			at_char[i] = at_char[i + 1];
		}

		len--;
	}
	str[len] = '\0';
}

static int convert(guint8* out, guint8* raw, gsize len) {
	g_printf("Size: %d Raw: %s\n", len, raw);
	guint32 i = 0;
	gsize newlen = 0;

	if (len % 2 == 1) {
		out[i] = g_ascii_xdigit_value(raw[i]);
		++i; ++newlen;
	}

	for ( ; i < len; i += 2) {
		guint8 first, second;

		first = g_ascii_xdigit_value(raw[i]) << 4;
		second = g_ascii_xdigit_value(raw[i+ 1]);

		out[i / 2 + i % 2] = first | second;
		++newlen;
	}

	return newlen;
}

static int get_input(guint8* out, gsize len, const gchar* const prompt) {
	guint8* raw;
	gsize newlen;

	raw = g_malloc0(len);

	fputs(prompt, stdout);
	if (fgets(raw, len, stdin) == NULL) {
		perror("get_input");
		return -1;
	}

	g_strchomp(raw);
	clean_str(raw);

	if ((newlen = convert(out, raw, strlen(raw))) == -1) {
		g_fprintf(stderr, "Could not convert mesage\n");
		return -1;
	}

	g_free(raw);

	return newlen;
}

static void print_results(guint8* in, gsize len) {
	for (gint32 i = 0; i < len; i++) {
		g_printf("%0hhx", in[i]);
	}

	g_printf("\n");
}

int main(int argc, char** argv) {
	guint8 * key, * msg, * plain;
	gsize key_len, msg_len;

	key = g_malloc0(KEY_SIZE);
	msg = g_malloc0(MSG_SIZE);
	plain = g_malloc0(MSG_SIZE);

	if ((key_len = get_input(key, KEY_SIZE, "Enter QQ key: ")) == -1) {
		return EXIT_FAILURE;
	}

	if ((msg_len = get_input(msg, MSG_SIZE, "Enter QQ msg: ")) == -1) {
		return EXIT_FAILURE;
	}

	qq_decrypt(plain, msg, msg_len, key);
	g_printf("Encrypted msg: ");
	print_results(msg, msg_len);

	g_printf("Key: ");
	print_results(key, key_len);

	g_printf("Decrypted msg: ");
	print_results(plain, msg_len);

	g_free(key);
	g_free(msg);
	g_free(plain);

	return EXIT_SUCCESS;
}
