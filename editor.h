#pragma once

#include "vra.h"
#define INFCANVAS_NONSTATIC
#include "header_utils/infcanvas.h"

extern u64 point_count;
extern shape_t *current_shape;

extern color current_shape_color;
extern color current_shape_outline;

shape_t* new_shape();

void join_contours();

void append_point(shape_t *shape, int_point point, bool control);
void move_point(shape_t *shape, linked_list_node_t *point_node, gpu_point new_pos);
void delete_point(shape_t *shape, linked_list_node_t *point_node);
void recalc_all();
shape_t* new_shape();
void join_contours();
void recalc_shape_size(shape_t *shape);
void recalc_slice(shape_t *shape,int start, int count);
void recalc_shape(shape_t *shape, vector_point *new_point);