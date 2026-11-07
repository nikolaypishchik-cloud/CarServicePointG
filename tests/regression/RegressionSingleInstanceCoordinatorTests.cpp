#include "app/SingleInstanceCoordinator.h"

#include <QCoreApplication>
#include <QDebug>
#include <QTimer>
#include <QUuid>

#include <atomic>
#include <thread>

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    const QString serverName = QStringLiteral("CarServicePointG.SingleInstance.Test.%1")
                                   .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));

    CarService::SingleInstanceCoordinator primary(serverName);
    if (primary.acquireOrActivate() != CarService::SingleInstanceCoordinator::StartResult::PrimaryInstance) {
        qCritical() << "Unable to create the primary single-instance server";
        return 1;
    }

    std::atomic_bool activationReceived = false;
    std::atomic_int secondaryResult = static_cast<int>(CarService::SingleInstanceCoordinator::StartResult::Unavailable);
    QObject::connect(&primary, &CarService::SingleInstanceCoordinator::activationRequested,
                     &app, [&app, &activationReceived]() {
        activationReceived.store(true);
        app.exit(0);
    });

    std::thread secondaryLaunch([serverName, &secondaryResult]() {
        CarService::SingleInstanceCoordinator secondary(serverName);
        secondaryResult.store(static_cast<int>(secondary.acquireOrActivate()));
    });
    QTimer::singleShot(3000, &app, [&app]() {
        app.exit(1);
    });

    const int eventLoopStatus = app.exec();
    secondaryLaunch.join();
    if (!activationReceived.load()) {
        qCritical() << "The primary instance did not receive an activation request";
        return 1;
    }
    if (secondaryResult.load()
        != static_cast<int>(CarService::SingleInstanceCoordinator::StartResult::SecondaryInstanceActivated)) {
        qCritical() << "The secondary launch was not redirected to the primary instance";
        return 1;
    }
    return eventLoopStatus;
}