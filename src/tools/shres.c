/*
  Builds MEGA65 system partition shared resource table from
  a set of files supplied on the command line.

*/


#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <errno.h>
#include <sys/mman.h>

#define METADATA_ENTRY_SIZE 256
#define SECTOR_SIZE 512

struct resource {
  unsigned int start_sector;
  unsigned int length_in_sectors;
  unsigned int length_in_bytes;
  unsigned int flags;
  char name[256];
  unsigned char *body;
};

#define MAX_RESOURCES 1024
struct resource resources[MAX_RESOURCES];

int resource_count=0;

void usage(void)
{
  fprintf(stderr,"usage: shres <output file> [file[=name[,flag[,flag ...]]]]\n");
  exit(-1);
}

int parse_flags(char *flag_string) {
  int flags = 0;
  char *token = strtok(flag_string, ",");
  while (token) {
    int flag_num = atoi(token);
    if (flag_num<0||flag_num>31) {
      fprintf(stderr,"ERROR: Flag numbers must be in range [0..31]\n");
      exit(-1);
    }
    flags |= 1<<flag_num;  // Extend this if needed
    token = strtok(NULL, ",");
  }
  return flags;
}

int prepare_resources(int argc, char **argv)
{
  for (int i = 0; i < argc; i++) {
    if (resource_count >= MAX_RESOURCES) {
      fprintf(stderr, "Too many resources (max %d)\n", MAX_RESOURCES);
      exit(1);
    }

    char *arg_copy = strdup(argv[i]);
    if (!arg_copy) {
      perror("strdup");
      exit(1);
    }

    char *filename = NULL;
    char *name = NULL;
    char *flag_string = NULL;

    char *eq = strchr(arg_copy, '=');

    if (eq) {
      // filename=name[,flags]
      *eq = '\0';
      filename = arg_copy;
      name = eq + 1;

      char *comma = strchr(name, ',');
      if (comma) {
        *comma = '\0';
        flag_string = comma + 1;
      }
    } else {
      // filename[,flags]
      char *comma = strchr(arg_copy, ',');
      if (comma) {
        *comma = '\0';
        filename = arg_copy;
        flag_string = comma + 1;
        name = filename;
      } else {
        filename = arg_copy;
        name = filename;
      }
    }

    // Validate file exists
    int fd = open(filename, O_RDONLY);
    if (fd < 0) {
      perror(filename);
      free(arg_copy);
      exit(1);
    }

    struct stat st;
    if (fstat(fd, &st) < 0) {
      perror("fstat");
      close(fd);
      free(arg_copy);
      exit(1);
    }

    if (st.st_size == 0) {
      fprintf(stderr, "Warning: %s is empty.\n", filename);
    }

    unsigned char *body = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (body == MAP_FAILED) {
      perror("mmap");
      close(fd);
      free(arg_copy);
      exit(1);
    }

    close(fd);

    struct resource *res = &resources[resource_count];
    res->start_sector = 0;
    res->length_in_bytes = st.st_size;
    res->length_in_sectors = (st.st_size + SECTOR_SIZE - 1) / SECTOR_SIZE;
    res->flags = flag_string ? parse_flags(flag_string) : 0;
    snprintf(res->name, sizeof(res->name), "%s", name);
    res->body = body;

    resource_count++;
    free(arg_copy);
  }

  return 0;
}



void dump_resources(void)
{
  printf("Resource Table (%d entries):\n", resource_count);
  printf("Idx  Start   Sectors  Bytes      Flags      Name\n");
  printf("---- ------- -------- ---------- ---------- -------------------------\n");

  for (int i = 0; i < resource_count; i++) {
    struct resource *res = &resources[i];
    printf("%-4d %-7u %-8u %-10u 0x%08x %s\n",
           i,
           res->start_sector,
           res->length_in_sectors,
           res->length_in_bytes,
           res->flags,
           res->name);
  }
}

int write_resources(char *out)
{
  FILE *f = fopen(out, "wb+");
  if (!f) {
    perror(out);
    exit(1);
  }

  // Step 1: Write header sector
  unsigned char header[SECTOR_SIZE] = {0};
  const char *magic = "MEGA65SHAREDRESOURCES";
  memcpy(header, magic, strlen(magic));

  // Store resource count at 0xF8–0xFB (little endian)
  header[0xF8] = resource_count & 0xFF;
  header[0xF9] = (resource_count >> 8) & 0xFF;
  header[0xFA] = (resource_count >> 16) & 0xFF;
  header[0xFB] = (resource_count >> 24) & 0xFF;

  // Version 1.0 (little endian)
  header[0xFC] = 1;
  header[0xFD] = 0;
  header[0xFE] = 0;
  header[0xFF] = 0;

  if (fwrite(header, 1, SECTOR_SIZE, f) != SECTOR_SIZE) {
    perror("writing header sector");
    fclose(f);
    exit(1);
  }

  // Step 2: Calculate where data starts
  int total_metadata_entries = resource_count + 1;  // +1 for terminator
  unsigned int metadata_sectors = total_metadata_entries;
  unsigned int data_start_sector = 1 + metadata_sectors;

  // Step 3: Write metadata entries (one per sector)
  for (int i = 0; i < resource_count; i++) {
    struct resource *res = &resources[i];
    unsigned char sector[SECTOR_SIZE] = {0};

    // Prepare metadata entry
    memcpy(sector + 0x00, &res->start_sector, sizeof(uint32_t));
    memcpy(sector + 0x04, &res->length_in_sectors, sizeof(uint32_t));
    memcpy(sector + 0x08, &res->length_in_bytes, sizeof(uint32_t));
    memcpy(sector + 0x0C, &res->flags, sizeof(uint32_t));

    size_t name_len = strnlen(res->name, 239);
    sector[0x10] = (unsigned char)name_len;
    memcpy(sector + 0x11, res->name, name_len);

    if (fwrite(sector, 1, SECTOR_SIZE, f) != SECTOR_SIZE) {
      perror("writing metadata sector");
      fclose(f);
      exit(1);
    }
  }

  // Step 4: Write final empty metadata entry (terminator)
  unsigned char empty_sector[SECTOR_SIZE] = {0};
  if (fwrite(empty_sector, 1, SECTOR_SIZE, f) != SECTOR_SIZE) {
    perror("writing terminator metadata sector");
    fclose(f);
    exit(1);
  }

  // Step 5: Write resource data
  unsigned int current_sector = data_start_sector;
  for (int i = 0; i < resource_count; i++) {
    struct resource *res = &resources[i];
    res->start_sector = current_sector;

    if (fwrite(res->body, 1, res->length_in_bytes, f) != res->length_in_bytes) {
      perror("writing resource body");
      fclose(f);
      exit(1);
    }

    unsigned int pad_bytes = (res->length_in_sectors * SECTOR_SIZE) - res->length_in_bytes;
    if (pad_bytes > 0) {
      unsigned char pad[SECTOR_SIZE] = {0};
      if (fwrite(pad, 1, pad_bytes, f) != pad_bytes) {
        perror("writing padding");
        fclose(f);
        exit(1);
      }
    }

    current_sector += res->length_in_sectors;
  }

  fclose(f);
  return 0;
}

int main(int argc, char **argv)
{

  if (argc<3) usage();
  
  prepare_resources(argc-2,&argv[2]);
  dump_resources();
  
  write_resources(argv[1]);
  
  return 0;
}
