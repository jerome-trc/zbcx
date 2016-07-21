#include <string.h>
#include <stdarg.h>
#include <time.h>

#include "task.h"

enum { STRTABLE_MAXSIZE = 65536 };
enum {
   STRTABLE_STR,
   STRTABLE_SCRIPTNAME,
};

static void init_str_table( struct str_table* table );
static void open_logfile( struct task* task );
void show_diag( struct task* task, int flags, va_list* args );
void log_diag( struct task* task, int flags, va_list* args );
static void decode_pos( struct task* task, struct pos* pos, const char** file,
   int* line, int* column );
static bool identify_file( struct task* task, struct file_query* query );
static struct file_entry* add_file( struct task* task,
   struct file_query* query );
static struct file_entry* create_file_entry( struct task* task,
   struct file_query* query );
static void link_file_entry( struct task* task, struct file_entry* entry );
static struct indexed_string* intern_string( struct str_table* table,
   const char* value, int length );
static void init_ref( struct ref* ref, int type );

void t_init( struct task* task, struct options* options, jmp_buf* bail ) {
   task->options = options;
   task->err_file = NULL;
   task->bail = bail;
   task->file_entries = NULL;
   init_str_table( &task->str_table );
   init_str_table( &task->script_name_table );
   task->library_main = NULL;
   list_init( &task->libraries );
   list_init( &task->altern_filenames );
   task->last_id = 0;
   task->compile_time = time( NULL );
   gbuf_init( &task->growing_buffer );
   task->mnemonics = NULL;
   list_init( &task->runtime_asserts );
   task->root_name = t_create_name();
   task->array_name = t_create_name();
   struct func_intern* impl = mem_alloc( sizeof( *impl ) );
   impl->id = INTERN_FUNC_ARRAY_LENGTH;
   struct func* func = t_alloc_func();
   func->object.resolved = true;
   func->type = FUNC_INTERNAL;
   func->impl = impl;
   func->return_spec = SPEC_INT;
   struct name* name = t_extend_name( task->array_name, ".length" );
   func->name = name;
   name->object = &func->object;

   struct func_format* format_impl = mem_alloc( sizeof( *format_impl ) );
   format_impl->opcode = 0;
   func = t_alloc_func();
   func->object.resolved = true;
   func->type = FUNC_FORMAT;
   func->impl = format_impl;
   task->append_func = func;
   func->name = t_extend_name( task->root_name, ".append" );

   // Dummy nodes.
   struct expr* expr = t_alloc_expr();
   expr->pos.id = ALTERN_FILENAME_COMPILER;
   expr->pos.line = 0;
   expr->pos.column = 0;
   expr->spec = SPEC_RAW;
   struct literal* literal = t_alloc_literal();
   expr->root = &literal->node;
   expr->folded = true;
   task->dummy_expr = expr;

   t_create_builtins( task );
}

struct ns* t_alloc_ns( struct task* task, struct name* name ) {
   struct ns* ns = mem_alloc( sizeof( *ns ) );
   t_init_object( &ns->object, NODE_NAMESPACE );
   ns->parent = NULL;
   ns->name = name;
   ns->body = t_extend_name( name, "." );
   ns->body_types = t_extend_name( name, ".!t." );
   ns->unresolved = NULL;
   ns->unresolved_tail = NULL;
   ns->links = NULL;
   list_init( &ns->objects );
   list_init( &ns->private_objects );
   list_init( &ns->funcs );
   list_init( &ns->scripts );
   list_init( &ns->usings );
   ns->defined = false;
   ns->explicit_imports = false;
   return ns;
}

void t_append_unresolved_namespace_object( struct ns* ns,
   struct object* object ) {
   if ( ns->unresolved ) {
      ns->unresolved_tail->next = object;
   }
   else {
      ns->unresolved = object;
   }
   ns->unresolved_tail = object;
}

void init_str_table( struct str_table* table ) {
   table->head = NULL;
   table->head_sorted = NULL;
   table->tail = NULL;
   table->size = 0;
}

