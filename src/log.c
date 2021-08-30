#include "log.h"

#include "config.h"
#include "util.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

int log_level_init()
{
	char *env = getenv("HTTPDIRFS_LOG_LEVEL");
	if (env) {
		return atoi(env);
	}
	return DEFAULT_LOG_LEVEL;
}

void
log_printf(LogType type, const char *file, const char *func, int line,
	   const char *format, ...)
{
	FILE *out = stderr;
	if (type & CONFIG.log_type) {
		switch (type) {
		case fatal:
			fprintf(out, "Fatal: ");
			break;
		case error:
			fprintf(out, "Error: ");
			break;
		case warning:
			fprintf(out, "Warning: ");
			break;
		case info:
			out = stdout;
			goto print_actual_message;
			break;
		default:
			fprintf(out, "Debug (%x):", type);
			break;
		}

		fprintf(out, "(%s:%s:%d): ", file, func, line);

 print_actual_message:
		{
		}
		va_list args;
		va_start(args, format);
		vfprintf(out, format, args);
		va_end(args);

		if (type == fatal) {
			exit_failure();
		}
	}
}
