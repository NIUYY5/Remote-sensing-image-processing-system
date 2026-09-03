#include "moban.h"
#include "gdal_loader.h"
#include "split_view.h"
#include "fusion_dialog.h"
#include "jihepeizhun.h"
#include "FeatureExtraction4.h"

#include <QtWidgets/QMenuBar>
#include <QtWidgets/QToolBar>
#include <QtWidgets/QStatusBar>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QInputDialog>
#include <QtWidgets/QApplication>
#include <QtWidgets/QGraphicsPixmapItem>
#include <QtWidgets/QFrame>
#include <QtGui/QPixmap>
#include <QtGui/QImageReader>
#include <QtGui/QIcon>
#include <QtCore/QDateTime>
#include <QtGui/QWheelEvent>

const double moban::ZoomStep = 1.25;
const double moban::MinZoom = 0.01;
const double moban::MaxZoom = 50.0;

moban::moban(QWidget *parent)
    : QMainWindow(parent)
    , m_graphicsScene(nullptr), m_graphicsView(nullptr), m_centralStack(nullptr)
    , m_splitView(nullptr)
    , m_dataDock(nullptr), m_dataTreeView(nullptr), m_dataModel(nullptr)
    , m_outputDock(nullptr), m_outputTextEdit(nullptr)
    , m_statusCoords(nullptr), m_statusZoom(nullptr), m_statusProject(nullptr)
    , m_fileMenu(nullptr), m_editMenu(nullptr), m_viewMenu(nullptr)
    , m_processMenu(nullptr), m_toolsMenu(nullptr), m_helpMenu(nullptr), m_recentMenu(nullptr)
    , m_actSplitView(nullptr), m_actToggleTheme(nullptr)
    , m_projectModified(false), m_currentZoom(1.0)
{
    ui.setupUi(this);

    // Create actions not defined in the .ui file
    m_actSplitView = new QAction(this);
    m_actSplitView->setText(QString::fromUtf8("分屏对比(&P)"));
    m_actSplitView->setToolTip(QString::fromUtf8("打开分屏对比视图"));

    m_actToggleTheme = new QAction(this);
    m_actToggleTheme->setText(QString::fromUtf8("浅色主题(&L)"));
    m_actToggleTheme->setCheckable(true);
    m_actToggleTheme->setChecked(false);

    GdalImageLoader::initialize();

    createCentralWidget();
    createDockWidgets();
    createStatusBar();
    createMenus();
    createToolBars();
    setupConnections();
    applyStyleSheet();

    loadRecentProjects();

    m_dataDock->show();
    m_outputDock->show();
    ui.mainToolBar->show();

    updateWindowTitle();

    ui.actUndo->setEnabled(false);
    ui.actRedo->setEnabled(false);

    appendLog(QString::fromUtf8("遥感图像处理系统 v2.0 已启动"));
    appendLog(QString::fromUtf8("GDAL %1 | 就绪。请新建工程或打开已有工程。")
        .arg(GdalImageLoader::version()));
    setStatusMessage(QString::fromUtf8("就绪"));
}

moban::~moban()
{
    GdalImageLoader::cleanup();
}

void moban::closeEvent(QCloseEvent *event)
{
    if (maybeSave())
    {
        QSettings settings(QString::fromUtf8("RSImageProcess"), QString::fromUtf8("RSIP"));
        settings.setValue("geometry", saveGeometry());
        settings.setValue("windowState", saveState());
        settings.setValue("lastProjectDir", m_lastProjectDir);
        settings.setValue("lastImageDir", m_lastImageDir);
        settings.setValue("recentProjects", m_recentProjects);
        event->accept();
    }
    else
    {
        event->ignore();
    }
}

bool moban::eventFilter(QObject *obj, QEvent *event)
{
    if (event->type() == QEvent::Wheel)
    {
        QWheelEvent *wheelEvent = static_cast<QWheelEvent *>(event);
        bool handled = false;

        if (obj == m_graphicsView->viewport())
            handled = true;
        else if (m_splitView && (obj == m_splitView->originalView()->viewport()
                              || obj == m_splitView->resultView()->viewport()))
            handled = true;

        if (handled)
        {
            if (wheelEvent->angleDelta().y() > 0) onZoomIn();
            else onZoomOut();
            return true;
        }
    }
    return QMainWindow::eventFilter(obj, event);
}

// ============================================================================
// UI Creation
// ============================================================================

void moban::createCentralWidget()
{
    m_centralStack = new QStackedWidget(this);

    m_graphicsScene = new QGraphicsScene(this);
    m_graphicsScene->setBackgroundBrush(QBrush(QColor(45, 45, 48)));

    m_graphicsView = new QGraphicsView(m_graphicsScene, this);
    m_graphicsView->setRenderHints(QPainter::Antialiasing);
    m_graphicsView->setOptimizationFlags(QGraphicsView::DontAdjustForAntialiasing);
    m_graphicsView->setDragMode(QGraphicsView::ScrollHandDrag);
    m_graphicsView->setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    m_graphicsView->setResizeAnchor(QGraphicsView::AnchorViewCenter);
    m_graphicsView->setViewportUpdateMode(QGraphicsView::SmartViewportUpdate);
    m_graphicsView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    m_graphicsView->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    m_graphicsView->setFrameShape(QFrame::NoFrame);
    m_graphicsView->viewport()->installEventFilter(this);
    m_centralStack->addWidget(m_graphicsView);

    m_splitView = new SplitViewWidget(this);
    m_centralStack->addWidget(m_splitView);

    m_splitView->originalView()->viewport()->installEventFilter(this);
    m_splitView->resultView()->viewport()->installEventFilter(this);

    m_centralStack->setCurrentIndex(0);
    setCentralWidget(m_centralStack);
}

void moban::createDockWidgets()
{
    m_dataDock = new QDockWidget(QString::fromUtf8("数据管理"), this);
    m_dataDock->setObjectName("DataDock");
    m_dataDock->setMinimumWidth(220);
    m_dataDock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);

    m_dataModel = new QStandardItemModel(this);
    m_dataModel->setHorizontalHeaderLabels(QStringList() << QString::fromUtf8("数据列表"));

    m_dataTreeView = new QTreeView(this);
    m_dataTreeView->setModel(m_dataModel);
    m_dataTreeView->setHeaderHidden(false);
    m_dataTreeView->setAnimated(true);
    m_dataTreeView->setIndentation(16);
    m_dataTreeView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_dataTreeView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_dataTreeView->setContextMenuPolicy(Qt::CustomContextMenu);
    m_dataTreeView->setIconSize(QSize(16, 16));
    m_dataDock->setWidget(m_dataTreeView);
    addDockWidget(Qt::LeftDockWidgetArea, m_dataDock);

    m_outputDock = new QDockWidget(QString::fromUtf8("输出"), this);
    m_outputDock->setObjectName("OutputDock");
    m_outputDock->setMinimumHeight(100);
    m_outputDock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);

    m_outputTextEdit = new QTextEdit(this);
    m_outputTextEdit->setReadOnly(true);
    m_outputTextEdit->setFont(QFont("Consolas", 9));
    m_outputTextEdit->document()->setMaximumBlockCount(2000);
    m_outputDock->setWidget(m_outputTextEdit);
    addDockWidget(Qt::BottomDockWidgetArea, m_outputDock);
}

