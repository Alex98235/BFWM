/**
 * @file win_error.h
 * @brief Win32 error logging utility.
 *
 * Provides a helper to log the last Win32 error code along with a
 * context prefix.
 */

#ifndef BFWM_WIN_ERROR_H
#define BFWM_WIN_ERROR_H

/**
 * @brief Log the last Win32 error via the logging subsystem.
 *
 * Retrieves GetLastError(), formats the message, and writes it at
 * LOG_ERROR level with an optional context prefix.
 *
 * @param prefix Optional context string prepended to the error message
 */
void BFWMLogLastError(const char *prefix);

#endif
