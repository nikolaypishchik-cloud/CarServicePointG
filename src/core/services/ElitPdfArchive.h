#pragma once
#include <QtCore/private/qzipreader_p.h>
#include <QBuffer>
#include <QMap>
#include <QSet>
#include <QRegularExpression>
#include <functional>

namespace ElitPdfArchive {
// Qt's ZIP implementation stays in memory; archive paths are never extracted.
// The private header is tied to the Qt version used by the runtime package gate.
inline QMap<QString,QByteArray> unpack(const QByteArray& bytes,const QStringList& numbers,
                                    const std::function<bool()>& current,QString* error) {
    auto fail=[&]{*error=QString::fromUtf8("PDF Еліт: пошкоджений або небезпечний ZIP-пакет, або операцію скасовано.");return QMap<QString,QByteArray>{};};
    if(!current() || bytes.size()>16*1024*1024 || !bytes.startsWith("PK\003\004"))return fail();
    QBuffer buffer;buffer.setData(bytes);buffer.open(QIODevice::ReadOnly);
    QZipReader archive(&buffer);
    const auto entries=archive.fileInfoList();
    if(archive.status()!=QZipReader::NoError || entries.isEmpty() || entries.size()>256)return fail();
    QSet<QString> seen;qint64 total=0;
    for(const auto& entry:entries) {
        if(!entry.isFile || entry.isSymLink || entry.isDir || seen.contains(entry.filePath)
            || !QRegularExpression("^[A-Za-z0-9_-]{1,80}\\.pdf$").match(entry.filePath).hasMatch()
            || entry.size<0 || entry.size>16*1024*1024)return fail();
        seen.insert(entry.filePath);total+=entry.size;
        if(total>32*1024*1024)return fail();
    }
    QMap<QString,QByteArray> result;
    for(const auto& entry:entries) {
        if(!current())return fail();
        const auto number=entry.filePath.chopped(4);
        if(!numbers.contains(number))continue;
        const auto body=archive.fileData(entry.filePath);
        if(archive.status()!=QZipReader::NoError || body.size()!=entry.size || !body.startsWith("%PDF-"))return fail();
        quint32 crc=0xffffffffu;
        for(unsigned char byte:body) {
            crc^=byte;
            for(int bit=0;bit<8;++bit)crc=(crc>>1)^(0xedb88320u & (0u-(crc&1u)));
        }
        if((crc^0xffffffffu)!=entry.crc)return fail();
        result.insert(number,body);
    }
    return result;
}
}