void moban::createStatusBar()
{
    m_statusCoords = new QLabel(QString::fromUtf8("坐标: (0, 0)"), this);
    m_statusCoords->setMinimumWidth(200);
    m_statusCoords->setFrameStyle(QFrame::StyledPanel);

    m_statusZoom = new QLabel(QString::fromUtf8("缩放: 100%"), this);
    m_statusZoom->setMinimumWidth(120);
    m_statusZoom->setFrameStyle(QFrame::StyledPanel);

    m_statusProject = new QLabel(QString::fromUtf8("无工程"), this);
    m_statusProject->setMinimumWidth(180);
    m_statusProject->setFrameStyle(QFrame::StyledPanel);

    statusBar()->addPermanentWidget(m_statusProject);
    statusBar()->addPermanentWidget(m_statusCoords);
    statusBar()->addPermanentWidget(m_statusZoom);
}

void moban::createMenus()
{
    m_fileMenu = menuBar()->addMenu(QString::fromUtf8("文件(&F)"));
    m_fileMenu->addAction(ui.actNewProject);
    m_fileMenu->addAction(ui.actOpenProject);
    m_fileMenu->addAction(ui.actSaveProject);
    m_fileMenu->addAction(ui.actSaveProjectAs);
    m_fileMenu->addSeparator();
    m_fileMenu->addAction(ui.actImportImage);
    m_fileMenu->addSeparator();
    m_recentMenu = m_fileMenu->addMenu(QString::fromUtf8("最近工程"));
    m_recentMenu->setIcon(QIcon());
    m_fileMenu->addSeparator();
    m_fileMenu->addAction(ui.actCloseProject);
    m_fileMenu->addSeparator();
    m_fileMenu->addAction(ui.actExit);

    m_editMenu = menuBar()->addMenu(QString::fromUtf8("编辑(&E)"));
    m_editMenu->addAction(ui.actUndo);
    m_editMenu->addAction(ui.actRedo);

    m_viewMenu = menuBar()->addMenu(QString::fromUtf8("视图(&V)"));
    m_viewMenu->addAction(m_actSplitView);
    m_viewMenu->addSeparator();
    m_viewMenu->addAction(ui.actToggleToolBar);
    m_viewMenu->addAction(ui.actToggleDataDock);
    m_viewMenu->addAction(ui.actToggleOutputDock);
    m_viewMenu->addSeparator();
    m_viewMenu->addAction(ui.actZoomIn);
    m_viewMenu->addAction(ui.actZoomOut);
    m_viewMenu->addAction(ui.actZoomFit);
    m_viewMenu->addSeparator();
    m_viewMenu->addAction(m_actToggleTheme);

    m_processMenu = menuBar()->addMenu(QString::fromUtf8("图像处理(&P)"));
    m_processMenu->addAction(ui.actGeoReg);
    m_processMenu->addAction(ui.actImageFusion);
    m_processMenu->addAction(ui.actClassification);
    m_processMenu->addSeparator();

    m_toolsMenu = menuBar()->addMenu(QString::fromUtf8("工具(&T)"));
    m_toolsMenu->addAction(ui.actSettings);

    m_helpMenu = menuBar()->addMenu(QString::fromUtf8("帮助(&H)"));
    m_helpMenu->addAction(ui.actHelp);
    m_helpMenu->addSeparator();
    m_helpMenu->addAction(ui.actAbout);
}

void moban::createToolBars()
{
    QToolBar *tb = ui.mainToolBar;
    tb->setIconSize(QSize(24, 24));
    tb->setToolButtonStyle(Qt::ToolButtonIconOnly);

    tb->addAction(ui.actNewProject);
    tb->addAction(ui.actOpenProject);
    tb->addAction(ui.actSaveProject);
    tb->addSeparator();
    tb->addAction(ui.actImportImage);
    tb->addSeparator();
    tb->addAction(m_actSplitView);
    tb->addSeparator();
    tb->addAction(ui.actZoomIn);
    tb->addAction(ui.actZoomOut);
    tb->addAction(ui.actZoomFit);
    tb->addSeparator();
    tb->addAction(ui.actGeoReg);
    tb->addAction(ui.actImageFusion);
    tb->addAction(ui.actClassification);
}

void moban::setupConnections()
{
    connect(ui.actNewProject, &QAction::triggered, this, &moban::onNewProject);
    connect(ui.actOpenProject, &QAction::triggered, this, &moban::onOpenProject);
    connect(ui.actSaveProject, &QAction::triggered, this, &moban::onSaveProject);
    connect(ui.actSaveProjectAs, &QAction::triggered, this, &moban::onSaveProjectAs);
    connect(ui.actImportImage, &QAction::triggered, this, &moban::onImportImage);
    connect(ui.actCloseProject, &QAction::triggered, this, &moban::onCloseProject);
    connect(ui.actExit, &QAction::triggered, this, &moban::onExit);

    connect(ui.actUndo, &QAction::triggered, this, &moban::onUndo);
    connect(ui.actRedo, &QAction::triggered, this, &moban::onRedo);

    connect(ui.actZoomIn, &QAction::triggered, this, &moban::onZoomIn);
    connect(ui.actZoomOut, &QAction::triggered, this, &moban::onZoomOut);
    connect(ui.actZoomFit, &QAction::triggered, this, &moban::onZoomFit);
    connect(m_actSplitView, &QAction::triggered, this, &moban::onSplitView);
    connect(m_actToggleTheme, &QAction::toggled, this, &moban::onToggleTheme);

    connect(ui.actGeoReg, &QAction::triggered, this, &moban::onGeoReg);
    connect(ui.actImageFusion, &QAction::triggered, this, &moban::onImageFusion);
    connect(ui.actClassification, &QAction::triggered, this, &moban::onClassification);

    connect(ui.actSettings, &QAction::triggered, this, &moban::onSettings);
    connect(ui.actAbout, &QAction::triggered, this, &moban::onAbout);
    connect(ui.actHelp, &QAction::triggered, this, &moban::onHelp);

    connect(ui.actToggleDataDock, &QAction::toggled, this, &moban::onToggleDataDock);
    connect(ui.actToggleOutputDock, &QAction::toggled, this, &moban::onToggleOutputDock);
    connect(ui.actToggleToolBar, &QAction::toggled, this, &moban::onToggleToolBar);

    connect(m_dataDock, &QDockWidget::visibilityChanged, ui.actToggleDataDock, &QAction::setChecked);
    connect(m_outputDock, &QDockWidget::visibilityChanged, ui.actToggleOutputDock, &QAction::setChecked);
    connect(ui.mainToolBar, &QToolBar::visibilityChanged, ui.actToggleToolBar, &QAction::setChecked);

    connect(m_dataTreeView, &QTreeView::customContextMenuRequested, this, &moban::onDataTreeContextMenu);
    connect(m_dataTreeView, &QTreeView::clicked, this, &moban::onImageItemClicked);
}

