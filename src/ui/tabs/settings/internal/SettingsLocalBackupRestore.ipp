// Extracted from src/ui/tabs/settings/SettingsPageLocalSupport.cpp for P1/P2 monolith reduction.
// Included exactly once by the original translation unit/header; do not compile separately.

    static const QString allowed = QStringLiteral("0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ $%*+-./:");
    s = s.toUpper();
    QString out;
    out.reserve(s.size());
    bool prevSpace = false;
    for (const QChar ch : s) {
        if (allowed.contains(ch)) {
            if (ch == QChar(' ')) {
                if (prevSpace) continue;
                prevSpace = true;
            } else {
                prevSpace = false;
            }
            out += ch;
        } else if (!prevSpace) {
            out += QChar(' ');
            prevSpace = true;
        }
    }
    return out.trimmed();
}

static int qrAlnumValue(QChar ch)
{
    static const QString table = QStringLiteral("0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ $%*+-./:");
    return table.indexOf(ch);
}

static void qrAppendBits(QVector<bool>& bits, int value, int count)
{
    for (int i = count - 1; i >= 0; --i) bits.append(((value >> i) & 1) != 0);
}

struct SimpleQrSpec {
    int version = 4;
    int size = 33;
    int dataCodewords = 80;
    int eccCodewords = 20;
    int alignmentCenter = 26;
    int quiet = 5;
    int scale = 10;
};

static QString escapeWifiQrField(const QString& value)
{
    QString out = value;
    out.replace(QStringLiteral("\\"), QStringLiteral("\\\\"));
    out.replace(QStringLiteral(";"), QStringLiteral("\\;"));
    out.replace(QStringLiteral(","), QStringLiteral("\\,"));
    out.replace(QStringLiteral(":"), QStringLiteral("\\:"));
    return out;
}

struct WindowsWifiScanResult {
    QList<SyncSettingsService::WindowsWifiNetworkItem> networks;
};


static bool looksLikeMobileBluetoothName(const QString& rawName)
{
    const QString name = rawName.trimmed();
    if (name.isEmpty()) return false;
    const QString lower = name.toLower();
    const QStringList positive = {
        QStringLiteral("iphone"), QStringLiteral("ipad"), QStringLiteral("samsung"), QStringLiteral("galaxy"),
        QStringLiteral("redmi"), QStringLiteral("xiaomi"), QStringLiteral("mi "), QStringLiteral("poco"),
        QStringLiteral("huawei"), QStringLiteral("honor"), QStringLiteral("pixel"), QStringLiteral("oneplus"),
        QStringLiteral("motorola"), QStringLiteral("nokia"), QStringLiteral("realme"), QStringLiteral("oppo"),
        QStringLiteral("vivo"), QStringLiteral("phone"), QStringLiteral("mobile")
    };
    for (const QString& token : positive) {
        if (lower.contains(token)) return true;
    }
    const QStringList negative = {
        QStringLiteral("tv"), QStringLiteral("headset"), QStringLiteral("speaker"), QStringLiteral("audio"),
        QStringLiteral("soundbar"), QStringLiteral("watch"), QStringLiteral("printer"), QStringLiteral("mouse"),
        QStringLiteral("keyboard"), QStringLiteral("earbuds"), QStringLiteral("voombox")
    };
    for (const QString& token : negative) {
        if (lower.contains(token)) return false;
    }
    return false;
}

static int bluetoothDeviceUiPriority(const BluetoothSyncManager::DeviceItem& device)
{
    int score = 0;
    if (device.connected) score += 200;
    if (device.preferred) score += 140;
    if (device.paired) score += 80;
    if (looksLikeMobileBluetoothName(device.name)) score += 60;
    const QString lower = device.name.trimmed().toLower();
    if (lower.startsWith(QStringLiteral("bluetooth "))) score -= 20;
    if (lower.contains(QStringLiteral("tv")) || lower.contains(QStringLiteral("speaker")) || lower.contains(QStringLiteral("audio"))) score -= 40;
    return score;
}

static SimpleQrSpec chooseByteQrSpec(int payloadSize)
{
    // Screen-rendered QR must stay compact enough to fit without cropping.
    // Using a smaller integer module size keeps edges sharp on desktop displays.
    if (payloadSize <= 78) return {4, 33, 80, 20, 26, 4, 6};
    return {5, 37, 108, 26, 30, 4, 6};
}

