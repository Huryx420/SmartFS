#include <stdio.h>
#include "storage.h"

int backup_create(const char *backup_file, int is_full) {
    printf("[Backup] Creating %s backup to %s...\n", is_full ? "FULL" : "INC", backup_file);
    return 0; // 接口占位
}