#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <assert.h>

#include "types.h"
#include "common.h"

int compare_types(struct type *a, struct type **a_children,
				  struct type *b) {
	if (a->type != b->type)
		return 0;

	if (a->n != b->n)
		return 0;

	switch(a->type) {
	case TY_FUNCTION:
		if (a->function.is_variadic != b->function.is_variadic)
			return 0;
		break;

	case TY_POINTER:
		if (a->pointer.is_const != b->pointer.is_const)
			return 0;
		break;

	case TY_SIMPLE:
		if (a->simple != b->simple)
			return 0;
		break;

	case TY_ENUM:
		if (a->enum_data != b->enum_data)
			return 0;
		break;

	case TY_STRUCT:
		if (a->struct_data != b->struct_data)
			return 0;
		break;

	case TY_ARRAY:
		if (a->array.length != b->array.length)
			return 0;
		break;

	case TY_INCOMPLETE_ARRAY:
		// TODO: Is this correct?
		break;

	case TY_VARIABLE_LENGTH_ARRAY:
		if (a->variable_length_array.length !=
			b->variable_length_array.length)
			return 0;
		break;

	default:
		ERROR("Not implemented %d", a->type);
		break;
	}

	for (int i = 0; i < a->n; i++) {
		if (!compare_types(a_children[i], a_children[i]->children, b->children[i]))
			return 0;
	}

	return 1;
}

// TODO: This is not a good hash function.
uint32_t hash_str(char *str) {
	uint32_t hash = 0;
	for (; *str; str++)
		hash ^= hash32(*str);
	return hash;
}

uint32_t type_hash(struct type *type, struct type **children) {
	uint32_t hash = 0;

	hash ^= hash32(type->type);

	switch (type->type) {
	case TY_SIMPLE:
		hash ^= hash32(type->simple);
		break;
	case TY_FUNCTION:
		hash ^= hash32(type->function.is_variadic);
		break;
	case TY_POINTER:
		hash ^= hash32(type->pointer.is_const);
		break;
	case TY_STRUCT:
		hash ^= hash32((size_t)type->struct_data);
		break;
	case TY_ENUM:
		hash ^= hash32((size_t)type->enum_data);
		break;
	case TY_ARRAY:
		hash ^= hash32(type->array.length);
		break;
	case TY_INCOMPLETE_ARRAY:
		break;
	case TY_VARIABLE_LENGTH_ARRAY:
		hash ^= hash32(type->variable_length_array.length);
		break;
	default:
		ERROR("Not implemented %d", (int)type->type);
	}

	for (int i = 0; i < type->n; i++) {
		hash ^= type_hash(children[i], children[i]->children);
	}

	return hash;
}

struct type *type_create(struct type *params, struct type **children) {
	static struct type **hashtable = NULL;
	static int hashtable_size = 0;

	if (hashtable_size == 0) {
		hashtable_size = 1024;
		hashtable = malloc(hashtable_size * sizeof(*hashtable));
		for (int i = 0; i < hashtable_size; i++) {
			hashtable[i] = NULL;
		}
	}

	uint32_t hash_idx = type_hash(params, children) % hashtable_size;
	struct type *first = hashtable[hash_idx];
	struct type *ffirst = first;

	while(first && !compare_types(params, children, first))
		first = first->next;

	if (first)
		return first;

	struct type *new = malloc(sizeof(*params) + sizeof(*children) * params->n);
	*new = *params;
	memcpy(new->children, children, sizeof(*children) * params->n);

	new->next = ffirst;
	hashtable[hash_idx] = new;

	return new;
}

struct type *type_simple(enum simple_type type) {
	struct type t = {
		.type = TY_SIMPLE,
		.simple = type,
		.n = 0
	};

	return type_create(&t, NULL);
}

struct type *type_pointer(struct type *type) {
	struct type params = {
		.type = TY_POINTER,
		.n = 1
	};
	struct type *children = type;
	return type_create(&params, &children);
}

struct type *type_struct(struct struct_data *struct_data) {
	struct type params = {
		.type = TY_STRUCT,
		.struct_data = struct_data
	};
	return type_create(&params, NULL);
}

struct type *type_deref(struct type *type) {
	if (type->type != TY_POINTER)
		ERROR("Expected type to be pointer when dereffing %s", type_to_string(type));
	return type->children[0];
}

// TODO: make this better.
struct struct_data *register_struct(void) {
	return malloc(sizeof (struct struct_data));

	static struct struct_data *array = NULL;
	static int capacity = 0;
	static int n = 0;

	if (n >= capacity) {
		capacity *= 2;

		if (capacity == 0)
			capacity = 4;

		array = realloc(array, capacity * sizeof (*array));
	}

	struct struct_data *ret = &array[n++];
	*ret = (struct struct_data) { 0 };

	return ret;
}

