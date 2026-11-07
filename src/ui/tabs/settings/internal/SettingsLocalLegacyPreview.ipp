// Extracted from src/ui/tabs/settings/SettingsPageLocalSupport.cpp for P1/P2 monolith reduction.
// Included exactly once by the original translation unit/header; do not compile separately.

namespace {
struct LegacyPreviewTaskResult {
    bool ok = false;
    LegacyImportAuditReport report;
    QString errorText;
};

struct LegacyExecuteTaskResult {
    bool ok = false;
    LegacyMigrationResult result;
    QString errorText;
};

struct NativeBackupAuditTaskResult {
    bool ok = false;
    NativeBackupAuditResult result;
    QString errorText;
};

struct NativeBackupRestoreTaskResult {
    bool ok = false;
    NativeBackupRestoreResult result;
    QString errorText;
};

static QProgressDialog* createBusyProgressDialog(QWidget* owner, const QString& title, const QString& text)
{
    auto* progress = new QProgressDialog(text, QString(), 0, 0, owner);
    progress->setWindowTitle(title);
    progress->setWindowModality(Qt::WindowModal);
    progress->setMinimumDuration(0);
    progress->setAutoClose(false);
    progress->setAutoReset(false);
    progress->setCancelButton(nullptr);
    progress->setStyleSheet((ThemeManager::currentTheme() == QStringLiteral("graphite"))
        ? QStringLiteral("QProgressDialog{background:qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 rgba(34,44,56,0.98),stop:1 rgba(18,24,31,0.98));color:#f7fbff;} QLabel{color:#f7fbff;} QProgressBar{background:rgba(255,255,255,0.08);color:#f7fbff;border:1px solid rgba(255,255,255,0.12);border-radius:8px;text-align:center;} QProgressBar::chunk{background:#79a8ef;border-radius:7px;} QPushButton{background:qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 rgba(113,129,149,0.98),stop:1 rgba(25,32,40,0.99));color:#f8fbff;border:1px solid rgba(228,236,246,0.34);border-radius:10px;padding:6px 14px;font-weight:700;}")
        : QStringLiteral("QProgressDialog{background:#ffffff;color:#101418;} QLabel{color:#101418;} QPushButton{background:qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #ffffff,stop:1 #e6e6e6);color:#101418;border:1px solid #bfc3c7;border-radius:10px;padding:6px 14px;font-weight:700;}"));
    progress->show();
    progress->repaint();
    return progress;
}


class LegacyImportProgressDialog final : public QDialog {
public:
    explicit LegacyImportProgressDialog(QWidget* owner, const QString& title)
        : QDialog(owner)
    {
        setObjectName(QStringLiteral("legacyImportProgressDialog"));
        setWindowTitle(title);
        setWindowModality(Qt::WindowModal);
        setAttribute(Qt::WA_DeleteOnClose, false);
        setAttribute(Qt::WA_TranslucentBackground, true);
        setMinimumWidth(560);
        setSizeGripEnabled(false);
        setModal(true);

        auto* root = new QVBoxLayout(this);
        root->setContentsMargins(10, 10, 10, 10);
        root->setSpacing(0);

        auto* panel = new QFrame(this);
        panel->setObjectName(QStringLiteral("legacyImportProgressPanel"));
        root->addWidget(panel);

        auto* panelLayout = new QVBoxLayout(panel);
        panelLayout->setContentsMargins(20, 18, 20, 18);
        panelLayout->setSpacing(10);

        m_title = new QLabel(title, panel);
        m_title->setStyleSheet(QStringLiteral("font-size:18px;font-weight:800;color:#f7fbff;"));
        panelLayout->addWidget(m_title);

        m_status = new QLabel(QString::fromUtf8("Підготовка..."), panel);
        m_status->setWordWrap(true);
        m_status->setStyleSheet(QStringLiteral("font-size:13px;color:#eef3fa;"));
        panelLayout->addWidget(m_status);

        m_activity = new QLabel(QString::fromUtf8("● Активний процес"), panel);
        m_activity->setStyleSheet(QStringLiteral("font-size:12px;font-weight:700;color:#b7d2ff;"));
        panelLayout->addWidget(m_activity);

        auto* currentCaption = new QLabel(QString::fromUtf8("Поточний етап"), panel);
        currentCaption->setStyleSheet(QStringLiteral("font-size:12px;font-weight:700;color:#edf3fb;"));
        panelLayout->addWidget(currentCaption);

        m_primary = new QProgressBar(panel);
        m_primary->setTextVisible(true);
        m_primary->setRange(0, 0);
        m_primary->setFormat(QString::fromUtf8("виконується..."));
        m_primary->setFixedHeight(24);
        panelLayout->addWidget(m_primary);

        auto* overallCaption = new QLabel(QString::fromUtf8("Загальний прогрес міграції"), panel);
        overallCaption->setStyleSheet(QStringLiteral("font-size:12px;font-weight:700;color:#edf3fb;"));
        panelLayout->addWidget(overallCaption);

        m_secondary = new QProgressBar(panel);
        m_secondary->setTextVisible(true);
        m_secondary->setRange(0, 100);
        m_secondary->setValue(0);
        m_secondary->setFormat(QStringLiteral("0%"));
        m_secondary->setFixedHeight(24);
        panelLayout->addWidget(m_secondary);

        auto* grid = new QGridLayout();
        grid->setHorizontalSpacing(18);
        grid->setVerticalSpacing(6);
        auto makeCounter = [&](const QString& caption, int row, int col) {
            auto* cap = new QLabel(caption, panel);
            cap->setStyleSheet(QStringLiteral("font-size:12px;color:#e8eef8;"));
            auto* val = new QLabel(QStringLiteral("0"), panel);
            val->setStyleSheet(QStringLiteral("font-size:14px;font-weight:800;color:#ffffff;"));
            grid->addWidget(cap, row, col * 2);
            grid->addWidget(val, row, col * 2 + 1);
            return val;
        };
        m_orders = makeCounter(QString::fromUtf8("Нарядів:"), 0, 0);
        m_services = makeCounter(QString::fromUtf8("Робіт:"), 0, 1);
        m_parts = makeCounter(QString::fromUtf8("Запчастин:"), 1, 0);
        m_warnings = makeCounter(QString::fromUtf8("Попереджень:"), 1, 1);
        m_errors = makeCounter(QString::fromUtf8("Помилок:"), 2, 0);
        panelLayout->addLayout(grid);

        m_details = new QLabel(QString(), panel);
        m_details->setWordWrap(true);
        m_details->setStyleSheet(QStringLiteral("font-size:12px;color:#e0e8f2;"));
        panelLayout->addWidget(m_details);

        setStyleSheet(QStringLiteral(
            "QDialog#legacyImportProgressDialog{background:transparent;}"
            "QFrame#legacyImportProgressPanel{background:qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 rgba(56,66,80,61),stop:1 rgba(24,30,38,51));border:1px solid rgba(236,242,250,77);border-radius:16px;}"
            "QProgressBar{background:rgba(255,255,255,26);color:#f8fbff;border:1px solid rgba(255,255,255,56);border-radius:10px;text-align:center;padding:0 6px;font-weight:700;}"
            "QProgressBar::chunk{background:qlineargradient(x1:0,y1:0,x2:1,y2:0,stop:0 rgba(94,188,255,242),stop:1 rgba(184,228,255,250));border-radius:9px;}"));

        m_timer = new QTimer(this);
        m_timer->setInterval(220);
        connect(m_timer, &QTimer::timeout, this, [this]() {
            static const QStringList dots = { QString::fromUtf8("●"), QString::fromUtf8("●●"), QString::fromUtf8("●●●"), QString::fromUtf8("●●") };
            m_activityPhase = (m_activityPhase + 1) % dots.size();
            const QString accent = dots.at(m_activityPhase);
            const QString stateText = m_finished ? m_finalCaption : QString::fromUtf8("Активний процес");
            m_activity->setText(accent + QStringLiteral(" ") + stateText);
        });
        m_timer->start();
    }

