#include "server_logic.hpp"
#include "cJSON.h"
#include "protocol.hpp"

#include <cstdlib>
#include <utility>

namespace {

namespace proto = roborace::protocol;

bool extract_nonempty_string(const cJSON *obj, const char *key, std::string &out) {
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (!cJSON_IsString(item) || item->valuestring == nullptr || item->valuestring[0] == '\0') {
        return false;
    }
    out = item->valuestring;
    return true;
}

bool chest_exists(const Scenario &scenario, const std::string &chest_id) {
    for (const auto &chest : scenario.chests) {
        if (chest.id == chest_id) {
            return true;
        }
    }
    return false;
}

std::string serialize_and_free(cJSON *obj) {
    if (obj == nullptr) {
        return "{}";
    }
    char *raw = cJSON_PrintUnformatted(obj);
    cJSON_Delete(obj);
    if (raw == nullptr) {
        return "{}";
    }
    std::string result(raw);
    std::free(raw);
    return result;
}

} // namespace

ServerLogic::ServerLogic(ServerNet &net, Logger &logger) : net_(net), logger_(logger) {
}

void ServerLogic::send_response(
    int fd, const std::string &type, const std::string &req_id, const std::string &payload_json
) {
    cJSON *msg = cJSON_CreateObject();
    if (msg == nullptr) {
        return;
    }
    cJSON_AddStringToObject(msg, "type", type.c_str());
    cJSON_AddStringToObject(msg, "race_id", scenario_.race_id.c_str());
    cJSON_AddStringToObject(msg, "sender", std::string(proto::ACTOR_SERVER).c_str());
    if (req_id.empty()) {
        cJSON_AddNullToObject(msg, "reply_to");
    } else {
        cJSON_AddStringToObject(msg, "reply_to", req_id.c_str());
    }
    cJSON_AddRawToObject(msg, "payload", payload_json.c_str());

    net_.send_line(fd, serialize_and_free(msg));
}

void ServerLogic::send_error(
    int fd, const std::string &req_id, const std::string &code, const std::string &msg
) {
    cJSON *payload = cJSON_CreateObject();
    if (payload == nullptr) {
        return;
    }
    cJSON_AddStringToObject(payload, "code", code.c_str());
    cJSON_AddStringToObject(payload, "message", msg.c_str());
    std::string payload_str = serialize_and_free(payload);

    send_response(fd, std::string(proto::MSG_ERROR), req_id, payload_str);
    logger_.log(-1, std::string(proto::ACTOR_SERVER), "RESPOND", "-", "ERROR", req_id);
}

void ServerLogic::broadcast(const std::string &type, const std::string &payload_json) {
    cJSON *msg = cJSON_CreateObject();
    if (msg == nullptr) {
        return;
    }
    cJSON_AddStringToObject(msg, "type", type.c_str());
    cJSON_AddStringToObject(msg, "race_id", scenario_.race_id.c_str());
    cJSON_AddStringToObject(msg, "sender", std::string(proto::ACTOR_SERVER).c_str());
    cJSON_AddRawToObject(msg, "payload", payload_json.c_str());

    std::string serialized = serialize_and_free(msg);
    for (const auto &entry : sessions_) {
        if (entry.second.registered) {
            net_.send_line(entry.first, serialized);
        }
    }
}

