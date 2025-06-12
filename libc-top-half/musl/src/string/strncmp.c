#include <string.h>

#ifdef __wasm_simd128__
#include <wasm_simd128.h>
#include <__macro_PAGESIZE.h>
#endif

int strncmp(const char *_l, const char *_r, size_t n)
{
	const unsigned char *l=(void *)_l, *r=(void *)_r;

#if defined(__wasm_simd128__) && defined(__wasilibc_simd_string)
	// How many bytes can be read before pointers go out of bounds.
	size_t N = __builtin_wasm_memory_size(0) * PAGESIZE - (size_t)(l > r ? l : r);
	if (n > N) n = N;

	// Unaligned loads handle the case where the strings
	// have mismatching alignments.
	const v128_t *v1 = (v128_t *)l;
	const v128_t *v2 = (v128_t *)r;
	for (; n >= sizeof(v128_t); n -= sizeof(v128_t)) {
		// Find any single bit difference.
		if (wasm_v128_any_true(wasm_v128_load(v1) ^ wasm_v128_load(v2))) {
			// The terminator may come before the difference.
			break;
		}
		// We know all characters are equal.
		// If any is a terminator the strings are equal.
		if (!wasm_i8x16_all_true(wasm_v128_load(v1))) {
			return 0;
		}
		v1++;
		v2++;
	}

	l = (unsigned char *)v1;
	r = (unsigned char *)v2;
#endif

	if (!n--) return 0;
	for (; *l && *r && n && *l == *r ; l++, r++, n--);
	return *l - *r;
}
