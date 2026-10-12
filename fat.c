#include "fat.h"
#include "ata.h"
#include <stdint.h>
#include <stddef.h>

extern void  *memcpy(void*, const void*, size_t);
extern void  *memset(void*, int, size_t);
extern size_t strlen(const char*);
extern int    strcmp(const char*, const char*);
extern int    strncmp(const char*, const char*, size_t);
extern int    toupper(int);

typedef struct __attribute__((packed)) {
    uint8_t  jmp[3];
    char     oem[8];
    uint16_t bytes_per_sector;
    uint8_t  sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t  num_fats;
    uint16_t root_entries;
    uint16_t total_sectors_16;
    uint8_t  media;
    uint16_t fat_size_16;
    uint16_t sectors_per_track;
    uint16_t num_heads;
    uint32_t hidden_sectors;
    uint32_t total_sectors_32;
} fat16_bpb_t;

typedef struct __attribute__((packed)) {
    char     name[8];
    char     ext[3];
    uint8_t  attr;
    uint8_t  reserved;
    uint8_t  create_tenths;
    uint16_t create_time;
    uint16_t create_date;
    uint16_t access_date;
    uint16_t cluster_hi;
    uint16_t modify_time;
    uint16_t modify_date;
    uint16_t cluster_lo;
    uint32_t size;
} fat16_dirent_t;

#define ATTR_READONLY 0x01
#define ATTR_HIDDEN   0x02
#define ATTR_SYSTEM   0x04
#define ATTR_VOLUME   0x08
#define ATTR_DIR      0x10
#define ATTR_ARCH     0x20
#define ATTR_LFN      0x0F

/* ---- estado global ---- */
static int      g_mounted;
static uint16_t g_bps;              /* bytes por setor */
static uint8_t  g_spc;              /* setores por cluster */
static uint16_t g_reserved;         /* setores reservados */
static uint8_t  g_nfats;
static uint16_t g_root_entries;
static uint16_t g_fat_size;         /* setores por FAT */
static uint32_t g_fat_start;        /* LBA da primeira FAT */
static uint32_t g_root_start;       /* LBA do diretório raiz */
static uint32_t g_data_start;       /* LBA da área de dados */
static uint32_t g_total_clusters;
static uint32_t g_root_sectors;

/* ---- helpers ---- */
static uint32_t cluster_to_lba(uint16_t cluster){
    return g_data_start + (uint32_t)(cluster - 2) * g_spc;
}
static uint32_t root_lba(void){ return g_root_start; }

/* lê uma entrada da FAT (FAT16 = uint16) */
static uint16_t fat_get(uint16_t cluster){
    if (cluster < 2) return 0xFFFF;
    uint32_t offset = (uint32_t)cluster * 2;
    uint32_t sector = g_fat_start + offset / g_bps;
    uint32_t off    = offset % g_bps;
    uint8_t  buf[512];
    if (ata_read(sector, 1, buf) != 0) return 0xFFFF;
    uint16_t *p = (uint16_t*)(buf + off);
    return *p;
}

static int fat_set(uint16_t cluster, uint16_t val){
    if (cluster < 2) return -1;
    uint32_t offset = (uint32_t)cluster * 2;
    uint32_t sector = g_fat_start + offset / g_bps;
    uint32_t off    = offset % g_bps;
    uint8_t  buf[512];
    if (ata_read(sector, 1, buf) != 0) return -1;
    *(uint16_t*)(buf + off) = val;
    /* escreve em todas as FATs */
    for (int i=0;i<g_nfats;i++){
        uint32_t sec = g_fat_start + (uint32_t)i * g_fat_size + offset / g_bps;
        if (ata_write(sec, 1, buf) != 0) return -1;
    }
    return 0;
}

