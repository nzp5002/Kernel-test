#ifndef ATA_H
#define ATA_H
#include <stdint.h>

int  ata_init(void);
int  ata_present(void);
uint32_t ata_sectors(void);
int  ata_read (uint32_t lba, uint8_t n, void *buf);
int  ata_write(uint32_t lba, uint8_t n, void *buf);
int  ata_flush(void);

#endif
