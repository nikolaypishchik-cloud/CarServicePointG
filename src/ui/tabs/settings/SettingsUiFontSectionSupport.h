#pragma once

#include <functional>

class QWidget;

namespace SettingsUiFontSectionSupport {

QWidget* build(QWidget* owner,
               const std::function<void()>& onBack,
               const std::function<void()>& onRefreshAll);

} // namespace SettingsUiFontSectionSupport
