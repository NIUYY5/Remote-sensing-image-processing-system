#pragma once

#include <QtWidgets/QDialog>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QLabel>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QHBoxLayout>
#include <QtGui/QMouseEvent>
#include <QtGui/QPixmap>
#include <QtCore/QDir>
#include <QtCore/QFileInfoList>

class LoginDialog : public QDialog
{
    Q_OBJECT

public:
    explicit LoginDialog(QWidget *parent = nullptr);
    ~LoginDialog();

    QString username() const;
    QString password() const;

protected:
    void resizeEvent(QResizeEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    void setupUi();
    void applyStyle();
    void findWallpaper();
    QString findSteamPath() const;
    void setupGifBackground(const QString &gifPath);
    void setupStaticBackground();
    void setupGradientBackground();
    void updateBgPixmap();

    QLineEdit *m_usernameEdit;
    QLineEdit *m_passwordEdit;
    QPushButton *m_loginButton;
    QLabel *m_titleLabel;
    QLabel *m_subtitleLabel;
    QLabel *m_versionLabel;
    QLabel *m_errorLabel;
    QLabel *m_iconLabel;
    QWidget *m_cardWidget;

    QLabel *m_bgMovieLabel;
    QPixmap m_bgPixmap;

    bool m_dragging;
    QPoint m_dragPosition;
};
