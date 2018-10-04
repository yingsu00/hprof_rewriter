#include <assert.h>
#include <endian.h>
#include <fstream>
#include <iostream>

#include <fcntl.h>
#include <stdio.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include "rewriter.h"
using namespace std;

#define TRIM_ELEMENTS 100

long long compression_time_used = 0;

GlobalData *gdata;

static GlobalData *
get_gdata(void)
{
  static GlobalData data;

  /* Create initial default values */
  (void)memset(&data, 0, sizeof(GlobalData));
  return &data;
}

void
init(void)
{
  gdata = get_gdata();

  // gdata->read_buffer_size = FILE_IO_BUFFER_SIZE;
  // gdata->read_buffer = (char *)malloc(gdata->read_buffer_size);
  // gdata->read_buffer_content_length = 0;
  gdata->compress_buffer_size = ZSTD_COMPRESSBOUND(FILE_IO_BUFFER_SIZE * 2);
  gdata->compress_buffer = (char *)malloc(gdata->compress_buffer_size);
  gdata->compress_buffer_index = 0;
  gdata->compress_level = DEFAULT_COMPRESSION_LEVEL;
  gdata->truncate_elems = DEFAULT_TRUNCATE_ELEMS;
}

void zstd_compress(char * buf, int len)
{
  struct timeval start, end;
  gettimeofday(&start, NULL);

  size_t compressedBytes = ZSTD_compress(gdata->compress_buffer, gdata->compress_buffer_size, buf, len, gdata->compress_level);
  //printf("input buf size %d compressedBytes %d \n", len, compressedBytes);
  if (ZSTD_isError(compressedBytes)) {
    cerr << "compression error: " << ZSTD_getErrorName(compressedBytes) << endl;
  }

  // The compress buffer should never overflow
  if (compressedBytes > gdata->compress_buffer_size) {
    cerr << "compressed bytes is higher than original" << endl;
  }
  gdata->compress_buffer_index = compressedBytes;

  gettimeofday(&end, NULL);
  long long time_used = end.tv_sec - start.tv_sec;
  compression_time_used += time_used;
}

int open_input_file()
{
  struct stat sb;

  gdata->input_file_fd = open (gdata->input_file_name, O_RDWR | O_CREAT, (mode_t)0600);
  if (gdata->input_file_fd == -1) {
    fprintf (stderr, "Failed open input file %s \n", gdata->input_file_name);
    return -1;
  }

  if (fstat(gdata->input_file_fd, &sb) == -1) {
    fprintf (stderr, "can't get fstat info from input file %s \n", gdata->input_file_name);
    close(gdata->input_file_fd);
    return -1;
  }

  if (!S_ISREG (sb.st_mode)) {
    fprintf (stderr, "%s is not a file\n", gdata->input_file_name);
    close(gdata->input_file_fd);
    return -1;
  }

  gdata->input_file_length = sb.st_size;
  printf("Input file size %ld\n", sb.st_size);

  gdata->input_mmap_start_pos = (char *) mmap (0, sb.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, gdata->input_file_fd, 0);
  if (gdata->input_mmap_start_pos == MAP_FAILED) {
    fprintf (stderr, "Failed open input file %s \n", gdata->input_file_name);
    close(gdata->input_file_fd);
    return -1;
  }
  return 0;
}

int read_header()
{
  char * pos = gdata->input_mmap_start_pos;
  if (strcmp(pos, "JAVA PROFILE 1.0.1")) {
    gdata->dump_version = "1.0.1";
    printf("Version %s\n", gdata->dump_version);
    //pos += 19;
  }
  else if (strcmp(pos, "JAVA PROFILE 1.0.2")) {
    gdata->dump_version = "1.0.2";
    printf("Version %s\n", gdata->dump_version);
    //pos += 19;
  }
  else {
    fprintf (stderr, "Failed to mmap input file %s\n", gdata->input_file_name);
    cerr << "Input file " << gdata->input_file_name << " is not a valid dump file";
    close(gdata->input_file_fd);
    return -1;
  }

  gdata->id_size = be32toh(*(unsigned int*)(pos + 19));
  printf("Id size %u\n", gdata->id_size);
  return 0;
}

