#pragma once

#include "vra.h"
#include "vro.h"
#include "files/buffer.h"

void serialize();
extern linked_list_t *export_canvases;
extern bool manual_canvases;

void ensure_default_canvas();
void enable_manual_canvas();
void register_new_canvas(gpu_point start, gpu_point end);

#define all_canvases(action) for (linked_list_node_t *all_canvases_node = export_canvases->head; all_canvases_node; all_canvases_node = all_canvases_node->next) { aabb2 *canvas = all_canvases_node->data; if (!canvas) continue; action }