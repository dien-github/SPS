#ifndef SPS_MESSAGE_QUEUE_H
#define SPS_MESSAGE_QUEUE_H

#include <QQueue>
#include <QMutex>
#include <QWaitCondition>
#include <memory>

// Thread-safe message queue for inter-process communication
template<typename T>
class MessageQueue {
public:
    MessageQueue(int maxSize = 1000) : m_maxSize(maxSize) {}

    // Add message to queue (thread-safe)
    bool enqueue(const T& message) {
        QMutexLocker lock(&m_mutex);

        if (m_queue.size() >= m_maxSize) {
            return false;  // Queue full
        }

        m_queue.enqueue(message);
        m_condition.wakeOne();
        return true;
    }

    // Retrieve message from queue (blocks if empty, returns false on timeout)
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

    // Non-blocking dequeue
    bool tryDequeue(T& message) {
        QMutexLocker lock(&m_mutex);
        if (m_queue.isEmpty()) {
            return false;
        }
        message = m_queue.dequeue();
        return true;
    }

    // Get queue size
    int size() const {
        QMutexLocker lock(&m_mutex);
        return m_queue.size();
    }

    // Clear queue
    void clear() {
        QMutexLocker lock(&m_mutex);
        m_queue.clear();
    }

    // Check if empty
    bool isEmpty() const {
        QMutexLocker lock(&m_mutex);
        return m_queue.isEmpty();
    }

    // Check if full
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