/* ---- mount ---- */
int fat_mount(void){
    g_mounted = 0;
    if (!ata_present()) return -1;

    uint8_t boot[512];
    if (ata_read(0, 1, boot) != 0) return -2;
    if (boot[510] != 0x55 || boot[511] != 0xAA) return -3;

    fat16_bpb_t *b = (fat16_bpb_t*)boot;
    if (b->bytes_per_sector != 512 && b->bytes_per_sector != 1024 &&
        b->bytes_per_sector != 2048 && b->bytes_per_sector != 4096) return -4;
    if (b->fat_size_16 == 0) return -4;      /* é FAT12 ou FAT32 */
    if (b->sectors_per_cluster == 0) return -4;

    g_bps            = b->bytes_per_sector;
    g_spc            = b->sectors_per_cluster;
    g_reserved       = b->reserved_sectors;
    g_nfats          = b->num_fats;
    g_root_entries   = b->root_entries;
    g_fat_size       = b->fat_size_16;
    g_fat_start      = g_reserved;
    g_root_start     = g_reserved + (uint32_t)g_nfats * g_fat_size;
    g_root_sectors   = ((uint32_t)g_root_entries * 32 + g_bps - 1) / g_bps;
    g_data_start     = g_root_start + g_root_sectors;

    uint32_t total_sectors = b->total_sectors_16 ? b->total_sectors_16 : b->total_sectors_32;
    if (total_sectors <= g_data_start) return -5;
    uint32_t data_sectors = total_sectors - g_data_start;
    g_total_clusters = data_sectors / g_spc;

    g_mounted = 1;
    return 0;
}

int fat_mounted(void){ return g_mounted; }

/* ---- comparação de nome 8.3 ---- */
static int name_match(const fat16_dirent_t *e, const char *name){
    /* monta "NAME.EXT" */
    char full[13]; int o=0;
    for (int i=0;i<8;i++){
        if (e->name[i] == ' ') break;
        full[o++] = e->name[i];
    }
    int has_ext = 0;
    for (int i=0;i<3;i++) if (e->ext[i] != ' ') { has_ext = 1; break; }
    if (has_ext){
        full[o++] = '.';
        for (int i=0;i<3;i++){
            if (e->ext[i] == ' ') break;
            full[o++] = e->ext[i];
        }
    }
    full[o] = 0;
    if (strlen(name) != (size_t)o) return 0;
    for (int i=0;i<o;i++)
        if (toupper((unsigned char)full[i]) != toupper((unsigned char)name[i])) return 0;
    return 1;
}

/* ---- listar diretório raiz ---- */
static int list_root(fat_list_cb cb, void *user){
    uint8_t sector[512];
    int per_sector = g_bps / 32;
    for (uint32_t s=0;s<g_root_sectors;s++){
        if (ata_read(root_lba() + s, 1, sector) != 0) return -1;
        fat16_dirent_t *e = (fat16_dirent_t*)sector;
        for (int i=0;i<per_sector;i++){
            if (e[i].name[0] == 0x00) return 0;         /* fim */
            if ((uint8_t)e[i].name[0] == 0xE5) continue; /* deletado */
            if (e[i].attr == ATTR_LFN) continue;         /* LFN */
            if (e[i].attr & ATTR_VOLUME) continue;       /* volume */

            fat_dirent_t d;
            d.attr = e[i].attr;
            d.is_dir = (e[i].attr & ATTR_DIR) ? 1 : 0;
            d.size = e[i].size;
            d.cluster = e[i].cluster_lo;
            int o=0;
            for (int k=0;k<8 && e[i].name[k]!=' ';k++) d.name[o++] = e[i].name[k];
            int he=0;
            for (int k=0;k<3;k++) if (e[i].ext[k]!=' ') { he=1; break; }
            if (he){
                d.name[o++]='.';
                for (int k=0;k<3 && e[i].ext[k]!=' ';k++) d.name[o++] = e[i].ext[k];
            }
            d.name[o]=0;
            cb(&d, user);
        }
    }
    return 0;
}

/* Para simplificar, só suportamos raiz. Path de subdiretório não implementado. */
int fat_ls(const char *path, fat_list_cb cb, void *user){
    if (!g_mounted) return -1;
    if (path && path[0] && strcmp(path, "/") != 0) return -2;   /* só raiz por enquanto */
    return list_root(cb, user);
}

