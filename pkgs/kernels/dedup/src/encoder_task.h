/*
 * Decoder for dedup files
 *
 * Copyright 2010 Princeton University.
 * All rights reserved.
 *
 * Originally written by Minlan Yu.
 * Largely rewritten by Christian Bienia.
 */

/*
 * The pipeline model for Encode is Fragment->FragmentRefine->Deduplicate->Compress->Reorder
 * Each stage has basically three steps:
 * 1. fetch a group of items from the queue
 * 2. process the items
 * 3. put them in the queue for the next stage
 */

#ifndef ENABLE_TASK
  #error "compile this program with ENABLE_TASK"
#endif

#include <assert.h>
#include <strings.h>
#include <math.h>
#include <limits.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>

#include "util.h"
#include "dedupdef.h"
#include "encoder.h"
#include "debug.h"
#include "hashtable.h"
#include "config.h"
#include "rabin.h"
#include "mbuffer.h"

#ifdef ENABLE_GZIP_COMPRESSION
#include <zlib.h>
#endif //ENABLE_GZIP_COMPRESSION

#ifdef ENABLE_BZIP2_COMPRESSION
#include <bzlib.h>
#endif //ENABLE_BZIP2_COMPRESSION

#include <tp_parsec.h>
#include "task_lock.h"

#ifdef ENABLE_PARSEC_HOOKS
#include <hooks.h>
#endif //ENABLE_PARSEC_HOOKS

//Cut-off constants
//Larger constants result in less parallelism and less overheads.
#define TASK_CUTOFF_FRAGMENT 1
#define TASK_CUTOFF_FRAGMENT_REFINE 20
//Frequency of writing data
//It tries to write down after processing every (TASK_FRAGMENT_SYNC_FREQUENCY * TASK_CUTOFF_FRAGMENT) chunks
//Note that smaller frequency reduces parallelism, while it is desirable to write calculated date to file concurrently
//For example, online processing is one typical example in which smaller frequency is preferable.
#define TASK_FRAGMENT_SYNC_FREQUENCY 25
//Use a buffer for xwrite
#define TASK_USE_XWRITE_BUFFER 0
#if TASK_USE_XWRITE_BUFFER
  //Buffer size of xwrite_buffer
  #define TASK_XWRITE_BUFFER_SIZE (1024*1024*8)
#endif

//Parallelize detecting segments in Fragment.
#define TASK_PARALLEL_FRAGMENT 1
#if TASK_PARALLEL_FRAGMENT
  //Granularity of parallel fragment.
  #define TASK_CUTOFF_PARALLEL_FRAGMENT 1
#endif


//The configuration block defined in main
static config_t * conf;

//Hash table data structure & utility functions
static struct hashtable *cache;

static unsigned int hash_from_key_fn( void *k ) {
  //NOTE: sha1 sum is integer-aligned
  return ((unsigned int *)k)[0];
}

static int keys_equal_fn ( void *key1, void *key2 ) {
  return (memcmp(key1, key2, SHA1_LEN) == 0);
}

#ifdef ENABLE_STATISTICS
//Keep track of block granularity with 2^CHUNK_GRANULARITY_POW resolution (for statistics)
#define CHUNK_GRANULARITY_POW (7)
//Number of blocks to distinguish, CHUNK_MAX_NUM * 2^CHUNK_GRANULARITY_POW is biggest block being recognized (for statistics)
#define CHUNK_MAX_NUM (8*32)
//Map a chunk size to a statistics array slot
#define CHUNK_SIZE_TO_SLOT(s) ( ((s)>>(CHUNK_GRANULARITY_POW)) >= (CHUNK_MAX_NUM) ? (CHUNK_MAX_NUM)-1 : ((s)>>(CHUNK_GRANULARITY_POW)) )
//Get the average size of a chunk from a statistics array slot
#define SLOT_TO_CHUNK_SIZE(s) ( (s)*(1<<(CHUNK_GRANULARITY_POW)) + (1<<((CHUNK_GRANULARITY_POW)-1)) )
//Deduplication statistics (only used if ENABLE_STATISTICS is defined)
typedef struct {
  /* Cumulative sizes */
  size_t total_input; //Total size of input in bytes
  size_t total_dedup; //Total size of input without duplicate blocks (after global compression) in bytes
  size_t total_compressed; //Total size of input stream after local compression in bytes
  size_t total_output; //Total size of output in bytes (with overhead) in bytes

  /* Size distribution & other properties */
  unsigned int nChunks[CHUNK_MAX_NUM]; //Coarse-granular size distribution of data chunks
  unsigned int nDuplicates; //Total number of duplicate blocks
} stats_t;

//variable with global statistics
stats_t g_stats;

//Initialize a statistics record
static void init_stats(stats_t *s) {
  int i;

  assert(s!=NULL);
  s->total_input = 0;
  s->total_dedup = 0;
  s->total_compressed = 0;
  s->total_output = 0;

  for(i=0; i<CHUNK_MAX_NUM; i++) {
    s->nChunks[i] = 0;
  }
  s->nDuplicates = 0;
}

#define atomic_fetch_and_add(ptr, num) __sync_fetch_and_add(ptr, num)

