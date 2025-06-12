#define _BSD_SOURCE
#include <string.h>
#include <strings.h>

#ifdef __wasm_simd128__
#include <wasm_simd128.h>
#endif

int bcmp(const void *s1, const void *s2, size_t n)
{
#if defined(__wasm_simd128__) && defined(__wasilibc_simd_string)
	if (n >= sizeof(v128_t)) {
		// bcmp is allowed to read up to n bytes from each object.
		// Unaligned loads handle the case where the objects
		// have mismatching alignments.
		const v128_t *v1 = (v128_t *)s1;
		const v128_t *v2 = (v128_t *)s2;
		while (n) {
			// Find any single bit difference.
			if (wasm_v128_any_true(wasm_v128_load(v1) ^ wasm_v128_load(v2))) {
				return 1;
			}
			// This makes n a multiple of sizeof(v128_t)
			// for every iteration except the first.
			size_t align = (n - 1) % sizeof(v128_t) + 1;
			v1 = (v128_t *)((char *)v1 + align);
			v2 = (v128_t *)((char *)v2 + align);
			n -= align;
		}
		return 0;
	}

	// Scalar algorithm.
	const unsigned char *u1 = (unsigned char *)s1;
	const unsigned char *u2 = (unsigned char *)s2;
	while (n--) {
		if (*u1 != *u2) return 1;
		u1++;
		u2++;
	}
	return 0;
#else
	return memcmp(s1, s2, n);
#endif
}
