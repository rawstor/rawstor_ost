#include "log.h"
#include "ost.h"
#include "ruring.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <sys/ioctl.h>
#include <errno.h>


/* Cheapest storage for our connections */
conn_t* conns[NUM_CONNS];
char objdir_path[256];
struct io_uring ring;


/* minimal request */
static request_t *req_new(req_kind event, int fd) {
  request_t *req = malloc(sizeof(request_t));
  req->event = event;
  req->fd    = fd;
  return req;
}

inline void req_free(request_t *req) {
  free(req);
}

inline io_request_t *io_req_new(req_kind event, int fd, u_int16_t cid) {
  io_request_t *req = malloc(sizeof(io_request_t));
  req->req.event  = event;
  req->req.fd     = fd;
  req->iovecs[0].iov_base = req->iobuf;
  req->iovecs[0].iov_len  = sizeof(req->iobuf);
  req->iovecs[1].iov_len  = 0;
  req->iovecs[2].iov_len  = 0;
  req->cid = cid;
  return req;
}

inline void io_req_free(io_request_t *ioreq) {
  for(int i = 0; i < IO_REQ_IOVECS; i++){
    LOG_DEBUG("io_req_free: i:%i\n", i);
    if (ioreq->iovecs[i].iov_len > 0 && ioreq->iovecs[i].iov_base != (void *)ioreq->iobuf) {
      free(ioreq->iovecs[i].iov_base);
    }
}
  free(ioreq);
}

static accept_request_t *accept_req_new(int fd) {
  accept_request_t *req = malloc(sizeof(accept_request_t));
  req->req.event = REQ_KIND_ACCEPT;
  req->req.fd    = fd;
  req->addrlen   = sizeof(req->addr);
  return req;
}

static conn_t *setup_conn(int fd) {
  conn_t *conn = malloc(sizeof(conn_t));
  conn->fd = fd;
  conn->in_bytes = 0;
  conn->in_buf = malloc(MAX_BUF_SIZE);
  conn->out_buf = malloc(MAX_BUF_SIZE);
  conn->op = NULL;
  return conn;
}

void close_conn(int fd) {
  if (conns[fd] == NULL) {
    return;
  }
    /* make a async close request. we use a minimal request object
    * because close has no interesting args or return; we just need a
    * marker so we can recognise the response for what it is */
  request_t *clreq = req_new(REQ_KIND_CLOSE, fd);
  struct io_uring_sqe *sqe = get_sqe(&ring);
  io_uring_prep_close(sqe, fd);
  io_uring_sqe_set_data(sqe, clreq);
  io_uring_submit(&ring);

  free(conns[fd]->in_buf);
  free(conns[fd]->out_buf);

  free(conns[fd]);
  conns[fd] = 0;
}

inline void block_io_req_reuse(io_request_t *ioreq, req_kind event, uint32_t len) {
  ioreq->req.event = event;
  if (len>BUFSIZE) {
    ioreq->iovecs[1].iov_base = malloc(len-BUFSIZE);
    ioreq->iovecs[1].iov_len = len-BUFSIZE;
  } else {
    ioreq->iovecs[0].iov_len = len;
  }
}

inline io_request_t *block_io_req_new(req_kind event, int fd, u_int16_t cid, uint32_t len) {
  io_request_t *ioreq = io_req_new(event, fd, cid);
  block_io_req_reuse(ioreq, event, len);
  return ioreq;
}

static proto_resp_frame_t *prep_resp_frame(commands_t cmd, u_int16_t cid, int res) {
  proto_resp_frame_t *rframe = malloc(sizeof(proto_resp_frame_t));
  rframe->cmd = cmd;
  rframe->cid = cid;
  rframe->res = res;
  return rframe;
}

static void prep_and_send_resp_frame(int fd, commands_t cmd, u_int16_t cid, int res) {
  proto_resp_frame_t *rframe = prep_resp_frame(cmd, cid, res);

  io_request_t *wreq = io_req_new(REQ_KIND_WRITE, fd, cid);
  wreq->iovecs[0].iov_base = rframe;
  wreq->iovecs[0].iov_len = sizeof(proto_resp_frame_t);

  struct io_uring_sqe *sqe = get_sqe(&ring);
  io_uring_prep_write(sqe, fd, wreq->iovecs[0].iov_base, wreq->iovecs[0].iov_len, 0);
  io_uring_sqe_set_data(sqe, wreq);
  io_uring_submit(&ring);

  //free(rframe);
}

