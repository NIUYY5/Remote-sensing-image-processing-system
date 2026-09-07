// ============================================================================
// 文件: zhupeizhun.cpp
// 功能: 遥感影像几何配准系统 — 主窗口实现
//       包含影像加载、控制点管理、自动匹配、几何配准、导出等核心功能
// ============================================================================

#include "jihepeizhun.h"
#include "geomodel.h"
#include "automatch.h"
#include "orthodialog.h"
#include "accuracydialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QMessageBox>
#include <QHeaderView>
#include <QApplication>
#include <QGroupBox>
#include <QSplitter>
#include <QFile>  
#include <QTextStream>
#include <QCoreApplication>
#include <QRegularExpression>
#include <QSet>
#include <QDebug>
#include <QProgressDialog>
#include <QStatusBar>
#include <QMenuBar>
#include <QDialogButtonBox>
#include <algorithm>
#include <cmath>

// GDAL 头文件
#include "gdal_priv.h"
#include "cpl_conv.h"

// ============================================================================
// 构造函数: jihepeizhun
// 功能: 初始化主窗口, 包括 GDAL 运行环境配置、UI 搭建、刺点管理器创建
// 流程:
//   1. 设置 GDAL 数据文件搜索路径 (GDAL_DATA / PROJ_LIB)
//   2. 注册所有 GDAL 驱动 (GDALAllRegister)
//   3. 调用 setupUI / setupMenuBar / setupToolBar / setupStatusBar / setupControlPanel
//      搭建完整用户界面
//   4. 调用 setupConnections 连接信号与槽
//   5. 创建 PickManager 刺点管理器, 连接 pairReady 和 statusChanged 信号
//   6. 设置窗口标题、默认尺寸、最小尺寸
// ============================================================================
jihepeizhun::jihepeizhun(QWidget* parent)
    : QDialog(parent)
    , m_pickManager(nullptr)     // 刺点管理器初始化为空, 稍后在 setupConnections 后创建
    , m_isRegistered(false)      // 配准状态标记: 初始为未配准
{
    // ------------------------------------------------------------------
    // 步骤1: 配置 GDAL 运行环境
    //   告诉 GDAL 去哪里查找坐标系统参数文件和数据文件。
    //   搜索优先级: 1) 系统环境变量 GDAL_DATA / PROJ_LIB
    //               2) 可执行文件同目录下的 gdal-data / proj 子目录
    //               3) GDAL 默认安装路径
    // ------------------------------------------------------------------
    QByteArray exeDir = QCoreApplication::applicationDirPath().toUtf8();
    CPLSetConfigOption("GDAL_DATA", (exeDir + "/gdal-data").constData());
    CPLSetConfigOption("PROJ_LIB", (exeDir + "/proj").constData());

    // ------------------------------------------------------------------
    // 步骤2: 注册所有 GDAL 驱动, 使程序支持多种栅格格式 (GeoTIFF, IMG, JPEG, PNG ...)
    // ------------------------------------------------------------------
    GDALAllRegister();

    // ------------------------------------------------------------------
    // 步骤3: 搭建用户界面 — QDialog 布局
    //   使用 QVBoxLayout 组织: 菜单栏 → 工具栏 → 分割器 → 控制面板 → 状态栏 → 关闭按钮
    // ------------------------------------------------------------------

    // 创建主布局
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // 创建菜单栏并添加到布局
    m_menuBar = new QMenuBar(this);
    mainLayout->addWidget(m_menuBar);

    // 创建工具栏并添加到布局
    m_toolBar = new QToolBar(QString::fromUtf8("主工具栏"), this);
    m_toolBar->setMovable(false);
    m_toolBar->setIconSize(QSize(26, 20));
    mainLayout->addWidget(m_toolBar);

    // 创建左右分栏的双视图布局 (setupUI 创建 m_splitter)
    setupUI();
    mainLayout->addWidget(m_splitter, 1);

    // 创建状态栏
    m_statusBar = new QStatusBar(this);
    setupStatusBar();
    mainLayout->addWidget(m_statusBar);

    // 创建控制面板
    setupControlPanel();

    // 将控制面板添加到分割器下方, 占 25% 宽度
    QHBoxLayout* controlLayout = new QHBoxLayout();
    controlLayout->addStretch(3);           // 75% 空白
    controlLayout->addWidget(m_controlPanel, 1);  // 25% 宽度
    mainLayout->addLayout(controlLayout);

    // 添加 QDialogButtonBox (关闭按钮)
    QDialogButtonBox* buttonBox = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLayout->addWidget(buttonBox);

    // 设置菜单栏和工具栏内容
    setupMenuBar();
    setupToolBar();
    setupConnections();

    // ------------------------------------------------------------------
    // 步骤4: 创建刺点管理器
    //   必须在 setupConnections 之后创建, 确保 m_pickAction 已存在。
    //   PickManager 负责在左右视图中同步拾取同名点对。
    // ------------------------------------------------------------------
    m_pickManager = new PickManager(m_srcView, m_refView,
                                      m_statusBar, m_pickAction, this);
    connect(m_pickManager, &PickManager::pairReady,
            this, &jihepeizhun::onPickPairReady);
    connect(m_pickManager, &PickManager::statusChanged,
            this, [this](const QString& msg) { if (m_statusLabel) m_statusLabel->setText(msg); });

    // 初始化为浏览模式 (非刺点模式)
    m_pickManager->stopPicking();

    // 设置窗口基本属性
    setWindowTitle(QString::fromUtf8("遥感影像几何配准系统"));
    resize(1400, 850);
    setMinimumSize(1000, 550);
    updatePickStatus();
}

// ============================================================================
// 析构函数: 停止刺点管理器, 释放资源
// ============================================================================
jihepeizhun::~jihepeizhun()
{
    if (m_pickManager) m_pickManager->stopPicking();
}

// ============================================================================
// setupUI — 搭建中央双视图布局
// 功能: 创建左右分栏的影像显示区域, 左侧为源影像(待配准), 右侧为参考影像
// 布局: 使用 QSplitter 水平分割, 左右各占一半 (stretchFactor 均为 1)
// ============================================================================
void jihepeizhun::setupUI()
{
    // 创建源影像视图 (左侧) 和参考影像视图 (右侧)
    m_srcView = new ImageView(this);
    m_refView = new ImageView(this);

    // 使用 QSplitter 实现可拖拽调整大小的左右分栏布局
    m_splitter = new QSplitter(Qt::Horizontal, this);
    m_splitter->addWidget(m_srcView);
    m_splitter->addWidget(m_refView);
    m_splitter->setStretchFactor(0, 1);   // 源视图伸缩因子
    m_splitter->setStretchFactor(1, 1);   // 参考视图伸缩因子
}

// ============================================================================
// setupMenuBar — 创建菜单栏
// 菜单分组:
//   文件(&F)   — 影像加载、保存配准结果、导出控制点、退出
//   配准(&G)   — 刺点、控制点管理、自动匹配、执行配准、正射校正
//   视图(&V)   — 适应窗口、放大、缩小
//   帮助(&H)   — 关于信息
// ============================================================================
void jihepeizhun::setupMenuBar()
{
    // ------------------------------------------------------------------
    // 【文件操作】组: 打开源/参考影像、保存配准结果、导出控制点、退出
    // ------------------------------------------------------------------
    QMenu* fileMenu = m_menuBar->addMenu(QString::fromUtf8("文件(&F)"));

    QAction* openSrcAct = fileMenu->addAction(QString::fromUtf8("打开源影像(&S)..."));
    openSrcAct->setShortcut(QKeySequence("Ctrl+O"));
    connect(openSrcAct, &QAction::triggered, this, &jihepeizhun::onOpenSourceImage);

    QAction* openRefAct = fileMenu->addAction(QString::fromUtf8("打开参考影像(&R)..."));
    openRefAct->setShortcut(QKeySequence("Ctrl+Shift+O"));
    connect(openRefAct, &QAction::triggered, this, &jihepeizhun::onOpenRefImage);

    fileMenu->addSeparator();

    QAction* saveAct = fileMenu->addAction(QString::fromUtf8("保存配准结果(&E)..."));
    saveAct->setShortcut(QKeySequence("Ctrl+S"));
    connect(saveAct, &QAction::triggered, this, &jihepeizhun::onSaveRegisteredImage);

    fileMenu->addSeparator();

    QAction* exportAct = fileMenu->addAction(QString::fromUtf8("导出控制点(&X)..."));
    connect(exportAct, &QAction::triggered, this, &jihepeizhun::onExportControlPoints);

    fileMenu->addSeparator();

    QAction* exitAct = fileMenu->addAction(QString::fromUtf8("退出(&Q)"));
    exitAct->setShortcut(QKeySequence("Alt+F4"));
    connect(exitAct, &QAction::triggered, this, &QDialog::reject);

    // ------------------------------------------------------------------
    // 【配准操作】组: 刺点开关、控制点管理(清除/删除/导入)、自动匹配、执行配准、正射校正
    // ------------------------------------------------------------------
    QMenu* regMenu = m_menuBar->addMenu(QString::fromUtf8("配准(&G)"));

    m_pickAction = regMenu->addAction(QString::fromUtf8("开始/停止刺点(&P)"));
    m_pickAction->setShortcut(QKeySequence("P"));
    m_pickAction->setCheckable(true);
    connect(m_pickAction, &QAction::triggered, this, &jihepeizhun::onTogglePickMode);

    regMenu->addSeparator();

    QAction* clearAct = regMenu->addAction(QString::fromUtf8("清除所有控制点(&C)"));
    connect(clearAct, &QAction::triggered, this, &jihepeizhun::onClearControlPoints);

    QAction* deleteLastAct = regMenu->addAction(QString::fromUtf8("删除上一个控制点(&D)"));
    deleteLastAct->setShortcut(QKeySequence("Ctrl+Z"));
    connect(deleteLastAct, &QAction::triggered, this, &jihepeizhun::onDeleteLastPoint);

    QAction* deleteSelAct = regMenu->addAction(QString::fromUtf8("删除选中的控制点(&S)"));
    deleteSelAct->setShortcut(QKeySequence(Qt::Key_Delete));
    connect(deleteSelAct, &QAction::triggered, this, &jihepeizhun::onDeleteSelectedPoint);

    QAction* importAct = regMenu->addAction(QString::fromUtf8("导入控制点(&I)..."));
    importAct->setShortcut(QKeySequence("Ctrl+I"));
    connect(importAct, &QAction::triggered, this, &jihepeizhun::onImportControlPoints);

    regMenu->addSeparator();

    QAction* autoMatchAct = regMenu->addAction(QString::fromUtf8("自动匹配控制点(&M)..."));
    autoMatchAct->setShortcut(QKeySequence("Ctrl+M"));
    connect(autoMatchAct, &QAction::triggered, this, &jihepeizhun::onAutoMatch);

    regMenu->addSeparator();

    m_runAction = regMenu->addAction(QString::fromUtf8("执行配准(&R)"));
    m_runAction->setShortcut(QKeySequence("Ctrl+R"));
    connect(m_runAction, &QAction::triggered, this, &jihepeizhun::onRunRegistration);

    regMenu->addSeparator();

    QAction* orthoAct = regMenu->addAction(QString::fromUtf8("正射校正(&O)..."));
    orthoAct->setToolTip(QString::fromUtf8("对单影像执行多项式正射校正"));
    connect(orthoAct, &QAction::triggered, this, &jihepeizhun::onOrthoCorrection);

    // ------------------------------------------------------------------
    // 【视图操作】组: 适应窗口、放大、缩小
    // ------------------------------------------------------------------
    QMenu* viewMenu = m_menuBar->addMenu(QString::fromUtf8("视图(&V)"));

    QAction* fitAct = viewMenu->addAction(QString::fromUtf8("适应窗口(&F)"));
    fitAct->setShortcut(QKeySequence("Ctrl+0"));
    connect(fitAct, &QAction::triggered, this, &jihepeizhun::onFitToWindow);

    QAction* zoomInAct = viewMenu->addAction(QString::fromUtf8("放大(&I)"));
    zoomInAct->setShortcut(QKeySequence("Ctrl+="));
    connect(zoomInAct, &QAction::triggered, this, &jihepeizhun::onZoomIn);

    QAction* zoomOutAct = viewMenu->addAction(QString::fromUtf8("缩小(&O)"));
    zoomOutAct->setShortcut(QKeySequence("Ctrl+-"));
    connect(zoomOutAct, &QAction::triggered, this, &jihepeizhun::onZoomOut);

    // ------------------------------------------------------------------
    // 【帮助】组: 关于信息
    // ------------------------------------------------------------------
    QMenu* helpMenu = m_menuBar->addMenu(QString::fromUtf8("帮助(&H)"));
    QAction* aboutAct = helpMenu->addAction(QString::fromUtf8("关于(&A)"));
    connect(aboutAct, &QAction::triggered, this, &jihepeizhun::onAbout);

    // ------------------------------------------------------------------
    // 【重叠/特征】组: 重叠区域分析和特征点操作
    // ------------------------------------------------------------------
    auto* overlapMenu = m_menuBar->addMenu(QString::fromUtf8("重叠/特征"));

    auto* actOverlap = overlapMenu->addAction(QString::fromUtf8("计算重叠区域"));
    connect(actOverlap, &QAction::triggered, this, &jihepeizhun::onComputeOverlap);

    auto* actExportOverlap = overlapMenu->addAction(QString::fromUtf8("导出重叠区域"));
    connect(actExportOverlap, &QAction::triggered, this, &jihepeizhun::onExportOverlap);

    auto* actExtractFeatures = overlapMenu->addAction(QString::fromUtf8("提取并显示特征点"));
    connect(actExtractFeatures, &QAction::triggered, this, &jihepeizhun::onExtractFeatures);
}

