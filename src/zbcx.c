#include "zbcx.h"

#include "cache/cache.h"
#include "codegen/phase.h"
#include "common.h"
#include "parse/phase.h"
#include "semantic/phase.h"
#include "task.h"

#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TAB_SIZE_MIN 1
#define TAB_SIZE_MAX 100

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

static const char* get_script_type_label(int type) {
	STATIC_ASSERT(SCRIPT_TYPE_NEXTFREENUMBER == SCRIPT_TYPE_REOPEN + 1);

	switch (type) {
	case SCRIPT_TYPE_CLOSED:
		return "closed";
	case SCRIPT_TYPE_OPEN:
		return "open";
	case SCRIPT_TYPE_RESPAWN:
		return "respawn";
	case SCRIPT_TYPE_DEATH:
		return "death";
	case SCRIPT_TYPE_ENTER:
		return "enter";
	case SCRIPT_TYPE_PICKUP:
		return "pickup";
	case SCRIPT_TYPE_BLUERETURN:
		return "bluereturn";
	case SCRIPT_TYPE_REDRETURN:
		return "redreturn";
	case SCRIPT_TYPE_WHITERETURN:
		return "whitereturn";
	case SCRIPT_TYPE_LIGHTNING:
		return "lightning";
	case SCRIPT_TYPE_UNLOADING:
		return "unloading";
	case SCRIPT_TYPE_DISCONNECT:
		return "disconnect";
	case SCRIPT_TYPE_RETURN:
		return "return";
	case SCRIPT_TYPE_EVENT:
		return "event";
	case SCRIPT_TYPE_KILL:
		return "kill";
	case SCRIPT_TYPE_REOPEN:
		return "reopen";
	default:
		return "";
	}
}

static void print_acc_stats(
	struct task* task,
	struct parse* parse,
	struct codegen* codegen
) {
	// acc includes imported functions in the function count. This can cause
	// confusion. We, instead, have two counts: one for functions in the library
	// being compiled, and another for imported functions.
	int imported_funcs = 0;
	zbcx_ListIter i;
	zbcx_list_iterate(&task->library_main->dynamic, &i);
	while (!zbcx_list_end(&i)) {
		struct library* lib = zbcx_list_data(&i);
		zbcx_ListIter k;
		zbcx_list_iterate(&lib->funcs, &k);
		while (!zbcx_list_end(&k)) {
			struct func* func = zbcx_list_data(&k);
			struct func_user* impl = func->impl;
			imported_funcs += (int)(impl->usage > 0);
			zbcx_list_next(&k);
		}
		zbcx_list_next(&i);
	}
	t_diag(
		task, DIAG_NONE,
		"\"%s\":\n"
		"  %d line%s (%d included)\n"
		"  %d function%s (%d imported)\n"
		"  %d script%s"
		"",
		task->library_main->file->path.value, parse->main_lib_lines,
		parse->main_lib_lines == 1 ? "" : "s", parse->included_lines,
		zbcx_list_size(&task->library_main->funcs),
		zbcx_list_size(&task->library_main->funcs) == 1 ? "" : "s", imported_funcs,
		zbcx_list_size(&task->library_main->scripts),
		zbcx_list_size(&task->library_main->scripts) == 1 ? "" : "s"
	);
	int script_counts[SCRIPT_TYPE_TOTAL] = { 0 };
	zbcx_list_iterate(&task->library_main->scripts, &i);
	while (!zbcx_list_end(&i)) {
		struct script* script = zbcx_list_data(&i);
		++script_counts[script->type];
		zbcx_list_next(&i);
	}
	for (int i = 0; i < ARRAY_SIZE(script_counts); ++i) {
		if (script_counts[i] > 0) {
			t_diag(
				task, DIAG_NONE, "    %d %s", script_counts[i], get_script_type_label(i)
			);
		}
	}
	int map_vars = 0;
	int world_vars = 0;
	int world_arrays = 0;
	int global_vars = 0;
	int global_arrays = 0;
	zbcx_list_iterate(&task->library_main->vars, &i);
	while (!zbcx_list_end(&i)) {
		struct var* var = zbcx_list_data(&i);
		switch (var->storage) {
		case STORAGE_MAP:
			++map_vars;
			break;
		case STORAGE_WORLD:
			if (var->desc == DESC_ARRAY) {
				++world_arrays;
			} else {
				++world_vars;
			}
			break;
		case STORAGE_GLOBAL:
			if (var->desc == DESC_ARRAY) {
				++global_arrays;
			} else {
				++global_vars;
			}
			break;
		default:
			break;
		}
		zbcx_list_next(&i);
	}
	t_diag(
		task, DIAG_NONE,
		"  %d global variable%s\n"
		"  %d world variable%s\n"
		"  %d map variable%s\n"
		"  %d global array%s\n"
		"  %d world array%s"
		"",
		global_vars, global_vars == 1 ? "" : "s", world_vars, world_vars == 1 ? "" : "s",
		map_vars, map_vars == 1 ? "" : "s", global_arrays, global_arrays == 1 ? "" : "s",
		world_arrays, world_arrays == 1 ? "" : "s"
	);
	t_diag(
		task, DIAG_NONE, "  object \"%s\": %d bytes", task->options->object_file,
		codegen->object_size
	);
}

