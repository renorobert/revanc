/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#pragma once

#include <stdint.h>
#include <stdlib.h>

#include "macros.h"

#if defined(__x86_64__)
#include <x86-64/profile.h>
#elif defined(__aarch64__)
#include <arm64/profile.h>
#elif defined(__arm__)
#include <arm/profile.h>
#else
#error unsupported architecture.
#endif

void slat_profile_page_table(
	struct cache *cache,
	struct page_level *level,
	size_t n,
	size_t ncache_lines,
	size_t nrounds,
	volatile char *target);

void slat_profile_page_tables(
	struct cache *cache,
	struct page_format *fmt,
	size_t nrounds,
	volatile void *target,
	const char *output_path);
