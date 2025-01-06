#include "ost.h"

#include <arpa/inet.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <unistd.h>


void set_params(int sockfd, char *buff) {
  proto_basic_frame_t *mframe = malloc(sizeof(proto_basic_frame_t));
  mframe->cmd = CMD_SET_OBJECT;
  strlcpy(mframe->var, "objid1", 10);
  int res = write(sockfd, mframe, sizeof(proto_basic_frame_t));
  printf("Sent request to set objid, res:%i\n", res);
  read(sockfd, buff, sizeof(buff));
  proto_resp_frame_t *rframe = malloc(sizeof(proto_resp_frame_t));
  memcpy(rframe, buff, sizeof(proto_resp_frame_t));
  printf("Response from Server: cmd:%i res:%i\n", rframe->cmd, rframe->res);
}

void read_something(int sockfd) {
  char buff[8192];
  int res;

  for (;;) {
    bzero(buff, sizeof(buff));

    set_params(sockfd, buff);

    printf("Read data?");
    getchar();

    proto_io_frame_t *frame = malloc(sizeof(proto_io_frame_t));
    frame->cmd = CMD_READ;
    frame->offset = 0;
    frame->len = 100;
    res = write(sockfd, frame, sizeof(proto_io_frame_t));
    printf("Sent request read command, res:%i\n", res);

    read(sockfd, buff, sizeof(proto_resp_frame_t));
    proto_resp_frame_t *rframe = malloc(sizeof(proto_resp_frame_t));
    memcpy(rframe, buff, sizeof(proto_resp_frame_t));
    printf("Response from Server: cmd:%i res:%i\n", rframe->cmd, rframe->res);

    if (rframe->res >= 0) {
      read(sockfd, buff, sizeof(buff));
      printf("Response from Server data:\n%.*s\n", rframe->res, buff);
    } else {
      printf("There was an error on server side, so no data for us\n");
    }

    printf("Retry?");
    getchar();
  }
}

void write_something(int sockfd, char *data) {
  char buff[8192];
  int res;

  bzero(buff, sizeof(buff));

  set_params(sockfd, buff);

  printf("Write data?");
  getchar();

  proto_io_w_frame_t *frame = malloc(sizeof(proto_io_w_frame_t));
  frame->cmd = CMD_WRITE;
  frame->offset = 0;
  frame->len = strlen(data);
  frame->sync = 1;

  struct iovec iovecs[2];

  iovecs[0].iov_base = frame;
  iovecs[0].iov_len = sizeof(proto_io_w_frame_t);

  iovecs[1].iov_base = data;
  iovecs[1].iov_len = strlen(data);

  res = writev(sockfd, iovecs, 2);
  printf("Sent request write command and data:%s, len:%li, res:%i\n", data, strlen(data), res);

  read(sockfd, buff, sizeof(proto_resp_frame_t));
  proto_resp_frame_t *rframe = malloc(sizeof(proto_resp_frame_t));
  memcpy(rframe, buff, sizeof(proto_resp_frame_t));
  printf("Response from Server: cmd:%i res:%i\n", rframe->cmd, rframe->res);
}


int main(int argc, char **argv) {
  if (argc < 2) {
    printf("usage: %s <port> [data_to_write]\n", argv[0]);
    exit(1);
  }

  int port = atoi(argv[1]);
  if (port <= 0) {
    printf("'%s' not a valid port number\n", argv[1]);
    exit(1);
  }

  int sockfd;
  struct sockaddr_in servaddr;
  // socket create and verification
  sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (sockfd == -1) {
    printf("socket creation failed...\n");
    exit(0);
  }
  else
    printf("Socket successfully created..\n");
  bzero(&servaddr, sizeof(servaddr));
  // assign IP, PORT
  servaddr.sin_family = AF_INET;
  servaddr.sin_addr.s_addr = inet_addr("127.0.0.1");
  servaddr.sin_port = htons(port);
  // connect the client socket to server socket
  if (connect(sockfd, (struct sockaddr*)&servaddr, sizeof(servaddr))
    != 0) {
    printf("connection with the server failed...\n");
    exit(0);
  }
  else
    printf("connected to the server..\n");

  if (argc == 3) {
    write_something(sockfd, argv[2]);
  }


  read_something(sockfd);

  close(sockfd);
}
