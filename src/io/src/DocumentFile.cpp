#include <io/DocumentFile.h>

#include <io/XmileFile.h>

#include <QCoreApplication>
#include <QFile>
#include <QSaveFile>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>
#include <QtGlobal>
#include <zlib.h>

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <QVector>

namespace qds::io {
namespace {

constexpr quint32 localHeaderSignature = 0x04034b50;
constexpr quint32 centralHeaderSignature = 0x02014b50;
constexpr quint32 endSignature = 0x06054b50;
constexpr int maximumEntrySize = 64 * 1024 * 1024;

using UidMap = std::unordered_map<quint64, core::EntityId>;

QString translate(const char *text)
{
    return QCoreApplication::translate("DocumentFile", text);
}

void setError(QString *errorMessage, const QString &message)
{
    if (errorMessage)
        *errorMessage = message;
}

quint16 read16(const QByteArray &data, int offset)
{
    return static_cast<quint16>(static_cast<quint8>(data.at(offset)))
        | (static_cast<quint16>(static_cast<quint8>(data.at(offset + 1))) << 8);
}

quint32 read32(const QByteArray &data, int offset)
{
    return static_cast<quint32>(read16(data, offset))
        | (static_cast<quint32>(read16(data, offset + 2)) << 16);
}

void append16(QByteArray &data, quint16 value)
{
    data.append(static_cast<char>(value & 0xff));
    data.append(static_cast<char>((value >> 8) & 0xff));
}

void append32(QByteArray &data, quint32 value)
{
    append16(data, static_cast<quint16>(value & 0xffff));
    append16(data, static_cast<quint16>(value >> 16));
}

quint32 checksum(const QByteArray &data)
{
    return static_cast<quint32>(::crc32(
        0,
        reinterpret_cast<const Bytef *>(data.constData()),
        static_cast<uInt>(data.size())));
}

bool compressRaw(const QByteArray &input, QByteArray &output, QString *errorMessage)
{
    z_stream stream = {};
    if (::deflateInit2(&stream, Z_BEST_COMPRESSION, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY) != Z_OK) {
        setError(errorMessage, translate("Could not initialize ZIP compression."));
        return false;
    }
    output.resize(static_cast<int>(::deflateBound(&stream, static_cast<uLong>(input.size()))));
    stream.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(input.constData()));
    stream.avail_in = static_cast<uInt>(input.size());
    stream.next_out = reinterpret_cast<Bytef *>(output.data());
    stream.avail_out = static_cast<uInt>(output.size());
    const int result = ::deflate(&stream, Z_FINISH);
    const uLong compressedSize = stream.total_out;
    ::deflateEnd(&stream);
    if (result != Z_STREAM_END) {
        setError(errorMessage, translate("Could not compress the QDS document."));
        return false;
    }
    output.resize(static_cast<int>(compressedSize));
    return true;
}

bool decompressRaw(const char *input, quint32 inputSize, quint32 outputSize, QByteArray &output, QString *errorMessage)
{
    if (inputSize > maximumEntrySize || outputSize > maximumEntrySize) {
        setError(errorMessage, translate("The ZIP entry is too large."));
        return false;
    }
    output.resize(static_cast<int>(std::max<quint32>(outputSize, 1)));
    z_stream stream = {};
    stream.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(input));
    stream.avail_in = static_cast<uInt>(inputSize);
    stream.next_out = reinterpret_cast<Bytef *>(output.data());
    stream.avail_out = static_cast<uInt>(output.size());
    if (::inflateInit2(&stream, -MAX_WBITS) != Z_OK) {
        setError(errorMessage, translate("Could not initialize ZIP decompression."));
        return false;
    }
    const int result = ::inflate(&stream, Z_FINISH);
    const uLong actualSize = stream.total_out;
    ::inflateEnd(&stream);
    if (result != Z_STREAM_END || actualSize != outputSize) {
        setError(errorMessage, translate("The compressed QDS document is damaged."));
        return false;
    }
    output.resize(static_cast<int>(actualSize));
    return true;
}

