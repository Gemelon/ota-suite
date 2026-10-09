#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "update_manifest.h"

typedef struct {
    bool configured;
    bool checking;
    bool available;
    bool stale;
    char version[32];
    char notes[513];
    char message[160];
    int64_t last_success;
    int64_t last_success_uptime;
} update_check_status_t;

esp_err_t update_check_init(void);
esp_err_t update_check_start(void);
void update_check_get_status(update_check_status_t *out);
/* Reserves an immutable snapshot; checks cannot replace it during installation. */
esp_err_t update_check_acquire(const char *expected_version, update_candidate_t *out);
void update_check_release(void);
