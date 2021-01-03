ACLOCAL_AMFLAGS = -I m4

SRC = src/additive.c src/adsr.c src/properties_simple.c src/voice.c src/generated/generated-genums.c \
	src/propsratecontrolsource.c src/lfo.c src/math.c

BUILT_SOURCES = src/generated/generated-genums.h src/generated/generated-genums.c
CLEANFILES = $(BUILT_SOURCES)

plugin_LTLIBRARIES = libadditive.la

libadditive_la_SOURCES = $(SRC)
libadditive_la_CFLAGS = $(PKGCONFIG_DEPS_CFLAGS) $(OPTIMIZE_CFLAGS) \
	-std=gnu99 -Werror -Wall -Wshadow -Wpointer-arith -Wstrict-prototypes \
	-Wno-error=unused-function -Wno-error=unused-variable \
	-fvisibility=hidden
libadditive_la_LDFLAGS = $(PKGCONFIG_DEPS_LIBS) -module -avoid-version

# The 'la' file is unneeded as the generated lib isn't intended to be linked with others.
install-data-hook:
	$(RM) $(DESTDIR)$(plugindir)/libadditive*.la
#	$(LN_S) -f TBD.html  $(DESTDIR)$(userdocdir)/TBD.html

#userdoc_DATA = TBD.html

# Auto-generate GObject enum definitions from src/genums.h
src/generated/generated-genums.h: src/genums.h src/generated-genums.h.in
	$(AM_V_GEN)$(GLIB_MKENUMS) \
		--template=$(top_srcdir)/src/generated-genums.h.in \
		--output=$@ \
		$(top_srcdir)/src/genums.h
	test -f $@ || { echo "ERROR, file not generated"; exit 1; }

src/generated/generated-genums.c: src/genums.h src/generated-genums.c.in src/generated/generated-genums.h
	$(AM_V_GEN)$(GLIB_MKENUMS) \
		--template=$(top_srcdir)/src/generated-genums.c.in \
		--output=$@ \
		$(top_srcdir)/src/genums.h
	test -f $@ || { echo "ERROR, file not generated"; exit 1; }