QByteArray makeZip(const QByteArray &xml)
{
    struct WrittenEntry {
        QByteArray name;
        QByteArray compressed;
        quint32 crc = 0;
        quint32 size = 0;
        quint32 offset = 0;
    };
    const QPair<QByteArray, QByteArray> sources[] = {{QByteArray("document.xml"), xml}};
    QByteArray output;
    QVector<WrittenEntry> entries;
    for (const auto &source : sources) {
        WrittenEntry entry;
        entry.name = source.first;
        entry.size = static_cast<quint32>(source.second.size());
        entry.crc = checksum(source.second);
        entry.offset = static_cast<quint32>(output.size());
        QString ignoredError;
        if (!compressRaw(source.second, entry.compressed, &ignoredError))
            return {};
        append32(output, localHeaderSignature);
        append16(output, 20);
        append16(output, 0x0800);
        append16(output, 8);
        append16(output, 0);
        append16(output, 0);
        append32(output, entry.crc);
        append32(output, static_cast<quint32>(entry.compressed.size()));
        append32(output, entry.size);
        append16(output, static_cast<quint16>(entry.name.size()));
        append16(output, 0);
        output.append(entry.name);
        output.append(entry.compressed);
        entries.push_back(std::move(entry));
    }
    const quint32 centralOffset = static_cast<quint32>(output.size());
    for (const WrittenEntry &entry : entries) {
        append32(output, centralHeaderSignature);
        append16(output, 20);
        append16(output, 20);
        append16(output, 0x0800);
        append16(output, 8);
        append16(output, 0);
        append16(output, 0);
        append32(output, entry.crc);
        append32(output, static_cast<quint32>(entry.compressed.size()));
        append32(output, entry.size);
        append16(output, static_cast<quint16>(entry.name.size()));
        append16(output, 0);
        append16(output, 0);
        append16(output, 0);
        append16(output, 0);
        append32(output, 0);
        append32(output, entry.offset);
        output.append(entry.name);
    }
    const quint32 centralSize = static_cast<quint32>(output.size()) - centralOffset;
    append32(output, endSignature);
    append16(output, 0);
    append16(output, 0);
    append16(output, static_cast<quint16>(entries.size()));
    append16(output, static_cast<quint16>(entries.size()));
    append32(output, centralSize);
    append32(output, centralOffset);
    append16(output, 0);
    return output;
}

struct ZipEntry {
    QByteArray name;
    quint16 flags = 0;
    quint16 method = 0;
    quint32 compressedSize = 0;
    quint32 size = 0;
    quint32 crc = 0;
    quint32 offset = 0;
};

bool extractEntry(const QByteArray &archive, const ZipEntry &entry, QByteArray &output, QString *errorMessage)
{
    if (archive.size() < 30 || (entry.flags & 1) != 0
        || entry.offset > static_cast<quint32>(archive.size() - 30)
        || read32(archive, static_cast<int>(entry.offset)) != localHeaderSignature) {
        setError(errorMessage, translate("The ZIP entry is invalid or encrypted."));
        return false;
    }
    const int headerOffset = static_cast<int>(entry.offset);
    const int dataOffset = headerOffset + 30 + read16(archive, headerOffset + 26) + read16(archive, headerOffset + 28);
    if (dataOffset < headerOffset || entry.compressedSize > static_cast<quint32>(archive.size() - dataOffset)) {
        setError(errorMessage, translate("The ZIP entry is truncated."));
        return false;
    }
    if (entry.method == 0) {
        if (entry.compressedSize != entry.size || entry.size > maximumEntrySize) {
            setError(errorMessage, translate("The stored ZIP entry is invalid."));
            return false;
        }
        output = archive.mid(dataOffset, static_cast<int>(entry.size));
    } else if (entry.method == 8) {
        if (!decompressRaw(archive.constData() + dataOffset, entry.compressedSize, entry.size, output, errorMessage))
            return false;
    } else {
        setError(errorMessage, translate("The ZIP compression method is not supported."));
        return false;
    }
    if (checksum(output) != entry.crc) {
        setError(errorMessage, translate("The QDS document failed its checksum verification."));
        return false;
    }
    return true;
}

