#ifndef CLOX_ERROR_H
#define CLOX_ERROR_H

#define CLOX_FATAL_ERROR(msg) clox_fatal_error((msg), __FILE__, __LINE__)

_Noreturn void clox_fatal_error(const char *message, const char *file, int line);

#endif
