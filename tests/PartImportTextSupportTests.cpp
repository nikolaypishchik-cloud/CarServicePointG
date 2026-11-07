#include <QtTest/QtTest>
#include "TestSourceAssertions.h"
#include "core/services/orders/PartImportTextSupport.h"

class PartImportTextSupportTests final : public QObject {
    Q_OBJECT
private slots:
    void criticalRegressionGuardsRemainWired();
    void professionalRuUaTranslatorCoversWorkshopTerms();
    void recommendations() {
        QCOMPARE(PartImportTextSupport::normalizeRecommendations(QString::fromUtf8("кожні 200 км перевіряти роботу омивача. через 1000 км це вже можна не делать.")),
            QString::fromUtf8("Кожні 200 км перевіряти роботу омивача. Через 1000 км це вже можна не робити."));
        QCOMPARE(PartImportTextSupport::normalizeRecommendations(QString::fromUtf8("перевирити 1.5 л оливи через 5 тис. км. замінити фільтр! перевіріти тиск? зробити огляд.")),
            QString::fromUtf8("Перевірити 1.5 л оливи через 5 тис. км. Замінити фільтр! Перевірити тиск? Зробити огляд."));
        QCOMPARE(PartImportTextSupport::normalizeRecommendations(QString::fromUtf8("Сделать техническое обслуживание через 8000км.\nПровести замену тормозных колодок через 5000км.")),
            QString::fromUtf8("Зробити технічне обслуговування через 8000 км.\nПровести заміну гальмівних колодок через 5000 км."));
        QCOMPARE(PartImportTextSupport::normalizeRecommendations(QString::fromUtf8("Техничне обслуговуваня\n\nELRING 150111")),
            QString::fromUtf8("Технічне обслуговування\n\nELRING 150111"));
    }
};

void PartImportTextSupportTests::criticalRegressionGuardsRemainWired()
{
    cs::tests::requireAnyContains("src/core/services/orders/PartImportTextSupport.cpp", {"translateTechnicalRuToUa", "normalize", "brand", "oem"});
    cs::tests::requireContains("src/core/services/orders/CurrentOrdersInvoiceImportService.cpp", "RAW_ROW");
}

void PartImportTextSupportTests::professionalRuUaTranslatorCoversWorkshopTerms()
{
    QCOMPARE(PartImportTextSupport::translateTechnicalRuToUa(QString::fromUtf8("стойка стабилизатора передняя")),
             QString::fromUtf8("Стійка стабілізатора передня"));
    QCOMPARE(PartImportTextSupport::translateTechnicalRuToUa(QString::fromUtf8("сайлентблок заднего рычага")),
             QString::fromUtf8("Сайлентблок заднього важеля"));
    QCOMPARE(PartImportTextSupport::translateTechnicalRuToUa(QString::fromUtf8("датчик положения коленвала")),
             QString::fromUtf8("Датчик положення колінчастого вала"));
    QCOMPARE(PartImportTextSupport::normalizePartName(QString::fromUtf8("масло моторное 5в40")),
             QString::fromUtf8("Олива моторна 5W40"));
}

QTEST_MAIN(PartImportTextSupportTests)
#include "PartImportTextSupportTests.moc"