bool readZip(const QByteArray &archive, QByteArray &xml, QString *errorMessage)
{
    if (archive.size() < 22) {
        setError(errorMessage, translate("The QDS container is too small."));
        return false;
    }
    const int firstCandidate = std::max(0, archive.size() - 22 - 65535);
    int endOffset = -1;
    for (int offset = archive.size() - 22; offset >= firstCandidate; --offset) {
        if (read32(archive, offset) == endSignature) {
            endOffset = offset;
            break;
        }
    }
    if (endOffset < 0) {
        setError(errorMessage, translate("The QDS container has no ZIP directory."));
        return false;
    }
    const quint16 entryCount = read16(archive, endOffset + 10);
    const quint32 centralOffset = read32(archive, endOffset + 16);
    int offset = static_cast<int>(centralOffset);
    ZipEntry selected;
    bool found = false;
    for (quint16 index = 0; index < entryCount; ++index) {
        if (offset < 0 || offset > archive.size() - 46 || read32(archive, offset) != centralHeaderSignature) {
            setError(errorMessage, translate("The ZIP directory is damaged."));
            return false;
        }
        ZipEntry entry;
        entry.flags = read16(archive, offset + 8);
        entry.method = read16(archive, offset + 10);
        entry.crc = read32(archive, offset + 16);
        entry.compressedSize = read32(archive, offset + 20);
        entry.size = read32(archive, offset + 24);
        const quint16 nameLength = read16(archive, offset + 28);
        const quint16 extraLength = read16(archive, offset + 30);
        const quint16 commentLength = read16(archive, offset + 32);
        entry.offset = read32(archive, offset + 42);
        if (offset > archive.size() - 46 - nameLength - extraLength - commentLength) {
            setError(errorMessage, translate("The ZIP directory is truncated."));
            return false;
        }
        entry.name = archive.mid(offset + 46, nameLength);
        const QString name = QString::fromUtf8(entry.name);
        if (name.compare(QStringLiteral("document.xml"), Qt::CaseInsensitive) == 0) {
            selected = entry;
            found = true;
        }
        offset += 46 + nameLength + extraLength + commentLength;
    }
    if (!found) {
        setError(errorMessage, translate("The QDS container does not contain document.xml."));
        return false;
    }
    return extractEntry(archive, selected, xml, errorMessage);
}

QString nativeKindTag(core::ElementKind kind)
{
    switch (kind) {
    case core::ElementKind::Stock:
        return QStringLiteral("stock");
    case core::ElementKind::Flow:
        return QStringLiteral("flow");
    case core::ElementKind::Auxiliary:
        return QStringLiteral("auxiliary");
    case core::ElementKind::Constant:
        return QStringLiteral("constant");
    case core::ElementKind::Data:
        return QStringLiteral("data");
    case core::ElementKind::Cloud:
        return QStringLiteral("cloud");
    case core::ElementKind::Comment:
        return QStringLiteral("comment");
    case core::ElementKind::Connector:
        return QStringLiteral("connector");
    }
    return {};
}

core::ElementKind nativeKind(const QString &tag, bool &valid)
{
    valid = true;
    if (tag == QStringLiteral("stock"))
        return core::ElementKind::Stock;
    if (tag == QStringLiteral("flow"))
        return core::ElementKind::Flow;
    if (tag == QStringLiteral("auxiliary"))
        return core::ElementKind::Auxiliary;
    if (tag == QStringLiteral("constant"))
        return core::ElementKind::Constant;
    if (tag == QStringLiteral("data"))
        return core::ElementKind::Data;
    if (tag == QStringLiteral("cloud"))
        return core::ElementKind::Cloud;
    if (tag == QStringLiteral("comment"))
        return core::ElementKind::Comment;
    if (tag == QStringLiteral("connector"))
        return core::ElementKind::Connector;
    valid = false;
    return core::ElementKind::Stock;
}

void writeNativeRoute(QXmlStreamWriter &writer, const core::Route &route)
{
    const QString mode = route.mode == core::RouteMode::Straight ? QStringLiteral("straight")
        : route.mode == core::RouteMode::Bezier ? QStringLiteral("bezier") : QStringLiteral("orthogonal");
    writer.writeAttribute(QStringLiteral("mode"), mode);
    writer.writeAttribute(QStringLiteral("manual"), QString::number(route.manual ? 1 : 0));
    writer.writeAttribute(QStringLiteral("locked"), QString::number(route.locked ? 1 : 0));
    for (const core::RoutePoint &point : route.controlPoints) {
        writer.writeStartElement(QStringLiteral("pt"));
        writer.writeAttribute(QStringLiteral("along"), QString::number(point.relative.x, 'g', 15));
        writer.writeAttribute(QStringLiteral("offset"), QString::number(point.relative.y, 'g', 15));
        writer.writeAttribute(QStringLiteral("automatic"), QString::number(point.automatic ? 1 : 0));
        writer.writeEndElement();
    }
}

void writeNativeFont(QXmlStreamWriter &writer, const core::FontStyle &font)
{
    if (!font.family.isEmpty())
        writer.writeAttribute(QStringLiteral("font-family"), (font.family));
    if (font.pointSize > 0)
        writer.writeAttribute(QStringLiteral("font-size"), QString::number(font.pointSize));
}

