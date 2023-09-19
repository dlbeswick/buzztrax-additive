AC_PREREQ([2.71])
AC_INIT([additive],[1],[dlbeswick@gmail.com])

# -O2 -g will always be added by AC_PROG_CC (or some other call?) without this line
: ${CFLAGS="-g"}

AC_CONFIG_SRCDIR([src/additive.c])
AM_INIT_AUTOMAKE([-Wall -Werror 1.16.1 foreign subdir-objects])
AM_PROG_AR
AC_CONFIG_MACRO_DIRS([m4])
AC_CONFIG_HEADERS(config.h)
AC_CONFIG_FILES([Makefile])

# This value must match that expected by Buzztrax, so that the lib is installed in the correct location.
GST_MAJORMINOR=1.0
AC_DEFINE_UNQUOTED(GST_MAJORMINOR, "$GST_MAJORMINOR", [gstreamer series])
AC_SUBST(GST_MAJORMINOR)

PKG_PROG_PKG_CONFIG

export PKG_CONFIG_PATH="$prefix/lib/pkgconfig$PATH_SEPARATOR$PKG_CONFIG_PATH"
AC_MSG_NOTICE([
  Note: PKG_CONFIG_PATH has been modified according to the given prefix in order
  to locate the correct Buzztrax libs and headers.
  
  PKG_CONFIG_PATH is now: $PKG_CONFIG_PATH
])

# Required for the `GLIB_MKENUMS' program used in Makefile.am
PKG_CHECK_VAR([GLIB_MKENUMS], [glib-2.0], [glib_mkenums])

PKG_CHECK_MODULES([PKGCONFIG_DEPS], [libbuzztrax-gst >= 0.11.0])

# No need to generate *.a files for machines.
LT_INIT([disable-static])

# Checks for programs.
AC_PROG_CC
AC_PROG_INSTALL
AC_PROG_LN_S

# enable runtime debugging code (this is more like a developer build mode)
AC_MSG_CHECKING(whether to enable runtime debugging code)
AC_ARG_ENABLE(
	debug,
	AS_HELP_STRING([--enable-debug],[enable runtime debugging code (default=no)]),
	,
	[enable_debug="no"])
AC_MSG_RESULT($enable_debug)
if test "$enable_debug" = "yes"; then
	AC_DEFINE(USE_DEBUG, [1], [enable runtime debugging code])
	OPTIMIZE_CFLAGS="-Og"
else
	OPTIMIZE_CFLAGS="-O2 -ffast-math -ftree-loop-vectorize -lm"
fi
AC_SUBST(OPTIMIZE_CFLAGS)

plugindir="$libdir/gstreamer-$GST_MAJORMINOR"
AC_SUBST(plugindir)
presetdir="\$(datadir)/Gear"
AC_SUBST(presetdir)
userdocdir="\$(datadir)/Gear"
AC_SUBST(userdocdir)
demosongdir="\$(datadir)/buzztrax/songs/Gear"
AC_SUBST(demosongdir)

AC_OUTPUT

echo "
Configuration
	Version                    : ${VERSION}
	Source code location       : ${srcdir}
	Prefix                     : ${prefix}
	Compiler                   : ${CC}
	Debug                      : ${enable_debug}
"
