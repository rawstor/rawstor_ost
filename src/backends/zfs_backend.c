#include "backend.h"
#include "log.h"

// Define LOG_ERR if not available
#ifndef LOG_ERR
#define LOG_ERR(...) fprintf(stderr, __VA_ARGS__)
#endif

#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <libgen.h>
#include <sys/statvfs.h>

// Internal ZFS backend functions using unified interface

static BackendObject zfs_backend_init(const rawstor_uuid* obj_id, Backend* backend) {
    BackendObject obj = {BOBJ_VERSION_ZERO, *obj_id, BOBJ_STATE_UNKNOWN};
    (void)backend; // unused
    return obj;
}

// Open a ZFS volume using the ZFS storage backend
static int zfs_backend_open(
    BackendObject* obj, struct io_uring* ring, void* sqe_data,
    Backend* backend
) {
    (void)ring;   // unused
    (void)sqe_data; // unused
    
    ZFSSettings* settings = &backend->settings.zfs;
    char dataset_path[512];
    char cmd[1024];
    char* zvol_path;
    rawstor_uuid_string uuid;
    rawstor_uuid_to_string(&obj->id, &uuid);
    
    // Construct dataset path: zpool/dataset/uuid
    snprintf(dataset_path, sizeof(dataset_path), "%s/%s/%s", 
             settings->zpool_name, settings->dataset_path, uuid);
    
    // Check if the dataset exists
    snprintf(cmd, sizeof(cmd), "zfs list %s > /dev/null 2>&1", dataset_path);
    if (system(cmd) != 0) {
        // Dataset doesn't exist, create it
        snprintf(cmd, sizeof(cmd), "zfs create -s -V %zu %s", settings->vol_size, dataset_path);
        if (system(cmd) != 0) {
            LOG_ERR("Failed to create ZFS volume: %s\n", dataset_path);
            return -1;
        }
    }
    
    // Open the ZFS volume - use a special path for accessing the volume
    // Use a local buffer large enough for /dev/zvol/ + dataset_path
    char zvol_path_buf[640];
    snprintf(zvol_path_buf, sizeof(zvol_path_buf), "/dev/zvol/%s", dataset_path);
    
    // Copy to backend_data for storage (truncation is acceptable for very long paths)
    strncpy((char*)obj->backend_data, zvol_path_buf, sizeof(obj->backend_data) - 1);
    ((char*)obj->backend_data)[sizeof(obj->backend_data) - 1] = '\0';
    zvol_path = zvol_path_buf;
    
    // Open the volume for reading/writing
    // There may be a race between `zfs create -s -V` and zvol_path being created
    // Retry once with 100ms sleep if the first attempt fails
    int fd = open(zvol_path, O_RDWR);
    if (fd == -1 && errno == ENOENT) {
        LOG_INFO("ZFS volume %s not ready yet, retrying in 100ms...\n", zvol_path);
        usleep(100000); // 100ms
        fd = open(zvol_path, O_RDWR);
    }
    if (fd == -1) {
        LOG_ERR("Failed to open ZFS volume %s: %s\n", zvol_path, strerror(errno));
        return -1;
    }
    
    // Set up the backend object
    obj->fd = fd;
    obj->format_version = BOBJ_VERSION_ZERO;
    obj->state = BOBJ_STATE_CLEAN;
    obj->serial = 0;
    obj->data_offset = DATA_OFFSET_V0;
    obj->data_size = settings->vol_size;
    
    return 0;
}

// Read from a ZFS volume using the ZFS storage backend
static void zfs_backend_readv(
    BackendObject* obj, const struct iovec* iov, int iovcnt, uint64_t offset,
    struct io_uring* ring, void* sqe_data, Backend* backend
) {
    (void)backend; // unused
    struct io_uring_sqe* sqe = get_sqe(ring);
    io_uring_prep_readv(sqe, obj->fd, iov, iovcnt, obj->data_offset + offset);
    io_uring_sqe_set_data(sqe, sqe_data);
}

// Write to a ZFS volume using the ZFS storage backend
static void zfs_backend_writev(
    BackendObject* obj, const struct iovec* iov, int iovcnt, uint64_t offset,
    bool sync, struct io_uring* ring, void* sqe_data, Backend* backend
) {
    (void)backend; // unused
    struct io_uring_sqe* sqe = get_sqe(ring);
    if (sync) {
        io_uring_prep_writev2(
            sqe, obj->fd, iov, iovcnt, obj->data_offset + offset, RWF_SYNC
        );
    } else {
        io_uring_prep_writev(
            sqe, obj->fd, iov, iovcnt, obj->data_offset + offset
        );
    }
    io_uring_sqe_set_data(sqe, sqe_data);
}

