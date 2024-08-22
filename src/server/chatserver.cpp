#include "chatserver.hpp"
#include "json.hpp"
#include "chatservice.hpp"

#include <functional>

#include <iostream>

using namespace std::placeholders;
using json = nlohmann::json;

ChatServer::ChatServer(EventLoop *loop, const InetAddress &listenAddr, const std::string nameArg)
    : _loop(loop),
      _server(loop, listenAddr, nameArg)
{
    // 注册连接回调
    _server.setConnectionCallback(std::bind(&ChatServer::onConnection, this, _1));

    // 注册读写事件回调
    _server.setMessageCallback(std::bind(&ChatServer::onMessage, this, _1, _2, _3));

    // 设置线程数量
    _server.setThreadNum(4);
}

void ChatServer::start()
{
    _server.start();
}

// 上报连接相关信息的回调函数
void ChatServer::onConnection(const TcpConnectionPtr &conn)
{
    // 客户端断开连接
    if (!conn->connected())
    {
        //处理客户端异常断开
        ChatService::instance()->clientCloseException(conn);

        conn->shutdown();
    }
}

// 上报读写事件相关信息的回调函数
void ChatServer::onMessage(const TcpConnectionPtr &conn, Buffer *buffer, Timestamp time)
{
    std::string buf = buffer->retrieveAllAsString();
    // std::cout << buf << std::endl;

    // 数据的反序列化
    json js = json::parse(buf);

    auto msgHandler = ChatService::instance()->getHandler(js["msgid"].get<int>());
    // 回调消息绑定好的事件处理器，来执行相应的业务处理
    msgHandler(conn, js, time);
}