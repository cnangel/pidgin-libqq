AM_CPPFLAGS= \
	$(DEBUG_CFLAGS) \
	$(GLIB_CFLAGS) \
	-I$(top_srcdir) \
	${PURPLE_CFLAGS}  \
	${st}	\
	-I$(top_srcdir)/libpurple \
	-I$(top_builddir)/libpurple

AM_CFLAGS= -std=gnu99


noinst_PROGRAMS = qq_decrypt
qq_decrypt_SOURCES = decrypt.c
qq_decrypt_LDADD = $(GLIB_LIBS) ../libqq.la $(PURPLE_LIBS)
