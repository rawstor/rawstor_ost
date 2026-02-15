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
              uuid.h \
              memory_pool.c \
              memory_pool.h \
              backends/backend.h \
              backends/file_backend.c \
              backends/zfs_backend.c

ost_LDADD = $(LIBXXHASH_LIBS) \
            $(LIBURING_LIBS)
