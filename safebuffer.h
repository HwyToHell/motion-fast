#ifndef SAFEBUFFER_H
#define SAFEBUFFER_H

extern "C" {
#include <libavformat/avformat.h>
}

#include <mutex>
#include <queue>


bool isKeyFrame(AVPacket* packet)
{
    return (packet->flags & AV_PKT_FLAG_KEY) ? true : false;
}


class PacketSafeCircularBuffer
{
public:
    PacketSafeCircularBuffer(size_t bufSize) : m_size(bufSize) {}

    ~PacketSafeCircularBuffer()
    {
        std::lock_guard<std::mutex> lock(m_mtx);
        while (m_queue.size() > 0) {
            av_packet_free(&m_queue.front());
            m_queue.pop();
        }
    }

    bool pop(AVPacket*& packet)
    {
        std::lock_guard<std::mutex> lock(m_mtx);
        if (m_queue.empty()) {
            return false;
        } else {
            packet = std::move(m_queue.front());
            m_queue.pop();
            return  true;
        }
    }

    bool popToNextKeyFrame(AVPacket*& packet)
    {
        std::lock_guard<std::mutex> lock(m_mtx);
        while (m_queue.size() > 0) {
            packet = std::move(m_queue.front());
            m_queue.pop();
            if (isKeyFrame(packet)) {
                return true;
            }
            av_packet_free(&m_queue.front());
        }
        return false;
    }

    void push(AVPacket *packet)
    {
        std::lock_guard<std::mutex> lock(m_mtx);
        m_queue.push(std::move(packet));
        if (m_queue.size() > m_size) {
            av_packet_free(&m_queue.front());
            m_queue.pop();
        }
    }

    size_t size()
    {
        std::lock_guard<std::mutex> lock(m_mtx);
        return m_queue.size();
    }

private:
    mutable std::mutex      m_mtx;
    std::queue<AVPacket*>   m_queue;
    size_t                  m_size;

};


class PacketSafeQueue
{
public:
    PacketSafeQueue() {}

    ~PacketSafeQueue()
    {
        std::lock_guard<std::mutex> lock(m_mtx);
        while (m_queue.size() > 0) {
            av_packet_free(&m_queue.front());
            m_queue.pop();
        }
    }

    bool pop(AVPacket* packet)
    {
        std::lock_guard<std::mutex> lock(m_mtx);
        if (m_queue.empty()) {
            return false;
        } else {
            packet = std::move(m_queue.front());
            m_queue.pop();
            return  true;
        }
    }

    void push(AVPacket* packet)
    {
        std::lock_guard<std::mutex> lock(m_mtx);
        m_queue.push(std::move(packet));
    }

    size_t size()
    {
        std::lock_guard<std::mutex> lock(m_mtx);
        return m_queue.size();
    }

private:
    mutable std::mutex      m_mtx;
    std::queue<AVPacket*>   m_queue;
};


#endif // SAFEBUFFER_H
