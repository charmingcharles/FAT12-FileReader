#ifndef PLIKSYS_FILE_READER_H
#define PLIKSYS_FILE_READER_H

#include <stdint.h>
#include <errno.h>

#define BYTES_PER_SECTOR 512

typedef uint8_t boolean;

#define FALSE 0
#define TRUE 1

typedef struct disk_t{
    FILE* file;
} __attribute__(( packed )) disk_t;

#define DISK_SIZE sizeof(disk_t)

typedef struct fat_super_t{
    uint8_t jump_code[3];
    char oem_name[8];

    uint16_t bytes_per_sector;
    uint8_t sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t fat_count;
    uint16_t root_dir_capacity;
    uint16_t logical_sectors16;
    uint8_t media_type;
    uint16_t sectors_per_fat;

    uint32_t chs_sectors_per_track;

    uint32_t hidden_sectors;
    uint32_t logical_sectors32;

    uint16_t media_id;
    uint8_t chs_head;

    uint32_t serial_number;

    char label[11];
    char f_sid[8];

    uint8_t boot_code[448];
    uint16_t magic; // 55 aa
}__attribute__(( packed )) fat_super_t;

#define FAT_SUPER_SIZE sizeof(fat_super_t)

typedef struct volume_t{
    fat_super_t* super_sector;
    int* fat_array;
    disk_t *disk;
} __attribute__(( packed )) volume_t;

#define VOLUME_SIZE sizeof(volume_t)

typedef enum{
    READ_ONLY_FILE = 0x01,
    HIDDEN_FILE = 0x02,
    SYSTEM_FILE = 0x04,
    VOLUME_LABEL = 0x08,
    DIRECTORY = 0x10,
    ARCHIVED = 0x20,
    LONG_FILE_NAME = 0x0F
} __attribute__(( packed )) fat_attribute_t;

typedef struct fat_sfn_t{
    char name[8];
    char extension[3];
    fat_attribute_t attributes;

    uint16_t reserved;
    uint16_t creation_time;
    uint16_t creation_date;
    uint16_t last_access_date;

    uint16_t high_cluster_index;

    uint16_t last_write_time;
    uint16_t last_write_date;

    uint16_t low_cluster_index;
    uint32_t file_size;
} __attribute__(( packed )) fat_sfn_t;

#define FAT_SFN_SIZE sizeof(fat_sfn_t)

typedef struct file_t{
    fat_sfn_t* fat_sfn;
    uint32_t offset;
    volume_t* volume;
} __attribute__(( packed )) file_t;

#define FILE_SIZE sizeof(file_t)

typedef struct dir_entry_t{
    char name[13];
    size_t size;
    boolean is_archived;
    boolean is_readonly;
    boolean is_system;
    boolean is_hidden;
    boolean is_directory;
} __attribute__(( packed )) dir_entry_t;

#define DIR_ENTRY_SIZE sizeof(dir_entry_t)

typedef struct dir_t{
    uint32_t offset;
    uint32_t number_of_entries;
    uint32_t root_dir_position;

    uint8_t* root_dir;
    const char* dir_path;

    volume_t* volume;
} __attribute__(( packed )) dir_t;

#define DIR_SIZE sizeof(dir_t)

#define ROOT_DIR_PATH "\\"
#define FAT12_DIRECTORY_MAX_CAPACITY 33554432

//MOJE FUNKCJE
int* fat_read(volume_t* volume, uint32_t first_sector);

void convert_name(const char* name, const char* extension, char* output);
void convert_directory(const char* name, char* output);

fat_sfn_t* find_file(volume_t * volume, const char* file_name);

int read_bytes(volume_t * volume, void* buffer, int32_t first_sector, uint32_t offset, uint32_t bytes_to_read);
int disk_read(disk_t* pdisk, int32_t first_sector, void* buffer, int32_t sectors_to_read);

void convert_entry(fat_sfn_t* read, dir_entry_t *entry);

//TESTY
disk_t* disk_open_from_file(const char* volume_file_name);
int disk_close(disk_t* pdisk);

volume_t* fat_open(disk_t* pdisk, uint32_t first_sector);
int fat_close(volume_t* pvolume);

file_t* file_open(volume_t* pvolume, const char* file_name);
int file_close(file_t* stream);
size_t file_read(void *ptr, size_t size, size_t nmemb, file_t *stream);
int32_t file_seek(file_t* stream, int32_t offset, int whence);

dir_t* dir_open(volume_t* pvolume, const char* dir_path);
int dir_read(dir_t* pdir, dir_entry_t* pentry);
int dir_close(dir_t* pdir);

#endif //PLIKSYS_FILE_READER_H