// ============================================================================
// setupToolBar — 创建主工具栏
// 功能: 提供影像加载、刺点、配准、控制点导入、自动匹配、正射校正、适应窗口
//       等常用操作的快捷按钮
// ============================================================================
void jihepeizhun::setupToolBar()
{
    // 打开源影像按钮
    QAction* openSrcTb = m_toolBar->addAction(QString::fromUtf8("源影像"));
    openSrcTb->setToolTip(QString::fromUtf8("打开待配准的源影像"));
    connect(openSrcTb, &QAction::triggered, this, &jihepeizhun::onOpenSourceImage);

    // 打开参考影像按钮
    QAction* openRefTb = m_toolBar->addAction(QString::fromUtf8("参考影像"));
    openRefTb->setToolTip(QString::fromUtf8("打开参考影像"));
    connect(openRefTb, &QAction::triggered, this, &jihepeizhun::onOpenRefImage);

    m_toolBar->addSeparator();

    // 刺点模式开关按钮 (可切换, 与菜单栏 m_pickAction 联动)
    QAction* pickTb = m_toolBar->addAction(QString::fromUtf8("刺点"));
    pickTb->setCheckable(true);
    pickTb->setToolTip(QString::fromUtf8("开始/停止控制点选取"));
    connect(pickTb, &QAction::toggled, this, [this](bool checked) {
        m_pickAction->setChecked(checked);
        onTogglePickMode();
    });

    // 执行配准按钮
    QAction* runTb = m_toolBar->addAction(QString::fromUtf8("执行配准"));
    runTb->setToolTip(QString::fromUtf8("基于当前控制点执行几何配准"));
    connect(runTb, &QAction::triggered, this, &jihepeizhun::onRunRegistration);

    m_toolBar->addSeparator();

    // 导入控制点按钮
    QAction* importTb = m_toolBar->addAction(QString::fromUtf8("导入控制点"));
    importTb->setToolTip(QString::fromUtf8("从外部文件导入控制点"));
    connect(importTb, &QAction::triggered, this, &jihepeizhun::onImportControlPoints);

    m_toolBar->addSeparator();

    // 自动匹配按钮
    QAction* autoMatchTb = m_toolBar->addAction(QString::fromUtf8("自动匹配"));
    autoMatchTb->setToolTip(QString::fromUtf8("基于角点特征自动识别同名点"));
    connect(autoMatchTb, &QAction::triggered, this, &jihepeizhun::onAutoMatch);

    // 正射校正按钮
    QAction* orthoTb = m_toolBar->addAction(QString::fromUtf8("正射校正"));
    orthoTb->setToolTip(QString::fromUtf8("对单影像执行多项式正射校正"));
    connect(orthoTb, &QAction::triggered, this, &jihepeizhun::onOrthoCorrection);

    m_toolBar->addSeparator();

    // 适应窗口按钮
    QAction* fitTb = m_toolBar->addAction(QString::fromUtf8("适应窗口"));
    connect(fitTb, &QAction::triggered, this, &jihepeizhun::onFitToWindow);
}

// ============================================================================
// setupStatusBar — 创建状态栏
// 功能: 在状态栏右侧永久显示源影像和参考影像的鼠标坐标位置
//       使用两个 QLabel 分别指示, 在鼠标移动时由 onSrcMouseMoved /
//       onRefMouseMoved 更新坐标数值
// ============================================================================
void jihepeizhun::setupStatusBar()
{
    m_srcCoordLabel = new QLabel(QString::fromUtf8("源影像: 未加载"));
    m_refCoordLabel = new QLabel(QString::fromUtf8("参考影像: 未加载"));

    m_statusBar->addPermanentWidget(m_srcCoordLabel, 1);
    m_statusBar->addPermanentWidget(m_refCoordLabel, 1);
}

// ============================================================================
// setupControlPanel — 创建右侧控制面板 (QDockWidget)
// 功能: 提供控制点管理、几何模型选择、操作按钮等核心交互界面
// 布局 (从上到下):
//   1. 控制点表格 — 显示所有同名点对的 ID、源坐标、参考坐标、残差
//   2. 状态标签 — 显示当前刺点/配准状态信息
//   3. 几何模型选择 — 仿射变换(6参数) / 二次多项式(12参数)
//   4. 操作按钮区 — 清除控制点、删除选中、自动匹配
//   5. 导入控制点 + 弹性留白
//   6. 执行配准按钮 — 突出样式, 主操作入口
// ============================================================================
void jihepeizhun::setupControlPanel()
{
    m_controlPanel = new QWidget(this);
    m_controlPanel->setMinimumWidth(360);

    QVBoxLayout* layout = new QVBoxLayout(m_controlPanel);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    // 控制点表格标题
    QLabel* cpLabel = new QLabel(QString::fromUtf8("控制点列表 (同名点对):"));
    cpLabel->setStyleSheet("font-weight: bold; font-size: 13px; padding: 2px 0;");
    layout->addWidget(cpLabel);

    // ------------------------------------------------------------------
    // 控制点表格: 6 列 — ID | 源X | 源Y | 参考X | 参考Y | 残差(px)
    //   属性: 自适应列宽、交替行颜色、整行选择、只读、右键菜单
    // ------------------------------------------------------------------
    m_cpTable = new QTableWidget(0, 6);
    m_cpTable->setHorizontalHeaderLabels({
        QString::fromUtf8("ID"),
        QString::fromUtf8("源X"),
        QString::fromUtf8("源Y"),
        QString::fromUtf8("参考X"),
        QString::fromUtf8("参考Y"),
        QString::fromUtf8("残差(px)")
    });
    m_cpTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_cpTable->verticalHeader()->setDefaultSectionSize(22);
    m_cpTable->setAlternatingRowColors(true);
    m_cpTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_cpTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_cpTable->setContextMenuPolicy(Qt::CustomContextMenu);
    // 右键菜单: 删除选中的控制点
    connect(m_cpTable, &QTableWidget::customContextMenuRequested,
            this, [this](const QPoint& pos) {
        QMenu menu;
        QAction* delAct = menu.addAction(QString::fromUtf8("删除选中的控制点"));
        connect(delAct, &QAction::triggered, this, &jihepeizhun::onDeleteSelectedPoint);
        menu.exec(m_cpTable->viewport()->mapToGlobal(pos));
    });
    layout->addWidget(m_cpTable, 1);

    // 状态标签: 显示当前刺点/配准状态, htmL 格式支持彩色文字和换行
    m_statusLabel = new QLabel();
    m_statusLabel->setStyleSheet("color: #0078D4; padding: 2px 4px;");
    m_statusLabel->setWordWrap(true);
    layout->addWidget(m_statusLabel);

    // ------------------------------------------------------------------
    // 几何模型选择区
    //   仿射变换 (Affine): 6 参数, 最少 3 个控制点
    //   二次多项式 (Polynomial2nd): 12 参数, 最少 6 个控制点
    // ------------------------------------------------------------------
    QHBoxLayout* modelLayout = new QHBoxLayout();
    modelLayout->addWidget(new QLabel(QString::fromUtf8("几何模型:")));
    m_modelCombo = new QComboBox();
    m_modelCombo->addItem(QString::fromUtf8("仿射变换 (6参数, 最少3点)"),
                          static_cast<int>(GeoModelType::Affine));
    m_modelCombo->addItem(QString::fromUtf8("二次多项式 (12参数, 最少6点)"),
                          static_cast<int>(GeoModelType::Polynomial2nd));
    modelLayout->addWidget(m_modelCombo, 1);
    layout->addLayout(modelLayout);

    // ------------------------------------------------------------------
    // 操作按钮区 — 分两行排列避免拥挤
    //   第1行: 清除控制点、删除选中、自动匹配
    //   第2行: 导入控制点 + 弹性留白
    // ------------------------------------------------------------------
    auto makeBtn = [](const QString& text) {
        QPushButton* btn = new QPushButton(text);
        btn->setMinimumHeight(28);
        return btn;
    };

    QHBoxLayout* btnRow1 = new QHBoxLayout();
    m_clearBtn = makeBtn(QString::fromUtf8("清除控制点"));
    QPushButton* delSelBtn = makeBtn(QString::fromUtf8("删除选中"));
    QPushButton* autoBtn   = makeBtn(QString::fromUtf8("自动匹配"));
    btnRow1->addWidget(m_clearBtn);
    btnRow1->addWidget(delSelBtn);
    btnRow1->addWidget(autoBtn);
    layout->addLayout(btnRow1);

    connect(delSelBtn, &QPushButton::clicked, this, &jihepeizhun::onDeleteSelectedPoint);
    connect(autoBtn, &QPushButton::clicked, this, [this]() { onAutoMatch(); });

    QHBoxLayout* btnRow2 = new QHBoxLayout();
    m_importBtn = makeBtn(QString::fromUtf8("导入控制点"));
    btnRow2->addWidget(m_importBtn);
    btnRow2->addStretch();
    layout->addLayout(btnRow2);

    // ------------------------------------------------------------------
    // 执行配准按钮 (突出样式, 主操作入口)
    //   蓝色背景、白色文字、圆角矩形, 禁用时灰色不可用
    // ------------------------------------------------------------------
    m_runBtn = new QPushButton(QString::fromUtf8("▶ 执行几何配准"));
    m_runBtn->setStyleSheet(
        "QPushButton { background-color: #0078D4; color: white; font-weight: bold; "
        "padding: 10px; border-radius: 4px; font-size: 14px; }"
        "QPushButton:hover { background-color: #106EBE; }"
        "QPushButton:disabled { background-color: #CCCCCC; }");
    m_runBtn->setMinimumHeight(40);
    layout->addWidget(m_runBtn);
}