void rewrite_heapdump(char* pos, unsigned int length)
{
  char * end = pos + length;
  while (pos < end) {
    char tag = *pos;
    pos++;
    //printf("sub tag %x\n", tag);
    switch (tag) {
      case -1:    // 0xFF // Root unknown
      case 0x05:  // ROOT STICKY CLASS
      case 0x07:  // Root monitor used
        pos += gdata->id_size;
        break;

      case 0x01: // Root JNI global
        pos += 2 * gdata->id_size;
        break;

      case 0x02: // Root JNI local
      case 0x03: // ROOT JAVA FRAME
      case 0x08: // Root thread object
        pos += gdata->id_size + 8;
        break;

      case 0x04: // Root native stack
      case 0x06: // ROOT THREAD BLOCK
        pos += gdata->id_size + 4;
        break;

      case 0x20: // Class dump
        {
          pos += gdata->id_size * 7 + 8;

          // number of constants
          unsigned int num_constants;
          memcpy(&num_constants, pos, 2);
          num_constants = be16toh(num_constants);
          pos += 2;

          for (unsigned int i=0; i < num_constants; i++) {
            pos += 2;  //constant pool index
            char elem_type = *(pos);
            pos++;

            switch (elem_type) {
              case 2: pos +=  gdata->id_size; break; //obj
              case 4: pos += 1; break;  // boolean
              case 5: pos += 2; break;  //char
              case 6: pos += 4; break;  //float
              case 7: pos += 8; break;  //double
              case 8: pos += 1; break;  //byte
              case 9: pos += 2; break;  //short
              case 10: pos += 4; break;  //int
              case 11: pos += 8; break;  //long
            }
          }

          // statics
          unsigned int num_statics;
          memcpy(&num_statics, pos, 2);
          num_statics = be16toh(num_statics);
          pos += 2;
          //printf("Class dump number statics %d\n", num_statics);

          for (unsigned int i=0; i < num_statics; i++) {
            pos += gdata->id_size;
            char elem_type = *(pos);
            pos++;

            switch (elem_type) {
              case 2: pos +=  gdata->id_size; break; //obj
              case 4: pos += 1; break;  // boolean
              case 5: pos += 2; break;  //char
              case 6: pos += 4; break;  //float
              case 7: pos += 8; break;  //double
              case 8: pos += 1; break;  //byte
              case 9: pos += 2; break;  //short
              case 10: pos += 4; break;  //int
              case 11: pos += 8; break;  //long
            }
          }

          /* Instance fields */
          // statics
          unsigned int num_instances;
          memcpy(&num_instances, pos, 2);
          num_instances = be16toh(num_instances);
          pos += 2;
          pos += num_instances * (gdata->id_size + 1);
        }
        break;

      case 0x21: // Instance dump
        {
          pos += gdata->id_size * 2 + 4;
          unsigned int num_bytes;
          memcpy(&num_bytes, pos, 4);
          num_bytes = be32toh(num_bytes);
          pos += 4;
          pos += num_bytes;
        }
        break;

      case 0x22:
        // Object array dump
        {
          pos += gdata->id_size + 4;
          unsigned int num_elements;
          memcpy(&num_elements, pos, 4);
          num_elements = be32toh(num_elements);
          pos += 4;
          pos += (num_elements + 1) * gdata->id_size;
        }
        break;

      case 0x23:
        // Primitive array dump
        {
          pos += gdata->id_size + 4;
          unsigned int num_elements;
          memcpy(&num_elements, pos, 4);
          num_elements = be32toh(num_elements);
          pos += 4;
          char elem_type = *pos;
          pos++;
          unsigned int elem_size = 0;
          switch (elem_type) {
            case 2: elem_size = gdata->id_size; break; //obj
            case 4: elem_size = 1; break;  // boolean
            case 5: elem_size = 2; break;  //char
            case 6: elem_size = 4; break;  //float
            case 7: elem_size = 8; break;  //double
            case 8: elem_size = 1; break;  //byte
            case 9: elem_size = 2; break;  //short
            case 10: elem_size = 4; break;  //int
            case 11: elem_size = 8; break;  //long
            default: perror("Invalide prim array elem_size\n");
          }
          //printf("Prim array num_elements %d elem_type %x elem_size %d\n", num_elements, elem_type, elem_size);
          if (num_elements > 100) {
            memset(pos + 100 * elem_size, 0, elem_size * (num_elements - 100));
          }
          pos += num_elements * elem_size;
        }
        break;
      default: printf("invalid sub tag %2x\n", tag);
        break;
    }
  }
}