/* ---- acha dirent por nome na raiz ---- */
static int find_root(const char *name, fat16_dirent_t *out, uint32_t *out_lba, int *out_idx){
    uint8_t sector[512];
    int per_sector = g_bps / 32;
    for (uint32_t s=0;s<g_root_sectors;s++){
        uint32_t lba = root_lba() + s;
        if (ata_read(lba, 1, sector) != 0) return -1;
        fat16_dirent_t *e = (fat16_dirent_t*)sector;
        for (int i=0;i<per_sector;i++){
            if (e[i].name[0] == 0x00) return 0;
            if ((uint8_t)e[i].name[0] == 0xE5) continue;
            if (e[i].attr == ATTR_LFN) continue;
            if (e[i].attr & ATTR_VOLUME) continue;
            if (name_match(&e[i], name)){
                if (out) *out = e[i];
                if (out_lba) *out_lba = lba;
                if (out_idx) *out_idx = i;
                return 1;
            }
        }
    }
    return 0;
}

/* ---- ler arquivo ---- */
int fat_read(const char *path, void *buf, int maxlen){
    if (!g_mounted) return -1;

    fat16_dirent_t e;
    int r = find_root(path, &e, 0, 0);
    if (r <= 0) return -2;
    if (e.attr & ATTR_DIR) return -3;

    uint32_t to_read = e.size;
    if (to_read > (uint32_t)maxlen) to_read = maxlen;

    uint16_t c = e.cluster_lo;
    uint32_t done = 0;
    uint8_t tmp[512];
    uint8_t *out = (uint8_t*)buf;

    while (c >= 2 && c < 0xFFF8 && done < to_read){
        uint32_t lba = cluster_to_lba(c);
        for (int s=0;s<g_spc && done < to_read;s++){
            if (ata_read(lba+s, 1, tmp) != 0) return -4;
            uint32_t n = g_bps;
            if (done + n > to_read) n = to_read - done;
            memcpy(out + done, tmp, n);
            done += n;
        }
        c = fat_get(c);
    }
    return (int)done;
}

/* ---- aloca um cluster livre ---- */
static uint16_t alloc_cluster(void){
    uint8_t buf[512];
    uint32_t fatsize_bytes = (uint32_t)g_fat_size * g_bps;
    uint32_t nclusters = fatsize_bytes / 2;
    for (uint32_t i=2;i<nclusters;i++){
        uint32_t sector = g_fat_start + (i*2) / g_bps;
        uint32_t off    = (i*2) % g_bps;
        if (ata_read(sector, 1, buf) != 0) return 0;
        uint16_t v = *(uint16_t*)(buf + off);
        if (v == 0){
            fat_set((uint16_t)i, 0xFFFF);
            return (uint16_t)i;
        }
    }
    return 0;
}

/* ---- grava dirent na raiz (acha slot livre) ---- */
static int write_root_dirent(const char *name, uint16_t cluster, uint32_t size, uint8_t attr){
    /* parseia nome 8.3 */
    char base[8]; char ext[3];
    memset(base, ' ', 8); memset(ext, ' ', 3);
    const char *p = name; int bi=0, ei=0; int in_ext=0;
    while (*p){
        if (*p == '.'){ in_ext = 1; p++; continue; }
        if (in_ext){
            if (ei < 3) ext[ei++] = toupper((unsigned char)*p);
        } else {
            if (bi < 8) base[bi++] = toupper((unsigned char)*p);
        }
        p++;
    }

    uint8_t sector[512];
    int per_sector = g_bps / 32;
    for (uint32_t s=0;s<g_root_sectors;s++){
        uint32_t lba = root_lba() + s;
        if (ata_read(lba, 1, sector) != 0) return -1;
        fat16_dirent_t *e = (fat16_dirent_t*)sector;
        for (int i=0;i<per_sector;i++){
            if (e[i].name[0] == 0x00 || (uint8_t)e[i].name[0] == 0xE5){
                memset(&e[i], 0, 32);
                memcpy(e[i].name, base, 8);
                memcpy(e[i].ext,  ext,  3);
                e[i].attr = attr;
                e[i].cluster_lo = cluster;
                e[i].cluster_hi = 0;
                e[i].size = size;
                if (ata_write(lba, 1, sector) != 0) return -2;
                return 0;
            }
        }
    }
    return -3;   /* raiz cheia */
}