void writeNativeDimensions(QXmlStreamWriter &writer, const core::Document &document)
{
    if (document.dimensions().isEmpty())
        return;
    writer.writeStartElement(QStringLiteral("dimensions"));
    for (const core::Dimension &dimension : document.dimensions()) {
        writer.writeStartElement(QStringLiteral("dim"));
        writer.writeAttribute(QStringLiteral("name"), (dimension.displayName));
        if (dimension.elements.front().nameKey.isEmpty()) {
            writer.writeAttribute(QStringLiteral("size"), QString::number(dimension.elements.size()));
        } else {
            for (const core::DimensionElement &element : dimension.elements) {
                writer.writeStartElement(QStringLiteral("elem"));
                writer.writeAttribute(QStringLiteral("name"), (element.displayName));
                writer.writeEndElement();
            }
        }
        writer.writeEndElement();
    }
    writer.writeEndElement();
}

void writeNativeMacros(QXmlStreamWriter &writer, const core::Document &document)
{
    if (document.macros().isEmpty())
        return;
    writer.writeStartElement(QStringLiteral("macros"));
    for (const core::MacroDefinition &macro : document.macros()) {
        writer.writeStartElement(QStringLiteral("macro"));
        writer.writeAttribute(QStringLiteral("name"), macro.displayName());
        for (const core::MacroParameter &parameter : macro.parameters()) {
            writer.writeStartElement(QStringLiteral("parm"));
            writer.writeAttribute(QStringLiteral("name"), parameter.displayName());
            if (!parameter.defaultEquation().isEmpty())
                writer.writeAttribute(QStringLiteral("default"), parameter.defaultEquation());
            writer.writeEndElement();
        }
        writer.writeTextElement(QStringLiteral("eqn"), macro.equation());
        writer.writeEndElement();
    }
    writer.writeEndElement();
}

void writeNativeArrayDefinition(QXmlStreamWriter &writer, const core::Document &document, const core::ModelElement &element)
{
    if (element.dimensions().isEmpty())
        return;
    writer.writeStartElement(QStringLiteral("dimensions"));
    for (const QString &name : element.dimensions()) {
        writer.writeStartElement(QStringLiteral("dim"));
        writer.writeAttribute(QStringLiteral("name"), (document.findDimension(name)->displayName));
        writer.writeEndElement();
    }
    writer.writeEndElement();
    for (const core::ArrayElementFormula &entry : element.elementEquations()) {
        writer.writeStartElement(QStringLiteral("element"));
        QStringList indices;
        for (std::size_t index : entry.indices())
            indices.append(QString::number(index + 1));
        writer.writeAttribute(QStringLiteral("subscript"), indices.join(','));
        writer.writeTextElement(QStringLiteral("eqn"), entry.equation());
        writer.writeEndElement();
    }
}

