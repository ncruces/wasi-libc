#if defined(__wasm_simd128__) && defined(__wasilibc_simd_string)

#define _GNU_SOURCE
#include <string.h>
#include <stdint.h>
#include <ctype.h>
#include <wasm_simd128.h>
#include <__macro_PAGESIZE.h>

// SIMD-friendly algorithms for substring searching
// http://0x80.pl/notesen/2016-11-28-simd-strfind.html

// For haystacks of known length and large enough needles,
// Boyer-Moore's bad-character rule may be useful,
// as proposed by Horspool, Sunday and Raita.
//
// We augment the SIMD algorithm with Quick Search's
// bad-character shift.
//
// https://igm.univ-mlv.fr/~lecroq/string/node14.html
// https://igm.univ-mlv.fr/~lecroq/string/node18.html
// https://igm.univ-mlv.fr/~lecroq/string/node19.html
// https://igm.univ-mlv.fr/~lecroq/string/node22.html

static const char *__memmem(const char *haystk, size_t sh,  //
                            const char *needle, size_t sn,  //
                            uint8_t bmbc[256]) {
  // We've handled empty and single character needles.
  // The needle is not longer than the haystack.
  __builtin_assume(2 <= sn && sn <= sh);

  // Find the farthest character not equal to the first one.
  size_t i = sn - 1;
  while (i > 0 && needle[0] == needle[i]) i--;
  if (i == 0) i = sn - 1;

  // Subtracting ensures sub_overflow overflows
  // when we reach the end of the haystack.
  if (sh != SIZE_MAX) sh -= sn;

  const v128_t fst = wasm_i8x16_splat(needle[0]);
  const v128_t lst = wasm_i8x16_splat(needle[i]);

  // The last haystack offset for which loading blk_lst is safe.
  const char *H = (char *)(__builtin_wasm_memory_size(0) * PAGESIZE -  //
                           (sizeof(v128_t) + i));

  while (haystk <= H) {
    const v128_t blk_fst = wasm_v128_load((v128_t *)(haystk));
    const v128_t blk_lst = wasm_v128_load((v128_t *)(haystk + i));
    const v128_t eq_fst = wasm_i8x16_eq(fst, blk_fst);
    const v128_t eq_lst = wasm_i8x16_eq(lst, blk_lst);

    const v128_t cmp = eq_fst & eq_lst;
    if (wasm_v128_any_true(cmp)) {
      // The terminator may come before the match.
      if (sh == SIZE_MAX && !wasm_i8x16_all_true(blk_fst)) break;
      // Find the offset of the first one bit (little-endian).
      // Each iteration clears that bit, tries again.
      for (uint32_t mask = wasm_i8x16_bitmask(cmp); mask; mask &= mask - 1) {
        size_t ctz = __builtin_ctz(mask);
        // The match may be after the end of the haystack.
        if (ctz > sh) return NULL;
        // We know the first character matches.
        if (!bcmp(haystk + ctz + 1, needle + 1, sn - 1)) {
          return haystk + ctz;
        }
      }
    }

    size_t skip = sizeof(v128_t);
    if (sh == SIZE_MAX) {
      // Have we reached the end of the haystack?
      if (!wasm_i8x16_all_true(blk_fst)) return NULL;
    } else {
      // Apply the bad-character rule to the character to the right
      // of the righmost character of the search window.
      if (bmbc) skip += bmbc[(unsigned char)haystk[sn - 1 + sizeof(v128_t)]];
      // Have we reached the end of the haystack?
      if (__builtin_sub_overflow(sh, skip, &sh)) return NULL;
    }
    haystk += skip;
  }

  // Scalar algorithm.
  for (size_t j = 0; j <= sh; j++) {
    for (size_t i = 0;; i++) {
      if (sn == i) return haystk;
      if (sh == SIZE_MAX && !haystk[i]) return NULL;
      if (needle[i] != haystk[i]) break;
    }
    haystk++;
  }
  return NULL;
}

