#include "ConnectionImpl.h"
#include <addin/biterp/Component.hpp>
#include <mutex>
#include <condition_variable>

ConnectionImpl::ConnectionImpl(const AMQP::Address& address) : 
    trChannel(nullptr)
{
    static bool sslInited = false;
    if (!sslInited){
        SSL_library_init();
        sslInited = true;
    } 
    eventLoop = event_base_new();
    handler = new AMQP::LibEventHandler(eventLoop);
    connection = new AMQP::TcpConnection(handler, address);
    thread = std::thread(ConnectionImpl::loopThread, this);
}

ConnectionImpl::~ConnectionImpl() {
   // LOGD("closeChannel");
    closeChannel(trChannel);
    //LOGD("while");
    while (connection->usable()) {
        connection->close();
    }
    if (!connection->closed()) {
        connection->close(true);
    }
    ///LOGD("event_base_loopbreak");
    event_base_loopbreak(eventLoop);
    //LOGD("thread");
    thread.join();
   // LOGD("delete connection");
    delete connection;
    //LOGD("delete handler");
    delete handler;
    //LOGD("event_base_free");
    event_base_free(eventLoop);
}

void ConnectionImpl::loopThread(ConnectionImpl* thiz) {
    event_base* loop = thiz->eventLoop;
    while(!thiz->connection->closed()) {
        event_base_loop(loop, EVLOOP_NONBLOCK);
    }
}


void ConnectionImpl::openChannel(std::unique_ptr<AMQP::TcpChannel>& channel) {
    LOGD("1");
    if (channel) {
        LOGD("2");
        closeChannel(channel);
        LOGD("3");
    }
    LOGD("4");
    if (!connection->usable()) {
        throw Biterp::Error("Connection lost");
    }
    std::mutex m;
    std::condition_variable cv;
    bool ready = false;
    LOGD("5");
    channel.reset(new AMQP::TcpChannel(connection));
    LOGD("6");
    channel->onReady([&]() {
        LOGD("7");
        std::unique_lock<std::mutex> lock(m);
        ready = true;
        cv.notify_all();
        });
    channel->onError([this, &channel](const char* message) {
        LOGD("8");
        LOGW("Channel closed with reason: " + std::string(message));
        channel.reset(nullptr);
        });
        LOGD("9");
    std::unique_lock<std::mutex> lock(m);
    LOGD("10");
    cv.wait(lock, [&] {LOGD("12"); return ready; });
    LOGD("11");
    if (!channel) {
        throw Biterp::Error("Channel not opened");
    }
}

void ConnectionImpl::closeChannel(std::unique_ptr<AMQP::TcpChannel>& channel) {
    if (channel && channel->usable()) {
        channel->close();
    }
    channel.reset(nullptr);
}


void ConnectionImpl::connect() {    
    const uint16_t timeout = 5000;
    std::chrono::milliseconds timeoutMs{ timeout };
    auto end = std::chrono::system_clock::now() + timeoutMs;
    while (!connection->ready() &&  !connection->closed() && (end - std::chrono::system_clock::now()).count() > 0) {
        this_thread::sleep_for(chrono::milliseconds(100));
    }
    if (!connection->ready()) {
        throw Biterp::Error("Wrong login, password or vhost");
    }
}


AMQP::Channel* ConnectionImpl::channel() {
    if (!trChannel || !trChannel->usable()) {
        openChannel(trChannel);
    }
    return trChannel.get();
}


AMQP::Channel* ConnectionImpl::readChannel() {
    return channel();
}
