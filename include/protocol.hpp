#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace roborace::protocol {

// Network defaults
inline constexpr std::string_view DEFAULT_HOST = "127.0.0.1";
inline constexpr std::uint16_t DEFAULT_PORT = 8080;
inline constexpr std::size_t MAX_LINE_LENGTH = 65536;

// Protocol message types
inline constexpr std::string_view MSG_SCENARIO_UPLOAD = "SCENARIO_UPLOAD";
inline constexpr std::string_view MSG_SCENARIO_ACCEPTED = "SCENARIO_ACCEPTED";
inline constexpr std::string_view MSG_REGISTER_ROBOT = "REGISTER_ROBOT";
inline constexpr std::string_view MSG_REGISTERED = "REGISTERED";
inline constexpr std::string_view MSG_READY = "READY";
inline constexpr std::string_view MSG_READY_ACCEPTED = "READY_ACCEPTED";
inline constexpr std::string_view MSG_START = "START";
inline constexpr std::string_view MSG_ACTION_REQUEST = "ACTION_REQUEST";
inline constexpr std::string_view MSG_STATE_UPDATE = "STATE_UPDATE";
inline constexpr std::string_view MSG_CLIENT_DONE = "CLIENT_DONE";
inline constexpr std::string_view MSG_DONE_ACCEPTED = "DONE_ACCEPTED";
inline constexpr std::string_view MSG_SESSION_FINISHED = "SESSION_FINISHED";
inline constexpr std::string_view MSG_ERROR = "ERROR";

// Protocol error codes
inline constexpr std::string_view ERR_INVALID_JSON = "INVALID_JSON";
inline constexpr std::string_view ERR_INVALID_MESSAGE = "INVALID_MESSAGE";
inline constexpr std::string_view ERR_INVALID_SCENARIO = "INVALID_SCENARIO";
inline constexpr std::string_view ERR_DUPLICATE_ID = "DUPLICATE_ID";
inline constexpr std::string_view ERR_INVALID_STATE = "INVALID_STATE";

// Reserved actor identifiers
inline constexpr std::string_view ACTOR_SERVER = "SERVER";
inline constexpr std::string_view ACTOR_CONTROLLER = "CONTROLLER";
inline constexpr std::string_view ACTOR_LAUNCHER = "LAUNCHER";

// Phase 1 State Update status
inline constexpr std::string_view STATUS_RECORDED = "RECORDED";

}  // namespace roborace::protocol
