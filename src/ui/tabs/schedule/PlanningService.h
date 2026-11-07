#pragma once
#include <QObject>
#include "core/model/PlanEntry.h"

// Business rules (stub): reminders, validation, etc.
class PlanningService : public QObject {
    Q_OBJECT
public:
    explicit PlanningService(QObject* parent=nullptr) : QObject(parent) {}
    bool validate(const PlanEntry& e, QString* error=nullptr) const;
};
