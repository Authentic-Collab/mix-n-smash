#pragma once

#include <stddef.h>
#include <stdint.h>

/** SHA-256 of a message, without any library dependency (FIPS 180-4). */
void sha256(const uint8_t *data, size_t len, uint8_t out[32]);