static int set_conn_params(conn_t *c, proto_basic_frame_t *frame) {
  conn_t *conn = c;
  // TODO: set valid len
  strlcpy(conn->obj.id, frame->obj_id, OBJID_LEN);
  char obj_file_path[600];
  sprintf(obj_file_path, "%s/%s", objdir_path, conn->obj.id);
  LOG_INFO("[%i]Set connection objid: %s, obj file:%s\n", conn->fd, conn->obj.id, obj_file_path);
  //TODO: validate incoming frame and objid
  conn->obj.fd = open(obj_file_path, O_RDWR | O_CREAT , 0664);
  if (conn->obj.fd < 0) {
      perror("open");
      close_conn(c->fd);
      return 1;
  }
  return 0;
}

int buffer_in_stream(conn_t *conn, proto_io_frame_t *frame, void *buf, int len) {
  memcpy(conn->in_buf + conn->in_bytes, buf, len);
  conn->in_bytes += len;

  LOG_DEBUG("in_bytes: %i, now_len:%i, total_len:%i\n", conn->in_bytes, len, frame->len);

  if (conn->in_bytes>frame->len) {
    LOG_INFO("[%i]too much data for incoming write!", conn->fd);
    return 1;
  }

  return 0;
}

void push_block_write(conn_t *conn, proto_io_frame_t *frame, void *buf) {
    struct io_uring_sqe *sqe = get_sqe(&ring);
    io_request_t *ioreq = block_io_req_new(IO_KIND_WRITE, conn->fd, frame->cid, frame->len);
    //TODO: optimize memcpy?
    char *nbuf = malloc(frame->len);
    memcpy(nbuf, buf, frame->len);
    ioreq->iovecs[0].iov_base = nbuf;
    ioreq->iovecs[0].iov_len = frame->len;
    LOG_DEBUG("[%i]push_block_write: block_fd:%i offset:%li len:%i, sync:%i\n",
           conn->fd, conn->obj.fd, frame->offset, frame->len, frame->sync);

    if (frame->sync) {
      io_uring_prep_writev2(sqe, conn->obj.fd, ioreq->iovecs, 1, frame->offset, RWF_SYNC);
    } else {
      io_uring_prep_writev(sqe, conn->obj.fd, ioreq->iovecs, 1, frame->offset);
    }

    io_uring_sqe_set_data(sqe, ioreq);
    io_uring_submit(&ring);
}

