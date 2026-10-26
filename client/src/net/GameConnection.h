#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "coder/Writer.h"

class GameConnection {
public:
    std::function<void()> onOpen;
    std::function<void(const uint8_t*, size_t)> onData;
    std::function<void()> onClose;

    void connect(const std::string& url);
    void close();
    void send(const Writer& w);
    bool isConnected() const { return connected; }

private:
    int socket = 0;
    bool connected = false;

    friend struct ConnectionCallbacks;
};
