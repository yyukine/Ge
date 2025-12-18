#pragma once
#include <ntifs.h>
#include "../Core/config.h"

// Logging macros
#if ENABLE_LOGGING

#define LOG_ERROR(fmt, ...)   DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "[UD][ERROR] " fmt "\n", ##__VA_ARGS__)
#define LOG_WARNING(fmt, ...) if (LOG_LEVEL >= 2) DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_WARNING_LEVEL, "[UD][WARN] " fmt "\n", ##__VA_ARGS__)
#define LOG_INFO(fmt, ...)    if (LOG_LEVEL >= 3) DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "[UD][INFO] " fmt "\n", ##__VA_ARGS__)
#define LOG_DEBUG(fmt, ...)   if (LOG_LEVEL >= 4) DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "[UD][DEBUG] " fmt "\n", ##__VA_ARGS__)

#else

#define LOG_ERROR(fmt, ...)   ((void)0)
#define LOG_WARNING(fmt, ...) ((void)0)
#define LOG_INFO(fmt, ...)    ((void)0)
#define LOG_DEBUG(fmt, ...)   ((void)0)

#endif
