/**
 * @file safe.h
 * @brief Safe memory and string helpers.
 *
 * Wrappers that abort on allocation failure and provide bounded
 * string copy operations to prevent buffer overflows.
 */

#ifndef BFWM_SAFE_H
#define BFWM_SAFE_H

#include <cstddef>
#include <windows.h>

/** @brief Return the number of elements in a static array. */
template <typename T, std::size_t N>
// NOLINTNEXTLINE
[[nodiscard]] constexpr auto ARRAY_SIZE(const T (&)[N]) -> std::size_t {
   return N;
}

/**
 * @brief malloc wrapper that aborts on failure.
 *
 * @param size    Allocation size in bytes
 * @param context Human-readable description for the error message on failure
 * @return Pointer to the allocated memory (never NULL)
 */
auto safe_malloc(size_t size, const char *context) -> void *;

/**
 * @brief calloc wrapper that aborts on failure.
 *
 * @param nmemb   Number of elements
 * @param size    Size of each element
 * @param context Human-readable description for the error message on failure
 * @return Pointer to the zero-initialised memory (never NULL)
 */
auto safe_calloc(size_t nmemb, size_t size, const char *context) -> void *;

/**
 * @brief realloc wrapper that aborts on failure.
 *
 * @param ptr      Previously allocated pointer (may be NULL)
 * @param new_size New allocation size
 * @param context  Human-readable description for the error message on failure
 * @return Pointer to the resized memory (never NULL)
 */
auto safe_realloc(void *ptr, size_t new_size, const char *context) -> void *;

/**
 * @brief Bounded narrow string copy (strncpy with null termination).
 *
 * @param dst      Destination buffer
 * @param dst_size Size of destination buffer
 * @param src      Source string
 */
void safe_strcpy(char *dst, size_t dst_size, const char *src);

#endif
