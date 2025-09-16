#ifndef RAWSTOR_FILEBACKEND_H
#define RAWSTOR_FILEBACKEND_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

#include "ruring.h"
#include "uuid.h"

typedef struct
{
    char path[256];
} FileSettings;

// 1MiB for metadata
#define DATA_OFFSET_V0 (1024 * 1024)

typedef enum
{
    BOBJ_VERSION_ZERO,
} BObjectVersion;

typedef enum
{
    BOBJ_STATE_UNKNOWN,
    BOBJ_STATE_CLEAN,
    BOBJ_STATE_DIRTY,
} BObjectState;

typedef struct
{
    int format_version;
    rawstor_uuid id;
    BObjectState state;
    uint64_t serial;
    uint64_t data_offset;
    uint64_t data_size;
    /* Next fields are in-memory only and may be specific for backeds */
    int fd;
} BackendObject;

// Should be small to fit in one disk block
typedef struct
{
    int format_version;
    rawstor_uuid id;
    BObjectState state;
    uint64_t serial;
    uint64_t data_offset;
    uint64_t data_size;
} OnDiskBackendObject;
typedef struct
{
    BackendObject (*init)(const rawstor_uuid *obj_id, FileSettings *settings);
    int (*open)(BackendObject *obj, struct io_uring *ring, void *sqe_data, FileSettings *settings);
    void (*readv)(BackendObject *obj, const struct iovec *iov, int iovcnt, uint64_t offset, struct io_uring *ring, void *sqe_data, FileSettings *settings);
    void (*writev)(BackendObject *obj, const struct iovec *iov, int iovcnt, uint64_t offset, bool sync, struct io_uring *ring, void *sqe_data, FileSettings *settings);
    void (*sync)(BackendObject *obj, struct io_uring *ring, void *sqe_data, FileSettings *settings);
    int (*close)(BackendObject *obj, struct io_uring *ring, void *sqe_data, FileSettings *settings);
    int (*allocate)(BackendObject *obj, size_t size, struct io_uring *ring, void *sqe_data, FileSettings *settings);
    FileSettings *settings;
} Backend;

BackendObject file_init(const rawstor_uuid *obj_id, FileSettings *settings);

// Open a file using the file-based storage backend
int file_open(BackendObject *obj, struct io_uring *ring, void *sqe_data, FileSettings *settings);

// Read from a file using the file-based storage backend
void file_readv(BackendObject *obj, const struct iovec *iov, int iovcnt, uint64_t offset, struct io_uring *ring, void *sqe_data, FileSettings *settings);

// Write to a file using the file-based storage backend
void file_writev(BackendObject *obj, const struct iovec *iov, int iovcnt, uint64_t offset, bool sync, struct io_uring *ring, void *sqe_data, FileSettings *settings);

// Sync the contents of the file to disk using the file-based storage backend
void file_sync(BackendObject *obj, struct io_uring *ring, void *sqe_data, FileSettings *settings);

// Close a file using the file-based storage backend
int file_close(BackendObject *obj, struct io_uring *ring, void *sqe_data, FileSettings *settings);

// Allocate space for existing object
int file_allocate(BackendObject *obj, size_t size, struct io_uring *ring, void *sqe_data, FileSettings *settings);

int init_file_backend(int argc, char **argv);
#endif // RAWSTOR_FILEBACKEND_H

extern Backend file_backend;
