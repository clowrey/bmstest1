#include "nvm.h"

#include "../../app/model.h"
#include "sys/logging/logging.h"

#include "hardware/flash.h"
#include "pico/flash.h"
#include "pico/stdlib.h"
#include "../vendor/littlefs/lfs.h"
#include <string.h>
#include <stdio.h>

// Use the last part of the flash. Assuming 2MB flash, start 128KB before the end.
#define NVM_FLASH_OFFSET ((2048 - 128) * 1024)
#define FLASH_MMAP_ADDR (XIP_BASE + NVM_FLASH_OFFSET)

typedef struct {
    uint32_t addr;
    const uint8_t *data;
    size_t len;
} flash_op_params_t;

static void nvm_flash_program_helper(void *param) {
    flash_op_params_t *p = (flash_op_params_t *)param;
    flash_range_program(p->addr, p->data, p->len);
}

static void nvm_flash_erase_helper(void *param) {
    flash_op_params_t *p = (flash_op_params_t *)param;
    flash_range_erase(p->addr, p->len);
}

bool nvm_read(uint32_t offset, void *dest, size_t size) {
    if (offset + size > NVM_SIZE) return false;
    memcpy(dest, (const void *)(FLASH_MMAP_ADDR + offset), size);
    return true;
}

bool nvm_write(uint32_t offset, const void *src, size_t size) {
    if (offset + size > NVM_SIZE) return false;
    if ((offset % FLASH_PAGE_SIZE) != 0 || (size % FLASH_PAGE_SIZE) != 0) return false;
    flash_op_params_t p = {NVM_FLASH_OFFSET + offset, (const uint8_t *)src, size};
    return flash_safe_execute(nvm_flash_program_helper, &p, 100) == PICO_OK;
}

bool nvm_erase_all(void) {
    flash_op_params_t p = {NVM_FLASH_OFFSET, NULL, NVM_SIZE};
    return flash_safe_execute(nvm_flash_erase_helper, &p, 100) == PICO_OK;
}

// LittleFS callbacks
static int lfs_read(const struct lfs_config *c, lfs_block_t block, lfs_off_t off, void *buffer, lfs_size_t size) {
    uint32_t abs_off = block * c->block_size + off;
    memcpy(buffer, (const void *)(FLASH_MMAP_ADDR + abs_off), size);
    return LFS_ERR_OK;
}

static int lfs_prog(const struct lfs_config *c, lfs_block_t block, lfs_off_t off, const void *buffer, lfs_size_t size) {
    uint32_t abs_off = block * c->block_size + off;
    flash_op_params_t p = {NVM_FLASH_OFFSET + abs_off, (const uint8_t *)buffer, size};
    if (flash_safe_execute(nvm_flash_program_helper, &p, 100) != PICO_OK) return LFS_ERR_IO;
    return LFS_ERR_OK;
}

static int lfs_erase(const struct lfs_config *c, lfs_block_t block) {
    uint32_t abs_off = block * c->block_size;
    flash_op_params_t p = {NVM_FLASH_OFFSET + abs_off, NULL, c->block_size};
    if (flash_safe_execute(nvm_flash_erase_helper, &p, 100) != PICO_OK) return LFS_ERR_IO;
    return LFS_ERR_OK;
}

static int lfs_sync(const struct lfs_config *c) {
    (void)c;
    return LFS_ERR_OK;
}

// LittleFS configuration
static const struct lfs_config cfg = {
    .read  = lfs_read,
    .prog  = lfs_prog,
    .erase = lfs_erase,
    .sync  = lfs_sync,

    .read_size = 1,
    .prog_size = FLASH_PAGE_SIZE,
    .block_size = FLASH_SECTOR_SIZE,
    .block_count = NVM_SIZE / FLASH_SECTOR_SIZE,
    .cache_size = FLASH_PAGE_SIZE,
    .lookahead_size = 32,
    .block_cycles = 500,
};

static lfs_t lfs;

