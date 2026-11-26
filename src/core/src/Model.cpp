#include <core/Model.h>

#include <core/Document.h>

namespace qds::core {

ModelRange Model::elements() const
{
    return ModelRange(m_document.m_elements);
}

const ModelElement *Model::findByName(const std::string &name) const
{
    return m_document.findByName(name);
}

} // namespace qds::core