QByteArray writeNativeXml(const core::Document &document)
{
    QByteArray output;
    QXmlStreamWriter writer(&output);
    writer.setAutoFormatting(true);
    writer.writeStartDocument();
    writer.writeStartElement(QStringLiteral("qds-document"));
    writer.writeAttribute(QStringLiteral("version"), QStringLiteral("1"));
    writeNativeDimensions(writer, document);
    writeNativeMacros(writer, document);
    writer.writeStartElement(QStringLiteral("model"));
    for (const core::ModelElement &element : document.model().elements()) {
        writer.writeStartElement(nativeKindTag(element.kind()));
        writer.writeAttribute(QStringLiteral("uid"), QString::number(element.id()));
        writer.writeAttribute(QStringLiteral("name"), (element.displayName()));
        writeNativeFont(writer, document.diagram().fontStyle(element.id()));
        const core::Vec2 position = document.diagram().position(element.id());
        writer.writeAttribute(QStringLiteral("x"), QString::number(position.x, 'g', 15));
        writer.writeAttribute(QStringLiteral("y"), QString::number(position.y, 'g', 15));
        if (element.kind() == core::ElementKind::Flow) {
            const auto &flow = static_cast<const core::Flow &>(element);
            writer.writeAttribute(QStringLiteral("from"), QString::number(flow.endpoints().from));
            writer.writeAttribute(QStringLiteral("to"), QString::number(flow.endpoints().to));
            writeNativeRoute(writer, document.diagram().route(element.id()));
        }
        writeNativeArrayDefinition(writer, document, element);
        if (element.elementEquations().empty())
            writer.writeTextElement(QStringLiteral("eqn"), (element.equation()));
        if (!element.unit().isEmpty())
            writer.writeTextElement(QStringLiteral("units"), (element.unit()));
        if (!element.comment().isEmpty())
            writer.writeTextElement(QStringLiteral("doc"), (element.comment()));
        writer.writeEndElement();
    }
    writer.writeEndElement();

    writer.writeStartElement(QStringLiteral("diagram"));
    for (const core::ViewElement &element : document.diagram().elements()) {
        writer.writeStartElement(nativeKindTag(element.kind()));
        writer.writeAttribute(QStringLiteral("uid"), QString::number(element.id()));
        writeNativeFont(writer, document.diagram().fontStyle(element.id()));
        const core::Vec2 position = document.diagram().position(element.id());
        writer.writeAttribute(QStringLiteral("x"), QString::number(position.x, 'g', 15));
        writer.writeAttribute(QStringLiteral("y"), QString::number(position.y, 'g', 15));
        if (element.kind() == core::ElementKind::Connector) {
            const auto &connector = static_cast<const core::Connector &>(element);
            writer.writeAttribute(QStringLiteral("from"), QString::number(connector.from()));
            writer.writeAttribute(QStringLiteral("to"), QString::number(connector.to()));
            writeNativeRoute(writer, document.diagram().route(element.id()));
        } else if (element.kind() == core::ElementKind::Comment) {
            writer.writeCharacters((static_cast<const core::Comment &>(element).content()));
        }
        writer.writeEndElement();
    }
    writer.writeEndElement();
    const core::Workspace &workspace = document.workspace();
    if (workspace.hasViewport) {
        writer.writeStartElement(QStringLiteral("workspace"));
        writer.writeAttribute(QStringLiteral("zoom"), QString::number(workspace.zoom, 'g', 15));
        writer.writeAttribute(QStringLiteral("center-x"), QString::number(workspace.center.x, 'g', 15));
        writer.writeAttribute(QStringLiteral("center-y"), QString::number(workspace.center.y, 'g', 15));
        if (workspace.hasScrollValues) {
            writer.writeAttribute(QStringLiteral("scroll-x"), QString::number(workspace.scrollX));
            writer.writeAttribute(QStringLiteral("scroll-y"), QString::number(workspace.scrollY));
        }
        writer.writeEndElement();
    } else {
        writer.writeEmptyElement(QStringLiteral("workspace"));
    }
    writer.writeEndElement();
    writer.writeEndDocument();
    return output;
}

struct NativeRecord {
    quint64 uid = 0;
    core::ElementData data;
};

bool parseNativeDimensions(QXmlStreamReader &reader, core::Document &document)
{
    while (reader.readNext() != QXmlStreamReader::Invalid) {
        if (reader.isEndElement() && reader.name() == QStringLiteral("dimensions"))
            return true;
        if (!reader.isStartElement() || reader.name() != QStringLiteral("dim")) {
            continue;
        }
        const QString name = reader.attributes().value(QStringLiteral("name")).toString();
        bool sizeOk = false;
        const int size = reader.attributes().value(QStringLiteral("size")).toInt(&sizeOk);
        QStringList elements;
        bool invalid = false;
        while (reader.readNext() != QXmlStreamReader::Invalid) {
            if (reader.isEndElement() && reader.name() == QStringLiteral("dim"))
                break;
            if (reader.isStartElement() && reader.name() == QStringLiteral("elem")) {
                elements.append(reader.attributes().value(QStringLiteral("name")).toString());
                reader.skipCurrentElement();
            }
        }
        if (elements.isEmpty()) {
            if (sizeOk && size > 0)
                invalid = !document.defineDimension(name, static_cast<std::size_t>(size));
            else
                invalid = true;
        } else {
            QVector<QString> names;
            for (const QString &element : elements)
                names.push_back(element);
            invalid = sizeOk || !document.defineDimension(name, names);
        }
        if (invalid)
            return false;
    }
    return false;
}

