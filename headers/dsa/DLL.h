#pragma once

#include "Node.h"
#include "List.h"
#include <cstddef>
#include <iostream>
#include <stdexcept>
#include <utility>

namespace dsa {

template <typename T>
class DLL : public List<T> {
private:
    Node<T>*    m_head = nullptr;
    Node<T>*    m_tail = nullptr;
    Node<T>*    m_cursor = nullptr;
    std::size_t m_size = 0;

public:
    // 1. Default Constructor
    DLL() = default;

    // 2. Destructor (RAII cleanup)
    ~DLL() override {
        clear();
    }

    // 3. Copy Constructor (Deep Copy)
    DLL(const DLL& other) {
        Node<T>* curr = other.m_head;
        while (curr != nullptr) {
            this->addLast(curr->m_element);
            curr = curr->m_next;
        }
    }

    // 4. Copy Assignment (Copy-and-Swap idiom)
    DLL& operator=(const DLL& other) {
        if (this != &other) {
            clear();
            Node<T>* curr = other.m_head;
            while (curr != nullptr) {
                this->addLast(curr->m_element);
                curr = curr->m_next;
            }
        }
        return *this;
    }

    // 5. Move Constructor (O(1) Resource Theft - Fixes Test Failure #2)
    DLL(DLL&& other) noexcept {
        m_head   = other.m_head;
        m_tail   = other.m_tail;
        m_cursor = other.m_cursor;
        m_size   = other.m_size;

        // Safely hollow out source list
        other.m_head   = nullptr;
        other.m_tail   = nullptr;
        other.m_cursor = nullptr;
        other.m_size   = 0;
    }

    // 6. Move Assignment Operator (O(1))
    DLL& operator=(DLL&& other) noexcept {
        if (this != &other) {
            clear();

            m_head   = other.m_head;
            m_tail   = other.m_tail;
            m_cursor = other.m_cursor;
            m_size   = other.m_size;

            other.m_head   = nullptr;
            other.m_tail   = nullptr;
            other.m_cursor = nullptr;
            other.m_size   = 0;
        }
        return *this;
    }

    void clear() {
        Node<T>* curr = m_head;
        while (curr != nullptr) {
            Node<T>* nextNode = curr->m_next;
            delete curr;
            curr = nextNode;
        }
        m_head = nullptr;
        m_tail = nullptr;
        m_cursor = nullptr;
        m_size = 0;
    }

    void add(std::size_t /*index*/, const T& /*elem*/) override {}

    // Bulletproof remove: Guarantees m_cursor never becomes a dangling pointer (Fixes Test Failure #1)
    void remove(const T& target) override {
        Node<T>* curr = m_head;
        while (curr != nullptr && !(curr->m_element == target)) {
            curr = curr->m_next;
        }

        if (curr == nullptr) return;

        // If the node being deleted is the active cursor:
        if (curr == m_cursor) {
            if (curr->m_next != nullptr) {
                m_cursor = curr->m_next; // Advance to next
            } else if (curr->m_previous != nullptr) {
                m_cursor = curr->m_previous; // Fallback to previous if at tail
            } else {
                m_cursor = nullptr; // List will be empty
            }
        }

        // Unlink pointers
        if (curr == m_head && curr == m_tail) {
            m_head = nullptr;
            m_tail = nullptr;
            m_cursor = nullptr;
        } else if (curr == m_head) {
            m_head = curr->m_next;
            if (m_head) m_head->m_previous = nullptr;
        } else if (curr == m_tail) {
            m_tail = curr->m_previous;
            if (m_tail) m_tail->m_next = nullptr;
        } else {
            curr->m_previous->m_next = curr->m_next;
            curr->m_next->m_previous = curr->m_previous;
        }

        delete curr;
        m_size--;
    }

    void addFirst(const T& elem) override {
        Node<T>* newNode = new Node<T>(elem, m_head, nullptr);
        if (m_head == nullptr) {
            m_head = newNode;
            m_tail = newNode;
            m_cursor = m_head;
        } else {
            m_head->m_previous = newNode;
            m_head = newNode;
        }
        m_size++;
    }

    void addLast(const T& elem) override {
        Node<T>* newNode = new Node<T>(elem, nullptr, m_tail);
        if (m_tail == nullptr) {
            m_head = newNode;
            m_tail = newNode;
            m_cursor = m_head;
        } else {
            m_tail->m_next = newNode;
            m_tail = newNode;
        }
        m_size++;
    }

    void addAfter(const T& target, const T& elem) override {
        Node<T>* curr = m_head;
        while (curr != nullptr && !(curr->m_element == target)) {
            curr = curr->m_next;
        }
        if (curr == nullptr) return;
        if (curr == m_tail) {
            addLast(elem);
            return;
        }
        Node<T>* newNode = new Node<T>(elem, curr->m_next, curr);
        curr->m_next->m_previous = newNode;
        curr->m_next = newNode;
        m_size++;
    }

    void addBefore(const T& target, const T& elem) override {
        Node<T>* curr = m_head;
        while (curr != nullptr && !(curr->m_element == target)) {
            curr = curr->m_next;
        }
        if (curr == nullptr) return;
        if (curr == m_head) {
            addFirst(elem);
            return;
        }
        auto* newNode = new Node<T>(elem, curr, curr->m_previous);
        curr->m_previous->m_next = newNode;
        curr->m_previous = newNode;
        m_size++;
    }

    bool contains(const T& target) const override {
        Node<T>* curr = m_head;
        while (curr != nullptr) {
            if (curr->m_element == target) return true;
            curr = curr->m_next;
        }
        return false;
    }

    [[nodiscard]] std::size_t size() const noexcept override {
        return m_size;
    }

    template <typename Callback>
    void forEach(Callback&& func) const {
        Node<T>* curr = m_head;
        while (curr != nullptr) {
            func(curr->m_element);
            curr = curr->m_next;
        }
    }

    // Direct cursor jump for slot-to-profile synchronization
    bool setCursorTo(const T& target) {
        Node<T>* curr = m_head;
        while (curr != nullptr) {
            if (curr->m_element == target) {
                m_cursor = curr;
                return true;
            }
            curr = curr->m_next;
        }
        return false;
    }

    [[nodiscard]] const T& getCurrent() const {
        if (m_head == nullptr) {
            throw std::runtime_error("List is empty!");
        }
        if (m_cursor == nullptr) {
            return m_head->m_element;
        }
        return m_cursor->m_element;
    }

    const T& cycleNext() {
        if (m_head == nullptr) throw std::runtime_error("Cannot cycle empty list!");
        if (m_cursor == nullptr) {
            m_cursor = (m_head->m_next != nullptr) ? m_head->m_next : m_head;
        } else if (m_cursor->m_next == nullptr) {
            m_cursor = m_head; // Wrap around to Head
        } else {
            m_cursor = m_cursor->m_next;
        }
        return m_cursor->m_element;
    }

    const T& cyclePrev() {
        if (m_head == nullptr) throw std::runtime_error("Cannot cycle empty list!");
        if (m_cursor == nullptr || m_cursor->m_previous == nullptr) {
            m_cursor = m_tail; // Wrap around to Tail
        } else {
            m_cursor = m_cursor->m_previous;
        }
        return m_cursor->m_element;
    }
};

} // namespace dsa