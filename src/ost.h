#include <arpa/inet.h>
#include <liburing.h>
#include "stdint.h"

#define BUFSIZE 8192
#define MIN_CMD_VAR_LEN 32
// TODO: set appropriate val
#define OBJID_LEN 255

#define URING_QUEUE_DEPTH (256)
#define NUM_CONNS 65536

// static const int COMMAND_BYTES = sizeof(uint8_t);
// static const int OFFSET_BYTES = sizeof(uint16_t);

// protocol section
typedef enum {
  CMD_SET_OBJECT,
  CMD_READ,
  CMD_WRITE,
  CMD_DISCARD,
} commands_t;

/* minimalistic protocol frame */
typedef struct {
  commands_t cmd;
  // var is for minimal commands only, will be overridden in other command structs
  char var[MIN_CMD_VAR_LEN];
}__attribute__((packed)) proto_basic_frame_t;

typedef struct {
  commands_t cmd;
  u_int64_t offset;
  u_int16_t len;
}__attribute__((packed)) proto_io_frame_t;

typedef struct {
  commands_t cmd;
  u_int64_t offset;
  u_int16_t len;
  bool sync;
}__attribute__((packed)) proto_io_w_frame_t;

/* response frames */
typedef struct {
  commands_t cmd;
  int16_t res;
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
  // usually we'll have response frame before actual data
  struct iovec iovecs[2];
  char iobuf[BUFSIZE];
} io_request_t;

typedef struct {
  request_t       req;
  struct sockaddr_in addr;
  socklen_t          addrlen;
} accept_request_t;

typedef struct {
  int fd;
  obj_t obj;
} conn_t;
