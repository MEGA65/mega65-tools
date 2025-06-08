#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <openssl/evp.h>
#include <sys/stat.h>
#include <unistd.h>

#define SECTOR_SIZE 512
#define METADATA_ENTRY_SIZE 256
#define MAX_RESOURCES 1024

void print_sha1(unsigned char *hash) {
    for (int i = 0; i < 20; i++) {
        printf("%02x", hash[i]);
    }
}

int sha1_of_file(const char *filename, unsigned char *digest_out, size_t *size_out) {
    FILE *f = fopen(filename, "rb");
    if (!f) return -1;

    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    if (!ctx) {
        fclose(f);
        return -1;
    }

    if (!EVP_DigestInit_ex(ctx, EVP_sha1(), NULL)) {
        EVP_MD_CTX_free(ctx);
        fclose(f);
        return -1;
    }

    unsigned char buf[4096];
    size_t total = 0;
    size_t n;

    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        if (!EVP_DigestUpdate(ctx, buf, n)) {
            EVP_MD_CTX_free(ctx);
            fclose(f);
            return -1;
        }
        total += n;
    }

    unsigned int out_len;
    if (!EVP_DigestFinal_ex(ctx, digest_out, &out_len)) {
        EVP_MD_CTX_free(ctx);
        fclose(f);
        return -1;
    }

    EVP_MD_CTX_free(ctx);
    fclose(f);
    if (size_out) *size_out = total;
    return 0;
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <shared-resource-file>\n", argv[0]);
        return 1;
    }

    FILE *f = fopen(argv[1], "rb");
    if (!f) {
        perror(argv[1]);
        return 1;
    }

    unsigned char header[SECTOR_SIZE];
    if (fread(header, 1, SECTOR_SIZE, f) != SECTOR_SIZE) {
        fprintf(stderr, "Failed to read header\n");
        fclose(f);
        return 1;
    }

    if (memcmp(header, "MEGA65SHAREDRESOURCES", 22) != 0) {
        fprintf(stderr, "Invalid shared resource file format\n");
        fclose(f);
        return 1;
    }

    uint32_t declared_count = 0;
    memcpy(&declared_count, header + 0xF8, sizeof(uint32_t));

    if (declared_count > MAX_RESOURCES) {
        fprintf(stderr, "Declared resource count too large (%u)\n", declared_count);
        fclose(f);
        return 1;
    }

    // Prepare storage for metadata
    uint32_t start_sectors[MAX_RESOURCES];
    uint32_t length_in_sectors[MAX_RESOURCES];
    uint32_t length_in_bytes[MAX_RESOURCES];
    uint32_t flags_array[MAX_RESOURCES];
    char names[MAX_RESOURCES][240];

    // Parse metadata sectors
    int metadata_seen = 0;
    int terminator_seen = 0;

    for (int index = 0; index < MAX_RESOURCES; index++) {
        long metadata_offset = (1 + index) * SECTOR_SIZE;
        if (fseek(f, metadata_offset, SEEK_SET) != 0) {
            fprintf(stderr, "Failed to seek to metadata sector %d\n", index);
            break;
        }

        unsigned char sector[SECTOR_SIZE];
        if (fread(sector, 1, SECTOR_SIZE, f) != SECTOR_SIZE) {
            fprintf(stderr, "Failed to read metadata sector %d\n", index);
            break;
        }

        int all_zero = 1;
        for (int i = 0; i < METADATA_ENTRY_SIZE; i++) {
            if (sector[i] != 0) {
                all_zero = 0;
                break;
            }
        }

        if (all_zero) {
            terminator_seen = 1;
            break;
        }

        if (metadata_seen >= declared_count) {
            fprintf(stderr, "Error: More metadata entries than declared in header\n");
            fclose(f);
            return 1;
        }

        memcpy(&start_sectors[metadata_seen], sector + 0x00, sizeof(uint32_t));
        memcpy(&length_in_sectors[metadata_seen], sector + 0x04, sizeof(uint32_t));
        memcpy(&length_in_bytes[metadata_seen], sector + 0x08, sizeof(uint32_t));
        memcpy(&flags_array[metadata_seen], sector + 0x0C, sizeof(uint32_t));

        uint8_t name_len = sector[0x10];
        memcpy(names[metadata_seen], sector + 0x11, name_len);
        names[metadata_seen][name_len] = '\0';

        metadata_seen++;
    }

    printf("MEGA65 Shared Resource File: %s\n", argv[1]);
    printf("Declared resources: %u\n\n", declared_count);

    // Print metadata table
    printf("Resource Table (%d entries):\n", metadata_seen);
    printf("Idx  Start   Sectors  Bytes      Flags      Name\n");
    printf("---- ------- -------- ---------- ---------- -------------------------\n");
    for (int i = 0; i < metadata_seen; i++) {
        printf("%-4d %-7u %-8u %-10u 0x%08x %s\n",
               i,
               start_sectors[i],
               length_in_sectors[i],
               length_in_bytes[i],
               flags_array[i],
               names[i]);
    }

    printf("\nSHA1                                     Bytes      Check      Name\n");
    printf("--------------------------------------------------------------------------------\n");

    for (int i = 0; i < metadata_seen; i++) {
        long data_offset = (long)start_sectors[i] * SECTOR_SIZE;
        if (fseek(f, data_offset, SEEK_SET) != 0) {
            fprintf(stderr, "Seek error for resource %s\n", names[i]);
            continue;
        }

        unsigned char *body = malloc(length_in_bytes[i]);
        if (!body) {
            fprintf(stderr, "Memory allocation failed\n");
            continue;
        }

        if (fread(body, 1, length_in_bytes[i], f) != length_in_bytes[i]) {
            fprintf(stderr, "Failed to read data for resource %s\n", names[i]);
            free(body);
            continue;
        }

        unsigned char embedded_hash[20];
        EVP_MD_CTX *ctx = EVP_MD_CTX_new();
        EVP_DigestInit_ex(ctx, EVP_sha1(), NULL);
        EVP_DigestUpdate(ctx, body, length_in_bytes[i]);
        EVP_DigestFinal_ex(ctx, embedded_hash, NULL);
        EVP_MD_CTX_free(ctx);
        free(body);

        print_sha1(embedded_hash);
        printf("  %-10u ", length_in_bytes[i]);

        struct stat st;
        if (stat(names[i], &st) == 0 && S_ISREG(st.st_mode)) {
            unsigned char local_hash[20];
            size_t local_size;
            if (sha1_of_file(names[i], local_hash, &local_size) == 0) {
                if (local_size != length_in_bytes[i]) {
                    printf("SIZE MISM ");
                } else if (memcmp(embedded_hash, local_hash, 20) == 0) {
                    printf("MATCH     ");
                } else {
                    printf("MISMATCH  ");
                }
            } else {
                printf("ERROR     ");
            }
        } else {
            printf("NO FILE   ");
        }

        printf(" %s\n", names[i]);
    }

    if (metadata_seen != declared_count) {
        fprintf(stderr, "\nError: Mismatch between declared and actual metadata count (%d vs %u)\n",
                metadata_seen, declared_count);
        fclose(f);
        return 1;
    }

    if (!terminator_seen) {
        fprintf(stderr, "\nError: Missing terminator metadata sector after entries\n");
        fclose(f);
        return 1;
    }

    fclose(f);
    return 0;
}