struct enum_data *register_enum(void) {
	return malloc(sizeof (struct enum_data));
	static struct enum_data *array = NULL;
	static int capacity = 0;
	static int n = 0;

	if (n >= capacity) {
		capacity *= 2;

		if (capacity == 0)
			capacity = 4;

		array = realloc(array, capacity * sizeof (*array));
	}

	return &array[n++];
}

int type_member_idx(struct type *type,
					const char *name) {
	if (type->type != TY_STRUCT) {
		pretty_print(type);
		printf("\n");
		ERROR("Not a struct, is %d", type->type);
	}

	struct struct_data *data = type->struct_data;

	if (!data->is_complete)
		ERROR("Member access on incomplete type not allowed");

	for (int i = 0; i < data->n; i++) {
		if (strcmp(name, data->names[i]) == 0)
			return i;
	}

	printf("TYPE: \"");
	pretty_print(type);
	printf("\"\n");
	ERROR("No member with name %s", name);
}

struct type *parameter_adjust(struct type *type) {
	struct type *children[type->n];
	struct type ntype = *type;

	for (int i = 0; i < type->n; i++) {
		children[i] = type->children[i];
	}

	switch (type->type) {
	case TY_ARRAY:
	case TY_INCOMPLETE_ARRAY:
		ntype = (struct type) {
			.type = TY_POINTER,
			.n = 1
		};
		children[0] = parameter_adjust(children[0]);
		break;

	case TY_FUNCTION:
		children[0] = parameter_adjust(children[0]);
		break;

	case TY_POINTER:
		children[0] = parameter_adjust(children[0]);
		break;

	case TY_STRUCT:
	case TY_SIMPLE:
	case TY_ENUM:
		break;

	default:
		ERROR("NOTIMP: %d", type->type);
		NOTIMP();
	}

	return type_create(&ntype, children);
}

int type_is_integer(struct type *type) {
	if (type->type != TY_SIMPLE)
		return 0;

	switch (type->simple) {
	case ST_VOID:
	case ST_FLOAT:
	case ST_DOUBLE:
	case ST_LDOUBLE:
	case ST_FLOAT_COMPLEX:
	case ST_DOUBLE_COMPLEX:
	case ST_LDOUBLE_COMPLEX:
		return 0;
	case ST_CHAR:
	case ST_SCHAR:
	case ST_UCHAR:
	case ST_SHORT:
	case ST_USHORT:
	case ST_INT:
	case ST_UINT:
	case ST_LONG:
	case ST_ULONG:
	case ST_LLONG:
	case ST_ULLONG:
	case ST_BOOL:
		return 1;
	default:
		NOTIMP();
	}
}

int type_is_floating(struct type *type) {
	if (type->type != TY_SIMPLE)
		return 0;

	switch (type->simple) {
	case ST_VOID:
	case ST_CHAR:
	case ST_SCHAR:
	case ST_UCHAR:
	case ST_SHORT:
	case ST_USHORT:
	case ST_INT:
	case ST_UINT:
	case ST_LONG:
	case ST_ULONG:
	case ST_LLONG:
	case ST_ULLONG:
	case ST_BOOL:
		return 0;
	case ST_FLOAT:
	case ST_DOUBLE:
	case ST_LDOUBLE:
	case ST_FLOAT_COMPLEX:
	case ST_DOUBLE_COMPLEX:
	case ST_LDOUBLE_COMPLEX:
		return 1;
	default:
		NOTIMP();
	}
}

int type_is_arithmetic(struct type *type) {
	return type_is_integer(type) ||
		type_is_floating(type);
}

int type_is_real(struct type *type) {
	if (type->type != TY_SIMPLE)
		return 0;

	if (type->simple == ST_FLOAT_COMPLEX ||
		type->simple == ST_DOUBLE_COMPLEX ||
		type->simple == ST_LDOUBLE_COMPLEX)
		return 0;
	return type_is_integer(type) ||
		type_is_floating(type);
}

int type_is_pointer(struct type *type) {
	return type->type == TY_POINTER;
}

void type_select(struct type *type, int index,
				 int *field_offset, struct type **field_type) {
	*field_offset = calculate_offset(type, index);

	switch (type->type) {
	case TY_STRUCT:
		*field_type = type->struct_data->types[index];
		break;

	case TY_ARRAY:
	case TY_INCOMPLETE_ARRAY:
		*field_type = type->children[0];
		break;

	default:
		NOTIMP();
	}
}

