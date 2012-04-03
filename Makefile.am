## Process this file with automake to produce Makefile.in
## Created by Anjuta

SUBDIRS = src

skeltrackdesktopcontroldocdir = ${prefix}/doc/skeltrack-desktop-control
skeltrackdesktopcontroldoc_DATA = \
				README\
				COPYING\
				AUTHORS\
				ChangeLog\
				INSTALL\
				NEWS

EXTRA_DIST = $(skeltrackdesktopcontroldoc_DATA)

# Remove doc directory on uninstall
uninstall-local:
	-rm -r $(skeltrackdesktopcontroldocdir)