// ============================================================================
// setupConnections — 连接信号与槽
// 功能: 将界面控件触发的事件连接到对应的处理函数
// 连接清单:
//   1. 鼠标移动 → 更新状态栏坐标 (onSrcMouseMoved / onRefMouseMoved)
//   2. 几何模型切换 → 更新状态提示 (updatePickStatus)
//   3. 控制面板按钮 → 对应的控制点操作函数
//   4. 控制点表格行点击 → 跳转到该点对 (onControlPointClicked)
// ============================================================================
void jihepeizhun::setupConnections()
{
    // 鼠标移动: 在状态栏中显示当前鼠标所在影像坐标
    connect(m_srcView, &ImageView::mouseMoved,
            this, &jihepeizhun::onSrcMouseMoved);
    connect(m_refView, &ImageView::mouseMoved,
            this, &jihepeizhun::onRefMouseMoved);

    // 几何模型切换: 更新最少控制点数量提示
    connect(m_modelCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) { updatePickStatus(); });

    // 控制面板按钮
    connect(m_clearBtn, &QPushButton::clicked,
            this, &jihepeizhun::onClearControlPoints);
    connect(m_importBtn, &QPushButton::clicked,
            this, &jihepeizhun::onImportControlPoints);
    connect(m_runBtn, &QPushButton::clicked,
            this, &jihepeizhun::onRunRegistration);

    // 控制点表格行点击: 左右视图中心跳转到该控制点位置
    connect(m_cpTable, &QTableWidget::cellClicked,
            this, &jihepeizhun::onControlPointClicked);
}

// ============================================================================
// 文件操作 — 打开源影像 / 打开参考影像
// ============================================================================

// ============================================================================
// onOpenSourceImage — 打开源影像 (待配准影像)
// 功能:
//   1. 弹出文件选择对话框, 支持多种遥感/图像格式
//   2. 创建 QProgressDialog 进度条, 连接 ImageView::loadProgress 信号显示进度
//   3. 调用 ImageView::loadImage 加载影像
//   4. 成功时在状态栏显示影像路径和尺寸, 失败时弹出错误提示
// ============================================================================
void jihepeizhun::onOpenSourceImage()
{
    QString path = QFileDialog::getOpenFileName(this,
        QString::fromUtf8("打开源影像 (待配准)"),
        QString(),
        QString::fromUtf8("遥感影像 (*.tif *.tiff *.img *.jpg *.png *.bmp);;所有文件 (*.*)"));
    if (path.isEmpty()) return;

    // 创建加载进度条 (模态、无取消按钮、立即显示)
    QProgressDialog* prog = new QProgressDialog(
        QString::fromUtf8("正在加载源影像..."), QString(), 0, 100, this);
    prog->setWindowModality(Qt::WindowModal);
    prog->setCancelButton(nullptr);
    prog->setMinimumDuration(0);
    connect(m_srcView, &ImageView::loadProgress, prog, &QProgressDialog::setValue);

    bool ok = m_srcView->loadImage(path);
    prog->close();
    delete prog;

    if (ok) {
        m_statusBar->showMessage(
            QString::fromUtf8("源影像已加载: %1 (%2×%3)")
                .arg(path).arg(m_srcView->imageWidth()).arg(m_srcView->imageHeight()), 5000);

        // ---- 地理参考信息深度检测 ----
        auto geoStat = m_srcView->checkGeoReference();
        if (!geoStat.valid) {
            QStringList warnings;
            for (const auto& issue : geoStat.issues)
                warnings << issue;
            QMessageBox::warning(this,
                QString::fromUtf8("地理参考检测 - 源影像"),
                QString::fromUtf8(
                    "【警告】源影像缺少有效的地理参考信息!\n\n"
                    "%1\n\n"
                    "无地理参考的后果:\n"
                    "  · 自动匹配无法计算重叠区域\n"
                    "  · 控制点坐标无地理意义\n"
                    "  · 正射校正功能无法使用\n\n"
                    "建议先对源影像进行正射校正。")
                    .arg(warnings.join("\n")));
        } else if (!geoStat.issues.isEmpty()) {
            // 有 GeoTransform 但有轻微问题，仅状态栏提示
            m_statusBar->showMessage(
                QString::fromUtf8("源影像: ⚠ %1").arg(geoStat.issues.first()), 8000);
        } else {
            // 一切正常
            QString crsInfo = geoStat.crsDescription.isEmpty()
                ? QString::fromUtf8("无CRS定义")
                : geoStat.crsDescription;
            m_statusBar->showMessage(
                QString::fromUtf8("源影像已加载: %1×%2 | 分辨率: %3×%4 | CRS: %5")
                    .arg(m_srcView->imageWidth()).arg(m_srcView->imageHeight())
                    .arg(geoStat.pixelWidth, 0, 'f', 3)
                    .arg(std::abs(geoStat.pixelHeight), 0, 'f', 3)
                    .arg(crsInfo), 10000);
        }

        // 加载新影像后清除之前的重叠区标记和特征点
        m_srcView->clearOverlayRect();
        m_srcView->clearFeaturePoints();
        m_refView->clearOverlayRect();
        m_refView->clearFeaturePoints();
        m_overlapResult = OverlapResult();
    } else {
        QMessageBox::warning(this,
            QString::fromUtf8("错误"),
            QString::fromUtf8("无法打开源影像文件: %1\n\nGDAL错误: %2")
                .arg(path, m_srcView->lastError()));
    }
}

// ============================================================================
// onOpenRefImage — 打开参考影像
// 功能: 与 onOpenSourceImage 结构相同, 但影像加载到右侧参考视图 (m_refView)
// ============================================================================
void jihepeizhun::onOpenRefImage()
{
    QString path = QFileDialog::getOpenFileName(this,
        QString::fromUtf8("打开参考影像"),
        QString(),
        QString::fromUtf8("遥感影像 (*.tif *.tiff *.img *.jpg *.png *.bmp);;所有文件 (*.*)"));
    if (path.isEmpty()) return;

    // 创建加载进度条 (模态、无取消按钮、立即显示)
    QProgressDialog* prog = new QProgressDialog(
        QString::fromUtf8("正在加载参考影像..."), QString(), 0, 100, this);
    prog->setWindowModality(Qt::WindowModal);
    prog->setCancelButton(nullptr);
    prog->setMinimumDuration(0);
    connect(m_refView, &ImageView::loadProgress, prog, &QProgressDialog::setValue);

    bool ok = m_refView->loadImage(path);
    prog->close();
    delete prog;

    if (ok) {
        m_statusBar->showMessage(
            QString::fromUtf8("参考影像已加载: %1 (%2×%3)")
                .arg(path).arg(m_refView->imageWidth()).arg(m_refView->imageHeight()), 5000);

        // ---- 地理参考信息深度检测 ----
        auto geoStat = m_refView->checkGeoReference();
        if (!geoStat.valid) {
            QStringList warnings;
            for (const auto& issue : geoStat.issues)
                warnings << issue;
            QMessageBox::warning(this,
                QString::fromUtf8("地理参考检测 - 参考影像"),
                QString::fromUtf8(
                    "【警告】参考影像缺少有效的地理参考信息!\n\n"
                    "%1\n\n"
                    "无地理参考的后果:\n"
                    "  · 自动匹配无法计算重叠区域\n"
                    "  · 控制点坐标无地理意义\n\n"
                    "建议使用正射校正后的影像(DOM)作为参考。")
                    .arg(warnings.join("\n")));
        } else if (!geoStat.issues.isEmpty()) {
            m_statusBar->showMessage(
                QString::fromUtf8("参考影像: ⚠ %1").arg(geoStat.issues.first()), 8000);
        } else {
            QString crsInfo = geoStat.crsDescription.isEmpty()
                ? QString::fromUtf8("无CRS定义")
                : geoStat.crsDescription;
            m_statusBar->showMessage(
                QString::fromUtf8("参考影像已加载: %1×%2 | 分辨率: %3×%4 | CRS: %5")
                    .arg(m_refView->imageWidth()).arg(m_refView->imageHeight())
                    .arg(geoStat.pixelWidth, 0, 'f', 3)
                    .arg(std::abs(geoStat.pixelHeight), 0, 'f', 3)
                    .arg(crsInfo), 10000);
        }

        // 加载新影像后清除之前的重叠区标记和特征点
        m_srcView->clearOverlayRect();
        m_srcView->clearFeaturePoints();
        m_refView->clearOverlayRect();
        m_refView->clearFeaturePoints();
        m_overlapResult = OverlapResult();
    } else {
        QMessageBox::warning(this,
            QString::fromUtf8("错误"),
            QString::fromUtf8("无法打开参考影像文件: %1").arg(path));
    }
}

