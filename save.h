#pragma once

#include "string/string.h"

typedef struct {
    u16 magic;
    u8 rsvd[6];
    u64 hdr_version;
    u64 num_shapes;
    u64 num_canvases;
} vra_save_hdr;

#define VRA_MAGIC_NUM 0xEC71

extern string file_location;
extern string file_name;

void save_file();
void restore_file(char *path);