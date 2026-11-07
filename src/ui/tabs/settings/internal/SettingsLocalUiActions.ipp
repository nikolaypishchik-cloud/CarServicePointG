// Extracted from src/ui/tabs/settings/SettingsPageLocalSupport.cpp for P1/P2 monolith reduction.
// Included exactly once by the original translation unit/header; do not compile separately.


// Continued after settingsRightQss() definition; the full raw QSS literal is kept in SettingsLocalBackupRestore.ipp
// to avoid MSVC C3516/C1057 when a raw string spans an included implementation fragment.

}

static QString settingsItemsPageQss()
{
    return settingsLeftQss();
}

static QString settingsNavButtonQss(bool submenu = false)
{
    const QString leftPad = submenu ? QStringLiteral("18px") : QStringLiteral("16px");
    if (isDarkUi()) {
        return QString::fromUtf8(R"QSS(
            QPushButton {
                background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                            stop:0 rgba(214,224,236,0.84),
                            stop:0.12 rgba(152,167,184,0.94),
                            stop:0.46 rgba(86,101,120,0.99),
                            stop:0.50 rgba(34,46,61,1.0),
                            stop:0.51 rgba(10,19,32,1.0),
                            stop:1 rgba(5,12,24,1.0));
                border: 1px solid rgba(255,255,255,0.88);
                border-top: 1px solid rgba(255,255,255,0.98);
                border-radius: 16px;
                color: #f8fbff;
                font-weight: 800;
                padding-left: %1;
                padding-right: 12px;
                text-align: left;
            }
            QPushButton:hover {
                background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                            stop:0 rgba(228,236,246,0.92),
                            stop:0.14 rgba(170,184,198,0.98),
                            stop:0.48 rgba(100,116,136,1.0),
                            stop:0.52 rgba(42,55,72,1.0),
                            stop:0.53 rgba(12,23,38,1.0),
                            stop:1 rgba(7,15,28,1.0));
                border: 1px solid rgba(255,255,255,0.98);
                border-top: 1px solid rgba(255,255,255,1.0);
            }
            QPushButton:pressed {
                padding-top: 1px;
                background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                            stop:0 rgba(42,54,70,1.0),
                            stop:0.40 rgba(18,29,43,1.0),
                            stop:1 rgba(6,14,28,1.0));
                border: 1px solid rgba(255,255,255,0.76);
            }
            QPushButton:checked {
                background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                            stop:0 rgba(226,233,242,0.98),
                            stop:0.12 rgba(176,189,203,0.98),
                            stop:0.46 rgba(103,118,136,0.99),
                            stop:0.50 rgba(40,52,68,1.0),
                            stop:0.51 rgba(9,18,31,1.0),
                            stop:1 rgba(4,11,22,1.0));
                border: 2px solid rgba(196,222,255,0.96);
                border-top: 1px solid rgba(255,255,255,1.0);
                color: #ffffff;
            }
            QPushButton:disabled {
                background: rgba(112,122,136,0.46);
                color: #dbe4ef;
                border: 1px solid rgba(255,255,255,0.28);
            }
        )QSS").arg(leftPad);
    }

    return QString::fromUtf8(R"QSS(
        QPushButton {
            background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                        stop:0 #ffffff, stop:1 #e6e6e6);
            border: 1px solid #bfc3c7;
            border-radius: 12px;
            color: #151a20;
            font-weight: 700;
            padding-left: %1;
            padding-right: 12px;
            text-align: left;
        }
        QPushButton:hover {
            border: 1px solid #98a5b3;
            color: #111418;
        }
        QPushButton:pressed {
            color: #111418;
        }
        QPushButton:checked {
            background: #d7ffd7;
            border: 2px solid #3ccf3c;
            color: #101418;
        }
        QPushButton:disabled {
            color: #4b5563;
        }
    )QSS").arg(leftPad);
}