struct name* t_create_name( void ) {
   struct name* name = mem_slot_alloc( sizeof( *name ) );
   name->parent = NULL;
   name->next = NULL;
   name->drop = NULL;
   name->object = NULL;
   name->ch = 0;
   return name;
}

struct name* t_extend_name( struct name* parent, const char* extension ) {
   struct name* name = parent->drop;
   const char* ch = extension;
   while ( *ch ) {
      // Find the correct node to enter.
      struct name* prev = NULL;
      while ( name && name->ch < *ch ) {
         prev = name;
         name = name->next;
      }
      // Enter a new node if no node could be found.
      if ( ! name ) {
         name = t_create_name();
         name->parent = parent;
         name->ch = *ch;
         if ( prev ) {
            prev->next = name;
         }
         else {
            parent->drop = name;
         }
      }
      // Enter a new node if no node with the same character exists in the
      // parent node.
      else if ( name->ch != *ch ) {
         struct name* smaller_name = t_create_name();
         smaller_name->next = name;
         smaller_name->parent = parent;
         smaller_name->ch = *ch;
         name = smaller_name;
         if ( prev ) {
            prev->next = name;
         }
         else {
            parent->drop = name;
         }
      }
      parent = name;
      name = name->drop;
      ++ch;
   }
   return parent;
}

void t_copy_name( struct name* start, bool full, struct str* str ) {
   int length = 0;
   struct name* name = start;
   if ( full ) {
      while ( ! ( name->ch == '.' && name->parent->ch == '\0' ) ) {
         name = name->parent;
         ++length;
      }
   }
   else {
      while ( name->ch != '.' ) {
         name = name->parent;
         ++length;
      }
   }
   if ( str->buffer_length < length + 1 ) {
      str_grow( str, length + 1 );
   }
   str->length = length;
   str->value[ length ] = 0;
   name = start;
   while ( length ) {
      --length;
      str->value[ length ] = name->ch;
      name = name->parent;
   }
}

int t_full_name_length( struct name* name ) {
   int length = 0;
   while ( ! ( name->ch == '.' && name->parent->ch == '\0' ) ) {
      name = name->parent;
      ++length;
   }
   return length;
}

void t_init_object( struct object* object, int node_type ) {
   object->node.type = node_type;
   object->resolved = false;
   object->depth = 0;
   object->next = NULL;
   object->next_scope = NULL;
}

/*
void t_init_type_members( struct task* task ) {
   static struct {
      const char* name;
      int param;
      int type;
      int value;
      int id;
   } list[] = {
      { "length", 0, TYPE_STR, TYPE_INT, INTERN_FUNC_STR_LENGTH },
      { "at", 1, TYPE_STR, TYPE_INT, INTERN_FUNC_STR_AT },
   };
   for ( size_t i = 0; i < ARRAY_SIZE( list ); ++i ) {
      struct func* func = mem_slot_alloc( sizeof( *func ) );
      t_init_object( &func->object, NODE_FUNC );
      func->object.resolved = true;
      func->type = FUNC_INTERNAL;
      struct structure* type = get_type( task, list[ i ].type );
      struct name* name = t_extend_name( type->body, list[ i ].name );
      name->object = &func->object;
      func->name = name;
      func->params = NULL;
      func->return_spec = SPEC_ZINT;
      struct func_intern* impl = mem_alloc( sizeof( *impl ) );
      impl->id = list[ i ].id;
      func->impl = impl;
      func->min_param = list[ i ].param; 
      func->max_param = list[ i ].param;
      func->hidden = false;
   }
} */

int t_get_script_number( struct script* script ) {
   if ( script->number ) {
      return script->number->value;
   }
   else {
      return 0;
   }
}

void t_diag( struct task* task, int flags, ... ) {
   va_list args;
   va_start( args, flags );
   t_diag_args( task, flags, &args );
   va_end( args );
}

void t_diag_args( struct task* task, int flags, va_list* args ) {
   if ( task->options->acc_err ) {
      log_diag( task, flags, args );
   }
   else {
      show_diag( task, flags, args );
   }
}

