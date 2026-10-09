#include "update_manifest.h"
#include "update_policy.h"
#include "cJSON.h"
#include <math.h>
#include <string.h>

bool update_manifest_parse(const char *body, size_t length, const char *manifest_url,
                           const char *project, const char *target, uint32_t max_size,
                           update_candidate_t *out)
{
    if (!length || length >= 2048 || !update_json_bounded(body, length)) return false;
    cJSON *root = cJSON_ParseWithLengthOpts(body, length + 1, NULL, true);
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return false;
    }
    const char *keys[] = {"project", "target", "version", "url", "size", "sha256", "notes"};
    cJSON *items[7] = {0};
    bool valid = true;
    for (cJSON *item = root->child; item; item = item->next) {
        int index = 0;
        while (index < 7 && strcmp(item->string, keys[index])) ++index;
        if (index == 7 || items[index]) { valid = false; break; }
        items[index] = item;
    }
    for (int i = 0; i < 7; ++i) {
        if (i == 4 ? !cJSON_IsNumber(items[i]) : !cJSON_IsString(items[i])) valid = false;
    }
    uint32_t parts[3];
    if (valid) {
        double size = items[4]->valuedouble;
        valid = isfinite(size) && size > 0 && size <= max_size && floor(size) == size &&
                !strcmp(items[0]->valuestring, project) && !strcmp(items[1]->valuestring, target) &&
                update_version_parse(items[2]->valuestring, parts) &&
                update_image_url_valid(manifest_url, items[3]->valuestring, project, items[2]->valuestring) &&
                strlen(items[5]->valuestring) == 64 && strlen(items[6]->valuestring) < sizeof(out->notes);
        if (valid) {
            for (unsigned i = 0; i < 32; ++i) {
                const char *digits = "0123456789abcdef";
                const char *a = strchr(digits, items[5]->valuestring[2 * i]);
                const char *b = strchr(digits, items[5]->valuestring[2 * i + 1]);
                if (!a || !b) { valid = false; break; }
                out->sha256[i] = (unsigned char)(((a - digits) << 4) | (b - digits));
            }
            out->size = (uint32_t)size;
            strcpy(out->version, items[2]->valuestring);
            strcpy(out->url, items[3]->valuestring);
            strcpy(out->notes, items[6]->valuestring);
        }
    }
    cJSON_Delete(root);
    return valid;
}
