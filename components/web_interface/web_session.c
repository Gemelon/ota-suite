/* SPDX-FileCopyrightText: 2026 Thomas Stoll
 * SPDX-License-Identifier: Apache-2.0
 */

#include "web_session.h"

#include <stddef.h>
#include <string.h>

bool web_session_equal(const char *a, const char *b)
{
    const size_t length = strlen(b);
    if (strlen(a) != length) return false;
    unsigned char difference = 0;
    for (size_t i = 0; i < length; ++i) {
        difference |= (unsigned char)a[i] ^ (unsigned char)b[i];
    }
    return difference == 0;
}

web_session_t *web_session_find(web_session_t *sessions, const char *cookies, uint64_t now)
{
    char token[WEB_SESSION_TOKEN_LENGTH + 1] = {0};
    bool found = false;
    while (*cookies) {
        while (*cookies == ' ' || *cookies == '\t' || *cookies == ';') ++cookies;
        const char *end = strchr(cookies, ';');
        if (end == NULL) end = cookies + strlen(cookies);
        const size_t prefix = sizeof(WEB_SESSION_COOKIE "=") - 1;
        if ((size_t)(end - cookies) >= prefix &&
            strncmp(cookies, WEB_SESSION_COOKIE "=", prefix) == 0) {
            if (found || (size_t)(end - cookies) != prefix + WEB_SESSION_TOKEN_LENGTH) return NULL;
            memcpy(token, cookies + prefix, WEB_SESSION_TOKEN_LENGTH);
            found = true;
        }
        cookies = end;
    }
    if (!found) return NULL;
    for (size_t i = 0; i < WEB_SESSION_COUNT; ++i) {
        if (sessions[i].expires_at > now && web_session_equal(token, sessions[i].token)) {
            return &sessions[i];
        }
    }
    return NULL;
}

web_session_t *web_session_create(web_session_t *sessions, const char *token, uint64_t now, unsigned days)
{
    if ((days != 1 && days != 7) || strlen(token) != WEB_SESSION_TOKEN_LENGTH) return NULL;
    for (size_t i = 0; i < WEB_SESSION_TOKEN_LENGTH; ++i) {
        if (!((token[i] >= '0' && token[i] <= '9') || (token[i] >= 'a' && token[i] <= 'f'))) return NULL;
    }
    for (size_t i = 0; i < WEB_SESSION_COUNT; ++i) {
        if (sessions[i].expires_at <= now) {
            memcpy(sessions[i].token, token, WEB_SESSION_TOKEN_LENGTH + 1);
            sessions[i].expires_at = now + (uint64_t)days * 86400;
            return &sessions[i];
        }
    }
    return NULL;
}