void show_diag( struct task* task, int flags, va_list* args ) {
   if ( flags & DIAG_FILE ) {
      const char* file = NULL;
      int line = 0;
      int column = 0;
      struct pos* pos = va_arg( *args, struct pos* );
      decode_pos( task, pos, &file, &line, &column );
      printf( "%s", file );
      if ( flags & DIAG_LINE ) {
         printf( ":%d", line );
         if ( flags & DIAG_COLUMN ) {
            printf( ":%d", column );
         }
      }
      printf( ": " );
   }
   if ( flags & DIAG_SYNTAX ) {
      printf( "syntax " );
   }
   else if ( flags & DIAG_CUSTOM ) {
      printf( "custom " );
   }
   else if ( flags & DIAG_INTERNAL ) {
      printf( "internal " );
   }
   if ( flags & DIAG_ERR ) {
      printf( "error: " );
   }
   else if ( flags & DIAG_WARN ) {
      printf( "warning: " );
   }
   const char* format = va_arg( *args, const char* );
   vprintf( format, *args );
   printf( "\n" );
}

// Line format: <file>:<line>: <message>
void log_diag( struct task* task, int flags, va_list* args ) {
   if ( ! task->err_file ) {
      open_logfile( task );
   }
   if ( flags & DIAG_FILE ) {
      const char* file = NULL;
      int line = 0;
      int column = 0;
      struct pos* pos = va_arg( *args, struct pos* );
      decode_pos( task, pos, &file, &line, &column );
      fprintf( task->err_file, "%s:", file );
      if ( flags & DIAG_LINE ) {
         // For some reason, DB2 decrements the line number by one. Add one to
         // make the number correct.
         fprintf( task->err_file, "%d:", line + 1 );
      }
   }
   fprintf( task->err_file, " " );
   if ( flags & DIAG_SYNTAX ) {
      fprintf( task->err_file, "syntax " );
   }
   else if ( flags & DIAG_CUSTOM ) {
      printf( "custom " );
   }
   else if ( flags & DIAG_INTERNAL ) {
      printf( "internal " );
   }
   if ( flags & DIAG_ERR ) {
      fprintf( task->err_file, "error: " );
   }
   else if ( flags & DIAG_WARN ) {
      fprintf( task->err_file, "warning: " );
   }
   const char* message = va_arg( *args, const char* );
   vfprintf( task->err_file, message, *args );
   fprintf( task->err_file, "\n" );
}

// TODO: Log message to file.
void t_intern_diag( struct task* task, const char* file, int line,
   const char* format, ... ) {
   va_list args;
   va_start( args, format );
   printf( "%s:%d: internal compiler error: ", file, line );
   vprintf( format, args );
   printf( "\n" );
   va_end( args );
}

void t_unhandlednode_diag( struct task* task, const char* file, int line,
   struct node* node ) {
   t_intern_diag( task, file, line,
      "unhandled node type: %d", node->type );
}

void open_logfile( struct task* task ) {
   struct str str;
   str_init( &str );
   str_copy( &str, task->options->source_file,
      strlen( task->options->source_file ) );
   while ( str.length && str.value[ str.length - 1 ] != '/' &&
      str.value[ str.length - 1 ] != '\\' ) {
      str.value[ str.length - 1 ] = 0;
      --str.length;
   }
   str_append( &str, "acs.err" );
   task->err_file = fopen( str.value, "w" );
   if ( ! task->err_file ) {
      printf( "error: failed to load error output file: %s\n", str.value );
      t_bail( task );
   }
   str_deinit( &str );
}

void decode_pos( struct task* task, struct pos* pos, const char** file,
   int* line, int* column ) {
   const char* filename = "";
   // Negative IDs indicate an alternative filename.
   if ( pos->id < 0 ) {
      switch ( pos->id ) {
         int id;
      case ALTERN_FILENAME_COMPILER:
         filename = "compiler";
         break;
      default:
         id = ALTERN_FILENAME_INITIAL_ID;
         list_iter_t i;
         list_iter_init( &i, &task->altern_filenames );
         while ( ! list_end( &i ) ) {
            if ( id == pos->id ) {
               filename = list_data( &i );
               break;
            }
            --id;
            list_next( &i );
         }
         break;
      }
   }
   else {
      struct file_entry* entry = task->file_entries;
      while ( entry ) {
         if ( entry->id == pos->id ) {
            filename = entry->path.value;
            break;
         }
         entry = entry->next;
      }
   }
   // Instead of printing blank filenames, provide a default value.
   if ( filename[ 0 ] == '\0' ) {
      filename = "<no-filename>";
   }
   *file = filename;
   *line = pos->line;
   *column = pos->column;
   if ( task->options->one_column ) {
      ++*column;
   }
}

