# $Id: Makefile.am,v 1.0.0-0 2012/08/16 17:09:55 Cnangel Exp $

AUTOMAKE_OPTIONS = foreign

EXTRA_DIST = protocols
			 $(NULL)

install-data-local: install-localdata-local

install-localdata-local:
	@echo install res files to $(datadir)
	@test -d $(datadir)/pixmaps/pidgin || mkdir -p $(datadir)/pixmaps/pidgin
	@cp -r $(top_srcdir)/pixmaps/protocols $(datadir)/pixmaps/pidgin

uninstall-local: uninstall-localdata-local

uninstall-localdata-local:
	@echo uninstall res files from $(datadir)
	@rm -f $(datadir)/pixmaps/pidgin/protocols/16/qq.png
	@rm -f $(datadir)/pixmaps/pidgin/protocols/22/qq.png
	@rm -f $(datadir)/pixmaps/pidgin/protocols/48/qq.png
	

