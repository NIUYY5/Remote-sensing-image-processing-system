#include "moban.h"
#include "login_dialog.h"
#include <QtWidgets/QApplication>
#include <QtGui/QIcon>

int main(int argc, char *argv[])
{
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
#endif

    QApplication app(argc, argv);
    app.setApplicationName(QString::fromUtf8("遥感图像处理系统"));
    app.setOrganizationName(QString::fromUtf8("RSImageProcess"));
    app.setApplicationVersion("1.0.0");
    app.setWindowIcon(QIcon(":/moban/icons/app_icon.svg"));

    LoginDialog loginDlg;
    if (loginDlg.exec() != QDialog::Accepted)
    {
        return 0;
    }

    moban window;
    window.show();

    return app.exec();
}
