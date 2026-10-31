// SPDX-License-Identifier: GPL-2.0-or-later
#include "addon_panel_icon.hpp"
#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

namespace vlc::addons::panelIcon {
namespace {
const QSet<QString>& shapes()
{
    static const QSet<QString> value{QStringLiteral("g"), QStringLiteral("path"), QStringLiteral("circle"), QStringLiteral("ellipse"),
        QStringLiteral("rect"), QStringLiteral("line"), QStringLiteral("polyline"), QStringLiteral("polygon")};
    return value;
}
// Each attribute with the characters its value can use. Paint attributes
// are rewritten, so their text is never copied.
const QHash<QString, QRegularExpression>& attributes()
{
    static const QRegularExpression number(QStringLiteral("^-?(?:\\d+(?:\\.\\d*)?|\\.\\d+)(?:e-?\\d+)?\\z"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression geometry(QStringLiteral("^[0-9A-Za-z.,+\\- ]{0,8192}\\z"));
    static const QRegularExpression transform(QStringLiteral("^(?:\\s*(?:matrix|translate|scale|rotate|skewX|skewY)\\([0-9.,+\\-e ]{1,128}\\))*\\s*\\z"));
    static const QRegularExpression keyword(QStringLiteral("^(?:butt|round|square|miter|bevel|nonzero|evenodd)\\z"));
    static const QHash<QString, QRegularExpression> value{
        {QStringLiteral("d"), geometry}, {QStringLiteral("points"), geometry}, {QStringLiteral("transform"), transform},
        {QStringLiteral("cx"), number}, {QStringLiteral("cy"), number}, {QStringLiteral("r"), number},
        {QStringLiteral("rx"), number}, {QStringLiteral("ry"), number}, {QStringLiteral("x"), number}, {QStringLiteral("y"), number},
        {QStringLiteral("width"), number}, {QStringLiteral("height"), number}, {QStringLiteral("x1"), number}, {QStringLiteral("y1"), number},
        {QStringLiteral("x2"), number}, {QStringLiteral("y2"), number}, {QStringLiteral("stroke-width"), number},
        {QStringLiteral("stroke-miterlimit"), number}, {QStringLiteral("opacity"), number}, {QStringLiteral("fill-opacity"), number},
        {QStringLiteral("stroke-opacity"), number}, {QStringLiteral("stroke-linecap"), keyword}, {QStringLiteral("stroke-linejoin"), keyword},
        {QStringLiteral("fill-rule"), keyword}, {QStringLiteral("clip-rule"), keyword}};
    return value;
}
bool paint(const QString& name) { return name == QLatin1String("fill") || name == QLatin1String("stroke"); }
// Copy one element's attributes, or refuse the icon.
bool copy(const QXmlStreamAttributes& input, QXmlStreamWriter& output, bool root)
{
    for (const auto& attribute : input) {
        const auto name = attribute.qualifiedName().toString(), value = attribute.value().toString().trimmed();
        if (!attribute.namespaceUri().isEmpty() && name != QLatin1String("xmlns")) return false;
        if (root && (name == QLatin1String("xmlns") || name == QLatin1String("viewBox") || name == QLatin1String("version")
                     || name == QLatin1String("width") || name == QLatin1String("height")))
            continue;
        if (name == QLatin1String("id")) continue;
        if (paint(name)) {
            // One color: every paint that is not "none" becomes the theme color.
            output.writeAttribute(name, value == QLatin1String("none") ? QStringLiteral("none") : QStringLiteral("COLOR"));
            continue;
        }
        const auto rule = attributes().constFind(name);
        if (rule == attributes().cend() || !rule->match(value).hasMatch()) return false;
        output.writeAttribute(name, value);
    }
    return true;
}
}

QString markup(const QByteArray& svg)
{
    if (svg.isEmpty() || svg.size() > MaximumBytes) return {};
    QXmlStreamReader reader(svg);
    QString result;
    QXmlStreamWriter writer(&result);
    int depth = 0, elements = 0;
    bool rootSeen = false, rootGroup = false;
    while (!reader.atEnd()) {
        switch (reader.readNext()) {
        case QXmlStreamReader::StartDocument: case QXmlStreamReader::Comment: case QXmlStreamReader::EndDocument: break;
        case QXmlStreamReader::Characters:
            if (!reader.isWhitespace()) return {};
            break;
        case QXmlStreamReader::StartElement: {
            const auto name = reader.name().toString();
            if (++elements > 256 || ++depth > 8) return {};
            if (!rootSeen) {
                if (name != QLatin1String("svg") || reader.namespaceUri() != QLatin1String("http://www.w3.org/2000/svg")) return {};
                const auto box = reader.attributes().value(QStringLiteral("viewBox")).toString().simplified();
                if (box != QLatin1String("0 0 24 24")) return {};
                rootSeen = true;
                // Paint and stroke settings on the root apply to the shapes.
                QXmlStreamAttributes inherited;
                for (const auto& attribute : reader.attributes())
                    if (attribute.qualifiedName() != QLatin1String("xmlns") && attribute.qualifiedName() != QLatin1String("viewBox")
                        && attribute.qualifiedName() != QLatin1String("version") && attribute.qualifiedName() != QLatin1String("width")
                        && attribute.qualifiedName() != QLatin1String("height") && attribute.qualifiedName() != QLatin1String("id"))
                        inherited.append(attribute);
                if (!inherited.isEmpty()) {
                    writer.writeStartElement(QStringLiteral("g"));
                    if (!copy(inherited, writer, false)) return {};
                    rootGroup = true;
                }
                break;
            }
            if (reader.namespaceUri() != QLatin1String("http://www.w3.org/2000/svg") || !shapes().contains(name)) return {};
            writer.writeStartElement(name);
            if (!copy(reader.attributes(), writer, false)) return {};
            break;
        }
        case QXmlStreamReader::EndElement:
            --depth;
            if (depth == 0) { if (rootGroup) writer.writeEndElement(); }
            else writer.writeEndElement();
            break;
        default: return {}; // DTD, entity reference, processing instruction
        }
    }
    if (reader.hasError() || !rootSeen || depth != 0 || elements < 2) return {};
    return result;
}
}
