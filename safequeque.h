#ifndef SAFEQUEUE_H
#define SAFEQUEUE_H

#include <queue>
#include <mutex>

template<class T>
class SafeQueue
{
public:
    SafeQueue() {}
    ~SafeQueue() {}

    bool pop(T& item)
    {
        std::lock_guard<std::mutex> lock(m_mtx);
        if (m_queue.empty()) {
            return false;
        } else {
            item = std::move(m_queue.front());
            m_queue.pop();
            return  true;
        }
    }

    void push(T&& item)
    {
        std::lock_guard<std::mutex> lock(m_mtx);
        m_queue.push(std::move(item));
    }

    size_t size()
    {
        std::lock_guard<std::mutex> lock(m_mtx);
        return m_queue.size();
    }

private:
    mutable std::mutex  m_mtx;
    std::queue<T>       m_queue;
};


#endif // SAFEQUEUE_H
