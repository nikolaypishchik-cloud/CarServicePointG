#pragma once

#include <QString>
#include <QRegularExpression>

namespace UkrainianDialogText {
inline QString translate(QString text)
{
    const QList<QPair<QString, QString>> phrases = {
        {"NOT NULL constraint failed:", QString::fromUtf8("Не вдалося зберегти запис: відсутнє значення обов'язкового поля:")},
        {"UNIQUE constraint failed:", QString::fromUtf8("Такий запис уже існує. Повторюється унікальне поле:")},
        {"FOREIGN KEY constraint failed", QString::fromUtf8("Не вдалося змінити запис: порушено зв'язок із пов'язаними даними.")},
        {"CHECK constraint failed:", QString::fromUtf8("Значення не відповідає правилам перевірки:")},
        {"database is locked", QString::fromUtf8("База даних зайнята іншою операцією. Спробуйте ще раз.")},
        {"database table is locked", QString::fromUtf8("Таблиця бази даних зайнята. Спробуйте ще раз.")},
        {"attempt to write a readonly database", QString::fromUtf8("База даних доступна лише для читання.")},
        {"unable to open database file", QString::fromUtf8("Не вдалося відкрити файл бази даних.")},
        {"database disk image is malformed", QString::fromUtf8("Файл бази даних пошкоджено. Потрібне відновлення з резервної копії.")},
        {"disk I/O error", QString::fromUtf8("Помилка читання або запису на диск.")},
        {"no such table:", QString::fromUtf8("У базі даних немає таблиці:")},
        {"no such column:", QString::fromUtf8("У базі даних немає поля:")},
        {"Unable to fetch row", QString::fromUtf8("Не вдалося обробити запис.")},
        {"Unable to execute statement", QString::fromUtf8("Не вдалося виконати запит.")},
        {"Unable to execute multiple statements at a time", QString::fromUtf8("Не вдалося виконати кілька запитів одночасно.")},
        {"Parameter count mismatch", QString::fromUtf8("Кількість параметрів запиту не збігається.")},
        {"SSL handshake failed", QString::fromUtf8("Не вдалося встановити захищене з'єднання із сервером.")},
        {"Authentication required", QString::fromUtf8("Потрібно ввійти до облікового запису.")},
        {"Invalid credentials", QString::fromUtf8("Неправильні дані для входу.")},
        {"Unauthorized", QString::fromUtf8("Потрібна авторизація.")},
        {"Forbidden", QString::fromUtf8("Недостатньо прав для цієї операції.")},
        {"Driver not loaded", QString::fromUtf8("Драйвер бази даних не завантажено.")},
        {"Connection refused", QString::fromUtf8("Сервер відхилив з'єднання.")},
        {"Host not found", QString::fromUtf8("Сервер не знайдено. Перевірте адресу та підключення до мережі.")},
        {"Connection timed out", QString::fromUtf8("Час очікування з'єднання минув.")},
        {"Operation timed out", QString::fromUtf8("Час очікування операції минув.")},
        {"Network is unreachable", QString::fromUtf8("Мережа недоступна.")},
        {"Remote host closed the connection", QString::fromUtf8("Сервер закрив з'єднання.")},
        {"Permission denied", QString::fromUtf8("Недостатньо прав доступу.")},
        {"Access is denied", QString::fromUtf8("Доступ заборонено.")},
        {"No such file or directory", QString::fromUtf8("Файл або папку не знайдено.")},
        {"No space left on device", QString::fromUtf8("На диску немає вільного місця.")},
        {"This device is already registered to another identity", QString::fromUtf8("Цей пристрій уже прив'язаний до іншого користувача.")}
    };
    for (const auto& phrase : phrases) text.replace(phrase.first, phrase.second, Qt::CaseInsensitive);
    text.replace("vehicle_intake.pdf_path", QString::fromUtf8("шлях до PDF акта приймання"));
    return text;
}
inline QString title(const QString& text)
{
    const QString key = text.trimmed().toLower();
    if (key == "error" || key == QString::fromUtf8("ошибка")) return QString::fromUtf8("Помилка");
    if (key == "warning" || key == QString::fromUtf8("предупреждение")) return QString::fromUtf8("Попередження");
    if (key == "information" || key == QString::fromUtf8("информация")) return QString::fromUtf8("Інформація");
    if (key == "confirmation" || key == "confirm" || key == QString::fromUtf8("подтверждение")) return QString::fromUtf8("Підтвердження");
    return text;
}
}