void ServerLogic::on_line(int client_fd, const std::string &line) {
    if (phase_ == SessionPhase::FINISHED || phase_ == SessionPhase::ABORTED) {
        return;
    }

    auto &session = sessions_[client_fd];
    session.fd = client_fd;

    const char *parse_end = nullptr;
    cJSON *root = cJSON_ParseWithOpts(line.c_str(), &parse_end, 1);
    if (root == nullptr || parse_end != line.c_str() + line.size() || !cJSON_IsObject(root)) {
        if (root != nullptr) {
            cJSON_Delete(root);
        }
        std::string actor = session.actor_id.empty() ? "-" : session.actor_id;
        std::string code(proto::ERR_INVALID_JSON);
        logger_.log(-1, actor, "ERROR", "-", code, "-");
        send_error(client_fd, "", code, "Malformed JSON message");
        bool was_registered = session.registered;
        net_.close_client(client_fd);
        sessions_.erase(client_fd);
        if (was_registered &&
            (phase_ == SessionPhase::WAIT_ROBOTS || phase_ == SessionPhase::RUNNING)) {
            phase_ = SessionPhase::ABORTED;
            logger_.log(-1, std::string(proto::ACTOR_SERVER), "SESSION_END", "-", "ABORTED", "-");
            net_.stop();
        }
        return;
    }

    std::string req_id;
    bool has_req_id = extract_nonempty_string(root, "request_id", req_id);

    std::string sender;
    bool has_sender = extract_nonempty_string(root, "sender", sender);
    std::string err_actor =
        has_sender ? sender : (session.actor_id.empty() ? "-" : session.actor_id);

    std::string type;
    std::string race_id;
    const cJSON *payload = cJSON_GetObjectItemCaseSensitive(root, "payload");

    if (!extract_nonempty_string(root, "type", type) ||
        !extract_nonempty_string(root, "race_id", race_id) || !has_sender || !has_req_id ||
        !cJSON_IsObject(payload)) {
        cJSON_Delete(root);
        std::string code(proto::ERR_INVALID_MESSAGE);
        logger_.log(-1, err_actor, "ERROR", "-", code, req_id);
        send_error(client_fd, req_id, code, "Missing or invalid message envelope fields");
        return;
    }

    if (!used_req_ids_.insert(req_id).second) {
        cJSON_Delete(root);
        std::string code(proto::ERR_DUPLICATE_ID);
        logger_.log(-1, err_actor, "ERROR", "-", code, req_id);
        send_error(client_fd, req_id, code, "Duplicate request_id: " + req_id);
        bool was_registered = session.registered;
        net_.close_client(client_fd);
        sessions_.erase(client_fd);
        if (was_registered &&
            (phase_ == SessionPhase::WAIT_ROBOTS || phase_ == SessionPhase::RUNNING)) {
            phase_ = SessionPhase::ABORTED;
            logger_.log(-1, std::string(proto::ACTOR_SERVER), "SESSION_END", "-", "ABORTED", "-");
            net_.stop();
        }
        return;
    }

    if (session.registered && sender != session.actor_id) {
        cJSON_Delete(root);
        std::string code(proto::ERR_INVALID_MESSAGE);
        logger_.log(-1, session.actor_id, "ERROR", "-", code, req_id);
        send_error(client_fd, req_id, code, "Sender does not match registered connection identity");
        return;
    }

    if (phase_ != SessionPhase::WAIT_SCENARIO && race_id != scenario_.race_id) {
        cJSON_Delete(root);
        std::string code(proto::ERR_INVALID_MESSAGE);
        logger_.log(-1, err_actor, "ERROR", "-", code, req_id);
        send_error(client_fd, req_id, code, "race_id does not match active scenario");
        return;
    }

    if (type == proto::MSG_SCENARIO_UPLOAD) {
        const cJSON *scenario_obj = cJSON_GetObjectItemCaseSensitive(payload, "scenario");
        if (!cJSON_IsObject(scenario_obj)) {
            cJSON_Delete(root);
            std::string code(proto::ERR_INVALID_SCENARIO);
            logger_.log(-1, err_actor, "ERROR", "-", code, req_id);
            send_error(
                client_fd, req_id, code, "SCENARIO_UPLOAD payload must contain scenario object"
            );
            return;
        }
        std::string scenario_race_id;
        if (extract_nonempty_string(scenario_obj, "race_id", scenario_race_id) &&
            scenario_race_id != race_id) {
            cJSON_Delete(root);
            std::string code(proto::ERR_INVALID_SCENARIO);
            logger_.log(-1, err_actor, "ERROR", "-", code, req_id);
            send_error(client_fd, req_id, code, "Envelope race_id does not match scenario.race_id");
            return;
        }
        char *scenario_raw = cJSON_PrintUnformatted(scenario_obj);
        cJSON_Delete(root);
        if (scenario_raw == nullptr) {
            std::string code(proto::ERR_INVALID_SCENARIO);
            logger_.log(-1, err_actor, "ERROR", "-", code, req_id);
            send_error(client_fd, req_id, code, "Failed to serialize scenario object");
            return;
        }
        std::string scenario_str(scenario_raw);
        std::free(scenario_raw);
        handle_scenario_upload(client_fd, sender, req_id, scenario_str);
        return;
    }

    if (type == proto::MSG_REGISTER_ROBOT) {
        std::string robot_id;
        if (!extract_nonempty_string(payload, "robot_id", robot_id) || robot_id != sender) {
            cJSON_Delete(root);
            std::string code(proto::ERR_INVALID_MESSAGE);
            logger_.log(-1, err_actor, "ERROR", "-", code, req_id);
            send_error(
                client_fd, req_id, code, "REGISTER_ROBOT payload.robot_id must match sender"
            );
            return;
        }
        cJSON_Delete(root);
        handle_register_robot(client_fd, sender, req_id);
        return;
    }

    if (type == proto::MSG_READY) {
        cJSON_Delete(root);
        handle_ready(client_fd, sender, req_id);
        return;
    }

    if (type == proto::MSG_ACTION_REQUEST) {
        std::string action;
        std::string resource;
        if (!extract_nonempty_string(payload, "action", action)) {
            cJSON_Delete(root);
            std::string code(proto::ERR_INVALID_MESSAGE);
            logger_.log(-1, err_actor, "ERROR", "-", code, req_id);
            send_error(client_fd, req_id, code, "ACTION_REQUEST missing action field");
            return;
        }
        extract_nonempty_string(payload, "resource", resource);
        cJSON_Delete(root);
        handle_action_request(client_fd, sender, req_id, action, resource);
        return;
    }

    if (type == proto::MSG_CLIENT_DONE) {
        cJSON_Delete(root);
        handle_client_done(client_fd, sender, req_id);
        return;
    }

    cJSON_Delete(root);
    std::string code(proto::ERR_INVALID_MESSAGE);
    logger_.log(-1, err_actor, "ERROR", "-", code, req_id);
    send_error(client_fd, req_id, code, "Unknown or unsupported message type: " + type);
}