bool parseNativeMacros(QXmlStreamReader &reader, core::Document &document)
{
    while (reader.readNext() != QXmlStreamReader::Invalid) {
        if (reader.isEndElement() && reader.name() == QStringLiteral("macros"))
            return true;
        if (!reader.isStartElement() || reader.name() != QStringLiteral("macro"))
            continue;
        core::MacroDefinitionData macro;
        macro.displayName = reader.attributes().value(QStringLiteral("name")).toString();
        while (reader.readNext() != QXmlStreamReader::Invalid) {
            if (reader.isEndElement() && reader.name() == QStringLiteral("macro"))
                break;
            if (!reader.isStartElement())
                continue;
            if (reader.name() == QStringLiteral("parm")) {
                core::MacroParameterData parameter;
                parameter.displayName = reader.attributes().value(QStringLiteral("name")).toString();
                parameter.defaultEquation = reader.attributes().value(QStringLiteral("default")).toString();
                macro.parameters.push_back(std::move(parameter));
                reader.skipCurrentElement();
            } else if (reader.name() == QStringLiteral("eqn")) {
                macro.equation = reader.readElementText();
            } else {
                reader.skipCurrentElement();
            }
        }
        if (!document.defineMacro(std::move(macro)))
            return false;
    }
    return false;
}

bool parseNativeVariableDimensions(QXmlStreamReader &reader, QVector<QString> &dimensions)
{
    while (reader.readNext() != QXmlStreamReader::Invalid) {
        if (reader.isEndElement() && reader.name() == QStringLiteral("dimensions"))
            return !dimensions.isEmpty();
        if (reader.isStartElement() && reader.name() == QStringLiteral("dim")) {
            dimensions.push_back(reader.attributes().value(QStringLiteral("name")).toString());
            reader.skipCurrentElement();
        }
    }
    return false;
}

bool parseNativeArrayElement(QXmlStreamReader &reader, core::ArrayElementEquation &entry)
{
    const QStringList indices = reader.attributes().value(QStringLiteral("subscript")).toString().split(',');
    if (indices.isEmpty())
        return false;
    for (const QString &text : indices) {
        bool ok = false;
        const int index = text.toInt(&ok);
        if (!ok || index <= 0)
            return false;
        entry.indices.push_back(static_cast<std::size_t>(index - 1));
    }
    bool foundEquation = false;
    while (reader.readNext() != QXmlStreamReader::Invalid) {
        if (reader.isEndElement() && reader.name() == QStringLiteral("element"))
            return foundEquation;
        if (reader.isStartElement() && reader.name() == QStringLiteral("eqn")) {
            if (foundEquation)
                return false;
            entry.equation = reader.readElementText();
            foundEquation = true;
        } else if (reader.isStartElement()) {
            reader.skipCurrentElement();
        }
    }
    return false;
}

bool parseNativeElement(QXmlStreamReader &reader, NativeRecord &record)
{
    const QString tag = reader.name().toString();
    bool validKind = false;
    record.data.kind = nativeKind(tag, validKind);
    if (!validKind)
        return false;
    const QXmlStreamAttributes attributes = reader.attributes();
    bool uidOk = false;
    record.uid = attributes.value(QStringLiteral("uid")).toULongLong(&uidOk);
    bool xOk = false;
    bool yOk = false;
    record.data.position = {attributes.value(QStringLiteral("x")).toDouble(&xOk),
                            attributes.value(QStringLiteral("y")).toDouble(&yOk)};
    if (!uidOk || record.uid == 0 || !xOk || !yOk)
        return false;
    record.data.displayName = attributes.value(QStringLiteral("name")).toString();
    record.data.font.family = attributes.value(QStringLiteral("font-family")).toString();
    if (attributes.hasAttribute(QStringLiteral("font-size"))) {
        bool fontSizeOk = false;
        record.data.font.pointSize = attributes.value(QStringLiteral("font-size")).toInt(&fontSizeOk);
        if (!fontSizeOk || record.data.font.pointSize <= 0)
            return false;
    }
    const quint64 from = attributes.value(QStringLiteral("from")).toULongLong();
    const quint64 to = attributes.value(QStringLiteral("to")).toULongLong();
    record.data.flowEndpoints = {from, to};
    record.data.from = from;
    record.data.to = to;
    record.data.route.mode = record.data.kind == core::ElementKind::Flow
        ? core::RouteMode::Straight
        : core::RouteMode::Bezier;
    const QString mode = attributes.value(QStringLiteral("mode")).toString();
    if (mode == QStringLiteral("straight"))
        record.data.route.mode = core::RouteMode::Straight;
    else if (mode == QStringLiteral("bezier"))
        record.data.route.mode = core::RouteMode::Bezier;
    record.data.route.manual = attributes.value(QStringLiteral("manual")).toInt() != 0;
    record.data.route.locked = attributes.value(QStringLiteral("locked")).toInt() != 0;
    if (record.data.kind == core::ElementKind::Comment) {
        record.data.content = reader.readElementText(QXmlStreamReader::IncludeChildElements);
        return !reader.hasError();
    }

    while (reader.readNext() != QXmlStreamReader::Invalid) {
        if (reader.isEndElement() && reader.name().toString() == tag)
            return true;
        if (!reader.isStartElement())
            continue;
        const QString child = reader.name().toString();
        if (child == QStringLiteral("eqn"))
            record.data.equation = reader.readElementText();
        else if (child == QStringLiteral("dimensions")) {
            if (!parseNativeVariableDimensions(reader, record.data.dimensions))
                return false;
        } else if (child == QStringLiteral("element")) {
            core::ArrayElementEquation entry;
            if (!parseNativeArrayElement(reader, entry))
                return false;
            record.data.elementEquations.push_back(std::move(entry));
        } else if (child == QStringLiteral("units"))
            record.data.unit = reader.readElementText();
        else if (child == QStringLiteral("doc"))
            record.data.comment = reader.readElementText();
        else if (child == QStringLiteral("pt")) {
            bool alongOk = false;
            bool offsetOk = false;
            const double along = reader.attributes().value(QStringLiteral("along")).toDouble(&alongOk);
            const double offset = reader.attributes().value(QStringLiteral("offset")).toDouble(&offsetOk);
            if (!alongOk || !offsetOk)
                return false;
            record.data.route.controlPoints.push_back({{along, offset}, reader.attributes().value(QStringLiteral("automatic")).toInt() != 0});
        } else {
            reader.skipCurrentElement();
        }
    }
    return false;
}

