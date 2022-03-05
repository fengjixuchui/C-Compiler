#include "directives.h"
#include "macro_expander.h"
#include "tokenizer.h"
#include "string_concat.h"

#include <common.h>
#include <precedence.h>
#include <arch/x64.h>

#include <assert.h>

static int pushed_idx = 0;
static struct token pushed[2];

void push(struct token t) {
	if (pushed_idx > 1)
		ICE("Pushed too many directive tokens.");
	pushed[pushed_idx++] = t;
}

static const char *new_filename = NULL;
static int line_diff = 0;

struct token next() {
	struct token t = pushed_idx ? pushed[--pushed_idx] : tokenizer_next();
	t.pos.line += line_diff;
	if (new_filename)
		t.pos.path = new_filename;
	return t;
}

void directiver_define(void) {
	struct token name = next();

	struct define def = define_init(name.str);

	struct token t = next();
	if(t.type == T_LPAR && !t.whitespace) {
		int idx = 0;
		do {
			t = next();
			if(idx == 0 && t.type == T_RPAR) {
				def.func = 1;
				break;
			}

			if (t.type == T_ELLIPSIS) {
				t = next();
				EXPECT(&t, T_RPAR);
				def.vararg = 1;
				def.func = 1;
				break;
			}
			EXPECT(&t, T_IDENT);
			define_add_par(&def, t);

			t = next();
			if (t.type != T_RPAR)
				EXPECT(&t, T_COMMA);

			idx++;
		} while(t.type == T_COMMA);

		t = next();
	}

	while(!t.first_of_line) {
		define_add_def(&def, t);
		t = next();
	}

	push(t);

	define_map_add(def);
}

void directiver_undef(void) {
	struct token name = next();

	EXPECT(&name, T_IDENT);
	define_map_remove(name.str);
}

static struct token_list buffer;
static int buffer_pos;

struct token buffer_next() {
	if (buffer_pos >= buffer.size)
		return (struct token) { .type = T_EOI, .str = { 0 } };
	struct token *t = buffer.list + buffer_pos;
	if (t->type == T_IDENT) {
		buffer_pos++;
		return (struct token) { .type = T_NUM, .str = sv_from_str("0") };
	} else {
		return buffer.list[buffer_pos++];
	}
}

