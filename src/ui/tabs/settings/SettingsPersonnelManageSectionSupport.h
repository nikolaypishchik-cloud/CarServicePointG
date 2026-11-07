#pragma once

#include <functional>

class QWidget;

namespace SettingsPersonnelManageSectionSupport {

QWidget* build(QWidget* parent, const std::function<void()>& queueRefreshAll);

} // namespace SettingsPersonnelManageSectionSupport
