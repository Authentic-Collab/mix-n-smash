#pragma once

#include <stdbool.h>
#include "settings.h"

#define AUTH_TOKEN_LEN 32

typedef enum {
    AUTH_LOGIN_OK = 0,
    AUTH_LOGIN_WRONG,        /* wrong password */
    AUTH_LOGIN_TOO_SOON,     /* a wrong attempt was made less than 2 s ago */
    AUTH_LOGIN_NOT_SET,      /* no password of this role exists */
} auth_login_result_t;

void auth_init(void);
/** Check the password; on success writes a session token (32 hex chars). */
auth_login_result_t auth_login(auth_role_t role, const char *password, char token_out[AUTH_TOKEN_LEN + 1]);
void auth_logout(const char *token);
bool auth_valid(auth_role_t role, const char *token);
/** Drop every session of a role (after its password changed). */
void auth_revoke_all(auth_role_t role);