    void applyProgress(const LegacyImportProgressState& s)
    {
        if (!s.title.trimmed().isEmpty()) m_title->setText(s.title);
        if (!s.status.trimmed().isEmpty()) m_status->setText(s.status);

        if (s.finished) {
            m_primary->setRange(0, 100);
            m_primary->setValue(100);
            m_primary->setFormat(QString::fromUtf8("етап завершено"));
        } else if (s.indeterminate) {
            m_primary->setRange(0, 0);
            m_primary->setFormat(QString::fromUtf8("виконується..."));
        } else {
            if (m_primary->maximum() == 0) m_primary->setRange(0, 100);
            const int stagePercent = s.stepTotal > 0
                ? qBound(0, qRound((100.0 * qMax(0, s.stepCurrent)) / qMax(1, s.stepTotal)), 100)
                : qBound(0, s.mainPercent, 100);
            m_primary->setValue(stagePercent);
            m_primary->setFormat(QString::number(stagePercent) + QStringLiteral("%"));
        }

        const int overallPercent = s.mainPercent >= 0 ? qBound(0, s.mainPercent, 100) : 0;
        m_secondary->setRange(0, 100);
        m_secondary->setValue(overallPercent);
        m_secondary->setFormat(QString::number(overallPercent) + QStringLiteral("%"));

        const int orderCount = qMax(0, s.orderCount);
        const int serviceCount = qMax(0, s.serviceCount);
        const int partCount = qMax(0, s.partCount);
        const int warningCount = qMax(0, s.warningCount);
        const int errorCount = qMax(0, s.errorCount);

        m_orders->setText(QString::number(orderCount));
        m_services->setText(QString::number(serviceCount));
        m_parts->setText(QString::number(partCount));
        m_warnings->setText(QString::number(warningCount));
        m_errors->setText(QString::number(errorCount));

        QStringList detailLines;
        if (s.stepTotal > 0) {
            detailLines << QString::fromUtf8("Етап %1 із %2").arg(qMax(0, s.stepCurrent)).arg(s.stepTotal);
        }
        detailLines << QString::fromUtf8("Загальний прогрес: %1%").arg(overallPercent);
        if (!s.metricsCaption.trimmed().isEmpty()) {
            detailLines << QString::fromUtf8("Лічильники: %1").arg(s.metricsCaption.trimmed());
        }
        detailLines << QString::fromUtf8("Сумарно об’єктів: %1").arg(qMax(0, s.processedCount));
        m_details->setText(detailLines.join(QStringLiteral("\n")));

        if (s.finished) {
            m_finished = true;
            m_finalCaption = s.errorCount > 0 ? QString::fromUtf8("Завершено з помилками") : QString::fromUtf8("Завершено");
            m_secondary->setValue(100);
            m_secondary->setFormat(QStringLiteral("100%"));
        }
        m_details->repaint();
    }

private:
    QLabel* m_title = nullptr;
    QLabel* m_status = nullptr;
    QLabel* m_activity = nullptr;
    QLabel* m_orders = nullptr;
    QLabel* m_services = nullptr;
    QLabel* m_parts = nullptr;
    QLabel* m_warnings = nullptr;
    QLabel* m_errors = nullptr;
    QLabel* m_details = nullptr;
    QProgressBar* m_primary = nullptr;
    QProgressBar* m_secondary = nullptr;
    QTimer* m_timer = nullptr;
    int m_activityPhase = 0;
    bool m_finished = false;
    QString m_finalCaption;
};

static void postLegacyProgress(LegacyImportProgressDialog* dialog, const LegacyImportProgressState& state)
{
    if (!dialog) return;
    QPointer<LegacyImportProgressDialog> guard(dialog);
    QMetaObject::invokeMethod(dialog, [guard, state]() {
        if (guard) guard->applyProgress(state);
    }, Qt::QueuedConnection);
}

class InlineWordDelegate final : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem&, const QModelIndex&) const override {
        auto* edit = new QLineEdit(parent);
        edit->setFrame(false);
        edit->setAutoFillBackground(true);
        edit->setStyleSheet(QStringLiteral("QLineEdit{background:#ffffff;border:1px solid #c7ccd4;border-radius:4px;padding:0 6px;color:#111111;selection-background-color:#d7e4ff;}"));
        QTimer::singleShot(0, edit, [edit]() { edit->selectAll(); });
        return edit;
    }
    void updateEditorGeometry(QWidget* editor, const QStyleOptionViewItem& option, const QModelIndex&) const override {
        QRect r = option.rect.adjusted(4, 2, -4, -2);
        editor->setGeometry(r);
    }
};