static QPixmap makeScannableQrBytes(QByteArray payload)
{
    if (payload.isEmpty()) payload = QByteArrayLiteral("CS");
    SimpleQrSpec spec = chooseByteQrSpec(payload.size());
    const int maxPayloadSize = spec.version == 4 ? 78 : 106;
    if (payload.size() > maxPayloadSize) payload = payload.left(maxPayloadSize);

    QVector<bool> bits;
    bits.reserve(spec.dataCodewords * 8);
    qrAppendBits(bits, 0x4, 4); // Byte mode
    qrAppendBits(bits, payload.size(), 8);
    for (unsigned char byte : payload) qrAppendBits(bits, static_cast<int>(byte), 8);

    const int capacityBits = spec.dataCodewords * 8;
    const int terminator = qMin(4, capacityBits - bits.size());
    for (int i = 0; i < terminator; ++i) bits.append(false);
    while (bits.size() % 8 != 0) bits.append(false);
    bool useEc = true;
    while (bits.size() < capacityBits) {
        qrAppendBits(bits, useEc ? 0xEC : 0x11, 8);
        useEc = !useEc;
    }

    QVector<int> data(spec.dataCodewords, 0);
    for (int i = 0; i < spec.dataCodewords; ++i) {
        int value = 0;
        for (int j = 0; j < 8; ++j) value = (value << 1) | (bits.at(i * 8 + j) ? 1 : 0);
        data[i] = value;
    }

    QVector<int> gfExp(512, 0), gfLog(256, 0);
    int x = 1;
    for (int i = 0; i < 255; ++i) {
        gfExp[i] = x;
        gfLog[x] = i;
        x <<= 1;
        if (x & 0x100) x ^= 0x11D;
    }
    for (int i = 255; i < 512; ++i) gfExp[i] = gfExp[i - 255];
    auto gfMul = [&](int a, int b) {
        if (a == 0 || b == 0) return 0;
        return gfExp[gfLog[a] + gfLog[b]];
    };

    QVector<int> generator{1};
    for (int i = 0; i < spec.eccCodewords; ++i) {
        QVector<int> next(generator.size() + 1, 0);
        for (int j = 0; j < generator.size(); ++j) {
            next[j] ^= gfMul(generator[j], gfExp[i]);
            next[j + 1] ^= generator[j];
        }
        generator = next;
    }

    QVector<int> ecc(spec.eccCodewords, 0);
    for (int byte : data) {
        const int factor = byte ^ ecc[0];
        for (int i = 0; i < spec.eccCodewords - 1; ++i) {
            ecc[i] = ecc[i + 1] ^ gfMul(generator[i + 1], factor);
        }
        ecc[spec.eccCodewords - 1] = gfMul(generator[spec.eccCodewords], factor);
    }

    QVector<int> codewords = data;
    for (int value : ecc) codewords.append(value);
    QVector<bool> allBits;
    allBits.reserve(codewords.size() * 8);
    for (int byte : codewords) qrAppendBits(allBits, byte, 8);

    QVector<QVector<bool>> modules(spec.size, QVector<bool>(spec.size, false));
    QVector<QVector<bool>> isFunction(spec.size, QVector<bool>(spec.size, false));
    auto setFunction = [&](int xPos, int yPos, bool dark) {
        if (xPos < 0 || yPos < 0 || xPos >= spec.size || yPos >= spec.size) return;
        modules[yPos][xPos] = dark;
        isFunction[yPos][xPos] = true;
    };
    auto drawFinder = [&](int x0, int y0) {
        for (int dy = -1; dy <= 7; ++dy) {
            for (int dx = -1; dx <= 7; ++dx) {
                const int xx = x0 + dx;
                const int yy = y0 + dy;
                if (xx < 0 || yy < 0 || xx >= spec.size || yy >= spec.size) continue;
                const bool inside = dx >= 0 && dx <= 6 && dy >= 0 && dy <= 6;
                const bool dark = inside && (dx == 0 || dx == 6 || dy == 0 || dy == 6 || (dx >= 2 && dx <= 4 && dy >= 2 && dy <= 4));
                setFunction(xx, yy, dark);
            }
        }
    };
    auto drawAlignment = [&](int cx, int cy) {
        for (int dy = -2; dy <= 2; ++dy) {
            for (int dx = -2; dx <= 2; ++dx) {
                const int dist = qMax(qAbs(dx), qAbs(dy));
                setFunction(cx + dx, cy + dy, dist != 1);
            }
        }
    };

    drawFinder(0, 0);
    drawFinder(spec.size - 7, 0);
    drawFinder(0, spec.size - 7);
    drawAlignment(spec.alignmentCenter, spec.alignmentCenter);
    for (int i = 8; i < spec.size - 8; ++i) {
        setFunction(i, 6, i % 2 == 0);
        setFunction(6, i, i % 2 == 0);
    }
    for (int i = 0; i <= 8; ++i) {
        if (i != 6) {
            isFunction[8][i] = true;
            isFunction[i][8] = true;
        }
    }
    for (int i = 0; i < 8; ++i) {
        isFunction[spec.size - 1 - i][8] = true;
        isFunction[8][spec.size - 1 - i] = true;
    }
    setFunction(8, spec.size - 8, true);

    int bitIndex = 0;
    for (int right = spec.size - 1; right >= 1; right -= 2) {
        if (right == 6) right = 5;
        for (int vert = 0; vert < spec.size; ++vert) {
            const bool upward = ((right + 1) & 2) == 0;
            const int y = upward ? (spec.size - 1 - vert) : vert;
            for (int j = 0; j < 2; ++j) {
                const int xPos = right - j;
                if (isFunction[y][xPos]) continue;
                modules[y][xPos] = bitIndex < allBits.size() ? allBits.at(bitIndex++) : false;
            }
        }
    }

    for (int y = 0; y < spec.size; ++y) {
        for (int xPos = 0; xPos < spec.size; ++xPos) {
            if (!isFunction[y][xPos] && ((xPos + y) % 2 == 0)) modules[y][xPos] = !modules[y][xPos];
        }
    }

    const int format = 0x77C4; // ECC L, mask 0
    auto bit = [&](int i) { return ((format >> i) & 1) != 0; };
    for (int i = 0; i <= 5; ++i) setFunction(8, i, bit(i));
    setFunction(8, 7, bit(6));
    setFunction(8, 8, bit(7));
    setFunction(7, 8, bit(8));
    for (int i = 9; i < 15; ++i) setFunction(14 - i, 8, bit(i));
    for (int i = 0; i < 8; ++i) setFunction(spec.size - 1 - i, 8, bit(i));
    for (int i = 8; i < 15; ++i) setFunction(8, spec.size - 15 + i, bit(i));
    setFunction(8, spec.size - 8, true);

    QImage image((spec.size + spec.quiet * 2) * spec.scale, (spec.size + spec.quiet * 2) * spec.scale, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::white);
    for (int y = 0; y < spec.size; ++y) {
        for (int xPos = 0; xPos < spec.size; ++xPos) {
            if (!modules[y][xPos]) continue;
            for (int yy = 0; yy < spec.scale; ++yy) {
                for (int xx = 0; xx < spec.scale; ++xx) {
                    image.setPixelColor((xPos + spec.quiet) * spec.scale + xx, (y + spec.quiet) * spec.scale + yy, QColor(0, 0, 0));
                }
            }
        }
    }
    return QPixmap::fromImage(image);
}

