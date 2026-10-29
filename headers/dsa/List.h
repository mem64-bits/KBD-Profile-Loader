#pragma once
#include <cstddef> // for std::size_t

namespace dsa {
    template <typename T>
    class List {
    public:
        virtual ~List() = default;

        virtual void add(std::size_t index, const T& elem) = 0;
        virtual void remove(const T& target) = 0;

        virtual void addFirst(const T& elem) = 0;
        virtual void addLast(const T& elem) = 0;
        virtual void addAfter(const T& target, const T& elem) = 0;
        virtual void addBefore(const T& target, const T& elem) = 0;

        virtual bool contains(const T& target) const = 0;
        [[nodiscard]] virtual std::size_t size() const = 0;
    };
};