static QImage applyLogoVisualSettings(const QImage& src, double contrast, double saturation, double opacity)
{
    if (src.isNull()) return src;

    const double safeContrast = qBound(0.0, contrast, 3.0);
    const double safeSaturation = qBound(0.0, saturation, 3.0);
    const double safeOpacity = qBound(0.0, opacity, 1.0);

    QImage img = src.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < img.height(); ++y) {
        QRgb* line = reinterpret_cast<QRgb*>(img.scanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            const QRgb px = line[x];
            const int a0 = qAlpha(px);
            if (a0 == 0) {
                line[x] = qRgba(0, 0, 0, 0);
                continue;
            }

            double r = qRed(px);
            double g = qGreen(px);
            double b = qBlue(px);
            const double gray = 0.299 * r + 0.587 * g + 0.114 * b;
            r = gray + (r - gray) * safeSaturation;
            g = gray + (g - gray) * safeSaturation;
            b = gray + (b - gray) * safeSaturation;
            r = ((r / 255.0 - 0.5) * safeContrast + 0.5) * 255.0;
            g = ((g / 255.0 - 0.5) * safeContrast + 0.5) * 255.0;
            b = ((b / 255.0 - 0.5) * safeContrast + 0.5) * 255.0;
            const int a = qBound(0, qRound(a0 * safeOpacity), 255);
            line[x] = qRgba(qBound(0, qRound(r), 255),
                            qBound(0, qRound(g), 255),
                            qBound(0, qRound(b), 255),
                            a);
        }
    }
    return img;
}