static QPixmap makeScannablePairingQr(const QString& desktopName, const QString& uuidText, const QString& codeText)
{
    QString safeName = sanitizeQrAlnum(desktopName);
    QString safeUuid = sanitizeQrAlnum(uuidText);
    QString safeCode = sanitizeQrAlnum(codeText);
    if (safeName.isEmpty()) safeName = QStringLiteral("CS DESKTOP");
    if (safeUuid.isEmpty()) safeUuid = QStringLiteral("E8E10F95-1A70-4B27-9CCF-02010264E9C8");
    if (safeCode.isEmpty()) safeCode = QStringLiteral("CS DESKTOP 01");
    const QString payload = QStringLiteral("CSPAIR/U/%1/N/%2/C/%3").arg(safeUuid.left(36), safeName.left(18), safeCode.left(24));
    return makeScannableQrBytes(payload.toUtf8());
}

static QPixmap makeScannableWifiQr(const QString& ssidText, const QString& passwordText)
{
    const QString safeSsid = escapeWifiQrField(ssidText.trimmed());
    const QString safePassword = escapeWifiQrField(passwordText.trimmed());
    const QString payload = QStringLiteral("WIFI:T:WPA;S:%1;P:%2;;").arg(safeSsid, safePassword);
    return makeScannableQrBytes(payload.toUtf8());
}