void moban::applyStyleSheet()
{
    applyLightStyleSheet();
}

void moban::applyDarkStyleSheet()
{
    const QString style = R"(
        QMainWindow { background-color: #2D2D30; }
        QMenuBar { background-color: #3C3C40; color: #E0E0E0; border-bottom: 1px solid #505050; padding: 2px; }
        QMenuBar::item { padding: 4px 12px; background: transparent; border-radius: 4px; margin: 2px; }
        QMenuBar::item:selected { background-color: #505050; }
        QMenu { background-color: #3C3C40; color: #E0E0E0; border: 1px solid #505050; padding: 4px; }
        QMenu::item { padding: 6px 32px 6px 20px; border-radius: 3px; }
        QMenu::item:selected { background-color: #007ACC; }
        QMenu::item:disabled { color: #666666; }
        QMenu::separator { height: 1px; background: #505050; margin: 4px 10px; }
        QToolBar { background-color: #353538; border-bottom: 1px solid #505050; spacing: 4px; padding: 3px; }
        QToolBar::separator { width: 1px; background: #505050; margin: 4px 6px; }
        QToolButton { background: transparent; color: #CCCCCC; border: 1px solid transparent; border-radius: 4px; padding: 4px 8px; margin: 1px; }
        QToolButton:hover { background-color: #4A4A50; border: 1px solid #5A5A60; }
        QToolButton:pressed { background-color: #007ACC; border: 1px solid #007ACC; }
        QStatusBar { background-color: #007ACC; color: #FFFFFF; border-top: 1px solid #005FA3; font-size: 12px; min-height: 22px; }
        QStatusBar QLabel { color: #FFFFFF; padding: 2px 8px; }
        QDockWidget { color: #E0E0E0; titlebar-close-icon: none; }
        QDockWidget::title { background-color: #3C3C40; padding: 6px 8px; border-bottom: 1px solid #505050; text-align: left; }
        QTreeView { background-color: #252526; color: #D4D4D4; border: none; alternate-background-color: #2A2A2E; selection-background-color: #094771; selection-color: #FFFFFF; font-size: 13px; }
        QTreeView::item { padding: 3px 4px; }
        QTreeView::item:hover { background-color: #2A2D2E; }
        QHeaderView::section { background-color: #3C3C40; color: #CCCCCC; padding: 4px 8px; border: none; border-bottom: 1px solid #505050; font-size: 12px; }
        QTextEdit { background-color: #1E1E1E; color: #D4D4D4; border: none; font-family: "Consolas", "Courier New", monospace; font-size: 12px; selection-background-color: #094771; }
        QGraphicsView { background-color: #2D2D30; border: none; }
        QScrollBar:horizontal { background: #252526; height: 12px; border: none; }
        QScrollBar::handle:horizontal { background: #505050; border-radius: 4px; min-width: 30px; margin: 2px; }
        QScrollBar::handle:horizontal:hover { background: #686868; }
        QScrollBar:vertical { background: #252526; width: 12px; border: none; }
        QScrollBar::handle:vertical { background: #505050; border-radius: 4px; min-height: 30px; margin: 2px; }
        QScrollBar::handle:vertical:hover { background: #686868; }
        QScrollBar::add-line, QScrollBar::sub-line { background: none; border: none; }
    )";
    qApp->setStyleSheet(style);
}

void moban::applyLightStyleSheet()
{
    const QString style = R"(
        QMainWindow { background-color: #F5F5F5; }
        QMenuBar { background-color: #FFFFFF; color: #333333; border-bottom: 1px solid #DDDDDD; padding: 2px; }
        QMenuBar::item { padding: 4px 12px; background: transparent; border-radius: 4px; margin: 2px; }
        QMenuBar::item:selected { background-color: #E0E0E0; }
        QMenu { background-color: #FFFFFF; color: #333333; border: 1px solid #CCCCCC; padding: 4px; }
        QMenu::item { padding: 6px 32px 6px 20px; border-radius: 3px; }
        QMenu::item:selected { background-color: #0078D4; color: #FFFFFF; }
        QMenu::item:disabled { color: #AAAAAA; }
        QMenu::separator { height: 1px; background: #DDDDDD; margin: 4px 10px; }
        QToolBar { background-color: #F0F0F0; border-bottom: 1px solid #CCCCCC; spacing: 4px; padding: 3px; }
        QToolBar::separator { width: 1px; background: #CCCCCC; margin: 4px 6px; }
        QToolButton { background: transparent; color: #333333; border: 1px solid transparent; border-radius: 4px; padding: 4px 8px; margin: 1px; }
        QToolButton:hover { background-color: #E0E0E0; border: 1px solid #CCCCCC; }
        QToolButton:pressed { background-color: #0078D4; border: 1px solid #0078D4; color: #FFFFFF; }
        QStatusBar { background-color: #0078D4; color: #FFFFFF; border-top: 1px solid #0060A4; font-size: 12px; min-height: 22px; }
        QStatusBar QLabel { color: #FFFFFF; padding: 2px 8px; }
        QDockWidget { color: #333333; titlebar-close-icon: none; }
        QDockWidget::title { background-color: #E8E8E8; padding: 6px 8px; border-bottom: 1px solid #CCCCCC; text-align: left; }
        QTreeView { background-color: #FFFFFF; color: #333333; border: none; alternate-background-color: #F8F8F8; selection-background-color: #0078D4; selection-color: #FFFFFF; font-size: 13px; }
        QTreeView::item { padding: 3px 4px; }
        QTreeView::item:hover { background-color: #E8F0F8; }
        QHeaderView::section { background-color: #F0F0F0; color: #333333; padding: 4px 8px; border: none; border-bottom: 1px solid #CCCCCC; font-size: 12px; }
        QTextEdit { background-color: #FFFFFF; color: #333333; border: 1px solid #DDDDDD; font-family: "Consolas", "Courier New", monospace; font-size: 12px; selection-background-color: #0078D4; }
        QGraphicsView { background-color: #E8E8E8; border: none; }
        QScrollBar:horizontal { background: #F0F0F0; height: 12px; border: none; }
        QScrollBar::handle:horizontal { background: #C0C0C0; border-radius: 4px; min-width: 30px; margin: 2px; }
        QScrollBar::handle:horizontal:hover { background: #A0A0A0; }
        QScrollBar:vertical { background: #F0F0F0; width: 12px; border: none; }
        QScrollBar::handle:vertical { background: #C0C0C0; border-radius: 4px; min-height: 30px; margin: 2px; }
        QScrollBar::handle:vertical:hover { background: #A0A0A0; }
        QScrollBar::add-line, QScrollBar::sub-line { background: none; border: none; }
    )";
    qApp->setStyleSheet(style);
}

void moban::onToggleTheme(bool checked)
{
    if (checked)
        applyLightStyleSheet();
    else
        applyDarkStyleSheet();
}

// ============================================================================
// Title / Data / Recent / Logging
// ============================================================================

void moban::updateWindowTitle()
{
    QString title = QString::fromUtf8("遥感图像处理系统 v2.0");
    if (!m_project.name.isEmpty())
        title = QString::fromUtf8("%1%2 - %3")
            .arg(m_projectModified ? "* " : "").arg(m_project.name).arg(title);
    setWindowTitle(title);
}

void moban::updateDataModel()
{
    m_dataModel->clear();
    m_dataModel->setHorizontalHeaderLabels(QStringList() << QString::fromUtf8("数据列表"));
    if (m_project.name.isEmpty()) return;

    QStandardItem *projectItem = new QStandardItem();
    QIcon folderIcon = style()->standardIcon(QStyle::SP_DirIcon);
    if (!folderIcon.isNull()) projectItem->setIcon(folderIcon);
    projectItem->setText(m_project.name);
    projectItem->setFlags(projectItem->flags() & ~Qt::ItemIsEditable);

    QStandardItem *imagesItem = new QStandardItem(QString::fromUtf8("图像"));
    imagesItem->setFlags(imagesItem->flags() & ~Qt::ItemIsEditable);
    projectItem->appendRow(imagesItem);

    QIcon imageIcon = style()->standardIcon(QStyle::SP_FileIcon);
    for (const QString &imgPath : m_project.imagePaths)
    {
        QStandardItem *imgItem = new QStandardItem(QFileInfo(imgPath).fileName());
        if (!imageIcon.isNull()) imgItem->setIcon(imageIcon);
        imgItem->setData(imgPath, Qt::UserRole);
        imgItem->setFlags(imgItem->flags() & ~Qt::ItemIsEditable);
        imagesItem->appendRow(imgItem);
    }

    QStandardItem *resultsItem = new QStandardItem(QString::fromUtf8("处理结果"));
    resultsItem->setFlags(resultsItem->flags() & ~Qt::ItemIsEditable);
    projectItem->appendRow(resultsItem);

    QMap<QString, QString> typeLabels;
    typeLabels["georeg"] = QString::fromUtf8("几何配准");
    typeLabels["fusion"] = QString::fromUtf8("影像融合");
    typeLabels["classification"] = QString::fromUtf8("地物识别");
    typeLabels["mosaic"] = QString::fromUtf8("影像拼接");

    QIcon resultIcon = style()->standardIcon(QStyle::SP_FileDialogContentsView);
    for (auto it = m_project.results.begin(); it != m_project.results.end(); ++it)
    {
        QString groupLabel = typeLabels.value(it.key(), it.key());
        QStandardItem *groupItem = new QStandardItem(groupLabel);
        groupItem->setFlags(groupItem->flags() & ~Qt::ItemIsEditable);
        resultsItem->appendRow(groupItem);
        for (const QString &resPath : it.value())
        {
            QStandardItem *resItem = new QStandardItem(QFileInfo(resPath).fileName());
            if (!resultIcon.isNull()) resItem->setIcon(resultIcon);
            resItem->setData(resPath, Qt::UserRole);
            resItem->setFlags(resItem->flags() & ~Qt::ItemIsEditable);
            groupItem->appendRow(resItem);
        }
    }
    m_dataModel->appendRow(projectItem);
    m_dataTreeView->expandAll();
}

void moban::updateRecentProjects()
{
    m_recentMenu->clear();
    for (int i = 0; i < m_recentProjects.size(); ++i)
    {
        const QString &path = m_recentProjects.at(i);
        QAction *act = m_recentMenu->addAction(
            QString("%1  %2").arg(i + 1).arg(QFileInfo(path).fileName()));
        act->setData(path);
        connect(act, &QAction::triggered, this, [this, path]() {
            if (maybeSave()) loadProjectFromFile(path);
        });
    }
    m_recentMenu->setEnabled(!m_recentProjects.isEmpty());
}

void moban::addRecentProject(const QString &path)
{
    m_recentProjects.removeAll(path);
    m_recentProjects.prepend(path);
    while (m_recentProjects.size() > MaxRecentProjects)
        m_recentProjects.removeLast();
    updateRecentProjects();
}

void moban::loadRecentProjects()
{
    QSettings settings(QString::fromUtf8("RSImageProcess"), QString::fromUtf8("RSIP"));
    m_recentProjects = settings.value("recentProjects").toStringList();
    m_lastProjectDir = settings.value("lastProjectDir").toString();
    m_lastImageDir = settings.value("lastImageDir").toString();
    updateRecentProjects();
    if (settings.contains("geometry")) restoreGeometry(settings.value("geometry").toByteArray());
    if (settings.contains("windowState")) restoreState(settings.value("windowState").toByteArray());
}

bool moban::saveProjectToFile(const QString &filePath)
{
    QJsonObject root;
    root["version"] = "2.0";
    root["name"] = m_project.name;
    root["created"] = QDateTime::currentDateTime().toString(Qt::ISODate);
    QJsonArray imagesArray;
    for (const QString &p : m_project.imagePaths) imagesArray.append(p);
    root["images"] = imagesArray;

    QString projDir = QFileInfo(filePath).absolutePath();
    QJsonObject resultsObj;
    for (auto it = m_project.results.begin(); it != m_project.results.end(); ++it)
    {
        QJsonArray arr;
        for (const QString &resPath : it.value())
            arr.append(QFileInfo(resPath).isAbsolute()
                ? QDir(projDir).relativeFilePath(resPath) : resPath);
        resultsObj[it.key()] = arr;
    }
    root["results"] = resultsObj;

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        QMessageBox::critical(this, QString::fromUtf8("错误"),
            QString::fromUtf8("无法保存工程文件:\n%1").arg(file.errorString()));
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    file.close();
    m_project.filePath = filePath;
    m_projectModified = false;
    addRecentProject(filePath);
    updateWindowTitle();
    appendLog(QString::fromUtf8("工程已保存: %1").arg(filePath));
    setStatusMessage(QString::fromUtf8("工程已保存"));
    return true;
}

bool moban::loadProjectFromFile(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        QMessageBox::critical(this, QString::fromUtf8("错误"),
            QString::fromUtf8("无法打开工程文件:\n%1").arg(file.errorString()));
        return false;
    }
    QByteArray data = file.readAll();
    file.close();
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(data, &err);
    if (err.error != QJsonParseError::NoError)
    {
        QMessageBox::critical(this, QString::fromUtf8("错误"),
            QString::fromUtf8("工程文件格式错误:\n%1").arg(err.errorString()));
        return false;
    }

    QJsonObject root = doc.object();
    m_project.name = root["name"].toString();
    m_project.filePath = filePath;
    m_project.imagePaths.clear();
    for (const QJsonValue &v : root["images"].toArray())
        m_project.imagePaths.append(v.toString());

    m_project.results.clear();
    QString projDir = QFileInfo(filePath).absolutePath();
    QJsonObject ro = root["results"].toObject();
    for (auto it = ro.begin(); it != ro.end(); ++it)
    {
        QStringList paths;
        for (const QJsonValue &v : it.value().toArray())
            paths.append(QDir::cleanPath(QDir(projDir).absoluteFilePath(v.toString())));
        m_project.results[it.key()] = paths;
    }

    m_projectModified = false;
    addRecentProject(filePath);
    updateWindowTitle();
    updateDataModel();
    m_statusProject->setText(QString::fromUtf8("工程: %1").arg(m_project.name));
    appendLog(QString::fromUtf8("工程已打开: %1").arg(filePath));
    appendLog(QString::fromUtf8("包含 %1 张图像").arg(m_project.imagePaths.size()));
    if (!m_project.imagePaths.isEmpty())
        displayImage(m_project.imagePaths.first());
    setStatusMessage(QString::fromUtf8("工程已打开: %1").arg(m_project.name));
    return true;
}

void moban::appendLog(const QString &message)
{
    m_outputTextEdit->append(QDateTime::currentDateTime().toString("[hh:mm:ss] ") + message);
}

void moban::setStatusMessage(const QString &message, int timeout)
{
    statusBar()->showMessage(message, timeout);
}

QString moban::resultsDir() const
{
    if (!m_projectResultsDir.isEmpty()) return m_projectResultsDir;
    if (m_project.filePath.isEmpty()) return QString();
    return QFileInfo(m_project.filePath).absolutePath() + "/"
        + QFileInfo(m_project.filePath).completeBaseName() + "_data";
}

void moban::ensureResultsDir()
{
    QString dir = resultsDir();
    if (!dir.isEmpty()) QDir().mkpath(dir);
}

void moban::addProcessingResult(const QString &type, const QString &filePath)
{
    if (!m_project.results[type].contains(filePath))
    {
        m_project.results[type].append(filePath);
        m_projectModified = true;
        updateWindowTitle();
        updateDataModel();
    }
}

bool moban::maybeSave()
{
    if (!m_projectModified) return true;
    QMessageBox::StandardButton ret = QMessageBox::warning(this,
        QString::fromUtf8("遥感图像处理系统"),
        QString::fromUtf8("当前工程已被修改。\n是否保存更改？"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    switch (ret)
    {
    case QMessageBox::Save: return saveProjectToFile(m_project.filePath);
    case QMessageBox::Discard: return true;
    default: return false;
    }
}

// ============================================================================
// Image loading (GDAL)
// ============================================================================

QPixmap moban::loadImage(const QString &filePath)
{
    if (filePath.isEmpty()) return QPixmap();

    if (GdalImageLoader::canOpen(filePath))
    {
        QPixmap p = GdalImageLoader::preview(filePath, QSize(4096, 4096));
        if (!p.isNull())
        {
            GdalImageInfo info = GdalImageLoader::info(filePath);
            appendLog(QString::fromUtf8("[GDAL] %1 (%2x%3, %4波段, %5)")
                .arg(QFileInfo(filePath).fileName())
                .arg(info.rasterCountX).arg(info.rasterCountY)
                .arg(info.bandCount).arg(info.driverLongName));
            return p;
        }
    }

    QImageReader reader(filePath);
    reader.setAutoTransform(true);
    QSize origSize = reader.size();
    if (!origSize.isValid())
    {
        QPixmap pixmap(filePath);
        if (!pixmap.isNull())
        {
            appendLog(QString::fromUtf8("[Qt] %1 (%2x%3)")
                .arg(QFileInfo(filePath).fileName()).arg(pixmap.width()).arg(pixmap.height()));
            return pixmap;
        }
        appendLog(QString::fromUtf8("错误: 无法加载 %1").arg(QFileInfo(filePath).fileName()));
        return QPixmap();
    }

    QSize targetSize = origSize;
    if (origSize.width() > 4096 || origSize.height() > 4096)
        targetSize.scale(4096, 4096, Qt::KeepAspectRatio);
    reader.setScaledSize(targetSize);

    QImage img = reader.read();
    if (!img.isNull())
    {
        appendLog(QString::fromUtf8("[Qt] %1 (%2x%3, 原始: %4x%5)")
            .arg(QFileInfo(filePath).fileName())
            .arg(img.width()).arg(img.height())
            .arg(origSize.width()).arg(origSize.height()));
        return QPixmap::fromImage(img);
    }

    appendLog(QString::fromUtf8("错误: 无法加载 %1").arg(QFileInfo(filePath).fileName()));
    return QPixmap();
}

void moban::displayImage(const QString &filePath)
{
    bool isSplit = (m_centralStack->currentIndex() == 1);
    QPixmap pixmap = loadImage(filePath);
    if (pixmap.isNull()) return;

    if (isSplit)
    {
        m_splitView->setOriginalImage(pixmap, QFileInfo(filePath).fileName());
    }
    else
    {
        m_graphicsScene->clear();
        m_graphicsScene->addPixmap(pixmap);
        m_graphicsScene->setSceneRect(pixmap.rect());
        m_graphicsView->fitInView(m_graphicsScene->sceneRect(), Qt::KeepAspectRatio);
        m_currentZoom = m_graphicsView->transform().m11();
        m_statusZoom->setText(QString::fromUtf8("缩放: %1%")
            .arg(static_cast<int>(m_currentZoom * 100)));
    }
}

// ============================================================================
// File menu
// ============================================================================

void moban::onNewProject()
{
    if (!maybeSave()) return;
    bool ok;
    QString name = QInputDialog::getText(this, QString::fromUtf8("新建工程"),
        QString::fromUtf8("请输入工程名称:"), QLineEdit::Normal,
        QString::fromUtf8("新工程"), &ok);
    if (!ok || name.trimmed().isEmpty()) return;

    m_project.name = name.trimmed();
    m_project.filePath.clear();
    m_project.imagePaths.clear();
    m_project.results.clear();
    m_projectModified = true;
    m_projectResultsDir.clear();
    m_graphicsScene->clear();
    m_splitView->clear();
    m_centralStack->setCurrentIndex(0);
    m_dataModel->clear();
    m_dataModel->setHorizontalHeaderLabels(QStringList() << QString::fromUtf8("数据列表"));
    updateWindowTitle();
    updateDataModel();
    m_statusProject->setText(QString::fromUtf8("工程: %1").arg(m_project.name));
    m_statusCoords->setText(QString::fromUtf8("坐标: (0, 0)"));
    m_statusZoom->setText(QString::fromUtf8("缩放: 100%"));
    m_currentZoom = 1.0;
    appendLog(QString::fromUtf8("新建工程: %1").arg(m_project.name));
    setStatusMessage(QString::fromUtf8("新建工程: %1").arg(m_project.name));
}

void moban::onOpenProject()
{
    if (!maybeSave()) return;
    QString filePath = QFileDialog::getOpenFileName(this, QString::fromUtf8("打开工程"),
        m_lastProjectDir, QString::fromUtf8("工程文件 (*.rsproj *.json);;所有文件 (*.*)"));
    if (filePath.isEmpty()) return;
    m_lastProjectDir = QFileInfo(filePath).absolutePath();
    loadProjectFromFile(filePath);
}

void moban::onSaveProject()
{
    if (m_project.name.isEmpty()) return;
    if (m_project.filePath.isEmpty()) { onSaveProjectAs(); return; }
    saveProjectToFile(m_project.filePath);
}

void moban::onSaveProjectAs()
{
    if (m_project.name.isEmpty())
    {
        QMessageBox::information(this, QString::fromUtf8("提示"), QString::fromUtf8("请先新建或打开一个工程。"));
        return;
    }
    QString defaultName = m_project.name + ".rsproj";
    QString filePath = QFileDialog::getSaveFileName(this, QString::fromUtf8("工程另存为"),
        m_lastProjectDir.isEmpty() ? defaultName : m_lastProjectDir + "/" + defaultName,
        QString::fromUtf8("工程文件 (*.rsproj);;JSON文件 (*.json);;所有文件 (*.*)"));
    if (filePath.isEmpty()) return;
    m_lastProjectDir = QFileInfo(filePath).absolutePath();
    saveProjectToFile(filePath);
}

void moban::onImportImage()
{
    if (m_project.name.isEmpty())
    {
        QMessageBox::information(this, QString::fromUtf8("提示"), QString::fromUtf8("请先新建或打开一个工程。"));
        return;
    }

    QStringList filters;
    filters << QString::fromUtf8("常用图像格式 (*.png *.jpg *.jpeg *.bmp *.gif *.tif *.tiff)");
    filters << GdalImageLoader::gdalFormatFilter();
    filters << QString::fromUtf8("所有文件 (*.*)");

    QStringList filePaths = QFileDialog::getOpenFileNames(this,
        QString::fromUtf8("导入图像"),
        m_lastImageDir.isEmpty() ? m_lastProjectDir : m_lastImageDir,
        filters.join(";;"));
    if (filePaths.isEmpty()) return;

    m_lastImageDir = QFileInfo(filePaths.first()).absolutePath();

    for (const QString &fp : filePaths)
    {
        if (!m_project.imagePaths.contains(fp))
        {
            m_project.imagePaths.append(fp);
            appendLog(QString::fromUtf8("导入图像: %1").arg(fp));

            if (GdalImageLoader::canOpen(fp))
            {
                GdalImageInfo info = GdalImageLoader::info(fp);
                appendLog(QString::fromUtf8("  [GDAL] %1x%2, 波段:%3, 驱动:%4")
                    .arg(info.rasterCountX).arg(info.rasterCountY)
                    .arg(info.bandCount).arg(info.driverLongName));
                if (!info.projection.isEmpty())
                    appendLog(QString::fromUtf8("  [投影] %1").arg(info.projection.left(100)));
            }
        }
    }

    m_projectModified = true;
    updateWindowTitle();
    updateDataModel();
    if (!m_project.imagePaths.isEmpty())
        displayImage(m_project.imagePaths.last());
    setStatusMessage(QString::fromUtf8("已导入 %1 张图像").arg(filePaths.size()));
}

void moban::onCloseProject()
{
    if (!maybeSave()) return;
    m_project.name.clear();
    m_project.filePath.clear();
    m_project.imagePaths.clear();
    m_project.results.clear();
    m_projectModified = false;
    m_projectResultsDir.clear();
    m_graphicsScene->clear();
    m_splitView->clear();
    m_centralStack->setCurrentIndex(0);
    m_dataModel->clear();
    m_dataModel->setHorizontalHeaderLabels(QStringList() << QString::fromUtf8("数据列表"));
    updateWindowTitle();
    m_statusProject->setText(QString::fromUtf8("无工程"));
    m_statusCoords->setText(QString::fromUtf8("坐标: (0, 0)"));
    m_statusZoom->setText(QString::fromUtf8("缩放: 100%"));
    m_currentZoom = 1.0;
    appendLog(QString::fromUtf8("工程已关闭"));
    setStatusMessage(QString::fromUtf8("就绪"));
}

void moban::onExit() { close(); }

// ============================================================================
// Edit
// ============================================================================

void moban::onUndo()
{
    appendLog(QString::fromUtf8("撤销操作（待实现）"));
}

void moban::onRedo()
{
    appendLog(QString::fromUtf8("重做操作（待实现）"));
}

// ============================================================================
// Zoom
// ============================================================================

void moban::onZoomIn()
{
    if (m_centralStack->currentIndex() == 1)
    {
        m_splitView->zoomIn();
        m_currentZoom = m_splitView->zoomLevel();
    }
    else
    {
        if (m_currentZoom >= MaxZoom) return;
        m_graphicsView->scale(ZoomStep, ZoomStep);
        m_currentZoom *= ZoomStep;
    }
    m_statusZoom->setText(QString::fromUtf8("缩放: %1%").arg(static_cast<int>(m_currentZoom * 100)));
}

void moban::onZoomOut()
{
    if (m_centralStack->currentIndex() == 1)
    {
        m_splitView->zoomOut();
        m_currentZoom = m_splitView->zoomLevel();
    }
    else
    {
        if (m_currentZoom <= MinZoom) return;
        m_graphicsView->scale(1.0 / ZoomStep, 1.0 / ZoomStep);
        m_currentZoom /= ZoomStep;
    }
    m_statusZoom->setText(QString::fromUtf8("缩放: %1%").arg(static_cast<int>(m_currentZoom * 100)));
}

void moban::onZoomFit()
{
    if (m_centralStack->currentIndex() == 1)
    {
        m_splitView->zoomFit();
        m_currentZoom = m_splitView->zoomLevel();
    }
    else
    {
        m_graphicsView->fitInView(m_graphicsScene->sceneRect(), Qt::KeepAspectRatio);
        m_currentZoom = m_graphicsView->transform().m11();
    }
    m_statusZoom->setText(QString::fromUtf8("缩放: %1%").arg(static_cast<int>(m_currentZoom * 100)));
}

// ============================================================================
// Split View
// ============================================================================

void moban::onSplitView()
{
    if (m_centralStack->currentIndex() == 0)
    {
        if (m_project.imagePaths.isEmpty())
        {
            QMessageBox::information(this, QString::fromUtf8("提示"), QString::fromUtf8("请先导入图像。"));
            return;
        }
        QList<QGraphicsItem*> items = m_graphicsScene->items();
        if (!items.isEmpty())
        {
            QGraphicsPixmapItem *pi = dynamic_cast<QGraphicsPixmapItem*>(items.first());
            if (pi)
                m_splitView->setOriginalImage(pi->pixmap(),
                    QFileInfo(m_project.imagePaths.last()).fileName());
        }
        m_centralStack->setCurrentIndex(1);
        m_actSplitView->setText(QString::fromUtf8("单视图模式(&S)"));
        appendLog(QString::fromUtf8("切换到分屏对比视图"));
    }
    else
    {
        m_centralStack->setCurrentIndex(0);
        m_actSplitView->setText(QString::fromUtf8("分屏对比(&P)"));
        appendLog(QString::fromUtf8("切换到单视图模式"));
    }
}

// ============================================================================
// Processing - Integration with modules
// ============================================================================

void moban::onGeoReg()
{
    if (m_project.name.isEmpty())
    {
        QMessageBox::information(this, QString::fromUtf8("提示"), QString::fromUtf8("请先新建或打开一个工程。"));
        return;
    }

    appendLog(QString::fromUtf8("打开几何配准模块..."));

    jihepeizhun *dlg = new jihepeizhun(this);
    dlg->setWindowTitle(QString::fromUtf8("几何配准"));
    
    // Set output directory for results
    ensureResultsDir();
    dlg->setOutputDir(resultsDir());

    // Connect result signal
    connect(dlg, &jihepeizhun::resultReady, this, [this](const QString &type, const QStringList &paths) {
        for (const QString &path : paths)
        {
            addProcessingResult("georeg", path);
            appendLog(QString::fromUtf8("几何配准结果: %1").arg(QFileInfo(path).fileName()));
        }
        // Show first result in split view
        if (!paths.isEmpty())
        {
            QPixmap p = loadImage(paths.first());
            if (!p.isNull())
            {
                m_splitView->setResultImage(p, QFileInfo(paths.first()).fileName());
                m_splitView->setViewMode(SplitViewWidget::SideBySide);
                m_centralStack->setCurrentIndex(1);
                m_actSplitView->setText(QString::fromUtf8("单视图模式(&S)"));
            }
        }
    });

    dlg->exec();
    dlg->deleteLater();
}

void moban::onImageFusion()
{
    if (m_project.name.isEmpty())
    {
        QMessageBox::information(this, QString::fromUtf8("提示"), QString::fromUtf8("请先新建或打开一个工程。"));
        return;
    }

    appendLog(QString::fromUtf8("打开影像融合模块..."));

    ensureResultsDir();
    FusionDialog *dlg = new FusionDialog(this);
    dlg->setWindowTitle(QString::fromUtf8("影像融合 (PCA/HIS)"));

    // Pre-fill with project images if available
    if (!m_project.imagePaths.isEmpty())
    {
        dlg->setOutputPath(resultsDir() + "/fusion_result.tif");
    }

    if (dlg->exec() == QDialog::Accepted && dlg->isOutputSaved())
    {
        QString outPath = dlg->getOutputPath();
        if (!outPath.isEmpty())
        {
            // 立即释放融合对话框内存（MultiBandImage 可能很大）
            delete dlg;
            dlg = nullptr;

            // 处理待处理事件，让 UI 及时响应
            QApplication::processEvents();

            addProcessingResult("fusion", outPath);
            appendLog(QString::fromUtf8("影像融合结果: %1").arg(QFileInfo(outPath).fileName()));

            // Show result in split view
            QPixmap p = loadImage(outPath);
            if (!p.isNull())
            {
                m_splitView->setResultImage(p, QFileInfo(outPath).fileName());
                m_splitView->setViewMode(SplitViewWidget::SideBySide);
                m_centralStack->setCurrentIndex(1);
                m_actSplitView->setText(QString::fromUtf8("单视图模式(&S)"));
            }
        }
    }
    if (dlg)
        dlg->deleteLater();
}

void moban::onClassification()
{
    if (m_project.name.isEmpty())
    {
        QMessageBox::information(this, QString::fromUtf8("提示"), QString::fromUtf8("请先新建或打开一个工程。"));
        return;
    }

    appendLog(QString::fromUtf8("打开地物提取模块..."));

    ensureResultsDir();
    FeatureExtraction4 *dlg = new FeatureExtraction4(this);
    dlg->setWindowTitle(QString::fromUtf8("地物识别与提取"));
    dlg->setOutputDir(resultsDir());

    connect(dlg, &FeatureExtraction4::resultReady, this, [this](const QString &type, const QStringList &paths) {
        for (const QString &path : paths)
        {
            addProcessingResult("classification", path);
            appendLog(QString::fromUtf8("地物识别结果: %1").arg(QFileInfo(path).fileName()));
        }
        if (!paths.isEmpty())
        {
            QPixmap p = loadImage(paths.first());
            if (!p.isNull())
            {
                m_splitView->setResultImage(p, QFileInfo(paths.first()).fileName());
                m_splitView->setViewMode(SplitViewWidget::SideBySide);
                m_centralStack->setCurrentIndex(1);
                m_actSplitView->setText(QString::fromUtf8("单视图模式(&S)"));
            }
        }
    });

    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->show();
}

// ============================================================================
// Settings / About / Help
// ============================================================================

void moban::onSettings()
{
    QMessageBox::information(this, QString::fromUtf8("设置"),
        QString::fromUtf8("设置功能将在后续版本中实现。"));
}

void moban::onAbout()
{
    QMessageBox::about(this, QString::fromUtf8("关于遥感图像处理系统"),
        QString::fromUtf8(
            "<h3>遥感图像处理系统 v2.0</h3>"
            "<p>遥感科学与技术专业综合程序设计实习</p>"
            "<p>基于Qt 5.15.2 + GDAL 框架开发</p><br/>"
            "<p>主要功能:</p><ul>"
            "<li>基于卫星影像的几何配准</li>"
            "<li>基于配准影像的影像融合 (PCA/HIS)</li>"
            "<li>基于融合影像的地物识别与提取</li>"
            "<li>影像拼接与镶嵌</li>"
            "<li>GDAL遥感格式支持 (TIFF/ENVI/HDF)</li>"
            "<li>多视图分屏对比</li>"
            "</ul><br/>"
            "<p>开发环境: Visual Studio 2022 + Qt 5.15.2 + GDAL</p>"));
}

void moban::onHelp()
{
    QMessageBox::information(this, QString::fromUtf8("使用帮助"),
        QString::fromUtf8(
            "<h3>快捷操作</h3><table>"
            "<tr><td>Ctrl+N</td><td>新建工程</td></tr>"
            "<tr><td>Ctrl+O</td><td>打开工程</td></tr>"
            "<tr><td>Ctrl+S</td><td>保存工程</td></tr>"
            "<tr><td>Ctrl+I</td><td>导入图像</td></tr>"
            "<tr><td>Ctrl++</td><td>放大</td></tr>"
            "<tr><td>Ctrl+-</td><td>缩小</td></tr>"
            "<tr><td>Ctrl+0</td><td>适应窗口</td></tr>"
            "<tr><td>滚轮</td><td>缩放图像</td></tr>"
            "<tr><td>拖拽</td><td>平移图像</td></tr>"
            "</table><br/>"
            "<h3>工作流程</h3><ol>"
            "<li><b>新建/打开工程</b> → 导入遥感影像</li>"
            "<li><b>几何配准</b> → 图像处理 → 几何配准</li>"
            "<li><b>影像融合</b> → 图像处理 → 影像融合</li>"
            "<li><b>地物识别</b> → 图像处理 → 地物提取</li>"
            "<li><b>结果查看</b> → 左侧数据树 → 处理结果</li>"
            "</ol>"));
}

// ============================================================================
// Dock / Data Tree
// ============================================================================

void moban::onToggleDataDock(bool v) { m_dataDock->setVisible(v); }
void moban::onToggleOutputDock(bool v) { m_outputDock->setVisible(v); }
void moban::onToggleToolBar(bool v) { ui.mainToolBar->setVisible(v); }

void moban::onDataTreeContextMenu(const QPoint &pos)
{
    QModelIndex index = m_dataTreeView->indexAt(pos);
    if (!index.isValid()) return;
    QString imagePath = index.data(Qt::UserRole).toString();
    if (imagePath.isEmpty()) return;

    QMenu ctxMenu(m_dataTreeView);
    QAction *removeAct = ctxMenu.addAction(QString::fromUtf8("移除图像"));
    removeAct->setIcon(style()->standardIcon(QStyle::SP_TrashIcon));
    ctxMenu.addSeparator();
    QAction *compareAct = ctxMenu.addAction(QString::fromUtf8("分屏对比此图像"));
    QAction *infoAct = ctxMenu.addAction(QString::fromUtf8("查看图像信息"));

    QAction *sel = ctxMenu.exec(m_dataTreeView->viewport()->mapToGlobal(pos));
    if (sel == removeAct)
    {
        onRemoveImage();
    }
    else if (sel == compareAct)
    {
        QPixmap p = loadImage(imagePath);
        if (!p.isNull())
        {
            m_splitView->setOriginalImage(p, QFileInfo(imagePath).fileName());
            m_splitView->setViewMode(SplitViewWidget::SideBySide);
            m_centralStack->setCurrentIndex(1);
            m_actSplitView->setText(QString::fromUtf8("单视图模式(&S)"));
        }
    }
    else if (sel == infoAct)
    {
        QString info;
        if (GdalImageLoader::canOpen(imagePath))
        {
            GdalImageInfo g = GdalImageLoader::info(imagePath);
            info = QString::fromUtf8(
                "<h3>GDAL图像信息</h3><table>"
                "<tr><td>文件:</td><td>%1</td></tr>"
                "<tr><td>驱动:</td><td>%2 (%3)</td></tr>"
                "<tr><td>尺寸:</td><td>%4 x %5 像素</td></tr>"
                "<tr><td>波段:</td><td>%6</td></tr>"
                "<tr><td>投影:</td><td>%7</td></tr></table>")
                .arg(QFileInfo(imagePath).fileName())
                .arg(g.driverLongName, g.driverName)
                .arg(g.rasterCountX).arg(g.rasterCountY)
                .arg(g.bandCount)
                .arg(g.projection.isEmpty() ? QString::fromUtf8("无") : g.projection.left(100));
        }
        else
        {
            QPixmap pix(imagePath);
            info = QString::fromUtf8(
                "<h3>图像信息</h3><table>"
                "<tr><td>文件:</td><td>%1</td></tr>"
                "<tr><td>尺寸:</td><td>%2 x %3 像素</td></tr></table>")
                .arg(QFileInfo(imagePath).fileName())
                .arg(pix.width()).arg(pix.height());
        }
        QMessageBox::information(this, QString::fromUtf8("图像信息"), info);
    }
}

void moban::onRemoveImage()
{
    QModelIndex index = m_dataTreeView->currentIndex();
    if (!index.isValid()) return;
    QString imagePath = index.data(Qt::UserRole).toString();
    if (imagePath.isEmpty()) return;

    if (QMessageBox::question(this, QString::fromUtf8("确认移除"),
        QString::fromUtf8("确定移除 \"%1\"？").arg(QFileInfo(imagePath).fileName()),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
        return;

    m_project.imagePaths.removeAll(imagePath);
    m_projectModified = true;
    updateWindowTitle();
    updateDataModel();

    if (m_project.imagePaths.isEmpty())
    {
        m_graphicsScene->clear();
        m_splitView->clear();
        m_currentZoom = 1.0;
        m_statusZoom->setText(QString::fromUtf8("缩放: 100%"));
    }
    else
        displayImage(m_project.imagePaths.first());

    appendLog(QString::fromUtf8("已移除: %1").arg(imagePath));
    setStatusMessage(QString::fromUtf8("图像已移除"));
}

void moban::onImageItemClicked(const QModelIndex &index)
{
    QString imagePath = index.data(Qt::UserRole).toString();
    if (imagePath.isEmpty()) return;
    displayImage(imagePath);
    appendLog(QString::fromUtf8("显示: %1").arg(QFileInfo(imagePath).fileName()));
}
