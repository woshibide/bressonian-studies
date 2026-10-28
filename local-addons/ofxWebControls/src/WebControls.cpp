#include "WebControls.h"

#include <Poco/Net/HTTPServer.h>
#include <Poco/Net/HTTPServerRequest.h>
#include <Poco/Net/HTTPServerResponse.h>
#include <Poco/Net/HTTPRequestHandler.h>
#include <Poco/Net/HTTPRequestHandlerFactory.h>
#include <Poco/Net/ServerSocket.h>
#include <Poco/Net/WebSocket.h>
#include <Poco/ThreadPool.h>

#include <atomic>
#include <algorithm>
#include <cctype>
#include <deque>
#include <filesystem>
#include <map>
#include <mutex>

namespace {
struct Channel {
    std::mutex mutex;
    std::atomic<bool> running{false};
    std::uint64_t nextClient = 1;
    std::string snapshot;
    std::deque<WebControls::Command> commands;
    std::map<std::uint64_t, std::deque<std::string>> replies;
};

class Handler : public Poco::Net::HTTPRequestHandler {
public:
    Handler(std::shared_ptr<Channel> channel, std::string root)
        : channel(std::move(channel)), root(std::move(root)) {}

    void handleRequest(Poco::Net::HTTPServerRequest &request,
                       Poco::Net::HTTPServerResponse &response) override {
        using namespace Poco::Net;
        const std::string host = request.get("Host", "");
        const std::string suffix = ":" + std::to_string(WebControls::port);
        if (host != "127.0.0.1" + suffix && host != "localhost" + suffix) {
            response.setStatus(HTTPResponse::HTTP_FORBIDDEN);
            response.send() << "Use http://127.0.0.1" << suffix;
            return;
        }
        response.set("X-Content-Type-Options", "nosniff");
        response.set("Cache-Control", "no-store");
        if (request.getMethod() != "GET") {
            response.setStatus(HTTPResponse::HTTP_METHOD_NOT_ALLOWED);
            response.send();
            return;
        }
        if (request.getURI() == "/ws") {
            const std::string origin = request.get("Origin", "");
            if (!origin.empty() && origin != "http://127.0.0.1" + suffix &&
                origin != "http://localhost" + suffix &&
                origin != "http://127.0.0.1:5173" && origin != "http://localhost:5173") {
                response.setStatus(HTTPResponse::HTTP_FORBIDDEN);
                response.send();
                return;
            }
            websocket(request, response);
            return;
        }
        std::string path = request.getURI();
        path = path.substr(0, path.find('?'));
        std::string relative;
        if (path == "/" || path == "/index.html") relative = "index.html";
        else if (path.rfind("/assets/", 0) == 0 && path.size() > 8) {
            const auto name = path.substr(8);
            if (name != "." && name != ".." && name.find("..") == std::string::npos &&
                std::all_of(name.begin(), name.end(), [](unsigned char c) {
                    return std::isalnum(c) || c == '-' || c == '_' || c == '.';
                })) relative = "assets/" + name;
        }
        const auto file = std::filesystem::path(root) / relative;
        if (relative.empty() || !std::filesystem::is_regular_file(file)) {
            response.setStatus(HTTPResponse::HTTP_NOT_FOUND);
            response.setContentType("text/plain");
            response.send() << "Controls unavailable. Run make dev to build the Vue interface.";
            return;
        }
        const auto extension = file.extension().string();
        response.sendFile(file.string(), extension == ".js" ? "text/javascript" :
            extension == ".css" ? "text/css" : "text/html; charset=utf-8");
    }
private:
    void websocket(Poco::Net::HTTPServerRequest &request,
                   Poco::Net::HTTPServerResponse &response) {
        using namespace Poco::Net;
        std::uint64_t client = 0;
        {
            std::lock_guard<std::mutex> lock(channel->mutex);
            if (channel->replies.size() < 8) {
                client = channel->nextClient++;
                channel->replies[client];
            }
        }
        if (client == 0) {
            response.setStatus(HTTPResponse::HTTP_SERVICE_UNAVAILABLE);
            response.send() << "Too many controls connections (maximum 8).";
            return;
        }
        try {
            WebSocket socket(request, response);
            socket.setMaxPayloadSize(16384);
            socket.setReceiveTimeout(Poco::Timespan(1, 0));
            socket.setSendTimeout(Poco::Timespan(1, 0));
            std::string lastSnapshot, fragments;
            bool receivingText = false;
            while (channel->running) {
                std::string snapshot;
                std::deque<std::string> replies;
                {
                    std::lock_guard<std::mutex> lock(channel->mutex);
                    snapshot = channel->snapshot;
                    replies.swap(channel->replies.at(client));
                }
                for (const auto &reply : replies)
                    socket.sendFrame(reply.data(), static_cast<int>(reply.size()), WebSocket::FRAME_TEXT);
                if (!snapshot.empty() && snapshot != lastSnapshot) {
                    socket.sendFrame(snapshot.data(), static_cast<int>(snapshot.size()), WebSocket::FRAME_TEXT);
                    lastSnapshot = std::move(snapshot);
                }
                if (!socket.poll(Poco::Timespan(0, 50000), Socket::SELECT_READ)) continue;
                char buffer[16384];
                int flags = 0;
                const int count = socket.receiveFrame(buffer, sizeof(buffer), flags);
                const int opcode = flags & WebSocket::FRAME_OP_BITMASK;
                if (opcode == WebSocket::FRAME_OP_CLOSE || (count == 0 && flags == 0)) break;
                if (opcode == WebSocket::FRAME_OP_PING) {
                    socket.sendFrame(buffer, count, WebSocket::FRAME_FLAG_FIN | WebSocket::FRAME_OP_PONG);
                    continue;
                }
                if (opcode == WebSocket::FRAME_OP_PONG) continue;
                if ((opcode == WebSocket::FRAME_OP_TEXT && receivingText) ||
                    (opcode == WebSocket::FRAME_OP_CONT && !receivingText) ||
                    (opcode != WebSocket::FRAME_OP_TEXT && opcode != WebSocket::FRAME_OP_CONT)) {
                    socket.shutdown(WebSocket::WS_PROTOCOL_ERROR);
                    break;
                }
                receivingText = true;
                fragments.append(buffer, count);
                if (fragments.size() > 16384) {
                    socket.shutdown(WebSocket::WS_PAYLOAD_TOO_BIG);
                    break;
                }
                if (!(flags & WebSocket::FRAME_FLAG_FIN)) continue;
                bool full = false;
                {
                    std::lock_guard<std::mutex> lock(channel->mutex);
                    full = channel->commands.size() >= 128;
                    if (!full) channel->commands.push_back({client, std::move(fragments)});
                }
                if (full) {
                    socket.shutdown(WebSocket::WS_POLICY_VIOLATION, "Command queue full; reconnect");
                    break;
                }
                fragments.clear();
                receivingText = false;
            }
        } catch (const Poco::Exception &) {
            // Disconnects and bounded read/write timeouts end only this client session.
        }
        std::lock_guard<std::mutex> lock(channel->mutex);
        channel->replies.erase(client);
        channel->commands.erase(std::remove_if(channel->commands.begin(), channel->commands.end(),
            [client](const WebControls::Command &command) { return command.client == client; }),
            channel->commands.end());
    }
    std::shared_ptr<Channel> channel;
    std::string root;
};

class Factory : public Poco::Net::HTTPRequestHandlerFactory {
public:
    Factory(std::shared_ptr<Channel> channel, std::string root)
        : channel(std::move(channel)), root(std::move(root)) {}
    Poco::Net::HTTPRequestHandler *createRequestHandler(const Poco::Net::HTTPServerRequest &) override {
        return new Handler(channel, root);
    }
private:
    std::shared_ptr<Channel> channel;
    std::string root;
};
} // namespace