class LogoPositionPreview final : public QWidget {
public:
    explicit LogoPositionPreview(QWidget* parent = nullptr) : QWidget(parent) {
        setMinimumHeight(180);
        setMouseTracking(true);
    }

    void setLogoPath(const QString& path) {
        m_logoPath = path.trimmed();
        m_logoImage = QImage(m_logoPath);
        rebuildPreviewImage();
    }

    void setNormalizedPosition(double x, double y) {
        m_normX = qBound(0.0, x, 1.0);
        m_normY = qBound(0.0, y, 1.0);
        update();
    }

    void setLogoScale(double value) {
        m_scale = qBound(0.3, value, 3.0);
        update();
    }

    void setLogoVisuals(double contrast, double saturation, double opacity) {
        m_contrast = qBound(0.0, contrast, 3.0);
        m_saturation = qBound(0.0, saturation, 3.0);
        m_opacity = qBound(0.0, opacity, 1.0);
        rebuildPreviewImage();
    }

    double normalizedX() const { return m_normX; }
    double normalizedY() const { return m_normY; }

    void setPositionChangedCallback(std::function<void(double, double)> cb) {
        m_onPositionChanged = std::move(cb);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.fillRect(rect(), QColor(255, 255, 255));

        const QRectF page = pageRect();
        painter.setPen(QPen(QColor(188, 194, 204), 1.0));
        painter.setBrush(QColor(255, 255, 255));
        painter.drawRoundedRect(page, 6.0, 6.0);

        painter.setPen(QColor(86, 92, 104));
        painter.setFont(QFont(QStringLiteral("Times New Roman"), 10, QFont::Bold));
        painter.drawText(page.adjusted(10, 8, -10, -8), Qt::AlignTop | Qt::AlignLeft, QString::fromUtf8("Сторінка PDF — перетягніть логотип у потрібне місце"));

        const QRectF lg = logoRect();
        if (m_previewImage.isNull()) {
            painter.setPen(QPen(QColor(160, 168, 180), 1.0, Qt::DashLine));
            painter.setBrush(QColor(245, 247, 250, 220));
            painter.drawRoundedRect(lg, 4.0, 4.0);
            painter.setPen(QColor(90, 96, 108));
            painter.setFont(QFont(QStringLiteral("Times New Roman"), 10, QFont::Bold));
            painter.drawText(lg.adjusted(8, 8, -8, -8), Qt::AlignCenter | Qt::TextWordWrap,
                             QString::fromUtf8("Тут буде логотип\nЗавантажте PNG/JPG і перетягніть його"));
            return;
        }

        painter.setPen(QPen(QColor(150, 158, 170), 1.0, Qt::DashLine));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(lg, 4.0, 4.0);
        painter.drawImage(lg, m_previewImage, m_previewImage.rect());
    }

    void mousePressEvent(QMouseEvent* e) override {
        if (e->button() != Qt::LeftButton) return;
        const QRectF lg = logoRect();
        if (lg.contains(e->position())) {
            m_dragging = true;
            m_dragOffset = e->position() - lg.topLeft();
        } else {
            m_dragging = true;
            m_dragOffset = QPointF(lg.width() / 2.0, lg.height() / 2.0);
            moveLogoTopLeft(e->position() - m_dragOffset, true);
        }
    }

    void mouseMoveEvent(QMouseEvent* e) override {
        if (!m_dragging) return;
        moveLogoTopLeft(e->position() - m_dragOffset, true);
    }

