#pragma once

#include <QString>
#include <QStringList>
#include <QList>
#include <QObject>
#include <QtGlobal>

class QSqlDatabase;

namespace FinanceSettingsService {

class MarkupSettingsBroadcaster final : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;

public slots:
    void broadcastMarkupSettingsChanged();

signals:
    void markupSettingsChanged();
};

enum class MarkupRecalculationMode {
    NoRecalculate,
    NewRowsOnly,
    OpenOrdersOnly,
    AllOrders
};

struct OilMarkupRule {
    QString store;
    QString brand;
    QString oilGrade;
    double pct = 0.0;
};

struct MarkupSnapshot {
    QString storeMarkupsJson;
    QString oilStoreMarkupsJson;
    double defaultPartsMarkupPercent = 0.0;
    double oilDefault = 0.0;
};

struct MarkupSaveRequest {
    QString storeMarkupsJson;
    QString oilStoreMarkupsJson;
    double defaultPartsMarkupPercent = 0.0;
    double oilDefault = 0.0;
    MarkupRecalculationMode recalculationMode = MarkupRecalculationMode::AllOrders;
};

MarkupSnapshot loadMarkupSnapshot();

bool saveMarkupSettings(const MarkupSaveRequest& request,
                        bool* changed = nullptr,
                        QString* errorText = nullptr);

bool saveMarkupSettings(QSqlDatabase db,
                        const MarkupSaveRequest& request,
                        bool* changed = nullptr,
                        QString* errorText = nullptr);

QStringList loadOilGradeChoices();
QList<OilMarkupRule> loadOilMarkupRules(const QString& raw);

quint64 markupSettingsRevision();
void notifyMarkupSettingsChanged();
MarkupSettingsBroadcaster* markupSettingsBroadcaster();

} // namespace FinanceSettingsService
