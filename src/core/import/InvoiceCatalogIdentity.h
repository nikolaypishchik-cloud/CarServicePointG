#pragma once
#include "InvoiceImportTypes.h"
#include <QJsonObject>

namespace InvoiceImport {
// Use explicit columns and a unique matching invoice line; never infer fitment
// from the customer's vehicle or confuse a manufacturer brand with a car make.
inline QJsonObject catalogIdentity(const QList<QStringList>& rows,const ImportedPartDraft& draft) {
    for(int header=0;header<rows.size();++header) {
        QMap<QString,int> columns;
        for(int c=0;c<rows[header].size();++c) {
            const auto h=rows[header][c].simplified().toCaseFolded();
            if(h=="oem" || h=="oem номер" || h=="oem / артикул" || h==QString::fromUtf8("оригінальний номер"))columns["oem"]=c;
            if(h==QString::fromUtf8("марка авто") || h==QString::fromUtf8("марка автомобіля") || h=="vehicle make")columns["brand"]=c;
            if(h==QString::fromUtf8("модель авто") || h==QString::fromUtf8("модель автомобіля") || h=="vehicle model")columns["model"]=c;
            if(h==QString::fromUtf8("вузол авто") || h==QString::fromUtf8("вузол") || h=="vehicle node")columns["node"]=c;
        }
        if(columns.size()!=4)continue;
        QJsonObject found;int matches=0;
        for(int r=header+1;r<rows.size();++r) {
            const auto& row=rows[r];
            if(row.value(columns["oem"]).trimmed()!=draft.oemNumber.trimmed() || !row.contains(draft.name))continue;
            ++matches;found["name"]=draft.name.trimmed();
            for(auto it=columns.cbegin();it!=columns.cend();++it)found[it.key()]=row.value(it.value()).trimmed();
        }
        if(matches==1) {
            bool complete=true;for(const auto& key:{"name","oem","brand","model","node"})complete &= !found[key].toString().isEmpty();
            if(complete)return found;
        }
    }
    return {};
}
}