bool parseNativeXml(const QByteArray &xml, core::Document &document, QString *errorMessage)
{
    QXmlStreamReader reader(xml);
    core::Document parsed;
    core::Workspace workspace;
    QVector<NativeRecord> records;
    std::unordered_set<quint64> uniqueUids;
    bool foundRoot = false;
    while (!reader.atEnd()) {
        reader.readNext();
        if (!reader.isStartElement())
            continue;
        if (reader.name() == QStringLiteral("qds-document")) {
            foundRoot = reader.attributes().value(QStringLiteral("version")).toString() == QStringLiteral("1");
            continue;
        }
        if (reader.name() == QStringLiteral("model") || reader.name() == QStringLiteral("diagram"))
            continue;
        if (reader.name() == QStringLiteral("macros")) {
            if (!parseNativeMacros(reader, parsed)) {
                setError(errorMessage, translate("The QDS macros are invalid."));
                return false;
            }
            continue;
        }
        if (reader.name() == QStringLiteral("dimensions")) {
            if (!parseNativeDimensions(reader, parsed)) {
                setError(errorMessage, translate("The QDS dimensions are invalid."));
                return false;
            }
            continue;
        }
        if (reader.name() == QStringLiteral("workspace")) {
            const QXmlStreamAttributes attributes = reader.attributes();
            bool zoomOk = true;
            bool xOk = true;
            bool yOk = true;
            bool scrollXOk = true;
            bool scrollYOk = true;
            if (attributes.hasAttribute(QStringLiteral("zoom"))) {
                workspace.hasViewport = true;
                workspace.zoom = attributes.value(QStringLiteral("zoom")).toDouble(&zoomOk);
            }
            if (attributes.hasAttribute(QStringLiteral("center-x"))) {
                workspace.hasViewport = true;
                workspace.center.x = attributes.value(QStringLiteral("center-x")).toDouble(&xOk);
            }
            if (attributes.hasAttribute(QStringLiteral("center-y"))) {
                workspace.hasViewport = true;
                workspace.center.y = attributes.value(QStringLiteral("center-y")).toDouble(&yOk);
            }
            if (attributes.hasAttribute(QStringLiteral("scroll-x"))) {
                workspace.hasScrollValues = true;
                workspace.scrollX = attributes.value(QStringLiteral("scroll-x")).toInt(&scrollXOk);
            }
            if (attributes.hasAttribute(QStringLiteral("scroll-y"))) {
                workspace.hasScrollValues = true;
                workspace.scrollY = attributes.value(QStringLiteral("scroll-y")).toInt(&scrollYOk);
            }
            if (!zoomOk || !xOk || !yOk || !scrollXOk || !scrollYOk || workspace.zoom <= 0.0
                || !std::isfinite(workspace.zoom) || !std::isfinite(workspace.center.x) || !std::isfinite(workspace.center.y)) {
                setError(errorMessage, translate("The QDS workspace is invalid."));
                return false;
            }
            reader.skipCurrentElement();
            continue;
        }
        bool validKind = false;
        nativeKind(reader.name().toString(), validKind);
        if (!validKind) {
            reader.skipCurrentElement();
            continue;
        }
        NativeRecord record;
        if (!parseNativeElement(reader, record) || !uniqueUids.insert(record.uid).second) {
            setError(errorMessage, translate("The QDS document contains an invalid or duplicate element."));
            return false;
        }
        records.push_back(std::move(record));
    }
    if (reader.hasError() || !foundRoot) {
        setError(errorMessage, translate("The document is not a supported QDS file."));
        return false;
    }

    UidMap uids;
    core::EntityId nextId = 1;
    for (const NativeRecord &record : records) {
        if (record.data.kind == core::ElementKind::Flow || record.data.kind == core::ElementKind::Connector)
            continue;
        if (!parsed.restore(record.data)) {
            setError(errorMessage, translate("Could not restore a QDS model element."));
            return false;
        }
        uids[record.uid] = nextId++;
    }
    const auto mappedId = [&uids](quint64 uid) {
        const auto found = uids.find(uid);
        return found == uids.end() ? core::EntityId(0) : found->second;
    };
    for (const NativeRecord &record : records) {
        if (record.data.kind != core::ElementKind::Flow && record.data.kind != core::ElementKind::Connector)
            continue;
        NativeRecord mapped = record;
        mapped.data.flowEndpoints = {mappedId(mapped.data.flowEndpoints.from), mappedId(mapped.data.flowEndpoints.to)};
        mapped.data.from = mappedId(mapped.data.from);
        mapped.data.to = mappedId(mapped.data.to);
        if ((record.data.kind == core::ElementKind::Flow
                && (mapped.data.flowEndpoints.from == 0 || mapped.data.flowEndpoints.to == 0))
            || (record.data.kind == core::ElementKind::Connector && (mapped.data.from == 0 || mapped.data.to == 0))) {
            setError(errorMessage, translate("The QDS document contains an invalid endpoint reference."));
            return false;
        }
        if (!parsed.restore(mapped.data)) {
            setError(errorMessage, translate("Could not restore a routed QDS element."));
            return false;
        }
        uids[record.uid] = nextId++;
    }
    parsed.setWorkspace(workspace);
    document = std::move(parsed);
    return true;
}

