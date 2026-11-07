#include "ui/tabs/settings/SettingsBrandingSectionSupport.h"
#include <functional>

#include "core/services/settings/BrandingAssetsSettingsService.h"
#include "core/services/settings/BrandingSettingsService.h"
#include "ui/tabs/settings/SettingsPageDataSupport.h"
#include "ui/theme/ThemeManager.h"

#include <QAbstractItemView>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QListWidget>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>
#include <QtMath>
#include <utility>

namespace {

bool isDarkUi()
{
    return ThemeManager::currentTheme() == QStringLiteral("graphite");
}

QString cPrimary()
{
    return QStringLiteral("#f7fbff");
}

QString cSecondary()
{
    return QStringLiteral("#dbe4ef");
}

QString cPanelBg()
{
    return QStringLiteral("rgba(31,40,51,0.88)");
}

QString cPanelBorder()
{
    return QStringLiteral("rgba(222,234,247,0.28)");
}

QImage applyLogoVisualSettings(const QImage& src,
                               double contrast,
                               double saturation,
                               double opacity)
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
    explicit LogoPositionPreview(QWidget* parent = nullptr) : QWidget(parent)
    {
        setMinimumHeight(180);
        setMouseTracking(true);
    }

    void setLogoPath(const QString& path)
    {
        m_logoPath = path.trimmed();
        m_logoImage = QImage(m_logoPath);
        rebuildPreviewImage();
    }

    void setNormalizedPosition(double x, double y)
    {
        m_normX = qBound(0.0, x, 1.0);
        m_normY = qBound(0.0, y, 1.0);
        update();
    }

    void setLogoScale(double value)
    {
        m_scale = qBound(0.3, value, 3.0);
        update();
    }

    void setLogoVisuals(double contrast, double saturation, double opacity)
    {
        m_contrast = qBound(0.0, contrast, 3.0);
        m_saturation = qBound(0.0, saturation, 3.0);
        m_opacity = qBound(0.0, opacity, 1.0);
        rebuildPreviewImage();
    }

