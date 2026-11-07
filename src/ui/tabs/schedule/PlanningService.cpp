#include "ui/tabs/schedule/PlanningService.h"

bool PlanningService::validate(const PlanEntry&, QString* error) const
{
    if (error) *error = {};
    return true;
}
