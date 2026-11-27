#include "scenario.hpp"
#include "cJSON.h"
#include <fstream>
#include <sstream>
#include <unordered_set>

namespace {

void set_err(std::string *err, const std::string &msg) {
    if (err != nullptr) {
        *err = msg;
    }
}

bool get_int_field(const cJSON *obj, const char *key, int &out, std::string *err) {
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (!cJSON_IsNumber(item) || item->valuedouble != static_cast<double>(item->valueint)) {
        set_err(err, std::string("Missing or invalid integer field: ") + key);
        return false;
    }
    out = item->valueint;
    return true;
}

bool get_string_field(const cJSON *obj, const char *key, std::string &out, std::string *err) {
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (!cJSON_IsString(item) || item->valuestring == nullptr) {
        set_err(err, std::string("Missing or invalid string field: ") + key);
        return false;
    }
    out = item->valuestring;
    return true;
}

bool is_valid_id(const std::string &id) {
    if (id.empty()) {
        return false;
    }
    if (id == "SERVER" || id == "CONTROLLER" || id == "LAUNCHER") {
        return false;
    }
    for (unsigned char ch : id) {
        bool is_ascii_alnum =
            (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9');
        if (!is_ascii_alnum && ch != '_') {
            return false;
        }
    }
    return true;
}

bool parse_chests(
    const cJSON *root,
    Scenario &out,
    std::unordered_set<std::string> &used_ids,
    std::unordered_set<std::string> &chest_ids,
    std::string *err
) {
    const cJSON *chests = cJSON_GetObjectItemCaseSensitive(root, "chests");
    if (!cJSON_IsArray(chests)) {
        set_err(err, "Missing or invalid array field: chests");
        return false;
    }

    const cJSON *item = nullptr;
    cJSON_ArrayForEach(item, chests) {
        if (!cJSON_IsObject(item)) {
            set_err(err, "Each chest entry must be an object");
            return false;
        }
        ChestSpec chest;
        if (!get_string_field(item, "id", chest.id, err) ||
            !get_int_field(item, "position", chest.position, err) ||
            !get_string_field(item, "type", chest.type, err) ||
            !get_int_field(item, "boost_moves", chest.boost_moves, err)) {
            return false;
        }
        if (!is_valid_id(chest.id) || !used_ids.insert(chest.id).second) {
            set_err(err, "Invalid or duplicate chest id: " + chest.id);
            return false;
        }
        if (chest.position < 0 || chest.position > out.track_length) {
            set_err(err, "Chest position out of range: " + chest.id);
            return false;
        }
        if (chest.type != "TURBO") {
            set_err(err, "Unsupported chest type: " + chest.type);
            return false;
        }
        if (chest.boost_moves <= 0) {
            set_err(err, "Chest boost_moves must be positive: " + chest.id);
            return false;
        }
        chest_ids.insert(chest.id);
        out.chests.push_back(std::move(chest));
    }
    return true;
}

bool parse_bridges(
    const cJSON *root, Scenario &out, std::unordered_set<std::string> &used_ids, std::string *err
) {
    const cJSON *bridges = cJSON_GetObjectItemCaseSensitive(root, "bridges");
    if (!cJSON_IsArray(bridges)) {
        set_err(err, "Missing or invalid array field: bridges");
        return false;
    }

    const cJSON *item = nullptr;
    cJSON_ArrayForEach(item, bridges) {
        if (!cJSON_IsObject(item)) {
            set_err(err, "Each bridge entry must be an object");
            return false;
        }
        BridgeSpec bridge;
        if (!get_string_field(item, "id", bridge.id, err) ||
            !get_int_field(item, "start", bridge.start, err) ||
            !get_int_field(item, "end", bridge.end, err) ||
            !get_int_field(item, "capacity", bridge.capacity, err)) {
            return false;
        }
        if (!is_valid_id(bridge.id) || !used_ids.insert(bridge.id).second) {
            set_err(err, "Invalid or duplicate bridge id: " + bridge.id);
            return false;
        }
        if (bridge.start < 1 || bridge.end < bridge.start || bridge.end >= out.track_length) {
            set_err(err, "Bridge range out of bounds: " + bridge.id);
            return false;
        }
        if (bridge.capacity <= 0) {
            set_err(err, "Bridge capacity must be positive: " + bridge.id);
            return false;
        }
        for (const auto &existing : out.bridges) {
            if (bridge.start <= existing.end && existing.start <= bridge.end) {
                set_err(err, "Overlapping bridge intervals: " + bridge.id + " and " + existing.id);
                return false;
            }
        }
        out.bridges.push_back(std::move(bridge));
    }
    return true;
}

bool parse_robots(
    const cJSON *root,
    Scenario &out,
    std::unordered_set<std::string> &used_ids,
    const std::unordered_set<std::string> &chest_ids,
    std::string *err
) {
    const cJSON *robots = cJSON_GetObjectItemCaseSensitive(root, "robots");
    if (!cJSON_IsArray(robots)) {
        set_err(err, "Missing or invalid array field: robots");
        return false;
    }

    const cJSON *item = nullptr;
    cJSON_ArrayForEach(item, robots) {
        if (!cJSON_IsObject(item)) {
            set_err(err, "Each robot entry must be an object");
            return false;
        }
        RobotSpec robot;
        if (!get_string_field(item, "id", robot.id, err)) {
            return false;
        }
        if (!is_valid_id(robot.id) || !used_ids.insert(robot.id).second) {
            set_err(err, "Invalid or duplicate robot id: " + robot.id);
            return false;
        }

        const cJSON *actions = cJSON_GetObjectItemCaseSensitive(item, "actions");
        if (!cJSON_IsArray(actions)) {
            set_err(err, "Missing or invalid actions array for robot: " + robot.id);
            return false;
        }

        const cJSON *action = nullptr;
        cJSON_ArrayForEach(action, actions) {
            if (!cJSON_IsString(action) || action->valuestring == nullptr) {
                set_err(err, "Each action must be a string for robot: " + robot.id);
                return false;
            }
            std::string act = action->valuestring;
            if (act != "MOVE") {
                constexpr const char *kOpenPrefix = "OPEN:";
                if (act.rfind(kOpenPrefix, 0) != 0) {
                    set_err(err, "Invalid action '" + act + "' for robot: " + robot.id);
                    return false;
                }
                std::string target_chest = act.substr(5);
                if (chest_ids.find(target_chest) == chest_ids.end()) {
                    set_err(err, "OPEN references unknown chest '" + target_chest + "'");
                    return false;
                }
            }
            robot.actions.push_back(std::move(act));
        }

        out.robots.push_back(std::move(robot));
    }

    if (out.robots.size() < 2) {
        set_err(err, "Scenario must define at least 2 robots");
        return false;
    }
    return true;
}

} // namespace

const RobotSpec *Scenario::find_robot(const std::string &id) const {
    for (const auto &robot : robots) {
        if (robot.id == id) {
            return &robot;
        }
    }
    return nullptr;
}

bool Scenario::has_robot(const std::string &id) const {
    return Scenario::find_robot(id) != nullptr;
}

bool parse_scenario_json(const std::string &json_text, Scenario &out, std::string *err) {
    out = Scenario{};
    if (err != nullptr) {
        err->clear();
    }

    const char *parse_end = nullptr;
    cJSON *parsed_json = cJSON_ParseWithOpts(json_text.c_str(), &parse_end, 1);
    if (parsed_json == nullptr) {
        const char *err_ptr = cJSON_GetErrorPtr();
        set_err(
            err, err_ptr != nullptr ? std::string("JSON parse error near: ") + err_ptr
                                    : "JSON parse error"
        );
        return false;
    }
    if (parse_end != json_text.c_str() + json_text.size()) {
        cJSON_Delete(parsed_json);
        set_err(err, "JSON parse error: unexpected null byte in input");
        return false;
    }

    bool ok = false;
    if (cJSON_IsObject(parsed_json) &&
        get_int_field(parsed_json, "schema_version", out.schema_version, err) &&
        get_string_field(parsed_json, "race_id", out.race_id, err) &&
        get_int_field(parsed_json, "track_length", out.track_length, err) &&
        get_int_field(parsed_json, "tick_ms", out.tick_ms, err) &&
        get_int_field(parsed_json, "max_ticks", out.max_ticks, err) &&
        get_int_field(parsed_json, "seed", out.seed, err)) {

        if (out.schema_version != 1) {
            set_err(err, "schema_version must be 1");
        } else if (!is_valid_id(out.race_id)) {
            set_err(err, "Invalid race_id: " + out.race_id);
        } else if (out.track_length <= 0 || out.tick_ms <= 0 || out.max_ticks <= 0) {
            set_err(err, "track_length, tick_ms, and max_ticks must be positive");
        } else if (out.seed < 0) {
            set_err(err, "seed must be non-negative");
        } else {
            std::unordered_set<std::string> used_ids;
            std::unordered_set<std::string> chest_ids;
            ok = parse_chests(parsed_json, out, used_ids, chest_ids, err) &&
                 parse_bridges(parsed_json, out, used_ids, err) &&
                 parse_robots(parsed_json, out, used_ids, chest_ids, err);
        }
    } else if (err != nullptr && err->empty()) {
        set_err(err, "Scenario root must be a JSON object");
    }

    cJSON_Delete(parsed_json);
    if (!ok) {
        out = Scenario{};
    }
    return ok;
}

bool parse_scenario_file(const std::string &path, Scenario &out, std::string *err) {
    if (err != nullptr) {
        err->clear();
    }
    std::ifstream file(path);
    if (!file.is_open()) {
        set_err(err, "Failed to open scenario file: " + path);
        return false;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    if (file.bad()) {
        set_err(err, "Failed to read scenario file: " + path);
        return false;
    }
    return parse_scenario_json(buffer.str(), out, err);
}
