/**
 * @file portable.h
 * @brief Portable string utility functions.
 *
 * Provides cross-platform implementations of common string operations
 * that may not be available on all Windows toolchains (e.g. mingw).
 */

#ifndef BFWM_PORTABLE_H
#define BFWM_PORTABLE_H

/**
 * @brief Portable strdup implementation.
 *
 * @param s Null-terminated string to duplicate
 * @return A malloc'd copy of s, or NULL on allocation failure
 */
auto strdup_portable(const char *s) -> char *;

/**
 * @brief Portable case-insensitive string comparison.
 *
 * @param a First string
 * @param b Second string
 * @return 0 if equal, negative if a < b, positive if a > b
 */
auto strcmpi_portable(const char *a, const char *b) -> int;

/**
 * @brief Thread-safe tokenisation (strtok_r equivalent).
 *
 * @param str     String to tokenise (NULL to continue)
 * @param delim   Delimiter characters
 * @param saveptr Save pointer for resuming
 * @return Next token, or NULL if no more tokens
 */
auto ltokenize(char *str, const char *delim, char **saveptr) -> char *;

#endif
