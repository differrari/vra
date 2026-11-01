#include "select.h"

linked_list_t *selected_list;

linked_list_node_t* is_selected(shape_t *s){
    if (!selected_list) return 0;
    selected_shapes({
        if (shape == s) return head;
    })
    return 0;
}

void unselect_all(){
    while (selected_list->head) linked_list_remove(selected_list, selected_list->head);
}