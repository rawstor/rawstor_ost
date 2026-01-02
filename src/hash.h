#include <xxhash.h>

#include <sys/uio.h>

inline XXH64_hash_t hash_buf(void* buf, size_t length) {
    return XXH3_64bits(buf, length);
}

XXH64_hash_t hash_vector(const struct iovec* iovecs, unsigned nr_vecs);
