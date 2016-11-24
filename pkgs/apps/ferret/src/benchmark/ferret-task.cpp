/* AUTORIGHTS
Copyright (C) 2007 Princeton University
Copyright (C) 2010 Christian Fensch

TBB version of ferret written by Christian Fensch.

Ferret Toolkit is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2, or (at your option)
any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software Foundation,
Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
*/
#include <stdio.h>
#include <math.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <pthread.h>
#include "../include/cass.h"
#include "../include/cass_timer.h"
#include "../image/image.h"

#ifdef ENABLE_PARSEC_HOOKS
#include <hooks.h>
#endif

#include <vector>
#include <string>
#include <memory>

#include <tp_parsec.h>

namespace /*unnamed*/ {

int g_top_K = 10;

const char* const extra_params = "-L 8 - T 20";

cass_table_t* g_table = nullptr;
cass_table_t* g_query_table = nullptr;

int g_vec_dist_id = 0;
int g_vecset_dist_id = 0;

int g_cnt_enqueue = 0;
int g_cnt_dequeue = 0;

struct load_data
{
	int width, height;
	char *name;
	unsigned char *HSV, *RGB;
};


struct seg_data
{
	int width, height, nrgn;
	char *name;
	unsigned char *mask;
	unsigned char *HSV;
};


struct extract_data
{
	cass_dataset_t ds;
	char *name;
};


struct vec_query_data
{
	char *name;
	cass_dataset_t *ds;
	cass_result_t result;
};


struct rank_data
{
	char *name;
	cass_dataset_t *ds;
	cass_result_t result;
};



struct all_data {
	union {
		struct load_data      load;
		struct rank_data      rank;
	} first;
	union {
		struct seg_data       seg;
		struct vec_query_data vec;
	} second;
	struct extract_data extract;
};


[[noreturn]] inline void fatal_error(const char* const msg) {
    fprintf(stderr, "Fatal error: %s", msg);
    exit(EXIT_FAILURE);
}

inline bool is_ignored_dir(const char* const dir)
{
    return dir[0] == '.'
        && (
            // dir == "."
            dir[1] == '\0'
            
            // dir == ".."
            || (dir[1] == '.' && dir[2] == '\0')
        );
}

inline bool is_directory(const char* const path)
{
    struct stat st;
    if (::stat(path, &st) != 0)
        fatal_error("stat() failed");
    
    if (S_ISREG(st.st_mode))
        return false;
    else if (S_ISDIR(st.st_mode))
        return true;
    else
        fatal_error("neither directory nor file");
}

using scan_dir_result = std::vector<std::string>;

inline scan_dir_result scan_dir_rec(const char* const dir)
{
    scan_dir_result ret;
    
    DIR* const pd = ::opendir(dir);
    if (pd == nullptr) {
        fatal_error("opendir() failed");
    }
    
    // Search for a directory.
    while (struct dirent* const ent = ::readdir(pd))
    {
        const char* const name = ent->d_name;
        
        if (is_ignored_dir(name))
            continue;
        
        // Create a path.
        std::string path(dir);
        path += '/';
        path += name;
        
        if (is_directory(path.c_str())) {
            // Recursively scan the directory.
            const auto r = scan_dir_rec(path.c_str());
            std::copy(std::begin(r), std::end(r), std::back_inserter(ret));
        }
        else {
            // Add the file as a result.
            ret.push_back(std::move(path));
        }
    }
    
    ::closedir(pd);
    
    return ret;
}

/* ------ The Stages ------ */

inline void load_image(all_data* const data, const char* const path)
{
    data->first.load.name = strdup(path);
    
    const auto r =
        image_read_rgb_hsv(
            path
        ,   &data->first.load.width
        ,   &data->first.load.height
        ,   &data->first.load.RGB
        ,   &data->first.load.HSV
        );
    
    assert(r == 0);
    
    g_cnt_enqueue++;
}

inline void segment_image(all_data* const data)
{
    data->second.seg.name = data->first.load.name;
    
    data->second.seg.width = data->first.load.width;
    data->second.seg.height = data->first.load.height;
    data->second.seg.HSV = data->first.load.HSV;
    
    image_segment(
        reinterpret_cast<void**>(&data->second.seg.mask)
    ,   &data->second.seg.nrgn
    ,   data->first.load.RGB
    ,   data->first.load.width
    ,   data->first.load.height
    );
    
    free(data->first.load.RGB);
}

inline void extract_image(all_data* const data)
{
    data->extract.name = data->second.seg.name;
    
    image_extract_helper(
        data->second.seg.HSV
    ,   data->second.seg.mask
    ,   data->second.seg.width
    ,   data->second.seg.height
    ,   data->second.seg.nrgn
    ,   &data->extract.ds
    );
    
    free(data->second.seg.mask);
    free(data->second.seg.HSV);
}

inline void query_image(all_data* const data)
{
    data->second.vec.name = data->extract.name;
    
    cass_query_t query{};
    
    query.flags = CASS_RESULT_LISTS | CASS_RESULT_USERMEM;
    
    data->second.vec.ds = query.dataset = &data->extract.ds;
    
    query.vecset_id = 0;
    query.vec_dist_id = g_vec_dist_id;
    query.vecset_dist_id = g_vecset_dist_id;
    query.topk = 2*g_top_K;
    query.extra_params = extra_params;
    
    cass_result_alloc_list(
        &data->second.vec.result
    ,   data->second.vec.ds->vecset[0].num_regions
    ,   query.topk
    );
    
    cass_table_query(g_table, &query, &data->second.vec.result);
}

inline void rank_result(all_data* const data)
{
    data->first.rank.name = data->second.vec.name;
    
    cass_query_t query{};
    
    query.flags = CASS_RESULT_LIST | CASS_RESULT_USERMEM | CASS_RESULT_SORT;
    query.dataset = data->second.vec.ds;
    query.vecset_id = 0;
    query.vec_dist_id = g_vec_dist_id;
    query.vecset_dist_id = g_vecset_dist_id;
    query.topk = g_top_K;
    query.extra_params = nullptr;
    
    cass_result_t* candidate = 
        cass_result_merge_lists(
            &data->second.vec.result
        ,   static_cast<cass_dataset_t*>(g_query_table->__private)
        ,   0
        );
    
    query.candidate = candidate;
    
    cass_result_alloc_list(&data->first.rank.result, 0, g_top_K);
    
    cass_table_query(g_query_table, &query, &data->first.rank.result);
    
    cass_result_free(&data->second.vec.result);
    cass_result_free(candidate);
    free(candidate);
    cass_dataset_release(data->second.vec.ds);
}

inline void process_image(all_data* const data, const char* const path)
{
    load_image(data, path);
    segment_image(data);
    extract_image(data);
    query_image(data);
    rank_result(data);
}

inline void output_result(FILE* const fd, all_data* const data)
{
    fprintf(fd, "%s", data->first.rank.name);
    
    ARRAY_BEGIN_FOREACH(data->first.rank.result.u.list, cass_list_entry_t p)
    {
        if (p.dist == HUGE) continue;
        
        char* obj = nullptr;
        cass_map_id_to_dataobj(g_query_table->map, p.id, &obj);
        
        assert(obj != nullptr);
        
        fprintf(fd, "\t%s:%g", obj, p.dist);
    }
    ARRAY_END_FOREACH;
    
    fprintf(fd, "\n");
    
    cass_result_free(&data->first.rank.result);
    free(data->first.rank.name);
    //free(data); // Do not free here.
    
    g_cnt_dequeue++;
    
    fprintf(stderr, "(%d,%d)\n", g_cnt_enqueue, g_cnt_dequeue);
}

template <typename InputIterator>
inline void output_multiple_results(FILE* const fout, InputIterator first, const InputIterator last)
{
    for ( ; first != last; ++first) {
        output_result(fout, &*first);
    }
}

inline void exec_ferret(const char* const query_dir, FILE* const fout, const std::size_t depth)
{
    cilk_begin;
    
    const auto paths = scan_dir_rec(query_dir);
    
    std::vector<all_data> ongoing_buf;
    std::vector<all_data> write_buf;
    
    ongoing_buf.reserve(depth);
    write_buf.reserve(depth);
    
    auto path_itr = std::begin(paths);
    const auto path_last = std::end(paths);
    
    while (path_itr != path_last)
    {
        mk_task_group;
        
        for (std::size_t i = 0; i < depth; ++i)
        {
            const auto path = path_itr->c_str();
            
            ongoing_buf.emplace_back();
            
            const auto data = &ongoing_buf.back();
            
            create_task0(spawn
                process_image(data, path)
            );
            
            if (++path_itr == path_last)
                break;
        }
        
        if (!write_buf.empty()) {
            const auto first = write_buf.begin();
            const auto last = write_buf.end();
            create_task0(spawn
                output_multiple_results(fout, first, last)
            );
        }
        
        wait_tasks;
        
        std::swap(ongoing_buf, write_buf);
        
        ongoing_buf.clear();
    }
    
    output_multiple_results(fout, write_buf.begin(), write_buf.end());
    
    cilk_void_return;
}

} // unnamed namespace

