#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include "file_reader.h"

int* fat_read(volume_t* volume, uint32_t first_sector){
    uint8_t *fat1_data = (uint8_t *) malloc(volume->super_sector->bytes_per_sector * volume->super_sector->sectors_per_fat);
    if(!fat1_data){
        errno = ENOMEM;
        return NULL;
    }
    uint8_t *fat2_data = (uint8_t *) malloc(volume->super_sector->bytes_per_sector * volume->super_sector->sectors_per_fat);
    if(!fat2_data){
        free(fat1_data);
        errno = ENOMEM;
        return NULL;
    }
    uint32_t sectors_per_root_dir = (volume->super_sector->root_dir_capacity * FAT_SFN_SIZE / volume->super_sector->bytes_per_sector);
    uint32_t volume_size = volume->super_sector->logical_sectors16 == 0 ? volume->super_sector->logical_sectors32 : volume->super_sector->logical_sectors16;
    uint32_t user_size = volume_size - (volume->super_sector->fat_count * volume->super_sector->sectors_per_fat) - volume->super_sector->reserved_sectors - sectors_per_root_dir;
    uint32_t number_of_cluster_per_volume = user_size / volume->super_sector->sectors_per_cluster;

    uint32_t fat1_position = first_sector + volume->super_sector->reserved_sectors;
    uint32_t  fat2_position = fat1_position + volume->super_sector->sectors_per_fat;
    int r1 = disk_read(volume->disk, fat1_position, fat1_data, volume->super_sector->sectors_per_fat);
    if(r1 != volume->super_sector->sectors_per_fat){
        free(fat1_data);
        free(fat2_data);
        errno = EINVAL;
        return NULL;
    }
    int r2 = disk_read(volume->disk, fat2_position, fat2_data, volume->super_sector->sectors_per_fat);
    if(r2 != volume->super_sector->sectors_per_fat){
        free(fat1_data);
        free(fat2_data);
        errno = EINVAL;
        return NULL;
    }
    if(memcmp(fat1_data, fat2_data, volume->super_sector->bytes_per_sector * volume->super_sector->sectors_per_fat) != 0){
        free(fat1_data);
        free(fat2_data);
        errno = EINVAL;
        return NULL;
    }
    free(fat2_data);
    int *buffer = malloc((number_of_cluster_per_volume + 2) * sizeof(int));
    for(uint32_t i = 0, j = 0; i < number_of_cluster_per_volume + 2; i += 2, j += 3){
        uint8_t b1 = fat1_data[j];
        uint8_t b2 = fat1_data[j + 1];
        uint8_t b3 = fat1_data[j + 2];

        int c1 = ((b2 & 0x0F) << 8) | b1;
        int c2 = ((b2 & 0xF0) >> 4) | (b3 << 4);
        buffer[i] = c1;
        buffer[i + 1] = c2;
    }
    free(fat1_data);
    return buffer;
}

void convert_name(const char* name, const char* extension, char* output){
    int name_counter = 0; int extension_counter = 0;
    for(int i = 0; i < 8; i++){
        if(name[i] != ' ')
            name_counter++;
        else
            break;
    }
    for(int i = 0; i < 3; i++){
        if(extension[i] != ' ')
            extension_counter++;
        else
            break;
    }
    int counter = name_counter + extension_counter;
    for(int i = 0; i < name_counter; i++){
        output[i] = name[i];
    }
    if(extension_counter > 0){
        output[name_counter] = '.';
        for(int i = 0; i < extension_counter; i++)
            output[name_counter + 1 + i] = extension[i];
        counter++;
    }
    output[counter] = '\0';
}

void convert_directory(const char* name, char* output){
    int counter = 0;
    for(int i = 0; i < 11; i++){
        if(name[i] != ' ')
            counter++;
        else
            break;
    }
    for(int i = 0; i < 11; i++){
        output[i] = name[i];
    }
    output[counter] = '\0';
}

