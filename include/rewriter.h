#ifndef _REWRITER_H
#define _REWRITER_H

#include <cstring>
#include <stdlib.h>
#include <fstream>
#include <iostream>
#include "zstd.h"
using namespace std;

#define FILE_IO_BUFFER_SIZE 1024 * 1024 * 16
#define DEFAULT_COMPRESSION_LEVEL 1
#define DEFAULT_TRUNCATE_ELEMS 100

struct GlobalData {
  const char *              dump_version;
  unsigned int        id_size;

  char *              input_file_name;
  char *              output_file_name;

  int                 input_file_fd;
  int                 output_file_fd;

  char *              input_mmap_start_pos;
  unsigned long long  input_file_length;

  ofstream *     output_file_stream;

  // char *              read_buffer;
  // unsigned int        read_buffer_content_length;
  // unsigned int        read_buffer_size;
  // unsigned int        reade_buffer_next_record_start_pos;

  char *              write_buffer;
  unsigned int        write_buffer_index;
  unsigned int        write_buffer_size;

  char *              compress_buffer;
  unsigned int        compress_buffer_index;
  unsigned int        compress_buffer_size;
  unsigned int        compress_level;
  unsigned int        truncate_elems;
};

struct RecordData {
  unsigned int        body_length;
  unsigned int        num_elements; // for primitive array Record;
  bool                record_finished;
  char                tag;
  char                primitive_type;
};
#endif
