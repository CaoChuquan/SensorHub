#include <QApplication>
#include <QMainWindow>
#include <QStringLiteral>

int main(int argc, char* argv[]) {
    // QApplication 管理 GUI 程序的事件循环、窗口系统集成、字体、风格。
    // 任何 Qt Widgets 程序必须有且仅有一个 QApplication 实例，
    // 且必须在创建任何 widget 之前构造。
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("SensorHub"));
    app.setApplicationVersion(QStringLiteral("0.1.0"));

    QMainWindow window;
    window.setWindowTitle(QStringLiteral("SensorHub - 物联网数据采集终端"));
    window.resize(900, 600);
    window.show();

    // exec() 进入事件循环，直到窗口被关闭才返回。
    return app.exec();
}