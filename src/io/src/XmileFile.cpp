#include <qds/io/XmileFile.h>

#include <QCoreApplication>
#include <QFile>
#include <QSaveFile>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>
#include <QtGlobal>
#include <zlib.h>

#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace qds::io {
namespace {

constexpr quint32 localHeaderSignature = 0x04034b50;
constexpr quint32 centralHeaderSignature = 0x02014b50;
constexpr quint32 endSignature = 0x06054b50;
constexpr int maximumEntrySize = 64 * 1024 * 1024;

using UidMap = std::unordered_map<quint64, core::EntityId>;
using VariableKindMap = std::unordered_map<std::string, core::ElementKind>;

QString translate(const char *text)
{
    return QCoreApplication::translate("XmileFile", text);
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
        setError(errorMessage, translate("Could not compress the XMILE document."));
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
        setError(errorMessage, translate("The compressed XMILE document is damaged."));
        return false;
    }
    output.resize(static_cast<int>(actualSize));
    return true;
}

QByteArray makeZip(const QByteArray &xml, const QByteArray &workspace)
{
    struct WrittenEntry {
        QByteArray name;
        QByteArray compressed;
        quint32 crc = 0;
        quint32 size = 0;
        quint32 offset = 0;
    };
    const QPair<QByteArray, QByteArray> sources[] = {
        {QByteArray("model.xmile"), xml}, {QByteArray("workspace.xml"), workspace}};
    QByteArray output;
    std::vector<WrittenEntry> entries;
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
        setError(errorMessage, translate("The XMILE document failed its checksum verification."));
        return false;
    }
    return true;
}

bool readZip(const QByteArray &archive, QByteArray &xml, QByteArray &workspace, QString *errorMessage)
{
    if (archive.size() < 22) {
        setError(errorMessage, translate("The XMILE container is too small."));
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
        setError(errorMessage, translate("The XMILE container has no ZIP directory."));
        return false;
    }

    const quint16 entryCount = read16(archive, endOffset + 10);
    const quint32 centralOffset = read32(archive, endOffset + 16);
    int offset = static_cast<int>(centralOffset);
    ZipEntry selected;
    ZipEntry workspaceEntry;
    bool found = false;
    bool foundWorkspace = false;
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
        if (name.compare(QStringLiteral("workspace.xml"), Qt::CaseInsensitive) == 0) {
            workspaceEntry = entry;
            foundWorkspace = true;
        } else if (name.compare(QStringLiteral("model.xmile"), Qt::CaseInsensitive) == 0) {
            selected = entry;
            found = true;
        } else if (!found && name.endsWith(QStringLiteral(".xmile"), Qt::CaseInsensitive)) {
            selected = entry;
            found = true;
        }
        offset += 46 + nameLength + extraLength + commentLength;
    }

    if (!found) {
        setError(errorMessage, translate("The ZIP container does not contain an XMILE document."));
        return false;
    }
    if (!extractEntry(archive, selected, xml, errorMessage))
        return false;
    workspace.clear();
    return !foundWorkspace || extractEntry(archive, workspaceEntry, workspace, errorMessage);
}

QString variableTag(core::ElementKind kind)
{
    switch (kind) {
    case core::ElementKind::Stock:
        return QStringLiteral("stock");
    case core::ElementKind::Flow:
        return QStringLiteral("flow");
    case core::ElementKind::Auxiliary:
    case core::ElementKind::Constant:
    case core::ElementKind::Data:
        return QStringLiteral("aux");
    case core::ElementKind::Cloud:
        return QStringLiteral("cloud");
    case core::ElementKind::Comment:
        return QStringLiteral("text_box");
    case core::ElementKind::Connector:
        return QStringLiteral("connector");
    }
    return {};
}

QString elementName(const core::DocumentElement &element)
{
    if (const auto *model = dynamic_cast<const core::ModelElement *>(&element))
        return QString::fromStdString(model->displayName);
    if (const auto *comment = dynamic_cast<const core::Comment *>(&element))
        return QString::fromStdString(comment->content);
    return {};
}

const core::ModelElement *findElementByName(const core::Document &document, const std::string &name)
{
    return document.model().findByName(name);
}

