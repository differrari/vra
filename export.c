#include "export.h"
#define MOG_MODULE "VRA_EXPORT"
#define MOG_LEVEL MOG_WARNING
#include "utils/mog.h"
#include "files/helpers.h"

extern u64 point_count;

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