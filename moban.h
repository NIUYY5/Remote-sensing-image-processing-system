#pragma once

#include <QtWidgets/QMainWindow>
#include <QtWidgets/QGraphicsView>
#include <QtWidgets/QGraphicsScene>
#include <QtWidgets/QDockWidget>
#include <QtWidgets/QTreeView>
#include <QtWidgets/QTextEdit>
#include <QtWidgets/QLabel>
#include <QtWidgets/QMenu>
#include <QtWidgets/QAction>
#include <QtWidgets/QStackedWidget>
#include <QtGui/QStandardItemModel>
#include <QtGui/QCloseEvent>
#include <QtCore/QEvent>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonArray>
#include <QtCore/QSettings>
#include <QtCore/QStringList>
#include <QtCore/QDir>
#include <QtCore/QFileInfo>
#include <QtCore/QMap>

#include "ui_moban.h"

class SplitViewWidget;
class FusionDialog;
class jihepeizhun;
class FeatureExtraction4;

struct ProjectData {
    QString name;
    QString filePath;
    QStringList imagePaths;
    QMap<QString, QStringList> results;  // key: "georeg"/"fusion"/"classification"
};

class moban : public QMainWindow
{
    Q_OBJECT

public:
    moban(QWidget *parent = nullptr);
    ~moban();

protected:
    void closeEvent(QCloseEvent *event) override;
    bool eventFilter(QObject *obj, QEvent *event) override;

private slots:
    // File
    void onNewProject();
    void onOpenProject();
    void onSaveProject();
    void onSaveProjectAs();
    void onImportImage();
    void onCloseProject();
    void onExit();

    // Edit
    void onUndo();
    void onRedo();

    // View
    void onZoomIn();
    void onZoomOut();
    void onZoomFit();
    void onSplitView();
    void onToggleTheme(bool checked);

    // Processing
    void onGeoReg();
    void onImageFusion();
    void onClassification();

    // Settings/Help
    void onSettings();
    void onAbout();
    void onHelp();

    // Dock
    void onToggleDataDock(bool visible);
    void onToggleOutputDock(bool visible);
    void onToggleToolBar(bool visible);

    // Data tree
    void onDataTreeContextMenu(const QPoint &pos);
    void onRemoveImage();
    void onImageItemClicked(const QModelIndex &index);

private:
    void createMenus();
    void createToolBars();
    void createDockWidgets();
    void createCentralWidget();
    void createStatusBar();
    void setupConnections();
    void applyStyleSheet();
    void applyLightStyleSheet();
    void applyDarkStyleSheet();

    void updateWindowTitle();
    void updateDataModel();
    void updateRecentProjects();
    void addRecentProject(const QString &path);
    void loadRecentProjects();

    bool saveProjectToFile(const QString &filePath);
    bool loadProjectFromFile(const QString &filePath);

    void appendLog(const QString &message);
    void setStatusMessage(const QString &message, int timeout = 5000);

    bool maybeSave();
    QString resultsDir() const;
    void ensureResultsDir();
    void addProcessingResult(const QString &type, const QString &filePath);

    QPixmap loadImage(const QString &filePath);
    void displayImage(const QString &filePath);

    Ui::mobanClass ui;

    QGraphicsScene *m_graphicsScene;
    QGraphicsView *m_graphicsView;
    QStackedWidget *m_centralStack;

    SplitViewWidget *m_splitView;

    QDockWidget *m_dataDock;
    QTreeView *m_dataTreeView;
    QStandardItemModel *m_dataModel;

    QDockWidget *m_outputDock;
    QTextEdit *m_outputTextEdit;

    QLabel *m_statusCoords;
    QLabel *m_statusZoom;
    QLabel *m_statusProject;

    QMenu *m_fileMenu;
    QMenu *m_editMenu;
    QMenu *m_viewMenu;
    QMenu *m_processMenu;
    QMenu *m_toolsMenu;
    QMenu *m_helpMenu;
    QMenu *m_recentMenu;

    QAction *m_actSplitView;
    QAction *m_actToggleTheme;

    ProjectData m_project;
    bool m_projectModified;
    QString m_projectResultsDir;

    QString m_lastProjectDir;
    QString m_lastImageDir;

    QStringList m_recentProjects;
    static const int MaxRecentProjects = 5;

    double m_currentZoom;
    static const double ZoomStep;
    static const double MinZoom;
    static const double MaxZoom;
};
