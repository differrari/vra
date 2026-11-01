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
#include "draw/color.h"
#include "math/aabb2.h"
#include "uno.h"
#include "files/helpers.h"
#define MOG_MODULE "VRA"
#define MOG_LEVEL MOG_WARNING
#include "utils/mog.h"

#define MAGIC_NUM 0xEC70

typedef struct {
    u16 magic;
    u8 rsvd[6];
    u64 hdr_version;
    u64 num_shapes;
}__attribute__((packed)) vro_header;

enum {
    action_drawing,
    action_panning,
} current_action;

enum {
    ui_tag_invalid,
    ui_tag_newshape,
    ui_tag_shape
} ui_tags;

draw_ctx ctx = { .width = 1920/2, .height = 1080/2 };

typedef struct {
    vec2 location;
    bool control;
} vector_point;

typedef struct {
    color outline_color;
    color fill_color;
    point_graph graph;
} shape_render_info;

typedef struct shape_t{
    vec2 min, max;
    shape_render_info render;
    linked_list_t *shape;
    buffer points_buffer;
    buffer slice_buffer;
    int current_slice;
    struct shape_t *contour_of;
    struct shape_t *link;
} shape_t;

theme_palette palette = {
    .background = 0xff888888,
    .foreground = 0xff555555,
    .accent = 0xffb4dd13,
    .error = 0xffdd2222
};

u64 point_count;

linked_list_t *shape_list;
#define all_shapes(action) for (linked_list_node_t *head = shape_list->head; head; head = head->next) { shape_t *shape = head->data; if (!shape) continue; action }
linked_list_t *selected_list;
#define selected_shapes(action) for (linked_list_node_t *head = selected_list->head; head; head = head->next) { shape_t *shape = head->data; if (!shape) continue; action }

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

buffer serialize(){
    size_t shape_count = linked_list_count(shape_list);
    buffer buf = buffer_create(sizeof(vro_header) + (sizeof(shape_t)*shape_count) + (point_count*sizeof(point_entry)), buffer_can_grow);
    
    vro_header hdr = {
        .hdr_version = VERSION_NUM(0, 1, 0, 0),
        .magic = MAGIC_NUM,
        .num_shapes = shape_count
    };
    buffer_write_lim(&buf, (char*)&hdr, sizeof(vro_header));
    
    all_shapes({
        shape_render_info info = shape->render;
        info.graph.graph = (void*)(buf.cursor + sizeof(shape_render_info) + (info.graph.num_slices * sizeof(point_graph_slice)));
        info.graph.slices = (void*)(buf.cursor + sizeof(shape_render_info));
        mog(VERBOSE,"Graph at %x slices at %x",info.graph.graph,info.graph.slices);
        buffer_write_lim(&buf, (char*)&info, sizeof(shape_render_info));
        for (u64 i = 0; i < shape->render.graph.num_slices; i++){
            buffer_write_lim(&buf, (char*)&shape->render.graph.slices[i], sizeof(point_graph_slice));
        }
        for (u64 i = 0; i < shape->render.graph.num_points; i++){
            buffer_write_lim(&buf, (char*)&shape->render.graph.graph[i], sizeof(point_entry));
        }
    });
    
    write_full_file("/home/di/playground/hey.vro", buf.buffer, buf.buffer_size);
    mog(VERBOSE,"Wrote %i vros",shape_count);
    
    return buf;
    
}

shape_t *current_shape;

color current_shape_color = 0xff732982;
color current_shape_outline = 0xff000000;

void recalc_shape_size(shape_t *shape){
    if (!shape || !shape->shape) return;
    if (shape->contour_of) recalc_shape_size(shape->contour_of);
    shape->min = (vec2){FLOAT_MAX,FLOAT_MAX};
    shape->max = (vec2){};
    shape_t *cur_shape = shape;
    while (cur_shape){
        for (linked_list_node_t *head = cur_shape->shape->head; head; head = head->next){
            vector_point *p = head->data;
            if (!p) continue;
            vec2 loc = vector2_scale(p->location,zoom);
            if (loc.x < shape->min.x){ shape->min.x = loc.x; }
            if (loc.y < shape->min.y){ shape->min.y = loc.y; }
            if (loc.x > shape->max.x){ shape->max.x = loc.x; }
            if (loc.y > shape->max.y){ shape->max.y = loc.y; }
        }
        cur_shape = cur_shape->link;
    }
}