void ServerLogic::handle_scenario_upload(
    int fd, const std::string &sender, const std::string &req_id, const std::string &payload_str
) {
    if (phase_ != SessionPhase::WAIT_SCENARIO) {
        std::string code(proto::ERR_INVALID_STATE);
        logger_.log(-1, sender, "ERROR", "-", code, req_id);
        send_error(fd, req_id, code, "Scenario already uploaded");
        return;
    }
    if (sender != proto::ACTOR_CONTROLLER) {
        std::string code(proto::ERR_INVALID_MESSAGE);
        logger_.log(-1, sender, "ERROR", "-", code, req_id);
        send_error(fd, req_id, code, "Only CONTROLLER may upload scenario");
        return;
    }

    Scenario parsed;
    std::string parse_err;
    if (!parse_scenario_json(payload_str, parsed, &parse_err)) {
        std::string code(proto::ERR_INVALID_SCENARIO);
        logger_.log(-1, sender, "ERROR", "-", code, req_id);
        send_error(fd, req_id, code, parse_err);
        return;
    }

    scenario_ = std::move(parsed);
    auto &session = sessions_[fd];
    session.actor_id = std::string(proto::ACTOR_CONTROLLER);
    session.is_controller = true;
    session.registered = true;
    phase_ = SessionPhase::WAIT_ROBOTS;

    logger_.log(-1, session.actor_id, "UPLOAD", "-", "OK", req_id);
    send_response(fd, std::string(proto::MSG_SCENARIO_ACCEPTED), req_id, "{}");
    logger_.log(
        -1, std::string(proto::ACTOR_SERVER), "RESPOND", "-",
        std::string(proto::MSG_SCENARIO_ACCEPTED), req_id
    );
}

