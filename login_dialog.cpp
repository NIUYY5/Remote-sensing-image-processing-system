#include "login_dialog.h"

#include <QtWidgets/QApplication>
#include <QtWidgets/QGraphicsDropShadowEffect>
#include <QtGui/QPixmap>
#include <QtGui/QMovie>
#include <QtGui/QIcon>
#include <QtCore/QDirIterator>
#include <QtCore/QFileInfo>
#include <QtCore/QFile>

#ifdef Q_OS_WIN
#include <windows.h>
#include <winreg.h>
#endif

LoginDialog::LoginDialog(QWidget *parent)
    : QDialog(parent)
    , m_usernameEdit(nullptr)
    , m_passwordEdit(nullptr)
    , m_loginButton(nullptr)
    , m_titleLabel(nullptr)
    , m_subtitleLabel(nullptr)
    , m_versionLabel(nullptr)
    , m_errorLabel(nullptr)
    , m_iconLabel(nullptr)
    , m_cardWidget(nullptr)
    , m_bgMovieLabel(nullptr)
    , m_dragging(false)
{
    setWindowFlags(Qt::FramelessWindowHint | Qt::Dialog);
    setAttribute(Qt::WA_TranslucentBackground);
    setFixedSize(960, 640);
    setWindowTitle(QString::fromUtf8("登录 - 遥感图像处理系统"));
    setWindowIcon(QIcon(":/moban/icons/app_icon.svg"));

    findWallpaper();
    setupUi();

    if (m_bgMovieLabel)
    {
        m_bgMovieLabel->lower();
    }
    if (m_cardWidget)
    {
        m_cardWidget->raise();
    }
}

LoginDialog::~LoginDialog()
{
}

QString LoginDialog::username() const
{
    return m_usernameEdit ? m_usernameEdit->text().trimmed() : QString();
}

QString LoginDialog::password() const
{
    return m_passwordEdit ? m_passwordEdit->text() : QString();
}

QString LoginDialog::findSteamPath() const
{
#ifdef Q_OS_WIN
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
        L"Software\\Valve\\Steam", 0, KEY_READ, &hKey) == ERROR_SUCCESS)
    {
        wchar_t value[512];
        DWORD valueSize = sizeof(value);
        DWORD type;
        if (RegQueryValueExW(hKey, L"SteamPath", nullptr, &type,
            reinterpret_cast<LPBYTE>(value), &valueSize) == ERROR_SUCCESS && type == REG_SZ)
        {
            RegCloseKey(hKey);
            return QString::fromWCharArray(value);
        }
        RegCloseKey(hKey);
    }
#endif

    QStringList commonPaths;
    commonPaths << "C:/Program Files (x86)/Steam"
                << "D:/Steam"
                << "E:/Steam"
                << "C:/Steam";

    for (const QString &p : commonPaths)
    {
        if (QDir(p).exists())
        {
            return p;
        }
    }

    return QString();
}

void LoginDialog::findWallpaper()
{
    QString exePath = QApplication::applicationDirPath() + "/background.jpg";
    QPixmap test(exePath);
    if (!test.isNull())
    {
        m_bgPixmap = test;
        setupStaticBackground();
        return;
    }

    QPixmap resPixmap(":/moban/background.jpg");
    if (!resPixmap.isNull())
    {
        m_bgPixmap = resPixmap;
        setupStaticBackground();
        return;
    }

    setupGradientBackground();
}

void LoginDialog::setupGifBackground(const QString &gifPath)
{
    m_bgMovieLabel = new QLabel(this);
    m_bgMovieLabel->setGeometry(rect());
    m_bgMovieLabel->setAlignment(Qt::AlignCenter);
    m_bgMovieLabel->setScaledContents(true);

    QMovie *movie = new QMovie(gifPath);
    m_bgMovieLabel->setMovie(movie);
    movie->start();
    m_bgMovieLabel->show();
}

void LoginDialog::setupStaticBackground()
{
    m_bgMovieLabel = new QLabel(this);
    m_bgMovieLabel->setGeometry(rect());
    m_bgMovieLabel->setAlignment(Qt::AlignCenter);
    m_bgMovieLabel->show();

    updateBgPixmap();
}

void LoginDialog::updateBgPixmap()
{
    if (m_bgPixmap.isNull() || !m_bgMovieLabel)
    {
        return;
    }

    QPixmap scaled = m_bgPixmap.scaled(size(), Qt::KeepAspectRatioByExpanding,
                                        Qt::SmoothTransformation);
    m_bgMovieLabel->setPixmap(scaled);
}

void LoginDialog::setupGradientBackground()
{
    m_bgMovieLabel = new QLabel(this);
    m_bgMovieLabel->setGeometry(rect());
    m_bgMovieLabel->setStyleSheet(
        "background-color: #14182A;"
    );
    m_bgMovieLabel->show();
}

