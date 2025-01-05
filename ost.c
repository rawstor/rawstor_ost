#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <liburing.h>
#include <errno.h>

#include "ost.h"


/* Cheapest storage for our connections */
conn_t* conns[NUM_CONNS];
struct io_uring ring;

/* minimal request */
static request_t *req_new(req_kind event, int fd) {
  request_t *req = malloc(sizeof(request_t));
  req->event = event;
  req->fd    = fd;
  return req;
}

static io_request_t *io_req_new(req_kind event, int fd) {
  io_request_t *req = malloc(sizeof(io_request_t));
  req->req.event  = event;
  req->req.fd     = fd;
  req->iovec.iov_base = req->iobuf;
  req->iovec.iov_len  = sizeof(req->iobuf);
  return req;
}

static accept_request_t *accept_req_new(int fd) {
  accept_request_t *req = malloc(sizeof(accept_request_t));
  req->req.event = REQ_KIND_ACCEPT;
  req->req.fd    = fd;
  req->addrlen   = sizeof(req->addr);
  return req;
}

static void req_free(request_t *req) {
  free(req);
}

conn_t *setup_conn(int fd) {
  conn_t *conn = calloc( 1, sizeof(conn_t) );
  conn->fd = fd;
  conn->auth = 0;
  return conn;
}

void close_conn(int fd) {
  free(conns[fd]);
  conns[fd] = 0;
}


static io_request_t *block_io_req_new(req_kind event, int fd,  uint16_t len) {
  io_request_t *req = io_req_new(event, fd);
  req->iovec.iov_len = len;
  return req;
}



void auth_conn(conn_t *c, io_request_t *rreq) {
  conn_t *conn = c;
  printf("New conn, try auth: %s\n", rreq->iobuf);
  char objid[255];
  strlcpy(objid, rreq->iobuf, 255);
  conn->auth = 1;
  conn->chunk_fd = open(objid, O_RDWR | O_CREAT , 0644);
  if (conn->chunk_fd < 0) {
      perror("open");
      //TODO error handling
  }
  printf("Created conn: auth:%i\n", conn->auth);
}

void process_read(int fd, int res, io_request_t *rreq) {
  printf("[%d] read: %.*s\n", fd, res, rreq->iobuf);
  conn_t *conn = conns[fd];
  if (conn->auth==0) {
    auth_conn(conn, rreq);
    goto out;
  }

  switch (rreq->req.event)
  {
  case REQ_KIND_READ:
      char command[255];
      strlcpy(command, rreq->iobuf, 255);
      char cmd[10];
      int offset;
      int len;
      printf("Command: %s\n", command);
      sscanf(command, "%s %d %d\n", cmd, &offset, &len);
      printf("parsed: %d %d %s %d\n", offset, len, cmd);
      if (strncmp(cmd, "read", 4) == 0) {
        // TODO: validate offsets and lens

        /* async read file */
        struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
        io_request_t *nreq = block_io_req_new(IO_KIND_READ, fd, len);
        printf("Let's read blocks! fd:%i offset:%i len %i iov_len\n", conn->chunk_fd, offset, len);
        io_uring_prep_readv(sqe, conn->chunk_fd, &nreq->iovec, 1, offset);
        io_uring_sqe_set_data(sqe, nreq);
        int err = io_uring_submit(&ring);
        if (err < 0) {
            perror("io_uring_submit");
            exit(1);
        }
      } else {
        printf("not parsed!\n");
      }
    break;

  default:
    break;
  }

  out:

    /* Just echo for test */
    io_request_t *wreq = io_req_new(REQ_KIND_WRITE, fd);
    memcpy(wreq->iobuf, rreq->iobuf, res);
    wreq->iovec.iov_len = res;

    struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
    io_uring_prep_writev(sqe, fd, &wreq->iovec, 1, 0);
    io_uring_sqe_set_data(sqe, wreq);
    io_uring_submit(&ring);

}

void io_process_read(int fd, int res, io_request_t *rreq) {
  printf("[%d] block_read: len:%i %i\n", fd, res, rreq->iovec.iov_len);
  //conn_t *conn = conns[fd];

    io_request_t *wreq = io_req_new(REQ_KIND_WRITE, fd);
    wreq->iovec.iov_base = rreq->iovec.iov_base;
    wreq->iovec.iov_len = rreq->iovec.iov_len;

    struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
    io_uring_prep_writev(sqe, fd, &wreq->iovec, 1, 0);
    io_uring_sqe_set_data(sqe, wreq);
    io_uring_submit(&ring);
}



