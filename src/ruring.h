#ifndef RAWSTOR_RURING_H
#define RAWSTOR_RURING_H

#include <liburing.h>

#include <arpa/inet.h>

#include <sys/mman.h>
#include <sys/uio.h>

#include <stdbool.h>
#include <stdint.h>

#define QD 64
#define CQES (QD * 4)
#define BUFFERS CQES

#define BUF_SHIFT 17 /* 128K */
#define CONTROLLEN 0

struct ctx {
    struct io_uring* ring;
    struct io_uring_buf_ring* buf_ring;
    int bgid;
    unsigned char* buffer_base;
    int buf_shift;
    int af;
    size_t buf_ring_size;
    uint16_t recycled_buffers;
};

size_t buffer_size(struct ctx* ctx);
unsigned char* get_buffer(struct ctx* ctx, int idx);
void recycle_buffer(struct ctx* ctx, int idx);
void advance_recycled_buffers_and_cqes(struct ctx* ctx, int cnum);

int setup_buffer_pool(struct ctx* ctx);
int setup_context(struct ctx* ctx);
void cleanup_context(struct ctx* ctx);
struct io_uring_sqe* get_sqe(struct io_uring* ring);
int add_recv(struct ctx* ctx, int idx, void* user_data);

int extract_cqe_buffer_idx(struct io_uring_cqe* cqe);
void* extract_cqe_recv_buf(struct ctx* ctx, struct io_uring_cqe* cqe);

#endif // RAWSTOR_RURING_H