/* ---- escrever arquivo (cria ou sobrescreve) ---- */
int fat_write(const char *path, const void *buf, int len){
    if (!g_mounted) return -1;

    /* se já existe, apaga */
    fat16_dirent_t old;
    int r = find_root(path, &old, 0, 0);
    if (r > 0) fat_unlink(path);

    uint16_t first_cluster = 0;
    uint16_t prev = 0;
    uint32_t done = 0;
    uint8_t tmp[512];
    const uint8_t *in = (const uint8_t*)buf;

    while (done < (uint32_t)len){
        uint16_t c = alloc_cluster();
        if (c == 0) return -2;
        if (first_cluster == 0) first_cluster = c;
        if (prev) fat_set(prev, c);
        prev = c;

        uint32_t lba = cluster_to_lba(c);
        for (int s=0;s<g_spc && done < (uint32_t)len;s++){
            uint32_t n = g_bps;
            if (done + n > (uint32_t)len) n = len - done;
            memset(tmp, 0, 512);
            memcpy(tmp, in + done, n);
            if (ata_write(lba+s, 1, tmp) != 0) return -3;
            done += n;
        }
    }

    return write_root_dirent(path, first_cluster, (uint32_t)len, ATTR_ARCH);
}

/* ---- apagar arquivo ---- */
int fat_unlink(const char *path){
    if (!g_mounted) return -1;

    uint8_t sector[512];
    int per_sector = g_bps / 32;
    for (uint32_t s=0;s<g_root_sectors;s++){
        uint32_t lba = root_lba() + s;
        if (ata_read(lba, 1, sector) != 0) return -1;
        fat16_dirent_t *e = (fat16_dirent_t*)sector;
        for (int i=0;i<per_sector;i++){
            if (e[i].name[0] == 0x00) return -2;
            if ((uint8_t)e[i].name[0] == 0xE5) continue;
            if (e[i].attr == ATTR_LFN) continue;
            if (name_match(&e[i], path)){
                /* libera clusters */
                uint16_t c = e[i].cluster_lo;
                while (c >= 2 && c < 0xFFF8){
                    uint16_t next = fat_get(c);
                    fat_set(c, 0);
                    c = next;
                }
                e[i].name[0] = (char)0xE5;
                if (ata_write(lba, 1, sector) != 0) return -1;
                return 0;
            }
        }
    }
    return -2;
}

/* mkdir não implementado nesta versão */
int fat_mkdir(const char *path){
    (void)path;
    return -1;
}

/* ---- df ---- */
int fat_df(uint32_t *total_kb, uint32_t *free_kb){
    if (!g_mounted) return -1;
    uint32_t free_c = 0;
    uint8_t buf[512];
    uint32_t fatsize_bytes = (uint32_t)g_fat_size * g_bps;
    uint32_t nclusters = fatsize_bytes / 2;
    if (nclusters > g_total_clusters + 2) nclusters = g_total_clusters + 2;

    for (uint32_t i=2;i<nclusters;i++){
        uint32_t sector = g_fat_start + (i*2) / g_bps;
        uint32_t off    = (i*2) % g_bps;
        if ((off == 0) && (ata_read(sector, 1, buf) != 0)) return -2;
        uint16_t v = *(uint16_t*)(buf + off);
        if (v == 0) free_c++;
    }

    uint32_t cluster_kb = ((uint32_t)g_spc * g_bps) / 1024;
    if (total_kb) *total_kb = g_total_clusters * cluster_kb;
    if (free_kb)  *free_kb  = free_c * cluster_kb;
    return 0;
}
