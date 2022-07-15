#include <arpa/inet.h>
#include <errno.h>
#include <liburing.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

struct request {
  int event_type;
  int iovec_count;
  int client_socket;
  struct iovec iov[];
};

#define REQ_TEST_TCP_STREAM 0x01
#define REQ_TEST_TCP_RR 0x02

struct io_uring ring;

#define QUEUE_DEPTH 256
#define READ_SZ 131072

#define BLOCK_SZ    1024

static char read_buf[READ_SZ];

#define EVENT_TYPE_ACCEPT 0
#define EVENT_TYPE_READ 1
#define EVENT_TYPE_WRITE 2

void fatal_error(const char *syscall) {
  perror(syscall);
  exit(1);
}

int add_accept_request(int server_socket, struct sockaddr_in *client_addr,
                       socklen_t *client_addr_len) {
  struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
  io_uring_prep_accept(sqe, server_socket, (struct sockaddr *)client_addr,
                       client_addr_len, 0);
  struct request *req = malloc(sizeof(*req));
  req->event_type = EVENT_TYPE_ACCEPT;
  io_uring_sqe_set_data(sqe, req);
  io_uring_submit(&ring);

  return 0;
}

int add_read_request(int client_socket) {
  struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
  struct request *req = malloc(sizeof(*req) + sizeof(struct iovec));
  //   req->iov[0].iov_base = read_buf;
  req->iov[0].iov_base = malloc(READ_SZ);
  req->iov[0].iov_len = READ_SZ;
  req->iovec_count = 1;
  req->event_type = EVENT_TYPE_READ;
  req->client_socket = client_socket;
  memset(req->iov[0].iov_base, 0, READ_SZ);
  /* Linux kernel 5.5 has support for readv, but not for recv() or read() */
  io_uring_prep_readv(sqe, client_socket, &req->iov[0], 1, 0);
  io_uring_sqe_set_data(sqe, req);
  io_uring_submit(&ring);
  return 1;
}

int add_write_request(struct request *req) {
  struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
  req->event_type = EVENT_TYPE_WRITE;
  io_uring_prep_writev(sqe, req->client_socket, req->iov, req->iovec_count, 0);
  io_uring_sqe_set_data(sqe, req);
  io_uring_submit(&ring);
  return 0;
}

void send_string(const char *str, int client_socket) {
  struct request *req = malloc(sizeof(*req) + sizeof(struct iovec));
  unsigned long slen = strlen(str);
  req->iovec_count = 1;
  req->client_socket = client_socket;
  req->iov[0].iov_base = malloc(slen);
  req->iov[0].iov_len = slen;
  memcpy(req->iov[0].iov_base, str, slen);
  add_write_request(req);
}

void send_data(const char *str, size_t len, int client_socket) {
  struct request *req = malloc(sizeof(*req) + sizeof(struct iovec));
  req->iovec_count = 1;
  req->client_socket = client_socket;
  req->iov[0].iov_base = malloc(len);
  req->iov[0].iov_len = len;
  memcpy(req->iov[0].iov_base, str, len);
  add_write_request(req);
}

int get_line(const char *src, char *dest, int dest_sz) {
  for (int i = 0; i < dest_sz; i++) {
    dest[i] = src[i];
    printf("%d\n", src[i]);
    if (src[i] == '\r' && src[i + 1] == '\n') {
      dest[i] = '\0';
      return 0;
    }
  }
  return 1;
}