// Sync the contents of the ZFS volume to disk using the ZFS storage backend
static void zfs_backend_sync(
    BackendObject* obj, struct io_uring* ring, void* sqe_data,
    Backend* backend
) {
    (void)backend; // unused
    struct io_uring_sqe* sqe = get_sqe(ring);
    io_uring_prep_fsync(sqe, obj->fd, IORING_FSYNC_DATASYNC);
    io_uring_sqe_set_data(sqe, sqe_data);
}

// Close a ZFS volume using the ZFS storage backend
static int zfs_backend_close(
    BackendObject* obj, struct io_uring* ring, void* sqe_data,
    Backend* backend
) {
    (void)ring;   // unused
    (void)sqe_data; // unused
    (void)backend; // unused
    int ret = close(obj->fd);
    if (ret == 0) {
        obj->fd = -1;
    }
    return ret;
}

// Allocate space for existing ZFS volume
static int zfs_backend_allocate(
    BackendObject* obj, size_t size, struct io_uring* ring, void* sqe_data,
    Backend* backend
) {
    (void)obj;      // unused - ZFS volumes are created with their size
    (void)size;     // unused - ZFS volumes are created with their size
    (void)ring;     // unused
    (void)sqe_data; // unused
    (void)backend;  // unused
    // ZFS volumes are created with their full size, no allocation needed
    // This is a no-op for ZFS backend
    return 0;
}

// Check if ZFS is available on the system
static int check_zfs_available(void) {
    if (system("which zfs > /dev/null 2>&1") != 0) {
        LOG_ERR("ZFS command not found. Is ZFS installed?\n");
        return -1;
    }
    return 0;
}

// Initialize ZFS backend settings
int init_zfs_backend(int argc, char** argv, Backend* backend) {
    // Check if ZFS is available
    if (check_zfs_available() != 0) {
        return 1;
    }
    
    backend->type = BACKEND_TYPE_ZFS;
    backend->init = zfs_backend_init;
    backend->open = zfs_backend_open;
    backend->readv = zfs_backend_readv;
    backend->writev = zfs_backend_writev;
    backend->sync = zfs_backend_sync;
    backend->close = zfs_backend_close;
    backend->allocate = zfs_backend_allocate;
    
    // Default settings - use argv[2] as zpool name if provided
    if (argc >= 3) {
        strlcpy(backend->settings.zfs.zpool_name, argv[2], 128);
    } else {
        strcpy(backend->settings.zfs.zpool_name, "tank");
    }
    strcpy(backend->settings.zfs.dataset_path, "rawstor");
    backend->settings.zfs.vol_size = 1024 * 1024 * 1024; // 1GB default
    
    // Parse ZFS-specific arguments
    for (int i = 3; i < argc; i++) {
        if (strncmp(argv[i], "--zpool=", 8) == 0) {
            strlcpy(backend->settings.zfs.zpool_name, argv[i] + 8, 128);
        } else if (strncmp(argv[i], "--dataset=", 10) == 0) {
            strlcpy(backend->settings.zfs.dataset_path, argv[i] + 10, 256);
        } else if (strncmp(argv[i], "--vol-size=", 11) == 0) {
            backend->settings.zfs.vol_size = strtoull(argv[i] + 11, NULL, 10);
        }
    }
    
    // Validate that the zpool exists
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "zpool list %s > /dev/null 2>&1", 
             backend->settings.zfs.zpool_name);
    if (system(cmd) != 0) {
        LOG_ERR("ZFS pool '%s' not found or not accessible\n", 
                backend->settings.zfs.zpool_name);
        return 1;
    }
    
    // Validate that the dataset exists (or create it)
    char dataset_full_path[384];
    snprintf(dataset_full_path, sizeof(dataset_full_path), "%s/%s",
             backend->settings.zfs.zpool_name, backend->settings.zfs.dataset_path);
    
    snprintf(cmd, sizeof(cmd), "zfs list %s > /dev/null 2>&1", dataset_full_path);
    if (system(cmd) != 0) {
        // Dataset doesn't exist, try to create it
        LOG_INFO("ZFS dataset '%s' not found, attempting to create it...\n", dataset_full_path);
        snprintf(cmd, sizeof(cmd), "zfs create -p %s", dataset_full_path);
        if (system(cmd) != 0) {
            LOG_ERR("Failed to create ZFS dataset '%s'\n", dataset_full_path);
            return 1;
        }
        LOG_INFO("ZFS dataset '%s' created successfully\n", dataset_full_path);
    }
    
    LOG_INFO("ZFS backend initialized with pool: %s, dataset: %s, vol_size: %zu\n",
             backend->settings.zfs.zpool_name, backend->settings.zfs.dataset_path,
             backend->settings.zfs.vol_size);
    
    return 0;
}

void cleanup_zfs_backend(Backend* backend) {
    (void)backend; // Cleanup ZFS backend resources if needed
}
