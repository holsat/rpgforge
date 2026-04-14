#include "librariandatabase.h"
#include <QDebug>
#include <QDateTime>
#include <QUuid>
#include <QSqlError>

LibrarianDatabase::LibrarianDatabase(QObject *parent)
    : QObject(parent)
{
}

LibrarianDatabase::~LibrarianDatabase()
{
    close();
}

bool LibrarianDatabase::open(const QString &path)
{
    if (m_db.isOpen()) {
        m_db.close();
    }

    // Use a unique connection name to avoid collisions
    QString connectionName = QStringLiteral("LibrarianConnection_%1").arg(QUuid::createUuid().toString());
    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
    m_db.setDatabaseName(path);

    if (!m_db.open()) {
        m_lastError = m_db.lastError().text();
        return false;
    }

    return initSchema();
}

void LibrarianDatabase::close()
{
    if (m_db.isOpen()) {
        QString connectionName = m_db.connectionName();
        m_db.close();
        m_db = QSqlDatabase();
        QSqlDatabase::removeDatabase(connectionName);
    }
}

bool LibrarianDatabase::beginTransaction()
{
    return m_db.transaction();
}

bool LibrarianDatabase::commit()
{
    return m_db.commit();
}

bool LibrarianDatabase::initSchema()
{
    QSqlQuery query(m_db);

    // Entities table
    if (!query.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS entities ("
                    "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                    "name TEXT NOT NULL, "
                    "type TEXT, "
                    "source_file TEXT, "
                    "last_modified DATETIME)"))) {
        m_lastError = query.lastError().text();
        return false;
    }

    // Attributes table (EAV)
    if (!query.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS attributes ("
                    "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                    "entity_id INTEGER, "
                    "key TEXT NOT NULL, "
                    "value TEXT, "
                    "datatype TEXT, "
                    "FOREIGN KEY(entity_id) REFERENCES entities(id) ON DELETE CASCADE)"))) {
        m_lastError = query.lastError().text();
        return false;
    }

    // Dependency Graph / References table
    if (!query.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS references_graph ("
                    "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                    "entity_id INTEGER, "
                    "referencing_file TEXT, "
                    "FOREIGN KEY(entity_id) REFERENCES entities(id) ON DELETE CASCADE)"))) {
        m_lastError = query.lastError().text();
        return false;
    }

    // Indexes for performance
    query.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_entities_name ON entities(name)"));
    query.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_attributes_entity ON attributes(entity_id)"));
    query.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_attributes_key ON attributes(key)"));

    return true;
}

qint64 LibrarianDatabase::addEntity(const QString &name, const QString &type, const QString &sourceFile)
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("INSERT INTO entities (name, type, source_file, last_modified) "
                  "VALUES (:name, :type, :source_file, :last_modified)"));
    query.bindValue(QStringLiteral(":name"), name);
    query.bindValue(QStringLiteral(":type"), type);
    query.bindValue(QStringLiteral(":source_file"), sourceFile);
    query.bindValue(QStringLiteral(":last_modified"), QDateTime::currentDateTime().toString(Qt::ISODate));

    if (!query.exec()) {
        m_lastError = query.lastError().text();
        return -1;
    }

    return query.lastInsertId().toLongLong();
}

bool LibrarianDatabase::updateEntity(qint64 id, const QString &name, const QString &type)
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("UPDATE entities SET name = :name, type = :type, last_modified = :last_modified WHERE id = :id"));
    query.bindValue(QStringLiteral(":name"), name);
    query.bindValue(QStringLiteral(":type"), type);
    query.bindValue(QStringLiteral(":last_modified"), QDateTime::currentDateTime().toString(Qt::ISODate));
    query.bindValue(QStringLiteral(":id"), id);

    if (!query.exec()) {
        m_lastError = query.lastError().text();
        return false;
    }
    return true;
}

bool LibrarianDatabase::deleteEntity(qint64 id)
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("DELETE FROM entities WHERE id = :id"));
    query.bindValue(QStringLiteral(":id"), id);

    if (!query.exec()) {
        m_lastError = query.lastError().text();
        return false;
    }
    return true;
}