//Print statistics
static void print_stats(stats_t *s) {
  const unsigned int unit_str_size = 7; //elements in unit_str array
  const char *unit_str[] = {"Bytes", "KB", "MB", "GB", "TB", "PB", "EB"};
  unsigned int unit_idx = 0;
  size_t unit_div = 1;

  assert(s!=NULL);

  //determine most suitable unit to use
  for(unit_idx=0; unit_idx<unit_str_size; unit_idx++) {
    unsigned int unit_div_next = unit_div * 1024;

    if(s->total_input / unit_div_next <= 0) break;
    if(s->total_dedup / unit_div_next <= 0) break;
    if(s->total_compressed / unit_div_next <= 0) break;
    if(s->total_output / unit_div_next <= 0) break;

    unit_div = unit_div_next;
  }

  printf("Total input size:              %14.2f %s\n", (float)(s->total_input)/(float)(unit_div), unit_str[unit_idx]);
  printf("Total output size:             %14.2f %s\n", (float)(s->total_output)/(float)(unit_div), unit_str[unit_idx]);
  printf("Effective compression factor:  %14.2fx\n", (float)(s->total_input)/(float)(s->total_output));
  printf("\n");

  //Total number of chunks
  unsigned int i;
  unsigned int nTotalChunks=0;
  for(i=0; i<CHUNK_MAX_NUM; i++) nTotalChunks+= s->nChunks[i];

  //Average size of chunks
  float mean_size = 0.0;
  for(i=0; i<CHUNK_MAX_NUM; i++) mean_size += (float)(SLOT_TO_CHUNK_SIZE(i)) * (float)(s->nChunks[i]);
  mean_size = mean_size / (float)nTotalChunks;

  //Variance of chunk size
  float var_size = 0.0;
  for(i=0; i<CHUNK_MAX_NUM; i++) var_size += (mean_size - (float)(SLOT_TO_CHUNK_SIZE(i))) *
                                             (mean_size - (float)(SLOT_TO_CHUNK_SIZE(i))) *
                                             (float)(s->nChunks[i]);

  printf("Mean data chunk size:          %14.2f %s (stddev: %.2f %s)\n", mean_size / 1024.0, "KB", sqrtf(var_size) / 1024.0, "KB");
  printf("Amount of duplicate chunks:    %14.2f%%\n", 100.0*(float)(s->nDuplicates)/(float)(nTotalChunks));
  printf("Data size after deduplication: %14.2f %s (compression factor: %.2fx)\n", (float)(s->total_dedup)/(float)(unit_div), unit_str[unit_idx], (float)(s->total_input)/(float)(s->total_dedup));
  printf("Data size after compression:   %14.2f %s (compression factor: %.2fx)\n", (float)(s->total_compressed)/(float)(unit_div), unit_str[unit_idx], (float)(s->total_dedup)/(float)(s->total_compressed));
  printf("Output overhead:               %14.2f%%\n", 100.0*(float)(s->total_output-s->total_compressed)/(float)(s->total_output));
}

#endif //ENABLE_STATISTICS

#if TASK_USE_XWRITE_BUFFER
typedef struct {
  u_char buffer[TASK_XWRITE_BUFFER_SIZE];
  size_t length;
} xwrite_buffer_t;

static int flush_xwrite_buffer(int fd, xwrite_buffer_t* xwrite_buffer) {
 if (xwrite(fd, xwrite_buffer->buffer, xwrite_buffer->length) < 0){
    perror("xwrite:");
    EXIT_TRACE("xwrite buffer fails\n");
    return -1;
  }
  xwrite_buffer->length = 0;
  return 0;
}
static int write_xwrite_buffer(xwrite_buffer_t* xwrite_buffer, int fd, void* data, int len) {
  if(xwrite_buffer->length + len > TASK_XWRITE_BUFFER_SIZE) {
    if(flush_xwrite_buffer(fd, xwrite_buffer) == -1)
      return -1;
    if(len > TASK_XWRITE_BUFFER_SIZE) {
      if(xwrite(fd, data, len) < 0) {
        perror("xwrite:");
        EXIT_TRACE("xwrite data fails\n");
        return -1;
      }
      return 0;
    }
  }
  //Append data.
  memcpy(xwrite_buffer->buffer + xwrite_buffer->length, data, len);
  xwrite_buffer->length += len;
  return 0;
}
static int write_file_use_buffer(xwrite_buffer_t* xwrite_buffer, int fd, u_char type, u_long len, u_char * content) {
  if (write_xwrite_buffer(xwrite_buffer, fd, &type, sizeof(type)) < 0){
    perror("xwrite:");
    EXIT_TRACE("xwrite type fails\n");
    return -1;
  }
  if (write_xwrite_buffer(xwrite_buffer, fd, &len, sizeof(len)) < 0){
    EXIT_TRACE("xwrite content fails\n");
  }
  if (write_xwrite_buffer(xwrite_buffer, fd, content, len) < 0){
    EXIT_TRACE("xwrite content fails\n");
  }
  return 0;
}
#else
//Simple write utility function
static int write_file(int fd, u_char type, u_long len, u_char * content) {
  if (xwrite(fd, &type, sizeof(type)) < 0){
    perror("xwrite:");
    EXIT_TRACE("xwrite type fails\n");
    return -1;
  }
  if (xwrite(fd, &len, sizeof(len)) < 0){
    EXIT_TRACE("xwrite content fails\n");
  }
  if (xwrite(fd, content, len) < 0){
    EXIT_TRACE("xwrite content fails\n");
  }
  return 0;
}
#endif//TASK_USE_XWRITE_BUFFER

/*
 * Helper function that creates and initializes the output file
 * Takes the file name to use as input and returns the file handle
 * The output file can be used to write chunks without any further steps
 */
static int create_output_file(char *outfile) {
  int fd;

  //Create output file
  fd = open(outfile, O_CREAT|O_TRUNC|O_WRONLY|O_TRUNC, S_IRGRP | S_IWUSR | S_IRUSR | S_IROTH);
  if (fd < 0) {
    EXIT_TRACE("Cannot open output file.");
  }

  //Write header
  if (write_header(fd, conf->compress_type)) {
    EXIT_TRACE("Cannot write output file header.\n");
  }

  return fd;
}

/*
 * Helper function that writes a chunk to an output file depending on
 * its state. The function will write the SHA1 sum if the chunk has
 * already been written before, or it will write the compressed data
 * of the chunk if it has not been written yet.
 *
 * This function will block if the compressed data is not available yet.
 * This function might update the state of the chunk if there are any changes.
 */
