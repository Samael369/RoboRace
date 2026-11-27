#pragma once

#include <string>
#include <vector>

struct RobotSpec {
    std::string id;
    std::vector<std::string> actions;
};

struct ChestSpec {
    std::string id;
    int position = 0;
    std::string type;
    int boost_moves = 0;
};

struct BridgeSpec {
    std::string id;
    int start = 0;
    int end = 0;
    int capacity = 1;
};

struct Scenario {
    int schema_version = 1;
    std::string race_id;
    int track_length = 0;
    int tick_ms = 0;
    int max_ticks = 0;
    int seed = 0;
    std::vector<RobotSpec> robots;
    std::vector<ChestSpec> chests;
    std::vector<BridgeSpec> bridges;

    const RobotSpec *find_robot(const std::string &id) const;
    bool has_robot(const std::string &id) const;
};

bool parse_scenario_json(const std::string &json_text, Scenario &out, std::string *err = nullptr);
bool parse_scenario_file(const std::string &path, Scenario &out, std::string *err = nullptr);