void t_decode_pos( struct task* task, struct pos* pos, const char** file,
   int* line, int* column ) {
   decode_pos( task, pos, file, line, column );
}

const char* t_decode_pos_file( struct task* task, struct pos* pos ) {
   const char* file;
   int line;
   int column;
   decode_pos( task, pos, &file, &line, &column );
   return file;
}
 
void t_bail( struct task* task ) {
   t_deinit( task );
   longjmp( *task->bail, 1 );
}

void t_deinit( struct task* task ) {
   if ( task->err_file ) {
      fclose( task->err_file );
   }
}

bool t_same_pos( struct pos* a, struct pos* b ) {
   return (
      a->id == b->id &&
      a->line == b->line &&
      a->column == b->column );
}

void t_init_file_query( struct file_query* query,
   struct file_entry* offset_file, const char* path ) {
   query->given_path = path;
   query->path = NULL;
   // NOTE: .fileid NOT initialized.
   query->file = NULL;
   query->offset_file = offset_file;
   query->success = false;
}

void t_find_file( struct task* task, struct file_query* query ) {
   struct str path;
   str_init( &path );
   query->path = &path;
   if ( identify_file( task, query ) ) {
      query->file = add_file( task, query );
      query->success = true;
   }
   str_deinit( &path );
}

bool identify_file( struct task* task, struct file_query* query ) {
   // Try path directly.
   str_append( query->path, query->given_path );
   if ( c_read_fileid( &query->fileid, query->path->value ) ) {
      return true;
   }
   // Try directory of current file.
   if ( query->offset_file ) {
      str_copy( query->path, query->offset_file->full_path.value,
         query->offset_file->full_path.length );
      c_extract_dirname( query->path );
      str_append( query->path, "/" );
      str_append( query->path, query->given_path );
      if ( c_read_fileid( &query->fileid, query->path->value ) ) {
         return true;
      }
   }
   // Try user-specified directories.
   list_iter_t i;
   list_iter_init( &i, &task->options->includes );
   while ( ! list_end( &i ) ) {
      char* include = list_data( &i ); 
      str_clear( query->path );
      str_append( query->path, include );
      str_append( query->path, "/" );
      str_append( query->path, query->given_path );
      if ( c_read_fileid( &query->fileid, query->path->value ) ) {
         return true;
      }
      list_next( &i );
   }
   return false;
}

struct file_entry* add_file( struct task* task, struct file_query* query ) {
   struct file_entry* entry = task->file_entries;
   while ( entry ) {
      if ( c_same_fileid( &query->fileid, &entry->file_id ) ) {
         return entry;
      }
      entry = entry->next;
   }
   return create_file_entry( task, query );
}

struct file_entry* create_file_entry( struct task* task,
   struct file_query* query ) {
   struct file_entry* entry = mem_alloc( sizeof( *entry ) );
   entry->next = NULL;
   entry->file_id = query->fileid;
   str_init( &entry->path );
   str_append( &entry->path, query->given_path );
   str_init( &entry->full_path );
   c_read_full_path( query->path->value, &entry->full_path );
   entry->id = task->last_id;
   ++task->last_id;
   link_file_entry( task, entry );
   return entry;
}

void link_file_entry( struct task* task, struct file_entry* entry ) {
   if ( task->file_entries ) {
      struct file_entry* prev = task->file_entries;
      while ( prev->next ) {
         prev = prev->next;
      }
      prev->next = entry;
   }
   else {
      task->file_entries = entry;
   }
}