QByteArray writeXml(const core::Document &document)
{
    const core::Model &model = document.model();
    QByteArray output;
    QXmlStreamWriter writer(&output);
    writer.setAutoFormatting(true);
    writer.writeStartDocument();
    writer.writeStartElement(QStringLiteral("xmile"));
    writer.writeDefaultNamespace(QStringLiteral("http://docs.oasis-open.org/xmile/ns/XMILE/v1.0"));
    writer.writeAttribute(QStringLiteral("version"), QStringLiteral("1.1"));

    writer.writeStartElement(QStringLiteral("header"));
    writer.writeStartElement(QStringLiteral("smile"));
    writer.writeAttribute(QStringLiteral("version"), QStringLiteral("1.0"));
    writer.writeAttribute(QStringLiteral("namespace"), QStringLiteral("std"));
    writer.writeEndElement();
    writer.writeTextElement(QStringLiteral("vendor"), QStringLiteral("QDynamicSimulator"));
    writer.writeTextElement(QStringLiteral("product"), QStringLiteral("QDynamicSimulator"));
    writer.writeEndElement();

    writer.writeStartElement(QStringLiteral("sim_specs"));
    writer.writeTextElement(QStringLiteral("start"), QStringLiteral("0"));
    writer.writeTextElement(QStringLiteral("stop"), QStringLiteral("100"));
    writer.writeTextElement(QStringLiteral("dt"), QStringLiteral("0.25"));
    writer.writeEndElement();

    writer.writeStartElement(QStringLiteral("model"));
    writer.writeStartElement(QStringLiteral("variables"));
    for (const core::ModelElement &variable : model.elements()) {
        const QString name = QString::fromStdString(variable.displayName);
        writer.writeStartElement(variableTag(variable.kind()));
        writer.writeAttribute(QStringLiteral("name"), name);
        writer.writeTextElement(
            QStringLiteral("eqn"),
            variable.equation.empty() ? QStringLiteral("0") : QString::fromStdString(variable.equation));
        if (!variable.unit.empty())
            writer.writeTextElement(QStringLiteral("units"), QString::fromStdString(variable.unit));
        if (!variable.comment.empty())
            writer.writeTextElement(QStringLiteral("doc"), QString::fromStdString(variable.comment));
        if (variable.kind() == core::ElementKind::Stock) {
            for (const core::ModelElement &candidate : model.elements()) {
                if (candidate.kind() != core::ElementKind::Flow)
                    continue;
                const auto &flow = static_cast<const core::Flow &>(candidate);
                if (flow.endpoints.to == variable.id())
                    writer.writeTextElement(QStringLiteral("inflow"), QString::fromStdString(flow.displayName));
                if (flow.endpoints.from == variable.id())
                    writer.writeTextElement(QStringLiteral("outflow"), QString::fromStdString(flow.displayName));
            }
        }
        writer.writeEndElement();
    }
    writer.writeEndElement();

    writer.writeStartElement(QStringLiteral("views"));
    writer.writeStartElement(QStringLiteral("view"));
    writer.writeAttribute(QStringLiteral("type"), QStringLiteral("stock_flow"));
    for (const core::DocumentElement &element : document.elements()) {
        if (element.kind() == core::ElementKind::Connector || element.kind() == core::ElementKind::Cloud)
            continue;
        writer.writeStartElement(variableTag(element.kind()));
        const core::Vec2 position = document.diagram().position(element.id());
        if (element.kind() != core::ElementKind::Comment)
            writer.writeAttribute(QStringLiteral("name"), elementName(element));
        else
            writer.writeAttribute(QStringLiteral("uid"), QString::number(element.id()));
        writer.writeAttribute(QStringLiteral("x"), QString::number(position.x, 'g', 15));
        writer.writeAttribute(QStringLiteral("y"), QString::number(position.y, 'g', 15));
        if (element.kind() == core::ElementKind::Flow) {
            const auto &flow = static_cast<const core::Flow &>(element);
            const core::Route &route = document.diagram().route(element.id());
            writer.writeStartElement(QStringLiteral("pts"));
            const auto writePoint = [&writer](const core::Vec2 &point) {
                writer.writeStartElement(QStringLiteral("pt"));
                writer.writeAttribute(QStringLiteral("x"), QString::number(point.x, 'g', 15));
                writer.writeAttribute(QStringLiteral("y"), QString::number(point.y, 'g', 15));
                writer.writeEndElement();
            };
            writePoint(document.diagram().position(flow.endpoints.from));
            for (const core::Vec2 &point : route.controlPoints)
                writePoint(point);
            writePoint(document.diagram().position(flow.endpoints.to));
            writer.writeEndElement();
        }
        if (element.kind() == core::ElementKind::Comment)
            writer.writeCharacters(elementName(element));
        writer.writeEndElement();
    }
    for (const core::ViewElement &element : document.diagram().elements()) {
        if (element.kind() != core::ElementKind::Connector)
            continue;
        const auto &connection = static_cast<const core::Connector &>(element);
        const core::DocumentElement *from = document.find(connection.from);
        const core::DocumentElement *to = document.find(connection.to);
        if (!from || !to || !dynamic_cast<const core::ModelElement *>(from)
            || !dynamic_cast<const core::ModelElement *>(to))
            continue;
        writer.writeStartElement(QStringLiteral("connector"));
        writer.writeAttribute(QStringLiteral("uid"), QString::number(connection.id()));
        writer.writeTextElement(QStringLiteral("from"), elementName(*from));
        writer.writeTextElement(QStringLiteral("to"), elementName(*to));
        const core::Route &route = document.diagram().route(connection.id());
        if (!route.controlPoints.empty()) {
            writer.writeStartElement(QStringLiteral("pts"));
            for (const core::Vec2 &point : route.controlPoints) {
                writer.writeStartElement(QStringLiteral("pt"));
                writer.writeAttribute(QStringLiteral("x"), QString::number(point.x, 'g', 15));
                writer.writeAttribute(QStringLiteral("y"), QString::number(point.y, 'g', 15));
                writer.writeEndElement();
            }
            writer.writeEndElement();
        }
        writer.writeEndElement();
    }
    writer.writeEndElement();
    writer.writeEndElement();
    writer.writeEndElement();
    writer.writeEndElement();
    writer.writeEndDocument();
    return output;
}