// ============================================================================
// onSaveRegisteredImage — 保存配准结果 (核心功能)
// 功能: 将源影像按照已解算的几何模型重采样到参考影像坐标系, 输出 GeoTIFF
//
// 算法流程:
//   步骤1 — 计算输出范围 (bounding box):
//     ① 将参考影像四角通过逆变换映射到源坐标系, 得到"参考在源上的锚点"
//     ② 将源影像四角 + 参考锚点共 8 个点做前向变换到参考坐标系
//     ③ 取变换结果的外接矩形, 并确保包含参考影像四角
//     ④ 外扩 2 像素避免边界裁剪
//     — 这样做的原因: 多项式模型的逆变换(牛顿迭代)并非精确逆,
//       只有将经过逆变换的点再做前向变换, 才能保证 bounding box
//       与后续重采样使用的逆变换路径一致, 避免边界漏配准
//
//   步骤2 — 创建输出 GeoTIFF:
//     ① 使用 GDAL GTiff 驱动创建文件, 设置 LZW 压缩、TILED 等优化选项
//     ② 基于参考影像的 GeoTransform, 推导输出影像(0,0)对应的地理坐标
//     ③ 写入投影信息
//
//   步骤3 — 分块逆向重采样 (双线性插值):
//     ① 以 512×512 块为单位遍历输出影像
//     ② 对块内每个像素, 用逆变换求其在源影像中的坐标
//     ③ 收集块内所有源坐标的范围, 裁剪出包含这些范围的最小源影像子块
//     ④ 对每个像素执行双线性插值: 取最近 4 邻域像素, 按距离权重加权
//     ⑤ 写回 GDAL 数据集
//
//   步骤4 — 清理与状态反馈
// ============================================================================
void jihepeizhun::onSaveRegisteredImage()
{
    if (!m_isRegistered) {
        QMessageBox::information(this,
            QString::fromUtf8("提示"),
            QString::fromUtf8("请先执行几何配准。"));
        return;
    }

    QString path = QFileDialog::getSaveFileName(this,
        QString::fromUtf8("保存配准结果"),
        QString(),
        QString::fromUtf8("GeoTIFF (*.tif *.tiff);;所有文件 (*.*)"));
    if (path.isEmpty()) return;

    if (!m_srcView->isLoaded() || !m_refView->isLoaded()) return;

    const QImage& srcImage = m_srcView->getImage();
    int srcW = srcImage.width();
    int srcH = srcImage.height();
    int srcBands = m_srcView->bandCount();
    const uchar* srcBits = srcImage.constBits();
    int srcBpl = srcImage.bytesPerLine();

    if (srcImage.isNull() || srcW <= 0 || srcH <= 0) {
        QMessageBox::warning(this, QString::fromUtf8("错误"),
            QString::fromUtf8("源影像数据无效, 请重新加载影像。"));
        return;
    }

    // ============================================================
    // 步骤1: 通过前向变换计算输出影像范围 (bounding box)
    //
    //   关键点: 先找出参考影像四角在源影像上的对应位置 (逆变换),
    //   将这些位置与源影像四角进行相应的位置关系计算, 统一做前向变换求外接矩形.
    //
    //   这是因为多项式模型的逆变换 (牛顿迭代) 并非精确逆,
    //   只有把经过逆变换的点再做前向变换, 才能保证 bounding box
    //   与后续重采样使用的逆变换路径一致, 避免边界区域漏配准.
    // ============================================================

    int refW = m_refView->imageWidth();
    int refH = m_refView->imageHeight();

    // 参考影像四角 — 逆变换到源影像坐标, 作为"参考在源上的锚点"
    QPointF refOnSrc[4] = {
        m_geoModel.inverseTransform(0, 0),
        m_geoModel.inverseTransform(refW - 1, 0),
        m_geoModel.inverseTransform(refW - 1, refH - 1),
        m_geoModel.inverseTransform(0, refH - 1)
    };

    // 合并: 源影像四角 + 参考影像在源上的锚点, 统一前向变换到参考坐标系
    QPointF allSrcPts[8] = {
        QPointF(0, 0), QPointF(srcW - 1, 0),
        QPointF(srcW - 1, srcH - 1), QPointF(0, srcH - 1),
        refOnSrc[0], refOnSrc[1], refOnSrc[2], refOnSrc[3]
    };

    double minRX =  1e30, maxRX = -1e30;
    double minRY =  1e30, maxRY = -1e30;
    for (int i = 0; i < 8; ++i) {
        QPointF rc = m_geoModel.forwardTransform(allSrcPts[i].x(), allSrcPts[i].y());
        minRX = qMin(minRX, rc.x()); maxRX = qMax(maxRX, rc.x());
        minRY = qMin(minRY, rc.y()); maxRY = qMax(maxRY, rc.y());
    }
    // 同时包含参考影像四角 (参考坐标系原点, 确保参考区域不丢失)
    minRX = qMin(minRX, 0.0); maxRX = qMax(maxRX, static_cast<double>(refW - 1));
    minRY = qMin(minRY, 0.0); maxRY = qMax(maxRY, static_cast<double>(refH - 1));

    // 外扩2像素避免边界裁剪
    minRX -= 2.0; maxRX += 2.0;
    minRY -= 2.0; maxRY += 2.0;

    int outW = static_cast<int>(std::ceil(maxRX - minRX));
    int outH = static_cast<int>(std::ceil(maxRY - minRY));

    // 安全上限: 防止异常控制点导致超大输出
    const int MAX_DIM = 50000;
    if (outW <= 0 || outH <= 0 || outW > MAX_DIM || outH > MAX_DIM) {
        QMessageBox::warning(this, QString::fromUtf8("错误"),
            QString::fromUtf8("计算出的输出尺寸异常 (%1×%2)。\n请检查控制点是否正确, 或配准是否有误。")
                .arg(outW).arg(outH));
        return;
    }

    // ============================================================
    // 步骤2: 创建输出 GeoTIFF, 写入地理参考
    //
    //   使用 GTiff 驱动创建文件:
    //     - COMPRESS=LZW   : 无损压缩, 减小文件体积
    //     - PREDICTOR=2    : 针对连续色调影像优化压缩比
    //     - BIGTIFF=IF_SAFER : 当文件超过 4GB 时自动转为 BigTIFF 格式
    //     - TILED=YES      : 使用分块存储 (而非条带), 提升随机访问性能
    //     - BLOCKXSIZE/YSIZE=256 : 块大小 256×256
    //
    //   GeoTransform 推导:
    //     输出影像 (0,0) 在参考坐标系中的位置为 (minRX, minRY)。
    //     其在参考影像的地理坐标由下式给出:
    //       geoX = refGT[0] + minRX * refGT[1] + minRY * refGT[2]
    //       geoY = refGT[3] + minRX * refGT[4] + minRY * refGT[5]
    //     像素分辨率 (refGT[1], refGT[5]) 保持不变。
    // ============================================================
    CPLSetConfigOption("GDAL_FILENAME_IS_UTF8", "YES");
    GDALDriver* driver = GetGDALDriverManager()->GetDriverByName("GTiff");
    if (!driver) {
        QMessageBox::warning(this, QString::fromUtf8("错误"),
            QString::fromUtf8("无法获取 GeoTIFF 驱动, GDAL 安装可能不完整。"));
        return;
    }

    QByteArray pathUtf8 = path.toUtf8();
    char** options = nullptr;
    options = CSLSetNameValue(options, "COMPRESS", "LZW");
    options = CSLSetNameValue(options, "PREDICTOR", "2");
    options = CSLSetNameValue(options, "BIGTIFF", "IF_SAFER");
    options = CSLSetNameValue(options, "TILED", "YES");
    options = CSLSetNameValue(options, "BLOCKXSIZE", "256");
    options = CSLSetNameValue(options, "BLOCKYSIZE", "256");
    GDALDataset* outDS = driver->Create(pathUtf8.constData(),
        outW, outH, srcBands, GDT_Byte, options);
    CSLDestroy(options);
    if (!outDS) {
        QString err = QString::fromUtf8(CPLGetLastErrorMsg());
        QMessageBox::warning(this, QString::fromUtf8("错误"),
            QString::fromUtf8("无法创建输出文件:\n%1\n\nGDAL错误: %2")
                .arg(path, err));
        return;
    }

    // 写入地理参考信息 (基于参考影像的 GeoTransform 推导)
    if (m_refView->hasGeoTransform()) {
        const double* refGT = m_refView->geoTransform();

        // 输出影像的像素(0,0) 对应参考坐标系 (minRX, minRY)
        // 该点在参考影像中的位置对应地理坐标:
        //   geoX = refGT[0] + minRX * refGT[1] + minRY * refGT[2]
        //   geoY = refGT[3] + minRX * refGT[4] + minRY * refGT[5]
        double outGT[6];
        outGT[0] = refGT[0] + minRX * refGT[1] + minRY * refGT[2];
        outGT[1] = refGT[1];
        outGT[2] = refGT[2];
        outGT[3] = refGT[3] + minRX * refGT[4] + minRY * refGT[5];
        outGT[4] = refGT[4];
        outGT[5] = refGT[5];

        outDS->SetGeoTransform(outGT);

        QByteArray projWkt = m_refView->projectionWkt().toUtf8();
        if (!projWkt.isEmpty()) {
            outDS->SetProjection(projWkt.constData());
        }

        // 为所有波段设置 NoData=0，表示无数据区域的像素值为 0
        // 这样在 GIS 软件中打开时，背景区域可被识别为透明/空值
        for (int b = 0; b < srcBands; ++b)
            outDS->GetRasterBand(b + 1)->SetNoDataValue(0);

        qDebug() << "Output GeoTransform:" << outGT[0] << outGT[1]
                 << outGT[2] << outGT[3] << outGT[4] << outGT[5];
    }

    // ============================================================
    // 步骤3: 分块逆向重采样 (512×512 tiles, 双线性插值)
    //
    //   分块策略: 以 512×512 块为单位遍历输出影像, 避免一次性处理
    //   超大影像导致内存溢出。
    //
    //   逆向重采样流程:
    //     ① 对块内每个像素 (rx, ry), 调用逆变换求源坐标 (sx, sy)
    //     ② 统计块内所有源坐标的范围, 确定需要读取的源区域
    //     ③ 对块内每个像素, 执行双线性插值:
    //          - 取最近 4 邻域像素 p00(左上), p10(右上), p01(左下), p11(右下)
    //          - 计算权重: wx = sx - floor(sx), wy = sy - floor(sy)
    //          - 插值公式:
    //            w00 = (1-wx)*(1-wy)  → p00 权重
    //            w10 = wx*(1-wy)      → p10 权重
    //            w01 = (1-wx)*wy      → p01 权重
    //            w11 = wx*wy          → p11 权重
    //          - 最终值 = sum(权重 × 像素值), 限幅到 [0, 255]
    //     ④ 使用 GDAL RasterIO 将块数据写回输出文件
    //
    //   使用 QProgressDialog 显示导出进度。
    // ============================================================
    const int TILE = 512;
    QVector<double> tileSX(TILE * TILE);
    QVector<double> tileSY(TILE * TILE);
    QVector<quint8> dstBuf(TILE * TILE * srcBands, 0);

    QProgressDialog* expProg = new QProgressDialog(
        QString::fromUtf8("正在导出配准结果..."), QString(), 0, 100, this);
    expProg->setWindowModality(Qt::WindowModal);
    expProg->setCancelButton(nullptr);
    expProg->setMinimumDuration(0);

    for (int ty = 0; ty < outH; ty += TILE) {
        expProg->setValue(ty * 100 / outH);
        QApplication::processEvents();
        for (int tx = 0; tx < outW; tx += TILE) {
            int tw = qMin(TILE, outW - tx);
            int th = qMin(TILE, outH - ty);
            std::fill(dstBuf.begin(), dstBuf.begin() + tw * th * srcBands, 0);

            double minSX = srcW, maxSX = -1.0;
            double minSY = srcH, maxSY = -1.0;

            // 子步骤 3a: 批量计算逆变换, 同时统计块内所有源坐标的范围
            for (int y = 0; y < th; ++y) {
                double ry = minRY + static_cast<double>(ty + y);
                for (int x = 0; x < tw; ++x) {
                    double rx = minRX + static_cast<double>(tx + x);
                    QPointF srcPt = m_geoModel.inverseTransform(rx, ry);
                    int idx = y * tw + x;
                    tileSX[idx] = srcPt.x();
                    tileSY[idx] = srcPt.y();
                    minSX = qMin(minSX, tileSX[idx]);
                    maxSX = qMax(maxSX, tileSX[idx]);
                    minSY = qMin(minSY, tileSY[idx]);
                    maxSY = qMax(maxSY, tileSY[idx]);
                }
            }

            // 子步骤 3b: 计算需要读取的源影像子块范围 (外扩 2 像素以保证插值边界)
            int ix0 = qMax(0, static_cast<int>(std::floor(minSX)));
            int iy0 = qMax(0, static_cast<int>(std::floor(minSY)));
            int iw = qMin(srcW - ix0, static_cast<int>(std::ceil(maxSX)) - ix0 + 2);
            int ih = qMin(srcH - iy0, static_cast<int>(std::ceil(maxSY)) - iy0 + 2);
            if (iw < 2 || ih < 2) continue;

            // 子步骤 3c: 对块内每个像素执行双线性插值
            for (int y = 0; y < th; ++y) {
                for (int x = 0; x < tw; ++x) {
                    int idx = y * tw + x;
                    double sx = tileSX[idx] - ix0;
                    double sy = tileSY[idx] - iy0;
                    if (sx < 0 || sx > iw - 2 || sy < 0 || sy > ih - 2) continue;

                    // 计算最近 4 邻域的位置 (ix, iy) 和距离权重 (wx, wy)
                    int ix = static_cast<int>(sx);
                    int iy = static_cast<int>(sy);
                    double wx = sx - ix, wy = sy - iy;
                    double w00 = (1.0 - wx) * (1.0 - wy);   // 左上权重
                    double w10 = wx * (1.0 - wy);           // 右上权重
                    double w01 = (1.0 - wx) * wy;           // 左下权重
                    double w11 = wx * wy;                   // 右下权重

                    // 读取 4 邻域像素值
                    int six = ix0 + ix, siy = iy0 + iy;
                    const QRgb* line0 = reinterpret_cast<const QRgb*>(srcBits + siy * srcBpl);
                    const QRgb* line1 = reinterpret_cast<const QRgb*>(srcBits + (siy + 1) * srcBpl);
                    QRgb p00 = line0[six];                  // 左上
                    QRgb p10 = line0[six + 1];              // 右上
                    QRgb p01 = line1[six];                  // 左下
                    QRgb p11 = line1[six + 1];              // 右下

                    // 对各波段执行加权求和, 结果限幅到 [0, 255]
                    int dstIdx = idx * srcBands;
                    if (srcBands >= 3) {
                        dstBuf[dstIdx]     = static_cast<quint8>(qBound(0.0, w00 * qRed(p00)   + w10 * qRed(p10)   + w01 * qRed(p01)   + w11 * qRed(p11),   255.0));
                        dstBuf[dstIdx + 1] = static_cast<quint8>(qBound(0.0, w00 * qGreen(p00) + w10 * qGreen(p10) + w01 * qGreen(p01) + w11 * qGreen(p11), 255.0));
                        dstBuf[dstIdx + 2] = static_cast<quint8>(qBound(0.0, w00 * qBlue(p00)  + w10 * qBlue(p10)  + w01 * qBlue(p01)  + w11 * qBlue(p11),  255.0));
                    } else {
                        dstBuf[dstIdx] = static_cast<quint8>(qBound(0.0, w00 * qRed(p00) + w10 * qRed(p10) + w01 * qRed(p01) + w11 * qRed(p11), 255.0));
                    }
                }
            }

            // 子步骤 3d: 将块数据写入 GDAL 数据集 (RasterIO)
            outDS->RasterIO(GF_Write, tx, ty, tw, th,
                dstBuf.data(), tw, th, GDT_Byte, srcBands, nullptr, 0, 0, 0);
        }
    }

    expProg->setValue(100);
    expProg->close();
    delete expProg;

    GDALClose(outDS);
    m_resultPaths.append(path);
    emit resultReady("georeg", m_resultPaths);
    m_statusBar->showMessage(
        QString::fromUtf8("配准结果已保存: %1 (%2×%3)  RMSE=%4px")
            .arg(path).arg(outW).arg(outH)
            .arg(m_lastRMSE, 0, 'f', 4), 10000);
}

