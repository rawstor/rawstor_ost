#include <sys/uio.h>

#include "xxhash.h"

XXH64_hash_t hash_buf(void *buf, size_t length);
XXH64_hash_t hash_vector(const struct iovec *iovecs, unsigned nr_vecs);