struct PendingConnection {
    std::string from;
    std::string to;
    quint64 fromUid = 0;
    quint64 toUid = 0;
    core::Route route;
};

struct PendingFlowEndpoint {
    std::string flow;
    std::string stock;
    bool inflow = false;
};

struct PendingFlowRoute {
    std::string flow;
    core::Route route;
    core::Vec2 position;
    bool hasPosition = false;
};

bool parseVariable(
    QXmlStreamReader &reader,
    core::Document &model,
    const VariableKindMap &variableKinds,
    std::vector<core::ElementData> &pendingFlows,
    std::vector<PendingFlowEndpoint> &pendingFlowEndpoints,
    QString *errorMessage)
{
    const QString tag = reader.name().toString();
    const QString name = reader.attributes().value(QStringLiteral("name")).toString().trimmed();
    if (name.isEmpty()) {
        setError(errorMessage, translate("A model variable has no name."));
        return false;
    }

    core::ElementData variable;
    variable.displayName = name.toStdString();
    if (tag == QStringLiteral("stock"))
        variable.kind = core::ElementKind::Stock;
    else if (tag == QStringLiteral("flow")) {
        variable.kind = core::ElementKind::Flow;
        variable.route.mode = core::RouteMode::Straight;
    } else {
        variable.kind = core::ElementKind::Auxiliary;
        const auto kind = variableKinds.find(variable.displayName);
        if (kind != variableKinds.end())
            variable.kind = kind->second;
    }

    QStringList inflows;
    QStringList outflows;
    while (reader.readNext() != QXmlStreamReader::Invalid) {
        if (reader.isEndElement() && reader.name().toString() == tag)
            break;
        if (!reader.isStartElement())
            continue;
        const QString child = reader.name().toString();
        if (child == QStringLiteral("eqn"))
            variable.equation = reader.readElementText().trimmed().toStdString();
        else if (child == QStringLiteral("units"))
            variable.unit = reader.readElementText().trimmed().toStdString();
        else if (child == QStringLiteral("doc"))
            variable.comment = reader.readElementText().toStdString();
        else if (child == QStringLiteral("inflow"))
            inflows.append(reader.readElementText().trimmed());
        else if (child == QStringLiteral("outflow"))
            outflows.append(reader.readElementText().trimmed());
        else
            reader.skipCurrentElement();
    }

    if (variable.kind == core::ElementKind::Flow) {
        pendingFlows.push_back(std::move(variable));
    } else if (!model.restore(variable)) {
        setError(errorMessage, translate("The model contains a duplicate variable name: %1").arg(name));
        return false;
    }
    for (const QString &flow : inflows)
        pendingFlowEndpoints.push_back({flow.toStdString(), variable.displayName, true});
    for (const QString &flow : outflows)
        pendingFlowEndpoints.push_back({flow.toStdString(), variable.displayName, false});
    return true;
}

bool parseVariables(
    QXmlStreamReader &reader,
    core::Document &model,
    const VariableKindMap &variableKinds,
    std::vector<core::ElementData> &pendingFlows,
    std::vector<PendingFlowEndpoint> &pendingFlowEndpoints,
    QString *errorMessage)
{
    while (reader.readNext() != QXmlStreamReader::Invalid) {
        if (reader.isEndElement() && reader.name() == QStringLiteral("variables"))
            return true;
        if (!reader.isStartElement())
            continue;
        const QString tag = reader.name().toString();
        if (tag == QStringLiteral("stock") || tag == QStringLiteral("flow") || tag == QStringLiteral("aux")) {
            if (!parseVariable(reader, model, variableKinds, pendingFlows, pendingFlowEndpoints, errorMessage))
                return false;
        } else {
            reader.skipCurrentElement();
        }
    }
    return true;
}

void parseConnectorEndpoint(QXmlStreamReader &reader, PendingConnection &pending, bool from)
{
    QString name;
    quint64 uid = 0;
    const QString tag = reader.name().toString();
    while (reader.readNext() != QXmlStreamReader::Invalid) {
        if (reader.isEndElement() && reader.name().toString() == tag)
            break;
        if (reader.isCharacters())
            name += reader.text().toString();
        if (!reader.isStartElement())
            continue;
        const QXmlStreamAttributes attributes = reader.attributes();
        if (attributes.hasAttribute(QStringLiteral("uid")))
            uid = attributes.value(QStringLiteral("uid")).toULongLong();
        if (attributes.hasAttribute(QStringLiteral("name")))
            name = attributes.value(QStringLiteral("name")).toString();
        name += reader.readElementText(QXmlStreamReader::IncludeChildElements);
    }
    if (from) {
        pending.from = name.trimmed().toStdString();
        pending.fromUid = uid;
    } else {
        pending.to = name.trimmed().toStdString();
        pending.toUid = uid;
    }
}

