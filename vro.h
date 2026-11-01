#pragma once

#include "types.h"

#define MAGIC_NUM 0xEC70

typedef struct {
    u16 magic;
    u8 rsvd[6];
    u64 hdr_version;
    u64 num_shapes;
}__attribute__((packed)) vro_header;