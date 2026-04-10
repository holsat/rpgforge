#ifndef LIBRARIANDATABASE_H
#define LIBRARIANDATABASE_H

#include <QObject>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QStringList>
#include <QVariantMap>

/**
 * @brief The LibrarianDatabase class manages a flexible EAV (Entity-Attribute-Value) 
 * SQLite database for game design data.
 */
class LibrarianDatabase : public QObject
{
    Q_OBJECT
public:
    explicit LibrarianDatabase(QObject *parent = nullptr);
    ~LibrarianDatabase();

    bool open(const QString &path);
    void close();

    // Entity Management
    qint64 addEntity(const QString &name, const QString &type, const QString &sourceFile);
    bool updateEntity(qint64 id, const QString &name, const QString &type);
    bool deleteEntity(qint64 id);

    // Attribute Management
    bool setAttribute(qint64 entityId, const QString &key, const QVariant &value);
    QVariant getAttribute(qint64 entityId, const QString &key) const;
    QVariantMap getAttributes(qint64 entityId) const;

    // Dependency Graph
    bool addReference(qint64 entityId, const QString &referencingFile);
    QStringList getReferences(qint64 entityId) const;

    // Querying
    QList<qint64> findEntitiesByType(const QString &type) const;
    QList<qint64> findEntitiesByAttribute(const QString &key, const QVariant &value) const;

    QSqlDatabase database() const { return m_db; }
    QString lastError() const { return m_lastError; }

private:
    bool initSchema();

    QSqlDatabase m_db;
    QString m_lastError;
};

#endif // LIBRARIANDATABASE_H