intmax_t evaluate_expression(int prec, int evaluate) {
	intmax_t expr = 0;
	struct token t = buffer_next();

	if (t.type == T_ADD) {
		expr = evaluate_expression(PREFIX_PREC, evaluate);
	} else if (t.type == T_SUB) {
		expr = -evaluate_expression(PREFIX_PREC, evaluate);
	} else if (t.type == T_NOT) {
		expr = !evaluate_expression(PREFIX_PREC, evaluate);
	} else if (t.type == T_LPAR) {
		expr = evaluate_expression(0, evaluate);
		struct token rpar = buffer_next();
		if (rpar.type != T_RPAR)
			ERROR(rpar.pos, "Expected ), got %s", dbg_token_type(rpar.type));
	} else if (t.type == T_NUM) {
		struct constant c = constant_from_string(t.str);
		assert(c.type == CONSTANT_TYPE);
		if (type_is_floating(c.data_type))
			ERROR(t.pos, "Floating point arithmetic in the preprocessor is not allowed.");
		if (!type_is_integer(c.data_type))
			ERROR(t.pos, "Preprocessor variables must be of integer type.");
		if (type_is_integer(c.data_type))
			expr = is_signed(c.data_type->simple) ? (intmax_t)c.int_d : (intmax_t)c.uint_d;
	} else if (t.type == T_CHARACTER_CONSTANT) {
		expr = escaped_character_constant_to_int(t);
	} else {
		ERROR(t.pos, "Invalid token in preprocessor expression. %s", dbg_token(&t));
	}

	t = buffer_next();

	while (prec < precedence_get(t.type, 1)) {
		int new_prec = precedence_get(t.type, 0);

		if (t.type == T_QUEST) {
			int mid = evaluate_expression(0, expr ? evaluate : 0);
			struct token colon = buffer_next();
			assert(colon.type == T_COLON);
			int rhs = evaluate_expression(new_prec, expr ? 0 : evaluate);
			expr = expr ? mid : rhs;
		} else if (t.type == T_AND) {
			int rhs = evaluate_expression(new_prec, expr ? evaluate : 0);
			expr = expr && rhs;
		} else if (t.type == T_OR) {
			int rhs = evaluate_expression(new_prec, expr ? 0 : evaluate);
			expr = expr || rhs;
		} else {
			// Standard binary operator.
			int rhs = evaluate_expression(new_prec, evaluate);
			if (evaluate) {
				switch (t.type) {
				case T_BOR: expr = expr | rhs; break;
				case T_XOR: expr = expr ^ rhs; break;
				case T_AMP: expr = expr & rhs; break;
				case T_EQ: expr = expr == rhs; break;
				case T_NEQ: expr = expr != rhs; break;
				case T_LEQ: expr = expr <= rhs; break;
				case T_GEQ: expr = expr >= rhs; break;
				case T_L: expr = expr < rhs; break;
				case T_G: expr = expr > rhs; break;
				case T_LSHIFT: expr = expr << rhs; break;
				case T_RSHIFT: expr = expr >> rhs; break;
				case T_ADD: expr = expr + rhs; break;
				case T_SUB: expr = expr - rhs; break;
				case T_STAR: expr = expr * rhs; break;
				case T_DIV:
					if (rhs)
						expr = expr / rhs;
					else
						ERROR(t.pos, "Division by zero");
					break;
				case T_MOD:
					if (rhs)
						expr = expr % rhs;
					else
						ERROR(t.pos, "Modulo by zero");
					break;
				default:
					ERROR(t.pos, "Invalid infix %s", dbg_token(&t));
				}
			}
		}

		t = buffer_next();
	}

	if (buffer_pos == 0)
		ICE("Buffer should not be empty.");
	buffer.list[--buffer_pos] = t;

	return expr;
}

intmax_t evaluate_until_newline() {
	buffer.size = 0;
	struct token t = next();
	while (!t.first_of_line) {
		if (sv_string_cmp(t.str, "defined")) {
			t = next();
			int has_lpar = t.type == T_LPAR;
			if (has_lpar)
				t = next();

			int is_defined = define_map_get(t.str) != NULL;
			token_list_add(&buffer, (struct token) {.type = T_NUM, .str = is_defined ? sv_from_str("1") :
					sv_from_str("0")});

			t = next();

			if (has_lpar) {
				EXPECT(&t, T_RPAR);
				t = next();
			}
		} else {
			token_list_add(&buffer, t);

			t = next();
		}
	}
	push(t);

	expand_token_list(&buffer);
	token_list_add(&buffer, (struct token) { .type = T_EOI });

	buffer_pos = 0;
	intmax_t result = evaluate_expression(0, 1);

	return result;
}

int directiver_evaluate_conditional(struct token dir) {
	if (sv_string_cmp(dir.str, "ifdef")) {
		return (define_map_get(next().str) != NULL);
	} else if (sv_string_cmp(dir.str, "ifndef")) {
		return !(define_map_get(next().str) != NULL);
	} else if (sv_string_cmp(dir.str, "if") ||
			   sv_string_cmp(dir.str, "elif")) {
		return evaluate_until_newline();
	} else if (sv_string_cmp(dir.str, "else")) {
		return 1;
	}

	ERROR(dir.pos, "Invalid conditional directive");
}

void directiver_handle_pragma(void) {
	struct token command = next();

	if (sv_string_cmp(command.str, "once")) {
		tokenizer_disable_current_path();
	} else {
		WARNING(command.pos, "\"#pragma %s\" not supported", dbg_token(&command));
		struct token t = next();
		while (!t.first_of_line)
			t = next();
		push(t);
	}
}

struct token directiver_next(void) {
	static int cond_stack_n = 0, cond_stack_cap = 0;
	static int *cond_stack = NULL;

	if (cond_stack_n == 0)
		ADD_ELEMENT(cond_stack_n, cond_stack_cap, cond_stack) = 1;

