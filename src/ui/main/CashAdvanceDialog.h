#pragma once
#include <QString>
class QWidget;
class CashJournalPage;
void openOutflowDialog(CashJournalPage* page,QString kind,QString uid,QString label,qint64 fullCents=0);
void openCashShiftDialog(CashJournalPage* page);
void openCashOpeningDialog(CashJournalPage* page);
void openCashAdvanceDialog(CashJournalPage* page, bool allocate);
