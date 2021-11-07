#include "rodata.h"
#include "codegen.h"

#include <common.h>
#include <arch/x64.h>
#include <parser/expression.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

struct entry {
	enum entry_type {
		ENTRY_STR,
		ENTRY_LABEL_NAME
	} type;
	struct string_view name;
	label_id id;
};

static struct entry *entries = NULL;
static int entries_size = 0, entries_cap = 0;

label_id label_register(enum entry_type type, struct string_view str) {
	if (type == ENTRY_STR) {
			for (int i = 0; i < entries_size; i++) {
				if (sv_cmp(entries[i].name, str)) {
					return entries[i].id;
				}
			}
	}

	int id = entries_size;
	ADD_ELEMENT(entries_size, entries_cap, entries) = (struct entry) {
		.type = type,
		.name = str,
		.id = id
	};

	return id;
}

label_id rodata_register(struct string_view str) {
	return label_register(ENTRY_STR, str);
}

const char *rodata_get_label_string(label_id id) {
	if (entries[id].type == ENTRY_STR) {
		static char *buffer = NULL;

		if (!buffer)
			buffer = malloc(128);

		sprintf(buffer, ".L_rodata%d", id);
		return buffer;
	} else if (entries[id].type == ENTRY_LABEL_NAME) {
		return sv_to_str(entries[id].name);
	} else {
		NOTIMP();
	}
}

void rodata_codegen(void) {
	for (int i = 0; i < entries_size; i++) {
		if (entries[i].type != ENTRY_STR)
			continue;
		emit("%s:", rodata_get_label_string(entries[i].id));

		emit_no_newline("\t.string \"", entries[i].name);
		struct string_view str = entries[i].name;
		for (int i = 0; i < str.len; i++) {
			char buffer[5];
			character_to_escape_sequence(str.str[i], buffer);
			emit_no_newline("%s", buffer);
		}
		emit_no_newline("\"\n");
	}
}

label_id register_label_name(struct string_view str) {
	return label_register(ENTRY_LABEL_NAME, str);
}

struct static_var {
	struct string_view label;
	struct type *type;
	struct initializer *init;
	int global;
};

static struct static_var *static_vars = NULL;
static int static_vars_size, static_vars_cap;

void data_register_static_var(struct string_view label, struct type *type, struct initializer *init, int global) {
	ADD_ELEMENT(static_vars_size, static_vars_cap, static_vars) = (struct static_var) {
		.label = label,
		.type = type,
		.init = init,
		.global = global
	};
}

void codegen_initializer_recursive(struct initializer *init,
								   uint8_t *buffer, label_id *labels,
								   int64_t *label_offsets, int *is_label,
								   size_t size) {
	for (int i = 0; i < init->size; i++) {
		struct init_pair *pair = init->pairs + i;
		size_t offset = pair->offset;

		if (offset >= size)
			ERROR("Internal compiler error");

		switch (pair->type) {
		case IP_EXPRESSION: {
			struct expr *expr = pair->u.expr;
			struct constant *c = expression_to_constant(expr);
			if (c) {
				switch (c->type) {
				case CONSTANT_TYPE:
					constant_to_buffer(buffer + offset, *c, pair->bit_offset, pair->bit_size);
					break;

				case CONSTANT_LABEL_POINTER:
					is_label[offset] = 1;
					labels[offset] = c->label.label;
					label_offsets[offset] = c->label.offset;
					break;

				case CONSTANT_LABEL:
					NOTIMP();
					break;

				default:
					NOTIMP();
				}
			} else if (expr->type == E_COMPOUND_LITERAL) {
				codegen_initializer_recursive(expr->compound_literal.init,
											  buffer + offset,
											  labels + offset,
											  label_offsets + offset,
											  is_label + offset,
											  size - offset);
			} else {
				ERROR("Invalid constant expression in static initializer of type %s.",
					  dbg_type(expr->data_type));
			}
		} break;
		case IP_STRING:
			for (int i = 0; i < pair->u.str.len; i++) 
				buffer[offset + i] = pair->u.str.str[i];
			break;
		}
	}
}

void codegen_initializer(struct type *type, struct initializer *init);

