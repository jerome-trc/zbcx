#include <string.h>

#include "phase.h"
#include "../cache/cache.h"

static void run_phase( struct parse* parse );

void p_init( struct parse* parse, struct task* task, struct cache* cache ) {
   parse->task = task;
   // NOTE: parse->queue not initialized.
   parse->token = NULL;
   parse->source = NULL;
   parse->free_source = NULL;
   parse->source_entry = NULL;
   parse->source_entry_free = NULL;
   parse->last_id = 0;
   str_init( &parse->temp_text );
   str_init( &parse->token_presentation );
   parse->read_flags = READF_CONCATSTRINGS | READF_ESCAPESEQ;
   parse->concat_strings = false;
   parse->macro_head = NULL;
   parse->macro_free = NULL;
   parse->macro_param_free = NULL;
   parse->macro_expan = NULL;
   parse->macro_expan_free = NULL;
   parse->macro_arg_free = NULL;
   parse->ifdirc = NULL;
   parse->ifdirc_free = NULL;
   
   parse->preproc_pragmas.raw_define = false;
   parse->preproc_pragmas.raw_include = false;

   parse->line = 0;
   parse->column = 0;
   parse->cache = cache;
   parse->create_nltk = false;
   p_init_stream( parse );
   parse->ns = NULL;
   parse->ns_fragment = NULL;
   parse->local_vars = NULL;
   parse->lib = NULL;
   parse->main_lib_lines = 0;
   parse->included_lines = 0;
   parse->main_source_deinited = false;
   parse->variadic_macro_context = false;
   parse->include_history_entry = NULL;
   t_init_pos_id( &parse->zcommon.pos, INTERNALFILE_COMPILER );
   parse->zcommon.included = false;
   t_init_pos_id( &parse->wadauthor.pos, INTERNALFILE_COMPILER );
   parse->wadauthor.specified = false;
   parse->wadauthor.enabled = false;
}

void p_run( struct parse* parse ) {
   bool success = false;
   jmp_buf bail, *prev_bail = parse->task->bail;
   if ( setjmp( bail ) == 0 ) {
      parse->task->bail = &bail;
      run_phase( parse );
      success = true;
   }
   parse->task->bail = prev_bail;
   p_deinit_tk( parse );
   if ( ! success ) {
      t_bail( parse->task );
   }
}

static void run_phase( struct parse* parse ) {
   struct library* lib = t_add_library( parse->task );
   parse->lang_limits = t_get_lang_limits();
   t_create_builtins( parse->task );
   lib->wadauthor = false;
   parse->task->library_main = lib;
   parse->lib = lib;
   parse->ns_fragment = lib->upmost_ns_fragment;
   parse->ns = parse->ns_fragment->ns;
   p_define_predef_macros( parse );
   p_define_cmdline_macros( parse );
   p_create_cmdline_library_links( parse );
   p_load_main_source( parse );
   if ( parse->task->options->preprocess ) {
      p_preprocess( parse );
   }
   else {
      p_read_tk( parse );
      p_read_target_lib( parse );
   }
}

void p_diag( struct parse* parse, int flags, ... ) {
   va_list args;
   va_start( args, flags );
   parse->task->options->diag(parse->task->options->context, flags, &args);
   va_end( args );
}

void p_unexpect_diag( struct parse* parse ) {
   p_diag( parse, DIAG_POS_ERR | DIAG_SYNTAX, &parse->tk_pos,
      "unexpected %s", p_present_token_temp( parse, parse->tk ) );
}

void p_unexpect_item( struct parse* parse, struct pos* pos, enum tk tk ) {
   p_unexpect_name( parse, pos, p_present_token_temp( parse, tk ) );
}

void p_unexpect_name( struct parse* parse, struct pos* pos,
   const char* subject ) {
   p_diag( parse, DIAG_POS, ( pos ? pos : &parse->tk_pos ),
      "expecting %s here, or", subject );
}

void p_unexpect_last( struct parse* parse, struct pos* pos, enum tk tk ) {
   p_unexpect_last_name( parse, pos, p_present_token_temp( parse, tk ) );
}

void p_unexpect_last_name( struct parse* parse, struct pos* pos,
   const char* subject ) {
   p_diag( parse, DIAG_POS, ( pos ? pos : &parse->tk_pos ),
      "expecting %s here", subject );
}

void p_bail( struct parse* parse ) {
   t_bail( parse->task );
}
