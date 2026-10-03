/*
 * SHL - Library Log/Debug Interface
 *
 * Copyright (c) 2010-2013 David Herrmann <dh.herrmann@gmail.com>
 * Dedicated to the Public Domain
 */

/*
 * Library Log/Debug Interface
 * Libraries should always avoid producing side-effects. This includes writing
 * log-messages of any kind. However, you often don't want to disable debugging
 * entirely, therefore, the core objects often contain a pointer to a function
 * which performs logging. If that pointer is NULL (default), logging is
 * disabled.
 *
 * This header should never be installed into the system! This is _no_ public
 * header. Instead, copy it into your application if you want and use it there.
 * Your public library API should include something like this:
 *
 *   typedef void (*MYPREFIX_log_t) (void *data,
 *                                   const char *file,
 *                                   int line,
 *                                   const char *func,
 *                                   const char *subs,
 *                                   unsigned int sev,
 *                                   const char *format,
 *                                   va_list args);
 *
 * And then the user can supply such a function when creating a new context
 * object of your library or simply supply NULL. Internally, you have a field of
 * type "MYPREFIX_log_t llog" in your main structure. If you pass this to the
 * convenience helpers like llog_dbg(), llog_warn() etc. it will automatically
 * use the "llog" field to print the message. If it is NULL, nothing is done.
 *
 * The arguments of the log-function are defined as:
 *   data: User-supplied data field that is passed straight through.
 *   file: Zero terminated string of the file-name where the log-message
 *         occurred. Can be NULL.
 *   line: Line number of @file where the message occurred. Set to 0 or smaller
 *         if not available.
 *   func: Function name where the log-message occurred. Can be NULL.
 *   subs: Subsystem where the message occurred (zero terminated). Can be NULL.
 *   sev: Severity of log-message. An integer between 0 and 7 as defined below.
 *        These are identical to the linux-kernel severities so there is no need
 *        to include these in your public API. Every app can define them
 *        themselves, if they need it.
 *   format: Format string. Must not be NULL.
 *   args: Argument array
 *
 * The user should also be able to optionally provide a data field which is
 * always passed unmodified as first parameter to the log-function. This allows
 * to add context to the logger.
 */

#ifndef SHL_LLOG_H
#define SHL_LLOG_H

#include <errno.h>
#include <zephyr/logging/log.h>

/*
 * libtsm logging compatibility wrapper for Zephyr.
 *
 * The original shl-llog API supports:
 *   - logging callbacks
 *   - arbitrary logger userdata
 *   - subsystem names
 *   - syslog-like severity levels
 *
 * For the Zephyr port those concepts are mapped onto Zephyr's logging
 * subsystem. The obj/data arguments are intentionally ignored so existing
 * libtsm call sites do not have to change.
 *
 * One translation unit must contain:
 *
 *     LOG_MODULE_REGISTER(libtsm, CONFIG_LIBTSM_LOG_LEVEL);
 *
 * All users of this header then share that logging module.
 */

/*
 * Generic logging helpers.
 *
 * Keep the original names and signatures for source compatibility.
 */

#define llog_debug(obj, format, ...) LOG_DBG(format, ##__VA_ARGS__)

#define llog_ddebug(obj, data, format, ...) LOG_DBG(format, ##__VA_ARGS__)

#define llog_info(obj, format, ...) LOG_INF(format, ##__VA_ARGS__)

#define llog_dinfo(obj, data, format, ...) LOG_INF(format, ##__VA_ARGS__)

/*
 * Zephyr has no NOTICE level. Map it to INFO.
 */
#define llog_notice(obj, format, ...) LOG_INF(format, ##__VA_ARGS__)

#define llog_dnotice(obj, data, format, ...) LOG_INF(format, ##__VA_ARGS__)

#define llog_warning(obj, format, ...) LOG_WRN(format, ##__VA_ARGS__)

#define llog_dwarning(obj, data, format, ...) LOG_WRN(format, ##__VA_ARGS__)

#define llog_error(obj, format, ...) LOG_ERR(format, ##__VA_ARGS__)

#define llog_derror(obj, data, format, ...) LOG_ERR(format, ##__VA_ARGS__)

/*
 * Zephyr has no CRITICAL / ALERT / FATAL log levels.
 * They are all represented as errors.
 */
#define llog_critical(obj, format, ...) LOG_ERR(format, ##__VA_ARGS__)

#define llog_dcritical(obj, data, format, ...) LOG_ERR(format, ##__VA_ARGS__)

#define llog_alert(obj, format, ...) LOG_ERR(format, ##__VA_ARGS__)

#define llog_dalert(obj, data, format, ...) LOG_ERR(format, ##__VA_ARGS__)

#define llog_fatal(obj, format, ...) LOG_ERR(format, ##__VA_ARGS__)

#define llog_dfatal(obj, data, format, ...) LOG_ERR(format, ##__VA_ARGS__)

/*
 * Common errno/error helpers.
 *
 * These preserve the original return-value semantics of shl-llog.
 */

#define llog_dEINVAL(obj, data) (llog_derror((obj), (data), "invalid arguments"), -EINVAL)

#define llog_EINVAL(obj) llog_dEINVAL(NULL, NULL)

#define llog_vEINVAL(obj) ((void) llog_EINVAL(obj))

#define llog_vdEINVAL(obj, data) ((void) llog_dEINVAL((obj), (data)))

#define llog_dEFAULT(obj, data) (llog_derror((obj), (data), "internal operation failed"), -EFAULT)

#define llog_EFAULT(obj) llog_dEFAULT(NULL, NULL)

#define llog_vEFAULT(obj) ((void) llog_EFAULT(obj))

#define llog_vdEFAULT(obj, data) ((void) llog_dEFAULT((obj), (data)))

#define llog_dENOMEM(obj, data) (llog_derror((obj), (data), "out of memory"), -ENOMEM)

#define llog_ENOMEM(obj) llog_dENOMEM(NULL, NULL)

#define llog_vENOMEM(obj) ((void) llog_ENOMEM(obj))

#define llog_vdENOMEM(obj, data) ((void) llog_dENOMEM((obj), (data)))

#define llog_dEPIPE(obj, data) (llog_derror((obj), (data), "fd closed unexpectedly"), -EPIPE)

#define llog_EPIPE(obj) llog_dEPIPE(NULL, NULL)

#define llog_vEPIPE(obj) ((void) llog_EPIPE(obj))

#define llog_vdEPIPE(obj, data) ((void) llog_dEPIPE((obj), (data)))

/*
 * Do not use "%m": Zephyr's formatter should not be assumed to implement
 * the GNU printf extension.
 */
#define llog_dERRNO(obj, data) (llog_derror((obj), (data), "operation failed: errno=%d", errno), -errno)

#define llog_ERRNO(obj) llog_dERRNO(NULL, NULL)

#define llog_vERRNO(obj) ((void) llog_ERRNO(obj))

#define llog_vdERRNO(obj, data) ((void) llog_dERRNO((obj), (data)))

#define llog_dERR(obj, data, result) (llog_derror((obj), (data), "operation failed: %d", (result)), (result))

#define llog_ERR(obj, result) llog_dERR(NULL, NULL, (result))

#define llog_vERR(obj, result) ((void) llog_ERR((obj), (result)))

#define llog_vdERR(obj, data, result) ((void) llog_dERR((obj), (data), (result)))

#endif /* SHL_LLOG_H */