static void print_cache(struct task* task, struct cache* cache) {
	if (cache) {
		cache_print(cache);
	} else {
		t_diag(task, DIAG_ERR, "attempting to print cache, but cache is not enabled");
		t_bail(task);
	}
}

static void clear_cache(struct task* task, struct cache* cache) {
	if (cache) {
		cache_clear(cache);
	} else {
		t_diag(task, DIAG_ERR, "attempting to clear cache, but cache is not enabled");
		t_bail(task);
	}
}

static void preprocess(struct task* task) {
	struct parse parse;
	p_init(&parse, task, NULL);
	p_run(&parse);
}

static void compile_mainlib(struct task* task, struct cache* cache) {
	struct parse parse;
	p_init(&parse, task, cache);
	p_run(&parse);
	struct semantic semantic;
	s_init(&semantic, task);
	s_test(&semantic);
	struct codegen codegen;
	c_init(&codegen, task);
	c_publish(&codegen);
	if (task->options->acc_stats) {
		print_acc_stats(task, &parse, &codegen);
	}
}

static void perform_selected_task(struct task* task, struct cache* cache) {
	if (task->options->cache.print) {
		print_cache(task, cache);
	} else if (task->options->cache.clear) {
		clear_cache(task, cache);
	} else if (task->options->preprocess) {
		preprocess(task);
	} else {
		compile_mainlib(task, cache);
	}
}

static void perform_task(struct task* task) {
	if (task->options->cache.enable) {
		struct cache cache;
		cache_init(&cache, task);
		cache_load(&cache);
		perform_selected_task(task, &cache);
		cache_close(&cache);
	} else {
		perform_selected_task(task, NULL);
	}
}

static bool perform_action(
	const zbcx_Options* options,
	jmp_buf* root_bail,
	struct str* compiler_dir
) {
	bool success = false;
	struct task task;
	t_init(&task, options, root_bail, compiler_dir);
	jmp_buf bail;
	if (setjmp(bail) == 0) {
		task.bail = &bail;
		perform_task(&task);
		success = true;
	}
	task.bail = root_bail;
	t_deinit(&task);
	return success;
}

bool zbcx_compile(const zbcx_Options* options) {
	mem_init(); // TODO: make this state local.
	jmp_buf bail;
	bool success = false;

	if (setjmp(bail) == 0) {
#error "TODO: NULL is a placeholder here"
		if (perform_action(options, &bail, NULL)) {
			success = true;
		}
	}

	mem_free_all();
	return success;
}