struct library* t_add_library( struct task* task ) {
   struct library* lib = mem_alloc( sizeof( *lib ) );
   str_init( &lib->name );
   str_copy( &lib->name, "", 0 );
   list_init( &lib->vars );
   list_init( &lib->funcs );
   list_init( &lib->scripts );
   list_init( &lib->objects );
   list_init( &lib->namespaces );
   list_init( &lib->files );
   list_init( &lib->import_dircs );
   list_init( &lib->dynamic );
   list_init( &lib->dynamic_links );
   list_init( &lib->incomplete_vars );
   list_init( &lib->incomplete_funcs );
   lib->upmost_ns = t_alloc_ns( task, task->root_name );
   // root_name->object = &lib->upmost_ns->object;
   list_append( &lib->namespaces, lib->upmost_ns );
   lib->file_pos.line = 0;
   lib->file_pos.column = 0;
   lib->file_pos.id = 0;
   lib->id = list_size( &task->libraries );
   lib->format = FORMAT_LITTLE_E;
   lib->type_mode = TYPEMODE_WEAK;
   lib->lang = LANG_BCS;
   lib->importable = false;
   lib->imported = false;
   lib->encrypt_str = false;
   list_append( &task->libraries, lib );
   lib->file = NULL;
   lib->header = false;
   lib->compiletime = false;
   lib->wadauthor = false;
   return lib;
}

struct indexed_string* t_intern_string( struct task* task,
   const char* value, int length ) {
   return intern_string( &task->str_table, value, length );
}

struct indexed_string* intern_string( struct str_table* table,
   const char* value, int length ) {
   struct indexed_string* prev_string = NULL;
   struct indexed_string* string = table->head_sorted;
   while ( string ) {
      int result = strcmp( string->value, value );
      if ( result == 0 ) {
         break;
      }
      else if ( result > 0 ) {
         string = NULL;
         break;
      }
      prev_string = string;
      string = string->next_sorted;
   }
   // Allocate a new indexed-string when one isn't interned.
   if ( ! string ) {
      string = mem_alloc( sizeof( *string ) );
      string->value = value;
      string->length = length;
      string->index = table->size;
      string->index_runtime = -1;
      string->next = NULL;
      string->next_sorted = NULL;
      string->used = false;
      if ( table->head ) {
         table->tail->next = string;
      }
      else {
         table->head = string;
      }
      table->tail = string;
      // List sorted alphabetically.
      if ( prev_string ) {
         string->next_sorted = prev_string->next_sorted;
         prev_string->next_sorted = string;
      }
      else {
         string->next_sorted = table->head_sorted;
         table->head_sorted = string;
      }
      ++table->size;
   }
   return string;
}

struct indexed_string* t_intern_script_name( struct task* task,
   const char* value, int length ) {
   struct indexed_string* string = intern_string( &task->script_name_table,
      value, length );
   // Encode number of string table in index.
   if ( string->index < STRTABLE_MAXSIZE ) {
      string->index += STRTABLE_SCRIPTNAME * STRTABLE_MAXSIZE;
   }
   return string;
}

struct indexed_string* t_lookup_string( struct task* task, int index ) {
   struct str_table* table = index / STRTABLE_MAXSIZE ==
      STRTABLE_SCRIPTNAME ? &task->script_name_table : &task->str_table;
   struct indexed_string* string = table->head;
   while ( string && string->index != index ) {
      string = string->next;
   }
   return string;
}

int t_add_altern_filename( struct task* task, const char* filename ) {
   int id = ALTERN_FILENAME_INITIAL_ID;
   list_iter_t i;
   list_iter_init( &i, &task->altern_filenames );
   while ( ! list_end( &i ) ) {
      const char* added_filename = list_data( &i );
      if ( strcmp( filename, added_filename ) == 0 ) {
         return id;
      }
      --id;
      list_next( &i );
   }
   list_append( &task->altern_filenames, ( void* ) filename );
   return id;
}

struct constant* t_alloc_constant( void ) {
   struct constant* constant = mem_slot_alloc( sizeof( *constant ) );
   t_init_object( &constant->object, NODE_CONSTANT );
   constant->name = NULL;
   constant->value_node = NULL;
   constant->spec = SPEC_NONE;
   constant->value = 0;
   constant->hidden = false;
   return constant;
}

