#include <string.h>

#include "phase.h"
#include "../codegen/phase.h"
#include "../codegen/pcode.h"

static struct inline_asm* alloc_inline_asm( void );
static void read_opcode( struct parse* parse, struct inline_asm* inline_asm );
static void read_arg( struct parse* parse, struct inline_asm* inline_asm );
static struct inline_asm_arg* alloc_inline_asm_arg( struct pos* pos );

void p_read_asm( struct parse* parse, struct stmt_reading* reading ) {
   parse->create_nltk = true;
   struct inline_asm* inline_asm = alloc_inline_asm();
   p_test_tk( parse, TK_GT );
   p_read_tk( parse );
   read_opcode( parse, inline_asm );
   if ( parse->tk != TK_NL ) {
      while ( true ) {
         read_arg( parse, inline_asm );
         if ( parse->tk == TK_COMMA ) {
            p_read_tk( parse );
         }
         else {
            break;
         }
      }
   }
   p_test_tk( parse, TK_NL );
   parse->create_nltk = false;
   p_read_tk( parse );
   reading->node = &inline_asm->node;
}

static struct inline_asm* alloc_inline_asm( void ) {
   struct inline_asm* inline_asm = mem_alloc( sizeof( *inline_asm ) );
   inline_asm->node.type = NODE_INLINE_ASM;
   inline_asm->name = NULL;
   inline_asm->next = NULL;
   list_init( &inline_asm->args );
   inline_asm->opcode = 0;
   inline_asm->obj_pos = 0;
   return inline_asm;
}

static void read_opcode( struct parse* parse, struct inline_asm* inline_asm ) {
   switch ( parse->tk ) {
   case TK_ID:
   case TK_TERMINATE:
   case TK_RESTART:
   case TK_SUSPEND:
   case TK_GOTO:
      inline_asm->name = parse->tk_text;
      inline_asm->pos = parse->tk_pos;
      p_read_tk( parse );
      break;
   default:
      p_unexpect_diag( parse );
      p_unexpect_last_name( parse, NULL, "instruction name" );
      p_bail( parse );
   }
}

static void read_arg( struct parse* parse, struct inline_asm* inline_asm ) {
   struct inline_asm_arg* arg = alloc_inline_asm_arg( &parse->tk_pos );
   list_append( &inline_asm->args, arg );
   if (
      parse->tk == TK_LIT_DECIMAL ||
      parse->tk == TK_LIT_OCTAL ||
      parse->tk == TK_LIT_HEX ||
      parse->tk == TK_LIT_BINARY ||
      parse->tk == TK_LIT_RADIX ||
      parse->tk == TK_LIT_CHAR ) {
      arg->value.number = p_extract_literal_value( parse );
      p_read_tk( parse );
   }
   else if ( parse->tk == TK_LIT_FIXED ) {
      arg->value.number = p_extract_fixed_literal_value( parse->tk_text );
      p_read_tk( parse );
   }
   // FIXME: missing maximum string length check.
   else if ( parse->tk == TK_LIT_STRING ) {
      arg->type = INLINE_ASM_ARG_STRING;
      struct indexed_string* string = t_intern_string( parse->task,
         parse->tk_text, parse->tk_length );
      string->in_source_code = true;
      arg->value.string = string;
      p_read_tk( parse );
   }
   else if ( parse->tk == TK_ID ) {
      arg->type = INLINE_ASM_ARG_ID;
      arg->value.id = parse->tk_text;
      p_read_tk( parse );
   }
   else {
      p_test_tk( parse, TK_PAREN_L );
      p_read_tk( parse );
      struct expr_reading expr;
      p_init_expr_reading( &expr, false, false, false, true );
      p_read_expr( parse, &expr );
      arg->type = INLINE_ASM_ARG_EXPR;
      arg->value.expr = expr.output_node;
      p_test_tk( parse, TK_PAREN_R );
      p_read_tk( parse );
   }
}

static struct inline_asm_arg* alloc_inline_asm_arg( struct pos* pos ) {
   struct inline_asm_arg* arg = mem_alloc( sizeof( *arg ) );
   arg->type = INLINE_ASM_ARG_NUMBER;
   arg->value.number = 0;
   arg->pos = *pos;
   return arg;
}
