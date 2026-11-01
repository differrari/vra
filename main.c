#include "syscalls/syscalls.h"
#include "vra.h"
#include "kbd_helper.h"
#include "math/math.h"
#include "math/vector.h"
#include "alloc/alloc.h"
#include "data/struct/linked_list.h"
#include "utils/theme.h"
#include "draw/pathdraw.h"
#include "debug/profiler.h"
#include "draw/color.h"
#include "math/aabb2.h"
#include "uno.h"
#include "files/helpers.h"
#define MOG_MODULE "VRA"
#define MOG_LEVEL MOG_WARNING
#include "utils/mog.h"
#include "select.h"
#include "ui.h"
#include "export.h"
#include "editor.h"

enum {
    action_drawing,
    action_panning,
} current_action;

bool snap_on = false, show_handle = true;

draw_ctx ctx = { .width = 1920/2, .height = 1080/2 };

theme_palette palette = {
    .background = 0xff888888,
    .foreground = 0xff555555,
    .accent = 0xffb4dd13,
    .error = 0xffdd2222
};

bool hide_handles = false;

linked_list_t *shape_list;

shape_t *current_shape;

const int handle_size = 30;
const int border_size = 3;
const int inner_size = handle_size-(border_size*2);

void render_shape(shape_t *shape){
    if (!shape->shape || !shape->render.graph.graph || (shape->contour_of && !is_selected(shape))) return;
    fb_render_path(&ctx, (gpu_point){
       .x = shape->min.x + pan.x,
       .y = shape->min.y + pan.y,
    }, shape->render.graph, (path_render_options){
        .color = shape->render.fill_color,
        .outline_color = is_selected (shape) ? palette.accent : shape->render.outline_color,
        .scale = 1,
        .sizing_policy = path_render_sizing_none,
        .type = path_render_outline | path_render_fill
    });
    for (linked_list_node_t *head = shape->shape->head; head; head = head->next){
        vector_point *p = head->data;
        if (!p) continue;
        vec2 loc = vector2_scale(p->location,zoom);
        if (!hide_handles) fb_fill_rect(&ctx, pan.x+ (i32)loc.x-handle_size/2, pan.y + (i32)loc.y-handle_size/2, handle_size, handle_size, (palette.foreground-0x00222222) | 0xFF000000);
        if (!hide_handles) fb_fill_rect(&ctx, pan.x+ (i32)loc.x-handle_size/2 + border_size, pan.y + (i32)loc.y-handle_size/2 + border_size, inner_size, inner_size, p->control ? palette.error : palette.accent);
    }
    if (!hide_handles && is_selected(shape)) fb_fill_rect(&ctx, pan.x+ (i32)shape->render.middle.x-handle_size/2, pan.y + (i32)shape->render.middle.y-handle_size/2, handle_size, handle_size, color_mix(palette.error,palette.accent) | 0xFF000000);
}

void on_zoom(float z){
    if (!shape_list) return;
    recalc_all();
}

void prototype_color_pciker(){
    float hue = 20;
    for (int y = 0; y < 255; y++){
        for (int x = 0; x < 255; x++){
            fb_draw_pixel(&ctx, x, y, ahsv_to_argb(argb_to_ahsv(ahsv_to_argb((ahsvcolor){
                .alpha = 1,
                .hue = hue,
                .saturation = x/255.f,
                .value = y/255.f
            }))).color);
        }
    }
}

bool find_point_handle(gpu_point p, shape_t **out_shape, linked_list_node_t **out_node){
    all_shapes({
        if (!shape->shape) continue;
        if (shape->contour_of && !is_selected(shape)) continue;
        vec2 v = ((vec2){p.x,p.y});
        for (linked_list_node_t *head = shape->shape->head; head; head = head->next){
            vector_point *p = head->data;
            if (!p) continue;
            aabb2 ab = ((aabb2){.min = vector2_sub(p->location, (vector2){handle_size/2.f, handle_size/2.f}), .max = vector2_add(p->location, (vector2){handle_size/2.f, handle_size/2.f})});
            if (aabb_point_intersect(ab, v)) {
                if (out_shape) *out_shape = shape;
                if (out_node) *out_node = head;
                return true;
            }
        }
    })
    return false;
}

bool clicked = false;
shape_t *out_shape = 0;
linked_list_node_t *out_node = 0;

void mouse_handle(mouse_data mouse, u64 dtms){
    
    canvas_mouse(mouse, 1.f/dtms);
    
    bool is_mod = is_mod_pressed(KEY_LEFTCTRL, true);
    
    gpu_point loc = mouse.position;
    loc.x -= pan.x;
    loc.y -= pan.y;
    loc.x /= zoom;
    loc.y /= zoom;
    gpu_point non_snap = loc;
    if (is_mod_pressed(KEY_LEFTALT, true) || snap_on){
        loc.x = round_to_int((float)loc.x/grid_size) * grid_size;
        loc.y = round_to_int((float)loc.y/grid_size) * grid_size;
    }
    
    if (mouse_button_down(&mouse, LMB)){
        if (!clicked){
            clicked = true;
            find_point_handle(non_snap, &out_shape, &out_node);
            if (is_mod && out_shape && out_node){
                delete_point(out_shape,out_node);
                out_shape = 0;
                out_node = 0;
            } else if (!out_shape || !out_node){
                if (!current_shape){
                    if (shape_list->tail)
                        current_shape = shape_list->tail->data;
                    if (!current_shape){
                        new_shape();
                    }
                }
                append_point(current_shape,loc, is_mod);
            }
        }
    } else {
        clicked = false;
        out_shape = 0;
        out_node = 0;
    }

    if (clicked && !is_mod && out_shape && out_node)
        move_point(out_shape,out_node,loc);
}

int main(int argc, char *argv[]){
    request_app_ctx(&ctx);
    
    shape_list = linked_list_create();
    selected_list = linked_list_create();
    
    profiler_init();
    
    on_canvas_zoom = on_zoom;
    
    uno_set_document_view(ui_builder, draw_ctx_rect(&ctx));
    
    uno_clear_color(0);
    
    uno_redraw_always();
    
    while (!should_close_ctx()){
        
        u64 dtms = profiler_delta();
        
        canvas_background(&ctx, palette.background, palette.foreground);
        
        // prototype_color_pciker();
        
        all_shapes({
            render_shape(shape);
        })
        
        kbd_event ev = {};
        if (read_event(&ev)){
            if (!handle_modifier(&ev)){
                if (ev.type == KEY_PRESS){
                    if (is_mod_pressed(KEY_LEFTALT, true) && ev.key == KEY_ESC) halt(0);
                    if (ev.key == KEY_N){
                        if (is_mod_pressed(KEY_LEFTALT, true))
                            new_contour();
                        else 
                            new_shape();
                    } 
                    if (ev.key == KEY_ENTER) serialize();
                    if (ev.key == KEY_J) join_contours();
                    if (ev.key == KEY_I) invert();
                }
                hide_handles = ev.key == KEY_U && ev.type != KEY_RELEASE || !show_handle;
            }
        }
        
        mouse_data mouse = {};
        get_mouse_status(&mouse);
        if (!uno_dispatch_mouse(mouse, current_modifier))
            mouse_handle(mouse, dtms);
        
        uno_draw(&ctx);
        
        commit_draw_ctx(&ctx);
    }
    
    return 0;
}