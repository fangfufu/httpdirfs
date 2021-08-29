#ifndef LOG_H
#define LOG_H
/**
 * \brief Log type: Informational
 */
#define linfo  1 << 0

/**
 * \brief Log type: Error
 */
#define lerror   1 << 1

/**
 * \brief Log type: Debug
 */
#define ldebug   1 << 2

/**
 * \brief The default log level
 */
#define DEFAULT_LOG_LEVEL ldebug

/**
 * \brief Display filename in log
 */
#define LOG_SHOW_FILENAME   1

/**
 * \brief Display line number in log
 */
#define LOG_SHOW_FILENAME_LINE_NUM  2

/**
 * \brief The default log verbosity
 */
#define DEFAULT_LOG_VERBOSITY LOG_SHOW_FILENAME_LINE_NUM

/**
 * \brief Get the log level from the environment.
 */
int log_level_init();

/**
 * \brief Get the log verbosity from the environment
 */
int log_verbosity_init();

/**
 * \brief Log printf
 * \details This is for printing nice log messages
 */
void log_printf(int type, const char *file, int line, const char *format, ...);

/**
 * \brief Log type printf
 * \details This macro automatically prints out the filename and line number
 */
#define lprintf(type, ...) \
    log_printf(type, __FILE__, __LINE__, __VA_ARGS__);
#endif