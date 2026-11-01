#include "editor.h"
#include "select.h"
#include "alloc/alloc.h"
#define MOG_MODULE "VRA_EDITOR"
#define MOG_LEVEL MOG_WARNING
#include "utils/mog.h"
#include "uno.h"
#include "math/vector.h"

editor_layers editor_layer = editor_layer_shapes;

float zoom = 1;
float max_zoom = 10;
float min_zoom = 0.1f;
float zoom_speed = 3;
gpu_point pan = {};
int grid_size = 128;

int max_grid = 1024;
int min_grid = 1;

void (*on_canvas_pan)(gpu_point point);
void (*on_canvas_zoom)(float zoom);

u64 point_count = 0;

color current_shape_color = 0xff732982;
color current_shape_outline = 0xff000000;

bool points_clockwise(vector_point *a, vector_point *b, shape_t *s){
    vec2 center = s->render.middle;
    if (a->location.x - center.x >= 0 && b->location.x - center.x < 0)
        return true;
    if (a->location.x - center.x < 0 && b->location.x - center.x >= 0)
        return false;
    if (a->location.x - center.x == 0 && b->location.x - center.x == 0) {
        if (a->location.y - center.y >= 0 || b->location.y - center.y >= 0)
            return a->location.y > b->location.y;
        return b->location.y > a->location.y;
    }

    int det = (a->location.x - center.x) * (b->location.y - center.y) - (b->location.x - center.x) * (a->location.y - center.y);
    if (det < 0)
        return true;
    if (det > 0)
        return false;

    int d1 = (a->location.x - center.x) * (a->location.x - center.x) + (a->location.y - center.y) * (a->location.y - center.y);
    int d2 = (b->location.x - center.x) * (b->location.x - center.x) + (b->location.y - center.y) * (b->location.y - center.y);
    return d1 > d2;
}

bool shape_sort(void *a, void *b, void *ctx){
    if (!ctx || !a || !b) return false;
    shape_t *s = ctx;
    vector_point *ap = a;
    vector_point *bp = b;
    return s->counter_clockwise ^ points_clockwise(ap, bp, s);
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
    bool need_reorder = !shape_sort(shape->shape->tail, p, shape);
    linked_list_push(shape->shape, p);
    // if (need_reorder){
        linked_list_sort(shape->shape, shape, shape_sort);
        recalc_shape_size(shape);
        recalc_shape(shape, 0);
    // } else recalc_shape(shape, p);
    point_count++;
}
void move_point(shape_t *shape, linked_list_node_t *point_node, gpu_point new_pos){
    if (!shape || !shape->shape || !point_node) return;
    if (current_shape != shape && !is_selected(shape)) return;
    vector_point *p = point_node->data;
    if (!p) return;
    p->location = (vec2){ new_pos.x, new_pos.y };
    recalc_shape_size(shape);
    recalc_shape(shape, 0);
}

void delete_point(shape_t *shape, linked_list_node_t *point_node){
    if (!shape || !shape->shape || !point_node) return;
    if (current_shape != shape && !is_selected(shape)) return;
    linked_list_remove(shape->shape, point_node);
    recalc_shape_size(shape);
    recalc_shape(shape, 0);
    point_count--;
}
void recalc_all(){
    all_shapes({
        recalc_shape_size(shape);
        recalc_shape(shape, 0);
    })
}

shape_t* new_shape(){
    shape_t *shape = new(shape_t);
    shape->render.fill_color = current_shape_color;
    shape->render.outline_color = current_shape_outline;
    linked_list_push(shape_list, shape);
    current_shape = shape;
    linked_list_clear(selected_list);
    linked_list_push(selected_list, shape);
    uno_refresh();
    return shape;
}

shape_t *new_contour(){
    if (!current_shape) return 0;
    shape_t *shape = new(shape_t);
    shape->render.fill_color = current_shape_color;
    shape->render.outline_color = current_shape_outline;
    linked_list_push(shape_list, shape);
    shape_t *parent = current_shape;
    while (parent->contour_of) parent = parent->contour_of;
    shape_t *last = parent;
    while (last->link) last = last->link;
    shape->contour_of = last;
    last->link = shape;
    shape->counter_clockwise = !last->counter_clockwise;
    current_shape = shape;
    linked_list_clear(selected_list);
    linked_list_push(selected_list, parent);
    while (parent->link){
        linked_list_push(selected_list, parent->link);
        parent = parent->link;
    }
    uno_refresh();
    return shape;
}

void join_contours(){
    shape_t *latest = 0;
    selected_shapes({
        if (latest) {
            shape->contour_of = latest;
            latest->link = shape;
        } else {
            current_shape = shape;
            shape->contour_of = 0;
        }
        latest = shape;
    });
    mog(INFO,"Joined %i contours",linked_list_count(selected_list));
    unselect_all();
    recalc_all();
    uno_refresh();
}

void invert(){
    selected_shapes({
        shape->counter_clockwise = !shape->counter_clockwise;
        linked_list_sort(shape->shape, shape, shape_sort);
    });
    recalc_all();
    uno_refresh();
}

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
        shape->render.middle = (vec2){};
        int total = 0;
        while (cur_shape){
            int c = 0;
            for (linked_list_node_t *head = cur_shape->shape->head; head; head = head->next){
                vector_point *p = head->data;
                if (!p) continue;
                vec2 np = vector2_scale(p->location, zoom);
                point_entry entry = {
                    .pos = vector2_sub(np, shape->min),
                    .on_curve = !p->control,
                };
                shape->render.middle = vector2_add(np, shape->render.middle);
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
        shape->render.middle = vector2_scale(shape->render.middle, 1.f/total);
    } else {
        vec2 np = vector2_scale(new_point->location, zoom);
        point_entry entry = {
            .pos = vector2_sub(np, shape->min),
            .on_curve = !new_point->control,
        };
        shape->render.middle = vector2_scale(shape->render.middle, count-1);
        shape->render.middle = vector2_add(shape->render.middle, np);
        shape->render.middle = vector2_scale(shape->render.middle, 1.f/count);
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