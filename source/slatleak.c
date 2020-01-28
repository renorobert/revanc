/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/types.h>

#include "args.h"
#include "buffer.h"
#include "cache.h"
#include "paging.h"
#include "profile.h"
#include "slatprofile.h"
#include "shuffle.h"
#include "sysfs.h"
#include "thread.h"
#include "macros.h"
#include "path.h"

#if defined(__i386__) || defined(__x86_64__)
#include <cpuid/cache.h>
#include <cpuid/cpuid.h>
#endif

int main(int argc, const char *argv[])
{
	struct args args = {
		.npages = { 128, 128, 128, 128 },
		.nentries = { SIZE_MAX, SIZE_MAX, SIZE_MAX, SIZE_MAX },
		.nrounds = 10,
		.line_size = 64,
		.nruns = 1,
		.output = "results"
	};
	struct buffer *buffer;
	struct cache *cache;
	struct page_format *page_format;
	int ret = -1;

	if (getuid() != 0 || geteuid() != 0) {
		dprintf("run program as root\n");
		return -1;
	}

	if (check_transparent_hugepages()) {
		dprintf("transparent huge pages seem to be enabled.\n"
				"please run 'echo \"never\" > /sys/kernel/mm/transparent_hugepage/"
				"enabled' as root.\n");
		return -1;
	}

	if (parse_args(&args, argc, argv) < 0) {
		show_usage(argv[0]);
		return -1;
	}

	detect_args(&args);

	if (!args.line_size) {
		dprintf("unable to detect line size, please specify the cache "
				"line size using --line-size.\n");
		return -1;
	}

	if (!args.cache_size) {
		dprintf("unable to detect cache size, please specify the "
				"cache size using --cache-size.\n");
		return -1;
	}

	if (!(page_format = get_page_format_from_args(&args))) {
		dprintf("unknown page format '%s', please use "
				"--list-page-formats to list all available page "
				"formats and specify the page format using "
				"--page-format.\n", args.page_format);
		return -1;
	}

	if (mkpath(args.output) < 0) {
		fprintf(stderr, "error: unable to create output directory on path '%s'!\n", args.output);
		return -1;
	}

	print_args(stdout, &args, page_format);

	if (init_profiler() < 0) {
		dprintf("unable to set up the profiler.\n");
		return -1;
	}

	if (pin_cpu(args.cpu) != 0) {
		dprintf("unable to pin the thread.\n");
		return -1;
	}

	if (!(buffer = new_buffer(page_format, (void *)args.target))) {
		dprintf("unable to allocate the target buffer.\n");
		return -1;
	}

	if (!(cache = new_cache(page_format, NULL, args.cache_size, args.line_size))) {
		dprintf("unable to allocate the eviction set.\n");
		goto err_del_buffer;
	}

#if defined(__i386__) || defined(__x86_64__)
	printf("Detected CPU name: %s\n\n", cpuid_get_cpu_name());
#endif

	srand(time(0));

	slat_profile_page_tables(cache, page_format, args.nrounds, buffer->data, args.output);

	ret = 0;
	del_cache(cache);

err_del_buffer:
	del_buffer(buffer);

	if (args.page_format)
		free(args.page_format);

	return ret;
}