// ============================================================================
// onOrthoCorrection — 正射校正
// 功能: 弹出 OrthoDialog 对话框, 对单影像执行多项式正射校正
//       如果源影像已加载且有地理参考, 自动传入影像路径
// ============================================================================
void jihepeizhun::onOrthoCorrection()
{
    OrthoDialog dlg(this);
    if (m_srcView->isLoaded() && m_srcView->hasGeoTransform())
        dlg.setCurrentSourcePath(m_srcView->imagePath());
    dlg.exec();
}

// ============================================================================
// onExportControlPoints — 导出控制点到文本文件
// 功能: 将当前所有控制点以 Tab 分隔的格式写入 .txt 或 .csv 文件
//       格式: ID  srcX  srcY  refX  refY  (每行一个控制点)
// ============================================================================
void jihepeizhun::onExportControlPoints()
{
    if (m_controlPoints.isEmpty()) {
        QMessageBox::information(this,
            QString::fromUtf8("提示"),
            QString::fromUtf8("没有控制点可供导出。"));
        return;
    }

    QString path = QFileDialog::getSaveFileName(this,
        QString::fromUtf8("导出控制点"),
        QString(),
        QString::fromUtf8("文本文件 (*.txt);;CSV文件 (*.csv)"));
    if (path.isEmpty()) return;

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, QString::fromUtf8("错误"),
            QString::fromUtf8("无法写入文件: %1\n可能原因: 磁盘已满或缺少写入权限。").arg(path));
        return;
    }

    QTextStream stream(&file);
    // 写表头行
    stream << QString::fromUtf8("ID\tsrcX\tsrcY\trefX\trefY\n");
    for (const auto& cp : m_controlPoints) {
        stream << cp.id << "\t"
               << QString::number(cp.srcX, 'f', 4) << "\t"
               << QString::number(cp.srcY, 'f', 4) << "\t"
               << QString::number(cp.refX, 'f', 4) << "\t"
               << QString::number(cp.refY, 'f', 4) << "\n";
    }

    file.close();
    m_statusBar->showMessage(
        QString::fromUtf8("控制点已导出: %1").arg(path), 3000);
}

// ============================================================================
// onImportControlPoints — 从外部文件导入控制点
// 功能: 从 .txt / .csv / .pts 文件中解析控制点并替换当前列表
//
// 支持的文件格式:
//
//   [1] 本程序导出格式 (.txt / .csv) — 5 列:
//       ID  srcX  srcY  refX  refY
//       1   100.5 200.3 150.0 250.0
//
//   [2] 兼容格式 (.txt / .csv) — 4 列 (无 ID):
//       srcX  srcY  refX  refY
//       100.5 200.3 150.0 250.0
//
//   [3] ENVI Tie Points 格式 (.pts) — 5 列, ; 开头为注释:
//       ; ENVI Image to Image Tie Points File
//       base_x  base_y  warp_x  warp_y  score
//       5143.0  7566.0  17085.1  5405.3  0.149
//       → srcPt = (warpX, warpY), refPt = (baseX, baseY)
//
// 解析规则:
//   - 空行 / # 注释 / 表头行 直接跳过
//   - .pts 文件: ; 注释跳过, 5列格式 (baseX baseY warpX warpY score)
//   - .txt/.csv: Tab/逗号/空格/分号 分隔, 4列或5列自动识别
// ============================================================================
void jihepeizhun::onImportControlPoints()
{
    QString path = QFileDialog::getOpenFileName(this,
        QString::fromUtf8("导入控制点"),
        QString(),
        QString::fromUtf8("控制点文件 (*.txt *.csv *.pts);;所有文件 (*.*)"));
    if (path.isEmpty()) return;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::warning(this, QString::fromUtf8("错误"),
            QString::fromUtf8("无法打开文件: %1").arg(path));
        return;
    }

    // 通过扩展名判断是否为 ENVI .pts 格式
    bool isPtsFormat = path.endsWith(".pts", Qt::CaseInsensitive);

    QVector<ControlPoint> imported;
    int lineNum = 0;
    QTextStream stream(&file);

    while (!stream.atEnd()) {
        QString line = stream.readLine().trimmed();
        lineNum++;

        // ---- 跳过无效行 ----
        // .txt/.csv: 跳过空行、#注释、ID表头
        // .pts: 跳过空行、;注释 (ENVI 标准注释符)
        if (line.isEmpty()) continue;
        if (isPtsFormat) {
            if (line.startsWith(';')) continue;
        } else {
            if (line.startsWith('#') || line.startsWith("ID")) continue;
        }

        QStringList parts = line.split(QRegularExpression("[\\t,; ]+"), Qt::SkipEmptyParts);

        if (isPtsFormat) {
            // ---- .pts 格式: 6 列 ----
            // 列: baseX  baseY  warpX  warpY  score
            //     (ref)   (ref) (src)  (src) (ignore)
            if (parts.size() < 5) continue;

            bool ok = true;
            double baseX = parts[0].toDouble(&ok); if (!ok) continue;
            double baseY = parts[1].toDouble(&ok); if (!ok) continue;
            double warpX = parts[2].toDouble(&ok); if (!ok) continue;
            double warpY = parts[3].toDouble(&ok); if (!ok) continue;
            // parts[4] = score, 忽略

            int id = imported.size() + 1;
            // ENVI 格式: base = 参考影像, warp = 源影像
            imported.append(ControlPoint(id, warpX, warpY, baseX, baseY));
        } else {
            // ---- .txt/.csv 格式: 4 列或 5 列 ----
            // 5列: ID srcX srcY refX refY (本程序导出)
            // 4列: srcX srcY refX refY (外部兼容)
            if (parts.size() < 4) continue;

            int offset = (parts.size() >= 5) ? 1 : 0;

            bool ok = true;
            double sx = parts[offset + 0].toDouble(&ok); if (!ok) continue;
            double sy = parts[offset + 1].toDouble(&ok); if (!ok) continue;
            double rx = parts[offset + 2].toDouble(&ok); if (!ok) continue;
            double ry = parts[offset + 3].toDouble(&ok); if (!ok) continue;

            int id = imported.size() + 1;
            imported.append(ControlPoint(id, sx, sy, rx, ry));
        }
    }

    file.close();

    if (imported.isEmpty()) {
        QString hint;
        if (isPtsFormat)
            hint = QString::fromUtf8(
                "ENVI .pts 格式说明 (Tab分隔):\n"
                "  baseX  baseY  warpX  warpY  score\n"
                "  5143.0  7566.0  17085.1  5405.3  0.149\n"
                "  以 ; 开头的行为注释行");
        else
            hint = QString::fromUtf8(
                "支持格式 (Tab/逗号/空格分隔):\n"
                "  ID  srcX  srcY  refX  refY\n"
                "  1  100.5 200.3 150.0 250.0\n"
                "  或(无ID列): srcX srcY refX refY");
        QMessageBox::information(this, QString::fromUtf8("提示"),
            QString::fromUtf8("未从文件中解析到任何有效控制点。\n\n%1").arg(hint));
        return;
    }

    // 清空现有控制点并替换为导入的数据
    m_controlPoints = imported;
    m_isRegistered = false;

    // 刷新视图中的控制点标记
    m_srcView->clearControlPoints();
    m_refView->clearControlPoints();
    for (const auto& cp : m_controlPoints) {
        m_srcView->addControlPoint(QPointF(cp.srcX, cp.srcY));
        m_refView->addControlPoint(QPointF(cp.refX, cp.refY));
    }

    updateControlPointTable();
    updatePickStatus();

    m_statusBar->showMessage(
        QString::fromUtf8("已导入 %1 个控制点: %2")
            .arg(m_controlPoints.size()).arg(path), 5000);
}

