#ifndef LOG_H
#define LOG_H
/**
 * \brief log level: notice
 */
#define LOG_LEVEL_NOTICE 1

/**
 * \brief the default log level
 */
#define DEFAULT_LOG_LEVEL LOG_LEVEL_NOTICE

/**
 * \brief Get the log level from the environment.
 */
int log_init();

/**
 * \brief log printf
 * \details This is for printing nice log messages
 */
void LOG_PRINTF(int level, const char *file, int line, const char *format, ...);

/**
 * \brief log printf
 * \details This macro automatically prints out the filename and line number
 */
#define log_printf(level, ...) \
    LOG_PRINTF (level, __FILE__, __LINE__, __VA_ARGS__);
#endif