    void setPositionChangedCallback(std::function<void(double, double)> cb)
    {
        m_onPositionChanged = std::move(cb);
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.fillRect(rect(), QColor(255, 255, 255));

        const QRectF page = pageRect();
        painter.setPen(QPen(QColor(188, 194, 204), 1.0));
        painter.setBrush(QColor(255, 255, 255));
        painter.drawRoundedRect(page, 6.0, 6.0);

        painter.setPen(QColor(86, 92, 104));
        painter.setFont(QFont(QStringLiteral("Times New Roman"), 10, QFont::Bold));
        painter.drawText(page.adjusted(10, 8, -10, -8), Qt::AlignTop | Qt::AlignLeft,
                         QString::fromUtf8("Сторінка PDF — перетягніть логотип у потрібне місце"));

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

    void mousePressEvent(QMouseEvent* e) override
    {
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

    void mouseMoveEvent(QMouseEvent* e) override
    {
        if (!m_dragging) return;
        moveLogoTopLeft(e->position() - m_dragOffset, true);
    }

    void mouseReleaseEvent(QMouseEvent* e) override
    {
        if (e->button() == Qt::LeftButton && m_dragging) {
            m_dragging = false;
            if (m_onPositionChanged) m_onPositionChanged(m_normX, m_normY);
        }
    }

private:
    QRectF pageRect() const { return rect().adjusted(12, 12, -12, -12); }

    void rebuildPreviewImage()
    {
        m_previewImage = applyLogoVisualSettings(m_logoImage, m_contrast, m_saturation, m_opacity);
        update();
    }

    QSizeF logoSize() const
    {
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
            const double ratio = (m_previewImage.width() > 0)
                ? static_cast<double>(m_previewImage.height()) / static_cast<double>(m_previewImage.width())
                : 0.35;
            src.setWidth(minW);
            src.setHeight(qMin(maxSize.height(), minW * ratio));
        }
        if (src.height() < minH) {
            const double ratio = (m_previewImage.height() > 0)
                ? static_cast<double>(m_previewImage.width()) / static_cast<double>(m_previewImage.height())
                : 2.0;
            src.setHeight(minH);
            src.setWidth(qMin(maxSize.width(), minH * ratio));
        }
        return src;
    }

    QRectF logoRect() const
    {
        const QRectF page = pageRect();
        const QSizeF size = logoSize();
        const double maxX = qMax(0.0, page.width() - size.width());
        const double maxY = qMax(0.0, page.height() - size.height());
        const double x = page.left() + m_normX * maxX;
        const double y = page.top() + m_normY * maxY;
        return QRectF(x, y, size.width(), size.height());
    }

    void moveLogoTopLeft(const QPointF& desiredTopLeft, bool notify)
    {
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

} // namespace

QWidget* SettingsBrandingSectionSupport::build(QWidget* owner,
                                               const std::function<void()>& queueRefreshAll)
{
    auto* w = new QWidget(owner);
    auto* outer = new QVBoxLayout(w);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    auto* scroll = new QScrollArea(w);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    outer->addWidget(scroll, 1);

    auto* body = new QWidget(scroll);
    auto* l = new QVBoxLayout(body);
    l->setContentsMargins(18, 18, 18, 18);
    l->setSpacing(16);
    scroll->setWidget(body);

    auto* titleRow = new QHBoxLayout();
    titleRow->setContentsMargins(0, 0, 0, 0);
    titleRow->setSpacing(12);
    auto* title = new QLabel(QString::fromUtf8("Брендинг СТО"), body);
    title->setStyleSheet(QString("font-size:14pt; font-weight:800; color:%1;").arg(cPrimary()));
    auto* btnSaveTop = new QPushButton(QString::fromUtf8("Зберегти"), body);
    btnSaveTop->setObjectName(QStringLiteral("settings_detail_top_action_button"));
    btnSaveTop->setFixedSize(120, 40);
    titleRow->addWidget(title);
    titleRow->addStretch(1);
    l->addLayout(titleRow);

    auto makeSectionTitle = [](const QString& text) {
        auto* lbl = new QLabel(text);
        lbl->setWordWrap(true);
        lbl->setStyleSheet(QString("font-size:11pt; font-weight:700; color:%1;").arg(cPrimary()));
        return lbl;
    };
    auto makeHint = [](const QString& text) {
        auto* lbl = new QLabel(text);
        lbl->setWordWrap(true);
        lbl->setStyleSheet(QString("font-size:9.5pt; color:%1;").arg(cSecondary()));
        return lbl;
    };
    auto makePanel = [body]() {
        auto* frame = new QFrame(body);
        frame->setFrameShape(QFrame::NoFrame);
        frame->setStyleSheet(QString("QFrame{background:%1; border:1px solid %2; border-radius:12px;}")
                                 .arg(cPanelBg())
                                 .arg(cPanelBorder()));
        auto* panelL = new QVBoxLayout(frame);
        panelL->setContentsMargins(16, 16, 16, 16);
        panelL->setSpacing(12);
        return std::pair<QFrame*, QVBoxLayout*>(frame, panelL);
    };

    const auto brandingSnapshot = BrandingSettingsService::loadSnapshot();
    QStringList brandingLogos = BrandingAssetsSettingsService::loadLogoListWithActive(brandingSnapshot.logoPath);
    QString activeLogoPath = brandingSnapshot.logoPath.trimmed();
    if (activeLogoPath.isEmpty() && !brandingLogos.isEmpty()) {
        activeLogoPath = brandingLogos.constFirst();
        BrandingSettingsService::saveLogoPath(activeLogoPath);
    }
    if (!activeLogoPath.isEmpty() && !brandingLogos.contains(activeLogoPath)) {
        brandingLogos.prepend(activeLogoPath);
        brandingLogos = BrandingAssetsSettingsService::sanitizeLogoList(brandingLogos);
        bool changed = false;
        BrandingAssetsSettingsService::saveLogoList(brandingLogos, &changed);
        (void)changed;
    }

    auto* name = new QLineEdit(brandingSnapshot.name.isEmpty() ? QString::fromUtf8("CarService PointG 2.1.2") : brandingSnapshot.name, body);
    auto* phone = new QLineEdit(brandingSnapshot.phone, body);

    auto [basicPanel, basicPanelL] = makePanel();
    basicPanelL->addWidget(makeSectionTitle(QString::fromUtf8("Основні дані")));

    auto* form = new QFormLayout();
    form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    form->setFormAlignment(Qt::AlignTop);
    form->setHorizontalSpacing(14);
    form->setVerticalSpacing(12);

    auto* logoField = new QWidget(basicPanel);
    auto* logoFieldL = new QVBoxLayout(logoField);
    logoFieldL->setContentsMargins(0, 0, 0, 0);
    logoFieldL->setSpacing(8);

    auto* logoPath = new QLineEdit(activeLogoPath, logoField);
    auto* logoButtons = new QWidget(logoField);
    auto* logoButtonsL = new QHBoxLayout(logoButtons);
    logoButtonsL->setContentsMargins(0, 0, 0, 0);
    logoButtonsL->setSpacing(8);
    auto* browse = new QPushButton(logoButtons);
    browse->setText(QString::fromUtf8("Додати логотипи"));
    browse->setFixedSize(216,52);
    auto* removeLogo = new QPushButton(logoButtons);
    removeLogo->setText(QString::fromUtf8("Видалити вибраний"));
    removeLogo->setFixedSize(216,52);
    logoButtonsL->addWidget(browse, 0, Qt::AlignLeft);
    logoButtonsL->addWidget(removeLogo, 0, Qt::AlignLeft);
    logoButtonsL->addStretch(1);

    logoFieldL->addWidget(logoPath);
    logoFieldL->addWidget(logoButtons);

    form->addRow(QString::fromUtf8("Назва СТО:"), name);
    form->addRow(QString::fromUtf8("Телефон:"), phone);
    form->addRow(QString::fromUtf8("Активний логотип:"), logoField);
    if (auto* lbl = form->labelForField(name)) {
        lbl->setStyleSheet(QString("font-weight:600; color:%1; background:transparent;").arg(cPrimary()));
    }
    if (auto* lbl = form->labelForField(phone)) {
        lbl->setStyleSheet(QString("font-weight:600; color:%1; background:transparent;").arg(cPrimary()));
    }
    if (auto* lbl = form->labelForField(logoField)) {
        lbl->setStyleSheet(QString("font-weight:600; color:%1; background:transparent;").arg(cPrimary()));
    }
    basicPanelL->addLayout(form);
    basicPanelL->addWidget(makeHint(QString::fromUtf8("Спочатку вибери активний логотип, потім нижче вистав позицію та параметри для друку в PDF.")));
    l->addWidget(basicPanel);

    auto [listPanel, listPanelL] = makePanel();
    listPanelL->addWidget(makeSectionTitle(QString::fromUtf8("Доступні логотипи")));
    listPanelL->addWidget(makeHint(QString::fromUtf8("Список з прев’ю. Клік по елементу робить логотип активним. Список прокручується вертикально.")));

    auto* logoList = new QListWidget(listPanel);
    logoList->setViewMode(QListView::ListMode);
    logoList->setFlow(QListView::TopToBottom);
    logoList->setMovement(QListView::Static);
    logoList->setSelectionMode(QAbstractItemView::SingleSelection);
    logoList->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    logoList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    logoList->setIconSize(QSize(150, 64));
    logoList->setSpacing(8);
    logoList->setMinimumHeight(240);
    logoList->setStyleSheet(QString(
        "QListWidget{background:#ffffff;border:1px solid %1;border-radius:8px;padding:8px;}"
        "QListWidget::item{min-height:76px;padding:8px 10px;border-radius:6px;color:#1f2937;background:transparent;}"
        "QListWidget::item:selected{background:#e8f0fe;color:#111827;border:1px solid #b8c7e6;}"
        "QListWidget::item:hover{background:#f5f7fb;color:#111827;}"
    ).arg(cPanelBorder()));
    listPanelL->addWidget(logoList);
    l->addWidget(listPanel);

    auto refreshLogoList = [logoList, logoPath](const QStringList& logos) {
        const QString current = QDir::fromNativeSeparators(logoPath->text().trimmed());
        logoList->clear();
        int selectRow = -1;
        for (int i = 0; i < logos.size(); ++i) {
            const QString path = logos.at(i);
            const QFileInfo fi(path);
            QPixmap pm(path);
            if (!pm.isNull()) {
                pm = pm.scaled(150, 64, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            } else {
                pm = QPixmap(150, 64);
                pm.fill(Qt::transparent);
            }
            auto* item = new QListWidgetItem(QIcon(pm), fi.completeBaseName(), logoList);
            item->setData(Qt::UserRole, path);
            item->setToolTip(fi.fileName() + QStringLiteral("\n") + path);
            item->setSizeHint(QSize(0, 78));
            if (!current.isEmpty() && current == QDir::fromNativeSeparators(path)) selectRow = i;
        }
        if (selectRow >= 0) {
            logoList->setCurrentRow(selectRow);
        } else if (logoList->count() > 0) {
            logoList->setCurrentRow(0);
        }
    };

    refreshLogoList(brandingLogos);

    auto [previewPanel, previewPanelL] = makePanel();
    previewPanelL->addWidget(makeSectionTitle(QString::fromUtf8("Позиція логотипу у PDF наряду")));
    previewPanelL->addWidget(makeHint(QString::fromUtf8("Нижче показаний макет сторінки наряду. Перетягни логотип у потрібне місце — так він і буде стояти в PDF відносно шапки, таблиці та підписів.")));

    auto* preview = new LogoPositionPreview(previewPanel);
    preview->setMinimumHeight(720);
    preview->setStyleSheet(QStringLiteral("border:1px solid rgba(200,205,214,0.95); border-radius:10px; background:#ffffff;"));
    preview->setLogoPath(logoPath->text().trimmed());
    preview->setNormalizedPosition(brandingSnapshot.logoPosX, brandingSnapshot.logoPosY);
    preview->setLogoScale(brandingSnapshot.logoScale);
    preview->setLogoVisuals(brandingSnapshot.logoContrast,
                            brandingSnapshot.logoSaturation,
                            brandingSnapshot.logoOpacity);
    auto publishBrandingSettingsBackupEvent = [](const QString& description) {
        SettingsPageDataSupport::publishSettingsBackupEvent(QStringLiteral("branding_settings_save"),
                                                            QStringLiteral("branding_settings"),
                                                            QStringLiteral("settings.branding"),
                                                            description,
                                                            {QStringLiteral("settings")});
    };

    preview->setPositionChangedCallback([publishBrandingSettingsBackupEvent](double x, double y) {
        bool changed = false;
        BrandingSettingsService::saveLogoPosition(x, y, &changed);
        if (changed) {
            publishBrandingSettingsBackupEvent(QString::fromUtf8("Змінено позицію логотипу у PDF"));
        }
    });
    previewPanelL->addWidget(preview);
    l->addWidget(previewPanel);

    auto [paramsPanel, paramsPanelL] = makePanel();
    paramsPanelL->addWidget(makeSectionTitle(QString::fromUtf8("Параметри логотипу у PDF")));
    paramsPanelL->addWidget(makeHint(QString::fromUtf8("Налаштуй розмір, контрастність, колір і прозорість. Зміни одразу видно в прев’ю вище.")));

    auto makePctSlider = [body](int min, int max, int value) {
        auto* slider = new QSlider(Qt::Horizontal, body);
        slider->setRange(min, max);
        slider->setValue(value);
        slider->setSingleStep(1);
        slider->setPageStep(qMax(1, (max - min) / 10));
        return slider;
    };
    auto makePctSpin = [body](int min, int max, int value) {
        auto* spin = new QSpinBox(body);
        spin->setRange(min, max);
        spin->setSuffix(QStringLiteral("%"));
        spin->setValue(value);
        spin->setAlignment(Qt::AlignCenter);
        spin->setFixedWidth(92);
        return spin;
    };
    auto bindPctControls = [w](QSlider* slider, QSpinBox* spin, const QString& key, std::function<void(int)> apply) {
        QObject::connect(slider, &QSlider::valueChanged, w, [spin](int v) {
            if (spin->value() == v) return;
            spin->setValue(v);
        });
        QObject::connect(spin, qOverload<int>(&QSpinBox::valueChanged), w, [slider](int v) {
            if (slider->value() == v) return;
            slider->setValue(v);
        });
        QObject::connect(slider, &QSlider::valueChanged, w, [key, apply](int v) {
            const auto snapshot = BrandingSettingsService::loadSnapshot();
            if (key == QStringLiteral("settings.branding.logo_scale")) {
                BrandingSettingsService::saveLogoVisuals(v / 100.0, snapshot.logoContrast, snapshot.logoSaturation, snapshot.logoOpacity);
            } else if (key == QStringLiteral("settings.branding.logo_contrast")) {
                BrandingSettingsService::saveLogoVisuals(snapshot.logoScale, v / 100.0, snapshot.logoSaturation, snapshot.logoOpacity);
            } else if (key == QStringLiteral("settings.branding.logo_saturation")) {
                BrandingSettingsService::saveLogoVisuals(snapshot.logoScale, snapshot.logoContrast, v / 100.0, snapshot.logoOpacity);
            } else if (key == QStringLiteral("settings.branding.logo_opacity")) {
                BrandingSettingsService::saveLogoVisuals(snapshot.logoScale, snapshot.logoContrast, snapshot.logoSaturation, v / 100.0);
            }
            apply(v);
        });
    };
    auto makeSliderRow = [body, paramsPanelL](const QString& caption, QSlider* slider, QSpinBox* spin) {
        auto* rowWrap = new QWidget(body);
        auto* rowWrapL = new QVBoxLayout(rowWrap);
        rowWrapL->setContentsMargins(0, 0, 0, 0);
        rowWrapL->setSpacing(6);
        auto* lbl = new QLabel(caption, rowWrap);
        lbl->setStyleSheet(QString("font-weight:600; color:%1; background:transparent;").arg(cPrimary()));
        rowWrapL->addWidget(lbl);
        auto* row = new QWidget(rowWrap);
        auto* rowL = new QHBoxLayout(row);
        rowL->setContentsMargins(0, 0, 0, 0);
        rowL->setSpacing(10);
        rowL->addWidget(slider, 1);
        rowL->addWidget(spin, 0, Qt::AlignRight);
        rowWrapL->addWidget(row);
        paramsPanelL->addWidget(rowWrap);
    };

    auto* sizeSlider = makePctSlider(30, 250, qRound(brandingSnapshot.logoScale * 100.0));
    auto* sizeSpin = makePctSpin(30, 250, sizeSlider->value());
    makeSliderRow(QString::fromUtf8("Розмір"), sizeSlider, sizeSpin);

    auto* contrastSlider = makePctSlider(0, 200, qRound(brandingSnapshot.logoContrast * 100.0));
    auto* contrastSpin = makePctSpin(0, 200, contrastSlider->value());
    makeSliderRow(QString::fromUtf8("Контрастність"), contrastSlider, contrastSpin);

    auto* saturationSlider = makePctSlider(0, 200, qRound(brandingSnapshot.logoSaturation * 100.0));
    auto* saturationSpin = makePctSpin(0, 200, saturationSlider->value());
    makeSliderRow(QString::fromUtf8("Колір"), saturationSlider, saturationSpin);

    auto* opacitySlider = makePctSlider(5, 100, qRound(brandingSnapshot.logoOpacity * 100.0));
    auto* opacitySpin = makePctSpin(5, 100, opacitySlider->value());
    makeSliderRow(QString::fromUtf8("Прозорість"), opacitySlider, opacitySpin);

    l->addWidget(paramsPanel);

    auto updatePreview = [preview, logoPath, sizeSlider, contrastSlider, saturationSlider, opacitySlider]() {
        preview->setLogoPath(logoPath->text().trimmed());
        preview->setLogoScale(sizeSlider->value() / 100.0);
        preview->setLogoVisuals(contrastSlider->value() / 100.0,
                                saturationSlider->value() / 100.0,
                                opacitySlider->value() / 100.0);
    };

    bindPctControls(sizeSlider, sizeSpin, QStringLiteral("settings.branding.logo_scale"), [preview](int v) {
        preview->setLogoScale(v / 100.0);
    });
    bindPctControls(contrastSlider, contrastSpin, QStringLiteral("settings.branding.logo_contrast"), [preview, saturationSlider, opacitySlider](int v) {
        preview->setLogoVisuals(v / 100.0, saturationSlider->value() / 100.0, opacitySlider->value() / 100.0);
    });
    bindPctControls(saturationSlider, saturationSpin, QStringLiteral("settings.branding.logo_saturation"), [preview, contrastSlider, opacitySlider](int v) {
        preview->setLogoVisuals(contrastSlider->value() / 100.0, v / 100.0, opacitySlider->value() / 100.0);
    });
    bindPctControls(opacitySlider, opacitySpin, QStringLiteral("settings.branding.logo_opacity"), [preview, contrastSlider, saturationSlider](int v) {
        preview->setLogoVisuals(contrastSlider->value() / 100.0, saturationSlider->value() / 100.0, v / 100.0);
    });

    updatePreview();
    l->addStretch(1);

    QObject::connect(name, &QLineEdit::editingFinished, w, [name, phone]() {
        BrandingSettingsService::saveNamePhone(name->text().trimmed(), phone->text().trimmed());
    });
    QObject::connect(phone, &QLineEdit::editingFinished, w, [name, phone]() {
        BrandingSettingsService::saveNamePhone(name->text().trimmed(), phone->text().trimmed());
    });
    QObject::connect(logoList, &QListWidget::currentItemChanged, w,
                     [logoPath, updatePreview, removeLogo](QListWidgetItem* current, QListWidgetItem*) {
        const bool hasCurrent = (current != nullptr);
        removeLogo->setEnabled(hasCurrent);
        if (!hasCurrent) {
            logoPath->clear();
            BrandingSettingsService::saveLogoPath(QString());
            updatePreview();
            return;
        }
        const QString path = current->data(Qt::UserRole).toString().trimmed();
        logoPath->setText(path);
        BrandingSettingsService::saveLogoPath(path);
        updatePreview();
    });
    removeLogo->setEnabled(logoList->currentItem() != nullptr);

    QObject::connect(logoPath, &QLineEdit::editingFinished, w, [logoPath, refreshLogoList, updatePreview, publishBrandingSettingsBackupEvent]() {
        QString path = logoPath->text().trimmed();
        if (path.isEmpty()) {
            BrandingSettingsService::saveLogoPath(QString());
            updatePreview();
            return;
        }
        QFileInfo fi(path);
        if (!fi.exists() || !fi.isFile()) {
            path = QDir::fromNativeSeparators(path);
            logoPath->setText(path);
            BrandingSettingsService::saveLogoPath(path);
            updatePreview();
            return;
        }
        path = fi.canonicalFilePath().isEmpty() ? fi.absoluteFilePath() : fi.canonicalFilePath();
        logoPath->setText(path);
        QStringList logos = BrandingAssetsSettingsService::loadLogoListWithActive(logoPath->text().trimmed());
        logos.prepend(path);
        logos = BrandingAssetsSettingsService::sanitizeLogoList(logos);
        bool changed = false;
        BrandingAssetsSettingsService::saveLogoList(logos, &changed);
        if (changed) {
            publishBrandingSettingsBackupEvent(QString::fromUtf8("Оновлено список логотипів бренду"));
        }
        BrandingSettingsService::saveLogoPath(path);
        refreshLogoList(logos);
        updatePreview();
    });
    QObject::connect(browse, &QPushButton::clicked, w, [owner, logoPath, refreshLogoList, updatePreview, publishBrandingSettingsBackupEvent]() {
        const QStringList files = QFileDialog::getOpenFileNames(
            owner,
            QString::fromUtf8("Додавання логотипів"),
            QString(),
            QString::fromUtf8("Зображення (*.png *.jpg *.jpeg)"));
        if (files.isEmpty()) return;

        QStringList logos = BrandingAssetsSettingsService::loadLogoListWithActive(logoPath->text().trimmed());
        QString lastAdded;
        for (const QString& f : files) {
            const QString dst = BrandingAssetsSettingsService::copyLogoToDataDir(f);
            if (dst.isEmpty()) continue;
            logos.prepend(dst);
            lastAdded = dst;
        }
        logos = BrandingAssetsSettingsService::sanitizeLogoList(logos);
        bool changed = false;
        BrandingAssetsSettingsService::saveLogoList(logos, &changed);
        if (changed) {
            publishBrandingSettingsBackupEvent(QString::fromUtf8("Оновлено список логотипів бренду"));
        }
        if (!lastAdded.isEmpty()) {
            logoPath->setText(lastAdded);
            BrandingSettingsService::saveLogoPath(lastAdded);
        }
        refreshLogoList(logos);
        updatePreview();
    });
    QObject::connect(removeLogo, &QPushButton::clicked, w, [logoList, logoPath, refreshLogoList, updatePreview, publishBrandingSettingsBackupEvent]() {
        auto* current = logoList->currentItem();
        if (!current) return;
        const QString path = current->data(Qt::UserRole).toString().trimmed();

        QStringList logos = BrandingAssetsSettingsService::loadLogoListWithActive(logoPath->text().trimmed());
        logos.removeAll(path);
        logos = BrandingAssetsSettingsService::sanitizeLogoList(logos);
        bool changed = false;
        BrandingAssetsSettingsService::saveLogoList(logos, &changed);
        if (changed) {
            publishBrandingSettingsBackupEvent(QString::fromUtf8("Оновлено список логотипів бренду"));
        }

        BrandingAssetsSettingsService::removeManagedLogoFile(path);

        QString nextActive;
        if (!logos.isEmpty()) nextActive = logos.constFirst();
        logoPath->setText(nextActive);
        BrandingSettingsService::saveLogoPath(nextActive);
        refreshLogoList(logos);
        updatePreview();
    });
    auto saveBrandingState = [name, phone, logoPath, sizeSlider, contrastSlider, saturationSlider, opacitySlider, publishBrandingSettingsBackupEvent, queueRefreshAll]() {
        BrandingSettingsService::BrandingSavePayload payload;
        payload.name = name->text().trimmed();
        payload.phone = phone->text().trimmed();
        payload.logoPath = logoPath->text().trimmed();
        const auto snapshot = BrandingSettingsService::loadSnapshot();
        payload.logoPosX = snapshot.logoPosX;
        payload.logoPosY = snapshot.logoPosY;
        payload.logoScale = sizeSlider->value() / 100.0;
        payload.logoContrast = contrastSlider->value() / 100.0;
        payload.logoSaturation = saturationSlider->value() / 100.0;
        payload.logoOpacity = opacitySlider->value() / 100.0;

        bool changed = false;
        BrandingSettingsService::save(payload, &changed);
        if (changed) {
            publishBrandingSettingsBackupEvent(QString::fromUtf8("Збережено параметри брендування і логотипу"));
        }
        if (queueRefreshAll) queueRefreshAll();
    };
    QObject::connect(btnSaveTop, &QPushButton::clicked, w, saveBrandingState);

    return w;
}