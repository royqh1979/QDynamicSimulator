#include <qds/core/Document.h>

namespace qds::core {

std::string trimDisplayName(const std::string &name)
{
    const auto first = name.find_first_not_of(" \t\r\n");
    if (first == std::string::npos)
        return {};
    const auto last = name.find_last_not_of(" \t\r\n");
    return name.substr(first, last - first + 1);
}

std::string normalizeIdentifier(const std::string &name)
{
    std::string normalized;
    normalized.reserve(name.size());
    bool separator = false;
    for (std::size_t index = 0; index < name.size();) {
        const unsigned char c = static_cast<unsigned char>(name[index]);
        if (c == 0xC2 && index + 1 < name.size() && static_cast<unsigned char>(name[index + 1]) == 0xA0) {
            separator = true;
            index += 2;
            continue;
        }
        if (c == '_' || c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            separator = true;
            ++index;
            continue;
        }
        if (separator && !normalized.empty())
            normalized.push_back('_');
        separator = false;
        if (c >= 'A' && c <= 'Z')
            normalized.push_back(static_cast<char>(c - 'A' + 'a'));
        else
            normalized.push_back(static_cast<char>(c));
        ++index;
    }
    return normalized;
}

const DimensionElement *Dimension::findElement(const std::string &name) const
{
    const std::string key = normalizeIdentifier(name);
    for (const DimensionElement &element : elements) {
        if (!element.nameKey.empty() && element.nameKey == key)
            return &element;
    }
    return nullptr;
}

ModelRange Model::elements() const
{
    return ModelRange(m_document.m_elements);
}

const ModelElement *Model::findByName(const std::string &name) const
{
    return m_document.findByName(name);
}

} // namespace qds::core
