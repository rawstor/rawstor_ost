#ifndef RAWSTOR_OST_H
#define RAWSTOR_OST_H

#include <liburing.h>

#include <arpa/inet.h>

#include <stdint.h>

#include "backends/file.h"
#include "uuid.h"

/* If request is larger, on-demand buffer of MAX_BUF_SIZE will be used */
#define BUFSIZE sizeof(proto_io_frame_t)
/* Use 3 iovecs for:
  - op frame
  - conn_t buf
  - optional read buf
  */
#define IO_REQ_IOVECS 3
#define MAX_BUF_SIZE 1048576 * 2
#define OBJID_LEN 128 / 8
#define MAX_OFFSET sizeof(uint64_t)
#define MAX_IN_FRAME_SIZE 256

#define NUM_CONNS 65536

#define RAWSTOR_MAGIC 0x72737472 // "rstr" as ascii

// static const int COMMAND_BYTES = sizeof(uint8_t);
// static const int OFFSET_BYTES = sizeof(uint16_t);

// protocol section
#define CMD_SET_OBJECT 0
#define CMD_READ 1
#define CMD_WRITE 2
#define CMD_DISCARD 3
#define CMD_ALLOCATE_OBJ_CHUNK 4
typedef uint16_t commands_t;

/* Minimal protocol frame (cmd only) */
typedef struct {
    uint32_t magic;
    commands_t cmd;
    uint16_t cid;
} __attribute__((packed)) proto_min_frame_t;

/* Basic protocol frame */
typedef struct {
    uint32_t magic;
    commands_t cmd;
    uint16_t cid;
    // var is for minimal commands only, will be overridden in other command
    // structs
    uint8_t obj_id[OBJID_LEN];
    uint64_t offset;
    uint64_t val;
} __attribute__((packed)) proto_basic_frame_t;

/* IO protocol frame */
typedef struct {
    uint32_t magic;
    commands_t cmd;
    uint16_t cid;
    uint64_t offset;
    uint32_t len;
    uint64_t hash;
    bool sync;
} __attribute__((packed)) proto_io_frame_t;

/* response frames */
typedef struct {
    uint32_t magic;
    commands_t cmd;
    uint16_t cid;
    // TODO: if we send length in res - it should be the same type
    // (signed-unsigned too)
    int32_t res;
    uint64_t hash;
} __attribute__((packed)) proto_resp_frame_t;

// uring eventloop section
typedef enum {
    // network request
    REQ_KIND_ACCEPT,
    REQ_KIND_READ,
    REQ_KIND_WRITE,
    REQ_KIND_CLOSE,
    // our block request
    IO_KIND_READ,
    IO_KIND_WRITE,
    // IO_KIND_FSYNC,
    // IO_KIND_DISCARD
} req_kind;

typedef struct {
    req_kind event;
    int fd;
} request_t;

typedef struct {
    request_t req;
    // usually we'll have response frame before actual data, or/and additional
    // iov
    struct iovec iovecs[IO_REQ_IOVECS];
    char iobuf[BUFSIZE];
    uint16_t cid;
} io_request_t;

typedef struct {
    request_t req;
    struct sockaddr_in addr;
    socklen_t addrlen;
} accept_request_t;

typedef struct {
    int fd;
    BackendObject obj;
    void* in_buf;
    int32_t in_bytes;
    proto_io_frame_t* op;
    char op_partial_buf[MAX_IN_FRAME_SIZE];
    uint8_t op_partial_offset;
} conn_t;

#endif // RAWSTOR_OST_H
