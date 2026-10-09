#include "update_policy.h"
#include <stdio.h>
#include <string.h>

bool update_version_parse(const char *text, uint32_t parts[3])
{
    if (!text || strlen(text) >= 32) return false;
    for (int i = 0; i < 3; ++i) {
        const char *start = text;
        uint32_t value = 0;
        while (*text >= '0' && *text <= '9') {
            unsigned digit = (unsigned)(*text++ - '0');
            if (value > (UINT32_MAX - digit) / 10) return false;
            value = value * 10 + digit;
        }
        if (text == start || (text - start > 1 && *start == '0')) return false;
        parts[i] = value;
        if (i < 2) {
            if (*text++ != '.') return false;
        } else if (*text) return false;
    }
    return true;
}

bool update_version_newer(const char *candidate, const char *running)
{
    uint32_t a[3], b[3];
    if (!update_version_parse(candidate, a) || !update_version_parse(running, b)) return false;
    for (int i = 0; i < 3; ++i) {
        if (a[i] != b[i]) return a[i] > b[i];
    }
    return false;
}

bool update_manifest_url_valid(const char *url, bool allow_http)
{
    const char *host = NULL;
    if (strncmp(url, "https://", 8) == 0) host = url + 8;
    else if (allow_http && strncmp(url, "http://", 7) == 0) host = url + 7;
    if (!host || strlen(url) >= 256) return false;
    const char *slash = strchr(host, '/');
    if (!slash || slash == host || strcmp(strrchr(url, '/'), "/firmware.json")) return false;
    for (const unsigned char *p = (const unsigned char *)host; *p; ++p) {
        if (*p <= 32 || *p >= 127 || strchr("@#?\\%", *p)) return false;
    }
    return strstr(slash, "//") == NULL && strstr(slash, "/../") == NULL &&
           strstr(slash, "/./") == NULL;
}

bool update_image_url_valid(const char *manifest, const char *url,
                            const char *project, const char *version)
{
    uint32_t parts[3];
    if (!update_version_parse(version, parts)) return false;
    const char *slash = strrchr(manifest, '/');
    if (!slash) return false;
    char expected[256];
    int length = snprintf(expected, sizeof(expected), "%.*s/%s-%s.bin",
                          (int)(slash - manifest), manifest, project, version);
    return length > 0 && (size_t)length < sizeof(expected) && strcmp(expected, url) == 0;
}

bool update_json_bounded(const char *body, size_t length)
{
    unsigned depth = 0;
    bool string = false, escaped = false;
    for (size_t i = 0; i < length; ++i) {
        unsigned char c = body[i];
        if (!c) return false;
        if (string) {
            if (c < 32) return false;
            if (escaped) {
                /* Reject encoded NULs before cJSON turns them into C strings. */
                if (c == 'u' && i + 4 < length && !memcmp(body + i + 1, "0000", 4)) return false;
                escaped = false;
            } else if (c == '\\') escaped = true;
            else if (c == '"') string = false;
        } else if (c == '"') string = true;
        else if (c == '{' || c == '[') {
            if (++depth > 2) return false;
        } else if (c == '}' || c == ']') {
            if (!depth) return false;
            --depth;
        }
    }
    return !string && !depth;
}
