#ifndef FAT_H
#define FAT_H
#include <stdint.h>

#define FAT_NAME_MAX 13   /* 8.3 + null */

typedef struct {
    char     name[FAT_NAME_MAX];
    uint32_t size;
    uint8_t  attr;
    uint16_t cluster;
    int      is_dir;
} fat_dirent_t;

typedef void (*fat_list_cb)(const fat_dirent_t *e, void *user);

int  fat_mount(void);
int  fat_mounted(void);

int  fat_ls(const char *path, fat_list_cb cb, void *user);
int  fat_read(const char *path, void *buf, int maxlen);
int  fat_write(const char *path, const void *buf, int len);
int  fat_unlink(const char *path);
int  fat_mkdir(const char *path);

int  fat_df(uint32_t *total_kb, uint32_t *free_kb);

#endif
