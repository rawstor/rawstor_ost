#include "ruring.h"
#include "log.h"
#include "ost.h"

#include <linux/mman.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

inline size_t buffer_size(struct ctx* ctx) {
    return 1U << ctx->buf_shift;
}

inline unsigned char* get_buffer(struct ctx* ctx, int idx) {
    return ctx->buffer_base + (idx << ctx->buf_shift);
}

/* Don't forget to run advance_recycled_buffers() at least once in a loop! */
void recycle_buffer(struct ctx* ctx, int idx) {
    io_uring_buf_ring_add(
        ctx->buf_ring, get_buffer(ctx, idx), buffer_size(ctx), idx,
        io_uring_buf_ring_mask(BUFFERS), ctx->recycled_buffers++
    );
}

inline void advance_recycled_buffers_and_cqes(struct ctx* ctx, int cnum) {
    if (ctx->recycled_buffers || cnum) {
        LOG_DEBUG(
            "advance_recycled_buffers_and_cqes: %i buffers %i cqes\n", ctx->recycled_buffers, cnum
        );
        __io_uring_buf_ring_cq_advance(ctx->ring, ctx->buf_ring, cnum, ctx->recycled_buffers);
        ctx->recycled_buffers = 0;
    }
}

int setup_buffer_pool(struct ctx* ctx) {
    int ret, i;
    void* mapped;
    struct io_uring_buf_reg reg = {
        .ring_addr = 0, .ring_entries = BUFFERS, .bgid = 0
    };

    ctx->buf_ring_size =
        (sizeof(struct io_uring_buf) + buffer_size(ctx)) * BUFFERS;
    mapped = mmap(
        NULL, ctx->buf_ring_size, PROT_READ | PROT_WRITE,
        MAP_ANONYMOUS | MAP_PRIVATE, 0, 0
    );
    if (mapped == MAP_FAILED) {
        fprintf(stderr, "buf_ring mmap: %s\n", strerror(errno));
        return -1;
    }
    ctx->buf_ring = (struct io_uring_buf_ring*)mapped;

    io_uring_buf_ring_init(ctx->buf_ring);

    reg = (struct io_uring_buf_reg){.ring_addr = (unsigned long)ctx->buf_ring,
                                    .ring_entries = BUFFERS,
                                    .bgid = 0};
    ctx->buffer_base =
        (unsigned char*)ctx->buf_ring + sizeof(struct io_uring_buf) * BUFFERS;

    ret = io_uring_register_buf_ring(ctx->ring, &reg, 0);
    if (ret) {
        fprintf(
            stderr,
            "buf_ring init failed: %s\n"
            "NB This requires a kernel version >= 6.0\n",
            strerror(-ret)
        );
        return ret;
    }

    for (i = 0; i < BUFFERS; i++) {
        io_uring_buf_ring_add(
            ctx->buf_ring, get_buffer(ctx, i), buffer_size(ctx), i,
            io_uring_buf_ring_mask(BUFFERS), i
        );
    }
    io_uring_buf_ring_advance(ctx->buf_ring, BUFFERS);

    return 0;
}

int setup_context(struct ctx* ctx) {
    struct io_uring_params params;
    int ret;

    ctx->bgid = 0;
    ctx->recycled_buffers = 0;

    memset(&params, 0, sizeof(params));
    params.cq_entries = CQES;
    params.flags = IORING_SETUP_SUBMIT_ALL | IORING_SETUP_DEFER_TASKRUN |
                   IORING_SETUP_CQSIZE | IORING_SETUP_SINGLE_ISSUER;

    ret = io_uring_queue_init_params(CQES, ctx->ring, &params);
    if (ret < 0) {
        fprintf(
            stderr,
            "queue_init failed: %s\n"
            "NB: This requires a kernel version >= 6.0\n",
            strerror(-ret)
        );
        return ret;
    }

    ret = setup_buffer_pool(ctx);
    if (ret)
        io_uring_queue_exit(ctx->ring);

    return ret;
}

/* While sq is not full - try to fill it */
inline struct io_uring_sqe* get_sqe(struct io_uring* ring) {
    struct io_uring_sqe* sqe;

    do {
        sqe = io_uring_get_sqe(ring);
        if (sqe)
            break;
        io_uring_submit(ring);
    } while (1);

    return sqe;
}

void cleanup_context(struct ctx* ctx) {
    munmap(ctx->buf_ring, ctx->buf_ring_size);
    io_uring_queue_exit(ctx->ring);
}

int add_recv(struct ctx* ctx, int idx, void* user_data) {
    struct io_uring_sqe* sqe = get_sqe(ctx->ring);

    io_uring_prep_recv_multishot(sqe, idx, NULL, 0, 0);

    sqe->flags |= IOSQE_BUFFER_SELECT;
    sqe->buf_group = ctx->bgid;
    io_uring_sqe_set_data(sqe, user_data);
    return 0;
}

inline int extract_cqe_buffer_idx(struct io_uring_cqe* cqe) {
    return cqe->flags >> IORING_CQE_BUFFER_SHIFT;
}

inline void* extract_cqe_recv_buf(struct ctx* ctx, struct io_uring_cqe* cqe) {
    int bid = extract_cqe_buffer_idx(cqe);

    LOG_DEBUG(
        "extract_cqe_recv_buf: bid=%d, res=%d, cflags=%x\n", bid, cqe->res,
        cqe->flags
    );

    return get_buffer(ctx, bid);
}
