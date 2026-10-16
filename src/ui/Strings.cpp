#include "ui/Strings.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDebug>

static void initializeStringsResource() { Q_INIT_RESOURCE(strings); }

namespace w11widget {
namespace {
const QJsonObject& catalog() {
    static const QJsonObject strings = [] {
        initializeStringsResource();
        QFile file(QStringLiteral(":/i18n/tr.json"));
        if (!file.open(QIODevice::ReadOnly)) qFatal("Source locale is missing.");
        QJsonParseError error;
        const auto document = QJsonDocument::fromJson(file.readAll(), &error);
        if (error.error != QJsonParseError::NoError || !document.isObject())
            qFatal("Source locale is invalid.");
        return document.object();
    }();
    return strings;
}
QString lookup(const QString& key) {
    const auto value = catalog().value(key);
    if (!value.isString()) {
        qWarning() << "Missing source locale key:" << key;
        return QStringLiteral("[") + key + QLatin1Char(']');
    }
    return value.toString();
}
}
QString uiText(const char* key) { return lookup(QString::fromLatin1(key)); }
QString Strings::text(const QString& key) const { return lookup(key); }
}