void parseConnector(QXmlStreamReader &reader, std::vector<PendingConnection> &pendingConnections)
{
    PendingConnection pending;
    pending.from = reader.attributes().value(QStringLiteral("from")).toString().toStdString();
    pending.to = reader.attributes().value(QStringLiteral("to")).toString().toStdString();
    while (reader.readNext() != QXmlStreamReader::Invalid) {
        if (reader.isEndElement() && reader.name() == QStringLiteral("connector"))
            break;
        if (!reader.isStartElement())
            continue;
        if (reader.name() == QStringLiteral("from"))
            parseConnectorEndpoint(reader, pending, true);
        else if (reader.name() == QStringLiteral("to"))
            parseConnectorEndpoint(reader, pending, false);
        else if (reader.name() == QStringLiteral("pts")) {
            while (reader.readNext() != QXmlStreamReader::Invalid) {
                if (reader.isEndElement() && reader.name() == QStringLiteral("pts"))
                    break;
                if (!reader.isStartElement() || reader.name() != QStringLiteral("pt"))
                    continue;
                bool xOk = false;
                bool yOk = false;
                const double x = reader.attributes().value(QStringLiteral("x")).toDouble(&xOk);
                const double y = reader.attributes().value(QStringLiteral("y")).toDouble(&yOk);
                if (xOk && yOk)
                    pending.route.controlPoints.push_back({x, y});
            }
        } else {
            reader.skipCurrentElement();
        }
    }
    if ((!pending.from.empty() || pending.fromUid != 0) && (!pending.to.empty() || pending.toUid != 0)) {
        pending.route.manual = !pending.route.controlPoints.empty();
        pendingConnections.push_back(std::move(pending));
    }
}

void parseView(
    QXmlStreamReader &reader,
    core::Document &model,
    std::vector<PendingConnection> &pendingConnections,
    std::vector<PendingFlowRoute> &pendingFlowRoutes,
    std::unordered_set<core::EntityId> &positioned,
    UidMap &uids)
{
    while (reader.readNext() != QXmlStreamReader::Invalid) {
        if (reader.isEndElement() && reader.name() == QStringLiteral("view"))
            return;
        if (!reader.isStartElement())
            continue;

        const QString tag = reader.name().toString();
        if (tag == QStringLiteral("stock") || tag == QStringLiteral("flow") || tag == QStringLiteral("aux")
            || tag == QStringLiteral("cloud") || tag == QStringLiteral("text_box")) {
            const QXmlStreamAttributes attributes = reader.attributes();
            const std::string name = attributes.value(QStringLiteral("name")).toString().toStdString();
            const core::DocumentElement *variable = findElementByName(model, name);
            if (!variable && (tag == QStringLiteral("cloud") || tag == QStringLiteral("text_box"))) {
                core::EntityId created = 0;
                if (tag == QStringLiteral("text_box")) {
                    QString content = reader.readElementText(QXmlStreamReader::IncludeChildElements);
                    if (content.isEmpty())
                        content = attributes.value(QStringLiteral("name")).toString();
                    created = model.createComment(content.toStdString());
                } else {
                    created = model.createCloud();
                }
                variable = model.find(created);
                const quint64 uid = attributes.value(QStringLiteral("uid")).toULongLong();
                if (created != 0 && uid != 0)
                    uids[uid] = created;
            }
            bool xOk = false;
            bool yOk = false;
            const double x = attributes.value(QStringLiteral("x")).toDouble(&xOk);
            const double y = attributes.value(QStringLiteral("y")).toDouble(&yOk);
            if (variable && xOk && yOk) {
                model.setPosition(variable->id(), {x, y});
                if (tag == QStringLiteral("stock") || tag == QStringLiteral("aux"))
                    positioned.insert(variable->id());
            }
            if (tag == QStringLiteral("flow")) {
                PendingFlowRoute pending;
                pending.flow = name;
                pending.route.mode = core::RouteMode::Straight;
                pending.position = {x, y};
                pending.hasPosition = xOk && yOk;
                while (reader.readNext() != QXmlStreamReader::Invalid) {
                    if (reader.isEndElement() && reader.name() == QStringLiteral("flow"))
                        break;
                    if (!reader.isStartElement() || reader.name() != QStringLiteral("pts"))
                        continue;
                    while (reader.readNext() != QXmlStreamReader::Invalid) {
                        if (reader.isEndElement() && reader.name() == QStringLiteral("pts"))
                            break;
                        if (!reader.isStartElement() || reader.name() != QStringLiteral("pt"))
                            continue;
                        bool pointXOk = false;
                        bool pointYOk = false;
                        const double pointX = reader.attributes().value(QStringLiteral("x")).toDouble(&pointXOk);
                        const double pointY = reader.attributes().value(QStringLiteral("y")).toDouble(&pointYOk);
                        if (pointXOk && pointYOk)
                            pending.route.controlPoints.push_back({pointX, pointY});
                    }
                }
                pending.route.manual = pending.route.controlPoints.size() > 2;
                pendingFlowRoutes.push_back(std::move(pending));
            }
        } else if (tag == QStringLiteral("connector")) {
            parseConnector(reader, pendingConnections);
        }
    }
}

