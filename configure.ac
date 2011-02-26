# Copyright (C) 2008 Alexander Logvinov <ports@logvinov.com> 
#   
# This file is free software; as a special exception the author gives 
# unlimited permission to copy and/or distribute it, with or without  
# modifications, as long as this notice is preserved. 
#  
# This program is distributed in the hope that it will be useful, but 
# WITHOUT ANY WARRANTY, to the extent permitted by law; without even the 
# implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. 

AC_INIT([libqq-pidgin],[0.43],[])
#AM_CONFIG_HEADER([config.h])
AM_INIT_AUTOMAKE([-Wall])
AC_CONFIG_MACRO_DIR([m4])

AC_PROG_CC
AM_DISABLE_STATIC
AC_LIBTOOL_DLOPEN
AC_PROG_LIBTOOL

AC_DEFINE(DISPLAY_VERSION,["0.43"])

PKG_CHECK_MODULES([PURPLE],[purple >= 2.0.0],,AC_MSG_ERROR([
*** libpurple 2.0+ is required to build.]))
AC_SUBST(PURPLE_CFLAGS)
AC_SUBST(PURPLE_LIBS)
PURPLE_LIBDIR=`pkg-config --variable=libdir purple`
PURPLE_DATADIR=`pkg-config --variable=datadir purple`
PURPLE_PLUGINDIR=`pkg-config --variable=plugindir purple`
AC_SUBST(PURPLE_LIBDIR)
AC_SUBST(PURPLE_DATADIR)
AC_SUBST(PURPLE_PLUGINDIR)
datadir=${PURPLE_DATADIR}

PKG_CHECK_MODULES([GLIB],[glib-2.0])

AM_CONDITIONAL([STATIC_QQ],[false])
AM_CONDITIONAL([BUILD_SEPERATE],[true])
AC_OUTPUT([Makefile])
