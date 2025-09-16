bin_PROGRAMS = ost


AM_CFLAGS = -g -O3 -Wall \
            $(LIBXXHASH_CFLAGS) \
            $(LIBURING_CFLAGS)


ost_SOURCES = hash.c \
              ost.c \
              ruring.c \
              uuid.c \
              backends/file.c

ost_LDADD = $(LIBXXHASH_LIBS) \
            $(LIBURING_LIBS)
