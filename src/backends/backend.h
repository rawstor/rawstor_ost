#ifndef RAWSTOR_BACKEND_H
#define RAWSTOR_BACKEND_H

#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>

#include "ruring.h"
#include "uuid.h"

// 1MiB for metadata
#define DATA_OFFSET_V0 (1024 * 1024)

typedef enum {
    BOBJ_VERSION_ZERO,
} BObjectVersion;

typedef enum {
    BOBJ_STATE_UNKNOWN,
    BOBJ_STATE_CLEAN,
    BOBJ_STATE_DIRTY,
} BObjectState;

// Common backend object structure
typedef struct {
    int format_version;
    rawstor_uuid id;
    BObjectState state;
    uint64_t serial;
    uint64_t data_offset;
    uint64_t data_size;
    /* Next fields are in-memory only and may be specific for backends */
    int fd;
    // Additional storage for backend-specific data
    char backend_data[512];
} BackendObject;

// Should be small to fit in one disk block
typedef struct {
    int format_version;
    rawstor_uuid id;
    BObjectState state;
    uint64_t serial;
    uint64_t data_offset;
    uint64_t data_size;
} OnDiskBackendObject;

// File backend settings
typedef struct {
    char path[256];
} FileSettings;

// ZFS backend settings
typedef struct {
    char zpool_name[128];
    char dataset_path[256];
    size_t vol_size;
} ZFSSettings;

// Backend types
typedef enum {
    BACKEND_TYPE_FILE,
    BACKEND_TYPE_ZFS,
    BACKEND_TYPE_UNKNOWN
} BackendType;

// Unified backend interface
typedef struct Backend {
    BackendType type;
    BackendObject (*init)(const rawstor_uuid* obj_id, struct Backend* backend);
    int (*open)(
        BackendObject* obj, struct io_uring* ring, void* sqe_data,
        struct Backend* backend
    );
    void (*readv)(
        BackendObject* obj, const struct iovec* iov, int iovcnt,
        uint64_t offset, struct io_uring* ring, void* sqe_data,
        struct Backend* backend
    );
    void (*writev)(
        BackendObject* obj, const struct iovec* iov, int iovcnt,
        uint64_t offset, bool sync, struct io_uring* ring, void* sqe_data,
        struct Backend* backend
    );
    void (*sync)(
        BackendObject* obj, struct io_uring* ring, void* sqe_data,
        struct Backend* backend
    );
    int (*close)(
        BackendObject* obj, struct io_uring* ring, void* sqe_data,
        struct Backend* backend
    );
    int (*allocate)(
        BackendObject* obj, size_t size, struct io_uring* ring, void* sqe_data,
        struct Backend* backend
    );
    // Backend-specific settings
    union {
        FileSettings file;
        ZFSSettings zfs;
    } settings;
} Backend;

// Backend initialization functions
int init_file_backend(int argc, char** argv, Backend* backend);
void cleanup_file_backend(Backend* backend);

int init_zfs_backend(int argc, char** argv, Backend* backend);
void cleanup_zfs_backend(Backend* backend);

// Backend selection and global access
Backend* get_backend(void);
int init_backend(int argc, char** argv);
void cleanup_backend(void);

#endif // RAWSTOR_BACKEND_H
