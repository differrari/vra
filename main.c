#include "syscalls/syscalls.h"
#include "kbd_helper.h"
#include "math/math.h"
#include "math/vector.h"
#include "alloc/alloc.h"
#include "data/struct/linked_list.h"
#include "utils/theme.h"
#include "draw/pathdraw.h"
#include "debug/profiler.h"
#include "header_utils/infcanvas.h"

enum {
    action_drawing,
    action_panning,
} current_action;

draw_ctx ctx = { .width = 1920/2, .height = 1080/2 };

typedef struct {
    vec2 location;
    bool control;
} vector_point;

typedef struct {
    vec2 min, max;
    color outline_color;
    color fill_color;
    point_graph_slice slice;
    linked_list_t *shape;
    point_graph graph;
    buffer points_buffer;
} shape_t;

shape_t *current_shape;

theme_palette palette = {
    .background = 0xff888888,
    .foreground = 0xff555555,
    .accent = 0xffb4dd13,
    .error = 0xffdd2222
};

void recalc_shape_size(shape_t *shape){
    shape->min = (vec2){FLOAT_MAX,FLOAT_MAX};
    shape->max = (vec2){};
    for (linked_list_node_t *head = shape->shape->head; head; head = head->next){
        vector_point *p = head->data;
        vec2 loc = vector2_scale(p->location,zoom);
        if (loc.x < shape->min.x){ shape->min.x = loc.x; }
        if (loc.y < shape->min.y){ shape->min.y = loc.y; }
        if (loc.x > shape->max.x){ shape->max.x = loc.x; }
        if (loc.y > shape->max.y){ shape->max.y = loc.y; }
    }
}

void recalc_shape(shape_t *shape, vector_point *new_point){
    if (!shape->shape) return;
    bool recalc_all = !new_point;
    if (new_point){
        vec2 loc = vector2_scale(new_point->location,zoom);
        if (loc.x < shape->min.x){ recalc_all = true; shape->min.x = loc.x; }
        if (loc.y < shape->min.y){ recalc_all = true; shape->min.y = loc.y; }
        if (loc.x > shape->max.x){ recalc_all = false; shape->max.x = loc.x; }
        if (loc.y > shape->max.y){ recalc_all = false; shape->max.y = loc.y; }
    }
    size_t count = linked_list_count(shape->shape);
    if (!shape->points_buffer.buffer) shape->points_buffer = buffer_create(sizeof(point_entry) * count, buffer_can_grow);
    
    if (recalc_all){
        buffer_wipe(&shape->points_buffer);
        for (linked_list_node_t *head = shape->shape->head; head; head = head->next){
            vector_point *p = head->data;
            point_entry entry = {
                .pos = vector2_sub(vector2_scale(p->location, zoom), shape->min),
                .on_curve = !p->control,
            };
            buffer_write_lim(&shape->points_buffer, (const char*)&entry, sizeof(point_entry));
        }   
    } else {
        point_entry entry = {
            .pos = vector2_sub(vector2_scale(new_point->location, zoom), shape->min),
            .on_curve = !new_point->control,
        };
        buffer_write_lim(&shape->points_buffer, (const char*)&entry, sizeof(point_entry));
    }
    shape->slice.close = true;
    shape->slice.start = 0;
    shape->slice.end = count;
    shape->graph = (point_graph){
        .graph = shape->points_buffer.buffer,
        .num_points = count,
        .num_slices = 1,
        .slices = &shape->slice,
        .size = vector2_sub(shape->max, shape->min)
    };
    print("Recalc'd shape with new min %f,%f max %f,%f points: %i",shape->min.x,shape->min.y,shape->max.x,shape->max.y,shape->graph.num_points);
}

void append_point(shape_t *shape, int_point point, bool control){
    vector_point *p = new(vector_point);
    p->location = (vec2){point.x,point.y};
    p->control = control;
    if (!shape->shape){
        shape->shape = linked_list_create();
        shape->min = (vec2){ FLOAT_MAX, FLOAT_MAX };
        shape->max = (vec2){};
    }
    linked_list_push_front(shape->shape, p);
    recalc_shape(shape, p);
}

void render_shape(shape_t *shape){
    if (!shape->shape || !shape->graph.graph) return;
    const int size = 30;
    const int border_size = 3;
    const int inner_size = size-(border_size*2);
    fb_render_path(&ctx, (gpu_point){
       .x = shape->min.x + pan.x,
       .y = shape->min.y + pan.y,
    }, shape->graph, (path_render_options){
        .color = shape->fill_color,
        .outline_color = shape->outline_color,
        .scale = 1,
        .ignore_char = true,
        .type = path_render_outline | path_render_fill
    });
    for (linked_list_node_t *head = shape->shape->head; head; head = head->next){
        vector_point *p = head->data;
        vec2 loc = vector2_scale(p->location,zoom);
        fb_fill_rect(&ctx, pan.x+ (i32)loc.x-size/2, pan.y + (i32)loc.y-size/2, size, size, (palette.foreground-0x00222222) | 0xFF000000);
        fb_fill_rect(&ctx, pan.x+ (i32)loc.x-size/2 + border_size, pan.y + (i32)loc.y-size/2 + border_size, inner_size, inner_size, palette.accent);
    }
}

void on_zoom(float z){
    recalc_shape_size(current_shape);
    recalc_shape(current_shape, 0);
}

int main(int argc, char *argv[]){
    request_app_ctx(&ctx);
    
    bool clicked = false;
    
    current_shape = new(shape_t);
    current_shape->fill_color = 0xff732982;
    current_shape->outline_color = 0xff000000;
    
    profiler_init();
    
    on_canvas_zoom = on_zoom;
    
    while (!should_close_ctx()){
        
        u64 dtms = profiler_delta();
        
        canvas_background(&ctx, palette.background, palette.foreground);
        
        if (current_shape){
            render_shape(current_shape);
        }
        
        kbd_event ev = {};
        if (read_event(&ev)){
            if (!handle_modifier(&ev)){
                if (ev.type == KEY_PRESS){
                    if (is_mod_pressed(KEY_LEFTALT, true) && ev.key == KEY_ESC) halt(0);
                }
            }
        }
        
        mouse_data mouse = {};
        get_mouse_status(&mouse);
        
        canvas_mouse(mouse, 1.f/dtms);
        
        if (mouse_button_down(&mouse, LMB)){
            if (!clicked){
                clicked = true;
                append_point(current_shape,mouse.position, false);
            }
        } else clicked = false;
        
        commit_draw_ctx(&ctx);
    }
    
    return 0;
}