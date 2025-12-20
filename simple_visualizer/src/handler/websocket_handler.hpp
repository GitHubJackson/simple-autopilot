#pragma once

#include "CivetServer.h"
#include <string>

// 前置声明，避免循环引用
class VisualizerServer;

class RealtimeWebSocketHandler : public CivetWebSocketHandler {
public:
    explicit RealtimeWebSocketHandler(VisualizerServer& server);
    
    void handleReadyState(CivetServer *server, struct mg_connection *conn) override;
    bool handleData(CivetServer *server, struct mg_connection *conn, int bits, char *data, size_t data_len) override;
    void handleClose(CivetServer *server, const struct mg_connection *conn) override;

private:
    VisualizerServer& server_;
};
