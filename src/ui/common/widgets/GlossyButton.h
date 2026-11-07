#pragma once

#include <QPushButton>
#include <QColor>

class GlossyButton : public QPushButton
{
    Q_OBJECT
public:
    enum class VisualStyle {
        DefaultDark,
        CatalogMetal
    };

    explicit GlossyButton(const QString& text, QWidget* parent=nullptr);

    void setVisualStyle(VisualStyle style);
    VisualStyle visualStyle() const { return m_visualStyle; }

protected:
    void paintEvent(QPaintEvent* e) override;

private:
    QColor m_textColor;
    QColor m_outlineColor;
    VisualStyle m_visualStyle = VisualStyle::DefaultDark;
};
