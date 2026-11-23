#include "RecentFiles.h"

#include <QFileInfo>

void RecentFiles::add(const QString &fileName)
{
    const QString path = QFileInfo(fileName).absoluteFilePath();
    m_files.removeAll(path);
    m_files.prepend(path);
    while (m_files.size() > 8)
        m_files.removeLast();
}

const QStringList &RecentFiles::files() const
{
    return m_files;
}
