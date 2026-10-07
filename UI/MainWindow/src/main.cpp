/// @file main.cpp
/// @brief AURA DAW application entry point (Qt6/QML).

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>

#include "Aura/MainWindow.hpp"

int main(int argc, char* argv[]) {
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName("AURA DAW");
    QGuiApplication::setOrganizationName("AURA");
    QGuiApplication::setApplicationVersion("1.0.0");
    QQuickStyle::setStyle("Fusion");

    Aura::Ui::MainWindow controller;

    QQmlApplicationEngine engine;
    engine.setInitialProperties({{"auraMain", QVariant::fromValue(&controller)}});
    engine.loadFromModule("AuraUI", "Main");
    if (engine.rootObjects().isEmpty()) {
        return 1;
    }
    return QGuiApplication::exec();
}
