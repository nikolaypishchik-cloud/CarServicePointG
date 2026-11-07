#pragma once

#include <functional>
#include <QString>

class QWidget;
struct RecoveryUiRestoreRequest;

struct RecoveryUiRestoreResult
{
    bool handled = false;
    bool restored = false;
    bool conflict = false;
    QString warning;
    QString error;
};

enum class RecoveryUiTarget
{
    None = 0,
    OrderEditor,
    WorkItemsEditor,
};

using RecoveryUiRestoreHandler = std::function<RecoveryUiRestoreResult(const RecoveryUiRestoreRequest&, QWidget*)>;
