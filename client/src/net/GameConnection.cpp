#include "net/GameConnection.h"

#include <emscripten/websocket.h>

struct ConnectionCallbacks {
    static EM_BOOL open(int, const EmscriptenWebSocketOpenEvent*, void* userData) {
        GameConnection* conn = (GameConnection*)userData;
        conn->connected = true;
        if (conn->onOpen) conn->onOpen();
        return EM_TRUE;
    }
    static EM_BOOL message(int, const EmscriptenWebSocketMessageEvent* e, void* userData) {
        GameConnection* conn = (GameConnection*)userData;
        if (e->isText) return EM_TRUE;
        if (conn->onData) conn->onData(e->data, e->numBytes);
        return EM_TRUE;
    }
    static EM_BOOL close(int, const EmscriptenWebSocketCloseEvent*, void* userData) {
        GameConnection* conn = (GameConnection*)userData;
        conn->connected = false;
        if (conn->onClose) conn->onClose();
        return EM_TRUE;
    }
};

void GameConnection::connect(const std::string& url) {
    EmscriptenWebSocketCreateAttributes attrs = { url.c_str(), nullptr, EM_TRUE };
    socket = emscripten_websocket_new(&attrs);
    if (socket <= 0) return;
    emscripten_websocket_set_onopen_callback(socket, this, ConnectionCallbacks::open);
    emscripten_websocket_set_onmessage_callback(socket, this, ConnectionCallbacks::message);
    emscripten_websocket_set_onclose_callback(socket, this, ConnectionCallbacks::close);
}

void GameConnection::close() {
    if (socket > 0) {
        emscripten_websocket_set_onopen_callback(socket, nullptr, nullptr);
        emscripten_websocket_set_onmessage_callback(socket, nullptr, nullptr);
        emscripten_websocket_set_onclose_callback(socket, nullptr, nullptr);
        emscripten_websocket_close(socket, 1000, "");
        emscripten_websocket_delete(socket);
        socket = 0;
    }
    connected = false;
}

void GameConnection::send(const Writer& w) {
    if (!connected) return;
    emscripten_websocket_send_binary(socket, (void*)w.data(), (uint32_t)w.size());
}
