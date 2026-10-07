/**
 * @file config_paths.h
 * @brief Configuration file path resolution.
 */

#ifndef BFWM_CONFIG_PATHS_H
#define BFWM_CONFIG_PATHS_H

#include <cstddef>

/**
 * @brief Resolve a config file path, searching standard locations.
 *
 * Looks for @p filename in the application directory and user config
 * directory. Writes the first match to @p out_path.
 *
 * @param filename  The config file name (e.g. "config.lua")
 * @param out_path  Buffer receiving the resolved path
 * @param out_size  Size of out_path buffer
 * @return true if a config file was found
 */
auto ResolveConfigPath(const char *filename, char *out_path, size_t out_size)
    -> bool;

#endif
