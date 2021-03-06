#include <RpT-Core/Executor.hpp>

#include <type_traits>
#include <RpT-Core/ServiceEventRequestProtocol.hpp>


namespace RpT::Core {

namespace { // Input handler only visible for Executor::run() implementation


/**
 * @brief Provides call operators set, one call operator per InputEvent type
 *
 * Has an access to Executor IO interface, running SER Protocol and Executor's logger
 */
class InputHandler {
private:
    InputOutputInterface& io_interface_;
    ServiceEventRequestProtocol& ser_protocol_;
    Utils::LoggerView& logger_;

public:
    /**
     * @brief Initializes handler to use given IO interface and SER Protocol
     *
     * @param io_interface Input/Output events interface
     * @param ser_protocol Running SER Protocol
     * @param caller_logger Logger used by caller (Executor)
     */
    InputHandler(InputOutputInterface& io_interface, ServiceEventRequestProtocol& ser_protocol,
                 Utils::LoggerView& caller_logger) :

                 io_interface_ { io_interface },
                 ser_protocol_ { ser_protocol },
                 logger_ { caller_logger } {}

    void operator()(const NoneEvent&) {
        logger_.debug("Null event, skipping...");
    }

    void operator()(const ServiceRequestEvent& event) {
        logger_.debug("Service Request command received from player \"{}\".", event.actor());

        const std::uint64_t actor_uid { event.actor() };
        try { // Tries to parse SR command
            // Give SR command to parse and execute by SER Protocol
            const std::string sr_command_response {
                    ser_protocol_.handleServiceRequest(event.actor(), event.serviceRequest())
            };

            // Replies to actor with command handling result
            io_interface_.replyTo(actor_uid, sr_command_response);
        } catch (const BadServiceRequest& err) { // If command cannot be parsed, SRR cannot be sent, pipeline broken
            // It is no longer possible to sync SR with actor as RUID might be wrong, closing pipeline with thrown
            // exception message
            io_interface_.closePipelineWith(actor_uid, Utils::HandlingResult { err.what() });

            logger_.error("SER Protocol broken for actor {}: {}. Closing pipeline...", actor_uid, err.what());
        }
    }

    void operator()(const TimerEvent&) {
        logger_.debug("Timer end, continuing...");
    }

    void operator()(const JoinedEvent& event) {
        logger_.info("Player \"{}\" joined server as actor {}.", event.playerName(), event.actor());
    }

    void operator()(const LeftEvent& event) {
        logger_.info("Actor {} left server.", event.actor());
    }
};


}

Executor::Executor(std::vector<boost::filesystem::path> game_resources_path, std::string game_name,
                   InputOutputInterface& io_interface, Utils::LoggingContext& logger_context) :
    logger_context_ { logger_context },
    logger_ { "Executor", logger_context_ },
    io_interface_ { io_interface } {

    logger_.debug("Game name: {}", game_name);

    for (const boost::filesystem::path& resource_path : game_resources_path)
        logger_.debug("Game resources path: {}", resource_path.string());
}

/**
 * @brief TEMPORARY : Chat service which can be toggled on/off with "/toggle" command
 *
 * Used to test efficiency of `InputOutputInterface::replyTo()`.
 */
class ChatService : public Service {
private:
    static constexpr bool isAdmin(const std::uint64_t actor) {
        return actor == 0;
    }

    /// Parses chat message, checks if it's a /toggle command, and checks if there are no extra argument given to cmd
    class ChatCommandParser : public Utils::TextProtocolParser {
    public:
        /// Parse first word, needs to check if it is a command
        explicit ChatCommandParser(const std::string_view chat_msg)
        : Utils::TextProtocolParser { chat_msg, 1 } {}

        /// Is message starting with /toggle ?
        bool isToggle() const {
            return getParsedWord(0) == "/toggle";
        }

        /// Are there extra unparsed arguments in addition to command ?
        bool extraArgs() const {
            return !unparsedWords().empty();
        }
    };

    bool enabled_;

public:
    explicit ChatService(ServiceContext& run_context) : Service { run_context }, enabled_ { true } {}

    std::string_view name() const override {
        return "Chat";
    }

    Utils::HandlingResult handleRequestCommand(const std::uint64_t actor,
                                               const std::string_view sr_command_data) override {

        try { // If message is empty, parsing will fail. A chat message should NOT be empty
            const ChatCommandParser chat_msg_parser { sr_command_data }; // Parsing message for potential command

            if (chat_msg_parser.isToggle()) { // If message begins with "/toggle"
                if (chat_msg_parser.extraArgs()) // This command hasn't any arguments
                    return Utils::HandlingResult { "Invalid arguments for /toggle: command hasn't any args" };

                if (!isAdmin(actor)) // Player using this command should be admin
                    return Utils::HandlingResult { "Permission denied: you must be admin to use that command" };

                enabled_ = !enabled_;

                emitEvent(enabled_ ? "ENABLED" : "DISABLED");

                return {}; // State was successfully changed
            } else if (enabled_) { // Checks for chat being enabled or not
                emitEvent("MESSAGE_FROM " + std::to_string(actor) + ' ' + std::string { sr_command_data });

                return {}; // Message should be sent to all players if chat is enabled
            } else { // If it isn't, message can't be sent
                return Utils::HandlingResult { "Chat disabled by admin." };
            }
        } catch (const Utils::NotEnoughWords&) { // If there is not at least one word to parse (message is empty)
            return Utils::HandlingResult { "Message cannot be empty" };
        }
    }
};

bool Executor::run() {
    logger_.info("Initializing online services...");

    /*
     * Initializes services and protocol
     */

    ServiceContext ser_protocol_context; // Context in which all online services will be running on
    ChatService chat_svc { ser_protocol_context }; // A test service fot chat feature

    // Protocol initialization with created services
    ServiceEventRequestProtocol ser_protocol {{ chat_svc }, logger_context_ };
    // Functions set for input events handling
    InputHandler input_handler { io_interface_, ser_protocol, logger_ };

    logger_.info("Starts main loop.");

    try { // Any errors occurring during main loop execution will
        while (!io_interface_.closed()) { // Main loop must run as long as inputs and outputs with players can occur
            // Blocking until receiving external event to handle (timer, data packet, etc.)
            const AnyInputEvent input_event { io_interface_.waitForInput() };

            boost::apply_visitor(input_handler, input_event);

            // After input event has been handled, events emitted by services should also be handled in the order they
            // appeared

            logger_.debug("Polling service events...");

            std::optional<std::string> next_svc_event { ser_protocol.pollServiceEvent() }; // Read first event
            while (next_svc_event) { // Then while next event actually exists, handles it
                logger_.debug("Output event: {}", *next_svc_event);
                io_interface_.outputEvent(*next_svc_event); // Sent across actors

                next_svc_event = ser_protocol.pollServiceEvent(); // Read next event
            }

            logger_.debug("Events polled.");
        }

        logger_.info("Stopped.");

        return true;
    } catch (const std::exception& err) {
        logger_.error("Runtime error: {}", err.what());

        return false;
    }
}

}