struct WebControls::Impl {
    std::shared_ptr<Channel> channel = std::make_shared<Channel>();
    Poco::ThreadPool pool{2, 12};
    std::unique_ptr<Poco::Net::HTTPServer> server;
};

WebControls::WebControls() : impl(std::make_unique<Impl>()) {}
WebControls::~WebControls() { stop(); }
void WebControls::start(const std::string &root) {
    Poco::Net::ServerSocket socket(Poco::Net::SocketAddress("127.0.0.1", port));
    auto *params = new Poco::Net::HTTPServerParams;
    params->setMaxThreads(12);
    params->setMaxQueued(16);
    params->setTimeout(Poco::Timespan(2, 0));
    impl->server = std::make_unique<Poco::Net::HTTPServer>(
        new Factory(impl->channel, root), impl->pool, socket, params);
    impl->channel->running = true;
    impl->server->start();
}
void WebControls::stop() {
    impl->channel->running = false;
    if (impl->server) {
        impl->server->stopAll(true);
        impl->pool.joinAll();
        impl->server.reset();
    }
}
std::vector<WebControls::Command> WebControls::drain() {
    std::lock_guard<std::mutex> lock(impl->channel->mutex);
    std::vector<Command> result;
    while (!impl->channel->commands.empty() && result.size() < 16) {
        result.push_back(std::move(impl->channel->commands.front()));
        impl->channel->commands.pop_front();
    }
    return result;
}
void WebControls::publish(const std::string &snapshot) {
    std::lock_guard<std::mutex> lock(impl->channel->mutex);
    impl->channel->snapshot = snapshot;
}
void WebControls::reply(std::uint64_t client, const std::string &message) {
    std::lock_guard<std::mutex> lock(impl->channel->mutex);
    const auto found = impl->channel->replies.find(client);
    if (found != impl->channel->replies.end() && found->second.size() < 128)
        found->second.push_back(message);
}