struct enumeration* t_alloc_enumeration( void ) {
   struct enumeration* enumeration = mem_alloc( sizeof( *enumeration ) );
   t_init_object( &enumeration->object, NODE_ENUMERATION );
   enumeration->head = NULL;
   enumeration->tail = NULL;
   enumeration->name = NULL;
   enumeration->body = NULL;
   enumeration->base_type = SPEC_INT;
   return enumeration;
}

struct enumerator* t_alloc_enumerator( void ) {
   struct enumerator* enumerator = mem_alloc( sizeof( *enumerator ) );
   t_init_object( &enumerator->object, NODE_ENUMERATOR );
   enumerator->name = NULL;
   enumerator->next = NULL;
   enumerator->initz = NULL;
   enumerator->enumeration = NULL;
   enumerator->value = 0;
   return enumerator;
}

void t_append_enumerator( struct enumeration* enumeration,
   struct enumerator* enumerator ) {
   if ( enumeration->head ) {
      enumeration->tail->next = enumerator;
   }
   else {
      enumeration->head = enumerator;
   }
   enumeration->tail = enumerator;
}

struct structure* t_alloc_structure( void ) {
   struct structure* structure = mem_alloc( sizeof( *structure ) );
   t_init_object( &structure->object, NODE_STRUCTURE );
   structure->name = NULL;
   structure->body = NULL;
   structure->member = NULL;
   structure->member_tail = NULL;
   structure->size = 0;
   structure->anon = false;
   structure->has_ref_member = false;
   return structure;
}

struct structure_member* t_alloc_structure_member( void ) {
   struct structure_member* member = mem_alloc( sizeof( *member ) );
   t_init_object( &member->object, NODE_STRUCTURE_MEMBER );
   member->name = NULL;
   member->ref = NULL;
   member->structure = NULL;
   member->enumeration = NULL;
   member->path = NULL;
   member->dim = NULL;
   member->next = NULL;
   member->spec = SPEC_NONE;
   member->offset = 0;
   member->size = 0;
   member->diminfo_start = 0;
   return member;
}

void t_append_structure_member( struct structure* structure,
   struct structure_member* member ) {
   if ( structure->member ) {
      structure->member_tail->next = member;
   }
   else {
      structure->member = member;
   }
   structure->member_tail = member;
}

struct dim* t_alloc_dim( void ) {
   struct dim* dim = mem_alloc( sizeof( *dim ) );
   dim->next = NULL;
   dim->length_node = NULL;
   dim->length = 0;
   dim->element_size = 0;
   return dim;
}

struct var* t_alloc_var( void ) {
   struct var* var = mem_alloc( sizeof( *var ) );
   t_init_object( &var->object, NODE_VAR );
   var->name = NULL;
   var->ref = NULL;
   var->structure = NULL;
   var->enumeration = NULL;
   var->type_path = NULL;
   var->dim = NULL;
   var->initial = NULL;
   var->value = NULL;
   var->next = NULL;
   var->spec = SPEC_NONE;
   var->storage = STORAGE_LOCAL;
   var->index = 0;
   var->size = 0;
   var->diminfo_start = 0;
   var->initz_zero = false;
   var->hidden = false;
   var->used = false;
   var->initial_has_str = false;
   var->imported = false;
   var->is_constant_init = false;
   var->addr_taken = false;
   var->in_shared_array = false;
   var->func_scope = false;
   var->constant = false;
   var->external = false;
   return var;
}

struct func* t_alloc_func( void ) {
   struct func* func = mem_slot_alloc( sizeof( *func ) );
   t_init_object( &func->object, NODE_FUNC );
   func->type = FUNC_ASPEC;
   func->ref = NULL;
   func->structure = NULL;
   func->enumeration = NULL;
   func->path = NULL;
   func->name = NULL;
   func->params = NULL;
   func->impl = NULL;
   func->return_spec = SPEC_VOID;
   func->min_param = 0;
   func->max_param = 0;
   func->hidden = false;
   func->msgbuild = false;
   func->imported = false;
   func->prototype = false;
   return func;
}