void ServerLogic::handle_register_robot(
    int fd, const std::string &sender, const std::string &req_id
) {
    if (phase_ != SessionPhase::WAIT_ROBOTS) {
        std::string code(proto::ERR_INVALID_STATE);
        logger_.log(-1, sender, "ERROR", "-", code, req_id);
        send_error(fd, req_id, code, "REGISTER_ROBOT is only allowed in WAIT_ROBOTS phase");
        return;
    }

    auto &session = sessions_[fd];
    if (session.registered) {
        std::string code(proto::ERR_DUPLICATE_ID);
        logger_.log(-1, sender, "ERROR", "-", code, req_id);
        send_error(fd, req_id, code, "Connection already registered");
        net_.close_client(fd);
        sessions_.erase(fd);
        return;
    }

    if (!scenario_.has_robot(sender)) {
        std::string code(proto::ERR_INVALID_MESSAGE);
        logger_.log(-1, sender, "ERROR", "-", code, req_id);
        send_error(fd, req_id, code, "Unknown robot_id: " + sender);
        return;
    }

    if (!registered_robots_.insert(sender).second) {
        std::string code(proto::ERR_DUPLICATE_ID);
        logger_.log(-1, sender, "ERROR", "-", code, req_id);
        send_error(fd, req_id, code, "Duplicate robot_id: " + sender);
        net_.close_client(fd);
        sessions_.erase(fd);
        return;
    }

    session.actor_id = sender;
    session.is_controller = false;
    session.registered = true;

    logger_.log(-1, sender, "REGISTER", "-", "OK", req_id);
    send_response(fd, std::string(proto::MSG_REGISTERED), req_id, "{}");
    logger_.log(
        -1, std::string(proto::ACTOR_SERVER), "RESPOND", "-", std::string(proto::MSG_REGISTERED),
        req_id
    );
}

void ServerLogic::handle_ready(int fd, const std::string &sender, const std::string &req_id) {
    auto &session = sessions_[fd];
    if (phase_ != SessionPhase::WAIT_ROBOTS || !session.registered || session.is_controller ||
        session.ready) {
        std::string code(proto::ERR_INVALID_STATE);
        logger_.log(-1, sender, "ERROR", "-", code, req_id);
        send_error(fd, req_id, code, "READY is not valid in current state");
        return;
    }

    session.ready = true;
    logger_.log(-1, session.actor_id, "READY", "-", "OK", req_id);
    send_response(fd, std::string(proto::MSG_READY_ACCEPTED), req_id, "{}");
    logger_.log(
        -1, std::string(proto::ACTOR_SERVER), "RESPOND", "-",
        std::string(proto::MSG_READY_ACCEPTED), req_id
    );

    std::size_t ready_count = 0;
    for (const auto &entry : sessions_) {
        if (entry.second.registered && !entry.second.is_controller && entry.second.ready) {
            ++ready_count;
        }
    }

    if (ready_count == scenario_.robots.size()) {
        phase_ = SessionPhase::RUNNING;
        logger_.log(-1, std::string(proto::ACTOR_SERVER), "START", "-", "OK", "-");
        broadcast(std::string(proto::MSG_START), "{}");
    }
}

