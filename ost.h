#include "stdint.h"

#define BUFSIZE 8192

#define URING_QUEUE_DEPTH (256)
#define NUM_CONNS 65536

// static const int COMMAND_BYTES = sizeof(uint8_t);
// static const int OFFSET_BYTES = sizeof(uint16_t);

//TODO: discard?
typedef enum {
  // network request
  REQ_KIND_ACCEPT,
  REQ_KIND_READ,
  REQ_KIND_WRITE,
  REQ_KIND_CLOSE,
  // our block request
  IO_KIND_READ
} req_kind;

typedef struct {
  req_kind event;
  int      fd;
} request_t;

typedef struct {
  request_t req;
  struct iovec iovec;
  char iobuf[BUFSIZE];
} io_request_t;

typedef struct {
  request_t       req;
  struct sockaddr_in addr;
  socklen_t          addrlen;
} accept_request_t;

typedef struct {
  int fd;
  bool auth;
  char objname[255];
  //chunk_t chunks[255];
  int chunk_fd;
} conn_t;