bool LibrarianDatabase::setAttribute(qint64 entityId, const QString &key, const QVariant &value)
{
    // First, check if attribute already exists
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("SELECT id FROM attributes WHERE entity_id = :entity_id AND key = :key"));
    query.bindValue(QStringLiteral(":entity_id"), entityId);
    query.bindValue(QStringLiteral(":key"), key);

    if (query.exec() && query.next()) {
        // Update existing
        qint64 attrId = query.value(0).toLongLong();
        query.prepare(QStringLiteral("UPDATE attributes SET value = :value, datatype = :datatype WHERE id = :id"));
        query.bindValue(QStringLiteral(":value"), value.toString());
        query.bindValue(QStringLiteral(":datatype"), QString::fromLatin1(value.typeName()));
        query.bindValue(QStringLiteral(":id"), attrId);
    } else {
        // Insert new
        query.prepare(QStringLiteral("INSERT INTO attributes (entity_id, key, value, datatype) "
                      "VALUES (:entity_id, :key, :value, :datatype)"));
        query.bindValue(QStringLiteral(":entity_id"), entityId);
        query.bindValue(QStringLiteral(":key"), key);
        query.bindValue(QStringLiteral(":value"), value.toString());
        query.bindValue(QStringLiteral(":datatype"), QString::fromLatin1(value.typeName()));
    }

    if (!query.exec()) {
        m_lastError = query.lastError().text();
        return false;
    }
    return true;
}

QVariant LibrarianDatabase::getAttribute(qint64 entityId, const QString &key) const
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("SELECT value, datatype FROM attributes WHERE entity_id = :entity_id AND key = :key"));
    query.bindValue(QStringLiteral(":entity_id"), entityId);
    query.bindValue(QStringLiteral(":key"), key);

    if (query.exec() && query.next()) {
        QString valStr = query.value(0).toString();
        QString typeName = query.value(1).toString();
        
        QVariant val(valStr);
        if (val.canConvert(QMetaType::fromName(typeName.toUtf8()))) {
            val.convert(QMetaType::fromName(typeName.toUtf8()));
        }
        return val;
    }

    return QVariant();
}

QVariantMap LibrarianDatabase::getAttributes(qint64 entityId) const
{
    QVariantMap attrs;
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("SELECT key, value, datatype FROM attributes WHERE entity_id = :entity_id"));
    query.bindValue(QStringLiteral(":entity_id"), entityId);

    if (query.exec()) {
        while (query.next()) {
            QString key = query.value(0).toString();
            QString valStr = query.value(1).toString();
            QString typeName = query.value(2).toString();

            QVariant val(valStr);
            if (val.canConvert(QMetaType::fromName(typeName.toUtf8()))) {
                val.convert(QMetaType::fromName(typeName.toUtf8()));
            }
            attrs.insert(key, val);
        }
    } else {
        qWarning() << "Failed to fetch attributes for entity" << entityId << ":" << query.lastError().text();
    }

    return attrs;
}

bool LibrarianDatabase::addReference(qint64 entityId, const QString &referencingFile)
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("INSERT INTO references_graph (entity_id, referencing_file) VALUES (:entity_id, :file)"));
    query.bindValue(QStringLiteral(":entity_id"), entityId);
    query.bindValue(QStringLiteral(":file"), referencingFile);

    if (!query.exec()) {
        m_lastError = query.lastError().text();
        return false;
    }
    return true;
}

QStringList LibrarianDatabase::getReferences(qint64 entityId) const
{
    QStringList refs;
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("SELECT referencing_file FROM references_graph WHERE entity_id = :entity_id"));
    query.bindValue(QStringLiteral(":entity_id"), entityId);

    if (query.exec()) {
        while (query.next()) {
            refs << query.value(0).toString();
        }
    }
    return refs;
}

QList<qint64> LibrarianDatabase::findEntitiesByType(const QString &type) const
{
    QList<qint64> ids;
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("SELECT id FROM entities WHERE type = :type"));
    query.bindValue(QStringLiteral(":type"), type);

    if (query.exec()) {
        while (query.next()) {
            ids << query.value(0).toLongLong();
        }
    }
    return ids;
}

QList<qint64> LibrarianDatabase::findEntitiesByAttribute(const QString &key, const QVariant &value) const
{
    QList<qint64> ids;
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("SELECT entity_id FROM attributes WHERE key = :key AND value = :value"));
    query.bindValue(QStringLiteral(":key"), key);
    query.bindValue(QStringLiteral(":value"), value.toString());

    if (query.exec()) {
        while (query.next()) {
            ids << query.value(0).toLongLong();
        }
    }
    return ids;
}