void ServerLogic::handle_action_request(
    int fd,
    const std::string &sender,
    const std::string &req_id,
    const std::string &action,
    const std::string &resource
) {
    auto &session = sessions_[fd];
    if (phase_ != SessionPhase::RUNNING || !session.registered || session.is_controller ||
        session.done) {
        std::string code(proto::ERR_INVALID_STATE);
        logger_.log(-1, sender, "ERROR", "-", code, req_id);
        send_error(fd, req_id, code, "ACTION_REQUEST is only allowed while RUNNING");
        return;
    }

    std::string log_resource;
    if (action == "MOVE") {
        if (!resource.empty()) {
            std::string code(proto::ERR_INVALID_MESSAGE);
            logger_.log(-1, sender, "ERROR", "-", code, req_id);
            send_error(fd, req_id, code, "MOVE action must not specify a resource");
            return;
        }
        log_resource = "TRACK";
    } else if (action == "OPEN") {
        if (resource.empty() || !chest_exists(scenario_, resource)) {
            std::string code(proto::ERR_INVALID_MESSAGE);
            logger_.log(-1, sender, "ERROR", "-", code, req_id);
            send_error(fd, req_id, code, "OPEN action references unknown chest: " + resource);
            return;
        }
        log_resource = resource;
    } else {
        std::string code(proto::ERR_INVALID_MESSAGE);
        logger_.log(-1, sender, "ERROR", "-", code, req_id);
        send_error(fd, req_id, code, "Unsupported action: " + action);
        return;
    }

    session.pending_req_id = req_id;
    session.pending_resource = log_resource;

    logger_.log(-1, session.actor_id, "RECEIVE", log_resource, action, req_id);
    send_response(
        fd, std::string(proto::MSG_STATE_UPDATE), req_id,
        "{\"status\":\"" + std::string(proto::STATUS_RECORDED) + "\"}"
    );
    logger_.log(
        -1, std::string(proto::ACTOR_SERVER), "RESPOND", log_resource,
        std::string(proto::STATUS_RECORDED), req_id
    );

    session.pending_req_id.clear();
    session.pending_resource.clear();
}

void ServerLogic::handle_client_done(int fd, const std::string &sender, const std::string &req_id) {
    auto &session = sessions_[fd];
    if (phase_ != SessionPhase::RUNNING || !session.registered || session.is_controller ||
        session.done || !session.pending_req_id.empty()) {
        std::string code(proto::ERR_INVALID_STATE);
        logger_.log(-1, sender, "ERROR", "-", code, req_id);
        send_error(fd, req_id, code, "CLIENT_DONE is not valid in current state");
        return;
    }

    session.done = true;
    logger_.log(-1, session.actor_id, "DONE", "-", "OK", req_id);
    send_response(fd, std::string(proto::MSG_DONE_ACCEPTED), req_id, "{}");
    logger_.log(
        -1, std::string(proto::ACTOR_SERVER), "RESPOND", "-", std::string(proto::MSG_DONE_ACCEPTED),
        req_id
    );

    std::size_t done_count = 0;
    for (const auto &entry : sessions_) {
        if (entry.second.registered && !entry.second.is_controller && entry.second.done) {
            ++done_count;
        }
    }

    if (done_count == scenario_.robots.size()) {
        phase_ = SessionPhase::FINISHED;
        broadcast(std::string(proto::MSG_SESSION_FINISHED), "{}");
        logger_.log(-1, std::string(proto::ACTOR_SERVER), "SESSION_END", "-", "OK", "-");
        net_.stop();
    }
}

void ServerLogic::on_disconnect(int client_fd) {
    auto it = sessions_.find(client_fd);
    if (it == sessions_.end()) {
        return;
    }

    ClientSession session = std::move(it->second);
    sessions_.erase(it);

    if (phase_ == SessionPhase::FINISHED || phase_ == SessionPhase::ABORTED) {
        return;
    }

    if (!session.registered) {
        logger_.log(-1, "-", "DISCONNECT", "-", "EOF", "-");
        return;
    }

    logger_.log(-1, session.actor_id, "DISCONNECT", "-", "EOF", "-");
    if (!session.pending_req_id.empty()) {
        std::string res = session.pending_resource.empty() ? "-" : session.pending_resource;
        logger_.log(-1, session.actor_id, "CANCEL", res, "DISCONNECTED", session.pending_req_id);
    }

    phase_ = SessionPhase::ABORTED;
    logger_.log(-1, std::string(proto::ACTOR_SERVER), "SESSION_END", "-", "ABORTED", "-");
    net_.stop();
}
