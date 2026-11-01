#include "ui.h"
#include "select.h"
#include "uno.h"
#include "export.h"
#include "syscalls/syscalls.h"
#include "behavior.h"
#include "editor.h"

enum {
    ui_tag_invalid,
    ui_tag_newshape,
    ui_tag_join,
    ui_tag_snap,
    ui_tag_export,
    ui_tag_show_handle,
    //NOTE: Leave the following tag last as it is an indexed tag (tag = this + index)
    ui_tag_shape
} ui_tags;

void new_shape_pressed(int tag, gpu_point point){
    new_shape();
}

void shape_select_toggle(int tag, gpu_point point){
    tag -= ui_tag_shape;
    shape_t *s = linked_list_get(shape_list, tag);
    if (!s) return;
    linked_list_node_t *sel = is_selected(s);
    if (sel)
        linked_list_remove(selected_list, sel);
    else {
        linked_list_push(selected_list, s);
        current_shape = s;
    }
    recalc_all();
    uno_refresh();
}

void toolbar_press(int tag, gpu_point p){
    switch (tag){
        case ui_tag_newshape: new_shape(); break;
        case ui_tag_join: join_contours(); break;
        case ui_tag_snap: snap_on = !snap_on; break;
        case ui_tag_export: serialize(); break;
        case ui_tag_show_handle: show_handle = !show_handle; hide_handles = !show_handle; break;
        default: return;
    }
    uno_refresh();
}

button_info select_shape_bi = {
    .press = shape_select_toggle
};

button_info core_actions_bi = {
    .press = toolbar_press
};

void *ui_strings_page;

void ui_builder(){
    if (ui_strings_page) release_page_all(ui_strings_page);
    ui_strings_page = page_alloc(PAGE_SIZE);
    HORIZONTAL((node_info){ .sizing_rule = size_fill}, {
        VERTICAL(((node_info){.sizing_rule = size_fill}), {
            HORIZONTAL(((node_info){ .padding = 30, .bg_color = 0xff676767 }), {
                uno_button(ui_tag_newshape,((node_info){ .padding = 5 }), &core_actions_bi, SLICE("New shape"));
                SPACER
                uno_button(ui_tag_join,((node_info){ .padding = 5 }), &core_actions_bi, SLICE("Join"));
                uno_label(((node_info){ .padding = 5 }), doc_text_body, SLICE("|"));
                SPACER
                uno_button(ui_tag_snap,((node_info){ .padding = 5 }), &core_actions_bi, SLICE(snap_on ? "Free place" : "Snap to grid"));
                uno_button(ui_tag_show_handle,((node_info){ .padding = 5 }), &core_actions_bi, SLICE(show_handle ? "Hide handles" : "Show handles"));
                uno_button(ui_tag_export,((node_info){ .padding = 5 }), &core_actions_bi, SLICE("Export"));
            });
            SPACER
        });
        SPACER
        VERTICAL(((node_info){ .sizing_rule = size_relative, .percentage = 0.2f, .bg_color = 0xff767676 }), {
            int count = 0;
            all_shapes({
                char *s = allocate(ui_strings_page,256,page_alloc);
                buffer b = ((buffer){.buffer = s, .buffer_size = 0, .limit = 256});
                buffer_write_const(&b,"[");
                if (is_selected(shape)) buffer_write_const(&b, "S");
                if (shape->contour_of) buffer_write_const(&b, "c");
                else if (shape->link) buffer_write_const(&b, "C");
                buffer_write_const(&b, shape->counter_clockwise ? "<" : ">");
                buffer_write(&b,"] Shape %i",count);
                uno_button(ui_tag_shape + count, (node_info){ .sizing_rule = size_fit, .fg_color = 0xff333333, }, &select_shape_bi, (string_slice){s,b.buffer_size});
                count++;
            });
            SPACER
            uno_button(ui_tag_newshape, (node_info){ .sizing_rule = size_fit, .fg_color = 0xff333333, }, &core_actions_bi, SLICE("New shape"));
        });
    });
}