fat_sfn_t* find_file(volume_t * volume, const char* file_name){
    fat_sfn_t* fat_sfn = (fat_sfn_t*)malloc(FAT_SFN_SIZE);
    if(!fat_sfn){
        errno = ENOMEM;
        return NULL;
    }
    fseek(volume->disk->file, ((volume->super_sector->fat_count * volume->super_sector->sectors_per_fat + volume->super_sector->reserved_sectors) * BYTES_PER_SECTOR), SEEK_SET);
    uint32_t sectors_per_root_dir = (volume->super_sector->root_dir_capacity * FAT_SFN_SIZE / volume->super_sector->bytes_per_sector);
    uint32_t volume_size = volume->super_sector->logical_sectors16 == 0 ? volume->super_sector->logical_sectors32 : volume->super_sector->logical_sectors16;
    uint32_t user_size = volume_size - (volume->super_sector->fat_count * volume->super_sector->sectors_per_fat) - volume->super_sector->reserved_sectors - sectors_per_root_dir;
    uint32_t number_of_cluster_per_volume = user_size / volume->super_sector->sectors_per_cluster;
    for(uint32_t i = 0; i < number_of_cluster_per_volume + 2; i++){
        unsigned int count = fread(fat_sfn, FAT_SFN_SIZE, 1, volume->disk->file);
        if(count != 1){
            free(fat_sfn);
            errno = EINVAL;
            return NULL;
        }
        char converted[13];
        convert_name(fat_sfn->name, fat_sfn->extension, converted);
        int check = memcmp(converted, file_name, strlen(file_name));
        if(!check && !(fat_sfn->attributes & DIRECTORY)){
            return fat_sfn;
        }
    }
    free(fat_sfn);
    errno = EINVAL;
    return NULL;
}

int read_bytes(volume_t * volume, void* buffer, int32_t first_sector, uint32_t offset, uint32_t bytes_to_read){
    if(!volume){
        errno = EFAULT;
        return -1;
    }
    uint32_t cluster_size = volume->super_sector->sectors_per_cluster * BYTES_PER_SECTOR;
    size_t read_size = (bytes_to_read == cluster_size ? bytes_to_read : bytes_to_read + (cluster_size - (bytes_to_read % cluster_size))) / BYTES_PER_SECTOR;
    uint8_t* temp_buffer = (uint8_t*)malloc(read_size * BYTES_PER_SECTOR);
    if(!temp_buffer){
        errno = ENOMEM;
        return -1;
    }
    int count = disk_read(volume->disk, first_sector, temp_buffer, read_size);
    if(count != (int)read_size){
        free(temp_buffer);
        errno = ENOENT;
        return -1;
    }
    memcpy(buffer, temp_buffer + offset, bytes_to_read);
    free(temp_buffer);
    return bytes_to_read;
}

int disk_read(disk_t* pdisk, int32_t first_sector, void* buffer, int32_t sectors_to_read){
    if(!pdisk || !buffer){
        errno = EFAULT;
        return -1;
    }
    fseek(pdisk->file, first_sector * BYTES_PER_SECTOR, SEEK_SET);
    uint32_t count = fread(buffer, BYTES_PER_SECTOR, sectors_to_read, pdisk->file);
    if(!count){
        errno = ENOENT;
        return -1;
    }
    return count;
}

void convert_entry(fat_sfn_t* read, dir_entry_t *entry){
    entry->is_directory = read->attributes & DIRECTORY;
    if(entry->is_directory)
        convert_directory(read->name, entry->name);
    else
        convert_name(read->name, read->extension, entry->name);
    entry->is_archived = read->attributes & ARCHIVED;
    entry->is_readonly = read->attributes & READ_ONLY_FILE;
    entry->is_system = read->attributes & SYSTEM_FILE;
    entry->is_hidden = read->attributes & HIDDEN_FILE;
    entry->size = read->file_size;
}

disk_t* disk_open_from_file(const char* volume_file_name){
    if(!volume_file_name){
        errno = EFAULT;
        return NULL;
    }
    FILE* file = fopen(volume_file_name, "r");
    if(!file){
        errno = ENOENT;
        return NULL;
    }
    disk_t* disk = (disk_t*)malloc(DISK_SIZE);
    if(!disk){
        errno = ENOMEM;
        return NULL;
    }
    disk->file = file;
    return disk;
}

int disk_close(disk_t* pdisk){
    if(!pdisk){
        errno = EFAULT;
        return -1;
    }
    fclose(pdisk->file);
    free(pdisk);
    return 0;
}

