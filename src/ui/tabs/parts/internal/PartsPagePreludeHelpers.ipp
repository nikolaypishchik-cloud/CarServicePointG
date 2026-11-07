// Extracted from src/ui/tabs/parts/PartsPageModulePrelude.h for P2 prelude monolith reduction.
// Included exactly once by the owning header; do not compile separately.
// The confirmUnpayPart() QStringLiteral block is intentionally kept entirely
// in PartsPagePreludeDelegates.ipp because MSVC does not allow macro calls
// to span included file boundaries.


QString smoothPaidButtonStyle(bool paid)
{
    if (paid) {
        return QString::fromUtf8(
            "QPushButton {"
            " background:qlineargradient(x1:0,y1:0,x2:0,y2:1,"
            "   stop:0 rgba(255,255,255,0.98),"
            "   stop:0.10 rgba(248,250,252,0.98),"
            "   stop:0.22 rgba(232,236,240,0.98),"
            "   stop:0.52 rgba(189,196,203,0.98),"
            "   stop:0.78 rgba(144,151,158,0.98),"
            "   stop:1 rgba(92,99,106,0.99));"
            " color:#111111;"
            " border:1px solid rgba(255,255,255,0.92);"
            " border-radius:13px;"
            " font-weight:800;"
            " padding:1px 10px 3px 10px;"
            " min-width:80px;"
            " }"
            "QPushButton:hover {"
            " background:qlineargradient(x1:0,y1:0,x2:0,y2:1,"
            "   stop:0 rgba(255,255,255,1.0),"
            "   stop:0.12 rgba(252,253,254,1.0),"
            "   stop:0.26 rgba(238,242,246,1.0),"
            "   stop:0.55 rgba(198,205,212,1.0),"
            "   stop:0.82 rgba(153,160,167,1.0),"
            "   stop:1 rgba(102,109,116,1.0));"
            " }"
            "QPushButton:pressed {"
            " background:qlineargradient(x1:0,y1:1,x2:0,y2:0,"
            "   stop:0 rgba(245,247,249,0.98),"
            "   stop:0.18 rgba(220,225,230,0.98),"
            "   stop:0.55 rgba(166,173,180,0.98),"
            "   stop:1 rgba(110,117,124,0.99));"
            " padding:4px 10px 0px 10px;"
            " }");
    }
    return QString::fromUtf8(
        "QPushButton {"
        " background:qlineargradient(x1:0,y1:0,x2:0,y2:1,"
        "   stop:0 rgba(255,239,239,0.99),"
        "   stop:0.10 rgba(255,203,203,0.99),"
        "   stop:0.24 rgba(255,126,126,0.99),"
        "   stop:0.58 rgba(226,61,61,1.0),"
        "   stop:0.82 rgba(171,25,25,1.0),"
        "   stop:1 rgba(116,10,10,1.0));"
        " color:#111111;"
        " border:1px solid rgba(255,255,255,0.92);"
        " border-radius:13px;"
        " font-weight:800;"
        " padding:1px 10px 3px 10px;"
        " min-width:80px;"
        " }"
        "QPushButton:hover {"
        " background:qlineargradient(x1:0,y1:0,x2:0,y2:1,"
        "   stop:0 rgba(255,248,248,1.0),"
        "   stop:0.12 rgba(255,219,219,1.0),"
        "   stop:0.28 rgba(255,145,145,1.0),"
        "   stop:0.60 rgba(236,73,73,1.0),"
        "   stop:0.84 rgba(182,31,31,1.0),"
        "   stop:1 rgba(126,14,14,1.0));"
        " }"
        "QPushButton:pressed {"
        " background:qlineargradient(x1:0,y1:1,x2:0,y2:0,"
        "   stop:0 rgba(255,230,230,0.99),"
        "   stop:0.18 rgba(255,170,170,0.99),"
        "   stop:0.52 rgba(220,52,52,1.0),"
        "   stop:1 rgba(122,12,12,1.0));"
        " padding:4px 10px 0px 10px;"
        " }");
}

QString buttonStyle(bool paid)
{
    return smoothPaidButtonStyle(paid);
}

// Row lookup and overdue coloring are model-driven through prepared indexes and roles.


}