void layoutMissingVariables(core::Document &document, const std::unordered_set<core::EntityId> &positioned)
{
    std::vector<const core::ModelElement *> missing;
    for (const core::ModelElement &element : document.model().elements()) {
        if (element.kind() != core::ElementKind::Flow && !positioned.count(element.id()))
            missing.push_back(&element);
    }
    std::sort(missing.begin(), missing.end(), [](const core::ModelElement *left, const core::ModelElement *right) {
        return left->id() < right->id();
    });
    for (std::size_t index = 0; index < missing.size(); ++index) {
        document.setPosition(
            missing[index]->id(),
            {100.0 + static_cast<int>(index % 5) * 180.0,
             100.0 + static_cast<int>(index / 5) * 140.0});
    }
}

const PendingFlowRoute *findFlowRoute(const std::vector<PendingFlowRoute> &routes, const std::string &name)
{
    const auto found = std::find_if(routes.begin(), routes.end(), [&name](const PendingFlowRoute &route) {
        return route.flow == name;
    });
    return found == routes.end() ? nullptr : &*found;
}

bool createImportedFlows(
    core::Document &document,
    const std::vector<core::ElementData> &flows,
    const std::vector<PendingFlowEndpoint> &endpoints,
    const std::vector<PendingFlowRoute> &routes,
    QString *errorMessage)
{
    for (const PendingFlowEndpoint &endpoint : endpoints) {
        const auto found = std::find_if(flows.begin(), flows.end(), [&endpoint](const core::ElementData &flow) {
            return flow.displayName == endpoint.flow;
        });
        if (found == flows.end() || !document.model().findByName(endpoint.stock)) {
            setError(errorMessage, translate("A stock references an undefined flow or stock."));
            return false;
        }
    }

    std::size_t flowIndex = 0;
    for (const core::ElementData &source : flows) {
        core::EntityId fromStock = 0;
        core::EntityId toStock = 0;
        for (const PendingFlowEndpoint &endpoint : endpoints) {
            if (endpoint.flow != source.displayName)
                continue;
            const core::ModelElement *stock = document.model().findByName(endpoint.stock);
            if (!stock || stock->kind() != core::ElementKind::Stock) {
                setError(errorMessage, translate("A flow endpoint is not a stock: %1")
                    .arg(QString::fromStdString(endpoint.stock)));
                return false;
            }
            core::EntityId &slot = endpoint.inflow ? toStock : fromStock;
            if (slot != 0 && slot != stock->id()) {
                setError(errorMessage, translate("A flow has multiple source or target stocks: %1")
                    .arg(QString::fromStdString(source.displayName)));
                return false;
            }
            slot = stock->id();
        }

        const PendingFlowRoute *view = findFlowRoute(routes, source.displayName);
        const std::vector<core::Vec2> points = view ? view->route.controlPoints : std::vector<core::Vec2> {};
        const bool hasPath = points.size() >= 2;
        core::Vec2 fromPosition;
        core::Vec2 toPosition;
        if (fromStock != 0) {
            fromPosition = document.diagram().position(fromStock);
        } else if (hasPath) {
            fromPosition = points.front();
        } else if (toStock != 0) {
            fromPosition = document.diagram().position(toStock) + core::Vec2 {-160.0, 0.0};
        } else if (view && view->hasPosition) {
            fromPosition = view->position + core::Vec2 {-80.0, 0.0};
        } else {
            fromPosition = {100.0 + static_cast<int>(flowIndex % 5) * 220.0,
                            100.0 + static_cast<int>(flowIndex / 5) * 180.0};
        }
        if (toStock != 0) {
            toPosition = document.diagram().position(toStock);
        } else if (hasPath) {
            toPosition = points.back();
        } else if (fromStock != 0) {
            toPosition = fromPosition + core::Vec2 {160.0, 0.0};
        } else if (view && view->hasPosition) {
            toPosition = view->position + core::Vec2 {80.0, 0.0};
        } else {
            toPosition = fromPosition + core::Vec2 {160.0, 0.0};
        }

        const core::EntityId from = fromStock != 0 ? fromStock : document.createCloud(fromPosition);
        const core::EntityId to = toStock != 0 ? toStock : document.createCloud(toPosition);
        const core::EntityId id = document.createFlow(from, to);
        if (id == 0) {
            setError(errorMessage, translate("Could not create flow endpoints for: %1")
                .arg(QString::fromStdString(source.displayName)));
            return false;
        }
        core::ElementData target = source;
        target.id = id;
        target.flowEndpoints = {from, to};
        target.position = view && view->hasPosition
            ? view->position
            : core::Vec2 {(fromPosition.x + toPosition.x) / 2.0, (fromPosition.y + toPosition.y) / 2.0};
        if (view) {
            target.route = view->route;
            target.route.controlPoints.clear();
            if (hasPath)
                target.route.controlPoints.assign(points.begin() + 1, points.end() - 1);
            target.route.manual = !target.route.controlPoints.empty();
        }
        if (!document.apply(target)) {
            setError(errorMessage, translate("The model contains a duplicate or invalid flow: %1")
                .arg(QString::fromStdString(source.displayName)));
            return false;
        }
        ++flowIndex;
    }
    return true;
}