int main(const int argc, char* const argv[])
{
#ifdef PARSEC_VERSION
#define __PARSEC_STRING(x) #x
#define __PARSEC_XSTRING(x) __PARSEC_STRING(x)
    printf("PARSEC Benchmark Suite Version " __PARSEC_XSTRING(PARSEC_VERSION)"\n");
    fflush(NULL);
#else
    printf("PARSEC Benchmark Suite\n");
    fflush(NULL);
#endif //PARSEC_VERSION
#ifdef ENABLE_PARSEC_HOOKS
    __parsec_bench_begin(__parsec_ferret);
#endif
    
#if defined(ENABLE_TASK)
    tp_init();
#endif
    
    if (argc < 8) {
        printf("%s <database> <table> <query dir> <top K> <depth> <n> <out>\n", argv[0]);
        return 0;
    }
    
    const char* const db_dir = argv[1];
    const char* const table_name = argv[2];
    const char* const query_dir = argv[3];
    g_top_K = atoi(argv[4]);
    
    const int depth = atoi(argv[5]);
    
    const int TOKEN_IN_FLIGHT = atoi(argv[6]);
    
    const char* const output_path = argv[7];
    
    FILE* const fout = fopen(output_path, "w");
    assert(fout != NULL);
    
    cass_init();
    
    cass_env_t* env = nullptr;
    const int open_ret = cass_env_open(&env, const_cast<char*>(db_dir), 0);
    if (open_ret != 0) {
        printf("ERROR: %s\n", cass_strerror(open_ret));
        return 0; // TODO
    }
    
    g_vec_dist_id = cass_reg_lookup(&env->vec_dist, "L2_float");
    assert(g_vec_dist_id >= 0);
    
    g_vecset_dist_id = cass_reg_lookup(&env->vecset_dist, "emd");
    assert(g_vecset_dist_id >= 0);
    
    int i = cass_reg_lookup(&env->table, table_name);
    
    g_table = g_query_table =
        static_cast<cass_table_t*>(
            cass_reg_get(&env->table, i)
        );
    
    i = g_table->parent_id;
    
    if (i >= 0) {
        g_query_table =
            static_cast<cass_table_t*>(
                cass_reg_get(&env->table, i)
            );
    }
    
    if (g_query_table != g_table) {
        cass_table_load(g_query_table);
    }
    
    cass_map_load(g_query_table->map);
    
    cass_table_load(g_table);
    
    image_init(argv[0]);
    
    stimer_t tmr;
    stimer_tick(&tmr);
    
#ifdef ENABLE_PARSEC_HOOKS
    __parsec_roi_begin();
#endif
    
    exec_ferret(query_dir, fout, static_cast<std::size_t>(depth));
    
#ifdef ENABLE_PARSEC_HOOKS
    __parsec_roi_end();
#endif
    
    stimer_tuck(&tmr, "QUERY TIME");
    
    const int close_ret = cass_env_close(env, 0);
    if (close_ret != 0) {
        printf("ERROR: %s\n", cass_strerror(close_ret));
        return 0;
    }
    
    cass_cleanup();
    
    image_cleanup();
    
    fclose(fout);
    
#ifdef ENABLE_PARSEC_HOOKS
    __parsec_bench_end();
#endif
    return 0;
}