struct func_user* t_alloc_func_user( void ) {
   struct func_user* impl = mem_alloc( sizeof( *impl ) );
   list_init( &impl->labels );
   impl->body = NULL;
   impl->next_nested = NULL;
   impl->nested_funcs = NULL;
   impl->nested_calls = NULL;
   impl->returns = NULL;
   impl->prologue_point = NULL;
   impl->return_table = NULL;
   list_init( &impl->vars );
   list_init( &impl->funcscope_vars );
   impl->index = 0;
   impl->size = 0;
   impl->usage = 0;
   impl->obj_pos = 0;
   impl->recursive = RECURSIVE_UNDETERMINED;
   impl->nested = false;
   return impl;
}

struct param* t_alloc_param( void ) {
   struct param* param = mem_slot_alloc( sizeof( *param ) );
   t_init_object( &param->object, NODE_PARAM );
   param->ref = NULL;
   param->structure = NULL;
   param->enumeration = NULL;
   param->next = NULL;
   param->name = NULL;
   param->default_value = NULL;
   param->spec = SPEC_NONE;
   param->index = 0;
   param->size = 0;
   param->obj_pos = 0;
   param->used = false;
   return param;
}

struct format_item* t_alloc_format_item( void ) {
   struct format_item* item = mem_alloc( sizeof( *item ) );
   item->cast = FCAST_DECIMAL;
   item->next = NULL;
   item->value = NULL;
   item->extra = NULL;
   return item;
}

struct call* t_alloc_call( void ) {
   struct call* call = mem_alloc( sizeof( *call ) );
   call->node.type = NODE_CALL;
   call->operand = NULL;
   call->func = NULL;
   call->ref_func = NULL;
   call->nested_call = NULL;
   call->format_item = NULL;
   list_init( &call->args );
   call->constant = false;
   return call;
}

int t_dim_size( struct dim* dim ) {
   return dim->length * dim->element_size;
}

const struct lang_limits* t_get_lang_limits( int lang ) {
   static const struct lang_limits acs = { 64, 0, 64, 3, 128, 256, 32 };
   static const struct lang_limits bcs = { 256, 64, 1000, 4, 32768, 32768, 0 };
   return ( lang == LANG_ACS95 ) ? &acs : &bcs;
}

const char* t_get_storage_name( int storage ) {
   switch ( storage ) {
   case STORAGE_MAP: return "map";
   case STORAGE_WORLD: return "world";
   case STORAGE_GLOBAL: return "global";
   default: return "local";
   }
}

struct literal* t_alloc_literal( void ) {
   struct literal* literal = mem_slot_alloc( sizeof( *literal ) );
   literal->node.type = NODE_LITERAL;
   literal->value = 0;
   return literal;
}

struct expr* t_alloc_expr( void ) {
   struct expr* expr = mem_slot_alloc( sizeof( *expr ) );
   expr->node.type = NODE_EXPR;
   expr->root = NULL;
   expr->spec = SPEC_NONE;
   expr->value = 0;
   expr->folded = false;
   expr->has_str = false;
   return expr;
}

struct indexed_string_usage* t_alloc_indexed_string_usage( void ) {
   struct indexed_string_usage* usage = mem_slot_alloc( sizeof( *usage ) );
   usage->node.type = NODE_INDEXED_STRING_USAGE;
   usage->string = NULL;
   return usage;
}

void t_init_pos_id( struct pos* pos, int id ) {
   pos->id = id;
   pos->line = 0;
   pos->column = 0;
}

void init_ref( struct ref* ref, int type ) {
   ref->next = NULL;
   ref->type = type;
   ref->nullable = false;
}

struct ref_func* t_alloc_ref_func( void ) {
   struct ref_func* func = mem_alloc( sizeof( *func ) );
   init_ref( &func->ref, REF_FUNCTION );
   func->params = NULL;
   func->min_param = 0;
   func->max_param = 0;
   func->msgbuild = false;
   return func;
}