AC_INIT([rawstor_ost], [0.0.0])
AM_INIT_AUTOMAKE([subdir-objects])

AC_CONFIG_HEADERS([config.h])

AC_PROG_CC


PKG_CHECK_MODULES([LIBURING], [liburing])

PKG_CHECK_MODULES([LIBXXHASH], [libxxhash])

# Check for io_uring_prep_cmd_discard (available since Linux 6.12)
# This requires both the header declaration and library implementation
AC_MSG_CHECKING([for io_uring_prep_cmd_discard])
saved_cflags="$CFLAGS"
CFLAGS="$CFLAGS $LIBURING_CFLAGS"
saved_libs="$LIBS"
LIBS="$LIBS $LIBURING_LIBS"
AC_LINK_IFELSE([AC_LANG_PROGRAM([
#include <liburing.h>
], [
    struct io_uring_sqe sqe;
    io_uring_prep_cmd_discard(&sqe, 0, 0, 0);
])], [
    AC_MSG_RESULT([yes])
    AC_DEFINE([HAVE_IORING_PREP_CMD_DISCARD], [1], [Define if io_uring_prep_cmd_discard is available])
], [
    AC_MSG_RESULT([no])
])
CFLAGS="$saved_cflags"
LIBS="$saved_libs"


AC_CONFIG_FILES(
    [Makefile]
    [src/Makefile]
)

AC_OUTPUT
