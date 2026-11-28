#include "protocol.hpp"
#include "scenario.hpp"

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {

using namespace roborace::protocol;

struct LogEntry {
    int line_no = 0;
    std::string tick;
    std::string actor;
    std::string event;
    std::string resource;
    std::string result;
    std::string req;
};

bool parse_kv(const std::string &token, const std::string &expected_key, std::string &out_val) {
    std::string prefix = expected_key + "=";
    if (token.rfind(prefix, 0) != 0) {
        return false;
    }
    out_val = token.substr(prefix.size());
    return !out_val.empty();
}

bool parse_events_log(const std::string &path, std::vector<LogEntry> &out, std::string &err) {
    std::ifstream file(path);
    if (!file.is_open()) {
        err = "failed to open log file: " + path;
        return false;
    }

    std::string line;
    int line_no = 0;
    while (std::getline(file, line)) {
        ++line_no;
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        std::istringstream iss(line);
        std::vector<std::string> tokens;
        std::string tok;
        while (iss >> tok) {
            tokens.push_back(tok);
        }

        if (tokens.size() != 6) {
            err = "line " + std::to_string(line_no) + ": expected exactly 6 key=value fields";
            return false;
        }

        LogEntry e;
        e.line_no = line_no;
        if (!parse_kv(tokens[0], "tick", e.tick) || !parse_kv(tokens[1], "actor", e.actor) ||
            !parse_kv(tokens[2], "event", e.event) ||
            !parse_kv(tokens[3], "resource", e.resource) ||
            !parse_kv(tokens[4], "result", e.result) || !parse_kv(tokens[5], "req", e.req)) {
            err = "line " + std::to_string(line_no) + ": invalid field names or order";
            return false;
        }

        if (e.tick != "-1") {
            err = "line " + std::to_string(line_no) + ": Phase 1 requires tick=-1";
            return false;
        }

        out.push_back(std::move(e));
    }

    if (out.empty()) {
        err = "events.log is empty";
        return false;
    }
    return true;
}

bool is_request_event(const std::string &ev) {
    return ev == "UPLOAD" || ev == "REGISTER" || ev == "READY" || ev == "RECEIVE" || ev == "DONE";
}

bool has_disconnect(const std::vector<LogEntry> &entries) {
    for (const auto &e : entries) {
        if (e.event == "DISCONNECT") {
            return true;
        }
    }
    return false;
}

bool check_unique_registration(
    const std::vector<LogEntry> &entries, const std::unordered_set<std::string> &expected_robots
) {
    std::unordered_map<std::string, int> reg_count;
    std::unordered_map<std::string, std::string> req_to_robot;
    std::unordered_map<std::string, int> resp_count;
    std::unordered_map<std::string, int> cancel_count;

    for (const auto &e : entries) {
        if (e.event == "REGISTER" && e.result == "OK") {
            if (expected_robots.find(e.actor) == expected_robots.end() || e.req == "-") {
                return false;
            }
            if (++reg_count[e.actor] > 1) {
                return false;
            }
            req_to_robot[e.req] = e.actor;
        } else if (e.event == "RESPOND" && e.actor == ACTOR_SERVER && e.result == "REGISTERED") {
            auto it = req_to_robot.find(e.req);
            if (it == req_to_robot.end()) {
                return false;
            }
            ++resp_count[it->second];
        } else if (e.event == "CANCEL" && e.result == "DISCONNECTED") {
            auto it = req_to_robot.find(e.req);
            if (it != req_to_robot.end()) {
                ++cancel_count[it->second];
            }
        }
    }

    bool disconnected = has_disconnect(entries);
    for (const auto &robot : expected_robots) {
        int regs = reg_count[robot];
        if (!disconnected) {
            if (regs != 1 || resp_count[robot] != 1) {
                return false;
            }
        } else if (regs == 1) {
            if (resp_count[robot] + cancel_count[robot] != 1) {
                return false;
            }
        }
    }
    return true;
}

bool check_ready_before_start(
    const std::vector<LogEntry> &entries, const std::unordered_set<std::string> &expected_robots
) {
    int start_idx = -1;
    int start_count = 0;

    std::unordered_map<std::string, int> ready_idx;
    std::unordered_map<std::string, std::string> ready_req;
    std::unordered_map<std::string, int> ready_resp_idx;

    for (int i = 0; i < static_cast<int>(entries.size()); ++i) {
        const auto &e = entries[static_cast<std::size_t>(i)];
        if (e.event == "START" && e.actor == ACTOR_SERVER && e.result == "OK" && e.req == "-") {
            ++start_count;
            start_idx = i;
        } else if (e.event == "READY" && e.result == "OK") {
            if (expected_robots.find(e.actor) == expected_robots.end() || e.req == "-") {
                return false;
            }
            if (ready_idx.count(e.actor)) {
                return false;
            }
            ready_idx[e.actor] = i;
            ready_req[e.req] = e.actor;
        } else if (
            e.event == "RESPOND" && e.actor == ACTOR_SERVER && e.result == "READY_ACCEPTED"
        ) {
            auto it = ready_req.find(e.req);
            if (it == ready_req.end()) {
                return false;
            }
            ready_resp_idx[it->second] = i;
        }
    }

    if (start_count > 1) {
        return false;
    }
    if (start_count == 0) {
        // Allowed only if session aborted before START
        return has_disconnect(entries);
    }

    for (const auto &robot : expected_robots) {
        if (!ready_idx.count(robot) || !ready_resp_idx.count(robot)) {
            return false;
        }
        if (!(ready_idx[robot] < ready_resp_idx[robot] && ready_resp_idx[robot] < start_idx)) {
            return false;
        }
    }
    return true;
}

bool check_one_response_per_request(const std::vector<LogEntry> &entries) {
    std::unordered_map<std::string, int> req_idx;
    std::unordered_map<std::string, int> resp_count;
    std::unordered_map<std::string, int> cancel_count;
    std::unordered_set<std::string> error_reqs;

    for (int i = 0; i < static_cast<int>(entries.size()); ++i) {
        const auto &e = entries[static_cast<std::size_t>(i)];
        if (is_request_event(e.event)) {
            if (e.req == "-" || req_idx.count(e.req)) {
                return false;
            }
            req_idx[e.req] = i;
        } else if (e.event == "ERROR") {
            if (e.req != "-") {
                error_reqs.insert(e.req);
            }
        } else if (e.event == "RESPOND" && e.actor == ACTOR_SERVER) {
            if (e.req == "-") {
                // Allowed only for ERROR responses where request_id could not be extracted
                if (e.result != "ERROR") {
                    return false;
                }
                continue;
            }
            if (req_idx.count(e.req)) {
                if (i <= req_idx[e.req]) {
                    return false;
                }
                ++resp_count[e.req];
            } else if (e.result == "ERROR" && error_reqs.count(e.req)) {
                // Response to a rejected/invalid request (not an accepted request)
                continue;
            } else {
                return false;
            }
        } else if (e.event == "CANCEL" && e.result == "DISCONNECTED") {
            if (!req_idx.count(e.req) || i <= req_idx[e.req]) {
                return false;
            }
            ++cancel_count[e.req];
        }
    }

    bool disconnected = has_disconnect(entries);
    for (const auto &kv : req_idx) {
        const std::string &req = kv.first;
        int r = resp_count[req];
        int c = cancel_count[req];
        if (!disconnected) {
            if (r != 1 || c != 0) {
                return false;
            }
        } else {
            if (r + c != 1) {
                return false;
            }
        }
    }
    return true;
}

bool check_no_action_before_start(const std::vector<LogEntry> &entries) {
    bool seen_start = false;
    for (const auto &e : entries) {
        if (e.event == "START" && e.actor == ACTOR_SERVER && e.result == "OK") {
            seen_start = true;
        } else if (e.event == "RECEIVE" && !seen_start) {
            return false;
        }
    }
    return true;
}

bool check_clean_session_end(
    const std::vector<LogEntry> &entries, const std::unordered_set<std::string> &expected_robots
) {
    int end_count = 0;
    for (const auto &e : entries) {
        if (e.event == "SESSION_END") {
            ++end_count;
        }
    }
    if (end_count != 1) {
        return false;
    }

    const auto &last = entries.back();
    if (last.event != "SESSION_END" || last.actor != ACTOR_SERVER || last.req != "-") {
        return false;
    }

    bool disconnected = has_disconnect(entries);
    if (last.result == "OK") {
        if (disconnected) {
            return false;
        }
        std::unordered_map<std::string, std::string> done_req;
        std::unordered_set<std::string> done_acked;
        for (const auto &e : entries) {
            if (e.event == "DONE" && e.result == "OK" && e.req != "-") {
                done_req[e.req] = e.actor;
            } else if (
                e.event == "RESPOND" && e.actor == ACTOR_SERVER && e.result == "DONE_ACCEPTED"
            ) {
                auto it = done_req.find(e.req);
                if (it != done_req.end()) {
                    done_acked.insert(it->second);
                }
            }
        }
        return done_acked.size() == expected_robots.size();
    }

    if (last.result == "ABORTED") {
        return true;
    }
    return false;
}

const char *pf(bool ok) {
    return ok ? "PASS" : "FAIL";
}

} // namespace