void recalc_slice(shape_t *shape,int start, int count){
    point_graph_slice new_slice = {
       .close = true,
       .start = start,
       .count = count,
       .curve_type = point_graph_curve_quad_only,
       .max_curve_points = 1,
    };
    if (!shape->render.graph.num_slices){
        shape->slice_buffer = buffer_create(sizeof(point_graph_slice), buffer_can_grow);
        buffer_write_lim(&shape->slice_buffer, (char*)&new_slice, sizeof(point_graph_slice));
        shape->current_slice = 0;
    } else {
        if (shape->slice_buffer.cursor <= shape->current_slice * sizeof(point_graph_slice)) {
            buffer_write_lim(&shape->slice_buffer, (char*)&new_slice, sizeof(point_graph_slice));
        } else 
            ((point_graph_slice*)shape->slice_buffer.buffer)[shape->current_slice] = new_slice;
    }
    shape->render.graph.num_slices = shape->current_slice+1;
}

void recalc_shape(shape_t *shape, vector_point *new_point){
    if (!shape->shape) return;
    if (shape->contour_of) recalc_shape(shape->contour_of, 0);
    bool recalc_all = !new_point;
    if (new_point){
        vec2 loc = vector2_scale(new_point->location,zoom);
        if (loc.x < shape->min.x){ recalc_all = true; shape->min.x = loc.x; }
        if (loc.y < shape->min.y){ recalc_all = true; shape->min.y = loc.y; }
        if (loc.x > shape->max.x){ shape->max.x = loc.x; }
        if (loc.y > shape->max.y){ shape->max.y = loc.y; }
    }
    if (shape->link) recalc_all = true;
    size_t count = 0;
    shape_t *cur_shape = shape;
    while (cur_shape){
        count += linked_list_count(cur_shape->shape);
        cur_shape = cur_shape->link;
    }
    if (!shape->points_buffer.buffer) shape->points_buffer = buffer_create(sizeof(point_entry) * count, buffer_can_grow);
    
    if (recalc_all){
        buffer_wipe(&shape->points_buffer);
        cur_shape = shape;
        shape->current_slice = 0;
        int total = 0;
        while (cur_shape){
            int c = 0;
            for (linked_list_node_t *head = cur_shape->shape->head; head; head = head->next){
                vector_point *p = head->data;
                if (!p) continue;
                point_entry entry = {
                    .pos = vector2_sub(vector2_scale(p->location, zoom), shape->min),
                    .on_curve = !p->control,
                };
                buffer_write_lim(&shape->points_buffer, (const char*)&entry, sizeof(point_entry));
                c++;
            }
            recalc_slice(shape, total, total+c);
            cur_shape = cur_shape->link;
            if (cur_shape) shape->current_slice++;
            total += c;
        }
        if (total != count){
            mog(WARNING,"Mismatch in size, expected %i, found %i",count,total);
        }
    } else {
        point_entry entry = {
            .pos = vector2_sub(vector2_scale(new_point->location, zoom), shape->min),
            .on_curve = !new_point->control,
        };
        buffer_write_lim(&shape->points_buffer, (const char*)&entry, sizeof(point_entry));
        recalc_slice(shape, shape->render.graph.slices[shape->current_slice].start, shape->render.graph.slices[shape->current_slice].count+1);
    }
    
    shape->render.graph = (point_graph){
        .graph = shape->points_buffer.buffer,
        .num_points = count,
        .slices = shape->slice_buffer.buffer,
        .num_slices = shape->render.graph.num_slices,
        .size = vector2_sub(shape->max, shape->min)
    };
    mog(INFO,"Recalc'd shape with new min %f,%f max %f,%f points: %i slices: %i",shape->min.x,shape->min.y,shape->max.x,shape->max.y,shape->render.graph.num_points,shape->render.graph.num_slices);
    if (shape->render.graph.num_slices > 1){
        for (int i = 0; i < shape->render.graph.num_points; i++){
            point_entry p = shape->render.graph.graph[i];
            mog(VERBOSE,"Point %f,%f",p.pos.x,p.pos.y);
        }
        for (int i = 0; i < shape->render.graph.num_slices; i++){
            point_graph_slice sl = shape->render.graph.slices[i];
            mog(VERBOSE,"Slice %i from %i to %i",i,sl.start,sl.count);
        }
    }
}

void append_point(shape_t *shape, int_point point, bool control){
    if (shape->contour_of && !is_selected(shape)) return;
    vector_point *p = new(vector_point);
    p->location = (vec2){point.x,point.y};
    p->control = control;
    if (!shape->shape){
        shape->shape = linked_list_create();
        shape->min = (vec2){ FLOAT_MAX, FLOAT_MAX };
        shape->max = (vec2){};
    }
    linked_list_push(shape->shape, p);
    recalc_shape(shape, p);
    point_count++;
}

void move_point(shape_t *shape, linked_list_node_t *point_node, gpu_point new_pos){
    if (!shape || !shape->shape || !point_node) return;
    if (shape->contour_of && !is_selected(shape)) return;
    vector_point *p = point_node->data;
    if (!p) return;
    p->location = (vec2){ new_pos.x, new_pos.y };
    recalc_shape_size(shape);
    recalc_shape(shape, 0);
}

