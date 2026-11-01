#pragma once

#include "graphic_types.h"
#include "draw/point_graph.h"
#include "data/struct/linked_list.h"
#include "files/buffer.h"

typedef struct {
    color outline_color;
    color fill_color;
    point_graph graph;
    vec2 middle;
} shape_render_info;

typedef struct shape_t{
    vec2 min, max;
    shape_render_info render;
    linked_list_t *shape;
    buffer points_buffer;
    buffer slice_buffer;
    int current_slice;
    bool counter_clockwise;
    struct shape_t *contour_of;
    struct shape_t *link;
} shape_t;

typedef struct {
    vec2 location;
    bool control;
} vector_point;

extern linked_list_t *shape_list;
#define all_shapes(action) for (linked_list_node_t *all_shapes_head = shape_list->head; all_shapes_head; all_shapes_head = all_shapes_head->next) { shape_t *shape = all_shapes_head->data; if (!shape) continue; action }