	struct token t = next();
	while (t.type == PP_DIRECTIVE || cond_stack[cond_stack_n - 1] != 1) {
		if (t.type != PP_DIRECTIVE) {
			t = next();
			continue;
		}
		struct token directive = next();

		if (directive.first_of_line) {
			t = directive;
			continue;
		}

		if (directive.type != T_IDENT &&
			cond_stack[cond_stack_n - 1] != 1) {
			t = directive;
			continue;
		}

		struct string_view name = directive.str;

		assert(directive.type == T_IDENT);

		if (sv_string_cmp(name, "ifndef") ||
			sv_string_cmp(name, "ifdef") ||
			sv_string_cmp(name, "if")) {
			if (cond_stack[cond_stack_n - 1] == 1) {
				int result = directiver_evaluate_conditional(directive);
				ADD_ELEMENT(cond_stack_n, cond_stack_cap, cond_stack) = result ? 1 : 0;
			} else {
				ADD_ELEMENT(cond_stack_n, cond_stack_cap, cond_stack) = -1;
			}
		} else if (sv_string_cmp(name, "elif") ||
				   sv_string_cmp(name, "else")) {
			if (cond_stack[cond_stack_n - 1] == 0) {
				int result = directiver_evaluate_conditional(directive);
				cond_stack[cond_stack_n - 1] = result;
			} else {
				cond_stack[cond_stack_n - 1] = -1;
			}
		} else if (sv_string_cmp(name, "endif")) {
			cond_stack_n--;
		} else if (cond_stack[cond_stack_n - 1] == 1) {
			if (sv_string_cmp(name, "define")) {
				directiver_define();
			} else if (sv_string_cmp(name, "undef")) {
				define_map_remove(next().str);
			} else if (sv_string_cmp(name, "error")) {
				ERROR(directive.pos, "#error directive was invoked.");
			} else if (sv_string_cmp(name, "include")) {
				// There is an issue with just resetting after include. But
				// I'm interpreting the standard liberally to allow for this.
				new_filename = NULL;
				line_diff = 0;
				struct token path_tok = next();
				int system = path_tok.type == PP_HEADER_NAME_H;
				struct string_view path = path_tok.str;
				path.str++;
				path.len -= 2;
				tokenizer_push_input(sv_to_str(path), system);
			} else if (sv_string_cmp(name, "endif")) {
				// Do nothing.
			} else if (sv_string_cmp(name, "pragma")) {
				directiver_handle_pragma();
			} else if (sv_string_cmp(name, "line")) {
				// 6.10.4
				struct token digit_seq = next(), s_char_seq;

				if (digit_seq.first_of_line)
					ERROR(digit_seq.pos, "Expected digit sequence after #line");

				int has_s_char_seq = 0;
				if (digit_seq.type != T_NUM) {
					buffer.size = 0;
					struct token t = digit_seq;
					while (!t.first_of_line) {
						token_list_add(&buffer, t);
						t = next();
					}

					push(t);

					expand_token_list(&buffer);

					if (buffer.size == 0) {
						ERROR(digit_seq.pos, "Invalid #line macro expansion");
					} else if (buffer.size >= 1) {
						digit_seq = buffer.list[0];
					} else if (buffer.size >= 2) {
						s_char_seq = buffer.list[1];
						has_s_char_seq = 1;
					}
				} else {
					s_char_seq = next();
					if (s_char_seq.first_of_line) {
						push(s_char_seq);
					} else {
						has_s_char_seq = 1;
					}
				}

				if (digit_seq.first_of_line || digit_seq.type != T_NUM)
					ERROR(digit_seq.pos, "Expected digit sequence after #line");

				line_diff += atoi(sv_to_str(digit_seq.str)) - directive.pos.line - 1;

				if (has_s_char_seq) {
					if (s_char_seq.type != T_STRING)
						ERROR(s_char_seq.pos, "Expected s char sequence as second argument to #line");
					s_char_seq.str.len -= 2;
					s_char_seq.str.str++;
					new_filename = sv_to_str(s_char_seq.str);
				}
			} else {
				ERROR(directive.pos, "#%s not implemented", dbg_token(&directive));
			}
		}

		t = next();
	}

	return t;
}