int handle_client_request(struct request *req, size_t req_sz) {
  // char http_request[1024];
  /* Get the first line, which will be the request */
  // if(get_line(req->iov[0].iov_base, http_request, sizeof(http_request))) {
  //     fprintf(stderr, "Malformed request\n");
  //     exit(1);
  // }
  // handle_http_method(http_request, req->client_socket);
  // printf("%ld\n", req_sz);
  // for (int i = 0; i < 1000; i++)
  //     printf("%d ", ((char *)(req->iov[0].iov_base))[i]);
  // printf("%ld\n", req->iov[0].iov_len);
  // send_string("test", req->client_socket);
  //   if (((char *)(req->iov[0].iov_base))[0] == REQ_TEST_TCP_STREAM) {
  //     printf("%ld\n", req->iov[0].iov_len);
  //   } else if (((char *)(req->iov[0].iov_base))[0] == REQ_TEST_TCP_RR) {
  //     printf("%ld\n", req->iov[0].iov_len);
  //   }
  // send_data(req->iov[0].iov_base, req->iov[0].iov_len, req->client_socket);

  int iter = req_sz / 16384;
  printf("len = %ld -> (len / 16384) = %d\n", req_sz, iter);
  // unsigned int crc = 0;
  // for (int i = 0; i < iter; ++i) {
  //   crc = crc_pcl((unsigned char *)req->iov[0].iov_base + i * 16384, 16384, crc);
  //   printf("crc for #%d complete 16384 bytes -> %u\n", i + 1, crc);
  // }

  return 0;
}

void sigint_handler(int signo) {
  printf("^C pressed. Shutting down.\n");
  io_uring_queue_exit(&ring);
  exit(0);
}

int main(int argc, char *argv[]) {
  if (argc != 2) {
    fprintf(stderr, "usage: %s [PORT]\n", argv[0]);
    exit(EXIT_FAILURE);
  }
  const int port = atoi(argv[1]);

  int server_socket;
  struct sockaddr_in srv_addr;

  server_socket = socket(PF_INET, SOCK_STREAM, 0);
  if (server_socket == -1)
    fatal_error("socket()");

  int enable = 1;
  if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &enable,
                 sizeof(int)) < 0)
    fatal_error("setsockopt(SO_REUSEADDR)");

  memset(&srv_addr, 0, sizeof(srv_addr));
  srv_addr.sin_family = AF_INET;
  srv_addr.sin_port = htons(port);
  srv_addr.sin_addr.s_addr = htonl(INADDR_ANY);

  /* We bind to a port and turn this socket into a listening
   * socket.
   * */
  if (bind(server_socket, (const struct sockaddr *)&srv_addr,
           sizeof(srv_addr)) < 0)
    fatal_error("bind()");

  if (listen(server_socket, 10) < 0)
    fatal_error("listen()");

  signal(SIGINT, sigint_handler);
  io_uring_queue_init(4, &ring, 0);

  struct io_uring_cqe *cqe;
  struct sockaddr_in client_addr;
  socklen_t client_addr_len = sizeof(client_addr);

  add_accept_request(server_socket, &client_addr, &client_addr_len);

  while (1) {
    io_uring_wait_cqe(&ring, &cqe);
    struct request *req = (struct request *)cqe->user_data;

    if (cqe->res < 0) {
      printf("cqe->res = %d\n", cqe->res);
      io_uring_cqe_seen(&ring, cqe);
      continue;
    }

    switch (req->event_type) {
    case EVENT_TYPE_ACCEPT:
      add_accept_request(server_socket, &client_addr, &client_addr_len);
      add_read_request(cqe->res);
      free(req);
      break;
    case EVENT_TYPE_READ:
      if (!cqe->res) {
        // end of request
        fprintf(stderr, "Empty request!\n");
        close(req->client_socket);
        break;
      }

      // handling req
// #ifdef _DEBUG
      handle_client_request(req, cqe->res);
// #endif

      // get check more data!
      add_read_request(req->client_socket);

      free(req->iov[0].iov_base);
      free(req);
      break;
    case EVENT_TYPE_WRITE:
      for (int i = 0; i < req->iovec_count; i++) {
        free(req->iov[i].iov_base);
      }
      close(req->client_socket);
      free(req);
      break;
    }

    io_uring_cqe_seen(&ring, cqe);
  }

  return 0;
}