// ============================================================================
// 刺点操作 — 控制点拾取流程
// ============================================================================

// ============================================================================
// onTogglePickMode — 切换刺点模式
// 功能: 在"浏览模式"和"刺点模式"之间切换
// 流程:
//   1. 检查源影像和参考影像是否已加载, 未加载则拒绝切换
//   2. 如果开启刺点模式: 将已有控制点同步到两视图, 调用 PickManager::startPicking
//   3. 如果关闭刺点模式: 调用 PickManager::stopPicking
//   4. 更新状态栏提示 (updatePickStatus)
// ============================================================================
void jihepeizhun::onTogglePickMode()
{
    if (!m_srcView->isLoaded() || !m_refView->isLoaded()) {
        m_pickAction->setChecked(false);
        QMessageBox::warning(this,
            QString::fromUtf8("提示"),
            QString::fromUtf8("请先加载源影像和参考影像。"));
        return;
    }

    bool start = m_pickAction->isChecked();
    if (start) {
        // 将已有控制点标记同步到两个视图, 确保刺点模式下显示完整
        m_srcView->clearControlPoints();
        m_refView->clearControlPoints();
        for (const auto& cp : m_controlPoints) {
            m_srcView->addControlPoint(QPointF(cp.srcX, cp.srcY));
            m_refView->addControlPoint(QPointF(cp.refX, cp.refY));
        }
        m_pickManager->startPicking();
    } else {
        m_pickManager->stopPicking();
    }
    updatePickStatus();
}

// ============================================================================
// onPickPairReady — 接收 PickManager 刺点完成的信号
// 功能: PickManager 在用户完成一次"点击源影像 → 点击参考影像"后发射此信号,
//       将新拾取的同名点对加入控制点列表, 刷新视图和表格
// 参数: cp — 包含源坐标 (srcX, srcY) 和参考坐标 (refX, refY) 的控制点
// ============================================================================
void jihepeizhun::onPickPairReady(ControlPoint cp)
{
    // 自动分配 ID (按现有数量 +1)
    cp.id = m_controlPoints.size() + 1;
    m_controlPoints.append(cp);
    // 在参考视图上添加标记点 (源视图标记已在 PickManager 中添加)
    m_refView->addControlPoint(QPointF(cp.refX, cp.refY));
    // 新加点后配准状态失效, 需重新执行配准
    m_isRegistered = false;

    updateControlPointTable();
    updatePickStatus();

    m_statusBar->showMessage(
        QString::fromUtf8("控制点 #%1 已添加: (%2, %3) → (%4, %5)")
            .arg(cp.id)
            .arg(cp.srcX, 0, 'f', 3).arg(cp.srcY, 0, 'f', 3)
            .arg(cp.refX, 0, 'f', 3).arg(cp.refY, 0, 'f', 3));
}

// ============================================================================
// 控制点管理 — 清除 / 删除上一个 / 删除选中
// ============================================================================

// ============================================================================
// onClearControlPoints — 清除所有控制点
// 功能: 停止刺点模式, 清空控制点列表, 清除视图标记, 刷新表格和状态
// ============================================================================
void jihepeizhun::onClearControlPoints()
{
    if (m_pickManager->isPicking()) m_pickManager->stopPicking();

    m_controlPoints.clear();
    m_isRegistered = false;

    m_srcView->clearControlPoints();
    m_refView->clearControlPoints();

    updateControlPointTable();
    updatePickStatus();

    m_statusBar->showMessage(QString::fromUtf8("所有控制点已清除"), 3000);
}

// ============================================================================
// onDeleteLastPoint — 删除最后一个控制点
// 功能: 弹出列表中最后一个控制点, 刷新视图标记和表格
//       快捷操作, 对应菜单 "删除上一个控制点 (Ctrl+Z)"
// ============================================================================
void jihepeizhun::onDeleteLastPoint()
{
    if (m_controlPoints.isEmpty()) return;

    m_controlPoints.removeLast();
    m_isRegistered = false;

    // 刷新视图中的控制点标记
    m_srcView->clearControlPoints();
    m_refView->clearControlPoints();
    for (const auto& cp : m_controlPoints) {
        m_srcView->addControlPoint(QPointF(cp.srcX, cp.srcY));
        m_refView->addControlPoint(QPointF(cp.refX, cp.refY));
    }

    updateControlPointTable();
    updatePickStatus();

    m_statusBar->showMessage(
        QString::fromUtf8("已删除最后一个控制点, 剩余 %1 个")
            .arg(m_controlPoints.size()), 3000);
}

// ============================================================================
// onDeleteSelectedPoint — 删除在表格中选中的控制点
// 功能:
//   1. 获取表格中所有选中行的行号
//   2. 按行号从大到小排序后依次删除 (避免删除时行号偏移)
//   3. 重新编号剩余控制点的 ID (保持连续)
//   4. 刷新视图标记、表格和状态
// ============================================================================
void jihepeizhun::onDeleteSelectedPoint()
{
    QList<QTableWidgetItem*> selected = m_cpTable->selectedItems();
    if (selected.isEmpty()) {
        m_statusBar->showMessage(QString::fromUtf8("请先在表格中选中要删除的控制点行"), 3000);
        return;
    }

    QSet<int> rowsToDelete;
    for (QTableWidgetItem* item : selected) {
        rowsToDelete.insert(item->row());
    }

    QList<int> sortedRows = rowsToDelete.values();
    std::sort(sortedRows.begin(), sortedRows.end(), std::greater<int>());

    for (int row : sortedRows) {
        if (row >= 0 && row < m_controlPoints.size()) {
            m_controlPoints.removeAt(row);
        }
    }

    // 重新编号控制点 ID
    for (int i = 0; i < m_controlPoints.size(); ++i) {
        m_controlPoints[i].id = i + 1;
    }

    m_isRegistered = false;

    // 刷新视图中的控制点标记
    m_srcView->clearControlPoints();
    m_refView->clearControlPoints();
    for (const auto& cp : m_controlPoints) {
        m_srcView->addControlPoint(QPointF(cp.srcX, cp.srcY));
        m_refView->addControlPoint(QPointF(cp.refX, cp.refY));
    }

    updateControlPointTable();
    updatePickStatus();

    m_statusBar->showMessage(
        QString::fromUtf8("已删除 %1 个控制点, 剩余 %2 个")
            .arg(sortedRows.size()).arg(m_controlPoints.size()), 3000);
}

// ============================================================================
// onControlPointClicked — 控制点表格点击跳转
// 功能: 用户在表格中点击某行时, 将左右视图的中心分别定位到该控制点的
//       源坐标和参考坐标位置, 方便用户快速检查同名点对的对应关系
// ============================================================================
void jihepeizhun::onControlPointClicked(int row, int)
{
    if (row < 0 || row >= m_controlPoints.size()) return;
    const ControlPoint& cp = m_controlPoints[row];
    m_srcView->centerOn(QPointF(cp.srcX, cp.srcY));
    m_refView->centerOn(QPointF(cp.refX, cp.refY));
    m_statusBar->showMessage(
        QString::fromUtf8("已跳转到控制点 #%1: (%2,%3) → (%4,%5)")
            .arg(cp.id)
            .arg(cp.srcX, 0, 'f', 3).arg(cp.srcY, 0, 'f', 3)
            .arg(cp.refX, 0, 'f', 3).arg(cp.refY, 0, 'f', 3), 3000);
}