int process_recv(struct ctx *ctx, struct io_uring_cqe *cqe, int fd, int res, io_request_t *ioreq) {
  int frame_offset = 0;

  LOG_DEBUG("[%d]process_recv: res:%i\n", fd, res);
  void *data = extract_cqe_recv_buf(ctx, cqe);

  conn_t *conn = conns[fd];

  if (uring_unlikely(conns[fd] == NULL)) {
    LOG_DEBUG("[%d]process_recv: packet from already disconnected client, ignore\n", fd);
    goto error;
  }

  process_start:

  if (conn->op == NULL) {
    if (uring_unlikely(res < sizeof(commands_t))) {
      FLOG_INFO(stderr, "[%d]Recv data is less than minimal op frame!: %i < %lu\n", fd, res, sizeof(commands_t));
      goto error;
    }

    /* try to cast raw stream bytes into our minimal structs */
    proto_basic_frame_t *mframe = (proto_basic_frame_t*) data;
    LOG_DEBUG("cmd: %i\n", mframe->cmd);

    if (!conn->obj.fd && mframe->cmd!=CMD_SET_OBJECT) {
      prep_and_send_resp_frame(fd, mframe->cmd, 0, -1);
      goto error;
    }

    /* Some commands won't have additional data */
    switch (mframe->cmd)
    {
    case CMD_SET_OBJECT: {
      int res = set_conn_params(conn, mframe);
      prep_and_send_resp_frame(fd, mframe->cmd, 0, res);
      goto out_free_op;
      break;
    }
    default: {
      if (res < sizeof(proto_io_frame_t)) {
        FLOG_INFO(stderr, "[%d]CMD_READ recv: data len:%i differs from frame size:%lu", fd, res, sizeof(proto_io_frame_t));
        goto error;
      }
      LOG_DEBUG("[%d]process_recv: set new op: %i\n", fd, mframe->cmd);

      /* Buffer may be reused on long multipart recv */
      conn->op = malloc(sizeof(proto_io_frame_t));
      memcpy(conn->op, mframe, sizeof(proto_io_frame_t));
      frame_offset = sizeof(proto_io_frame_t);
      LOG_DEBUG("[%d]process_recv after copy: set new op: %i\n", fd,  conn->op->cmd);
    }
    }
  }

  LOG_DEBUG("process_recv after basic parse: %i %u %lu %u\n", conn->op->cmd, conn->op->len, conn->op->offset, conn->op->sync);

  switch (conn->op->cmd)
  {
  case CMD_READ: {
    /* async read object */
    struct io_uring_sqe *sqe = get_sqe(&ring);
    io_request_t *nioreq = block_io_req_new(IO_KIND_READ, fd, conn->op->cid, conn->op->len);
    LOG_DEBUG("[%i]CMD_READ: block_fd:%i offset:%li len:%i\n", fd, conn->obj.fd, conn->op->offset, conn->op->len);
    io_uring_prep_readv(sqe, conn->obj.fd, nioreq->iovecs, 2, conn->op->offset);
    io_uring_sqe_set_data(sqe, nioreq);
    io_uring_submit(&ring);

    goto out_free_op;
    break;
  }
  case CMD_WRITE: {
    int data_offset = frame_offset;
    int data_len = res - frame_offset;

    // There may be another frame after end of data
    if (data_len > conn->op->len - conn->in_bytes) {
      data_len = conn->op->len - conn->in_bytes;
      frame_offset += data_len;
    }

    /* Fast path, when full data is already here */
    if (data_len == conn->op->len) {
      push_block_write(conn, conn->op, data + data_offset);
      goto out_free_op;
    }

    LOG_DEBUG("CMD_WRITE: got part of data, use slow path with buffer. Remaining:%i, actual:%i\n",
                conn->op->len - conn->in_bytes, data_len);
    if (buffer_in_stream(conn, conn->op, data + data_offset, data_len)) {
      goto error;
    }

    if (conn->in_bytes == conn->op->len) {
      push_block_write(conn, conn->op, conn->in_buf);
      memset(conn->in_buf, 0, conn->in_bytes);
      conn->in_bytes = 0;
      goto out_free_op;
    }
    break;
  }
  default:
    FLOG_INFO(stderr, "[%d]Unknown proto command: %i\n", fd, conn->op->cmd);
    goto error;
    break;
  }

  if (res - frame_offset > 0) {
    LOG_DEBUG("[%d]process_recv: there's additional data, process it too. len:%i\n",
              fd, res - frame_offset);

    free(conn->op);
    conn->op = NULL;
    res = res - frame_offset;
    data = data + res;
    goto process_start;
  }

  recycle_buffer(ctx, extract_cqe_buffer_idx(cqe));
  return 0;

  out_free_op:
  free(conn->op);
  conn->op = NULL;
  recycle_buffer(ctx, extract_cqe_buffer_idx(cqe));
  return 0;

  error:
  recycle_buffer(ctx, extract_cqe_buffer_idx(cqe));
  return 1;
}

void io_process_read(int fd, int res, io_request_t *ioreq) {
  LOG_DEBUG("[%d]io_process_read: res:%i len:%li\n", fd, res, ioreq->iovecs[0].iov_len + ioreq->iovecs[1].iov_len);

  // Reuse ioreq to minimize allocations
  ioreq->req.event = REQ_KIND_WRITE;
  // Add space for resp frame before data
  //memmove(&ioreq->iovecs[1], &ioreq->iovecs[0], 2*sizeof(struct iovec));
  ioreq->iovecs[2] = ioreq->iovecs[1];
  ioreq->iovecs[1] = ioreq->iovecs[0];

  proto_resp_frame_t *rframe = prep_resp_frame(CMD_READ, ioreq->cid, res);
  ioreq->iovecs[0].iov_base = rframe;
  ioreq->iovecs[0].iov_len = sizeof(proto_resp_frame_t);

  struct msghdr msg = {
    .msg_iov = ioreq->iovecs,
    .msg_iovlen = 3
  };

  struct io_uring_sqe *sqe = get_sqe(&ring);
  io_uring_prep_sendmsg(sqe, fd, &msg, MSG_WAITALL);
  io_uring_sqe_set_data(sqe, ioreq);
  io_uring_submit(&ring);
}

void io_process_write(int fd, int res, io_request_t *ioreq) {
  LOG_DEBUG("[%d]block_write: res:%i\n", fd, res);

  prep_and_send_resp_frame(fd, CMD_WRITE, ioreq->cid, res);
}

