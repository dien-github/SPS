#ifndef SPS_MESSAGE_QUEUE_H
#define SPS_MESSAGE_QUEUE_H

#include <QQueue>
#include <QMutex>
#include <QWaitCondition>
#include <memory>

/** Thread-safe message queue (producer-consumer) for inter-process communication. */
template<typename T>
class MessageQueue {
public:
    /** Constructor: create a queue with the given maximum size. */
    MessageQueue(int maxSize = 1000) : m_maxSize(maxSize) {}

    /** Add a message to the queue (thread-safe). Returns false if the queue is full. */
    bool enqueue(const T& message) {
        QMutexLocker lock(&m_mutex);

        if (m_queue.size() >= m_maxSize) {
            return false;  // Queue full
        }

        m_queue.enqueue(message);
        m_condition.wakeOne();
        return true;
    }

    /** Retrieve a message from the queue (blocks if empty, returns false on timeout). */
    bool dequeue(T& message, unsigned long timeoutMs = ULONG_MAX) {
        QMutexLocker lock(&m_mutex);

        if (m_queue.isEmpty()) {
            if (!m_condition.wait(&m_mutex, timeoutMs)) {
                return false;  // Timeout
            }
        }

        if (m_queue.isEmpty()) {
            return false;  // Still empty
        }

        message = m_queue.dequeue();
        return true;
    }

    /** Non-blocking dequeue: returns false immediately if the queue is empty. */
    bool tryDequeue(T& message) {
        QMutexLocker lock(&m_mutex);
        if (m_queue.isEmpty()) {
            return false;
        }
        message = m_queue.dequeue();
        return true;
    }

    /** Returns the current number of items in the queue. */
    int size() const {
        QMutexLocker lock(&m_mutex);
        return m_queue.size();
    }

    /** Remove all items from the queue. */
    void clear() {
        QMutexLocker lock(&m_mutex);
        m_queue.clear();
    }

    /** Returns true if the queue contains no items. */
    bool isEmpty() const {
        QMutexLocker lock(&m_mutex);
        return m_queue.isEmpty();
    }

    /** Returns true if the queue has reached its maximum capacity. */
    bool isFull() const {
        QMutexLocker lock(&m_mutex);
        return m_queue.size() >= m_maxSize;
    }

private:
    mutable QMutex m_mutex;
    QWaitCondition m_condition;
    QQueue<T> m_queue;
    int m_maxSize;
};

#endif // SPS_MESSAGE_QUEUE_H