volume_t* fat_open(disk_t* pdisk, uint32_t first_sector){
    if(!pdisk){
        errno = EFAULT;
        return NULL;
    }
   volume_t* volume = (volume_t*)malloc(VOLUME_SIZE);
    if(!volume){
        errno = ENOMEM;
        return NULL;
    }
    fat_super_t* super_sector = (fat_super_t*)malloc(FAT_SUPER_SIZE);
    if(!super_sector){
        free(volume);
        errno = ENOMEM;
        return NULL;
    }
    if(disk_read(pdisk, first_sector, super_sector, 1) != 1){
        free(super_sector);
        free(volume);
        errno = EINVAL;
        return NULL;
    }
    if(super_sector->bytes_per_sector != BYTES_PER_SECTOR || (super_sector->fat_count != 1 && super_sector->fat_count != 2)){
        free(super_sector);
        free(volume);
        errno = EINVAL;
        return NULL;
    }
    volume->disk = pdisk;
    volume->super_sector = super_sector;
    int* fat_array = fat_read(volume, first_sector);
    if(!fat_array){
        free(volume->super_sector);
        free(volume);
        return NULL;
    }
    volume->fat_array = fat_array;
    return volume;
}

int fat_close(volume_t* pvolume){
    if(!pvolume){
        errno = EFAULT;
        return -1;
    }
    free(pvolume->fat_array);
    free(pvolume->super_sector);
    free(pvolume);
    return 0;
}

file_t* file_open(volume_t* pvolume, const char* file_name){
    if(!pvolume || !file_name){
        errno = EFAULT;
        return NULL;
    }
    file_t* file = (file_t*)malloc(FILE_SIZE);
    if(!file){
        errno = ENOMEM;
        return NULL;
    }
    fat_sfn_t* fat_sfn = find_file(pvolume, file_name);
    if(!fat_sfn){
        free(file);
        return NULL;
    }
    file->volume = pvolume;
    file->offset = 0;
    file->fat_sfn = fat_sfn;
    return file;
}

int file_close(file_t* stream){
    if(!stream)
        return 1;
    free(stream->fat_sfn);
    free(stream);
    return 0;
}

size_t file_read(void *ptr, size_t size, size_t nmemb, file_t *stream){
    if(!ptr || !stream){
        errno = EFAULT;
        return -1;
    }
    size_t true_size = size * nmemb;
    if(stream->offset == stream->fat_sfn->file_size && true_size > 0)
        return 0;
    int* fat_array = stream->volume->fat_array;
    int index = stream->fat_sfn->low_cluster_index;
    uint32_t root_dir = stream->volume->super_sector->fat_count * stream->volume->super_sector->sectors_per_fat + stream->volume->super_sector->reserved_sectors;
    uint32_t data_block = root_dir + (stream->volume->super_sector->root_dir_capacity * FAT_SFN_SIZE) / BYTES_PER_SECTOR;
    int check; size_t cluster_size = stream->volume->super_sector->sectors_per_cluster * BYTES_PER_SECTOR;
    size_t file_size = stream->fat_sfn->file_size; size_t read_size;
    size_t _used_size; size_t _data_size;
    size_t counter = 0; size_t _offset = stream->offset;
    uint8_t* temp_buffer = (uint8_t *)malloc(cluster_size);
    boolean stop = FALSE;
    while(TRUE) {
        uint32_t cur_data = data_block + (index - 2) * stream->volume->super_sector->sectors_per_cluster;
        _data_size = file_size > cluster_size ? cluster_size : file_size;
        _used_size = _data_size > true_size ? true_size : _data_size;
        if (_offset < cluster_size) {
            read_size = _used_size + _offset > cluster_size ? (cluster_size - _offset) : _used_size;
            size_t bytes_left = stream->fat_sfn->file_size - stream->offset;
            if (bytes_left < read_size) {
                read_size = bytes_left;
                stop = TRUE;
            }
            check = read_bytes(stream->volume, temp_buffer, cur_data, _offset, read_size);
            if (check == -1) {
                free(temp_buffer);
                errno = ERANGE;
                return -1;
            }
            memcpy((uint8_t *) ptr + counter, temp_buffer, read_size);
            if (stop) {
                stream->offset += check;
                free(temp_buffer);
                return 0;
            }
            if(read_size != _used_size){
                _offset = 0;
            }
            counter += check;
            file_size -= _data_size;
            true_size -= read_size;
        } else if (_offset >= cluster_size)
            _offset -= cluster_size;
        else
            _offset -= _used_size;
        if (file_size == 0 || true_size == 0) {
            break;
        }
        int value = fat_array[index];
        if (value > 0xFF8)
            break;
        index = value;
    }
    stream->offset += counter;
    free(temp_buffer);
    if(counter == 0)
        return counter;
    size_t result = counter / size;
    if(result == 0)
        return 1;
    return result;
}

