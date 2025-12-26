#include "websocket_handler.hpp"
#include "../server/visualizer_server.hpp"
#include <iostream>
#include <cstring>

RealtimeWebSocketHandler::RealtimeWebSocketHandler(VisualizerServer& server) 
    : server_(server) {}

void RealtimeWebSocketHandler::handleReadyState(CivetServer *server, struct mg_connection *conn) {
    server_.AddConnection(conn);
    const char *welcome = "{\"type\": \"system\", \"msg\": \"Connected to SenseAuto Demo (Refactored)\"}";
    mg_websocket_write(conn, MG_WEBSOCKET_OPCODE_TEXT, welcome, strlen(welcome));
}

void RealtimeWebSocketHandler::handleClose(CivetServer *server, const struct mg_connection *conn) {
    server_.RemoveConnection(conn);
}

bool RealtimeWebSocketHandler::handleData(CivetServer *server, struct mg_connection *conn, int bits, char *data, size_t data_len) {
    std::string received(data, data_len);
    // std::cout << "[WS] Recv: " << received << std::endl;
    server_.HandleClientCommand(received);
    return true;
}
