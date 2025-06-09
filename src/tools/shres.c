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

int file_check(char *filename, unsigned long long *area_start, unsigned long long *area_length) {

  fprintf(stderr,"INFO: Attempting to open shared resource file or disk image '%s'\n",filename);
  
    int fd = open(filename, O_RDWR);
    if (fd < 0) {
        if (errno == ENOENT) {
            *area_length = 0xFFFFFFFFULL; // File doesn't exist yet
	    fprintf(stderr,"INFO: File does not exist.\n");
            return -1;
        } else {
	    fprintf(stderr,"INFO: Failed to open file.\n");
            perror("open");
            return -1;
        }
    }

    unsigned char sector[SECTOR_SIZE];

    // Read first sector (MBR or shared resource header)
    if (pread(fd, sector, SECTOR_SIZE, 0) != SECTOR_SIZE) {
        perror("reading sector 0");
        close(fd);
        return -1;
    }

    // Check for MBR signature
    if (sector[510] != 0x55 || sector[511] != 0xAA) {
        fprintf(stderr, "INFO: Not an MBR - assuming raw resource file\n");

        if (memcmp(sector, "MEGA65SHAREDRESOURCES", 22) != 0) {
            fprintf(stderr, "ERROR: Missing MEGA65 shared resource magic\n");
            close(fd);
            return -1;
        }

        *area_start = 0ULL;
        off_t length = lseek(fd, 0, SEEK_END);
        if (length < 0) {
            perror("lseek");
            close(fd);
            return -1;
        }
        *area_length = (unsigned long long)length;
        lseek(fd, 0, SEEK_SET);
        return fd;
    }

    // Scan partition table entries
    for (int i = 0; i < 4; i++) {
        int entry = 0x1BE + i * 16;
        uint8_t p_type = sector[entry + 4];

        if (p_type == 0x41) {  // MEGA65 system partition
            uint32_t syspart_start = *(uint32_t *)&sector[entry + 8];
            unsigned long long syspart_offset = (unsigned long long)syspart_start * SECTOR_SIZE;

	    fprintf(stderr,"INFO: Found MEGA65 SYSPART at sector 0x%08x\n",syspart_start);
	    
            if (pread(fd, sector, SECTOR_SIZE, syspart_offset) != SECTOR_SIZE) {
                perror("reading system partition header");
                close(fd);
                return -1;
            }

            if (memcmp(sector, "MEGA65SYS00", 11) != 0) {
                fprintf(stderr, "Invalid system partition magic\n");
                close(fd);
                return -1;
            }

            uint32_t rel_start = *(uint32_t *)&sector[0x30];
            uint32_t rel_size  = *(uint32_t *)&sector[0x34];

            *area_start  = (unsigned long long)(syspart_start + rel_start) * SECTOR_SIZE;
            *area_length = (unsigned long long)rel_size * SECTOR_SIZE;

	    fprintf(stderr,"INFO: Found MEGA65 SYSPART shared resource area of %lld MiB at sector %d of SYSPART.\n",
		    (*area_length)>>20, rel_start);
	    
            return fd;
        }
    }

    fprintf(stderr, "ERROR: No MEGA65 system partition found in MBR\n");
    close(fd);
    return -1;
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


int write_resources(char *out) {
    unsigned long long area_start = 0;
    unsigned long long area_length = 0;
    int fd = file_check(out, &area_start, &area_length);

    fprintf(stderr,"DEBUG: area_start=0x%012llx, area_length=0x%08llx\n",
            area_start, area_length);

    if (fd < 0) {
        if (area_length == 0xFFFFFFFFULL) {
            fprintf(stderr, "Error: Output file '%s' does not exist and cannot be created yet.\n", out);
        } else {
            fprintf(stderr, "Error: Cannot access output file '%s'.\n", out);
        }
        exit(1);
    }

    // Step 1: Prepare header sector
    unsigned char header[SECTOR_SIZE] = {0};
    const char *magic = "MEGA65SHAREDRESOURCES";
    memcpy(header, magic, strlen(magic));

    header[0xF8] = resource_count & 0xFF;
    header[0xF9] = (resource_count >> 8) & 0xFF;
    header[0xFA] = (resource_count >> 16) & 0xFF;
    header[0xFB] = (resource_count >> 24) & 0xFF;

    header[0xFC] = 1;
    header[0xFD] = 0;
    header[0xFE] = 0;
    header[0xFF] = 0;

    // Step 2: Calculate sector layout
    int total_metadata_entries = resource_count + 1;
    unsigned int metadata_sectors = total_metadata_entries;
    unsigned int data_start_sector = 1 + metadata_sectors;

    // Step 3: Assign start sectors to resources
    unsigned int current_sector = data_start_sector;
    for (int i = 0; i < resource_count; i++) {
        resources[i].start_sector = current_sector;
        current_sector += resources[i].length_in_sectors;
    }

    // Step 4: Validate bounds
    if (area_length > 0 && ((unsigned long long)current_sector * SECTOR_SIZE) > area_length) {
        fprintf(stderr, "Error: Resources exceed size of shared resource area (%u sectors = %llu bytes > %llu bytes)\n",
                current_sector, (unsigned long long)current_sector * SECTOR_SIZE, area_length);
        close(fd);
        exit(1);
    }

    // Step 5: Write header
    if (pwrite(fd, header, SECTOR_SIZE, area_start + 0) != SECTOR_SIZE) {
        perror("writing header sector");
        close(fd);
        exit(1);
    }

    // Step 6: Write metadata
    for (int i = 0; i < resource_count; i++) {
        struct resource *res = &resources[i];
        unsigned char sector[SECTOR_SIZE] = {0};

        memcpy(sector + 0x00, &res->start_sector, sizeof(uint32_t));
        memcpy(sector + 0x04, &res->length_in_sectors, sizeof(uint32_t));
        memcpy(sector + 0x08, &res->length_in_bytes, sizeof(uint32_t));
        memcpy(sector + 0x0C, &res->flags, sizeof(uint32_t));

        size_t name_len = strnlen(res->name, 239);
        sector[0x10] = (unsigned char)name_len;
        memcpy(sector + 0x11, res->name, name_len);

        if (pwrite(fd, sector, SECTOR_SIZE, area_start + ((1 + i) * SECTOR_SIZE)) != SECTOR_SIZE) {
            perror("writing metadata sector");
            close(fd);
            exit(1);
        }
    }

    // Step 7: Write terminator metadata sector
    unsigned char terminator[SECTOR_SIZE] = {0};
    if (pwrite(fd, terminator, SECTOR_SIZE,
               area_start + ((1 + resource_count) * SECTOR_SIZE)) != SECTOR_SIZE) {
        perror("writing terminator metadata sector");
        close(fd);
        exit(1);
    }

    // Step 8: Write resource data
    for (int i = 0; i < resource_count; i++) {
        struct resource *res = &resources[i];
        unsigned long long offset = area_start + ((unsigned long long)res->start_sector * SECTOR_SIZE);

        if (pwrite(fd, res->body, res->length_in_bytes, offset) != res->length_in_bytes) {
            perror("writing resource data");
            close(fd);
            exit(1);
        }

        unsigned int pad_bytes = (res->length_in_sectors * SECTOR_SIZE) - res->length_in_bytes;
        if (pad_bytes > 0) {
            unsigned char pad[SECTOR_SIZE] = {0};
            if (pwrite(fd, pad, pad_bytes, offset + res->length_in_bytes) != pad_bytes) {
                perror("writing padding");
                close(fd);
                exit(1);
            }
        }
    }

    close(fd);
    return 0;
}



int main(int argc, char **argv)
{

  if (argc<3) usage();
  
  prepare_resources(argc-2,&argv[2]);  
  write_resources(argv[1]);
  dump_resources();
  
  return 0;
}
