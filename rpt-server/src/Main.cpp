#include <cstdlib>
#include <RpT-Config/Config.hpp>
#include <RpT-Core/Executor.hpp>
#include <RpT-Utils/CommandLineOptionsParser.hpp>


/**
 * @brief Tests purposes IO interface, should be deleted in further build
 */
class SimpleIO : public RpT::Core::InputOutputInterface {
private:
    RpT::Utils::LoggerView logger_;

public:
    SimpleIO(RpT::Utils::LoggingContext& logger_context)
    : RpT::Core::InputOutputInterface {}, logger_ { "IO-Events", logger_context } {}

    RpT::Core::AnyInputEvent waitForInput() override {
        return RpT::Core::StopEvent { "Server", 0 };
    }

    void replyTo(const RpT::Core::ServiceRequestEvent& service_request, const bool success,
                 const std::optional<std::string>& error_message) override {

        logger_.info("Reply to {} \"{}\": {}", service_request.actor(), service_request.serviceRequest(), success);

        if (!success)
            logger_.error("Error: {}", *error_message);
    }

    void outputRequest(const RpT::Core::ServiceRequestEvent& service_request) override {
        logger_.info("Request handled for {}: \"{}\"", service_request.actor(), service_request.serviceRequest());
    }

    void outputEvent(const std::string& event) override {
        logger_.info("Event triggered: \"{}\"", event);
    }
};


constexpr int SUCCESS { 0 };
constexpr int INVALID_ARGS { 1 };
constexpr int RUNTIME_ERROR { 2 };


/**
 * @brief Parses log level string and converts to enum value.
 *
 * @param level String or first char of log level
 *
 * @throws std::invalid_argument If level string cannot be parsed into LogLevel value
 *
 * @return Corresponding `RpT::Core::LogLevel` enum value
 */
constexpr RpT::Utils::LogLevel parseLogLevel(const std::string_view level) {
    // Initialization is necessary for constexpr function until C++20
    RpT::Utils::LogLevel parsed_level { RpT::Utils::LogLevel::INFO };

    if (level == "t" || level == "trace")
        parsed_level = RpT::Utils::LogLevel::TRACE;
    else if (level == "d" || level == "debug")
        parsed_level = RpT::Utils::LogLevel::DEBUG;
    else if (level == "i" || level == "info")
        parsed_level = RpT::Utils::LogLevel::INFO;
    else if (level == "w" || level == "warn")
        parsed_level = RpT::Utils::LogLevel::WARN;
    else if (level == "e" || level == "error")
        parsed_level = RpT::Utils::LogLevel::ERROR;
    else if (level == "f" || level == "fatal")
        parsed_level = RpT::Utils::LogLevel::FATAL;
    else
        throw std::invalid_argument { "Unable to parse level \"" + std::string { level }+ "\"" };

    return parsed_level;
}

int main(const int argc, const char** argv) {
    RpT::Utils::LoggingContext server_logging;
    RpT::Utils::LoggerView logger { "Main", server_logging };

    try {
        // Read and parse command line options
        const RpT::Utils::CommandLineOptionsParser cmd_line_options { argc, argv, { "game", "log-level" } };

        // Get game name from command line options
        const std::string_view game_name { cmd_line_options.get("game") };

        // Try to get and parse logging level from command line options
        if (cmd_line_options.has("log-level")) { // Only if option is enabled
            try {
                // Get option value from command line
                const std::string_view log_level_argument { cmd_line_options.get("log-level") };
                // Parse option value
                const RpT::Utils::LogLevel parsed_log_level { parseLogLevel(log_level_argument) };

                server_logging.updateLoggingLevel(parsed_log_level);
                logger.debug("Logging level set to \"{}\".", log_level_argument);
            } catch (const std::logic_error& err) { // Option value may be missing, or parse may fail
                logger.error("Log-level parsing: {}", err.what());
                logger.warn("log-level option has been ignored, \"info\" will be used.");
            }
        }

        logger.info("Running RpT server {} on {}.", RpT::Config::VERSION, RpT::Config::runtimePlatformName());

        std::vector<boost::filesystem::path> game_resources_path;
        // At max 3 paths : next to server executable, into user directory and into /usr/share for Unix platform
        game_resources_path.reserve(3);

        boost::filesystem::path local_path { ".rpt-server" };
        boost::filesystem::path user_path; // Platform dependent

        // Get user home directory depending of the current runtime platform
        if constexpr (RpT::Config::isUnixBuild()) {
            user_path = std::getenv("HOME");
        } else {
            user_path = std::getenv("UserProfile");
        }

        user_path /= ".rpt-server"; // Search for .rpt-server hidden subdirectory inside user directory

        if constexpr (RpT::Config::isUnixBuild()) { // Unix systems may have /usr/share directory for programs data
            boost::filesystem::path system_path { "/usr/share/rpt-server" };

            game_resources_path.push_back(std::move(system_path)); // Add Unix /usr/share directory
        }

        // Add path for subdirectory inside working directory and inside user directory
        game_resources_path.push_back(std::move(user_path));
        game_resources_path.push_back(std::move(local_path));

        // Create executor with listed resources paths, game name argument and run main loop with simple IO interface
        // required to build
        SimpleIO io { server_logging };
        RpT::Core::Executor rpt_executor { std::move(game_resources_path), std::string { game_name }, io, server_logging };
        const bool done_successfully { rpt_executor.run() };

        // Process exit code depends on main loop result
        if (done_successfully) {
            logger.info("Successfully shut down.");

            return SUCCESS;
        } else {
            logger.fatal("Shut down for unhandled error.");

            return RUNTIME_ERROR;
        }
    } catch (const RpT::Utils::OptionsError& err) {
        logger.fatal("Command line error: {}", err.what());

        return INVALID_ARGS;
    }
}
