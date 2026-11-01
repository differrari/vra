#pragma once

#include "data/struct/linked_list.h"
#include "vra.h"

extern linked_list_t *selected_list;
#define selected_shapes(action) for (linked_list_node_t *head = selected_list->head; head; head = head->next) { shape_t *shape = head->data; if (!shape) continue; action }

linked_list_node_t* is_selected(shape_t *s);

void unselect_all();