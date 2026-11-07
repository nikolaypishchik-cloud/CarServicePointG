#pragma once

#include <QPixmap>
#include <QSize>
#include <QString>
#include <QStringList>

namespace VehiclePreviewProvider {

QPixmap preview(const QString& brand,
                const QString& model,
                int year,
                const QString& color,
                const QSize& size = QSize(80, 48));

QPixmap preview(const QString& brand,
                const QString& model,
                const QString& displayName,
                int year,
                const QString& color,
                const QSize& size = QSize(80, 48));

QPixmap preview(const QString& brand,
                const QString& model,
                const QString& color,
                const QSize& size = QSize(80, 48));

QStringList candidateResourcePaths(const QString& brand,
                                   const QString& model,
                                   int year,
                                   const QString& color);

QStringList candidateResourcePaths(const QString& brand,
                                   const QString& model,
                                   const QString& color);

} // namespace VehiclePreviewProvider