// ============================================================================
// onAutoMatch — 自动匹配同名控制点
// 功能: 使用特征匹配算法自动识别源影像和参考影像之间的同名点对
// 流程:
//   1. 创建 AutoMatch 实例
//   2. 传入两幅影像的 QImage 和地理变换参数 (如有)
//   3. 调用 matcher.match() 执行特征提取与匹配
//   4. 匹配成功: 替换当前控制点列表, 刷新视图标记、表格和状态
//   5. 匹配失败: 弹出提示信息, 建议手动刺点补充
// ============================================================================
void jihepeizhun::onAutoMatch()
{
    if (!m_srcView->isLoaded() || !m_refView->isLoaded()) {
        QMessageBox::warning(this, QString::fromUtf8("提示"),
            QString::fromUtf8("请先加载源影像和参考影像。"));
        return;
    }

    m_statusBar->showMessage(QString::fromUtf8("正在进行自动特征匹配..."), 0);
    QApplication::processEvents();

    AutoMatch matcher;
    const double* srcGT = m_srcView->hasGeoTransform() ? m_srcView->geoTransform() : nullptr;
    const double* refGT = m_refView->hasGeoTransform() ? m_refView->geoTransform() : nullptr;
    QVector<ControlPoint> autoPoints = matcher.match(
        m_srcView->getImage(), m_refView->getImage(), srcGT, refGT);

    if (autoPoints.isEmpty()) {
        m_statusBar->showMessage(QString::fromUtf8("自动匹配失败"), 5000);
        QMessageBox::information(this,
            QString::fromUtf8("自动匹配结果"),
            QString::fromUtf8("未能识别到可靠的匹配点对。\n\n%1\n\n建议:\n"
                "  · 确保两幅影像有足够的重叠区域\n"
                "  · 影像内容纹理丰富时效果更好\n"
                "  · 可尝试手动刺点作为补充")
                .arg(matcher.lastError()));
        return;
    }

    m_controlPoints = autoPoints;
    m_isRegistered = false;

    m_srcView->clearControlPoints();
    m_refView->clearControlPoints();
    for (const auto& cp : m_controlPoints) {
        m_srcView->addControlPoint(QPointF(cp.srcX, cp.srcY));
        m_refView->addControlPoint(QPointF(cp.refX, cp.refY));
    }

    updateControlPointTable();
    updatePickStatus();

    m_statusBar->showMessage(
        QString::fromUtf8("自动匹配完成: 识别到 %1 对同名点, 请检查后执行配准")
            .arg(m_controlPoints.size()), 10000);

    QMessageBox::information(this,
        QString::fromUtf8("自动匹配完成"),
        QString::fromUtf8("成功识别 %1 对同名点。\n\n"
            "已显示在控制点列表中, 请:\n"
            "  1. 检查匹配点是否正确\n"
            "  2. 如有需要可手动补充或删除点\n"
            "  3. 点击「执行配准」进行几何校正")
            .arg(m_controlPoints.size()));
}

// ============================================================================
// onRunRegistration — 执行几何配准 (核心功能)
// 功能: 基于当前控制点和选定的几何模型, 执行最小二乘平差解算
// 流程:
//   1. 检查控制点列表是否为空, 为空则提示
//   2. 从界面获取用户选择的几何模型类型 (仿射/二次多项式)
//   3. 检查控制点数量是否满足模型最低要求
//   4. 调用 GeoModel::detectEdgeWeights 用 Sobel 算子检测边缘点
//   5. 调用 GeoModel::solveWeightedAdjustment 执行加权平差解算:
//      - 边缘点(梯度大)权重 = 5, 非边缘点权重 = 1
//      - 建立误差方程 V = AX - L
//      - 解法方程 (AᵀPA)X = AᵀPL（加权法方程）
//      - 计算各点残差和总体 RMSE
//   6. 解算成功: 更新控制点残差, 标记 m_isRegistered = true
//   6. 弹出精度评定报告窗口 (AccuracyReportDialog)
//   7. 解算失败: 提示控制点共线或法方程奇异
// ============================================================================
void jihepeizhun::onRunRegistration()
{
    if (m_controlPoints.isEmpty()) {
        QMessageBox::warning(this,
            QString::fromUtf8("提示"),
            QString::fromUtf8("请先选取控制点。"));
        return;
    }

    GeoModelType type = static_cast<GeoModelType>(m_modelCombo->currentData().toInt());
    m_geoModel.setModelType(type);

    if (m_controlPoints.size() < m_geoModel.minControlPoints()) {
        QMessageBox::warning(this,
            QString::fromUtf8("控制点不足"),
            QString::fromUtf8("当前模型至少需要 %1 个控制点, 当前仅有 %2 个。")
                .arg(m_geoModel.minControlPoints()).arg(m_controlPoints.size()));
        return;
    }

    // 基于源影像计算边缘权重，边缘点权重=5，非边缘点=1
    // 边缘是纹理最丰富的位置，赋予更高权重可提升配准精度
    QImage srcGray = m_srcView->getImage().convertToFormat(QImage::Format_Grayscale8);
    QVector<double> weights = GeoModel::detectEdgeWeights(srcGray, m_controlPoints);

    int edgeCount = 0;
    for (double w : weights) if (w > 1.5) ++edgeCount;

    // 执行加权平差解算
    AdjustmentResult result = m_geoModel.solveWeightedAdjustment(m_controlPoints, weights);

    if (!result.valid) {
        QMessageBox::critical(this,
            QString::fromUtf8("配准失败"),
            QString::fromUtf8("平差解算失败, 请检查控制点布置。\n"
                                   "可能原因: 控制点共线、法方程奇异。"));
        return;
    }

    // 更新控制点残差
    m_controlPoints = result.points;
    m_isRegistered = true;
    m_lastRMSE = result.rmseTotal;

    // 更新控制点表格 (显示残差)
    updateControlPointTable();
    updatePickStatus();

    // 弹出精度评定报告窗口（含边缘加权信息）
    AccuracyReportDialog dlg(type, m_controlPoints, result, this);
    dlg.exec();

    m_statusBar->showMessage(
        QString::fromUtf8("配准完成! 边缘点=%1/%2, 总体RMSE=%3 像素")
            .arg(edgeCount)
            .arg(m_controlPoints.size())
            .arg(result.rmseTotal, 0, 'f', 4), 10000);
}

// ============================================================================
// 视图操作 — 适应窗口 / 放大 / 缩小
// 功能: 同步控制源影像视图和参考影像视图的缩放行为, 保证两视图保持一致
// ============================================================================

// ============================================================================
// onFitToWindow — 适应窗口显示
// ============================================================================
void jihepeizhun::onFitToWindow()
{
    m_srcView->fitToWindow();
    m_refView->fitToWindow();
}

// ============================================================================
// onZoomIn — 同步放大两视图
// ============================================================================
void jihepeizhun::onZoomIn()
{
    m_srcView->zoomIn();
    m_refView->zoomIn();
}

// ============================================================================
// onZoomOut — 同步缩小两视图
// ============================================================================
void jihepeizhun::onZoomOut()
{
    m_srcView->zoomOut();
    m_refView->zoomOut();
}

// ============================================================================
// 状态栏坐标更新 — 鼠标移动时更新坐标显示
// ============================================================================

// ============================================================================
// onSrcMouseMoved — 更新源影像鼠标坐标
// ============================================================================
void jihepeizhun::onSrcMouseMoved(QPointF imgCoord)
{
    if (m_srcView->isLoaded()) {
        m_srcCoordLabel->setText(
            QString::fromUtf8("源影像: (%1, %2)")
                .arg(imgCoord.x(), 0, 'f', 1).arg(imgCoord.y(), 0, 'f', 1));
    }
}

// ============================================================================
// onRefMouseMoved — 更新参考影像鼠标坐标
// ============================================================================
void jihepeizhun::onRefMouseMoved(QPointF imgCoord)
{
    if (m_refView->isLoaded()) {
        m_refCoordLabel->setText(
            QString::fromUtf8("参考影像: (%1, %2)")
                .arg(imgCoord.x(), 0, 'f', 1).arg(imgCoord.y(), 0, 'f', 1));
    }
}

// ============================================================================
// 界面更新 — 控制点表格刷新 / 状态提示更新
// ============================================================================

// ============================================================================
// updateControlPointTable — 刷新控制点表格
// 功能: 将 m_controlPoints 中的数据逐行写入 QTableWidget
//       6 列: ID / 源X / 源Y / 参考X / 参考Y / 残差(px)
//       配准完成后, 残差大于 1 像素的点以红色标记, 便于识别粗差
// ============================================================================
void jihepeizhun::updateControlPointTable()
{
    m_cpTable->setRowCount(m_controlPoints.size());
    for (int i = 0; i < m_controlPoints.size(); ++i) {
        const auto& cp = m_controlPoints[i];
        m_cpTable->setItem(i, 0, new QTableWidgetItem(QString::number(cp.id)));
        m_cpTable->setItem(i, 1, new QTableWidgetItem(QString::number(cp.srcX, 'f', 4)));
        m_cpTable->setItem(i, 2, new QTableWidgetItem(QString::number(cp.srcY, 'f', 4)));
        m_cpTable->setItem(i, 3, new QTableWidgetItem(QString::number(cp.refX, 'f', 4)));
        m_cpTable->setItem(i, 4, new QTableWidgetItem(QString::number(cp.refY, 'f', 4)));

        // 残差列: 各点残差 = sqrt(resX² + resY²), 配准后大残差点标红
        double err = std::sqrt(cp.resX * cp.resX + cp.resY * cp.resY);
        QTableWidgetItem* resItem = new QTableWidgetItem(
            QString::number(err, 'f', 4));
        if (m_isRegistered && err > 1.0) {
            resItem->setForeground(Qt::red);  // 残差大于 1 像素的点标红
        }
        m_cpTable->setItem(i, 5, resItem);
    }
}

// ============================================================================
// updatePickStatus — 更新界面状态提示
// 功能:
//   1. 根据选择的几何模型, 判断当前控制点数量是否满足执行配准的条件
//   2. 启用/禁用"执行配准"按钮和菜单项
//   3. 在状态标签中追加显示控制点数量信息 (格式: "控制点: N/N (至少需要M个)")
// ============================================================================
void jihepeizhun::updatePickStatus()
{
    GeoModelType type = static_cast<GeoModelType>(m_modelCombo->currentData().toInt());
    int minPts = (type == GeoModelType::Affine) ? 3 : 6;
    int curPts = m_controlPoints.size();

    bool canRun = (curPts >= minPts);
    m_runBtn->setEnabled(canRun);
    if (m_runAction) m_runAction->setEnabled(canRun);

    if (m_statusLabel) {
        // 控制点数量信息追加到已有状态文本后面（PickManager负责刺点状态）
        QString ptsInfo = QString::fromUtf8("<br>控制点: %1/%2 (至少需要%3个)")
            .arg(curPts).arg(curPts).arg(minPts);
        QString curText = m_statusLabel->text();
        // 移除旧的控制点信息部分
        int brPos = curText.indexOf(QString::fromUtf8("<br>控制点:"));
        if (brPos >= 0) curText = curText.left(brPos);
        m_statusLabel->setText(curText + ptsInfo);
    }
}