void delete_point(shape_t *shape, linked_list_node_t *point_node){
    if (!shape || !shape->shape || !point_node) return;
    if (shape->contour_of && !is_selected(shape)) return;
    linked_list_remove(shape->shape, point_node);
    recalc_shape_size(shape);
    recalc_shape(shape, 0);
    point_count--;
}

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
        fb_fill_rect(&ctx, pan.x+ (i32)loc.x-handle_size/2, pan.y + (i32)loc.y-handle_size/2, handle_size, handle_size, (palette.foreground-0x00222222) | 0xFF000000);
        fb_fill_rect(&ctx, pan.x+ (i32)loc.x-handle_size/2 + border_size, pan.y + (i32)loc.y-handle_size/2 + border_size, inner_size, inner_size, p->control ? palette.error : palette.accent);
    }
}

void recalc_all(){
    all_shapes({
        recalc_shape_size(shape);
        recalc_shape(shape, 0);
    })
}

void on_zoom(float z){
    if (!shape_list) return;
    recalc_all();
}

shape_t* new_shape(){
    shape_t *shape = new(shape_t);
    shape->render.fill_color = current_shape_color;
    shape->render.outline_color = current_shape_outline;
    linked_list_push(shape_list, shape);
    uno_refresh();
    return shape;
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

void new_shape_pressed(int tag, gpu_point point){
    current_shape = new_shape();
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

button_info new_shape_bi = {
    .press = new_shape_pressed
};

button_info select_shape_bi = {
    .press = shape_select_toggle
};

void *ui_strings_page;

void ui_builder(){
    if (ui_strings_page) release_page_all(ui_strings_page);
    ui_strings_page = page_alloc(PAGE_SIZE);
    HORIZONTAL((node_info){ .sizing_rule = size_fill}, {
        SPACER
        VERTICAL(((node_info){ .sizing_rule = size_relative, .percentage = 0.2f, .bg_color = 0xbb4382 }), {
            int count = 0;
            all_shapes({
                char *s = allocate(ui_strings_page,256,page_alloc);
                size_t n = string_format_buf(s, 256, "%sShape %i", is_selected(shape) ? "[S]" : "", count);
                uno_button(ui_tag_shape + count, (node_info){ .sizing_rule = size_fit, .fg_color = 0xff333333, }, &select_shape_bi, (string_slice){s,n});
                count++;
            });
            SPACER
            uno_button(ui_tag_newshape, (node_info){ .sizing_rule = size_fit, .fg_color = 0xff333333, }, &new_shape_bi, SLICE("New shape"));
        });
    });
}

bool clicked = false;
shape_t *out_shape = 0;
linked_list_node_t *out_node = 0;

void mouse_handle(mouse_data mouse, u64 dtms){
    
    canvas_mouse(mouse, 1.f/dtms);
    
    bool is_mod = is_mod_pressed(KEY_LEFTCTRL, true);
    
    gpu_point loc = mouse.position;
    if (is_mod_pressed(KEY_LEFTALT, true)){
        loc.x = round_to_int((float)loc.x/grid_size) * grid_size;
        loc.y = round_to_int((float)loc.y/grid_size) * grid_size;
    }
    
    if (mouse_button_down(&mouse, LMB)){
        if (!clicked){
            clicked = true;
            find_point_handle(loc, &out_shape, &out_node);
            if (is_mod && out_shape && out_node){
                delete_point(out_shape,out_node);
                out_shape = 0;
                out_node = 0;
            } else if (!out_shape || !out_node){
                if (!current_shape){
                    if (shape_list->tail)
                        current_shape = shape_list->tail->data;
                    if (!current_shape){
                        current_shape = new_shape();
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

void join_contours(){
    shape_t *latest = 0;
    selected_shapes({
        if (latest) {
            shape->contour_of = latest;
            latest->link = shape;
        } else shape->contour_of = 0;
        latest = shape;
    });
    mog(INFO,"Joined %i contours",linked_list_count(selected_list));
    unselect_all();
    recalc_all();
    uno_refresh();
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
                    if (ev.key == KEY_TAB) current_shape = new_shape();
                    if (ev.key == KEY_ENTER) serialize();
                    if (ev.key == KEY_J) join_contours();
                }
            }
        }
        
        mouse_data mouse = {};
        get_mouse_status(&mouse);
        if (!uno_dispatch_mouse(mouse, current_modifier)){
            mouse_handle(mouse, dtms);
        }
        
        uno_draw(&ctx);
        
        commit_draw_ctx(&ctx);
    }
    
    return 0;
}