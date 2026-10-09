#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

bool update_version_parse(const char *text, uint32_t parts[3]);
bool update_version_newer(const char *candidate, const char *running);
bool update_manifest_url_valid(const char *url, bool allow_http);
bool update_image_url_valid(const char *manifest, const char *url,
                            const char *project, const char *version);
bool update_json_bounded(const char *body, size_t length);