static void applySettingsNavButtonStyle(QPushButton* button, bool submenu = false)
{
    if (!button) return;
    button->setStyleSheet(settingsNavButtonQss(submenu));
}

static bool styleDeclaresTextColor(const QString& styleSheet)
{
    return styleSheet.contains(QRegularExpression(QStringLiteral(R"((^|[;{\s])color\s*:)"), QRegularExpression::CaseInsensitiveOption));
}

static bool styleSuggestsLightSurface(const QString& styleSheet)
{
    const QString s = styleSheet.toLower();
    return s.contains(QStringLiteral("background:#fff"))
        || s.contains(QStringLiteral("background:#ffffff"))
        || s.contains(QStringLiteral("background: #fff"))
        || s.contains(QStringLiteral("background: #ffffff"))
        || s.contains(QStringLiteral("stop:0 #ffffff"))
        || s.contains(QStringLiteral("stop:0 #f2f4f8"))
        || s.contains(QStringLiteral("background:#f2f4f8"))
        || s.contains(QStringLiteral("background:#edf1f5"))
        || s.contains(QStringLiteral("background:rgba(255,255,255,0.88"))
        || s.contains(QStringLiteral("background:rgba(255,255,255,0.9"))
        || s.contains(QStringLiteral("background:rgba(255,255,255,0.92"))
        || s.contains(QStringLiteral("background:rgba(255,255,255,0.94"))
        || s.contains(QStringLiteral("background:rgba(255,255,255,0.96"))
        || s.contains(QStringLiteral("background:rgba(255,255,255,0.98"));
}

static bool widgetPrefersLightTextSurface(const QWidget* widget)
{
    if (!widget) return false;
    const QVariant surface = widget->property("settingsSurface");
    if (surface.isValid()) {
        const QString value = surface.toString().trimmed().toLower();
        if (value == QStringLiteral("light")) return true;
        if (value == QStringLiteral("dark")) return false;
    }

    if (styleSuggestsLightSurface(widget->styleSheet())) return true;
    if (qobject_cast<const QLineEdit*>(widget) ||
        qobject_cast<const QComboBox*>(widget) ||
        qobject_cast<const QSpinBox*>(widget) ||
        qobject_cast<const QDoubleSpinBox*>(widget) ||
        qobject_cast<const QDateEdit*>(widget) ||
        qobject_cast<const QDateTimeEdit*>(widget) ||
        qobject_cast<const QTextEdit*>(widget) ||
        qobject_cast<const QPlainTextEdit*>(widget) ||
        qobject_cast<const QTableWidget*>(widget) ||
        qobject_cast<const QTableView*>(widget) ||
        qobject_cast<const QListView*>(widget) ||
        qobject_cast<const QTreeWidget*>(widget) ||
        qobject_cast<const QHeaderView*>(widget) ||
        qobject_cast<const QAbstractItemView*>(widget)) {
        return true;
    }

    const QWidget* parent = widget->parentWidget();
    while (parent) {
        const QVariant parentSurface = parent->property("settingsSurface");
        if (parentSurface.isValid()) {
            const QString value = parentSurface.toString().trimmed().toLower();
            if (value == QStringLiteral("light")) return true;
            if (value == QStringLiteral("dark")) return false;
        }
        if (parent->objectName() == QStringLiteral("settings_right") ||
            parent->objectName() == QStringLiteral("box") ||
            parent->objectName() == QStringLiteral("hdr") ||
            parent->objectName() == QStringLiteral("ftr") ||
            parent->objectName() == QStringLiteral("settings_items_page") ||
            parent->objectName() == QStringLiteral("settings_detail_page")) {
            return false;
        }
        if (styleSuggestsLightSurface(parent->styleSheet())) return true;
        parent = parent->parentWidget();
    }
    return !isDarkUi();
}

