#pragma once

#include "core/readmodel/TabularReadModel.h"

class TabularSnapshotTableModel;

namespace TabularReadModelMapper {

// P1/pass9 snapshot migration closure: CurrentOrders/History/Parts use QAbstractTableModel snapshots only.
// The legacy legacy item-model mapper was removed to prevent accidental row-by-row item allocation in hot paths.
void apply(TabularSnapshotTableModel* model, const TabularReadModel& readModel);

} // namespace TabularReadModelMapper