void LoginDialog::setupUi()
{
    m_cardWidget = new QWidget(this);
    m_cardWidget->setObjectName("loginCard");
    m_cardWidget->setFixedSize(400, 500);
    m_cardWidget->move((width() - 400) / 2, (height() - 500) / 2);

    QGraphicsDropShadowEffect *shadow = new QGraphicsDropShadowEffect(m_cardWidget);
    shadow->setBlurRadius(40);
    shadow->setColor(QColor(0, 0, 0, 160));
    shadow->setOffset(0, 8);
    m_cardWidget->setGraphicsEffect(shadow);

    QVBoxLayout *cardLayout = new QVBoxLayout(m_cardWidget);
    cardLayout->setContentsMargins(40, 36, 40, 36);
    cardLayout->setSpacing(0);

    m_iconLabel = new QLabel(m_cardWidget);
    QPixmap icon(":/moban/icons/app_icon.svg");
    if (!icon.isNull())
    {
        m_iconLabel->setPixmap(icon.scaled(56, 56, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
    m_iconLabel->setAlignment(Qt::AlignCenter);
    m_iconLabel->setFixedHeight(80);
    cardLayout->addWidget(m_iconLabel);

    m_titleLabel = new QLabel(QString::fromUtf8("遥感图像处理系统"), m_cardWidget);
    m_titleLabel->setObjectName("titleLabel");
    m_titleLabel->setAlignment(Qt::AlignCenter);
    cardLayout->addWidget(m_titleLabel);

    m_subtitleLabel = new QLabel(QString::fromUtf8("Remote Sensing Image Processing System"), m_cardWidget);
    m_subtitleLabel->setObjectName("subtitleLabel");
    m_subtitleLabel->setAlignment(Qt::AlignCenter);
    cardLayout->addWidget(m_subtitleLabel);

    cardLayout->addSpacing(28);

    QLabel *userIcon = new QLabel(m_cardWidget);
    userIcon->setFixedSize(48, 42);
    userIcon->setAlignment(Qt::AlignCenter);
    userIcon->setStyleSheet("background: rgba(255,255,255,0.06); border: 1px solid rgba(255,255,255,0.12); border-right: none; border-top-left-radius: 8px; border-bottom-left-radius: 8px; font-size: 16px; color: #A0A0B0;");
    userIcon->setText(QString::fromUtf8("\xF0\x9F\x91\xA4"));

    m_usernameEdit = new QLineEdit(m_cardWidget);
    m_usernameEdit->setObjectName("usernameEdit");
    m_usernameEdit->setPlaceholderText(QString::fromUtf8("用户名"));
    m_usernameEdit->setFixedHeight(42);

    QHBoxLayout *userLayout = new QHBoxLayout();
    userLayout->setSpacing(0);
    userLayout->setContentsMargins(0, 0, 0, 0);
    userLayout->addWidget(userIcon);
    userLayout->addWidget(m_usernameEdit);
    cardLayout->addLayout(userLayout);

    cardLayout->addSpacing(14);

    QLabel *passIcon = new QLabel(m_cardWidget);
    passIcon->setFixedSize(48, 42);
    passIcon->setAlignment(Qt::AlignCenter);
    passIcon->setStyleSheet("background: rgba(255,255,255,0.06); border: 1px solid rgba(255,255,255,0.12); border-right: none; border-top-left-radius: 8px; border-bottom-left-radius: 8px; font-size: 16px; color: #A0A0B0;");
    passIcon->setText(QString::fromUtf8("\xF0\x9F\x94\x92"));

    m_passwordEdit = new QLineEdit(m_cardWidget);
    m_passwordEdit->setObjectName("passwordEdit");
    m_passwordEdit->setPlaceholderText(QString::fromUtf8("密码"));
    m_passwordEdit->setEchoMode(QLineEdit::Password);
    m_passwordEdit->setFixedHeight(42);

    QHBoxLayout *passLayout = new QHBoxLayout();
    passLayout->setSpacing(0);
    passLayout->setContentsMargins(0, 0, 0, 0);
    passLayout->addWidget(passIcon);
    passLayout->addWidget(m_passwordEdit);
    cardLayout->addLayout(passLayout);

    cardLayout->addSpacing(10);

    m_errorLabel = new QLabel(m_cardWidget);
    m_errorLabel->setObjectName("errorLabel");
    m_errorLabel->setAlignment(Qt::AlignCenter);
    m_errorLabel->setFixedHeight(24);
    m_errorLabel->hide();
    cardLayout->addWidget(m_errorLabel);

    cardLayout->addSpacing(18);

    m_loginButton = new QPushButton(QString::fromUtf8("登  录"), m_cardWidget);
    m_loginButton->setObjectName("loginButton");
    m_loginButton->setFixedHeight(44);
    m_loginButton->setCursor(Qt::PointingHandCursor);
    cardLayout->addWidget(m_loginButton);

    cardLayout->addSpacing(16);

    QLabel *hintLabel = new QLabel(QString::fromUtf8("默认账户: admin / admin123"), m_cardWidget);
    hintLabel->setObjectName("hintLabel");
    hintLabel->setAlignment(Qt::AlignCenter);
    cardLayout->addWidget(hintLabel);

    cardLayout->addStretch();

    m_versionLabel = new QLabel(QString::fromUtf8("v1.0.0"), m_cardWidget);
    m_versionLabel->setObjectName("versionLabel");
    m_versionLabel->setAlignment(Qt::AlignCenter);
    cardLayout->addWidget(m_versionLabel);

    connect(m_loginButton, &QPushButton::clicked, this, [this]() {
        QString user = username();
        QString pass = password();

        if (user.isEmpty() || pass.isEmpty())
        {
            m_errorLabel->setText(QString::fromUtf8("请输入用户名和密码"));
            m_errorLabel->show();
            return;
        }

        if (user == "admin" && pass == "admin123")
        {
            accept();
        }
        else
        {
            m_errorLabel->setText(QString::fromUtf8("用户名或密码错误"));
            m_errorLabel->show();
            m_passwordEdit->clear();
            m_passwordEdit->setFocus();
        }
    });

    connect(m_passwordEdit, &QLineEdit::returnPressed, this, [this]() {
        m_loginButton->click();
    });

    connect(m_usernameEdit, &QLineEdit::returnPressed, this, [this]() {
        m_passwordEdit->setFocus();
    });

    applyStyle();
}

void LoginDialog::applyStyle()
{
    setStyleSheet(R"(
        #loginCard {
            background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                stop:0 rgba(30, 32, 42, 0.70),
                stop:0.5 rgba(24, 26, 36, 0.72),
                stop:1 rgba(18, 20, 30, 0.75));
            border: 1px solid rgba(255, 255, 255, 0.10);
            border-radius: 16px;
        }
        #titleLabel {
            font-size: 22px;
            font-weight: bold;
            color: #EAEAEC;
            padding: 2px 0;
        }
        #subtitleLabel {
            font-size: 11px;
            color: #707080;
            padding: 2px 0 4px 0;
        }
        #usernameEdit, #passwordEdit {
            background: rgba(255, 255, 255, 0.05);
            border: 1px solid rgba(255, 255, 255, 0.12);
            border-left: none;
            border-top-right-radius: 8px;
            border-bottom-right-radius: 8px;
            padding: 0 14px;
            font-size: 14px;
            color: #D4D4DC;
            selection-background-color: #4A6FFF;
            selection-color: #FFFFFF;
        }
        #usernameEdit:focus, #passwordEdit:focus {
            border: 1px solid rgba(74, 111, 255, 0.5);
            border-left: none;
            background: rgba(255, 255, 255, 0.08);
        }
        #usernameEdit::placeholder, #passwordEdit::placeholder {
            color: #5A5A6E;
        }
        #loginButton {
            background: qlineargradient(x1:0, y1:0, x2:1, y2:0,
                stop:0 #4A6FFF,
                stop:1 #6C5CE7);
            border: none;
            border-radius: 10px;
            color: #FFFFFF;
            font-size: 16px;
            font-weight: bold;
            letter-spacing: 4px;
        }
        #loginButton:hover {
            background: qlineargradient(x1:0, y1:0, x2:1, y2:0,
                stop:0 #5B7FFF,
                stop:1 #7D6DF7);
        }
        #loginButton:pressed {
            background: qlineargradient(x1:0, y1:0, x2:1, y2:0,
                stop:0 #3A5FEF,
                stop:1 #5C4CD7);
        }
        #errorLabel {
            font-size: 12px;
            color: #FF6B6B;
        }
        #hintLabel {
            font-size: 11px;
            color: #505060;
        }
        #versionLabel {
            font-size: 11px;
            color: #404050;
            padding-top: 8px;
        }
    )");
}

void LoginDialog::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
}

void LoginDialog::resizeEvent(QResizeEvent *event)
{
    QDialog::resizeEvent(event);

    if (m_bgMovieLabel)
    {
        m_bgMovieLabel->setGeometry(rect());
    }
    if (!m_bgPixmap.isNull())
    {
        updateBgPixmap();
    }
    if (m_cardWidget)
    {
        m_cardWidget->move((width() - m_cardWidget->width()) / 2,
                           (height() - m_cardWidget->height()) / 2);
    }
}

void LoginDialog::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
    {
        m_dragging = true;
        m_dragPosition = event->globalPos() - frameGeometry().topLeft();
        event->accept();
    }
}

void LoginDialog::mouseMoveEvent(QMouseEvent *event)
{
    if (m_dragging && (event->buttons() & Qt::LeftButton))
    {
        move(event->globalPos() - m_dragPosition);
        event->accept();
    }
}

void LoginDialog::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
    {
        m_dragging = false;
        event->accept();
    }
}