bool readFileBytes(const QString &fileName, QByteArray &contents, QString *errorMessage)
{
    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly)) {
        setError(errorMessage, translate("Could not open the file for reading: %1").arg(file.errorString()));
        return false;
    }
    contents = file.readAll();
    if (file.error() != QFileDevice::NoError) {
        setError(errorMessage, translate("Could not read the file: %1").arg(file.errorString()));
        return false;
    }
    return true;
}

bool writeFileBytes(const QString &fileName, const QByteArray &contents, QString *errorMessage)
{
    QSaveFile file(fileName);
    if (!file.open(QIODevice::WriteOnly)) {
        setError(errorMessage, translate("Could not open the file for writing: %1").arg(file.errorString()));
        return false;
    }
    if (file.write(contents) != contents.size() || !file.commit()) {
        setError(errorMessage, translate("Could not write the file: %1").arg(file.errorString()));
        return false;
    }
    return true;
}

} // namespace

bool DocumentFile::save(const QString &fileName, const core::Document &document, QString *errorMessage)
{
    const QByteArray archive = makeZip(writeNativeXml(document));
    if (archive.isEmpty()) {
        setError(errorMessage, translate("Could not create the QDS ZIP container."));
        return false;
    }
    return writeFileBytes(fileName, archive, errorMessage);
}

bool DocumentFile::open(const QString &fileName, core::Document &document, QString *errorMessage, bool *nativeFormat)
{
    if (nativeFormat)
        *nativeFormat = false;
    QByteArray contents;
    if (!readFileBytes(fileName, contents, errorMessage))
        return false;
    if (!contents.startsWith("PK"))
        return XmileFile::importFile(fileName, document, errorMessage);

    QByteArray xml;
    if (!readZip(contents, xml, errorMessage) || !parseNativeXml(xml, document, errorMessage))
        return false;
    if (nativeFormat)
        *nativeFormat = true;
    return true;
}

} // namespace qds::io