bool parseXml(
    const QByteArray &xml,
    core::Document &model,
    const VariableKindMap &variableKinds,
    UidMap &uids,
    QString *errorMessage)
{
    QXmlStreamReader reader(xml);
    core::Document parsed;
    std::vector<PendingConnection> pendingConnections;
    std::vector<core::ElementData> pendingFlows;
    std::vector<PendingFlowEndpoint> pendingFlowEndpoints;
    std::vector<PendingFlowRoute> pendingFlowRoutes;
    std::unordered_set<core::EntityId> positioned;
    bool foundRoot = false;
    bool foundView = false;

    while (!reader.atEnd()) {
        reader.readNext();
        if (!reader.isStartElement())
            continue;
        const QString tag = reader.name().toString();
        if (tag == QStringLiteral("xmile"))
            foundRoot = true;
        else if (tag == QStringLiteral("variables")) {
            if (!parseVariables(reader, parsed, variableKinds, pendingFlows, pendingFlowEndpoints, errorMessage))
                return false;
        } else if (tag == QStringLiteral("view") && !foundView) {
            foundView = true;
            parseView(reader, parsed, pendingConnections, pendingFlowRoutes, positioned, uids);
        }
    }

    if (reader.hasError()) {
        setError(
            errorMessage,
            translate("Invalid XMILE XML at line %1: %2").arg(reader.lineNumber()).arg(reader.errorString()));
        return false;
    }
    if (!foundRoot) {
        setError(errorMessage, translate("The document is not an XMILE model."));
        return false;
    }

    layoutMissingVariables(parsed, positioned);
    if (!createImportedFlows(parsed, pendingFlows, pendingFlowEndpoints, pendingFlowRoutes, errorMessage))
        return false;

    for (const PendingConnection &connection : pendingConnections) {
        const core::DocumentElement *from = connection.fromUid != 0
            ? parsed.find(uids[connection.fromUid])
            : findElementByName(parsed, connection.from);
        const core::DocumentElement *to = connection.toUid != 0
            ? parsed.find(uids[connection.toUid])
            : findElementByName(parsed, connection.to);
        if (!from || !to)
            continue;
        core::EntityId id = parsed.createConnector(from->id(), to->id());
        if (id == 0) {
            for (const core::ViewElement &element : parsed.diagram().elements()) {
                if (element.kind() != core::ElementKind::Connector)
                    continue;
                const auto &existing = static_cast<const core::Connector &>(element);
                if (existing.from == from->id() && existing.to == to->id()) {
                    id = existing.id();
                    break;
                }
            }
        }
        if (id != 0)
            parsed.setRoute(id, connection.route);
    }

    model = std::move(parsed);
    return true;
}

void writeWorkspaceEndpoint(
    QXmlStreamWriter &writer,
    const core::Document &document,
    const QString &attribute,
    core::EntityId id)
{
    const core::DocumentElement *element = document.find(id);
    if (!element)
        return;
    if (dynamic_cast<const core::ModelElement *>(element))
        writer.writeAttribute(attribute, elementName(*element));
    else
        writer.writeAttribute(attribute + QStringLiteral("-uid"), QString::number(id));
}

