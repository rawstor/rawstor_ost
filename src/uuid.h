#ifndef RAWSTOR_UUID_H
#define RAWSTOR_UUID_H

#include "ost.h"

#include <stdint.h>


// defined in ost.h
// typedef struct {
//     uint8_t bytes[16];
// } rawstor_uuid;

// defined in ost.h
// typedef char rawstor_uuid_string[37];


int rawstor_uuid_from_string(rawstor_uuid *uuid, const char *s);

void rawstor_uuid_to_string(const rawstor_uuid *uuid, rawstor_uuid_string *s);


#endif // RAWSTOR_UUID_H
