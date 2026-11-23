#pragma once

#include <QStringList>

class RecentFiles
{
public:
    void add(const QString &fileName);
    const QStringList &files() const;

private:
    QStringList m_files;
};
