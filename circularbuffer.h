#ifndef CIRCULARBUFFER_H
#define CIRCULARBUFFER_H

#include <vector>


template <class T> class CircularBuffer
{
public:
    CircularBuffer(size_t bufSize) :
        m_buffer(bufSize),
        m_capacity(bufSize),
        m_full(false),
        m_head(0),
        m_tail(0)
    {

    }

    // access element (head + idx)
    T& at(size_t idx)
    {
        idx = idx < size() ? idx : size() - 1;
        return m_buffer[(m_head + idx) % m_capacity];
    }

    /*
    // reverse access (tail - idx)
    T& atReverse(size_t idx)
    {
        // minus and modulo approach doesnt work with unsigned
        idx = idx < size() ? idx : size() - 1;
        return m_buffer[(m_tail - 1 - idx) % m_capacity];
    }
    */

    // access head
    T& front()
    {
        return m_buffer[m_head];
    }

    bool isEmpty()
    {
        return m_head == m_tail && !m_full;
    }

    bool isFull()
    {
        return m_full;
    }

    // pop from head
    void pop()
    {
        if (!isEmpty()) {
            incHead();
        }
        m_full = false;
    }


    // push to tail
    void push(T& item)
    {
        m_buffer[m_tail] = (item);
        incTail();

        if (m_full) {
            incHead();
        }

        m_full = m_tail == m_head ? true : false;
    }

    void reset()
    {
        m_head = m_tail = 0;
        m_full = false;
    }

    size_t size()
    {
        if (m_full) {
            return m_capacity;
        } else {
            if (m_tail >= m_head) {
                return m_tail - m_head;
            } else {
                return m_capacity - m_head + m_tail;
            }
        }
    }

private:
    std::vector<T>  m_buffer;
    size_t          m_capacity;
    bool            m_full;
    size_t          m_head;
    size_t          m_tail;

    void incHead()
    {
        if (++m_head == m_capacity)
            m_head = 0;
    }

    void incTail()
    {
        if (++m_tail == m_capacity)
            m_tail = 0;
    }
};

#endif // CIRCULARBUFFER_H
