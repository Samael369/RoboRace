#pragma once

#include "logger.hpp"
#include "scenario.hpp"
#include "server_net.hpp"
#include <string>
#include <unordered_map>
#include <unordered_set>

enum class SessionPhase { WAIT_SCENARIO, WAIT_ROBOTS, RUNNING, FINISHED, ABORTED };

struct ClientSession {
    int fd = -1;
    std::string actor_id; // "CONTROLLER" or robot id ("R1", ...)
    bool is_controller = false;
    bool registered = false;
    bool ready = false;
    bool done = false;
    std::string pending_req_id;   // Set while processing an active request
    std::string pending_resource; // Resource associated with pending_req_id
};

class ServerLogic {
  public:
    ServerLogic(ServerNet &net, Logger &logger);

    void on_line(int client_fd, const std::string &line);
    void on_disconnect(int client_fd);

  private:
    ServerNet &net_;
    Logger &logger_;
    SessionPhase phase_ = SessionPhase::WAIT_SCENARIO;
    Scenario scenario_;
    std::unordered_map<int, ClientSession> sessions_;
    std::unordered_set<std::string> registered_robots_;
    std::unordered_set<std::string> used_req_ids_;

    void send_response(
        int fd, const std::string &type, const std::string &req_id, const std::string &payload_json
    );
    void
    send_error(int fd, const std::string &req_id, const std::string &code, const std::string &msg);
    void broadcast(const std::string &type, const std::string &payload_json);

    void handle_scenario_upload(
        int fd, const std::string &sender, const std::string &req_id, const std::string &payload_str
    );
    void handle_register_robot(int fd, const std::string &sender, const std::string &req_id);
    void handle_ready(int fd, const std::string &sender, const std::string &req_id);
    void handle_action_request(
        int fd,
        const std::string &sender,
        const std::string &req_id,
        const std::string &action,
        const std::string &resource
    );
    void handle_client_done(int fd, const std::string &sender, const std::string &req_id);
};