//NOTE: The serial version relies on the fact that chunks are processed in-order,
//      which means if it reaches the function it is guaranteed all data is ready.
#if TASK_USE_XWRITE_BUFFER
static void write_chunk_to_file_use_buffer(int fd, chunk_t *_chunk, xwrite_buffer_t* xwrite_buffer) {
  chunk_t* chunk = _chunk;
  assert(chunk!=NULL);
  if(chunk->header.state == CHUNK_STATE_EMPTY) {
    //Nothing to do.
    return;
  }
  if(chunk->header.isDuplicate)
    chunk = chunk->compressed_data_ref;

  if(chunk->header.state == CHUNK_STATE_COMPRESSED) {
    //Chunk data has not been written yet, do so now
    write_file_use_buffer(xwrite_buffer, fd, TYPE_COMPRESS, chunk->compressed_data.n, (u_char*)chunk->compressed_data.ptr);
    mbuffer_free(&chunk->compressed_data);
    chunk->header.state = CHUNK_STATE_FLUSHED;
  } else {
    assert(chunk->header.state == CHUNK_STATE_FLUSHED);
    //Duplicate chunk, data has been written to file before, just write SHA1
    write_file_use_buffer(xwrite_buffer, fd, TYPE_FINGERPRINT, SHA1_LEN, (unsigned char *)(chunk->sha1));
  }
}
#else
static void write_chunk_to_file(int fd, chunk_t *_chunk) {
  chunk_t* chunk = _chunk;
  assert(chunk!=NULL);
  if(chunk->header.state == CHUNK_STATE_EMPTY) {
    //Nothing to do.
    return;
  }
  if(chunk->header.isDuplicate)
    chunk = chunk->compressed_data_ref;

  if(chunk->header.state == CHUNK_STATE_COMPRESSED) {
    //Chunk data has not been written yet, do so now
    write_file(fd, TYPE_COMPRESS, chunk->compressed_data.n, (u_char*)chunk->compressed_data.ptr);
    mbuffer_free(&chunk->compressed_data);
    chunk->header.state = CHUNK_STATE_FLUSHED;
  } else {
    assert(chunk->header.state == CHUNK_STATE_FLUSHED);
    //Duplicate chunk, data has been written to file before, just write SHA1
    write_file(fd, TYPE_FINGERPRINT, SHA1_LEN, (unsigned char *)(chunk->sha1));
  }
}
#endif
/*
 * Write chunks starting from head_chunk.
 * This funciton will be parallelized.
 */
#if TASK_USE_XWRITE_BUFFER
void write_all_chunks_to_file_use_buffer(int fd, chunk_t *chunk_in, xwrite_buffer_t* xwrite_buffer) {
  chunk_t* chunk = chunk_in;
  while(chunk){
    write_chunk_to_file_use_buffer(fd,chunk,xwrite_buffer);
    chunk = chunk->next;
  }
  flush_xwrite_buffer(fd, xwrite_buffer);
}
#else
void write_all_chunks_to_file(int fd, chunk_t *chunk_in) {
  chunk_t* chunk = chunk_in;
  while(chunk){
    write_chunk_to_file(fd,chunk);
    chunk = chunk->next;
  }
}
#endif

int rf_win;
int rf_win_dataprocess;

/*
 * Computational kernel of compression stage
 *
 * Actions performed:
 *  - Compress a data chunk
 * Note it processes a single element of chunk. 
 */
void sub_Compress(chunk_t *chunk) {
    size_t n;
    int r;

    assert(chunk!=NULL);
    //compress the item and add it to the database
    switch (conf->compress_type) {
      case COMPRESS_NONE:
        //Simply duplicate the data
        n = chunk->uncompressed_data.n;
        r = mbuffer_create(&chunk->compressed_data, n);
        if(r != 0) {
          EXIT_TRACE("Creation of compression buffer failed.\n");
        }
        //copy the block
        memcpy(chunk->compressed_data.ptr, chunk->uncompressed_data.ptr, chunk->uncompressed_data.n);
        break;
#ifdef ENABLE_GZIP_COMPRESSION
      case COMPRESS_GZIP:
        //Gzip compression buffer must be at least 0.1% larger than source buffer plus 12 bytes
        n = chunk->uncompressed_data.n + (chunk->uncompressed_data.n >> 9) + 12;
        r = mbuffer_create(&chunk->compressed_data, n);
        if(r != 0) {
          EXIT_TRACE("Creation of compression buffer failed.\n");
        }
        //compress the block
        r = compress((Bytef*)chunk->compressed_data.ptr, &n, (Bytef*)chunk->uncompressed_data.ptr, chunk->uncompressed_data.n);
        if (r != Z_OK) {
          EXIT_TRACE("Compression failed\n");
        }
        //Shrink buffer to actual size
        if(n < chunk->compressed_data.n) {
          r = mbuffer_realloc(&chunk->compressed_data, n);
          assert(r == 0);
        }
        break;
#endif //ENABLE_GZIP_COMPRESSION
#ifdef ENABLE_BZIP2_COMPRESSION
      case COMPRESS_BZIP2:
        //Bzip compression buffer must be at least 1% larger than source buffer plus 600 bytes
        n = chunk->uncompressed_data.n + (chunk->uncompressed_data.n >> 6) + 600;
        r = mbuffer_create(&chunk->compressed_data, n);
        if(r != 0) {
          EXIT_TRACE("Creation of compression buffer failed.\n");
        }
        //compress the block
        unsigned int int_n = n;
        r = BZ2_bzBuffToBuffCompress(chunk->compressed_data.ptr, &int_n, chunk->uncompressed_data.ptr, chunk->uncompressed_data.n, 9, 0, 30);
        n = int_n;
        if (r != BZ_OK) {
          EXIT_TRACE("Compression failed\n");
        }
        //Shrink buffer to actual size
        if(n < chunk->compressed_data.n) {
          r = mbuffer_realloc(&chunk->compressed_data, n);
          assert(r == 0);
        }
        break;
#endif //ENABLE_BZIP2_COMPRESSION
      default:
        EXIT_TRACE("Compression type not implemented.\n");
        break;
    }
    mbuffer_free(&chunk->uncompressed_data);
    chunk->header.state = CHUNK_STATE_COMPRESSED;
    return;
}
/*
 * Computational kernel of deduplication stage
 *
 * Actions performed:
 *  - Calculate SHA1 signature for each incoming data chunk
 *  - Perform database lookup to determine chunk redundancy status
 *  - On miss add chunk to database
 *  - Returns chunk redundancy status
 * Note it processes a single element of chunk.
 */