int update_boot_count(void) {
    int err = lfs_mount(&lfs, &cfg);
    if (err) {
        lfs_format(&lfs, &cfg);
        err = lfs_mount(&lfs, &cfg);
        if (err) return -1;
    }

    uint32_t boot_count = 0;
    lfs_file_t file;
    err = lfs_file_open(&lfs, &file, "boot_count", LFS_O_RDWR | LFS_O_CREAT);
    if (err) return -1;

    lfs_file_read(&lfs, &file, &boot_count, sizeof(boot_count));
    boot_count++;
    lfs_file_rewind(&lfs, &file);
    lfs_file_write(&lfs, &file, &boot_count, sizeof(boot_count));
    lfs_file_close(&lfs, &file);

    lfs_unmount(&lfs);
    return (int)boot_count;
}

bool nvm_save(const char *name, const void *data, size_t size) {
    int err = lfs_mount(&lfs, &cfg);
    if (err) return false;

    lfs_file_t file;
    err = lfs_file_open(&lfs, &file, name, LFS_O_WRONLY | LFS_O_CREAT);
    if (err) return false;

    lfs_file_write(&lfs, &file, data, size);
    lfs_file_close(&lfs, &file);

    lfs_unmount(&lfs);
    return true;
}

bool nvm_load(const char *name, void *data, size_t size) {
    int err = lfs_mount(&lfs, &cfg);
    if (err) return false;

    lfs_file_t file;
    err = lfs_file_open(&lfs, &file, name, LFS_O_RDONLY);
    if (err) return false;

    lfs_file_read(&lfs, &file, data, size);
    lfs_file_close(&lfs, &file);

    lfs_unmount(&lfs);
    return true;
}

// typedef struct __attribute__((packed)) {
//     uint32_t version;

//     // ADS1115 voltage calibration
//     int32_t battery_voltage_mul;
//     int32_t output_voltage_mul;
//     int32_t neg_contactor_mul;
//     int32_t neg_contactor_offset_mV;
//     int32_t pos_contactor_mul;

//     // Current calibration
//     int32_t current_offset;
// } calibration_data_t;

typedef struct {
    uint32_t version;
    bms_model_persistent_fast_t data;
} stored_persistent_fast_t;

bool nvm_save_persistent_fast(bms_model_t *model) {
    stored_persistent_fast_t data = {
        .version = BMS_MODEL_PERSISTENT_FAST_VERSION,
        .data = model->persistent_fast,
    };

    // Allow missing a deadline due to NVM write slowness
    model->ignore_missed_deadline = true;

    return nvm_save("fast", &data, sizeof(data));
}

bool nvm_load_persistent_fast(bms_model_t *model) {
    stored_persistent_fast_t data = {0};
    if(!nvm_load("fast", &data, sizeof(data))) {
        return false;
    }

    if (data.version != BMS_MODEL_PERSISTENT_FAST_VERSION) {
        return false;
    }
    
    model->persistent_fast = data.data;

    return true;
}

typedef struct {
    uint32_t version;
    bms_model_persistent_slow_t data;
} stored_persistent_slow_t;

bool nvm_save_persistent_slow(bms_model_t *model) {
    stored_persistent_slow_t data = {
        .version = BMS_MODEL_PERSISTENT_SLOW_VERSION,
        .data = model->persistent_slow,
    };

    // Allow missing a deadline due to NVM write slowness
    model->ignore_missed_deadline = true;

    return nvm_save("slow", &data, sizeof(data));
}

bool nvm_load_persistent_slow(bms_model_t *model) {
    stored_persistent_slow_t data = {0};
    if(!nvm_load("slow", &data, sizeof(data))) {
        return false;
    }

    if (data.version != BMS_MODEL_PERSISTENT_SLOW_VERSION) {
        return false;
    }

    model->persistent_slow = data.data;

    return true;
}

void nvm_schedule_save_persistent_fast(bms_model_t *model) {
    // TODO: prevent too many saves in a short time?

    // Schedule immediate save of 'fast' persistent data
    model->nvm_fast_saved_timestep = timestep() - (300000 / TIMESTEP_PERIOD_MS) + 1;
}

void nvm_tick(bms_model_t *model) {
    // Saving takes a few ms (or longer, if erase is needed).

    // Save 'fast' persistent data every 5 minutes
    if(timestep_every_ms(300000, &model->nvm_fast_saved_timestep)) {
        uint32_t start = time_us_32();
        nvm_save_persistent_fast(model);
        debug_printf("NVM: saved persistent fast data in %ld us\n", time_us_32() - start);
    }
}