#pragma once

#include <QDate>
#include <QMap>
#include <QString>

#include <functional>

#include "core/services/ReportsDataLoader.h"

class QObject;
class QTableView;
namespace ReportsTableSupport { class ReportsSnapshotTableModel; }
class AsyncTaskHandle;

namespace ReportsAsyncSupport {

bool isAsyncTab(int idx);
QString buildTabKey(int idx, const QDate& from, const QDate& to);
void showLoadingRow(QTableView* table, ReportsTableSupport::ReportsSnapshotTableModel* model, const QString& text);

void startPeriodLoad(QObject* owner,
                     const AsyncTaskHandle& asyncHandle,
                     const QString& dbPath,
                     const QString& df,
                     const QString& dt,
                     std::function<void(const PeriodSnapshot&)> onReady);

void startOrdersLoad(QObject* owner,
                     const AsyncTaskHandle& asyncHandle,
                     const QString& dbPath,
                     const QString& df,
                     const QString& dt,
                     std::function<void(const OrdersSnapshot&)> onReady,
                     const QString& filter = QString());

void startPartsLoad(QObject* owner,
                    const AsyncTaskHandle& asyncHandle,
                    const QString& dbPath,
                    const QString& df,
                    const QString& dt,
                    double defaultParts,
                    double defaultOil,
                    const QMap<QString,double>& partsMap,
                    const QMap<QString,double>& oilMap,
                    std::function<void(const PartsSnapshot&)> onReady);

void startOutsourceLoad(QObject* owner,
                        const AsyncTaskHandle& asyncHandle,
                        const QString& dbPath,
                        const QString& df,
                        const QString& dt,
                        std::function<void(const OutsourceSnapshot&)> onReady);

void startExpensesLoad(QObject* owner,
                       const AsyncTaskHandle& asyncHandle,
                       const QString& dbPath,
                       const QString& df,
                       const QString& dt,
                       std::function<void(const ExpensesSnapshot&)> onReady);

void startTotalExpensesLoad(QObject* owner,
                            const AsyncTaskHandle& asyncHandle,
                            const QString& dbPath,
                            const QString& df,
                            const QString& dt,
                            std::function<void(const TotalExpensesSnapshot&)> onReady);

void startPayrollLoad(QObject* owner,
                      const AsyncTaskHandle& asyncHandle,
                      const QString& dbPath,
                      const QString& df,
                      const QString& dt,
                      std::function<void(const PayrollSnapshot&)> onReady);

} // namespace ReportsAsyncSupport
