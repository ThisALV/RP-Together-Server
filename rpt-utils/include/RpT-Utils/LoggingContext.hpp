#ifndef RPTOGETHER_SERVER_LOGGINGCONTEXT_HPP
#define RPTOGETHER_SERVER_LOGGINGCONTEXT_HPP

#include <functional>
#include <string_view>
#include <unordered_map>
#include <vector>

/**
 * @file LoggingContext.hpp
 */


namespace RpT::Utils {


class LoggerView; // Need to be referenced by LoggingContext when a LoggerView is regietered

/**
 * @brief Available logging levels to use with `LoggingContext::updateLoggingLevel()`
 *
 * @see LoggingContext
 *
 * @author ThisALV, https://github.com/ThisALV
 */
enum struct LogLevel {
    TRACE, DEBUG, INFO, WARN, ERR, FATAL
};

/**
 * @brief Provides context for multiple LoggerView management
 *
 * This context keep count for all LoggerView that are registered in it, so it can be used to determine logger UID
 * for unique name, and keep trace for last assigned default logging level. Logging can be toggled inside whole
 * context so backend is not called by `LoggerView` logging methods.
 *
 * LoggerView should keep reference to it's assigned context, so it can be refreshed and check for newly assigned
 * default logging level.
 *
 * @author ThisALV, https://github.com/ThisALV
 */
class LoggingContext {
private:
    // Count for each logging backend created, classed by general purpose
    std::unordered_map<std::string_view, std::size_t> logging_backend_records_;
    // Logging level for registered loggers
    LogLevel logging_level_;
    // Logging can be disabled at any moment
    bool enabled_;

public:
    /**
     * @brief Constructs new logging context with empty logging backend records and given default logging level.
     * Logging is enabled by default.
     *
     * @param logging_level Logging level default used by LoggerView referencing this context
     */
    explicit LoggingContext(LogLevel logging_level = LogLevel::INFO);

    /*
     * Entity class semantic
     */

    LoggingContext(const LoggingContext&) = delete;
    LoggingContext& operator=(const LoggingContext&) = delete;

    bool operator==(const LoggingContext&) const = delete;

    /**
     * @brief Increments loggers count for given general purpose and retrieve next logger expected UID for this
     * general purpose
     *
     * @param generic_name General purpose for created logger
     *
     * @returns Expected UID for created logger
     */
    std::size_t newLoggerFor(std::string_view generic_name);

    /**
     * @brief Update logging level for all loggers in this context
     *
     * @param default_logging_level New logging level applied
     */
    void updateLoggingLevel(LogLevel default_logging_level);

    /**
     * @brief Retrieve current default logging level
     *
     * @returns Currently set default logging level
     */
    LogLevel retrieveLoggingLevel() const;

    /**
     * @brief After call, `isEnabled()` returns `true`
     */
    void enable();

    /**
     * @brief After call, `isEnabled()` returns `false`
     */
    void disable();

    /**
     * @brief Gets if `LoggerView` should call backend loggers
     *
     * @returns `true` if messages will be logged, `false` otherwise
     */
    bool isEnabled() const;
};


}


#endif //RPTOGETHER_SERVER_LOGGINGCONTEXT_HPP