// ============================================================================
// onComputeOverlap — 计算源/参考影像的重叠区域
// 功能: 使用 AutoMatch::computeOverlap 计算并显示重叠范围信息
// ============================================================================
void jihepeizhun::onComputeOverlap()
{
    if (!m_srcView->isLoaded() || !m_refView->isLoaded()) {
        QMessageBox::warning(this, QString::fromUtf8("提示"),
            QString::fromUtf8("请先加载源影像和参考影像"));
        return;
    }
    if (!m_srcView->hasGeoTransform() || !m_refView->hasGeoTransform()) {
        QMessageBox::warning(this, QString::fromUtf8("提示"),
            QString::fromUtf8("影像缺少地理参考信息 (GeoTransform)\n无法计算重叠区域"));
        return;
    }

    AutoMatch matcher;
    auto result = matcher.computeOverlap(
        m_srcView->imageWidth(), m_srcView->imageHeight(), m_srcView->geoTransform(),
        m_refView->imageWidth(), m_refView->imageHeight(), m_refView->geoTransform());

    if (!result.valid) {
        QMessageBox::information(this, QString::fromUtf8("重叠分析"),
            QString::fromUtf8("两幅影像无重叠区域"));
        return;
    }

    QString info;
    info += QString::fromUtf8("===== 重叠区域分析结果 =====\n\n");

    info += QString::fromUtf8("--- 源影像地理范围 ---\n");
    info += QString::fromUtf8("  X: %1 ~ %2\n").arg(result.srcGeoXmin, 0, 'f', 6).arg(result.srcGeoXmax, 0, 'f', 6);
    info += QString::fromUtf8("  Y: %1 ~ %2\n").arg(result.srcGeoYmin, 0, 'f', 6).arg(result.srcGeoYmax, 0, 'f', 6);

    info += QString::fromUtf8("\n--- 参考影像地理范围 ---\n");
    info += QString::fromUtf8("  X: %1 ~ %2\n").arg(result.refGeoXmin, 0, 'f', 6).arg(result.refGeoXmax, 0, 'f', 6);
    info += QString::fromUtf8("  Y: %1 ~ %2\n").arg(result.refGeoYmin, 0, 'f', 6).arg(result.refGeoYmax, 0, 'f', 6);

    info += QString::fromUtf8("\n--- 重叠区域 ---\n");
    info += QString::fromUtf8("  地理 X: %1 ~ %2\n").arg(result.geoXmin, 0, 'f', 6).arg(result.geoXmax, 0, 'f', 6);
    info += QString::fromUtf8("  地理 Y: %1 ~ %2\n").arg(result.geoYmin, 0, 'f', 6).arg(result.geoYmax, 0, 'f', 6);
    info += QString::fromUtf8("  源影像像素 (x,y,w,h): %1, %2, %3, %4\n")
        .arg(result.srcROI.x()).arg(result.srcROI.y())
        .arg(result.srcROI.width()).arg(result.srcROI.height());
    info += QString::fromUtf8("  参考影像像素 (x,y,w,h): %1, %2, %3, %4\n")
        .arg(result.refROI.x()).arg(result.refROI.y())
        .arg(result.refROI.width()).arg(result.refROI.height());

    m_overlapResult = result;  // 缓存结果，供导出使用

    // 在源影像上以绿色半透明矩形显示重叠区
    m_srcView->setOverlayRect(QRectF(result.srcROI));

    QMessageBox::information(this, QString::fromUtf8("重叠分析"), info);
}

// ============================================================================
// onExportOverlap — 将重叠区域分析结果导出到文本文件
// 功能: 导出内容包括地理范围和像素坐标范围两个层级的重叠信息
// ============================================================================
void jihepeizhun::onExportOverlap()
{
    if (!m_overlapResult.valid) {
        QMessageBox::warning(this, QString::fromUtf8("提示"),
            QString::fromUtf8("请先执行「计算重叠区域」"));
        return;
    }

    QString path = QFileDialog::getSaveFileName(this,
        QString::fromUtf8("导出重叠区域"), "",
        QString::fromUtf8("文本文件 (*.txt);;CSV文件 (*.csv);;所有文件 (*.*)"));
    if (path.isEmpty()) return;

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, QString::fromUtf8("错误"),
            QString::fromUtf8("无法写入文件: %1").arg(path));
        return;
    }

    QTextStream out(&file);
    const auto& r = m_overlapResult;

    out << QString::fromUtf8("重叠区域分析报告\n");
    out << QString::fromUtf8("================\n\n");

    out << QString::fromUtf8("--- 源影像地理范围 ---\n");
    out << QString("X: %1 %2\n").arg(r.srcGeoXmin, 0, 'f', 6).arg(r.srcGeoXmax, 0, 'f', 6);
    out << QString("Y: %1 %2\n").arg(r.srcGeoYmin, 0, 'f', 6).arg(r.srcGeoYmax, 0, 'f', 6);

    out << QString::fromUtf8("\n--- 参考影像地理范围 ---\n");
    out << QString("X: %1 %2\n").arg(r.refGeoXmin, 0, 'f', 6).arg(r.refGeoXmax, 0, 'f', 6);
    out << QString("Y: %1 %2\n").arg(r.refGeoYmin, 0, 'f', 6).arg(r.refGeoYmax, 0, 'f', 6);

    out << QString::fromUtf8("\n--- 重叠区域 ---\n");
    out << QString("GeoX: %1 %2\n").arg(r.geoXmin, 0, 'f', 6).arg(r.geoXmax, 0, 'f', 6);
    out << QString("GeoY: %1 %2\n").arg(r.geoYmin, 0, 'f', 6).arg(r.geoYmax, 0, 'f', 6);
    out << QString("SrcPixel: %1 %2 %3 %4\n")
        .arg(r.srcROI.x()).arg(r.srcROI.y())
        .arg(r.srcROI.width()).arg(r.srcROI.height());
    out << QString("RefPixel: %1 %2 %3 %4\n")
        .arg(r.refROI.x()).arg(r.refROI.y())
        .arg(r.refROI.width()).arg(r.refROI.height());

    file.close();
    QMessageBox::information(this, QString::fromUtf8("导出完成"),
        QString::fromUtf8("重叠区域已导出到:\n%1").arg(path));
}

// ============================================================================
// onExtractFeatures — 对源/参考影像提取 Harris 角点并显示
// 功能: 使用 AutoMatch::extractFeatures 提取特征点并显示在影像视图中
// ============================================================================
void jihepeizhun::onExtractFeatures()
{
    if (!m_srcView->isLoaded() || !m_refView->isLoaded()) {
        QMessageBox::warning(this, QString::fromUtf8("提示"),
            QString::fromUtf8("请先加载源影像和参考影像"));
        return;
    }

    AutoMatch matcher;

    // 使用降采样后的灰度图提取特征点（与自动匹配一致）
    QImage srcGray = m_srcView->getImage();
    QImage refGray = m_refView->getImage();

    // 如果影像太大，先降采样到最长边3000px（与AutoMatch内部逻辑一致）
    auto downsample = [](const QImage& img, int maxDim) -> QImage {
        int m = qMax(img.width(), img.height());
        if (m <= maxDim) return img;
        double s = double(maxDim) / m;
        return img.scaled(int(img.width()*s), int(img.height()*s),
                          Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    };
    QImage srcSmall = downsample(srcGray, 3000);
    QImage refSmall = downsample(refGray, 3000);

    double srcSx = double(srcGray.width()) / srcSmall.width();
    double srcSy = double(srcGray.height()) / srcSmall.height();
    double refSx = double(refGray.width()) / refSmall.width();
    double refSy = double(refGray.height()) / refSmall.height();

    // 若有重叠区域结果，约束特征点提取到重叠区域内
    // 将原始像素坐标的 ROI 缩放到降采样空间作为提取范围
    QRect srcROI_ds, refROI_ds;
    if (m_overlapResult.valid) {
        int dsx0 = static_cast<int>(m_overlapResult.srcROI.x() / srcSx);
        int dsy0 = static_cast<int>(m_overlapResult.srcROI.y() / srcSy);
        int dsx1 = static_cast<int>(std::ceil(
            (m_overlapResult.srcROI.x() + m_overlapResult.srcROI.width()) / srcSx));
        int dsy1 = static_cast<int>(std::ceil(
            (m_overlapResult.srcROI.y() + m_overlapResult.srcROI.height()) / srcSy));
        int drx0 = static_cast<int>(m_overlapResult.refROI.x() / refSx);
        int dry0 = static_cast<int>(m_overlapResult.refROI.y() / refSy);
        int drx1 = static_cast<int>(std::ceil(
            (m_overlapResult.refROI.x() + m_overlapResult.refROI.width()) / refSx));
        int dry1 = static_cast<int>(std::ceil(
            (m_overlapResult.refROI.y() + m_overlapResult.refROI.height()) / refSy));

        srcROI_ds = QRect(dsx0, dsy0, qMax(1, dsx1 - dsx0), qMax(1, dsy1 - dsy0));
        refROI_ds = QRect(drx0, dry0, qMax(1, drx1 - drx0), qMax(1, dry1 - dry0));

        // 在源影像上显示重叠区覆盖
        m_srcView->setOverlayRect(QRectF(m_overlapResult.srcROI));
    }

    // 提取 Harris 角点：有重叠约束时只在重叠区域内提取
    QVector<QPointF> srcCorners = matcher.extractFeatures(
        srcSmall, 1000, m_overlapResult.valid ? srcROI_ds : QRect());
    QVector<QPointF> refCorners = matcher.extractFeatures(
        refSmall, 1000, m_overlapResult.valid ? refROI_ds : QRect());

    // 将降采样角点坐标缩放到原始分辨率，并标注

    QVector<QPointF> srcPts, refPts;
    for (const auto& p : srcCorners)
        srcPts.append(QPointF(p.x() * srcSx, p.y() * srcSy));
    for (const auto& p : refCorners)
        refPts.append(QPointF(p.x() * refSx, p.y() * refSy));

    m_srcView->setFeaturePoints(srcPts);
    m_refView->setFeaturePoints(refPts);

    m_statusLabel->setText(QString::fromUtf8("特征点: 源=%1, 参考=%2")
        .arg(srcPts.size()).arg(refPts.size()));
}

// ============================================================================
// onAbout — 显示关于对话框
// 功能: 显示软件的版本信息、功能简介和编译环境
// ============================================================================
void jihepeizhun::onAbout()
{
    QMessageBox::about(this,
        QString::fromUtf8("关于"),
        QString::fromUtf8(
            "遥感影像几何配准系统 v1.0\n\n"
            "功能:\n"
            "  · 多源卫星影像控制点交互量测 (子像素精度)\n"
            "  · 仿射变换 / 二次多项式 几何模型\n"
            "  · 自编最小二乘平差解算\n"
            "  · 精度评定 (单位权中误差、RMSE、粗差检测)\n\n"
            "编译环境: VS2022 + Qt 5.15.2 + GDAL\n"
            "遥感综合程序设计与开发 课程项目"));
}

// ============================================================================
// setOutputDir — 设置输出目录
// ============================================================================
void jihepeizhun::setOutputDir(const QString &dir) { m_outputDir = dir; }

// ============================================================================
// getResultPaths — 获取结果文件路径列表
// ============================================================================
QStringList jihepeizhun::getResultPaths() const { return m_resultPaths; }
  