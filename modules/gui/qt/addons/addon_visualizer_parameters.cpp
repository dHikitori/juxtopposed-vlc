// SPDX-License-Identifier: GPL-2.0-or-later
#include "addon_visualizer_parameters.hpp"
#include <QJsonArray>
#include <QJsonDocument>
#include <QMap>
#include <QRegularExpression>
#include <QSet>
#include <cmath>
#include <functional>

namespace vlc::addons {
namespace {
struct Range { double minimum, maximum; bool integer = false; };
struct Target { QJsonObject node; QList<int> path; };
bool fail(QString* error)
{
    if (error) *error = QStringLiteral("The visualizer parameters are not correct.");
    return false;
}
bool identifier(const QString& id)
{
    static const QRegularExpression pattern(QStringLiteral("^[A-Za-z_][A-Za-z0-9_-]{0,63}$"));
    return pattern.match(id).hasMatch() && id != QLatin1String("__proto__")
        && id != QLatin1String("prototype") && id != QLatin1String("constructor");
}
bool finite(const QJsonValue& value)
{ return value.isDouble() && std::isfinite(value.toDouble()); }
bool within(const QJsonValue& value, const Range& range)
{
    return finite(value) && value.toDouble() >= range.minimum && value.toDouble() <= range.maximum
        && (!range.integer || std::floor(value.toDouble()) == value.toDouble());
}
bool propertyRange(const QString& type, const QString& field, Range* range)
{
    if (field == QLatin1String("opacity")) *range = {0, 1};
    else if ((type == QLatin1String("spectrum") || type == QLatin1String("waveform") || type == QLatin1String("spectrogram")) && field == QLatin1String("gain")) *range = {0, 8};
    else if (type == QLatin1String("spectrum") && field == QLatin1String("count")) *range = {1, 64, true};
    else if (type == QLatin1String("waveform") && field == QLatin1String("points")) *range = {2, 256, true};
    else if (type == QLatin1String("waveform") && field == QLatin1String("lineWidth")) *range = {0.1, 8};
    else if (type == QLatin1String("particles") && field == QLatin1String("count")) *range = {1, 2048, true};
    else if (type == QLatin1String("particles") && field == QLatin1String("seed")) *range = {0, 1000000, true};
    else if (type == QLatin1String("particles") && field == QLatin1String("speed")) *range = {0, 2};
    else if (type == QLatin1String("particles") && field == QLatin1String("size")) *range = {0.001, 0.1};
    else if (type == QLatin1String("group") && field == QLatin1String("rotation")) *range = {-360, 360};
    else if (type == QLatin1String("group") && field == QLatin1String("scale")) *range = {0.1, 4};
    else if (type == QLatin1String("feedback") && field == QLatin1String("decay")) *range = {0, 0.99};
    else if (type == QLatin1String("feedback") && field == QLatin1String("scale")) *range = {0.5, 1.5};
    else return false;
    return true;
}
bool targets(const QJsonArray& nodes, QMap<QString, Target>* result, int* count,
             int* particles, QList<int> path = {})
{
    if (path.size() > 8) return false;
    for (int index = 0; index < nodes.size(); ++index) {
        if (!nodes[index].isObject() || ++*count > 256) return false;
        const auto node = nodes[index].toObject(); auto current = path; current.append(index);
        if (node.contains("id")) {
            const auto id = node.value("id").toString();
            if (!node.value("id").isString() || !identifier(id) || result->contains(id)) return false;
            result->insert(id, {node, current});
        }
        if (node.value("type").toString() == QLatin1String("particles")) {
            if (!within(node.value("count"), {1, 2048, true})) return false;
            *particles += node.value("count").toInt(); if (*particles > 2048) return false;
        }
        if (node.value("type").toString() == QLatin1String("group")) {
            if (!node.value("children").isArray() || !targets(node.value("children").toArray(), result, count, particles, current)) return false;
        }
    }
    return true;
}
bool declarations(const QJsonObject& scene, QMap<QString, Target>* indexed, QString* error)
{
    int count = 0, particles = 0;
    if (!scene.value("nodes").isArray() || !targets(scene.value("nodes").toArray(), indexed, &count, &particles)) return fail(error);
    if (scene.contains("parameters") && !scene.value("parameters").isObject()) return fail(error);
    const auto parameters = scene.value("parameters").toObject();
    if (parameters.size() > 64) return fail(error);
    QSet<QString> used;
    for (auto it = parameters.begin(); it != parameters.end(); ++it) {
        const auto entry = it.value().toObject(); const auto node = entry.value("node").toString(), field = entry.value("property").toString();
        if (!identifier(it.key()) || !it.value().isObject() || entry.size() != 4 || !entry.value("node").isString()
            || !entry.value("property").isString() || !indexed->contains(node) || used.contains(node + '/' + field)) return fail(error);
        Range allowed;
        const auto target = indexed->value(node).node;
        if (!propertyRange(target.value("type").toString(), field, &allowed) || !within(entry.value("minimum"), allowed)
            || !within(entry.value("maximum"), allowed) || entry.value("minimum").toDouble() > entry.value("maximum").toDouble()) return fail(error);
        const Range declared{entry.value("minimum").toDouble(), entry.value("maximum").toDouble(), allowed.integer};
        if (!within(target.value(field), declared)) return fail(error);
        used.insert(node + '/' + field);
    }
    return true;
}
QJsonArray replace(QJsonArray nodes, const QList<int>& path, qsizetype depth, const QString& field, const QJsonValue& value)
{
    const auto index = path[depth]; auto node = nodes[index].toObject();
    if (depth + 1 == path.size()) node.insert(field, value);
    else node.insert("children", replace(node.value("children").toArray(), path, depth + 1, field, value));
    nodes[index] = node; return nodes;
}
}
bool AddonVisualizerParameters::validate(const QJsonObject& scene, QString* error)
{
    if (error) error->clear(); QMap<QString, Target> indexed;
    return declarations(scene, &indexed, error);
}
bool AddonVisualizerParameters::apply(const QJsonObject& scene, const QJsonObject& values, QJsonObject* output, QString* error)
{
    if (error) error->clear(); QMap<QString, Target> indexed;
    if (!output || values.size() > 64 || QJsonDocument(values).toJson(QJsonDocument::Compact).size() > 16384
        || !declarations(scene, &indexed, error)) return fail(error);
    const auto parameters = scene.value("parameters").toObject(); auto result = scene;
    for (auto it = values.begin(); it != values.end(); ++it) {
        if (!parameters.contains(it.key())) return fail(error);
        const auto entry = parameters.value(it.key()).toObject(); const auto id = entry.value("node").toString(), field = entry.value("property").toString();
        Range allowed; if (!propertyRange(indexed.value(id).node.value("type").toString(), field, &allowed)) return fail(error);
        const Range declared{entry.value("minimum").toDouble(), entry.value("maximum").toDouble(), allowed.integer};
        if (!within(it.value(), declared)) return fail(error);
        result.insert("nodes", replace(result.value("nodes").toArray(), indexed.value(id).path, 0, field, it.value()));
    }
    QMap<QString, Target> changed;
    if (!declarations(result, &changed, error)) return false;
    *output = std::move(result); return true;
}
}
