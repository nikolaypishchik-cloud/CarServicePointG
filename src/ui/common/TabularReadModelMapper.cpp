#include "ui/common/TabularReadModelMapper.h"

#include "ui/common/TabularSnapshotTableModel.h"

namespace TabularReadModelMapper {

void apply(TabularSnapshotTableModel* model, const TabularReadModel& readModel)
{
    if (!model) return;
    // P1/pass9 snapshot migration closure: apply snapshots by model reset only.
    // No legacy item allocation is allowed in current/history/parts hot paths.
    model->setSnapshot(readModel);
}

} // namespace TabularReadModelMapper
