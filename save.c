#include "save.h"
#include "vra.h"
#include "export.h"
#include "files/helpers.h"
#include "data/serialize/binary_scanner.h"
#define MOG_LEVEL MOG_VERBOSE
#define MOG_MODULE "Save"
#include "utils/mog.h"
#include "editor.h"

string file_location;
string file_name;

buffer save_buf;

static inline bool ensure_save_path(){
    if (!file_location.length || !file_name.length){
        file_location = string_from_literal("/home/di/vra/test");
        file_name = string_from_literal("test_file.vra");
    }
    return true;
}

void save_file(){
    if (!ensure_save_path()){
        mog(ERROR,"No location to save into");
    }
    if (!save_buf.buffer) save_buf = buffer_create(0x1000, buffer_can_grow);
    else buffer_wipe(&save_buf);
    
    vra_save_hdr save_hdr = {
        .hdr_version = VERSION_NUM(0, 1, 0, 0),
        .magic = VRA_MAGIC_NUM,
        .num_shapes = linked_list_count(shape_list),
        .num_canvases = manual_canvases ? linked_list_count(export_canvases) : 0,
    };
    
    buffer_write_lim(&save_buf, (char*)&save_hdr, sizeof(vra_save_hdr));
    all_shapes({
        shape_t sh_copy = *shape;
        i64 contour = shape->contour_of ? linked_list_index_of(shape_list, shape->contour_of) : -1;
        sh_copy.contour_of = (void*)(contour < 0 ? UINT64_MAX : contour);
        i64 link = shape->link ? linked_list_index_of(shape_list, shape->link) : -1;
        sh_copy.link = (void*)(link < 0 ? UINT64_MAX : link);
        sh_copy.render.graph = (point_graph){};
        sh_copy.points_buffer = (buffer){};
        sh_copy.slice_buffer = (buffer){};
        sh_copy.current_slice = 0;
        u64 num_points = linked_list_count(sh_copy.shape);
        sh_copy.shape = 0;
        buffer_write_lim(&save_buf, (char*)&sh_copy, sizeof(shape_t));
        buffer_write_lim(&save_buf, (char*)&num_points, sizeof(u64));
        for (linked_list_node_t *n = shape->shape->head; n; n = n->next){
            vector_point *p = n->data;
            if (!p) continue;
            buffer_write_lim(&save_buf, (char*)p, sizeof(vector_point));
        }
    });
    all_canvases({
        buffer_write_lim(&save_buf, (char*)canvas, sizeof(aabb2));
    });
    
    string path = string_format("%S/%S",file_location,file_name);
    write_full_file(path.data, save_buf.buffer, save_buf.buffer_size);
    string_free(path);
}

#define check_scan(cond) if (!cond){ mog(ERROR, "Failed to scan"); return; }

void restore_file(char *path){
    string_slice content = read_file_slice(path);
    
    binary_scanner scan = bin_scan_create((void*)content.data, content.length, false);
    
    vra_save_hdr save_hdr = {};
    check_scan(bin_scan_size(&scan, sizeof(vra_save_hdr), &save_hdr));
    
    if (save_hdr.magic != VRA_MAGIC_NUM){
        mog(ERROR,"File %S is not a vra document",path);
        return;
    } 
    
    for (u64 i = 0; i < save_hdr.num_shapes; i++){
        shape_t *s = new_shape();
        check_scan(bin_scan_size(&scan, sizeof(shape_t), s));
        s->shape = linked_list_create();
        u64 num_points = 0;
        check_scan(bin_scan_u64(&scan, &num_points));
        for (u64 j = 0; j < num_points; j++){
            vector_point *p = zalloc(sizeof(vector_point));
            check_scan(bin_scan_size(&scan, sizeof(vector_point), p));
            linked_list_push(s->shape, p);
        }
        if (s->order != shape_ordering_manual) linked_list_sort(s->shape, s, shape_sort);
    }
    
    all_shapes({
        if ((uptr)shape->link < UINT64_MAX){
            shape->link = linked_list_get(shape_list, (u64)shape->link);
        } else shape->link = 0;
        if ((uptr)shape->contour_of < UINT64_MAX){
            shape->contour_of = linked_list_get(shape_list, (u64)shape->contour_of);
        } else shape->contour_of = 0;
    });
    
    all_shapes({
        recalc_shape(shape, 0);
    })
    
    if (save_hdr.num_canvases){
        enable_manual_canvas();
        for (u64 i = 0; i < save_hdr.num_canvases; i++){
            aabb2 *new_canvas = zalloc(sizeof(aabb2));
            check_scan(bin_scan_size(&scan, sizeof(aabb2), new_canvas));
            linked_list_push(export_canvases, new_canvas);
        }
    }
    
}