QByteArray writeWorkspace(const core::Document &document)
{
    QByteArray output;
    QXmlStreamWriter writer(&output);
    writer.setAutoFormatting(true);
    writer.writeStartDocument();
    writer.writeStartElement(QStringLiteral("qds-workspace"));
    writer.writeAttribute(QStringLiteral("version"), QStringLiteral("2"));
    for (const core::ViewElement &element : document.diagram().elements()) {
        if (element.kind() != core::ElementKind::Cloud)
            continue;
        const core::Vec2 position = document.diagram().position(element.id());
        writer.writeStartElement(QStringLiteral("cloud"));
        writer.writeAttribute(QStringLiteral("uid"), QString::number(element.id()));
        writer.writeAttribute(QStringLiteral("x"), QString::number(position.x, 'g', 15));
        writer.writeAttribute(QStringLiteral("y"), QString::number(position.y, 'g', 15));
        writer.writeEndElement();
    }
    for (const core::ModelElement &element : document.model().elements()) {
        if (element.kind() != core::ElementKind::Constant && element.kind() != core::ElementKind::Data)
            continue;
        writer.writeStartElement(QStringLiteral("variable-kind"));
        writer.writeAttribute(QStringLiteral("name"), QString::fromStdString(element.displayName));
        writer.writeAttribute(
            QStringLiteral("kind"),
            element.kind() == core::ElementKind::Constant ? QStringLiteral("constant") : QStringLiteral("data"));
        writer.writeEndElement();
    }
    for (const core::ViewElement &element : document.diagram().elements()) {
        if (element.kind() != core::ElementKind::Connector)
            continue;
        const auto &connection = static_cast<const core::Connector &>(element);
        const core::DocumentElement *from = document.find(connection.from);
        const core::DocumentElement *to = document.find(connection.to);
        if (!from || !to)
            continue;
        const core::Route route = document.diagram().route(connection.id());
        writer.writeStartElement(QStringLiteral("route"));
        writeWorkspaceEndpoint(writer, document, QStringLiteral("from"), connection.from);
        writeWorkspaceEndpoint(writer, document, QStringLiteral("to"), connection.to);
        const QString mode = route.mode == core::RouteMode::Straight ? QStringLiteral("straight")
            : route.mode == core::RouteMode::Bezier ? QStringLiteral("bezier") : QStringLiteral("orthogonal");
        writer.writeAttribute(QStringLiteral("mode"), mode);
        writer.writeAttribute(QStringLiteral("manual"), QString::number(route.manual ? 1 : 0));
        writer.writeAttribute(QStringLiteral("locked"), QString::number(route.locked ? 1 : 0));
        for (const core::Vec2 &point : route.controlPoints) {
            writer.writeStartElement(QStringLiteral("pt"));
            writer.writeAttribute(QStringLiteral("x"), QString::number(point.x, 'g', 15));
            writer.writeAttribute(QStringLiteral("y"), QString::number(point.y, 'g', 15));
            writer.writeEndElement();
        }
        writer.writeEndElement();
    }
    for (const core::ModelElement &element : document.model().elements()) {
        if (element.kind() != core::ElementKind::Flow)
            continue;
        const auto &flow = static_cast<const core::Flow &>(element);
        const core::Route route = document.diagram().route(flow.id());
        writer.writeStartElement(QStringLiteral("flow-route"));
        writer.writeAttribute(QStringLiteral("name"), QString::fromStdString(flow.displayName));
        writeWorkspaceEndpoint(writer, document, QStringLiteral("from"), flow.endpoints.from);
        writeWorkspaceEndpoint(writer, document, QStringLiteral("to"), flow.endpoints.to);
        writer.writeAttribute(QStringLiteral("mode"), route.mode == core::RouteMode::Bezier ? QStringLiteral("bezier") : QStringLiteral("straight"));
        writer.writeAttribute(QStringLiteral("manual"), QString::number(route.manual ? 1 : 0));
        writer.writeAttribute(QStringLiteral("locked"), QString::number(route.locked ? 1 : 0));
        for (const core::Vec2 &point : route.controlPoints) {
            writer.writeStartElement(QStringLiteral("pt"));
            writer.writeAttribute(QStringLiteral("x"), QString::number(point.x, 'g', 15));
            writer.writeAttribute(QStringLiteral("y"), QString::number(point.y, 'g', 15));
            writer.writeEndElement();
        }
        writer.writeEndElement();
    }
    writer.writeEndElement();
    writer.writeEndDocument();
    return output;
}

core::EntityId workspaceEndpoint(
    const core::Document &document,
    const QXmlStreamAttributes &attributes,
    const QString &name,
    UidMap &uids,
    core::EntityId current = 0)
{
    if (attributes.hasAttribute(name)) {
        const core::ModelElement *element = document.model().findByName(attributes.value(name).toString().toStdString());
        return element ? element->id() : 0;
    }
    const QString uidName = name + QStringLiteral("-uid");
    const quint64 uid = attributes.value(uidName).toULongLong();
    if (uid == 0)
        return 0;
    const auto found = uids.find(uid);
    if (found != uids.end())
        return found->second;
    const core::DocumentElement *element = document.find(current);
    if (element && element->kind() == core::ElementKind::Cloud) {
        uids[uid] = current;
        return current;
    }
    return 0;
}

VariableKindMap readWorkspaceVariableKinds(const QByteArray &xml)
{
    VariableKindMap kinds;
    QXmlStreamReader reader(xml);
    while (!reader.atEnd()) {
        reader.readNext();
        if (!reader.isStartElement() || reader.name() != QStringLiteral("variable-kind"))
            continue;
        const QXmlStreamAttributes attributes = reader.attributes();
        const QString kind = attributes.value(QStringLiteral("kind")).toString();
        if (kind != QStringLiteral("constant") && kind != QStringLiteral("data")) {
            reader.skipCurrentElement();
            continue;
        }
        kinds[attributes.value(QStringLiteral("name")).toString().toStdString()] =
            kind == QStringLiteral("constant") ? core::ElementKind::Constant : core::ElementKind::Data;
        reader.skipCurrentElement();
    }
    return kinds;
}

