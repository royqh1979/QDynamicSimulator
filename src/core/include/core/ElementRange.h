#pragma once

#include <core/Element.h>

#include <iterator>
#include <memory>
#include <type_traits>
#include <unordered_map>

namespace qds::core {

class ModelElement;
class ViewElement;

template <typename T>
class ElementRange
{
    using Store = std::unordered_map<EntityId, std::unique_ptr<DocumentElement>>;
    using BaseIterator = Store::const_iterator;

    static bool matches(ElementKind kind)
    {
        if constexpr (std::is_same_v<T, DocumentElement>) {
            return true;
        } else if constexpr (std::is_same_v<T, ModelElement>) {
            return kind <= ElementKind::Variable;
        } else {
            return kind >= ElementKind::Connector;
        }
    }

public:
    class Iterator
    {
    public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = const T;
        using difference_type = std::ptrdiff_t;
        using pointer = const T *;
        using reference = const T &;

        Iterator(BaseIterator current, BaseIterator end)
            : m_current(current)
            , m_end(end)
        {
            seek();
        }

        reference operator*() const { return static_cast<const T &>(*m_current->second); }
        pointer operator->() const { return &static_cast<const T &>(*m_current->second); }
        Iterator &operator++()
        {
            ++m_current;
            seek();
            return *this;
        }
        bool operator==(const Iterator &other) const { return m_current == other.m_current; }
        bool operator!=(const Iterator &other) const { return !(*this == other); }

    private:
        void seek()
        {
            while (m_current != m_end && !matches(m_current->second->kind()))
                ++m_current;
        }

        BaseIterator m_current;
        BaseIterator m_end;
    };

    explicit ElementRange(const Store &elements)
        : m_elements(elements)
    {
    }

    Iterator begin() const { return Iterator(m_elements.begin(), m_elements.end()); }
    Iterator end() const { return Iterator(m_elements.end(), m_elements.end()); }
    bool empty() const { return begin() == end(); }

private:
    const Store &m_elements;
};

} // namespace qds::core
