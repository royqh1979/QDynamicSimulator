#pragma once

#include <core/Document.h>

class QString;

namespace qds::io {

struct Workspace {
    bool hasViewport = false;
    double zoom = 1.0;
    core::Vec2 center;
    bool hasScrollValues = false;
    int scrollX = 0;
    int scrollY = 0;

    bool operator==(const Workspace &other) const
    {
        return hasViewport == other.hasViewport && zoom == other.zoom && center == other.center
            && hasScrollValues == other.hasScrollValues && scrollX == other.scrollX && scrollY == other.scrollY;
    }
    bool operator!=(const Workspace &other) const { return !(*this == other); }
};

class DocumentFile
{
public:
    static bool save(const QString &fileName, const core::Document &document, const Workspace &workspace,
        QString *errorMessage = nullptr);
    static bool open(const QString &fileName, core::Document &document, Workspace &workspace,
        QString *errorMessage = nullptr, bool *nativeFormat = nullptr);
};

} // namespace qds::io