void applyWorkspace(core::Document &document, const QByteArray &xml, UidMap &uids)
{
    struct CloudRecord {
        quint64 uid = 0;
        core::Vec2 position;
    };
    std::vector<CloudRecord> clouds;
    QXmlStreamReader reader(xml);
    while (!reader.atEnd()) {
        reader.readNext();
        if (!reader.isStartElement())
            continue;
        if (reader.name() == QStringLiteral("cloud")) {
            const QXmlStreamAttributes attributes = reader.attributes();
            clouds.push_back({attributes.value(QStringLiteral("uid")).toULongLong(),
                              {attributes.value(QStringLiteral("x")).toDouble(),
                               attributes.value(QStringLiteral("y")).toDouble()}});
            reader.skipCurrentElement();
            continue;
        }
        if (reader.name() != QStringLiteral("route") && reader.name() != QStringLiteral("flow-route"))
            continue;
        const bool flowRoute = reader.name() == QStringLiteral("flow-route");
        const QXmlStreamAttributes attributes = reader.attributes();
        const std::string flowName = attributes.value(QStringLiteral("name")).toString().toStdString();
        const core::ModelElement *flow = document.model().findByName(flowName);
        if (flowRoute && !flow) {
            reader.skipCurrentElement();
            continue;
        }
        const core::FlowEndpoints current = flowRoute
            ? static_cast<const core::Flow *>(flow)->endpoints
            : core::FlowEndpoints {};
        const core::EntityId from = workspaceEndpoint(document, attributes, QStringLiteral("from"), uids, current.from);
        const core::EntityId to = workspaceEndpoint(document, attributes, QStringLiteral("to"), uids, current.to);
        if (from == 0 || to == 0) {
            reader.skipCurrentElement();
            continue;
        }
        core::Route route = flowRoute ? document.diagram().route(flow->id()) : core::Route();
        route.controlPoints.clear();
        const QString mode = attributes.value(QStringLiteral("mode")).toString();
        if (mode == QStringLiteral("straight"))
            route.mode = core::RouteMode::Straight;
        else if (mode == QStringLiteral("bezier"))
            route.mode = core::RouteMode::Bezier;
        route.manual = attributes.value(QStringLiteral("manual")).toInt() != 0;
        route.locked = attributes.value(QStringLiteral("locked")).toInt() != 0;
        while (reader.readNext() != QXmlStreamReader::Invalid) {
            if (reader.isEndElement()
                && (reader.name() == QStringLiteral("route") || reader.name() == QStringLiteral("flow-route")))
                break;
            if (!reader.isStartElement() || reader.name() != QStringLiteral("pt"))
                continue;
            bool xOk = false;
            bool yOk = false;
            const double x = reader.attributes().value(QStringLiteral("x")).toDouble(&xOk);
            const double y = reader.attributes().value(QStringLiteral("y")).toDouble(&yOk);
            if (xOk && yOk)
                route.controlPoints.push_back({x, y});
        }
        if (flowRoute) {
            document.reconnectFlow(flow->id(), from, to);
            document.setRoute(flow->id(), route);
            continue;
        }
        core::EntityId id = 0;
        for (const core::ViewElement &element : document.diagram().elements()) {
            if (element.kind() != core::ElementKind::Connector)
                continue;
            const auto &connection = static_cast<const core::Connector &>(element);
            if (connection.from == from && connection.to == to) {
                id = connection.id();
                break;
            }
        }
        if (id == 0)
            id = document.createConnector(from, to);
        if (id != 0)
            document.setRoute(id, route);
    }
    for (const CloudRecord &cloud : clouds) {
        if (cloud.uid == 0)
            continue;
        const auto found = uids.find(cloud.uid);
        if (found != uids.end() && document.find(found->second)) {
            document.setPosition(found->second, cloud.position);
        } else {
            const core::EntityId id = document.createCloud(cloud.position);
            if (id != 0)
                uids[cloud.uid] = id;
        }
    }
}

} // namespace

bool saveXmile(const QString &fileName, const core::Document &model, QString *errorMessage)
{
    const QByteArray xml = writeXml(model);
    const QByteArray archive = makeZip(xml, writeWorkspace(model));
    if (archive.isEmpty()) {
        setError(errorMessage, translate("Could not create the XMILE ZIP container."));
        return false;
    }

    QSaveFile file(fileName);
    if (!file.open(QIODevice::WriteOnly)) {
        setError(errorMessage, translate("Could not open the file for writing: %1").arg(file.errorString()));
        return false;
    }
    if (file.write(archive) != archive.size() || !file.commit()) {
        setError(errorMessage, translate("Could not write the XMILE file: %1").arg(file.errorString()));
        return false;
    }
    return true;
}

bool loadXmile(const QString &fileName, core::Document &model, QString *errorMessage)
{
    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly)) {
        setError(errorMessage, translate("Could not open the XMILE file: %1").arg(file.errorString()));
        return false;
    }
    const QByteArray contents = file.readAll();
    if (file.error() != QFileDevice::NoError) {
        setError(errorMessage, translate("Could not read the XMILE file: %1").arg(file.errorString()));
        return false;
    }

    QByteArray xml;
    QByteArray workspace;
    if (contents.startsWith("PK")) {
        if (!readZip(contents, xml, workspace, errorMessage))
            return false;
    } else {
        xml = contents;
    }
    const VariableKindMap variableKinds = workspace.isEmpty() ? VariableKindMap {} : readWorkspaceVariableKinds(workspace);
    UidMap uids;
    if (!parseXml(xml, model, variableKinds, uids, errorMessage))
        return false;
    if (!workspace.isEmpty())
        applyWorkspace(model, workspace, uids);
    return true;
}

} // namespace qds::io