int sub_Deduplicate(chunk_t *chunk) {
  int isDuplicate;
  chunk_t *entry;

  assert(chunk!=NULL);
  assert(chunk->uncompressed_data.ptr!=NULL);

  SHA1_Digest(chunk->uncompressed_data.ptr, chunk->uncompressed_data.n, (unsigned char *)(chunk->sha1));

  //Query database to determine whether we've seen the data chunk before
#ifdef ENABLE_TASK_PTHREADS_LOCK
  pthread_mutex_t *ht_lock = hashtable_getlock(cache, (void *)(chunk->sha1));
  pthread_mutex_lock(ht_lock);
#else
  task_lock_t *ht_lock = hashtable_getlock(cache, (void *)(chunk->sha1));
  task_lock_lock(ht_lock);
#endif
  entry = (chunk_t *)hashtable_search(cache, (void *)(chunk->sha1));
  isDuplicate = (entry != NULL);
  chunk->header.isDuplicate = isDuplicate;
  if (!isDuplicate) {
    // Cache miss: Create entry in hash table and forward data to compression stage
    //NOTE: chunk->compressed_data.buffer will be computed in compression stage
    if (hashtable_insert(cache, (void *)(chunk->sha1), (void *)chunk) == 0) {
      EXIT_TRACE("hashtable_insert failed");
    }
  } else {
    // Cache hit: Skipping compression stage
    chunk->compressed_data_ref = entry;
    mbuffer_free(&chunk->uncompressed_data);
  }
#ifdef ENABLE_TASK_PTHREADS_LOCK
  pthread_mutex_unlock(ht_lock);
#else
  task_lock_unlock(ht_lock);
#endif

  return isDuplicate;
}

/*
 * Parallelized function. It literally consists of Deduplicate and Compress
 * This funciton just modifies chunks of size chunk_num.
 */
void DeduplicateAndCompress(chunk_t * _chunk_in, int _chunk_num) {
  cilk_begin;
  int chunk_num = _chunk_num;
  chunk_t *chunk = _chunk_in;
#ifdef ENABLE_STATISTICS
  size_t stats_nDuplicates = 0;
  size_t stats_total_dedup = 0;
  size_t stats_total_compressed = 0;
#endif //ENABLE_STATISTICS
  while(--chunk_num >=0) {
    assert(chunk);
    //Do the processing
    int isDuplicate = sub_Deduplicate(chunk);
#ifdef ENABLE_STATISTICS
    if(isDuplicate) {
      stats_nDuplicates++;
    } else {
      stats_total_dedup += chunk->uncompressed_data.n;
    }
#endif //ENABLE_STATISTICS
    if (!isDuplicate) {
      sub_Compress(chunk);
#ifdef ENABLE_STATISTICS
      stats_total_compressed += chunk->compressed_data.n;
#endif //ENABLE_STATISTICS
    }
    chunk = chunk->next;
  }
#ifdef ENABLE_STATISTICS
  atomic_fetch_and_add(&g_stats.nDuplicates, stats_nDuplicates);
  atomic_fetch_and_add(&g_stats.total_dedup, stats_total_dedup);
  atomic_fetch_and_add(&g_stats.total_compressed, stats_total_compressed);
#endif //ENABLE_STATISTICS
  cilk_void_return;
}

/*
 * This funcion take coarse chunks from the parent task (Fragment)
 * and returns finer-grained tasks.
 * Note that this function may increase the number of chunks.
 */
void FragmentRefine(chunk_t * _chunk_in, int _chunk_num) {
  cilk_begin;
  int r;

  u32int * rabintab = (u32int*) malloc(256*sizeof rabintab[0]);
  u32int * rabinwintab = (u32int*) malloc(256*sizeof rabintab[0]);
  if(rabintab == NULL || rabinwintab == NULL) {
    EXIT_TRACE("Memory allocation failed.\n");
  }
  int chunk_num = _chunk_num;
  chunk_t * chunk = _chunk_in;

  //These values will be given to child tasks (DeduplicateAndCompress)
  int child_task_chunk_num = 0;
  chunk_t* child_task_chunk_head = chunk;

  mk_task_group;
  while (--chunk_num >= 0) {
    assert(chunk);
    rabininit(rf_win, rabintab, rabinwintab);

    int split;
    do {
      //Find next anchor with Rabin fingerprint
      int offset = rabinseg((uchar*)chunk->uncompressed_data.ptr, chunk->uncompressed_data.n, rf_win, rabintab, rabinwintab);
      //Can we split the buffer?
      if(offset < (int)chunk->uncompressed_data.n) {
        //Allocate a new chunk and create a new memory buffer
        chunk_t * temp = (chunk_t *)malloc(sizeof(chunk_t));
        if(temp==NULL) EXIT_TRACE("Memory allocation failed.\n");
        temp->header.state = chunk->header.state;
        //[... -> chunk -> ...] is split and becomes [... -> chunk -> temp -> ...].
        temp->next = chunk->next;
        chunk->next = temp;
        //split it into two pieces
        r = mbuffer_split(&chunk->uncompressed_data, &temp->uncompressed_data, offset);
        if(r!=0) EXIT_TRACE("Unable to split memory buffer.\n");

#ifdef ENABLE_STATISTICS
        //update statistics
        atomic_fetch_and_add(&g_stats.nChunks[CHUNK_SIZE_TO_SLOT(chunk->uncompressed_data.n)], 1);
#endif //ENABLE_STATISTICS
        //prepare for next iteration
        chunk = temp;
        split = 1;
        //Here, check if chunks are sufficiently created.
        child_task_chunk_num++;
        if(child_task_chunk_num >= TASK_CUTOFF_FRAGMENT_REFINE) {
          //Now chunks are processed.
          create_task0(spawn DeduplicateAndCompress(child_task_chunk_head, child_task_chunk_num));
          child_task_chunk_head = chunk;
          child_task_chunk_num = 0;
        }
      } else {
        //End of buffer reached.
#ifdef ENABLE_STATISTICS
        //update statistics
        atomic_fetch_and_add(&g_stats.nChunks[CHUNK_SIZE_TO_SLOT(chunk->uncompressed_data.n)], 1);
#endif //ENABLE_STATISTICS
        //prepare for next iteration
        split = 0;
      }
    } while(split);
    chunk = chunk->next;
    child_task_chunk_num++;
    if(child_task_chunk_num >= TASK_CUTOFF_FRAGMENT_REFINE) {
      //chunks are processed.
      create_task0(spawn DeduplicateAndCompress(child_task_chunk_head, child_task_chunk_num));
      child_task_chunk_head = chunk;
      child_task_chunk_num = 0;
    }
  }
  //Create tasks to process the remaming chunks.
  call_task(spawn DeduplicateAndCompress(child_task_chunk_head, child_task_chunk_num));
  //sync here.
  wait_tasks;

  free(rabintab);
  free(rabinwintab);

  cilk_void_return;
}

