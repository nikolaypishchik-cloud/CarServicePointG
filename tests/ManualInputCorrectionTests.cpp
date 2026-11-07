#include <QtTest>
#include <QApplication>
#include <QTemporaryDir>
#include <QSettings>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QTextEdit>
#include <QDateEdit>
#include <QDateTimeEdit>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QLabel>
#include <QComboBox>
#include <QTableWidget>
#include <QClipboard>
#include <QElapsedTimer>
#include <QInputMethodEvent>
#include <QInputDialog>
#include <QMenu>
#include <QFontDatabase>
#include "core/util/ManualInputNormalizer.h"
#include "ui/common/filters/ManualInputCorrectionFilter.h"
#include "core/util/VehicleNameUtils.h"
#include "ui/tabs/schedule/PlanningPageSupport.h"
#include "ui/common/filters/WindowsContextMenuFilter.h"

using Role=ManualInputNormalizer::Role;
class ManualInputCorrectionTests:public QObject {
    Q_OBJECT
    QTemporaryDir dir;
    ManualInputCorrectionFilter filter;
private slots:
    void unifiedPopupAppearance(){
        QFontDatabase::addApplicationFont(qEnvironmentVariable("WINDIR")+"/Fonts/times.ttf");
        const QFont previousFont=qApp->font();qApp->setFont(QFont("Times New Roman",12));
        WindowsContextMenuFilter menus;qApp->installEventFilter(&menus);
        QWidget host;host.setStyleSheet("QMenu { background: red; color: black; }");
        QMenu menu(&host);menu.setStyleSheet("QMenu { background: white; }");
        auto* open=menu.addAction(QString::fromUtf8("Відкрити наряд"));
        auto* copy=menu.addAction(QString::fromUtf8("Копіювати"));copy->setShortcut(QKeySequence::Copy);
        menu.addAction(QString::fromUtf8("Виділити все"))->setShortcut(QKeySequence::SelectAll);
        auto* disabled=menu.addAction(QString::fromUtf8("Видалити наряд"));disabled->setEnabled(false);
        auto* sub=menu.addMenu(QString::fromUtf8("Автовиправлення"));sub->addAction(QString::fromUtf8("Виправити зараз"));
        menu.popup(QPoint(20,20));QApplication::processEvents();
        QVERIFY(menu.styleSheet().contains("border-radius: 14px"));
        QVERIFY(menu.actionGeometry(open).height()>=46);
        QVERIFY(menu.actionGeometry(copy).width()>QFontMetrics(menu.font()).horizontalAdvance(copy->text()+"Ctrl+C"));
        QCOMPARE(copy->shortcut(),QKeySequence(QKeySequence::Copy));QVERIFY(!disabled->isEnabled());
        const auto screenshot=qEnvironmentVariable("CS_MENU_SCREENSHOT");
        if(!screenshot.isEmpty())QVERIFY(menu.grab().save(screenshot));
        sub->popup(QPoint(450,20));QApplication::processEvents();QCOMPARE(sub->styleSheet(),menu.styleSheet());sub->close();menu.close();
        menu.setStyleSheet("QMenu { background: red; }");menu.popup(QPoint(20,20));QApplication::processEvents();
        QVERIFY(menu.styleSheet().contains("background: #414956"));menu.close();
        qApp->removeEventFilter(&menus);
        qApp->setFont(previousFont);
    }
    void uniqueVehicleOptions(){
        const QStringList input={"A6 Allroad A6 Allroad"," a6 allroad ","A6 Avant","Model 3 Model 3","Model 3","Golf Golf","Golf GTI",""};
        const auto result=VehicleNameUtils::uniqueModelOptions(input);
        QCOMPARE(result,QStringList({"A6 Allroad","A6 Avant","Golf","Golf GTI","Model 3"}));
        QCOMPARE(VehicleNameUtils::uniqueModelOptions(result),result);
    }
    void initTestCase(){QCoreApplication::setOrganizationName("CarServiceTests");QCoreApplication::setApplicationName("ManualInput-"+QUuid::createUuid().toString(QUuid::Id128));QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,dir.path());qApp->installEventFilter(&filter);}
    void cleanupTestCase(){qApp->removeEventFilter(&filter);}
    void examples_data(){
        QTest::addColumn<QString>("input");QTest::addColumn<int>("role");QTest::addColumn<QString>("expected");
        auto add=[](const char* name,const QString& input,Role r,const QString& expected){QTest::newRow(name)<<input<<int(r)<<expected;};
        add("plate-latin","aa1122ba",Role::Plate,QString::fromUtf8("АА 1122 ВА"));
        add("plate-mixed",QString::fromUtf8("аa-1122-вa"),Role::Plate,QString::fromUtf8("АА 1122 ВА"));
        add("foreign-plate","WOB AB 1234",Role::Plate,"WOB AB 1234");
        add("partial-plate","AA 12",Role::Plate,"AA 12");
        add("date-short","1/2/26",Role::Date,"01.02.26");
        add("date-iso","2026-10-28",Role::Date,"28.10.26");
        add("date-digits","28102026",Role::Date,"28.10.26");
        add("date-6digits","281026",Role::Date,"28.10.26");
        add("date-leap","29.02.24",Role::Date,"29.02.24");
        add("date-invalid","31.02.26",Role::Date,"31.02.26");
        add("date-old-century","28.10.1999",Role::Date,"28.10.1999");
        add("vehicle-ua",QString::fromUtf8("ауді ку7 3.0 тді"),Role::Vehicle,"AUDI Q7 3.0 TDI");
        add("vehicle-ru",QString::fromUtf8("фольцваген гольф"),Role::Vehicle,"VOLKSWAGEN Golf");
        add("vehicle-layout",QString::fromUtf8("фгвш"),Role::Vehicle,"AUDI");
        add("vehicle-no-invention","Audi A6 2.0 TDI 2015",Role::Vehicle,"AUDI A6 2.0 TDI 2015");
        add("vehicle-nissan",QString::fromUtf8("ніссан кашкай"),Role::Vehicle,"NISSAN Qashqai");
        add("vehicle-renault",QString::fromUtf8("рено трафік"),Role::Vehicle,"RENAULT Trafic");
        add("vehicle-toyota",QString::fromUtf8("тойота корола"),Role::Vehicle,"TOYOTA Corolla");
        add("vehicle-vw",QString::fromUtf8("фольксваген пасат"),Role::Vehicle,"VOLKSWAGEN Passat");
        add("oil-ru",QString::fromUtf8("масло моторное 5в40"),Role::Technical,QString::fromUtf8("Олива моторна 5W40"));
        add("oil-reverse",QString::fromUtf8("моторное масло 0 w - 20"),Role::Technical,QString::fromUtf8("Олива моторна 0W20"));
        add("oil-layout",QString::fromUtf8("олива моторна 5ц40"),Role::Technical,QString::fromUtf8("Олива моторна 5W40"));
        add("oil-gear",QString::fromUtf8("олива 75в90"),Role::Technical,QString::fromUtf8("Олива 75W90"));
        add("oil-typo",QString::fromUtf8("фільтер оливний"),Role::Technical,QString::fromUtf8("Фільтр оливний"));
        add("layout-oil","vfckj",Role::Technical,QString::fromUtf8("Олива"));
        add("protected","VF1ABC00012345678",Role::Protected,"VF1ABC00012345678");
        add("search",QString::fromUtf8("масло 5в40"),Role::Search,QString::fromUtf8("масло 5в40"));
    }
    void examples(){QFETCH(QString,input);QFETCH(int,role);QFETCH(QString,expected);const auto r=Role(role);auto result=ManualInputNormalizer::normalize(input,r);QCOMPARE(result.text,expected);QCOMPARE(ManualInputNormalizer::normalize(result.text,r).text,result.text);}
    void preservedIdentifiersAndMeaning(){
        const auto value=QString::fromUtf8("Не заменять AB-1234, VIN WBA12345678901234. Проверить https://example.com/масло?x=1 и name@example.com\n  Проверить давление 2.5 бар");
        const auto result=ManualInputNormalizer::normalize(value,Role::Text).text;
        QVERIFY(result.startsWith(QString::fromUtf8("Не ")));for(const auto& code:QStringList{"AB-1234","WBA12345678901234","https://example.com/масло?x=1","name@example.com","2.5"})QVERIFY2(result.contains(code),qPrintable(result));QVERIFY(result.contains("\n  "));
        const auto suggestions=ManualInputNormalizer::suggestions(QString::fromUtf8("амортизатро"),Role::Technical);QVERIFY(suggestions.contains(QString::fromUtf8("амортизатор")));
        QCOMPARE(ManualInputNormalizer::normalize(QString::fromUtf8("амортизатро"),Role::Text).text,QString::fromUtf8("Амортизатро"));
    }
    void customRules(){QMap<QString,QString> rules{{QString::fromUtf8("флж"),QString::fromUtf8("Фільтр паливний")}};QCOMPARE(ManualInputNormalizer::normalize(QString::fromUtf8("флж"),Role::Technical,rules).text,QString::fromUtf8("Фільтр паливний"));}
    void semanticFields(){
        QCOMPARE(ManualInputNormalizer::roleForHint(QString::fromUtf8("Примітка майстра")),Role::Text);
        QCOMPARE(ManualInputNormalizer::roleForHint("Plate endpoint"),Role::Protected);
        QCOMPARE(ManualInputNormalizer::roleForHint(QString::fromUtf8("Бренд")),Role::Protected);
        QWidget host;QFormLayout form(&host);QLineEdit plate,vin,car,note,phone;form.addRow(QString::fromUtf8("Державний номер"),&plate);form.addRow("VIN",&vin);form.addRow(QString::fromUtf8("Марка / модель"),&car);form.addRow(QString::fromUtf8("Коментар"),&note);form.addRow(QString::fromUtf8("Телефон"),&phone);
        QCOMPARE(ManualInputCorrectionFilter::roleFor(&plate),Role::Plate);QCOMPARE(ManualInputCorrectionFilter::roleFor(&vin),Role::Protected);QCOMPARE(ManualInputCorrectionFilter::roleFor(&car),Role::Vehicle);QCOMPARE(ManualInputCorrectionFilter::roleFor(&note),Role::Text);QCOMPARE(ManualInputCorrectionFilter::roleFor(&phone),Role::Protected);
        QWidget gridHost;QGridLayout grid(&gridHost);QLineEdit material,article;grid.addWidget(new QLabel(QString::fromUtf8("Матеріал")),0,0);grid.addWidget(&material,1,0);grid.addWidget(new QLabel(QString::fromUtf8("Артикул")),0,1);grid.addWidget(&article,1,1);QCOMPARE(ManualInputCorrectionFilter::roleFor(&material),Role::Technical);QCOMPARE(ManualInputCorrectionFilter::roleFor(&article),Role::Protected);
    }
    void manualOnlyUndoAndPaste(){
        QWidget host;QFormLayout form(&host);QLineEdit edit,other;form.addRow(QString::fromUtf8("Матеріал"),&edit);form.addRow("Other",&other);host.show();edit.setFocus();QApplication::processEvents();
        const auto original=QString::fromUtf8("масло моторное 5в40");edit.setText(original);other.setFocus();QApplication::processEvents();QCOMPARE(edit.text(),original); // DB/programmatic loads are untouched.
        edit.setFocus();edit.selectAll();QApplication::clipboard()->setText(original);QTest::keyClick(&edit,Qt::Key_V,Qt::ControlModifier);QTest::keyClick(&edit,Qt::Key_Tab);QCOMPARE(edit.text(),QString::fromUtf8("Олива моторна 5W40"));
        edit.setFocus();QTest::keyClick(&edit,Qt::Key_Z,Qt::ControlModifier);QCOMPARE(edit.text(),original);other.setFocus();QApplication::processEvents();QCOMPARE(edit.text(),original);
        edit.setFocus();edit.selectAll();QTest::keyClicks(&edit,"vfckj ");QApplication::processEvents();QCOMPARE(edit.text(),QString::fromUtf8("Олива "));
    }
    void richAndPlainAndIme(){
        QWidget host;QFormLayout form(&host);QPlainTextEdit plain;QTextEdit rich;QLineEdit other;form.addRow(QString::fromUtf8("Рекомендації"),&plain);form.addRow(QString::fromUtf8("Примітка"),&rich);form.addRow("Other",&other);host.show();plain.setFocus();QApplication::processEvents();
        QApplication::clipboard()->setText(QString::fromUtf8("проверить давление\n  заменить фильтр"));QTest::keyClick(&plain,Qt::Key_V,Qt::ControlModifier);other.setFocus();QApplication::processEvents();QVERIFY(plain.toPlainText().contains(QString::fromUtf8("Перевірити тиск")));QVERIFY(plain.toPlainText().contains("\n  "));
        rich.setHtml("<b>AB-1234</b> ");rich.moveCursor(QTextCursor::End);rich.setFocus();QApplication::processEvents();QApplication::clipboard()->setText(QString::fromUtf8("проверить"));QTest::keyClick(&rich,Qt::Key_V,Qt::ControlModifier);other.setFocus();QApplication::processEvents();QVERIFY(rich.toPlainText().contains(QString::fromUtf8("перевірити"),Qt::CaseInsensitive));auto c=rich.textCursor();c.setPosition(0);c.setPosition(2,QTextCursor::KeepAnchor);QVERIFY(c.charFormat().fontWeight()>=QFont::Bold);
        QLineEdit ime;ime.setProperty("inputCorrectionRole","technical");ime.show();ime.setFocus();QApplication::processEvents();QInputMethodEvent preedit(QString::fromUtf8("масло"),{});QApplication::sendEvent(&ime,&preedit);QVERIFY(!ManualInputCorrectionFilter::correct(&ime,true));QInputMethodEvent commit;commit.setCommitString(QString::fromUtf8("масло 5в40"));QApplication::sendEvent(&ime,&commit);QVERIFY(ManualInputCorrectionFilter::correct(&ime));QVERIFY(ime.text().contains("5W40"));
    }
    void datesAndSecrets(){
        QDateEdit date(QDate(2026,10,28));date.setDisplayFormat("dd.MM.yyyy");date.show();QApplication::processEvents();QCOMPARE(date.displayFormat(),QString("dd.MM.yy"));QCOMPARE(date.date(),QDate(2026,10,28));
        date.activateWindow();date.setFocus();QApplication::processEvents();date.setSelectedSection(QDateTimeEdit::DaySection);QTest::keyClicks(&date,"29");date.interpretText();QCOMPARE(date.date(),QDate(2026,10,29));
        QDateTimeEdit stamp(QDateTime(QDate(2026,10,28),QTime(12,45)));stamp.setDisplayFormat("yyyy-MM-dd HH:mm:ss.zzz");stamp.show();QApplication::processEvents();QCOMPARE(stamp.displayFormat(),QString("dd.MM.yy HH:mm:ss.zzz"));QCOMPARE(stamp.time(),QTime(12,45));
        QLineEdit password;password.setEchoMode(QLineEdit::Password);password.setText("vfckj");QVERIFY(!ManualInputCorrectionFilter::correct(&password,true));QCOMPARE(password.text(),QString("vfckj"));
        QDoubleSpinBox amount;amount.setMaximum(1000000);amount.setValue(500);amount.show();QApplication::processEvents();auto* line=amount.findChild<QLineEdit*>();QVERIFY(line);QCOMPARE(ManualInputCorrectionFilter::roleFor(line),Role::Protected);QCOMPARE(amount.value(),500.0);
    }
    void planningAndPersonalRulesStayStable(){
        const auto source=QString::fromUtf8("ауді ку7 aa1122ba | заменить фильтр | Майстер ID: MASTER-Case-1");
        const auto r=ManualInputNormalizer::normalize(source,Role::Planning).text;
        QVERIFY2(r.contains("AUDI Q7"),qPrintable(r));QVERIFY(r.contains(QString::fromUtf8("АА 1122 ВА")));QVERIFY(r.endsWith(QString::fromUtf8(" | Майстер ID: MASTER-Case-1")));
        QCOMPARE(ManualInputNormalizer::normalize(r,Role::Planning).text,r);
        QMap<QString,QString> rules{{QString::fromUtf8("олива"),QString::fromUtf8("Олива моторна")}};
        const auto expanded=ManualInputNormalizer::normalize(QString::fromUtf8("олива"),Role::Text,rules).text;
        QCOMPARE(expanded,QString::fromUtf8("Олива моторна"));QCOMPARE(ManualInputNormalizer::normalize(expanded,Role::Text,rules).text,expanded);
        rules={{QString::fromUtf8("флж"),QStringLiteral("\\1")}};
        QCOMPARE(ManualInputNormalizer::normalize(QString::fromUtf8("флж"),Role::Text,rules).text,QStringLiteral("\\1"));
    }
    void planningAbbreviationsAndVehicleOrder_data(){
        QTest::addColumn<QString>("input");QTest::addColumn<QString>("expected");
        QTest::newRow("upper") << QString::fromUtf8("ТО ГУР ДВЗ ГРМ АКПП ТНВД ABS") << QString::fromUtf8("ТО ГУР ДВЗ ГРМ АКПП ТНВД ABS");
        QTest::newRow("lower") << QString::fromUtf8("то гур двз грм акпп тнвд abs") << QString::fromUtf8("ТО ГУР ДВЗ ГРМ АКПП ТНВД ABS");
        QTest::newRow("vehicle-first") << QString::fromUtf8("ауді ку7 на проведеня ТО") << QString::fromUtf8("AUDI Q7 на проведення ТО");
        QTest::newRow("vehicle-last") << QString::fromUtf8("замена масла ауді ку7") << QString::fromUtf8("Заміна оливи AUDI Q7");
        QTest::newRow("work-before-bmw") << QString::fromUtf8("ремонт ГУР бмв х5") << QString::fromUtf8("Ремонт ГУР BMW X5");
        QTest::newRow("negation") << QString::fromUtf8("не заменять ГРМ, проверить ДВЗ") << QString::fromUtf8("Не замінювати ГРМ, перевірити ДВЗ");
        QTest::newRow("punctuation") << QString::fromUtf8("перевірити гур, двз; ABS/ESP") << QString::fromUtf8("Перевірити ГУР, ДВЗ; ABS/ESP");
        QTest::newRow("word-boundaries") << QString::fromUtf8("мотор, готово, потом") << QString::fromUtf8("Мотор, готово, потом");
        QTest::newRow("plate") << QString::fromUtf8("ке8571ар ауді ку7 на проведеня то") << QString::fromUtf8("КЕ 8571 АР AUDI Q7 на проведення ТО");
        QTest::newRow("unknown-acronym") << QString::fromUtf8("перевірити ADAS") << QString::fromUtf8("Перевірити ADAS");
    }
    void planningAbbreviationsAndVehicleOrder(){
        QFETCH(QString,input);QFETCH(QString,expected);
        const auto actual=ManualInputNormalizer::normalize(input,Role::Planning).text;
        QCOMPARE(actual,expected);
        QCOMPARE(ManualInputNormalizer::normalize(actual,Role::Planning).text,actual);
        QCOMPARE(PlanningPageSupport::normalizePlanningSlotText(actual),actual);
    }
    void planningWordByWord(){
        QLineEdit edit;edit.setProperty("inputCorrectionRole","planning");edit.show();edit.setFocus();QApplication::processEvents();
        for(const auto& word:QString::fromUtf8("замена масла ауді ку7 то гур двз ").split(' ',Qt::SkipEmptyParts)){
            QInputMethodEvent input;input.setCommitString(word+" ");QApplication::sendEvent(&edit,&input);QApplication::processEvents();
        }
        QTest::keyClick(&edit,Qt::Key_Return);QApplication::processEvents();
        QCOMPARE(edit.text().trimmed(),QString::fromUtf8("Заміна оливи AUDI Q7 ТО ГУР ДВЗ"));
        const auto once=edit.text();ManualInputCorrectionFilter::correct(&edit,true);QCOMPARE(edit.text(),once);
        // Editing work before an already entered vehicle must retain the suffix.
        edit.setCursorPosition(0);QInputMethodEvent insert;insert.setCommitString(QString::fromUtf8("ТО, "));QApplication::sendEvent(&edit,&insert);
        QTest::keyClick(&edit,Qt::Key_Return);QApplication::processEvents();
        QVERIFY(edit.text().startsWith(QString::fromUtf8("ТО, ")));QVERIFY(edit.text().contains("AUDI Q7"));
    }
    void editableComboAndRefresh(){
        QWidget host;QFormLayout form(&host);QComboBox combo;combo.setEditable(true);QLineEdit other;
        form.addRow(QString::fromUtf8("Матеріал"),&combo);form.addRow("Other",&other);host.show();auto* edit=combo.lineEdit();edit->setFocus();QApplication::processEvents();
        QApplication::clipboard()->setText(QString::fromUtf8("масло 5в40"));QTest::keyClick(edit,Qt::Key_V,Qt::ControlModifier);other.setFocus();QApplication::processEvents();QCOMPARE(combo.currentText(),QString::fromUtf8("Олива 5W40"));
        edit->setFocus();edit->selectAll();QTest::keyClicks(edit,"vfckj");edit->setText(QString::fromUtf8("масло из БД"));other.setFocus();QApplication::processEvents();QCOMPARE(edit->text(),QString::fromUtf8("масло из БД"));
    }
    void tableDelegateCommit(){
        QTableWidget table(1,1);table.setHorizontalHeaderLabels({QString::fromUtf8("Запчастина / матеріал")});table.setItem(0,0,new QTableWidgetItem(""));table.show();table.setCurrentCell(0,0);table.editItem(table.item(0,0));QApplication::processEvents();auto* edit=table.findChild<QLineEdit*>();QVERIFY(edit);
        QApplication::clipboard()->setText(QString::fromUtf8("масло моторное 5в40"));QTest::keyClick(edit,Qt::Key_V,Qt::ControlModifier);QTest::keyClick(edit,Qt::Key_Return);QApplication::processEvents();QCOMPARE(table.item(0,0)->text(),QString::fromUtf8("Олива моторна 5W40"));
    }
    void personalDictionaryMenu(){
        QLineEdit edit;edit.setProperty("inputCorrectionRole","technical");edit.setText(QString::fromUtf8("флж"));edit.selectAll();
        QMenu menu;ManualInputCorrectionFilter::appendMenu(menu,&edit);QVERIFY(!menu.actions().isEmpty());auto* sub=menu.actions().first()->menu();QVERIFY(sub);
        QAction* remember=nullptr;for(auto* a:sub->actions())if(a->text().startsWith(QString::fromUtf8("Запам’ятати")))remember=a;QVERIFY(remember);
        QTimer::singleShot(0,[]{for(auto* w:QApplication::topLevelWidgets())if(auto* d=qobject_cast<QInputDialog*>(w)){d->setTextValue(QString::fromUtf8("Фільтр паливний"));d->accept();}});
        remember->trigger();QCOMPARE(edit.text(),QString::fromUtf8("Фільтр паливний"));
        QLineEdit another;another.setProperty("inputCorrectionRole","technical");another.setText(QString::fromUtf8("флж"));QVERIFY(ManualInputCorrectionFilter::correct(&another,true));QCOMPARE(another.text(),edit.text());
        edit.setText(QString::fromUtf8("флж"));edit.selectAll();QMenu forgetMenu;ManualInputCorrectionFilter::appendMenu(forgetMenu,&edit);
        QAction* forget=nullptr;for(auto* a:forgetMenu.actions().first()->menu()->actions())if(a->text().startsWith(QString::fromUtf8("Забути")))forget=a;QVERIFY(forget);forget->trigger();
        another.setText(QString::fromUtf8("флж"));ManualInputCorrectionFilter::correct(&another,true);QCOMPARE(another.text(),QString::fromUtf8("Флж"));
        const auto screenshot=qEnvironmentVariable("CS_INPUT_SCREENSHOT");if(!screenshot.isEmpty()){
            QFontDatabase::addApplicationFont(qEnvironmentVariable("WINDIR")+"/Fonts/times.ttf");sub->setFont(QFont("Times New Roman",12));sub->popup(QPoint(30,30));QApplication::processEvents();QVERIFY(sub->grab().save(screenshot));sub->hide();
        }
    }
    void responsiveness(){
        QList<qint64> times;for(int i=0;i<20;++i){QElapsedTimer t;t.start();auto r=ManualInputNormalizer::normalize(QString::fromUtf8("ауді ку7 3.0 тді 2015"),Role::Vehicle);QVERIFY(!r.text.contains("quattro"));times.append(t.nsecsElapsed()/1000);}
        std::sort(times.begin(),times.end());qInfo()<<"vehicle correction microseconds median/p95/max"<<times[10]<<times[18]<<times[19];QVERIFY(times[18]<100000);
    }
};
QTEST_MAIN(ManualInputCorrectionTests)
#include "ManualInputCorrectionTests.moc"