int main(int argc, char *argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <scenario.race> <events.log> [--phase=1]\n";
        return 1;
    }

    std::string scenario_path = argv[1];
    std::string log_path = argv[2];
    if (argc >= 4 && std::string(argv[3]) != "--phase=1") {
        std::cerr << "Unsupported phase argument: " << argv[3] << " (expected --phase=1)\n";
        return 1;
    }

    Scenario scenario;
    std::string err;
    if (!parse_scenario_file(scenario_path, scenario, &err)) {
        std::cerr << "[verifier] scenario error: " << err << '\n';
        return 1;
    }

    std::unordered_set<std::string> expected_robots;
    for (const auto &r : scenario.robots) {
        expected_robots.insert(r.id);
    }

    std::vector<LogEntry> entries;
    bool format_ok = parse_events_log(log_path, entries, err);
    if (!format_ok) {
        std::cerr << "[verifier] log format error: " << err << '\n';
    }

    bool c_unique_reg = format_ok && check_unique_registration(entries, expected_robots);
    bool c_ready_start = format_ok && check_ready_before_start(entries, expected_robots);
    bool c_one_resp = format_ok && check_one_response_per_request(entries);
    bool c_no_action = format_ok && check_no_action_before_start(entries);
    bool c_clean_end = format_ok && check_clean_session_end(entries, expected_robots);

    bool all_pass = c_unique_reg && c_ready_start && c_one_resp && c_no_action && c_clean_end;

    std::ostringstream report;
    report << "schema=1 race_id=" << scenario.race_id << " phase=1 status=" << pf(all_pass) << '\n'
           << "check=unique_registration result=" << pf(c_unique_reg) << '\n'
           << "check=ready_before_start result=" << pf(c_ready_start) << '\n'
           << "check=one_response_per_request result=" << pf(c_one_resp) << '\n'
           << "check=no_action_before_start result=" << pf(c_no_action) << '\n'
           << "check=clean_session_end result=" << pf(c_clean_end) << '\n';

    std::string report_str = report.str();
    std::cout << report_str;

    std::ofstream out_file("verify_report.txt", std::ios::out | std::ios::trunc);
    if (!out_file.is_open()) {
        std::cerr << "[verifier] failed to write verify_report.txt\n";
        return 1;
    }
    out_file << report_str;
    out_file.close();

    return all_pass ? 0 : 1;
}