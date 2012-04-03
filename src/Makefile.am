## Process this file with automake to produce Makefile.in

## Created by Anjuta

AM_CPPFLAGS = \
	-DPACKAGE_LOCALE_DIR=\""$(prefix)/$(DATADIRNAME)/locale"\" \
	-DPACKAGE_SRC_DIR=\""$(srcdir)"\" \
	-DPACKAGE_DATA_DIR=\""$(datadir)"\" 

AM_CFLAGS = \
	$(DEPS_CFLAGS) \
	-I$(top_srcdir)/@PRJ_NAME@
	-Wall \
	-g

bin_PROGRAMS = skeltrack-desktop-control

skeltrack_desktop_control_SOURCES = \
	main.c

skeltrack_desktop_control_LDFLAGS = 

skeltrack_desktop_control_LDADD = \
	$(DEPS_LIBS)

