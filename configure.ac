dnl Process this file with autoconf to produce a configure script.
dnl Created by Anjuta application wizard.

m4_define([prj_name],       [Skeltrack Desktop Control])
m4_define([prj_short_name], [DesktopControl])
m4_define([prj_home],       [http://labs.igalia.com/skeltrack-desktop-control])

AC_INIT([prj_name],
	[prj_version],
	[prj_home])

AC_CONFIG_HEADERS([config.h])

AM_INIT_AUTOMAKE([1.11])

AM_SILENT_RULES([yes])

PRJ_NAME="prj_short_name"
AC_SUBST(PRJ_NAME)

AC_PROG_CC

SKELTRAC_REQUIRED=0.1.2
GFREENECT_REQUIRED=0.1.4
CLUTTER_REQUIRED=1.8.4
GLIB_REQUIRED=2.28.0
XTST_REQUIRED=1.2.0
PKG_CHECK_MODULES(DEPS, gfreenect-0.1 >= GFREENECT_REQUIRED
                        skeltrack-0.1 >= SKELTRACK-0
                        clutter-1.0 >= CLUTTER_REQUIRED
                        glib-2.0 >= $GLIB_REQUIRED
                        gio-2.0 >= $GLIB_REQUIRED
                        gobject-2.0 >= $GLIB_REQUIRED
                        xtst >= XTST_REQUIRED)

AC_OUTPUT([
Makefile
src/Makefile

])
