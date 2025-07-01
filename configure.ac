AC_INIT([rawstor_ost], [0.0.0])
AM_INIT_AUTOMAKE([subdir-objects])

AC_CONFIG_HEADERS([config.h])

AC_PROG_CC


PKG_CHECK_MODULES([LIBURING], [liburing])

PKG_CHECK_MODULES([LIBXXHASH], [libxxhash])


AC_CONFIG_FILES(
    [Makefile]
    [src/Makefile]
    [examples/Makefile]
)

AC_OUTPUT