/*
 * Task for Fragmentation. It is a root task.
 */
#if TASK_PARALLEL_FRAGMENT
//Try to find segment from {head + ANCHOR_JUMP} to {head + size}.
//If it succeeds to find a suitable break, it return offsets of offsets_len.
//Otherwise, offsets_len becomes 0.
typedef struct {
  size_t* offsets;
  int offsets_len;
} fragment_task_offset_t;

void SplitStream(u_char* head, size_t size, fragment_task_offset_t* offset_info) {
  cilk_begin;
  if(size < ANCHOR_JUMP * 2 * TASK_CUTOFF_PARALLEL_FRAGMENT) {
    //Try to split the region: head ~ head + size.
    u32int * rabintab = (u32int*) malloc(256*sizeof rabintab[0]);
    u32int * rabinwintab = (u32int*) malloc(256*sizeof rabintab[0]);
    if(rabintab == NULL || rabinwintab == NULL) {
      EXIT_TRACE("Memory allocation failed.\n");
    }
    rabininit(rf_win_dataprocess, rabintab, rabinwintab);

    offset_info->offsets = NULL;
    offset_info->offsets_len = 0;
    size_t head_offset = 0;
    while(size > ANCHOR_JUMP + head_offset) {
      int offset = rabinseg(head + head_offset + ANCHOR_JUMP, size - ANCHOR_JUMP - head_offset, rf_win_dataprocess, rabintab, rabinwintab);
      //Did we find a split location?
      if(offset == 0) {
        //Ignore this case.
        break;
      } else if(head_offset + offset + ANCHOR_JUMP < size) {
        //Successfully find a split location.
        if(!offset_info->offsets)
          //The number of offsets is at most size / ANCHOR_JUMP
          offset_info->offsets = (size_t*)malloc(sizeof(size_t) * (size / ANCHOR_JUMP));
        head_offset += offset + ANCHOR_JUMP;
        offset_info->offsets[offset_info->offsets_len++] = head_offset;
      }
    }
    free(rabintab);
    free(rabinwintab);
  } else {
    //divide-and-conquer.
    mk_task_group;

    u_char* head1 = head;
    size_t  size1 = size / 2;
    fragment_task_offset_t* offset_info1=(fragment_task_offset_t*)malloc(sizeof(fragment_task_offset_t));;
    create_task0(spawn SplitStream(head1, size1, offset_info1));

    u_char* head2 = head1 + size1;
    size_t  size2 = size - size1;
    fragment_task_offset_t* offset_info2=(fragment_task_offset_t*)malloc(sizeof(fragment_task_offset_t));;
    call_task   (spawn SplitStream(head2, size2, offset_info2));

    wait_tasks;
    int new_offsets_len = offset_info1->offsets_len + offset_info2->offsets_len;
    offset_info->offsets_len = new_offsets_len;
    if(new_offsets_len > 0) {
      offset_info->offsets = (size_t*)malloc(sizeof(size_t) * new_offsets_len);
      int index = 0;
      for(int i = 0; i < offset_info1->offsets_len; i++)
        offset_info->offsets[index++] = offset_info1->offsets[i];
      if(offset_info1->offsets_len > 0)
        free(offset_info1->offsets);
      for(int i = 0; i < offset_info2->offsets_len; i++)
        offset_info->offsets[index++] = offset_info2->offsets[i] + size1;
      if(offset_info2->offsets_len > 0)
        free(offset_info2->offsets);
    }
    free(offset_info1);
    free(offset_info2);
  }
  cilk_void_return;
}

