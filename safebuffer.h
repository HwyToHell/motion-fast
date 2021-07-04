#ifndef SAFEBUFFER_H
#define SAFEBUFFER_H

extern "C" {
#include <libavformat/avformat.h>
}

#include <condition_variable>
#include <mutex>
#include <queue>


bool isKeyFrame(AVPacket* packet)
{
    return (packet->flags & AV_PKT_FLAG_KEY) ? true : false;
}


class PacketSafeCircularBuffer
{
public:
    PacketSafeCircularBuffer(size_t bufSize) :
        m_capacity(bufSize),
        m_newPacket(false)
    {}

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
        {
            std::lock_guard<std::mutex> lock(m_mtx);
            m_queue.push(std::move(packet));
            if (m_queue.size() > m_capacity) {
                av_packet_free(&m_queue.front());
                m_queue.pop();
            }
            m_newPacket = true;
        }
        m_newPacketCnd.notify_one();
    }

    size_t size()
    {
        std::lock_guard<std::mutex> lock(m_mtx);
        return m_queue.size();
    }

    void waitForNewPacket()
    {
        std::unique_lock<std::mutex> lock(m_mtx);
        m_newPacketCnd.wait(lock, [this]{return m_newPacket;});
        m_newPacket = false;
    }

private:
    size_t                  m_capacity;
    mutable std::mutex      m_mtx;
    bool                    m_newPacket;
    std::condition_variable m_newPacketCnd;
    std::queue<AVPacket*>   m_queue;
};


class PacketSafeQueue
{
public:
    PacketSafeQueue() :
        m_newPacket(false),
        m_terminate(false)
    {}

    ~PacketSafeQueue()
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

    void push(AVPacket* packet)
    {
        {
            std::lock_guard<std::mutex> lock(m_mtx);
            m_queue.push(std::move(packet));
            m_newPacket = true;
        }
        m_newPacketCnd.notify_one();
    }

    size_t size()
    {
        std::lock_guard<std::mutex> lock(m_mtx);
        return m_queue.size();
    }

    void terminate()
    {
        std::lock_guard<std::mutex> lock(m_mtx);
        m_terminate = true;
        m_newPacketCnd.notify_one();
    }

    void waitForNewPacket()
    {
        std::unique_lock<std::mutex> lock(m_mtx);
        m_newPacketCnd.wait(lock, [this]{return (m_newPacket || m_terminate);});
        m_newPacket = false;
    }

private:
    mutable std::mutex      m_mtx;
    bool                    m_newPacket;
    std::condition_variable m_newPacketCnd;
    std::queue<AVPacket*>   m_queue;
    bool                    m_terminate;
};


#endif // SAFEBUFFER_H
