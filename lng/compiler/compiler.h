#ifndef COMPILER_H
#define COMPILER_H

#include "../chain/chain.h"

// pattern compiler: text → event chain
// pattern language (regex 2.0 style):
//   SET <offset> <width> <value>
//   MATCH <offset> <width> <mask> <expect>
//   EMIT <tag>
//   BRANCH <label> IF MATCH
//   HALT
//
// compiles to evt_op_t instructions pushed into a chain

typedef enum {
    COMP_OK = 0,
    COMP_ERR_SYNTAX,
    COMP_ERR_CHAIN,
} comp_status_t;

// compile a single instruction line into events
comp_status_t compile_line(chain_t *c, const char *line);

// compile a multi-line program
comp_status_t compile_program(chain_t *c, const char *source);

// compile from file
comp_status_t compile_file(chain_t *c, const char *path);

#endif
