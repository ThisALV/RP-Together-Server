#include <RpT-Core/ServiceEventRequestProtocol.hpp>

#include <algorithm>
#include <cassert>


namespace RpT::Core {


ServiceEventRequestProtocol::ServiceRequestCommandParser::ServiceRequestCommandParser(
        const std::string_view sr_command) : Utils::TextProtocolParser { sr_command, 3 } {

    try {
        const std::string ruid_copy { getParsedWord(1) }; // Required for conversion to unsigned integer

        // `unsigned long long` return type, which is ALWAYS 64 bits large
        // See: https://en.cppreference.com/w/cpp/language/types
        parsed_ruid_ = std::stoull(ruid_copy);
    } catch (const std::invalid_argument&) { // If parsed actor UID argument isn't a valid unsigned integer
        throw BadServiceRequest { "Request UID must be an unsigned integer of 64 bits" };
    }
}

bool ServiceEventRequestProtocol::ServiceRequestCommandParser::isValidRequest() const {
    return getParsedWord(0) == REQUEST_PREFIX;
}

std::uint64_t ServiceEventRequestProtocol::ServiceRequestCommandParser::ruid() const {
    return parsed_ruid_;
}

std::string_view ServiceEventRequestProtocol::ServiceRequestCommandParser::intendedServiceName() const {
    return getParsedWord(2);
}

std::string_view ServiceEventRequestProtocol::ServiceRequestCommandParser::commandData() const {
    return unparsedWords();
}


bool ServiceEventRequestProtocol::CachedServiceEventEmitter::operator>(
        const ServiceEventRequestProtocol::CachedServiceEventEmitter& rhs) const {

    assert(emittedEventId != rhs.emittedEventId); // All emitted SE must have an unique ID

    return emittedEventId < rhs.emittedEventId;
}

bool ServiceEventRequestProtocol::CachedServiceEventEmitter::operator<(
        const ServiceEventRequestProtocol::CachedServiceEventEmitter& rhs) const {

    return !(*this > rhs);
}


Service& ServiceEventRequestProtocol::latestEventEmitter() {
    assert(!latest_se_emitters_cache_.empty()); // Cache queue must contains at least one event emitter

    // Retrieves ref for Service which has emitted the next event to poll
    Service& latest_event_emitter { *latest_se_emitters_cache_.top().queuedEmitter };
    // Removes emitter from cache, its event will be polled
    latest_se_emitters_cache_.pop();

    return latest_event_emitter;
}

ServiceEventRequestProtocol::ServiceEventRequestProtocol(
        const std::initializer_list<std::reference_wrapper<Service>>& services,
        Utils::LoggingContext& logging_context) :

        logger_ { "SER-Protocol", logging_context } {

    // Each given service reference must be registered as running service
    for (const auto service_ref : services) {
        const std::string_view service_name { service_ref.get().name() };

        if (isRegistered(service_name)) // Service name must be unique among running services
            throw ServiceNameAlreadyRegistered { service_name };

        const auto service_registration_result { running_services_.insert({ service_name, service_ref }) };
        // Must be sure that service has been successfully registered, this is why insertion result is saved
        assert(service_registration_result.second);

        logger_.debug("Registered service {}.", service_name);
    }
}


bool ServiceEventRequestProtocol::isRegistered(const std::string_view service) const {
    return running_services_.count(service) == 1; // Returns if service name is present among running services
}

std::string ServiceEventRequestProtocol::handleServiceRequest(const std::uint64_t actor,
                                                              const std::string_view service_request) {

    logger_.trace("Handling SR command from \"{}\": {}", actor, service_request);

    std::uint64_t request_uid;
    std::string_view intended_service_name;
    std::string_view command_data;
    try { // Tries to parse received SR command
        const ServiceRequestCommandParser sr_command_parser { service_request }; // Parsing

        if (!sr_command_parser.isValidRequest()) // Checks for SER command prefix, must be REQUEST for a SR command
            throw InvalidRequestFormat { service_request, "Expected SER command prefix \"REQUEST\" for SR command" };

        // Set given parameters to corresponding parsed (or not) arguments
        intended_service_name = sr_command_parser.intendedServiceName();
        request_uid = sr_command_parser.ruid();
        command_data = sr_command_parser.commandData();
    } catch (const Utils::NotEnoughWords& err) { // Catches error if SER command format is invalid
        throw InvalidRequestFormat { service_request, "Expected SER command prefix and request service name" };
    }

    assert(!intended_service_name.empty()); // Service name must be initialized if try statement passed successfully

    // Checks for intended service registration
    if (!isRegistered(intended_service_name))
        throw ServiceNotFound { intended_service_name };

    Service& intended_service { running_services_.at(intended_service_name).get() };

    logger_.trace("SR command successfully parsed, handled by service: {}", intended_service_name);

    // SRR beginning is always `RESPONSE <RUID>`
    const std::string sr_response_prefix {
        std::string { RESPONSE_PREFIX } + ' ' + std::to_string(request_uid) + ' '
    };

    // Try to handle SR command, catching errors occurring inside handlers
    try {
        // Handles SR command and saves result
        const Utils::HandlingResult command_result { intended_service.handleRequestCommand(actor, command_data) };

        if (command_result) // If command was successfully handled, must retrieves OK Service Request Response
            return sr_response_prefix + "OK";
        else // Else, command failed and KO response must be retrieved
            return sr_response_prefix + "KO " + command_result.errorMessage();
    } catch (const std::exception& err) { // If exception is thrown by intended service
        logger_.error("Service \"{}\" failed to handle command: {}" , intended_service_name, err.what());

        // Retrieves error Service Request Response with given caught message `RESPONSE <RUID> KO <ERR_MSG>`
        return sr_response_prefix + std::to_string(request_uid) + " KO " + err.what();
    }
}

std::optional<std::string> ServiceEventRequestProtocol::pollServiceEvent() {
    std::optional<std::string> next_event; // Event to poll is first uninitialized

    Service* latest_event_emitter { nullptr }; // Will be set any event has been emitted by a service

    // There might be events emitter cached if we know they are holding Service Event with the next higher priority
    // (lower unsigned integer)
    if (!latest_se_emitters_cache_.empty()) {
        latest_event_emitter = &latestEventEmitter(); // Saves address for next event emitter
    } else { // If not any Service is cached as next event emitter...
        // ...checks next event ID for each service and caches service into next events emiiters queue

        for (auto [service_name, registered_service] : running_services_) {
            const std::optional<std::size_t> next_event_id { registered_service.get().checkEvent() };

            if (next_event_id.has_value()) { // If Service emitted event...
                // ...then cache emitter into queue with corresponding event emitter priority
                latest_se_emitters_cache_.push({ *next_event_id, &registered_service.get() });

                logger_.trace("Service {} last event ID: {}. Cached as emitter.", service_name, *next_event_id);
            } else { // If Service didn't emit anything
                logger_.trace("Service {} hasn't any event.", service_name);
            }
        }

        // All next known events are cached, next from cached will be retrieved (if any event has been emitted)
        if (!latest_se_emitters_cache_.empty())
            latest_event_emitter = &latestEventEmitter();
    }

    if (latest_event_emitter) { // If there is any emitted event, format it and move it into polled event
        const std::string service_name_copy { latest_event_emitter->name() }; // Required for concatenation

        next_event = std::string { EVENT_PREFIX } + ' ' + service_name_copy + ' ' + latest_event_emitter->pollEvent();

        logger_.trace("Polled event from service {}: {}", latest_event_emitter->name(), *next_event);
    } else {
        logger_.trace("No event to retrieve.");
    }

    return next_event;
}

}