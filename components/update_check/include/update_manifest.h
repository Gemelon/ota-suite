#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

typedef struct {
    char version[32];
    char url[256];
    char notes[513];
    uint32_t size;
    unsigned char sha256[32];
} update_candidate_t;

bool update_manifest_parse(const char *body, size_t length, const char *manifest_url,
                           const char *project, const char *target, uint32_t max_size,
                           update_candidate_t *out);