void Fragment(int fd, void *input_file_buffer, size_t input_file_size) {
  cilk_begin;

  if(!conf->preloading)
    EXIT_TRACE("This version assumes preloading. Disable TASK_PARALLEL_FRAGMENT to disable preloading.\n");

  int r;

  u32int * rabintab = (u32int*) malloc(256*sizeof rabintab[0]);
  u32int * rabinwintab = (u32int*) malloc(256*sizeof rabintab[0]);
  if(rabintab == NULL || rabinwintab == NULL) {
    EXIT_TRACE("Memory allocation failed.\n");
  }

  rf_win_dataprocess = 0;
  rabininit(rf_win_dataprocess, rabintab, rabinwintab);

  #if TASK_USE_XWRITE_BUFFER
    xwrite_buffer_t* xwrite_buffer = (xwrite_buffer_t*)malloc(sizeof(xwrite_buffer_t));
    xwrite_buffer->length = 0;
  #endif
  //Sanity check
  if(MAXBUF < 8 * ANCHOR_JUMP) {
    printf("WARNING: I/O buffer size is very small. Performance degraded.\n");
    fflush(NULL);
  }

  int fd_out = create_output_file(conf->outfile);

  //First, roughly extract chunks.
  size_t* chunk_offsets = 0;
  int chunk_offsets_len = 0;
  {
    u_char* head = (u_char*)input_file_buffer;
    size_t size = input_file_size;
    fragment_task_offset_t* offset_info=(fragment_task_offset_t*)malloc(sizeof(fragment_task_offset_t));
    mk_task_group;
    call_task(spawn SplitStream(head, size, offset_info));
    wait_tasks;
    chunk_offsets = offset_info->offsets;
    chunk_offsets_len = offset_info->offsets_len;
    free(offset_info);
  }
  //processed_chunk_head is a head of chunks which are ready to write to a file.
  //NOTE: processing_chunk_head and processed_chunk_head are not connected by links. 
  //processign_chunk_head is declared in the following while-loop. 
  chunk_t* processed_chunk_head = NULL;

  //read from input buffer
  int chunk_offset_index = 0;
  size_t buffer_seek = 0;
  while (1) {
    //These values will be given to child tasks (FragmentRefine)
    int task_creation_num = 0;
    //Head chunk of current loop.
    chunk_t* head_chunk = NULL;
    //Head chunk of chunks, which are not refined.
    chunk_t* child_task_chunk_head = NULL;
    //This is for cut-off.
    int child_task_chunk_num = 0;
    //processing_chunk_head is a head of chunks which are now being refined, deduplicated and perhaps compressed.  
    chunk_t* processing_chunk_head = NULL;
    //Flag to distinguish between end of read and periodic synchronization.
    int read_all_flag = 0;
    mk_task_group;
    if(processed_chunk_head) {
      #if TASK_USE_XWRITE_BUFFER
        create_task0(spawn write_all_chunks_to_file_use_buffer(fd_out, processed_chunk_head, xwrite_buffer));
      #else
        create_task0(spawn write_all_chunks_to_file(fd_out, processed_chunk_head));
      #endif
    }
    while(1) {
      //Check how much data left over from previous iteration resp. create an initial chunk
      //Allocate a new chunk and create a new memory buffer
      size_t chunk_offset = (chunk_offset_index >= chunk_offsets_len) ? input_file_size : chunk_offsets[chunk_offset_index];
      if(chunk_offset == buffer_seek)
        //No data can be read.
        break;
      chunk_t* new_chunk = (chunk_t *)malloc(sizeof(chunk_t));
      //Setup chunk.
      {
        if(new_chunk==NULL) EXIT_TRACE("Memory allocation failed.\n");
        new_chunk->next = NULL;
        if(child_task_chunk_head == NULL)
          child_task_chunk_head = new_chunk;
        if(processing_chunk_head == NULL)
          processing_chunk_head = new_chunk;
        //Make sure that system supports new buffer size
        r = mbuffer_create(&new_chunk->uncompressed_data, chunk_offset - buffer_seek);
        if(r!=0) EXIT_TRACE("Unable to initialize memory buffer.\n");
        new_chunk->header.state = CHUNK_STATE_UNCOMPRESSED;
      }
      memcpy((unsigned char*)new_chunk->uncompressed_data.ptr, (unsigned char*)input_file_buffer + buffer_seek, chunk_offset - buffer_seek);
      //Update offsets.
      buffer_seek = chunk_offset;
      chunk_offset_index++;
      //Connect chunk to newchunk
      if(!head_chunk) {
        head_chunk = new_chunk;
      } else {
        head_chunk->next = new_chunk;
        head_chunk = new_chunk;
        child_task_chunk_num++;
        if(child_task_chunk_num >= TASK_CUTOFF_FRAGMENT) {
          //Now chunks is processed.
          create_task0(spawn FragmentRefine(child_task_chunk_head, child_task_chunk_num));
          child_task_chunk_head = new_chunk;
          child_task_chunk_num = 0;
          task_creation_num++;
        }
      }
      if(task_creation_num > TASK_FRAGMENT_SYNC_FREQUENCY)
        goto SYNCHRONIZE;
    }
    //end of read.
    read_all_flag = 1;
SYNCHRONIZE:
    //Process remaining chunks.
    //chunk becomes a tail.
    if(head_chunk && head_chunk->header.state != CHUNK_STATE_EMPTY) {
      assert(!head_chunk->next);
      child_task_chunk_num++;
    }
    //Process remaining chunks.
    call_task(spawn FragmentRefine(child_task_chunk_head, child_task_chunk_num));
    wait_tasks;
    processed_chunk_head = processing_chunk_head;
    if(read_all_flag == 1) {
      //Be sure to write all chunks.
      mk_task_group;
      #if TASK_USE_XWRITE_BUFFER
        call_task(spawn write_all_chunks_to_file_use_buffer(fd_out, processed_chunk_head, xwrite_buffer));
      #else
        call_task(spawn write_all_chunks_to_file(fd_out, processed_chunk_head));
      #endif
      wait_tasks;
      break;
    }
  }
  #if TASK_USE_XWRITE_BUFFER
    free(xwrite_buffer);
  #endif
  free(chunk_offsets);
  free(rabintab);
  free(rabinwintab);
  close(fd_out);
  cilk_void_return;
}
#else
void Fragment(int fd, void *input_file_buffer, size_t input_file_size) {
  cilk_begin;
  size_t preloading_buffer_seek = 0;
  int r;

  u32int * rabintab = (u32int*) malloc(256*sizeof rabintab[0]);
  u32int * rabinwintab = (u32int*) malloc(256*sizeof rabintab[0]);
  if(rabintab == NULL || rabinwintab == NULL) {
    EXIT_TRACE("Memory allocation failed.\n");
  }

  rf_win_dataprocess = 0;
  rabininit(rf_win_dataprocess, rabintab, rabinwintab);

  #if TASK_USE_XWRITE_BUFFER
    xwrite_buffer_t* xwrite_buffer = (xwrite_buffer_t*)malloc(sizeof(xwrite_buffer_t));
    xwrite_buffer->length = 0;
  #endif
  //Sanity check
  if(MAXBUF < 8 * ANCHOR_JUMP) {
    printf("WARNING: I/O buffer size is very small. Performance degraded.\n");
    fflush(NULL);
  }

  int fd_out = create_output_file(conf->outfile);

  //processed_chunk_head is a head of chunks which are ready to write to a file.
  //NOTE: processing_chunk_head and processed_chunk_head are not connected by links. 
  //processign_chunk_head is declared in the following while-loop. 
  chunk_t* processed_chunk_head = NULL;


  //read from input file / buffer
  while (1) {
    //This while-loop is for synchronization in an appropriate interval.
    chunk_t *chunk = NULL;
    //These values will be given to child tasks (FragmentRefine)
    int task_creation_num = 0;
    chunk_t* child_task_chunk_head = NULL;
    //This is for cut-off.
    int child_task_chunk_num = 0;
    //processing_chunk_head is a head of chunks which are now being refined, deduplicated and perhaps compressed.  
    chunk_t* processing_chunk_head = NULL;
    //Flag to distinguish between end of read and periodic synchronization.
    int read_all_flag = 0;
    mk_task_group;
    if(processed_chunk_head) {
      #if TASK_USE_XWRITE_BUFFER
        create_task0(spawn write_all_chunks_to_file_use_buffer(fd_out, processed_chunk_head, xwrite_buffer));
      #else
        create_task0(spawn write_all_chunks_to_file(fd_out, processed_chunk_head));
      #endif
    }
    while (1) {
      //This while-loop is for reading data.
      size_t bytes_left; //amount of data left over in last_mbuffer from previous iteration

      //Check how much data left over from previous iteration resp. create an initial chunk
      if(chunk != NULL) {
        bytes_left = chunk->uncompressed_data.n;
        //Expand chunk->uncompressed_data.
        mbuffer_t temp_buffer;
        mbuffer_create(&temp_buffer, MAXBUF+bytes_left);
        memcpy(temp_buffer.ptr, chunk->uncompressed_data.ptr, bytes_left);
        r = mbuffer_renew(&chunk->uncompressed_data, &temp_buffer);
        //No need to release temp_buffer
        assert(r == 0);
      } else {
        bytes_left = 0;
        //Allocate a new chunk and create a new memory buffer
        chunk = (chunk_t *)malloc(sizeof(chunk_t));
        if(chunk==NULL) EXIT_TRACE("Memory allocation failed.\n");
        chunk->next = NULL;
        if(child_task_chunk_head == NULL)
          child_task_chunk_head = chunk;
        if(processing_chunk_head == NULL)
          processing_chunk_head = chunk;
        //Make sure that system supports new buffer size
        if(MAXBUF+bytes_left > SSIZE_MAX) {
          EXIT_TRACE("Input buffer size exceeds system maximum.\n");
        }
        r = mbuffer_create(&chunk->uncompressed_data, MAXBUF);
        if(r!=0) {
          EXIT_TRACE("Unable to initialize memory buffer.\n");
        }
        chunk->header.state = CHUNK_STATE_UNCOMPRESSED;
      }
      //Read data until buffer full
      size_t bytes_read=0;
      if(conf->preloading) {
        assert(chunk->header.state == CHUNK_STATE_UNCOMPRESSED);
        size_t max_read = MIN(MAXBUF, input_file_size-preloading_buffer_seek);
        memcpy((unsigned char*)chunk->uncompressed_data.ptr+bytes_left, (unsigned char*)input_file_buffer+preloading_buffer_seek, max_read);
        bytes_read = max_read;
        preloading_buffer_seek += max_read;
      } else {
        while(bytes_read < MAXBUF) {
          r = read(fd, (unsigned char*)chunk->uncompressed_data.ptr+bytes_left+bytes_read, MAXBUF-bytes_read);
          if(r<0) switch(errno) {
            case EAGAIN:
              EXIT_TRACE("I/O error: No data available\n");break;
            case EBADF:
              EXIT_TRACE("I/O error: Invalid file descriptor\n");break;
            case EFAULT:
              EXIT_TRACE("I/O error: Buffer out of range\n");break;
            case EINTR:
              EXIT_TRACE("I/O error: Interruption\n");break;
            case EINVAL:
              EXIT_TRACE("I/O error: Unable to read from file descriptor\n");break;
            case EIO:
              EXIT_TRACE("I/O error: Generic I/O error\n");break;
            case EISDIR:
              EXIT_TRACE("I/O error: Cannot read from a directory\n");break;
            default:
              EXIT_TRACE("I/O error: Unrecognized error\n");break;
          }
          if(r==0) break;
          bytes_read += r;
        }
      }
      //No data left over from last iteration and also nothing new read in, simply clean up and quit
      if(bytes_left + bytes_read == 0) {
        mbuffer_free(&chunk->uncompressed_data);
        chunk->header.state = CHUNK_STATE_EMPTY;
        break;
      }
      //Shrink buffer to actual size
      if(bytes_left+bytes_read < chunk->uncompressed_data.n) {
        r = mbuffer_realloc(&chunk->uncompressed_data, bytes_left+bytes_read);
        assert(r == 0);
      }
      if(bytes_read == 0) {
        //current chunk is the last one.
        break;
      }
      //partition input block into large, coarse-granular chunks
      int split;
      do {
        split = 0;
        //Try to split the buffer at least ANCHOR_JUMP bytes away from its beginning
        if(ANCHOR_JUMP < chunk->uncompressed_data.n) {
          int offset = rabinseg((unsigned char*)chunk->uncompressed_data.ptr + ANCHOR_JUMP, chunk->uncompressed_data.n - ANCHOR_JUMP, rf_win_dataprocess, rabintab, rabinwintab);
          //Did we find a split location?
          if(offset == 0) {
            //Split found at the very beginning of the buffer (should never happen due to technical limitations)
            assert(0);
            split = 0;
          } else if(offset + ANCHOR_JUMP < (int)chunk->uncompressed_data.n) {
            //Split found somewhere in the middle of the buffer
            //Allocate a new chunk and create a new memory buffer
            chunk_t * temp = (chunk_t *)malloc(sizeof(chunk_t));
            if(temp==NULL) EXIT_TRACE("Memory allocation failed.\n");

            //split it into two pieces
            r = mbuffer_split(&chunk->uncompressed_data, &temp->uncompressed_data, offset + ANCHOR_JUMP);
            if(r!=0) EXIT_TRACE("Unable to split memory buffer.\n");
            temp->header.state = CHUNK_STATE_UNCOMPRESSED;
            // [... -> chunk] becomes [... -> chunk -> temp]
            chunk->next = temp;
            temp->next = NULL;
            //prepare for next iteration
            chunk = temp;
            split = 1;
            //Here, check if chunks is sufficiently created.
            child_task_chunk_num++;
            if(child_task_chunk_num >= TASK_CUTOFF_FRAGMENT) {
              //Now chunks is processed.
              create_task0(spawn FragmentRefine(child_task_chunk_head, child_task_chunk_num));
              child_task_chunk_head = chunk;
              child_task_chunk_num = 0;
              task_creation_num++;
            }
          } else {
            //Due to technical limitations we can't distinguish the cases "no split" and "split at end of buffer"
            //This will result in some unnecessary (and unlikely) work but yields the correct result eventually.
            split = 0;
          }
        } else {
          //NOTE: We don't process the stub, instead we try to read in more data so we might be able to find a proper split.
          //      Only once the end of the file is reached do we get a genuine stub which will be enqueued right after the read operation.
          split = 0;
        }
      } while(split);
      if(task_creation_num > TASK_FRAGMENT_SYNC_FREQUENCY)
        goto SYNCHRONIZE;
    }
    //end of read.
    read_all_flag = 1;
SYNCHRONIZE:
    //Process remaining chunks.
    //chunk becomes a tail.
    if(chunk && chunk->header.state != CHUNK_STATE_EMPTY) {
      assert(!chunk->next);
      child_task_chunk_num++;
    }
    //Now chunks is processed.
    call_task(spawn FragmentRefine(child_task_chunk_head, child_task_chunk_num));
    wait_tasks;
    processed_chunk_head = processing_chunk_head;
    if(read_all_flag == 1) {
      //Be sure to write all chunks.
      mk_task_group;
      #if TASK_USE_XWRITE_BUFFER
        call_task(spawn write_all_chunks_to_file_use_buffer(fd_out, processed_chunk_head, xwrite_buffer));
      #else
        call_task(spawn write_all_chunks_to_file(fd_out, processed_chunk_head));
      #endif
      wait_tasks;
      break;
    }
  }
  #if TASK_USE_XWRITE_BUFFER
    free(xwrite_buffer);
  #endif
  free(rabintab);
  free(rabinwintab);
  close(fd_out);
  cilk_void_return;
}
#endif//TASK_PARALLEL_FRAGMENT
/*--------------------------------------------------------------------------*/
/* Encode
 * Compress an input stream
 *
 * Arguments:
 *   conf:    Configuration parameters
 *
 */
