#include "uuid.h"

#include <errno.h>
#include <stdint.h>


int rawstor_uuid_from_string(rawstor_uuid *uuid, const char *s) {
    const char *p = s;
    for (int i = 0; i < 32; i++) {
        char c = *p++;

        uint8_t x =
            (c >= '0' && c <= '9') ? c - '0'
            : (c >= 'a' && c <= 'f') ? 10 + c - 'a'
            : (c >= 'A' && c <= 'F') ? 10 + c - 'A'
            : 0xff;

        if (x == 0xff) {
            errno = EINVAL;
            return -errno;
        }

        if ((i & 1) == 0) {
            uuid->bytes[i >> 1] = x << 4;
        } else {
            uuid->bytes[i >> 1] |= x;
        }

        if ((i == 7 || i == 11 || i == 15 || i == 19) && (*p++ != '-')) {
            errno = EINVAL;
            return -errno;
        }
    }
    return 0;
}


void rawstor_uuid_to_string(const rawstor_uuid *uuid, rawstor_uuid_string *s) {
    static const char alphabet[] = "0123456789abcdef";
    char *p = *s;
    for (int i = 0; i < 16; ++i) {
      uint_fast8_t e = uuid->bytes[i];
      *p++ = alphabet[e >> 4];
      *p++ = alphabet[e & 0b00001111];
      if (i == 3 || i == 5 || i == 7 || i == 9) {
        *p++ = '-';
      }
    }
    *p = '\0';
}