    void mouseReleaseEvent(QMouseEvent* e) override {
        if (e->button() == Qt::LeftButton && m_dragging) {
            m_dragging = false;
            if (m_onPositionChanged) m_onPositionChanged(m_normX, m_normY);
        }
    }

private:
    QRectF pageRect() const {
        return rect().adjusted(12, 12, -12, -12);
    }

    void rebuildPreviewImage() {
        m_previewImage = applyLogoVisualSettings(m_logoImage, m_contrast, m_saturation, m_opacity);
        update();
    }

    QSizeF logoSize() const {
        const QRectF page = pageRect();
        const double scale = qBound(0.3, m_scale, 3.0);
        QSizeF maxSize(page.width() * 0.26 * scale, page.height() * 0.16 * scale);
        maxSize.setWidth(qMin(maxSize.width(), page.width() * 0.9));
        maxSize.setHeight(qMin(maxSize.height(), page.height() * 0.8));
        if (m_previewImage.isNull() || m_previewImage.width() <= 0 || m_previewImage.height() <= 0) {
            return QSizeF(qMin(170.0 * scale, maxSize.width()), qMin(72.0 * scale, maxSize.height()));
        }
        QSizeF src(m_previewImage.width(), m_previewImage.height());
        src.scale(maxSize, Qt::KeepAspectRatio);
        const double minW = qMin(120.0 * qMin(scale, 1.6), maxSize.width());
        const double minH = qMin(42.0 * qMin(scale, 1.6), maxSize.height());
        if (src.width() < minW) {
            const double ratio = (m_previewImage.width() > 0) ? (double)m_previewImage.height() / (double)m_previewImage.width() : 0.35;
            src.setWidth(minW);
            src.setHeight(qMin(maxSize.height(), minW * ratio));
        }
        if (src.height() < minH) {
            const double ratio = (m_previewImage.height() > 0) ? (double)m_previewImage.width() / (double)m_previewImage.height() : 2.0;
            src.setHeight(minH);
            src.setWidth(qMin(maxSize.width(), minH * ratio));
        }
        return src;
    }

    QRectF logoRect() const {
        const QRectF page = pageRect();
        const QSizeF size = logoSize();
        const double maxX = qMax(0.0, page.width() - size.width());
        const double maxY = qMax(0.0, page.height() - size.height());
        const double x = page.left() + m_normX * maxX;
        const double y = page.top() + m_normY * maxY;
        return QRectF(x, y, size.width(), size.height());
    }

    void moveLogoTopLeft(const QPointF& desiredTopLeft, bool notify) {
        const QRectF page = pageRect();
        const QSizeF size = logoSize();
        const double minX = page.left();
        const double minY = page.top();
        const double maxX = qMax(minX, page.right() - size.width());
        const double maxY = qMax(minY, page.bottom() - size.height());
        const double x = qBound(minX, desiredTopLeft.x(), maxX);
        const double y = qBound(minY, desiredTopLeft.y(), maxY);
        const double denomX = qMax(1.0, page.width() - size.width());
        const double denomY = qMax(1.0, page.height() - size.height());
        m_normX = qBound(0.0, (x - page.left()) / denomX, 1.0);
        m_normY = qBound(0.0, (y - page.top()) / denomY, 1.0);
        update();
        if (notify && m_onPositionChanged) m_onPositionChanged(m_normX, m_normY);
    }

private:
    QString m_logoPath;
    QImage m_logoImage;
    QImage m_previewImage;
    double m_normX = 0.04;
    double m_normY = 0.04;
    double m_scale = 1.0;
    double m_contrast = 1.0;
    double m_saturation = 1.0;
    double m_opacity = 1.0;
    bool m_dragging = false;
    QPointF m_dragOffset;
    std::function<void(double, double)> m_onPositionChanged;
};


static QString contrastCheckboxRuleBlock(const QString& selector = QStringLiteral("QCheckBox"))
{
    return QString::fromUtf8(
        "%1{spacing:8px; background:transparent;}"
    ).arg(selector);
}

static QString titleCaseWords(QString s)
{
    s = s.trimmed();
    if (s.isEmpty()) return s;
    const auto parts = s.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
    QStringList out;
    out.reserve(parts.size());
    for (QString p : parts) {
        if (p.isEmpty()) continue;
        p = p.toLower();
        p[0] = p.at(0).toUpper();
        out << p;
    }
    return out.join(QChar(' '));
}

static QString sanitizeQrAlnum(QString s)
{
