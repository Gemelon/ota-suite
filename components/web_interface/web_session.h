/* SPDX-FileCopyrightText: 2026 Thomas Stoll
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#define WEB_SESSION_COUNT 8
#define WEB_SESSION_TOKEN_LENGTH 64
#define WEB_SESSION_COOKIE "ota_session"

typedef struct {
    char token[WEB_SESSION_TOKEN_LENGTH + 1];
    uint64_t expires_at;
} web_session_t;

bool web_session_equal(const char *a, const char *b);
web_session_t *web_session_find(web_session_t *sessions, const char *cookies, uint64_t now);
web_session_t *web_session_create(web_session_t *sessions, const char *token, uint64_t now, unsigned days);