int32_t file_seek(file_t* stream, int32_t offset, int whence){
    if(!stream){
        errno = EFAULT;
        return -1;
    }
    int move;
    if(whence == SEEK_SET)
        move = 0;
    else if(whence == SEEK_CUR)
        move = stream->offset;
    else if(whence == SEEK_END)
        move = stream->fat_sfn->file_size;
    else{
        errno = EINVAL;
        return -1;
    }
    if((uint32_t)(offset + move) > stream->fat_sfn->file_size){
        errno = ENXIO;
        return -1;
    }
    stream->offset = move + offset;
    return stream->offset;
}

dir_t* dir_open(volume_t* pvolume, const char* dir_path){
    if(!pvolume || !dir_path){
        errno = EFAULT;
        return NULL;
    }
    if(memcmp(dir_path, ROOT_DIR_PATH, strlen(ROOT_DIR_PATH)) != 0){
        errno = ENOENT;
        return NULL;
    }
    struct dir_t* dir = (dir_t*)malloc(DIR_SIZE);
    if(!dir){
        errno = ENOMEM;
        return NULL;
    }
    uint32_t root_dir_position = pvolume->super_sector->fat_count * pvolume->super_sector->sectors_per_fat + pvolume->super_sector->reserved_sectors;
    uint32_t sectors_per_root_dir = (pvolume->super_sector->root_dir_capacity * FAT_SFN_SIZE / pvolume->super_sector->bytes_per_sector);
    uint32_t volume_size = pvolume->super_sector->logical_sectors16 == 0 ? pvolume->super_sector->logical_sectors32 : pvolume->super_sector->logical_sectors16;
    uint32_t user_size = volume_size - (pvolume->super_sector->fat_count * pvolume->super_sector->sectors_per_fat) - pvolume->super_sector->reserved_sectors - sectors_per_root_dir;
    uint32_t number_of_cluster_per_volume = user_size / pvolume->super_sector->sectors_per_cluster;
    size_t bytes_to_read = (number_of_cluster_per_volume + 2) * FAT_SFN_SIZE;
    uint8_t* root_dir = (uint8_t*)malloc(bytes_to_read);
    if(!root_dir){
        free(dir);
        errno = ENOMEM;
        return NULL;
    }
    int check = read_bytes(pvolume, root_dir, root_dir_position, 0, bytes_to_read);
    if(check == -1){
        free(dir);
        free(root_dir);
        errno = ERANGE;
        return NULL;
    }
    dir->volume = pvolume;
    dir->offset = 0;
    dir->dir_path = dir_path;
    dir->root_dir = root_dir;
    dir->number_of_entries = pvolume->super_sector->root_dir_capacity;
    dir->root_dir_position = root_dir_position;
    return dir;
}

int dir_read(dir_t* pdir, dir_entry_t* pentry){
    if(!pdir || ! pentry){
        errno = EFAULT;
        return -1;
    }
    if(pdir->offset == pdir->number_of_entries)
        return 1;
    if(pdir->offset > pdir->number_of_entries){
        errno = ENXIO;
        return -1;
    }
    if(!memcmp(pdir->dir_path, ROOT_DIR_PATH, strlen(ROOT_DIR_PATH))){
        fat_sfn_t* temp_buffer = (fat_sfn_t *)malloc(FAT_SFN_SIZE);
        boolean stop = FALSE;
        do {
            if(pdir->offset == pdir->number_of_entries){
                free(temp_buffer);
                return 1;
            }
            if (!temp_buffer) {
                errno = ENOMEM;
                return -1;
            }
            int check = read_bytes(pdir->volume, temp_buffer, pdir->root_dir_position, pdir->offset * FAT_SFN_SIZE, FAT_SFN_SIZE);
            if (check != FAT_SFN_SIZE) {
                free(temp_buffer);
                errno = EIO;
                return -1;
            }
            convert_entry(temp_buffer, pentry);
            pdir->offset++;
            if(pentry->name[0] != -27 && pentry->name[0] != '\0' && pentry->name[1] != '\b')
                stop = TRUE;
        }while(!stop);
        free(temp_buffer);
    }
    else{
        errno = EIO;
        return -1;
    }
    if(pdir->offset >= pdir->number_of_entries)
        return 1;
    return 0;
}

int dir_close(dir_t* pdir){
    if(!pdir){
        errno = EFAULT;
        return -1;
    }
    free(pdir->root_dir);
    free(pdir);
    return 0;
}

