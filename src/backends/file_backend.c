#include "backend.h"
#include "log.h"
#include <sys/stat.h>

// Internal file backend functions using unified interface

static BackendObject file_backend_init(const rawstor_uuid* obj_id, Backend* backend) {
    BackendObject obj = {BOBJ_VERSION_ZERO, *obj_id, BOBJ_STATE_UNKNOWN};
    (void)backend; // unused
    return obj;
}

// Open a file using the file-based storage backend
static int file_backend_open(
    BackendObject* obj, struct io_uring* ring, void* sqe_data,
    Backend* backend
) {
    (void)ring;   // unused
    (void)sqe_data; // unused
    
    FileSettings* settings = &backend->settings.file;
    char obj_file_path[600];
    rawstor_uuid_string uuid;
    rawstor_uuid_to_string(&obj->id, &uuid);
    sprintf(obj_file_path, "%s/%s", settings->path, uuid);
    
    int fd = open(obj_file_path, O_RDWR, 0644);
    if (fd == -1) {
        if (errno == ENOENT) {
            fd = open(obj_file_path, O_RDWR | O_CREAT, 0644);
            if (fd == -1) {
                perror("open");
                return -1;
            }
            obj->format_version = BOBJ_VERSION_ZERO;
            obj->state = BOBJ_STATE_CLEAN;
            obj->serial = 0;
            obj->data_offset = DATA_OFFSET_V0;
            // TODO: remove hardcode!
            obj->data_size = 1024 * 1024 * 1024;
            
            if (ftruncate(fd, obj->data_offset + obj->data_size) == -1) {
                perror("ftruncate");
                return -1;
            }
            ssize_t bytes_written =
                pwrite(fd, obj, sizeof(OnDiskBackendObject), 0);
            if (bytes_written != sizeof(OnDiskBackendObject)) {
                perror("pwrite");
                return -1;
            }
            LOG_INFO("New file created!\n");
        } else {
            perror("open");
            return -1;
        }
    }

    OnDiskBackendObject on_disk_obj;
    ssize_t bytes_read = pread(fd, &on_disk_obj, sizeof(on_disk_obj), 0);
    if (bytes_read != sizeof(OnDiskBackendObject)) {
        FLOG_INFO(stderr, "File has wrong meta!");
        close(fd);
        return -2;
    }

    obj->format_version = on_disk_obj.format_version;
    obj->id = on_disk_obj.id;
    obj->state = on_disk_obj.state;
    obj->serial = on_disk_obj.serial;
    obj->data_offset = on_disk_obj.data_offset;
    obj->data_size = on_disk_obj.data_size;
    obj->fd = fd;
    return 0;
}

// Read from a file using the file-based storage backend
static void file_backend_readv(
    BackendObject* obj, const struct iovec* iov, int iovcnt, uint64_t offset,
    struct io_uring* ring, void* sqe_data, Backend* backend
) {
    (void)backend; // unused
    struct io_uring_sqe* sqe = get_sqe(ring);
    io_uring_prep_readv(sqe, obj->fd, iov, iovcnt, obj->data_offset + offset);
    io_uring_sqe_set_data(sqe, sqe_data);
}

// Write to a file using the file-based storage backend
static void file_backend_writev(
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

// Sync the contents of the file to disk using the file-based storage backend
static void file_backend_sync(
    BackendObject* obj, struct io_uring* ring, void* sqe_data,
    Backend* backend
) {
    (void)backend; // unused
    struct io_uring_sqe* sqe = get_sqe(ring);
    io_uring_prep_fsync(sqe, obj->fd, IORING_FSYNC_DATASYNC);
    io_uring_sqe_set_data(sqe, sqe_data);
}

// Close a file using the file-based storage backend
static int file_backend_close(
    BackendObject* obj, struct io_uring* ring, void* sqe_data,
    Backend* backend
) {
    (void)ring;   // unused
    (void)sqe_data; // unused
    (void)backend; // unused
    return close(obj->fd);
}

// Allocate space for existing object
static int file_backend_allocate(
    BackendObject* obj, size_t size, struct io_uring* ring, void* sqe_data,
    Backend* backend
) {
    (void)ring;   // unused
    (void)sqe_data; // unused
    (void)backend; // unused
    if (ftruncate(obj->fd, size) == -1) {
        return -1;
    }
    return 0;
}

// Initialize file backend
int init_file_backend(int argc, char** argv, Backend* backend) {
    (void)argc; // argc may be used for additional args in future
    
    backend->type = BACKEND_TYPE_FILE;
    backend->init = file_backend_init;
    backend->open = file_backend_open;
    backend->readv = file_backend_readv;
    backend->writev = file_backend_writev;
    backend->sync = file_backend_sync;
    backend->close = file_backend_close;
    backend->allocate = file_backend_allocate;

    if (argc < 3) {
        fprintf(stderr, "File backend requires path argument\n");
        return 1;
    }

    strlcpy(backend->settings.file.path, argv[2], 256);
    struct stat info;

    if (stat(backend->settings.file.path, &info) != 0) {
        fprintf(stderr, "cannot access %s\n", backend->settings.file.path);
        return 1;
    } else if (!S_ISDIR(info.st_mode)) {
        fprintf(stderr, "%s is not a directory\n", backend->settings.file.path);
        return 1;
    }

    return 0;
}

void cleanup_file_backend(Backend* backend) {
    (void)backend; // Nothing to cleanup for file backend
}
