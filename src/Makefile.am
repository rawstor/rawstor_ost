bin_PROGRAMS = ost


AM_CFLAGS = -g -O3 -Wall \
            $(LIBXXHASH_CFLAGS) \
            $(LIBURING_CFLAGS)


ost_SOURCES = hash.c \
              hash.h \
              log.h \
              ost.c \
              ost.h \
              ruring.c \
              ruring.h \
              uuid.c \
              backends/file.c \
              backends/file.h

ost_LDADD = $(LIBXXHASH_LIBS) \
            $(LIBURING_LIBS)