void Encode(config_t * _conf) {
  struct stat filestat;
  int32 fd;

  conf = _conf;

#ifdef ENABLE_STATISTICS
  init_stats(&g_stats);
#endif

  //Create chunk cache
  cache = hashtable_create(65536, hash_from_key_fn, keys_equal_fn, FALSE);
  if(cache == NULL) {
    printf("ERROR: Out of memory\n");
    exit(1);
  }

  assert(!mbuffer_system_init());

  /* src file stat */
  if (stat(conf->infile, &filestat) < 0)
      EXIT_TRACE("stat() %s failed: %s\n", conf->infile, strerror(errno));

  if (!S_ISREG(filestat.st_mode))
    EXIT_TRACE("not a normal file: %s\n", conf->infile);

#ifdef ENABLE_STATISTICS
  g_stats.total_input = filestat.st_size;
#endif //ENABLE_STATISTICS

  /* src file open */
  if((fd = open(conf->infile, O_RDONLY | O_LARGEFILE)) < 0)
    EXIT_TRACE("%s file open error %s\n", conf->infile, strerror(errno));

  //Load entire file into memory if requested by user
  void *preloading_buffer = NULL;
  size_t input_file_size = 0;
  void* input_file_buffer = NULL;
  if(conf->preloading) {
    size_t bytes_read=0;
    int r;

    preloading_buffer = malloc(filestat.st_size);
    if(preloading_buffer == NULL)
      EXIT_TRACE("Error allocating memory for input buffer.\n");

    //Read data until buffer full
    while((int)bytes_read < filestat.st_size) {
      r = read(fd, (unsigned char*)preloading_buffer+bytes_read, filestat.st_size-bytes_read);
      if(r<0) switch(errno) {
        case EAGAIN:
          EXIT_TRACE("I/O error: No data available\n");break;
        case EBADF:
          EXIT_TRACE("I/O error: Invalid file descriptor\n");break;
        case EFAULT:
          EXIT_TRACE("I/O error: Buffer out of range\n");break;
        case EINTR:
          EXIT_TRACE("I/O error: Interruption\n");break;
        case EINVAL:
          EXIT_TRACE("I/O error: Unable to read from file descriptor\n");break;
        case EIO:
          EXIT_TRACE("I/O error: Generic I/O error\n");break;
        case EISDIR:
          EXIT_TRACE("I/O error: Cannot read from a directory\n");break;
        default:
          EXIT_TRACE("I/O error: Unrecognized error\n");break;
      }
      if(r==0) break;
      bytes_read += r;
    }
    input_file_size = filestat.st_size;
    input_file_buffer = preloading_buffer;
  }

#ifdef ENABLE_PARSEC_HOOKS
  __parsec_roi_begin();
#endif
#if defined(ENABLE_TASK) && defined(TO_OMP)
  #pragma omp parallel
  #pragma omp single nowait
#endif
  Fragment(fd, input_file_buffer, input_file_size);
#ifdef ENABLE_PARSEC_HOOKS
  __parsec_roi_end();
#endif

  //clean up after preloading
  if(conf->preloading) {
    free(preloading_buffer);
  }

  /* clean up with the src file */
  if (conf->infile != NULL)
    close(fd);

  assert(!mbuffer_system_destroy());

  hashtable_destroy(cache, TRUE);

#ifdef ENABLE_STATISTICS
  /* dest file stat */
  if (stat(conf->outfile, &filestat) < 0)
      EXIT_TRACE("stat() %s failed: %s\n", conf->outfile, strerror(errno));
  g_stats.total_output = filestat.st_size;

  //Analyze and print statistics
  if(conf->verbose) print_stats(&g_stats);
#endif //ENABLE_STATISTICS
}

