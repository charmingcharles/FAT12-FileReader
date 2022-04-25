#include <stdio.h>
#include <stdlib.h>
#include "file_reader.h"

int main() {
    char *filecontent = (char *)calloc(1565, 1);
    char expected_filecontent[1566] = "";
    struct disk_t* disk = disk_open_from_file("count_fat12_volume.img");
    struct volume_t* volume = fat_open(disk, 0);
    struct file_t* file = file_open(volume, "HUMANHIG.BIN");
    size_t size = file_read(filecontent, 1, 1946, file);
    printf("filecontent: %s\n", filecontent);
    free(filecontent);
    file_close(file);
    fat_close(volume);
    disk_close(disk);
    return 0;
}