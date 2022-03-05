#include <assert.h>
#include <string.h>
#include <stdio.h>

int main() {
	// 6.7.9p11
	int i1 = 10;
	int i2 = {10};
	int i3 = {10 * 10 / 10};
	assert(i1 == i2);
	assert(i1 == i3);

	unsigned a = (char)-1; // Type conversion.
	assert(a == 0xffffffff);

	// 6.7.9p13
	struct T {
		int a, b;
	};

	struct T t1;
	t1.a = 10;
	t1.b = 20;
	struct T t2 = t1;
	assert(t2.a == t1.a && t2.b == t2.b);


	// 6.7.9p14
	char str1[] = "Hello";
	char str2[] = {"Hello"};
	//char small_arr[4] = "ABCD";
	assert(strcmp(str1, str2) == 0);
	/* assert(small_arr[3] == 'D'); */
	/* assert(sizeof small_arr == 4); */

	// Misc brace-enclosed initializer lists.
	// Most of these represent really bad code,
	// and give warnings on both GCC and clang.
	// But since they are technically correct,
	// they must be implemented correctly.
	struct T t3 = {{64}, {64}};
	assert(t3.a == t3.b && t3.a == 64);

	struct T2 {
		int a;
		struct {
			char arr[6];
			int c;
		} inner;
		int outer;
	};

	struct T2 t4 = {
		10,
		{"Hello"},
		7
	};

	assert(t4.inner.c == 0);

	struct T3 {
		int a;
		char arr[6];
		struct {
			int c;
		} inner;
	};

	struct T3 t5 = {
		10,
		{"Hello"},
		7
	};

	assert(t5.inner.c == 7);

	struct T4 {
		struct T5 {
			int a;
			struct T6 {
				int a;
				int b;
			} t6;
		} t5;
	};

	struct T4 t6 = { (struct T5) { 1, 2 } };
	struct T4 t7 = { .t5.a = 1, (struct T6) { 1, 2 } };
	struct T4 t8 = { 1, 2, 3 };

	assert(t6.t5.a == 1 && t6.t5.t6.a == 2);
	assert(t8.t5.a == 1 && t8.t5.t6.a == 2 && t8.t5.t6.b == 3);
	assert(t7.t5.a == 1);

	struct T7 {
		int arr[4];
	};

	struct T7 t9 = {1, 2, 3, 4};
	assert(t9.arr[3] == 4);

	struct T8 {
		struct {
			int a, b;
		} inner;
		int c;
	};

	{
		struct T8 t = {.inner.a = 2, 4};
		assert(t.inner.a == 2 && t.inner.b == 4 && t.c == 0);
	}

	char arr[] = {"hello"[0], 'b', 'c'};

	const char *matrix[2][2] = {
		[0][0] = "AAA",
		[1][1] = "BBB",
	};

	assert(strcmp(matrix[0][0], "AAA") == 0);

	{
		struct T {
			int a;
			union {
				int b, c;
			};
			struct T2 {
				int d, e;
			} t2;
		};

		struct T t = {1, 2, {3, 4}};
		assert(t.t2.d == 3 && t.a == 1 && t.b == 2);
	}

	{
		struct T {
			int x;
			int y;
			int z;
		} t = {.z = 3, .x = 2, 5};
		assert(t.z == 3 && t.x == 2 && t.y == 5);
	}

	unsigned char ustr[] = "string";
	signed char sstr[] = "string";
	assert(strcmp((char *)ustr, "string") == 0);
	assert(strcmp((char *)sstr, "string") == 0);

	{
		char str1[] = "ABC\0\0\0DEF";
		char str2[] = "\0\0\0ABCDEF";
		char str3[] = "ABCDEF\0\0\0";

		assert(sizeof str1 == 10);
		assert(sizeof str2 == 10);
		assert(sizeof str3 == 10);
	}

	{
		struct T {
			struct {
				int a, b, c;
			};
		} t = { .a = 1, .c = 2, .b = 3 };
		assert(t.b == 3);
	}

	{
		int a[] = {5, [2] = 2, 3};
		assert(sizeof a == 4 * sizeof(int));
	}

	{
		struct T {
			struct {
				int a, b;
			};
		};

		struct T t = {
			.a = 3,
			.b = 10,
		};

		assert(t.a == 3);
	}

	{
		struct Taa { int a, b; } t = {1, 2};
		struct Tbb {
			struct Taa a;
			int b;
		} t2 = {t, 10};

		assert(t2.a.b == 2);
	}

	{
		static struct T {
			int a;
		} t;

		static int *ptr = &t.a;
		*ptr = 13;
		assert(t.a == 13);
	}
}
