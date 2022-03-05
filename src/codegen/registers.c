#include "registers.h"
#include "codegen.h"

#include <common.h>
#include <parser/parser.h>

#include <assert.h>

const char *registers[][4] = {
	{"%rax", "%eax", "%ax", "%al"}, // 0 0
	{"%rbx", "%ebx", "%bx", "%bl"}, // 1 1
	{"%rcx", "%ecx", "%cx", "%cl"}, // 2 0
	{"%rdx", "%edx", "%dx", "%dl"}, // 3 0
	{"%rsi", "%esi", "%si", "%sil"}, // 4 0
	{"%rdi", "%edi", "%di", "%dil"}, // 5 0
	{"%rbp", "%ebp", "%bp", "%bpl"}, // 6 1
	{"%rsp", "%esp", "%sp", "%spl"}, // 7 1
	{"%r8", "%r8d", "%r8w", "%r8b"}, // 8 0
	{"%r9", "%r9d", "%r9w", "%r9b"}, // 9 0
	{"%r10", "%r10d", "%r10w", "%r10b"}, // 10 0
	{"%r11", "%r11d", "%r11w", "%r11b"}, // 11 0
	{"%r12", "%r12d", "%r12w", "%r12b"}, // 12 1
	{"%r13", "%r13d", "%r13w", "%r13b"}, // 13 1
	{"%r14", "%r14d", "%r14w", "%r14b"}, // 14 1
	{"%r15", "%r15d", "%r15w", "%r15b"}}; // 15 1

int size_to_idx(int size) {
	switch (size) {
	case 8: return 0;
	case 4: return 1;
	case 2: return 2;
	case 1: return 3;
	default: ICE("Invalid register size, %d", size);
	}
}

char size_to_suffix(int size) {
	switch (size) {
	case 8: return 'q';
	case 4: return 'l';
	case 2: return 'w';
	case 1: return 'b';
	default: ICE("Invalid register size %d", size);
	}
}

const char *get_reg_name(int id, int size) {
	return registers[id][size_to_idx(size)];
}

void scalar_to_reg(var_id scalar, int reg) {
	int size = get_variable_size(scalar);
	struct operand mem = MEM(-variable_info[scalar].stack_location, REG_RBP);
	switch (size) {
	case 1:
		asm_ins2("xorq", R8(reg), R8(reg));
		asm_ins2("movb", mem, R1(reg));
		break;
	case 2:
		asm_ins2("xorq", R8(reg), R8(reg));
		asm_ins2("movw", mem, R2(reg));
		break;
	case 4:
		asm_ins2("movl", mem, R4(reg));
		break;
	case 8:
		asm_ins2("movq", mem, R8(reg));
		break;
	}
}

void reg_to_scalar(int reg, var_id scalar) {
	int size = get_variable_size(scalar);
	int msize = 0;
	for (int i = 0; i < size;) {
		if (msize)
			asm_ins2("shrq", IMM(msize * 8), R8(reg));

		struct operand mem = MEM(-variable_info[scalar].stack_location + i, REG_RBP);
		if (i + 8 <= size) {
			asm_ins2("movq", R8(reg), mem);
			msize = 8;
		} else if (i + 4 <= size) {
			asm_ins2("movl", R4(reg), mem);
			msize = 4;
		} else if (i + 2 <= size) {
			asm_ins2("movw", R2(reg), mem);
			msize = 2;
		} else if (i + 1 <= size) {
			asm_ins2("movb", R1(reg), mem);
			msize = 1;
		}

		i += msize;
	}
}

void load_address(struct type *type, var_id result) {
	if (type_is_pointer(type)) {
		asm_ins2("movq", MEM(0, REG_RDI), R8(REG_RAX));
		reg_to_scalar(REG_RAX, result);
	} else if (type->type == TY_SIMPLE) {
		switch (type->simple) {
		case ST_INT:
			asm_ins2("movl", MEM(0, REG_RDI), R4(REG_RAX));
			reg_to_scalar(REG_RAX, result);
			break;

		default:
			NOTIMP();
		}
	} else {
		ICE("Can't load type %s", dbg_type(type));
	}
}

void store_address(struct type *type, var_id result) {
	if (type_is_pointer(type)) {
		scalar_to_reg(result, REG_RAX);
		asm_ins2("movq", R8(REG_RAX), MEM(0, REG_RDI));
	} else if (type->type == TY_SIMPLE) {
		switch (type->simple) {
		case ST_INT:
			scalar_to_reg(result, REG_RAX);
			asm_ins2("movl", R4(REG_RAX), MEM(0, REG_RDI));
			break;

		default:
			NOTIMP();
		}
	} else {
		NOTIMP();
	}
}