static void applyContrastPaletteToWidget(QWidget* widget)
{
    if (!widget) return;

    const bool lightSurface = widgetPrefersLightTextSurface(widget);
    const QColor textColor(lightSurface ? cTextOnLight() : cTextOnDark());
    const QColor mutedColor(lightSurface ? cMutedTextOnLight() : cMutedTextOnDark());
    const QColor disabledColor(lightSurface ? cDisabledTextOnLight() : cDisabledTextOnDark());
    const QColor placeholderColor(lightSurface ? cPlaceholderOnLight() : cMutedTextOnDark());

    QPalette pal = widget->palette();
    if (!styleDeclaresTextColor(widget->styleSheet())) {
        pal.setColor(QPalette::WindowText, textColor);
        pal.setColor(QPalette::Text, textColor);
        pal.setColor(QPalette::ButtonText, textColor);
        pal.setColor(QPalette::HighlightedText, lightSurface ? QColor(cTextOnLight()) : QColor(cTextOnDark()));
        pal.setColor(QPalette::PlaceholderText, placeholderColor);
        pal.setColor(QPalette::Disabled, QPalette::WindowText, disabledColor);
        pal.setColor(QPalette::Disabled, QPalette::Text, disabledColor);
        pal.setColor(QPalette::Disabled, QPalette::ButtonText, disabledColor);
        widget->setPalette(pal);
    } else if (qobject_cast<QLineEdit*>(widget) || qobject_cast<QTextEdit*>(widget) || qobject_cast<QPlainTextEdit*>(widget)) {
        pal.setColor(QPalette::PlaceholderText, placeholderColor);
        widget->setPalette(pal);
    }

    if (auto* group = qobject_cast<QGroupBox*>(widget)) {
        QPalette groupPal = group->palette();
        groupPal.setColor(QPalette::WindowText, textColor);
        groupPal.setColor(QPalette::Disabled, QPalette::WindowText, disabledColor);
        group->setPalette(groupPal);
    }
    if (auto* header = qobject_cast<QHeaderView*>(widget)) {
        QPalette hp = header->palette();
        hp.setColor(QPalette::ButtonText, QColor(cTextOnLight()));
        hp.setColor(QPalette::WindowText, QColor(cTextOnLight()));
        header->setPalette(hp);
    }
    if (auto* itemView = qobject_cast<QAbstractItemView*>(widget)) {
        QPalette vp = itemView->palette();
        vp.setColor(QPalette::Text, QColor(cTextOnLight()));
        vp.setColor(QPalette::HighlightedText, QColor(cTextOnLight()));
        itemView->setPalette(vp);
    }
    if (auto* label = qobject_cast<QLabel*>(widget)) {
        if (!styleDeclaresTextColor(label->styleSheet())) {
            label->setStyleSheet(label->styleSheet());
        }
        QPalette lp = label->palette();
        lp.setColor(QPalette::WindowText, label->property("settingsMutedText").toBool() ? mutedColor : textColor);
        lp.setColor(QPalette::Disabled, QPalette::WindowText, disabledColor);
        label->setPalette(lp);
    }
}

static void applySettingsContrastPalette(QWidget* root)
{
    if (!root) return;
    applyContrastPaletteToWidget(root);
    const auto widgets = root->findChildren<QWidget*>();
    for (auto* widget : widgets) {
        applyContrastPaletteToWidget(widget);
    }
}

static QPushButton* makeNavBtn(const QString& text, QWidget* parent)
{
    auto* b = new QPushButton(text, parent);
    b->setCheckable(true);
    b->setCursor(Qt::PointingHandCursor);
    b->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    // Default size for right menu; left menu is adjusted separately.
    b->setFixedSize(186, 57);
    b->setFocusPolicy(Qt::NoFocus);

    // Shadow to mimic "raised" glossy buttons
    auto* sh = new QGraphicsDropShadowEffect(b);
    sh->setBlurRadius(14);
    sh->setOffset(0, 2);
    sh->setColor(QColor(0,0,0,80));
    b->setGraphicsEffect(sh);

    return b;
}

