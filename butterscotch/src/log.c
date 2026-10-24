#include "log.h"
#include <stdarg.h>

void platformLog(const logType type, const char *format, va_list va);

void logInfo(const char* fmt, ...) {
    va_list va;
    va_start(va, fmt);
    platformLog(LOG_TYPE_NORMAL, fmt, va);
    va_end(va);
}

void vLogInfo(const char* fmt, va_list va) {
    platformLog(LOG_TYPE_NORMAL, fmt, va);
}

void logWarn(const char* fmt, ...) {
    va_list va;
    va_start(va, fmt);
    platformLog(LOG_TYPE_WARNING, fmt, va);
    va_end(va);
}

void vLogWarn(const char* fmt, va_list va) {
    platformLog(LOG_TYPE_WARNING, fmt, va);
}

void logError(const char* fmt, ...) {
    va_list va;
    va_start(va, fmt);
    platformLog(LOG_TYPE_ERROR, fmt, va);
    va_end(va);
}

void vLogError(const char* fmt, va_list va) {
    platformLog(LOG_TYPE_ERROR, fmt, va);
}

void logDebug(const char* fmt, ...) {
    va_list va;
    va_start(va, fmt);
    platformLog(LOG_TYPE_DEBUG, fmt, va);
    va_end(va);
}

void vLogDebug(const char* fmt, va_list va) {
    platformLog(LOG_TYPE_DEBUG, fmt, va);
}