static int handle_cqe(struct ctx *ctx, struct io_uring_cqe *cqe) {
  int ret = 0;
  struct io_uring_sqe *sqe;

  /* get our own request back. for the moment, just the header */
  request_t *req = (request_t *) cqe->user_data;
  int fd = req->fd;

  /* the return value of the underlying syscall. typically a negative value
    * will be the negated errno value for the call, so we can still do error
    * handling */
  int res = cqe->res;

  /* do the right thing depending on what kind of request just completed */
  switch (req->event) {

    /* someone connected! */
    case REQ_KIND_ACCEPT: {
      /* get a handle on the more specialised request */
      accept_request_t *areq = (accept_request_t *) req;

      /* maybe it failed? */
      if (res < 0) {
        /* note negation of return value in place of errno */
        FLOG_INFO(stderr, "accept: %s\n", strerror(-res));
      } else {
        /* hello! client address is in the req object, because that's what we
          * pointed the request to in io_uring_prep_accept */
        LOG_INFO("[%d]Connect from %s:%d\n", res, inet_ntoa(areq->addr.sin_addr), ntohs(areq->addr.sin_port));

        /* remember our new connection. in a real server, you'd create a
          * connection or user object of some sort, maybe send them a
          * greeting, begin authentication, etc */

        conns[res] = setup_conn(res);

        /* set up an async read for the new connection */
        io_request_t *ioreq = io_req_new(REQ_KIND_READ, res, 0);
        /* Don't try to reuse this ioreq! It'll be reused for recv_multishot! */
        add_recv(ctx, res, ioreq);
        io_uring_submit(&ring);
      }

      /* make a new async accept, since the previous one was consumed. note
        * that we're reusing the request object, but its not special - freeing
        * it and making a new one would also be just fine */
      sqe = get_sqe(&ring);
      io_uring_prep_accept(sqe, fd, (struct sockaddr *) &areq->addr, &areq->addrlen, 0);
      io_uring_sqe_set_data(sqe, areq);
      io_uring_submit(&ring);

      break;
    }

    /* someone sent something */
    case REQ_KIND_READ: {
      /* Important - ioreq will be reused for same connection in recv_multishot, don't touch it! */
      /* get a handle on the more specialised request */
      io_request_t *ioreq = (io_request_t *) req;

      /* some error, disconnect them */
      if (res < 0) {
        FLOG_INFO(stderr, "readv(%d): %i %s\n", fd, res, strerror(-res));

        close_conn(fd);

        /* free the read request, since we're not going to be reissuing it */
        io_req_free(ioreq);
      }

      /* zero read, they gracefully closed the connection */
      else if (uring_unlikely(res == 0)) {
        LOG_INFO("[%d]closed\n", fd);

        /* see error block above, this is the same behaviour */

        close_conn(fd);
        io_req_free(ioreq);
      }

      else {
        /* they sent some data, which is now in the request iobuf (via the
          * iovecs we sent in) */
        if (!(cqe->flags & IORING_CQE_F_MORE) && conns[fd] != NULL) {
          LOG_DEBUG("Incoming cqe didn't have IORING_CQE_F_MORE flag! Recreate recv event\n");

          /* set up an async read for the new connection */
          add_recv(ctx, res, ioreq);
          break;

          // if (ret)
          // 	return ret;
        }
        //process_read(ctx, cqe, fd, res, rreq);
        ret = process_recv(ctx, cqe, fd, res, ioreq);
        if (ret) {
          FLOG_DEBUG(stderr, "[%d]process_recv returned %i, disconnect client\n", fd, ret);
          // TODO: client doesn't see disconnection
          close_conn(fd);
        }
      }

      break;
    }

    /* they finished receiving what we sent */
    case REQ_KIND_WRITE: {
      io_request_t *ioreq = (io_request_t *) cqe->user_data;
      /* failed write, so disconnect them */
      if (res < 0) {
        FLOG_INFO(stderr, "[%d]REQ_KIND_WRITE: err %s\n", fd, strerror(-res));
        io_req_free(ioreq);

        /* see read error handling */
        close_conn(fd);
      }

      else {
        FLOG_DEBUG(stderr, "[%d]REQ_KIND_WRITE success: %i\n", fd, res);
        /* written successfully, so just free req */
        io_req_free(ioreq);
      }

      break;
    }

    /* async close completed */
    case REQ_KIND_CLOSE: {
      /* just free the request, we've already cleaned up and there's nothing
        * useful we could do if the close failed anyway */
      req_free(req);
      break;
    }
    /* Block read came back */
    case IO_KIND_READ: {
      /* get a handle on the more specialised request */
      io_request_t *ioreq = (io_request_t *) req;

      // TODO: handle read errors
      io_process_read(fd, res, ioreq);

      break;
    }
    /* Block write came back */
    case IO_KIND_WRITE: {
      /* get a handle on the more specialised request */
      io_request_t *ioreq = (io_request_t *) cqe->user_data;

      io_process_write(fd, res, ioreq);
      io_req_free(ioreq);

      break;
    }
    default: {
      FLOG_INFO(stderr, "[%d]handle_cqe: get cqe without useful user_data? req->event:%i, res:%i, user_data:%llu\n", fd, req->event, res, cqe->user_data);
    }
  }
  return ret;
}