const char *simple_to_str(enum simple_type st) {
	switch (st) {
	case ST_VOID: return "ST_VOID";
	case ST_CHAR: return "ST_CHAR";
	case ST_SCHAR: return "ST_SCHAR";
	case ST_UCHAR: return "ST_UCHAR";
	case ST_SHORT: return "ST_SHORT";
	case ST_USHORT: return "ST_USHORT";
	case ST_INT: return "ST_INT";
	case ST_UINT: return "ST_UINT";
	case ST_LONG: return "ST_LONG";
	case ST_ULONG: return "ST_ULONG";
	case ST_LLONG: return "ST_LLONG";
	case ST_ULLONG: return "ST_ULLONG";
	case ST_FLOAT: return "ST_FLOAT";
	case ST_DOUBLE: return "ST_DOUBLE";
	case ST_LDOUBLE: return "ST_LDOUBLE";
	case ST_BOOL: return "ST_BOOL";
	case ST_FLOAT_COMPLEX: return "ST_FLOAT_COMPLEX";
	case ST_DOUBLE_COMPLEX: return "ST_DOUBLE_COMPLEX";
	case ST_LDOUBLE_COMPLEX: return "ST_LDOUBLE_COMPLEX";
	default: return "???";
	}
}

// TODO: remove this and replace completely with type_to_string.
void pretty_print(struct type *type) {
	printf("%s", type_to_string(type));
}

void merge_anonymous(struct struct_data *data) {
	// types names offsets need to be modified.
	for (int i = 0; i < data->n; i++) {
		if (data->names[i])
			continue;

		int offset = data->offsets[i];
		struct type *type = data->types[i];

		assert(type->type == TY_STRUCT);
		struct struct_data *sub_data = type->struct_data;

		int n_new_elements = sub_data->n;
		int new_n = data->n + n_new_elements - 1;

		if (n_new_elements == 0) {
			ERROR("This should not be allowed");
		}

		// Make place for new elements.
		data->names = realloc(data->names, sizeof *data->names * new_n);
		data->types = realloc(data->types, sizeof *data->types * new_n);
		data->offsets = realloc(data->offsets, sizeof *data->offsets * new_n);

		for (int j = 0; j < data->n - i - 1; j++) {
			data->names[i + n_new_elements + j] = data->names[i + 1 + j];
			data->types[i + n_new_elements + j] = data->types[i + 1 + j];
			data->offsets[i + n_new_elements + j] = data->offsets[i + 1 + j];
		}

		data->n = new_n;

		for (int j = 0; j < sub_data->n; j++) {
			data->names[i + j] = sub_data->names[j];
			data->types[i + j] = sub_data->types[j];
			data->offsets[i + j] = sub_data->offsets[j] + offset;
		}

		i += n_new_elements - 1;
	}
}

const char *type_to_string(struct type *type) {
	static int char_buffer_size = 100;
	static char *buffer = NULL;

	if (!buffer) {
		buffer = malloc(char_buffer_size);
	}

	int curr_pos = 0;
	
#define PRINT(STR, ...) do {											\
		int print_size = snprintf(buffer + curr_pos, char_buffer_size - 1 - curr_pos, STR, ##__VA_ARGS__); \
		int req_size = print_size + 1 + curr_pos; \
		if (req_size > char_buffer_size) {								\
			char_buffer_size = req_size;								\
			buffer = realloc(buffer, char_buffer_size);					\
			snprintf(buffer + curr_pos, char_buffer_size - 1 - curr_pos, STR, ##__VA_ARGS__); \
		}																\
		curr_pos += print_size;\
	} while (0)

	while (type) {
		switch (type->type) {
		case TY_ENUM:
			PRINT("ENUM");
			type = NULL;
			break;
		case TY_SIMPLE:
			PRINT("SIMPLE %s", simple_to_str(type->simple));
			type = NULL;
			break;
		case TY_ARRAY:
			PRINT("ARRAY of ");
			type = type->children[0];
			break;
		case TY_VARIABLE_LENGTH_ARRAY:
			PRINT("VARIABLE LENGTH ARRAY ARRAY OF ");
			type = type->children[0];
			break;
		case TY_POINTER:
			PRINT("POINTER to ");
			type = type->children[0];
			break;
		case TY_FUNCTION:
			PRINT("FUNCTION returning ");
			type = type->children[0];
			break;
		case TY_STRUCT:
			PRINT("STRUCT (%s)", type->struct_data->name);
			type = NULL;
			break;
		case TY_INCOMPLETE_ARRAY:
			PRINT("INCOMPLETE ARRAY of ");
			type = type->children[0];
			break;
		default:
			type = NULL;
		}
	}
	return buffer;
}

int has_variable_size(struct type *type) {
	switch (type->type) {
	case TY_SIMPLE:
	case TY_ENUM:
	case TY_STRUCT:
	case TY_POINTER:
		return 0;

	case TY_FUNCTION:
		return 0; // At least I hope this is correct.

	case TY_ARRAY:
		return has_variable_size(type->children[0]);

	case TY_INCOMPLETE_ARRAY:
		return 0;

	case TY_VARIABLE_LENGTH_ARRAY:
		return 1;

	default:
		NOTIMP();
	}
}

int type_is_simple(struct type *type, enum simple_type st) {
	return type->type == TY_SIMPLE &&
		type->simple == st;
}
