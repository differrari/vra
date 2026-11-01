#pragma once

#include "vra.h"
#include "vro.h"
#include "files/buffer.h"

void serialize();
extern linked_list_t *export_canvases;

void ensure_default_canvas();

#define all_canvases(action) for (linked_list_node_t *all_canvases_node = export_canvases->head; all_canvases_node; all_canvases_node = all_canvases_node->next) { aabb2 *canvas = all_canvases_node->data; if (!canvas) continue; action }