void rewrite()
{
  printf("gdata->input_mmap_start_pos %ld\n", gdata->input_mmap_start_pos);
  char * compress_start_pos = gdata->input_mmap_start_pos;
  char * pos = gdata->input_mmap_start_pos + 31; //skip header
  char * end_pos = gdata->input_mmap_start_pos + gdata->input_file_length;

  printf("gdata->input_mmap_start_pos %ld\n", gdata->input_mmap_start_pos);
  printf("end_pos %ld\n", end_pos);
  while (pos < end_pos) {
    char tag = *pos;
    pos += 5;  //skip time
    //unsigned int record_length = be32toh(*(unsigned int*)(pos));
    unsigned int record_length;
    memcpy(&record_length, pos, 4);
    record_length = be32toh(record_length);
    pos += 4;

    if (end_pos - pos < 3159364) {
      printf("tag %x record_length %u\n", tag, record_length);
    }
    //printf("tag %x record_length %u\n", tag, record_length);

    if (tag == 0x0C || tag == 0x1C) {
      printf("heap dump tag %x record_length %u\n", tag, record_length);
      rewrite_heapdump(pos, record_length);
      printf("finished rewrite_heapdump\n");
    }
    else if (tag == 0x2C) {
      printf("heap dump end tag %x record_length %u\n", tag, record_length);
      printf("end_pos %ld, pos %ld diff %ld compress_start_pos %ld diff %ld\n", end_pos, pos, end_pos - pos, compress_start_pos, pos - compress_start_pos);
    }

    pos = pos + record_length;
    //printf("new pos %ld \n", pos);


    //printf("\nnow compress.\n");
    while (compress_start_pos <= pos - FILE_IO_BUFFER_SIZE) {

      //printf("end_pos %ld, pos %ld diff %ld compress_start_pos %ld diff %ld\n", end_pos, pos, end_pos - pos, compress_start_pos, pos - compress_start_pos);
      zstd_compress(compress_start_pos, FILE_IO_BUFFER_SIZE);
      gdata->output_file_stream->write(gdata->compress_buffer, gdata->compress_buffer_index);
      compress_start_pos += FILE_IO_BUFFER_SIZE;
      //printf("compressed len %ld\n", gdata->compress_buffer_index);
      assert (gdata->compress_buffer_index < FILE_IO_BUFFER_SIZE);
    }
    //printf("pos %ld compress_start_pos %ld diff %ld\n", pos, compress_start_pos, pos - compress_start_pos);
  }
  //printf("end_pos %ld, pos %ld diff %ld compress_start_pos %ld diff %ld\n", end_pos, pos, end_pos - pos, compress_start_pos, pos - compress_start_pos);
  //  printf("Finished loop pos %ld\n", pos);

  if (compress_start_pos < pos) {
    zstd_compress(compress_start_pos, pos - compress_start_pos);
    gdata->output_file_stream->write(gdata->compress_buffer, gdata->compress_buffer_index);
  }
  printf("Finished rewrite\n");
}

int main(int argc, char** argv)
{
  if (argc < 3) {
    cout << "Must provide file name. Usage:\n";
    cout << "hprof-rewriter java.hprof\n";
  }
  //cout << argv[1];

  init();

  gdata->input_file_name = argv[1];
  //cout << gdata->input_file_name;
  gdata->output_file_name = argv[2];

  struct timeval start, end;
  gettimeofday(&start, NULL);

  if (open_input_file() == -1) {
    return -1;
  }
  read_header();

  std::ofstream os(gdata->output_file_name, std::ifstream::binary);
  gdata->output_file_stream = &os;
  rewrite();

  gettimeofday(&end, NULL);
  long long time_used = end.tv_sec - start.tv_sec;
  cout << "rewrite() Time used " << time_used << " seconds." << endl;
  cout << "zstd compress Time used " << compression_time_used << " seconds." << endl;

  if (close(gdata->input_file_fd) == -1) {
    perror("close");
    return 1;
  }

  if (munmap(gdata->input_mmap_start_pos, gdata->input_file_length) == -1) {
    perror ("mmap");
    return 1;
  }

  return 0;
}
