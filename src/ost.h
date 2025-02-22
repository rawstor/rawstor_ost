#include <arpa/inet.h>
#include <liburing.h>
#include "stdint.h"

/* If request is larger, on-demand buffer of MAX_BUF_SIZE will be used */
#define BUFSIZE 8192
/* Use 3 iovecs for:
  - op frame
  - conn_t buf
  - optional read buf
  */
#define IO_REQ_IOVECS 3
#define MAX_BUF_SIZE 1048576 * 2
#define OBJID_LEN 128
#define MAX_OFFSET sizeof(u_int64_t)

#define NUM_CONNS 65536

// static const int COMMAND_BYTES = sizeof(uint8_t);
// static const int OFFSET_BYTES = sizeof(uint16_t);

// protocol section
typedef enum {
  // IO commands
  CMD_SET_OBJECT,
  CMD_READ,
  CMD_WRITE,
  CMD_DISCARD,
  // control commands
  CMD_ALLOCATE_OBJECT,
} commands_t;

/* Minimal protocol frame (cmd only) */
typedef struct {
  commands_t cmd;
}__attribute__((packed)) proto_min_frame_t;

/* Basic protocol frame */
typedef struct {
  commands_t cmd;
  // var is for minimal commands only, will be overridden in other command structs
  char obj_id[OBJID_LEN];
  u_int64_t offset;
  u_int64_t val;
}__attribute__((packed)) proto_basic_frame_t;

/* IO protocol frame */
typedef struct {
  commands_t cmd;
  u_int16_t cid;
  u_int64_t offset;
  u_int32_t len;
  bool sync;
}__attribute__((packed)) proto_io_frame_t;

/* response frames */
typedef struct {
  commands_t cmd;
  u_int16_t cid;
  // TODO: if we send length in res - it should be the same type (signed-unsigned too)
  int32_t res;
}__attribute__((packed)) proto_resp_frame_t;

typedef struct {
  char id[OBJID_LEN];
  int fd;
} obj_t;

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
  //IO_KIND_FSYNC,
  //IO_KIND_DISCARD
} req_kind;

typedef struct {
  req_kind event;
  int      fd;
} request_t;

typedef struct {
  request_t req;
  // usually we'll have response frame before actual data, or/and additional iov
  struct iovec iovecs[IO_REQ_IOVECS];
  char iobuf[BUFSIZE];
  u_int16_t cid;
} io_request_t;

typedef struct {
  request_t       req;
  struct sockaddr_in addr;
  socklen_t          addrlen;
} accept_request_t;

typedef struct {
  int fd;
  obj_t obj;
  void *in_buf;
  void *out_buf;
  int32_t in_bytes;
  proto_io_frame_t *op;
} conn_t;
