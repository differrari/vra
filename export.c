#include "export.h"
#define MOG_MODULE "VRA_EXPORT"
#define MOG_LEVEL MOG_WARNING
#include "utils/mog.h"
#include "files/helpers.h"
#include "alloc/alloc.h"
#include "math/aabb2.h"

extern u64 point_count;

linked_list_t *export_canvases;

void serialize(){
    if (!linked_list_count(export_canvases)){
        aabb2 *canvas = new(aabb2);
        *canvas = (aabb2){
            .min = { FLOAT_MAX,FLOAT_MAX},
            .max = { 0, 0}
        };
        all_shapes({
            if (shape->min.x < canvas->min.x) canvas->min.x = shape->min.x;
            if (shape->min.y < canvas->min.y) canvas->min.y = shape->min.y;
            if (shape->max.x > canvas->max.x) canvas->max.x = shape->max.x;
            if (shape->max.y > canvas->max.y) canvas->max.y = shape->max.y;
        });
        linked_list_push(export_canvases, canvas);
        mog(VERBOSE,"Created new canvas %f,%f - %f,%f",canvas->min.x,canvas->min.y,canvas->max.x,canvas->max.y);
    }
    for (linked_list_node_t *node = export_canvases->head; node; node = node->next){
        if (!node->data) continue;
        aabb2 *bounds = node->data;
        buffer buf = buffer_create(sizeof(vro_header) + (sizeof(shape_t)*linked_list_count(shape_list)) + (point_count*sizeof(point_entry)), buffer_can_grow);
        size_t shape_count = 0;
        vro_header hdr = {
            .hdr_version = VERSION_NUM(0, 1, 0, 0),
            .magic = MAGIC_NUM,
            .num_shapes = 0
        };
        buffer_write_lim(&buf, (char*)&hdr, sizeof(vro_header));
        
        all_shapes({
            if (!aabb2_check_collision((aabb2){.min = shape->min, .max = shape->max}, *bounds)) continue;
            shape_count++;
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
        
        vro_header *written_hdr = buf.buffer;
        written_hdr->num_shapes = shape_count;
        
        mog(VERBOSE,"Wrote %i vros",written_hdr->num_shapes);
        
        write_full_file("/home/di/playground/hey.vro", buf.buffer, buf.buffer_size);
    } 
}