static void styleMessageBoxReadableLocal(QMessageBox* box)
{
    applyStandardMessageBoxStyle(box);
}

}

static bool getSoundEnabledSetting();
static void setSoundEnabledSetting(bool on);

static bool isDarkUi();
static QString cPrimary();
static QString cSecondary();
static QString cPanelBg();
static QString cPanelBorder();
static QString cBarBg();
static QString cListText();
static QString cSelBg();

namespace {

static QString canonStoreKey_local(const QString& s)
{
    return StoreNameNormalizer::canonKey(s);
}

static QString detectOilGradeFromText_local(const QString& text);

static bool isMotorOilPartName_local(const QString& name)
{
    return OilServiceClassifier::isMotorOilProductName(name);
}


} // namespace

static bool isDarkUi()
{
    return ThemeManager::currentTheme() == "graphite";
}

static QString cTextOnDark()
{
    return QStringLiteral("#f7fbff");
}

static QString cMutedTextOnDark()
{
    return QStringLiteral("#e8eef6");
}

static QString cTextOnLight()
{
    return QStringLiteral("#101418");
}

static QString cMutedTextOnLight()
{
    return QStringLiteral("#1d242c");
}

static QString cPlaceholderOnLight()
{
    return QStringLiteral("#566171");
}

static QString cDisabledTextOnDark()
{
    return QStringLiteral("#dbe4ef");
}

static QString cDisabledTextOnLight()
{
    return QStringLiteral("#4b5563");
}

static QString cPrimary()
{
    return isDarkUi() ? cTextOnDark() : cTextOnLight();
}

static QString cSecondary()
{
    return isDarkUi() ? cMutedTextOnDark() : cMutedTextOnLight();
}

static QString cPanelBg()
{
    return isDarkUi() ? QStringLiteral("rgba(31,40,51,0.88)") : QStringLiteral("#ffffff");
}

static QString cPanelBorder()
{
    return isDarkUi() ? QStringLiteral("rgba(222,234,247,0.28)") : QStringLiteral("#3a3a3a");
}

static QString cBarBg()
{
    return isDarkUi() ? QStringLiteral("rgba(22,28,36,0.86)") : QStringLiteral("#8f8f8f");
}

static QString cListText()
{
    return isDarkUi() ? cTextOnDark() : cTextOnLight();
}

static QString cSelBg()
{
    return isDarkUi() ? QStringLiteral("rgba(223,233,245,0.20)") : QStringLiteral("rgba(0,0,0,0.06)");
}

static QString cSettingsTextPrimary()
{
    return cTextOnDark();
}

static QString cSettingsTextSecondary()
{
    return cMutedTextOnDark();
}

static QString settingsLeftQss()
{
    if (isDarkUi()) {
        return QString::fromUtf8(R"QSS(
            QFrame#settings_left { background: transparent; }
            QPushButton {
                background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                            stop:0 rgba(104,120,141,0.98),
                            stop:0.14 rgba(84,98,117,0.98),
                            stop:0.52 rgba(52,63,77,0.99),
                            stop:1 rgba(27,34,43,0.99));
                border: 1px solid rgba(228,236,246,0.26);
                border-radius: 14px;
                color: #f8fbff;
                font-weight: 700;
                padding-left: 16px;
                text-align: left;
            }
            QPushButton:hover {
                background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                            stop:0 rgba(126,144,166,0.99),
                            stop:0.18 rgba(93,109,129,0.99),
                            stop:0.58 rgba(58,70,85,0.99),
                            stop:1 rgba(31,39,49,0.99));
                border: 1px solid rgba(206,224,246,0.52);
            }
            QPushButton:pressed {
                background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                            stop:0 rgba(43,53,65,0.99),
                            stop:1 rgba(21,27,35,0.99));
            }
            QPushButton:checked {
                background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                            stop:0 rgba(215,225,236,0.98),
                            stop:0.16 rgba(177,190,205,0.98),
                            stop:0.52 rgba(118,132,149,0.99),
                            stop:1 rgba(72,85,100,0.99));
                border: 1px solid rgba(220,234,252,0.82);
                color: #0e151d;
            }
        )QSS");
    }
    return QString::fromUtf8(R"QSS(
        QFrame#settings_left { background: transparent; }
        QPushButton {
            background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                        stop:0 #ffffff, stop:1 #e6e6e6);
            border: 1px solid #bfc3c7;
            border-radius: 12px;
            color: #151a20;
            font-weight: 700;
            padding-left: 14px;
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
    )QSS");
}

