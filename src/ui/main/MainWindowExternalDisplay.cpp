#include "ui/main/MainWindowModulePrelude.h"
#include "ui/main/ExternalDisplayController.h"
#include <type_traits>

QWidget* MainWindow::externalDisplaySettings(QWidget* parent) {
    if (!m_externalDisplays) {
        m_externalDisplays = new ExternalDisplayController(this, [this](int id, QWidget* host) {
            ExternalDisplayController::Page result;
            auto assign = [&](auto* page) {
                result.widget = page;
                QPointer<std::remove_pointer_t<decltype(page)>> guard(page);
                result.refresh = [guard] {
                    if (!guard) return;
                    using T = std::remove_pointer_t<decltype(page)>;
                    if constexpr (std::is_same_v<T, CurrentOrdersPage> || std::is_same_v<T, HistoryPage>
                        || std::is_same_v<T, PartsPage> || std::is_same_v<T, ReportsPage>) guard->markDirty();
                    guard->refresh();
                };
                connectPageSignals(page);
            };
            switch (id) {
            case 0: assign(new PlanningPage(host)); break;
            case 100: {
                auto* page = new PlanningPage(host); page->setDailyView(true); assign(page); break;
            }
            case 1: assign(new CurrentOrdersPage(host)); break;
            case 3: assign(new HistoryPage(host)); break;
            case 4: assign(new PartsPage(host)); break;
            case 5: assign(new ReportsPage(host)); break;
            case 6: assign(new StatsPage(host)); break;
            case 13: assign(new WorkshopInventoryPage(host)); break;
            }
            return result;
        }, [this](int id) { return canAccessPage(id == 100 ? 0 : id); });
    }
    return m_externalDisplays->settingsWidget(parent);
}
