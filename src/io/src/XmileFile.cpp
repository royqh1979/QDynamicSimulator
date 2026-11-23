#include <qds/io/XmileFile.h>

#include <QCoreApplication>
#include <QFile>
#include <QSaveFile>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>
#include <QtGlobal>
#include <zlib.h>

#include <algorithm>
#include <utility>
#include <vector>

namespace qds::io {
namespace {

constexpr quint32 localHeaderSignature = 0x04034b50;
constexpr quint32 centralHeaderSignature = 0x02014b50;
constexpr quint32 endSignature = 0x06054b50;
constexpr int maximumEntrySize = 64 * 1024 * 1024;

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

QByteArray makeZip(const QByteArray &xml)
{
    QByteArray compressed;
    QString ignoredError;
    if (!compressRaw(xml, compressed, &ignoredError))
        return {};

    const QByteArray name("model.xmile");
    const quint32 crc = checksum(xml);
    QByteArray output;
    append32(output, localHeaderSignature);
    append16(output, 20);
    append16(output, 0x0800);
    append16(output, 8);
    append16(output, 0);
    append16(output, 0);
    append32(output, crc);
    append32(output, static_cast<quint32>(compressed.size()));
    append32(output, static_cast<quint32>(xml.size()));
    append16(output, static_cast<quint16>(name.size()));
    append16(output, 0);
    output.append(name);
    output.append(compressed);

    const quint32 centralOffset = static_cast<quint32>(output.size());
    append32(output, centralHeaderSignature);
    append16(output, 20);
    append16(output, 20);
    append16(output, 0x0800);
    append16(output, 8);
    append16(output, 0);
    append16(output, 0);
    append32(output, crc);
    append32(output, static_cast<quint32>(compressed.size()));
    append32(output, static_cast<quint32>(xml.size()));
    append16(output, static_cast<quint16>(name.size()));
    append16(output, 0);
    append16(output, 0);
    append16(output, 0);
    append16(output, 0);
    append32(output, 0);
    append32(output, 0);
    output.append(name);

    const quint32 centralSize = static_cast<quint32>(output.size()) - centralOffset;
    append32(output, endSignature);
    append16(output, 0);
    append16(output, 0);
    append16(output, 1);
    append16(output, 1);
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

bool readZip(const QByteArray &archive, QByteArray &xml, QString *errorMessage)
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
        if (name.compare(QStringLiteral("model.xmile"), Qt::CaseInsensitive) == 0) {
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
    return extractEntry(archive, selected, xml, errorMessage);
}

QString variableTag(core::VariableType type)
{
    switch (type) {
    case core::VariableType::Stock:
        return QStringLiteral("stock");
    case core::VariableType::Flow:
        return QStringLiteral("flow");
    case core::VariableType::Auxiliary:
        return QStringLiteral("aux");
    case core::VariableType::Constant:
    case core::VariableType::Exogenous:
        return QStringLiteral("aux");
    case core::VariableType::Cloud:
        return QStringLiteral("cloud");
    case core::VariableType::Annotation:
        return QStringLiteral("text_box");
    }
    return {};
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
    writer.writeAttribute(QStringLiteral("version"), QStringLiteral("1.0"));

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
    for (const core::Variable &variable : model.variables()) {
        if (variable.type == core::VariableType::Cloud || variable.type == core::VariableType::Annotation)
            continue;
        const QString name = QString::fromStdString(variable.name);
        writer.writeStartElement(variableTag(variable.type));
        writer.writeAttribute(QStringLiteral("name"), name);
        writer.writeTextElement(
            QStringLiteral("eqn"),
            variable.equation.empty() ? QStringLiteral("0") : QString::fromStdString(variable.equation));
        if (!variable.unit.empty())
            writer.writeTextElement(QStringLiteral("units"), QString::fromStdString(variable.unit));
        if (!variable.comment.empty())
            writer.writeTextElement(QStringLiteral("doc"), QString::fromStdString(variable.comment));
        if (variable.type == core::VariableType::Stock) {
            for (const core::Connection &connection : model.connections()) {
                if (connection.type != core::ConnectionType::MaterialFlow)
                    continue;
                const core::Variable *from = model.variable(connection.from);
                const core::Variable *to = model.variable(connection.to);
                if (from && to && from->type == core::VariableType::Flow && connection.to == variable.id)
                    writer.writeTextElement(QStringLiteral("inflow"), QString::fromStdString(from->name));
                if (from && to && to->type == core::VariableType::Flow && connection.from == variable.id)
                    writer.writeTextElement(QStringLiteral("outflow"), QString::fromStdString(to->name));
            }
        }
        writer.writeEndElement();
    }
    writer.writeEndElement();

    writer.writeStartElement(QStringLiteral("views"));
    writer.writeStartElement(QStringLiteral("view"));
    writer.writeAttribute(QStringLiteral("type"), QStringLiteral("stock_flow"));
    for (const core::Variable &variable : model.variables()) {
        writer.writeStartElement(variableTag(variable.type));
        const core::Vec2 position = document.diagram().position(variable.id);
        writer.writeAttribute(QStringLiteral("name"), QString::fromStdString(variable.name));
        writer.writeAttribute(QStringLiteral("x"), QString::number(position.x, 'g', 15));
        writer.writeAttribute(QStringLiteral("y"), QString::number(position.y, 'g', 15));
        writer.writeEndElement();
    }
    for (const core::Connection &connection : model.connections()) {
        const core::Variable *from = model.variable(connection.from);
        const core::Variable *to = model.variable(connection.to);
        if (!from || !to)
            continue;
        writer.writeStartElement(QStringLiteral("connector"));
        writer.writeTextElement(QStringLiteral("from"), QString::fromStdString(from->name));
        writer.writeTextElement(QStringLiteral("to"), QString::fromStdString(to->name));
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
    core::ConnectionType type = core::ConnectionType::Information;
};

bool parseVariable(
    QXmlStreamReader &reader,
    core::Document &model,
    std::vector<PendingConnection> &pendingConnections,
    QString *errorMessage)
{
    const QString tag = reader.name().toString();
    const QString name = reader.attributes().value(QStringLiteral("name")).toString().trimmed();
    if (name.isEmpty()) {
        setError(errorMessage, translate("A model variable has no name."));
        return false;
    }

    core::Variable variable;
    variable.name = name.toStdString();
    if (tag == QStringLiteral("stock"))
        variable.type = core::VariableType::Stock;
    else if (tag == QStringLiteral("flow"))
        variable.type = core::VariableType::Flow;
    else
        variable.type = core::VariableType::Auxiliary;

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

    if (model.addVariable(variable) == 0) {
        setError(errorMessage, translate("The model contains a duplicate variable name: %1").arg(name));
        return false;
    }
    for (const QString &flow : inflows)
        pendingConnections.push_back({flow.toStdString(), variable.name, core::ConnectionType::MaterialFlow});
    for (const QString &flow : outflows)
        pendingConnections.push_back({variable.name, flow.toStdString(), core::ConnectionType::MaterialFlow});
    return true;
}

bool parseVariables(
    QXmlStreamReader &reader,
    core::Document &model,
    std::vector<PendingConnection> &pendingConnections,
    QString *errorMessage)
{
    while (reader.readNext() != QXmlStreamReader::Invalid) {
        if (reader.isEndElement() && reader.name() == QStringLiteral("variables"))
            return true;
        if (!reader.isStartElement())
            continue;
        const QString tag = reader.name().toString();
        if (tag == QStringLiteral("stock") || tag == QStringLiteral("flow") || tag == QStringLiteral("aux")) {
            if (!parseVariable(reader, model, pendingConnections, errorMessage))
                return false;
        } else {
            reader.skipCurrentElement();
        }
    }
    return true;
}

void parseConnector(QXmlStreamReader &reader, std::vector<PendingConnection> &pendingConnections)
{
    QString from = reader.attributes().value(QStringLiteral("from")).toString();
    QString to = reader.attributes().value(QStringLiteral("to")).toString();
    while (reader.readNext() != QXmlStreamReader::Invalid) {
        if (reader.isEndElement() && reader.name() == QStringLiteral("connector"))
            break;
        if (!reader.isStartElement())
            continue;
        if (reader.name() == QStringLiteral("from"))
            from = reader.readElementText().trimmed();
        else if (reader.name() == QStringLiteral("to"))
            to = reader.readElementText().trimmed();
        else
            reader.skipCurrentElement();
    }
    if (!from.isEmpty() && !to.isEmpty())
        pendingConnections.push_back({from.toStdString(), to.toStdString(), core::ConnectionType::Information});
}

void parseView(QXmlStreamReader &reader, core::Document &model, std::vector<PendingConnection> &pendingConnections)
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
            const core::Variable *variable = model.model().variableByName(name);
            if (!variable && (tag == QStringLiteral("cloud") || tag == QStringLiteral("text_box"))) {
                core::Variable created;
                created.name = name;
                created.type = tag == QStringLiteral("cloud") ? core::VariableType::Cloud : core::VariableType::Annotation;
                const core::EntityId id = model.addVariable(created);
                variable = model.variable(id);
            }
            bool xOk = false;
            bool yOk = false;
            const double x = attributes.value(QStringLiteral("x")).toDouble(&xOk);
            const double y = attributes.value(QStringLiteral("y")).toDouble(&yOk);
            if (variable && xOk && yOk)
                model.setPosition(variable->id, {x, y});
        } else if (tag == QStringLiteral("connector")) {
            parseConnector(reader, pendingConnections);
        }
    }
}

bool parseXml(const QByteArray &xml, core::Document &model, QString *errorMessage)
{
    QXmlStreamReader reader(xml);
    core::Document parsed;
    std::vector<PendingConnection> pendingConnections;
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
            if (!parseVariables(reader, parsed, pendingConnections, errorMessage))
                return false;
        } else if (tag == QStringLiteral("view") && !foundView) {
            foundView = true;
            parseView(reader, parsed, pendingConnections);
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

    for (const PendingConnection &connection : pendingConnections) {
        const core::Variable *from = parsed.model().variableByName(connection.from);
        const core::Variable *to = parsed.model().variableByName(connection.to);
        if (!from || !to)
            continue;
        core::ConnectionType type = connection.type;
        if (from->type == core::VariableType::Annotation || to->type == core::VariableType::Annotation)
            type = core::ConnectionType::Annotation;
        else if (type == core::ConnectionType::Information
            && (from->type == core::VariableType::Flow || to->type == core::VariableType::Flow)
            && (from->type == core::VariableType::Stock || from->type == core::VariableType::Cloud
                || to->type == core::VariableType::Stock || to->type == core::VariableType::Cloud))
            type = core::ConnectionType::MaterialFlow;
        parsed.createConnection(from->id, to->id, type);
    }

    if (!foundView) {
        const auto &variables = parsed.model().variables();
        for (std::size_t index = 0; index < variables.size(); ++index) {
            parsed.setPosition(
                variables[index].id,
                {100.0 + static_cast<int>(index % 5) * 180.0,
                 100.0 + static_cast<int>(index / 5) * 140.0});
        }
    }

    model = std::move(parsed);
    return true;
}

} // namespace

bool saveXmile(const QString &fileName, const core::Document &model, QString *errorMessage)
{
    const QByteArray xml = writeXml(model);
    const QByteArray archive = makeZip(xml);
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
    if (contents.startsWith("PK")) {
        if (!readZip(contents, xml, errorMessage))
            return false;
    } else {
        xml = contents;
    }
    return parseXml(xml, model, errorMessage);
}

} // namespace qds::io