void codegen_compound_literals(struct expr **expr, int lvalue) {
	switch ((*expr)->type) {
	case E_GENERIC_SELECTION:
	case E_DOT_OPERATOR: NOTIMP();
	case E_COMPOUND_LITERAL:
		if (lvalue) {
			static int counter = 0;
			char name[256];
			sprintf(name, ".compundliteral%d", counter++);
			emit("%s:", name);
			codegen_initializer((*expr)->compound_literal.type,
								(*expr)->compound_literal.init);

			label_id label = register_label_name(sv_from_str(strdup(name)));

			*expr = expr_new((struct expr) {
					.type = E_CONSTANT,
					.constant = {
						.type = CONSTANT_LABEL,
						.data_type = (*expr)->compound_literal.type,
						.label = { label, 0 }
					}
				});
		}
		break;
	case E_ARRAY_PTR_DECAY:
	case E_ADDRESS_OF:
		codegen_compound_literals(&(*expr)->args[0], 1);
		break;
	case E_INDIRECTION:
		codegen_compound_literals(&(*expr)->args[0], 0);
		break;
	case E_UNARY_OP:
		codegen_compound_literals(&(*expr)->args[0], lvalue);
		break;
	case E_CAST:
		codegen_compound_literals(&(*expr)->cast.arg, lvalue);
		break;
	case E_ALIGNOF:
	case E_POINTER_ADD:
	case E_POINTER_SUB:
		codegen_compound_literals(&(*expr)->args[0], lvalue);
		codegen_compound_literals(&(*expr)->args[1], lvalue);
		break;

	case E_POINTER_DIFF:
	case E_ASSIGNMENT:
	case E_ASSIGNMENT_POINTER:
	case E_ASSIGNMENT_OP:
	case E_CONDITIONAL:
	case E_COMMA:
	case E_BUILTIN_VA_START:
	case E_BUILTIN_VA_END:
	case E_BUILTIN_VA_ARG:
	case E_BUILTIN_VA_COPY:
		break;

	default: return;
	}

	if ((*expr)->type != E_CONSTANT) {
		struct constant c;
		if (evaluate_constant_expression(*expr, &c)) {
			*expr = expr_new((struct expr) {
					.type = E_CONSTANT,
					.constant = c
				});
		}
	}
}

void codegen_pre_initializer(struct initializer *init) {
	// Iterate through all child expressions and find uninitialized compound literals.
	for (int i = 0; i < init->size; i++) {
		struct init_pair *pair = init->pairs + i;

		if (pair->type == IP_EXPRESSION)
			codegen_compound_literals(&pair->u.expr, 0);
	}
}

void codegen_initializer(struct type *type,
						 struct initializer *init) {
	// TODO: Make this more portable.
	size_t size = calculate_size(type);

	uint8_t *buffer = malloc(sizeof *buffer * size);
	label_id *labels = malloc(sizeof *labels * size);
	int64_t *label_offsets = malloc(sizeof *label_offsets * size);
	int *is_label = malloc(sizeof *is_label * size);

	for (unsigned i = 0; i < size; i++) {
		buffer[i] = 0;
		is_label[i] = 0;
		labels[i] = 0;
		label_offsets[i] = 0;
	}

	codegen_initializer_recursive(init, buffer, labels, label_offsets, is_label, size);

	for (unsigned i = 0; i < size; i++) {
		if (is_label[i]) {
			if (label_offsets[i] == 0)
				emit(".quad %s", rodata_get_label_string(labels[i]));
			else
				emit(".quad %s+%lld", rodata_get_label_string(labels[i]),
					label_offsets[i]);
			i += 7;
		} else {
			int how_long = 0;
			for (; how_long < 8 && i + how_long < size; how_long++) {
				if (is_label[i + how_long])
					break;
			}
			if (how_long == 8) {
				emit(".quad %" PRIu64, *(uint64_t *)(buffer + i));
				i += how_long - 1;
			} else {
				emit(".byte %d", (int)buffer[i]);
			}
		}
	}

	free(buffer);
	free(labels);
	free(label_offsets);
	free(is_label);
}

void codegen_static_var(struct static_var *static_var) {
	set_section(".data");
	if (static_var->global)
		emit(".global %.*s", static_var->label.len, static_var->label.str);
	if (static_var->init) {
		codegen_pre_initializer(static_var->init);

		emit("%.*s:", static_var->label.len, static_var->label.str);

		codegen_initializer(static_var->type,
							static_var->init);
	} else {
		emit("%.*s:", static_var->label.len, static_var->label.str);

		emit(".zero %d", calculate_size(static_var->type));
	}
	set_section(".text");
}

void data_codegen(void) {
	for (int i = 0; i < static_vars_size; i++) {
		codegen_static_var(static_vars + i);
	}
}