int main(int argc, char **argv) {
  if (argc < 2) {
    printf("usage: %s <port>\n", argv[0]);
    exit(1);
  }

  int port = atoi(argv[1]);
  if (port <= 0) {
    printf("'%s' not a valid port number\n", argv[1]);
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

  printf("listening on port %d\n", port);

  memset(&conns, 0, sizeof(conns));

  if (io_uring_queue_init(URING_QUEUE_DEPTH, &ring, 0) < 0) {
    perror("io_uring_queue_init");
    exit(1);
  }

  /* start with async form of accept(). just like the traditional version, it
   * will "block" until there's something to read, but that all happens inside
   * the kernel so we don't have to worry about it.
   *
   * we acquire a free submission queue entry (SQE) from the kernel, set it up
   * for the an async accept(), include our own request state so we can
   * understand the completion queue entry (CQE) that comes back, and submit it
   * for processing */
  struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
  accept_request_t *req = accept_req_new(server_fd);
  io_uring_prep_accept(sqe, server_fd, (struct sockaddr *) &req->addr, &req->addrlen, 0);
  io_uring_sqe_set_data(sqe, req);
  io_uring_submit(&ring);

  /* main loop. we just wait until a CQE is available, then process it */
  struct io_uring_cqe *cqe;
  while (io_uring_wait_cqe(&ring, &cqe) >= 0) {

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
          fprintf(stderr, "accept: %s\n", strerror(-res));
        }

        else {
          /* hello! client address is in the req object, because that's what we
           * pointed the request to in io_uring_prep_accept */
          printf("[%d] connect from %s:%d\n", res, inet_ntoa(areq->addr.sin_addr), ntohs(areq->addr.sin_port));

          /* remember our new connection. in a real server, you'd create a
           * connection or user object of some sort, maybe send them a
           * greeting, begin authentication, etc */

          conns[res] = setup_conn(res);

          /* set up an async read for the new connection */
          sqe = io_uring_get_sqe(&ring);
          io_request_t *rreq = io_req_new(REQ_KIND_READ, res);
          io_uring_prep_readv(sqe, res, &rreq->iovec, 1, 0);
          io_uring_sqe_set_data(sqe, rreq);
          io_uring_submit(&ring);
        }

        /* make a new async accept, since the previous one was consumed. note
         * that we're reusing the request object, but its not special - freeing
         * it and making a new one would also be just fine */
        sqe = io_uring_get_sqe(&ring);
        io_uring_prep_accept(sqe, fd, (struct sockaddr *) &areq->addr, &areq->addrlen, 0);
        io_uring_sqe_set_data(sqe, areq);
        io_uring_submit(&ring);

        break;
      }

      /* someone sent something */
      case REQ_KIND_READ: {
        /* get a handle on the more specialised request */
        io_request_t *rreq = (io_request_t *) req;

        /* some error, disconnect them */
        if (res < 0) {
          fprintf(stderr, "readv(%d): %s\n", fd, strerror(-res));

          /* make a async close request. we use a minimal request object
           * because close has no interesting args or return; we just need a
           * marker so we can recognise the response for what it is */
          request_t *clreq = req_new(REQ_KIND_CLOSE, fd);
          sqe = io_uring_get_sqe(&ring);
          io_uring_prep_close(sqe, fd);
          io_uring_sqe_set_data(sqe, clreq);
          io_uring_submit(&ring);

          /* free the read request, since we're not going to be reissuing it */
          req_free(req);
          close_conn(fd);
        }

        /* zero read, they gracefully closed the connection */
        else if (res == 0) {
          printf("[%d] closed\n", fd);

          /* see error block above, this is the same behaviour */

          request_t *clreq = req_new(REQ_KIND_CLOSE, fd);
          sqe = io_uring_get_sqe(&ring);
          io_uring_prep_close(sqe, fd);
          io_uring_sqe_set_data(sqe, clreq);
          io_uring_submit(&ring);

          req_free(req);
          close_conn(fd);
        }

        else {
          /* they sent some data, which is now in the request iobuf (via the
           * iovec we sent in) */
          process_read(fd, res, rreq);


        /* make a new async read, since the previous one was consumed. note
         * that we're reusing the request object, but its not special - freeing
         * it and making a new one would also be just fine */
          sqe = io_uring_get_sqe(&ring);
          io_uring_prep_readv(sqe, fd, &rreq->iovec, 1, 0);
          io_uring_sqe_set_data(sqe, rreq);
          io_uring_submit(&ring);
        }

        break;
      }

      /* they finished receiving what we sent */
      case REQ_KIND_WRITE: {

        /* failed write, so disconnect them */
        if (res < 0) {
          fprintf(stderr, "writev(%d): %s\n", fd, strerror(-res));
          req_free(req);

          /* see read error handling */

          request_t *clreq = req_new(REQ_KIND_CLOSE, fd);
          sqe = io_uring_get_sqe(&ring);
          io_uring_prep_close(sqe, fd);
          io_uring_sqe_set_data(sqe, clreq);
          io_uring_submit(&ring);

          close_conn(fd);
        }

        else {
          fprintf(stderr, "[%d] writev success: %i\n", fd, res);
          /* written successfully, so just free the read req */
          req_free(req);
        }

        break;
      }

      /* async close completed */
      case REQ_KIND_CLOSE: {
        /* just free the request, we've already cleaned up and there's nothing
         * useful we could do if the close failed anyway */
        req_free(req);
        close_conn(fd);
        break;
      }
      /* someone sent something */
      case IO_KIND_READ: {
        /* get a handle on the more specialised request */
        io_request_t *rreq = (io_request_t *) req;

        // TODO: handle read errors
        io_process_read(fd, res, rreq);

        break;
      }
    }

    /* mark the CQE "seen", returning it to the ring for reuse */
    io_uring_cqe_seen(&ring, cqe);
  }

  /* io_uring_wait_cqe failed. in a real server you might actually need to
   * handle non-error cases like EINTR, but it complicates this example so we
   * won't bother */
  perror("io_uring_wait_cqe");
  exit(1);
}
