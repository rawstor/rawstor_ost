#include "ost.h"

#include <arpa/inet.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <unistd.h>


void send_something(int sockfd) {
    char buff[8192];

    for (;;) {
        bzero(buff, sizeof(buff));

        proto_basic_frame_t *mframe = malloc(sizeof(proto_basic_frame_t));
        mframe->cmd = CMD_SET_OBJECT;
        strlcpy(mframe->var, "objid1", 10);
        int res = write(sockfd, mframe, sizeof(proto_basic_frame_t));
        printf("Sent request to set objid, res:%i\n", res);

        printf("Read data?");
        getchar();

        proto_io_frame_t *frame = malloc(sizeof(proto_io_frame_t));
        frame->cmd = CMD_READ;
        frame->offset = 0;
        frame->len = 100;
        res = write(sockfd, frame, sizeof(proto_io_frame_t));
        printf("Sent request read command, res:%i\n", res);
        read(sockfd, buff, sizeof(buff));
        printf("From Server : %s\n", buff);

        printf("Retry?");
        getchar();
    }
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
  // function for chat
  send_something(sockfd);
  // close the socket
  close(sockfd);
}
