#pragma once
#include <QAbstractTableModel>
#include <QBrush>
#include <QMap>
#include <QStringList>
#include <QVector>

// Disposable, editable invoice preview. No persistence or database dependency.
class InvoiceReviewTableModel final : public QAbstractTableModel {
public:
    struct Cell {QString text;QMap<int,QVariant> roles;};
    using Row=QVector<Cell>;
    explicit InvoiceReviewTableModel(QObject* parent=nullptr):QAbstractTableModel(parent){}
    int rowCount(const QModelIndex& parent={}) const override{return parent.isValid()?0:m_rows.size();}
    int columnCount(const QModelIndex& parent={}) const override{return parent.isValid()?0:m_headers.size();}
    QVariant headerData(int section,Qt::Orientation orientation,int role) const override {
        return orientation==Qt::Horizontal && role==Qt::DisplayRole && section>=0 && section<m_headers.size()?QVariant(m_headers[section]):QVariant();
    }
    QVariant data(const QModelIndex& index,int role=Qt::DisplayRole) const override {
        if(!valid(index))return {};
        const auto& cell=m_rows[index.row()][index.column()];
        if(role==Qt::DisplayRole || role==Qt::EditRole)return cell.text;
        if(role==Qt::CheckStateRole && index.column()==0)return cell.roles.value(role,Qt::Checked);
        return cell.roles.value(role);
    }
    Qt::ItemFlags flags(const QModelIndex& index) const override {
        if(!valid(index))return Qt::NoItemFlags;
        if(index.column()==0)return Qt::ItemIsEnabled|Qt::ItemIsUserCheckable;
        auto value=Qt::ItemIsEnabled|Qt::ItemIsSelectable;
        if(index.column()!=2 || !m_fixedOrder)value|=Qt::ItemIsEditable;
        return value;
    }
    bool setData(const QModelIndex& index,const QVariant& value,int role=Qt::EditRole) override {
        if(!valid(index))return false;
        auto& cell=m_rows[index.row()][index.column()];
        if(role==Qt::EditRole){if(!(flags(index)&Qt::ItemIsEditable))return false;if(cell.text==value.toString())return true;cell.text=value.toString();}
        else {if(cell.roles.value(role)==value)return true;cell.roles[role]=value;}
        emit dataChanged(index,index,{role});return true;
    }
    void setRows(const QStringList& headers,const QList<QStringList>& values,bool fixedOrder) {
        beginResetModel();m_headers=headers;m_rows.clear();m_fixedOrder=fixedOrder;
        for(const auto& valuesRow:values)m_rows.append(makeRow(valuesRow));endResetModel();
    }
    void appendRow(const QStringList& values){const int row=m_rows.size();beginInsertRows({},row,row);m_rows.append(makeRow(values));endInsertRows();}
    bool removeRows(int row,int count,const QModelIndex& parent={}) override {
        if(parent.isValid() || count<=0 || row<0 || row+count>m_rows.size())return false;
        beginRemoveRows({},row,row+count-1);m_rows.remove(row,count);endRemoveRows();return true;
    }
    void setAllChecked(bool checked){for(int row=0;row<rowCount();++row)setData(index(row,0),checked?Qt::Checked:Qt::Unchecked,Qt::CheckStateRole);}
    void setColumnForSelected(int column,const QString& value){for(int row=0;row<rowCount();++row)if(data(index(row,0),Qt::CheckStateRole).toInt()==Qt::Checked)setData(index(row,column),value);}
private:
    bool valid(const QModelIndex& index)const{return index.isValid()&&index.model()==this&&index.row()>=0&&index.row()<m_rows.size()&&index.column()>=0&&index.column()<m_headers.size();}
    Row makeRow(const QStringList& values)const{Row row;for(int column=0;column<m_headers.size();++column){Cell cell;cell.text=values.value(column);row.append(cell);}return row;}
    QStringList m_headers;QVector<Row> m_rows;bool m_fixedOrder=false;
};
