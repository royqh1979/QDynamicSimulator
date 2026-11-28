#include <core/Identifier.h>
#include <core/formula/Function.h>

#include <QString>

namespace qds::core {
namespace {

bool trimCharacter(QChar c)
{
    return c == QChar(' ') || c == QChar('\t') || c == QChar('\r') || c == QChar('\n');
}

} // namespace

QString trimDisplayName(const QString &name)
{
    int first = 0;
    while (first < name.size() && trimCharacter(name.at(first)))
        ++first;
    int last = name.size() - 1;
    while (last >= first && trimCharacter(name.at(last)))
        --last;
    return first > last ? QString {} : name.mid(first, last - first + 1);
}

QString normalizeIdentifier(const QString &name)
{
    QString normalized;
    normalized.reserve(name.size());
    bool separator = false;
    for (int index = 0; index < name.size(); ++index) {
        const QChar c = name.at(index);
        if (c == QChar(0x00A0) || c == QChar('_') || trimCharacter(c)) {
            separator = true;
            continue;
        }
        if (separator && !normalized.isEmpty())
            normalized.push_back(QChar('_'));
        separator = false;
        if (c >= QChar('A') && c <= QChar('Z'))
            normalized.push_back(QChar(c.unicode() - QChar('A').unicode() + QChar('a').unicode()));
        else
            normalized.push_back(c);
    }
    return normalized;
}

bool isReservedIdentifier(const QString &nameKey)
{
    return findFunctionSignature(normalizeIdentifier(nameKey)) != nullptr;
}

} // namespace qds::core
