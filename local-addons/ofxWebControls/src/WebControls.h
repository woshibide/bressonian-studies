#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// Network threads exchange text only. The app drains commands on its GL owner thread.
class WebControls {
public:
    struct Command { std::uint64_t client; std::string text; };
    static constexpr int port = 8190;
    WebControls();
    ~WebControls();
    WebControls(const WebControls &) = delete;
    WebControls &operator=(const WebControls &) = delete;
    void start(const std::string &documentRoot);
    void stop();
    std::vector<Command> drain();
    void publish(const std::string &snapshot);
    void reply(std::uint64_t client, const std::string &message);
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
