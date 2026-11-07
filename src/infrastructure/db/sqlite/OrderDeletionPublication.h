#pragma once
#include <QSqlDatabase>
#include <QString>
#include <QHash>
#include <QPair>

namespace OrderDeletionPublication
{
class Retirements
{
  public:
    bool load(QSqlDatabase workspace, QString *error);
    bool classify(int internal, const QString &uid, int year, int visible, bool *retired, QString *error) const;
    bool removeFromRebuiltCache(QSqlDatabase workspace, QString *error) const;

  private:
    struct Identity
    {
        QString uid;
        int year;
        int visible;
    };
    QHash<int, Identity> byInternal;
    QHash<QString, int> byUid;
    QHash<QPair<int, int>, int> byAnnual;
};
bool validatePartLocators(int number, const QString &uid, QString *error);
bool record(QSqlDatabase source, int number, QString *error);
bool replay(QSqlDatabase source, QString *error);
bool alreadyDeleted(QSqlDatabase source, int number, QString *error);
bool checkOpenAllowed(QSqlDatabase source, int number, const QString &uid, QString *error);
} // namespace OrderDeletionPublication
