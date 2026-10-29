#pragma once
#include <utility>

namespace dsa {
    template <typename T>
    struct Node {
        T m_element {};
        Node* m_next = nullptr;
        Node* m_previous = nullptr;

        Node() = default;

        explicit Node(T elem, Node* next = nullptr, Node* prev = nullptr)
            : m_element(std::move(elem)), m_next(next), m_previous(prev)
        {}
    };
}