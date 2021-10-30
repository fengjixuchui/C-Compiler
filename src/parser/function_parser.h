#ifndef FUNCTION_PARSER_H
#define FUNCTION_PARSER_H

#include "parser.h"

void parse_function(const char *name, struct type *type, int arg_n, var_id *args, int global);
const char *get_current_function_name(void);

#endif
