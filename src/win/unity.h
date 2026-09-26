#pragma once

#include <map>
#include <optional>
#include <string>

struct GameState {
    int frame = 0;
    bool loadingLevel = false;
    std::string level;
    std::optional<int> joinState;
    std::optional<int> connectionState;
    std::optional<int> serverTime;
};

class UnityBridge {
public:
    std::optional<GameState> read();
    bool callGame(const std::string& method);
    bool waitingForSession();
    bool startSession(const std::string& json);

private:
    bool attach();
    void* property(void* image, const char* ns, const char* cls, const char* name, void* target);
    void* game();

    bool loaded = false;
    bool failed = false;
    void* domain = nullptr;
    void* gameImage = nullptr;
    void* engineImage = nullptr;
};

class LevelLoadWatcher {
public:
    explicit LevelLoadWatcher(std::map<int, std::string> rules) : rules(std::move(rules)) {}

    void tick(UnityBridge& unity);

private:
    std::map<int, std::string> rules;
    bool wasLoading = false;
    unsigned long finishedAt = 0;
    std::optional<int> stateAtFinish;
};