static QString settingsRightQss()
{
    return QString::fromUtf8(R"QSS(
        QFrame#settings_right { background: transparent; }
        QFrame#box {
            background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                                        stop:0 rgba(21,27,34,0.98),
                                        stop:0.07 rgba(56,66,80,0.98),
                                        stop:0.16 rgba(90,103,119,0.92),
                                        stop:0.23 rgba(61,73,87,0.96),
                                        stop:0.58 rgba(34,44,56,0.98),
                                        stop:1 rgba(46,58,71,0.99));
            border: 1px solid %1;
            border-radius: 18px;
        }
        QFrame#box > QFrame#hdr,
        QFrame#box > QFrame#ftr {
            background: qlineargradient(x1:0, y1:0, x2:1, y2:0,
                                        stop:0 rgba(28,34,42,0.97),
                                        stop:0.50 rgba(48,58,71,0.93),
                                        stop:1 rgba(24,30,38,0.97));
            border: none;
        }
        QFrame#box > QFrame#hdr {
            border-top-left-radius: 18px;
            border-top-right-radius: 18px;
            border-bottom: 1px solid rgba(226,235,246,0.14);
        }
        QFrame#box > QFrame#ftr {
            border-bottom-left-radius: 18px;
            border-bottom-right-radius: 18px;
            border-top: 1px solid rgba(226,235,246,0.12);
        }
        QWidget#settings_items_page,
        QWidget#settings_detail_page {
            background: transparent;
        }
        QFrame#settings_right QLabel,
        QFrame#settings_right QCheckBox,
        QFrame#settings_right QRadioButton,
        QFrame#settings_right QGroupBox {
            color: %2;
            background: transparent;
        }
        QFrame#settings_right QLabel:disabled,
        QFrame#settings_right QCheckBox:disabled,
        QFrame#settings_right QRadioButton:disabled,
        QFrame#settings_right QGroupBox:disabled {
            color: %5;
        }
        QFrame#settings_right QGroupBox::title {
            color: %2;
            subcontrol-origin: margin;
            left: 10px;
            padding: 0 4px;
        }
        QLabel#hdrTitle {
            color: %2;
            font-weight: 800;
            font-size: 12pt;
            padding-left: 0px;
        }
        QLabel#footerText {
            color: %3;
            padding-left: 0px;
            padding-top: 3px;
            padding-bottom: 3px;
            font-size: 9.5pt;
        }
        QFrame#settings_right QLineEdit,
        QFrame#settings_right QComboBox,
        QFrame#settings_right QSpinBox,
        QFrame#settings_right QDoubleSpinBox,
        QFrame#settings_right QDateEdit,
        QFrame#settings_right QDateTimeEdit,
        QFrame#settings_right QTextEdit,
        QFrame#settings_right QPlainTextEdit {
            background: #ffffff;
            color: %4;
            border: 1px solid rgba(22,28,35,0.22);
            border-radius: 10px;
            padding: 6px 10px;
            selection-background-color: rgba(128,170,235,0.28);
            selection-color: %4;
        }
        QFrame#settings_right QLineEdit:focus,
        QFrame#settings_right QComboBox:focus,
        QFrame#settings_right QSpinBox:focus,
        QFrame#settings_right QDoubleSpinBox:focus,
        QFrame#settings_right QTextEdit:focus,
        QFrame#settings_right QPlainTextEdit:focus {
            background: #ffffff;
            color: %4;
            border: 1px solid rgba(94,142,220,0.86);
        }
        QFrame#settings_right QLineEdit:disabled,
        QFrame#settings_right QComboBox:disabled,
        QFrame#settings_right QSpinBox:disabled,
        QFrame#settings_right QDoubleSpinBox:disabled,
        QFrame#settings_right QDateEdit:disabled,
        QFrame#settings_right QDateTimeEdit:disabled,
        QFrame#settings_right QTextEdit:disabled,
        QFrame#settings_right QPlainTextEdit:disabled {
            background: #edf1f5;
            color: %6;
            border: 1px solid rgba(22,28,35,0.14);
        }
        QFrame#settings_right QComboBox QAbstractItemView {
            background: #ffffff;
            color: %4;
            border: 1px solid rgba(22,28,35,0.22);
            selection-background-color: #dfe8f5;
            selection-color: %4;
        }
        QFrame#settings_right QPushButton,
        QFrame#settings_right QToolButton {
            min-height: 38px;
            background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                                        stop:0 rgba(113,129,149,0.98),
                                        stop:0.16 rgba(87,102,121,0.98),
                                        stop:0.54 rgba(50,60,73,0.99),
                                        stop:1 rgba(25,32,40,0.99));
            color: #f8fbff;
            border: 1px solid rgba(228,236,246,0.34);
            border-radius: 12px;
            padding: 7px 16px;
            font-weight: 700;
        }
        QFrame#settings_right QPushButton:hover,
        QFrame#settings_right QToolButton:hover {
            background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                                        stop:0 rgba(131,148,171,0.99),
                                        stop:0.18 rgba(98,114,134,0.99),
                                        stop:0.58 rgba(58,69,84,0.99),
                                        stop:1 rgba(30,38,48,0.99));
            border: 1px solid rgba(214,229,248,0.58);
        }
        QFrame#settings_right QPushButton:pressed,
        QFrame#settings_right QToolButton:pressed {
            background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                                        stop:0 rgba(43,52,64,0.99),
                                        stop:1 rgba(21,27,34,0.99));
        }
        QFrame#settings_right QPushButton:disabled,
        QFrame#settings_right QToolButton:disabled {
            background: rgba(160,172,186,0.32);
            color: %5;
            border: 1px solid rgba(228,236,246,0.22);
        }
        QFrame#settings_right QListWidget {
            border: none;
            background: transparent;
            color: %2;
            font-size: 11pt;
        }
        QFrame#settings_right QTableWidget,
        QFrame#settings_right QTableView,
        QFrame#settings_right QListView,
        QFrame#settings_right QTreeWidget {
            background: #ffffff;
            color: %4;
            border: 1px solid rgba(22,28,35,0.18);
            border-radius: 10px;
            gridline-color: rgba(22,28,35,0.10);
            selection-background-color: #dfe8f5;
            selection-color: %4;
            alternate-background-color: #f6f8fb;
        }
        QFrame#settings_right QTableWidget::item:selected,
        QFrame#settings_right QTableView::item:selected,
        QFrame#settings_right QListView::item:selected,
        QFrame#settings_right QTreeWidget::item:selected {
            background: #dfe8f5;
            color: %4;
        }
        QFrame#settings_right QHeaderView::section {
            background: #f2f4f8;
            color: %4;
            border: none;
            border-right: 1px solid #d7dee8;
            border-bottom: 1px solid #d7dee8;
            padding: 6px 8px;
            font-weight: 700;
        }
        QFrame#settings_right QTableCornerButton::section {
            background: #f2f4f8;
            border: none;
            border-right: 1px solid #d7dee8;
            border-bottom: 1px solid #d7dee8;
        }
        QFrame#settings_right QTabBar::tab {
            color: %2;
            background: rgba(255,255,255,0.06);
            border: 1px solid rgba(255,255,255,26);
            border-bottom: none;
            padding: 8px 14px;
            border-top-left-radius: 8px;
            border-top-right-radius: 8px;
        }
        QFrame#settings_right QTabBar::tab:selected {
            color: %4;
            background: #ffffff;
            border: 1px solid rgba(22,28,35,0.18);
            border-bottom-color: #ffffff;
        }
        QFrame#settings_right QTabWidget::pane {
            border: 1px solid rgba(22,28,35,0.18);
            border-radius: 10px;
            background: #ffffff;
        }
        QFrame#settings_right QListWidget::item {
            padding: 10px 10px;
        }
        QFrame#settings_right QListWidget::item:selected {
            background: %7;
            border-radius: 6px;
        }
        QFrame#settings_right QRadioButton::indicator {
            width: 22px;
            height: 22px;
        }
        %8
    )QSS").arg(cPanelBorder())
         .arg(cSettingsTextPrimary())
         .arg(cSettingsTextSecondary())
         .arg(cTextOnLight())
         .arg(isDarkUi() ? cDisabledTextOnDark() : cDisabledTextOnLight())
         .arg(cDisabledTextOnLight())
         .arg(cSelBg())
         .arg(contrastCheckboxRuleBlock(QStringLiteral("QFrame#settings_right QCheckBox")));
