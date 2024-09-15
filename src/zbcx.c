#include "zbcx.h"

#include <stdlib.h>

zbcx_Options zbcx_options_init(void) {
	zbcx_Options options;
	zbcx_list_init(&options.includes);
	zbcx_list_init(&options.defines);
	zbcx_list_init(&options.library_links);
	options.source_file = NULL;
	options.object_file = NULL;
	// Default tab size for now is 4, since it's a common indentation size.
	options.tab_size = 4;
	options.acc_err = false;
	options.acc_stats = false;
	options.help = false;
	options.preprocess = false;
	options.write_asserts = true;
	options.show_version = false;
	options.cache.dir_path = NULL;
	options.cache.lifetime = -1;
	options.cache.enable = false;
	options.cache.clear = false;
	options.cache.print = false;
	return options;
}

void zbcx_options_deinit(zbcx_Options* options) {
	zbcx_list_deinit(&options->includes);
	zbcx_list_deinit(&options->defines);
	zbcx_list_deinit(&options->library_links);
}