int main(int argc, char **argv) {
  int ret;

  if (argc < 3) {
    printf("usage: %s <port> <path_to_objdir>\n", argv[0]);
    exit(1);
  }

  int port = atoi(argv[1]);
  if (port <= 0) {
    printf("'%s' not a valid port number\n", argv[1]);
    exit(1);

  }

  /* Use dir to store objects */
  strlcpy(objdir_path, argv[2], 256);
  struct stat info;

  if( stat( objdir_path, &info ) != 0 ) {
      printf( "cannot access %s\n", objdir_path );
      exit(1);
  } else if(!S_ISDIR(info.st_mode)) {
      printf( "%s is not a directory\n", objdir_path );
      exit(1);
  }

  /* create the server socket */
  int server_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (server_fd < 0) {
    perror("socket");
    exit(1);
  }

  /* arrange for the listening address to be reusable. This makes TCP
   * marginally "less safe" (for a whole bunch of obscure reasons) but allows
   * us to kill and restart the program with ease */
  int onoff = 1;
  if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &onoff, sizeof(onoff)) < 0) {
    perror("setsockopt");
    exit(1);
  }
  if (setsockopt(server_fd, IPPROTO_TCP, TCP_NODELAY, &onoff, sizeof(onoff)) < 0) {
    perror("setsockopt");
    exit(1);
  }

  /* set up the address structure for binding, which is *:<port> */
  struct sockaddr_in sin = {
    .sin_family = AF_INET,
    .sin_port   = htons(port),
    .sin_addr   = {
      .s_addr = htonl(INADDR_ANY)
    }
  };
  socklen_t sin_len = sizeof(sin);

  /* bind the server socket to the wanted address */
  if (bind(server_fd, (struct sockaddr *) &sin, sin_len) < 0) {
    perror("bind");
    exit(1);
  }

  /* and open it for connections! */
  if (listen(server_fd, 10) < 0) {
    perror("listen");
    exit(1);
  }

  LOG_DEBUG("listening on port %d\n", port);

  struct ctx ctx;
  ctx.ring = &ring;
	ctx.af = AF_INET;
	ctx.buf_shift = BUF_SHIFT;

  if (setup_context(&ctx)) {
		return 1;
	}

  memset(&conns, 0, sizeof(conns));

	ret = io_uring_register_files(ctx.ring, &server_fd, 1);
	if (ret) {
		fprintf(stderr, "register files: %s\n", strerror(-ret));
		return -1;
	}

  /* start with async form of accept(). just like the traditional version, it
   * will "block" until there's something to read, but that all happens inside
   * the kernel so we don't have to worry about it.
   *
   * we acquire a free submission queue entry (SQE) from the kernel, set it up
   * for the an async accept(), include our own request state so we can
   * understand the completion queue entry (CQE) that comes back, and submit it
   * for processing */
  struct io_uring_sqe *sqe = get_sqe(&ring);
  accept_request_t *req = accept_req_new(server_fd);
  io_uring_prep_accept(sqe, server_fd, (struct sockaddr *) &req->addr, &req->addrlen, 0);
  io_uring_sqe_set_data(sqe, req);
  io_uring_submit(&ring);

  // struct __kernel_timespec active_ts = {0, 1000};

  /* main loop. we just wait until a CQE is available, then process it */
  struct io_uring_cqe *cqe;

  while (1) {
    unsigned head;
    unsigned int i = 0;
    //int ret;

    // For single connection it may only worsen IOPS
    //io_uring_submit_and_wait_timeout(&ring, &cqe, 10, &active_ts, NULL);

    io_uring_for_each_cqe(&ring, head, cqe) {
      handle_cqe(&ctx, cqe);
      i++;
    }

    io_uring_submit(&ring);
    advance_recycled_buffers(&ctx);

		if (i) {
			io_uring_cq_advance(&ring, i);
		}

    LOG_DEBUG("Main_loop: processed %i cqes via io_uring_for_each_cqe\n", i);
    LOG_DEBUG("Unconsumed buffers: %i\n", io_uring_buf_ring_available(ctx.ring, ctx.buf_ring, ctx.bgid));

    /* We already processed what we could, now wait for new events, we'll process them in next loop */
    if (io_uring_wait_cqe(&ring, &cqe)<0) {
      perror("io_uring_wait_cqe");
      exit(1);
    }
  }
}