void *memmem(const void *vh, size_t sh, const void *vn, size_t sn) {
  // Return immediately on empty needle.
  if (sn == 0) return (void *)vh;

  // Return immediately when needle is longer than haystack.
  if (sn > sh) return NULL;

  // Skip to the first matching character using memchr,
  // thereby handling single character needles.
  const char *needle = (char *)vn;
  const char *haystk = (char *)memchr(vh, *needle, sh);
  if (!haystk || sn == 1) return (void *)haystk;

  // The haystack got shorter, is the needle now longer than it?
  sh -= haystk - (char *)vh;
  if (sn > sh) return NULL;

  // Is Boyer-Moore's bad-character rule useful?
  if (sn < sizeof(v128_t) || sh - sn < sizeof(v128_t)) {
    return (void *)__memmem(haystk, sh, needle, sn, NULL);
  }

  // Compute Boyer-Moore's bad-character shift function.
  // Only the last 255 characters of the needle matter for shifts up to 255,
  // which is good enough for most needles.
  size_t c = sn;
  size_t i = 0;
  if (c >= 255) {
    i = sn - 255;
    c = 255;
  }

#ifndef _REENTRANT
  static
#endif
  uint8_t bmbc[256];
  memset(bmbc, c, sizeof(bmbc));
  for (; i < sn; i++) {
    // One less than the usual offset because
    // we advance at least one vector at a time.
    bmbc[(unsigned char)needle[i]] = sn - i - 1;
  }

  return (void *)__memmem(haystk, sh, needle, sn, bmbc);
}

char *strstr(const char *haystk, const char *needle) {
  // Return immediately on empty needle.
  if (!needle[0]) return (char *)haystk;

  // Skip to the first matching character using strchr,
  // thereby handling single character needles.
  haystk = strchr(haystk, *needle);
  if (!haystk || !needle[1]) return (char *)haystk;

  return (char *)__memmem(haystk, SIZE_MAX, needle, strlen(needle), NULL);
}

char *strcasestr(const char *haystk, const char *needle) {
  // Return immediately on empty needle.
  if (!needle[0]) return (char *)haystk;

  // We've handled empty needles.
  size_t sn = strlen(needle);
  __builtin_assume(sn >= 1);

  // Find the farthest character not equal to the first one.
  size_t i = sn - 1;
  while (i > 0 && needle[0] == needle[i]) i--;
  if (i == 0) i = sn - 1;

  const v128_t fstl = wasm_i8x16_splat(tolower(needle[0]));
  const v128_t fstu = wasm_i8x16_splat(toupper(needle[0]));
  const v128_t lstl = wasm_i8x16_splat(tolower(needle[i]));
  const v128_t lstu = wasm_i8x16_splat(toupper(needle[i]));

  // The last haystk offset for which loading blk_lst is safe.
  const char *H = (char *)(__builtin_wasm_memory_size(0) * PAGESIZE -  //
                           (sizeof(v128_t) + i));

  while (haystk <= H) {
    const v128_t blk_fst = wasm_v128_load((v128_t *)(haystk));
    const v128_t blk_lst = wasm_v128_load((v128_t *)(haystk + i));
    const v128_t eq_fst =
        wasm_i8x16_eq(fstl, blk_fst) | wasm_i8x16_eq(fstu, blk_fst);
    const v128_t eq_lst =
        wasm_i8x16_eq(lstl, blk_lst) | wasm_i8x16_eq(lstu, blk_lst);

    const v128_t cmp = eq_fst & eq_lst;
    if (wasm_v128_any_true(cmp)) {
      // The terminator may come before the match.
      if (!wasm_i8x16_all_true(blk_fst)) break;
      // Find the offset of the first one bit (little-endian).
      // Each iteration clears that bit, tries again.
      for (uint32_t mask = wasm_i8x16_bitmask(cmp); mask; mask &= mask - 1) {
        size_t ctz = __builtin_ctz(mask);
        if (!strncasecmp(haystk + ctz + 1, needle + 1, sn - 1)) {
          return (char *)haystk + ctz;
        }
      }
    }

    // Have we reached the end of the haystack?
    if (!wasm_i8x16_all_true(blk_fst)) return NULL;
    haystk += sizeof(v128_t);
  }

  // Scalar algorithm.
  for (;;) {
    for (size_t i = 0;; i++) {
      if (sn == i) return (char *)haystk;
      if (!haystk[i]) return NULL;
      if (tolower(needle[i]) != tolower(haystk[i])) break;
    }
    haystk++;
  }
  return NULL;
}

#endif
