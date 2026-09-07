#include "FeatureExtraction4.h"
#include <QStatusBar>
#include <QColorDialog>
#include <QInputDialog>
#include <QMouseEvent>
#include <QScrollBar>
#include <QApplication>
#include <QTextStream>
#include <QPainter>
#include <QFileInfo>
#include <QHeaderView>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QScrollArea>
#include <random>
#include <map>
#include <gdal_priv.h>

FeatureExtraction4::FeatureExtraction4(QWidget* parent)
    : QMainWindow(parent)
    , m_clusteringEngine(nullptr)
    , m_classificationEngine(nullptr)
    , m_featureExtractor(nullptr)
    , m_accuracyAssessment(nullptr)
    , m_exportManager(nullptr)
{
    ui.setupUi(this);

    // ========== 窗口布局优化 ==========
    // 设置合理的最小尺寸，适应不同屏幕分辨率
    setMinimumSize(1024, 680);
    resize(1400, 900);

    // 将左侧参数面板的内容包裹在 QScrollArea 中，确保内容过多时可滚动
    {
        QWidget* oldContent = ui.parameterDock->widget();
        if (oldContent) {
            QScrollArea* scrollArea = new QScrollArea();
            scrollArea->setWidgetResizable(true);
            scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
            scrollArea->setWidget(oldContent);
            ui.parameterDock->setWidget(scrollArea);
        }
    }

    // 将右侧 infoDock 的内容也包裹在 QScrollArea 中
    {
        QWidget* oldContent = ui.infoDock->widget();
        if (oldContent) {
            QScrollArea* scrollArea = new QScrollArea();
            scrollArea->setWidgetResizable(true);
            scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
            scrollArea->setWidget(oldContent);
            ui.infoDock->setWidget(scrollArea);
        }
    }

    // 设置 dock 初始宽度
    ui.parameterDock->setMinimumWidth(280);
    ui.parameterDock->setMaximumWidth(420);
    ui.infoDock->setMinimumWidth(260);
    ui.infoDock->setMaximumWidth(400);

    // ========== 全局样式表 ==========
    setStyleSheet(QString::fromUtf8(
        "QMainWindow { background-color: #f5f6fa; }"
        "QMenuBar { background-color: #2c3e50; color: #ecf0f1; padding: 2px; font-size: 13px; }"
        "QMenuBar::item:selected { background-color: #34495e; }"
        "QMenu { background-color: #ffffff; border: 1px solid #dcdde1; padding: 4px; font-size: 12px; }"
        "QMenu::item:selected { background-color: #3498db; color: white; }"
        "QToolBar { background-color: #ffffff; border-bottom: 1px solid #dcdde1; spacing: 4px; padding: 2px 4px; }"
        "QToolBar QToolButton { padding: 4px 8px; border-radius: 3px; font-size: 12px; }"
        "QToolBar QToolButton:hover { background-color: #e8f4fd; }"
        "QToolBar QToolButton:pressed { background-color: #bee5fd; }"
        "QGroupBox { font-weight: bold; border: 1px solid #dcdde1; border-radius: 4px; margin-top: 6px; padding-top: 10px; background-color: #ffffff; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 6px; color: #2c3e50; }"
        "QDockWidget { font-weight: bold; font-size: 12px; color: #2c3e50; }"
        "QDockWidget::title { background-color: #ecf0f1; padding: 6px; border-bottom: 1px solid #dcdde1; }"
        "QTabWidget::pane { border: 1px solid #dcdde1; background-color: #ffffff; }"
        "QTabBar::tab { padding: 6px 14px; margin-right: 2px; border: 1px solid #dcdde1; border-bottom: none; border-top-left-radius: 4px; border-top-right-radius: 4px; background-color: #ecf0f1; font-size: 12px; }"
        "QTabBar::tab:selected { background-color: #ffffff; font-weight: bold; color: #2c3e50; }"
        "QTabBar::tab:hover { background-color: #d5dbdb; }"
        "QPushButton { padding: 6px 14px; border: 1px solid #bdc3c7; border-radius: 3px; background-color: #ffffff; font-size: 12px; }"
        "QPushButton:hover { background-color: #3498db; color: white; border-color: #2980b9; }"
        "QPushButton:pressed { background-color: #2980b9; }"
        "QComboBox, QSpinBox, QDoubleSpinBox { padding: 4px; border: 1px solid #dcdde1; border-radius: 3px; font-size: 12px; }"
        "QComboBox:hover { border-color: #3498db; }"
        "QTableWidget { font-size: 11px; gridline-color: #ecf0f1; border: 1px solid #dcdde1; }"
        "QHeaderView::section { background-color: #f0f3f5; padding: 4px; border: 1px solid #dcdde1; font-size: 11px; font-weight: bold; }"
        "QStatusBar { background-color: #2c3e50; color: #ecf0f1; font-size: 11px; padding: 2px; }"
        "QProgressBar { border: 1px solid #7f8c8d; border-radius: 2px; text-align: center; font-size: 10px; height: 14px; }"
        "QProgressBar::chunk { background-color: #2ecc71; border-radius: 1px; }"
    ));

    m_clusteringEngine = new ClusteringEngine(this);
    m_classificationEngine = new ClassificationEngine(this);
    m_featureExtractor = new FeatureExtractor(this);
    m_accuracyAssessment = new AccuracyAssessment(this);
    m_exportManager = new ExportManager(this);
    m_landCoverExtractor = new LandCoverExtractor(this);

    setupConnections();
    setupClassTable();
    setupSampleControls();
    initDefaultClasses();
    onClusterAlgoChanged(0);

    statusBar()->showMessage(QString::fromUtf8("\u5C31\u7EEA"));
    addLog(QString::fromUtf8("\u7CFB\u7EDF\u5C31\u7EEA"));

    ui.imageDisplayLabel->installEventFilter(this);
    ui.imageDisplayLabel->setMouseTracking(true);
    ui.imageDisplayLabel->setCursor(Qt::OpenHandCursor);
    ui.clusterDisplayLabel->installEventFilter(this);
    ui.classDisplayLabel->installEventFilter(this);
    ui.compareLeftLabel->installEventFilter(this);
    ui.compareRightLabel->installEventFilter(this);

    // ===== 创建 .ui 中缺失的控件 =====
    // 人工判读模式 QAction
    m_actionAnnotationMode = new QAction(QString::fromUtf8("人工判读"), this);
    m_actionAnnotationMode->setCheckable(true);
    m_actionAnnotationMode->setShortcut(QKeySequence("Ctrl+M"));
    m_actionAnnotationMode->setToolTip(QString::fromUtf8("进入人工判读精度评定模式（手动调整、标注、生成精度报告）"));

    // ===== 创建人工判读 Tab =====
    m_annotationTab = new QWidget();
    m_annotationTab->setObjectName("annotationTab");
    QVBoxLayout* annotationTabLayout = new QVBoxLayout(m_annotationTab);
    annotationTabLayout->setSpacing(6);
    annotationTabLayout->setContentsMargins(6, 6, 6, 6);

    // 会话管理按钮行
    {
        QHBoxLayout* sessionBtnLayout = new QHBoxLayout();
        m_createSessionBtn = new QPushButton(QString::fromUtf8("新建会话"), m_annotationTab);
        m_createSessionBtn->setObjectName("createSessionBtn");
        m_loadSessionBtn = new QPushButton(QString::fromUtf8("加载会话"), m_annotationTab);
        m_loadSessionBtn->setObjectName("loadSessionBtn");
        m_saveSessionBtn = new QPushButton(QString::fromUtf8("保存会话"), m_annotationTab);
        m_saveSessionBtn->setObjectName("saveSessionBtn");
        sessionBtnLayout->addWidget(m_createSessionBtn);
        sessionBtnLayout->addWidget(m_loadSessionBtn);
        sessionBtnLayout->addWidget(m_saveSessionBtn);
        sessionBtnLayout->addStretch();
        annotationTabLayout->addLayout(sessionBtnLayout);
    }

    // 会话信息标签
    m_sessionInfoLabel = new QLabel(m_annotationTab);
    m_sessionInfoLabel->setObjectName("sessionInfoLabel");
    m_sessionInfoLabel->setWordWrap(true);
    m_sessionInfoLabel->setStyleSheet("QLabel { font-size: 10px; color: #555; padding: 4px; background: #f8f9fa; border-radius: 4px; }");
    m_sessionInfoLabel->setText(QString::fromUtf8("未创建会话 — 请先运行分类/聚类分析，然后点击\"新建会话\""));
    annotationTabLayout->addWidget(m_sessionInfoLabel);

    // 标注记录列表
    {
        QLabel* recordListLabel = new QLabel(QString::fromUtf8("标注记录列表:"), m_annotationTab);
        recordListLabel->setStyleSheet("font-weight: bold; margin-top: 4px;");
        annotationTabLayout->addWidget(recordListLabel);
    }

    m_annotationRecordTable = new QTableWidget(m_annotationTab);
    m_annotationRecordTable->setObjectName("annotationRecordTable");
    m_annotationRecordTable->setColumnCount(5);
    m_annotationRecordTable->setMinimumHeight(120);
    m_annotationRecordTable->setStyleSheet("font-size: 10px;");
    m_annotationRecordTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_annotationRecordTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_annotationRecordTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_annotationRecordTable->setHorizontalHeaderLabels(QStringList()
        << QString::fromUtf8("序号")
        << QString::fromUtf8("坐标")
        << QString::fromUtf8("自动标签")
        << QString::fromUtf8("人工标签")
        << QString::fromUtf8("状态"));
    annotationTabLayout->addWidget(m_annotationRecordTable);

    // 标注编辑面板
    m_annotationEditGroup = new QGroupBox(QString::fromUtf8("标注编辑"), m_annotationTab);
    m_annotationEditGroup->setObjectName("annotationEditGroup");
    m_annotationEditGroup->setStyleSheet("font-size: 11px;");
    QFormLayout* annotationEditLayout = new QFormLayout(m_annotationEditGroup);
    annotationEditLayout->setSpacing(4);
    annotationEditLayout->setContentsMargins(6, 10, 6, 6);

    m_editPosLabel = new QLabel(QString::fromUtf8("未选中"), m_annotationEditGroup);
    m_editPosLabel->setObjectName("editPosLabel");
    annotationEditLayout->addRow(QString::fromUtf8("坐标："), m_editPosLabel);

    m_editClassCombo = new QComboBox(m_annotationEditGroup);
    m_editClassCombo->setObjectName("editClassCombo");
    m_editClassCombo->addItem(QString::fromUtf8("-- 不修改 --"), -1);
    annotationEditLayout->addRow(QString::fromUtf8("人工标签："), m_editClassCombo);

    m_editCommentEdit = new QLineEdit(m_annotationEditGroup);
    m_editCommentEdit->setObjectName("editCommentEdit");
    m_editCommentEdit->setPlaceholderText(QString::fromUtf8("备注（可选）"));
    annotationEditLayout->addRow(QString::fromUtf8("备注："), m_editCommentEdit);

    m_applyEditBtn = new QPushButton(QString::fromUtf8("应用修改"), m_annotationEditGroup);
    m_applyEditBtn->setObjectName("applyEditBtn");
    annotationEditLayout->addRow(m_applyEditBtn);

    annotationTabLayout->addWidget(m_annotationEditGroup);

    // 批量操作按钮行
    {
        QHBoxLayout* batchBtnLayout = new QHBoxLayout();
        m_confirmAllBtn = new QPushButton(QString::fromUtf8("全部确认"), m_annotationTab);
        m_confirmAllBtn->setObjectName("confirmAllBtn");
        m_resetAllBtn = new QPushButton(QString::fromUtf8("重置全部"), m_annotationTab);
        m_resetAllBtn->setObjectName("resetAllBtn");
        m_resetAllBtn->setStyleSheet("QPushButton { color: #c0392b; }");
        batchBtnLayout->addWidget(m_confirmAllBtn);
        batchBtnLayout->addWidget(m_resetAllBtn);
        batchBtnLayout->addStretch();
        annotationTabLayout->addLayout(batchBtnLayout);
    }

    // 版本对比
    m_compareVersionsBtn = new QPushButton(QString::fromUtf8("版本对比分析"), m_annotationTab);
    m_compareVersionsBtn->setObjectName("compareVersionsBtn");
    annotationTabLayout->addWidget(m_compareVersionsBtn);

    // 导出报告
    m_exportReportBtn = new QPushButton(QString::fromUtf8("导出精度评定专业报告"), m_annotationTab);
    m_exportReportBtn->setObjectName("exportReportBtn");
    m_exportReportBtn->setStyleSheet("QPushButton { font-weight: bold; color: #1a5276; }");
    annotationTabLayout->addWidget(m_exportReportBtn);

    // PDF/Excel 导出按钮
    {
        QHBoxLayout* exportLayout = new QHBoxLayout();
        m_exportPdfBtn = new QPushButton(QString::fromUtf8("导出 PDF"), m_annotationTab);
        m_exportPdfBtn->setObjectName("exportPdfBtn");
        m_exportExcelBtn = new QPushButton(QString::fromUtf8("导出 Excel"), m_annotationTab);
        m_exportExcelBtn->setObjectName("exportExcelBtn");
        exportLayout->addWidget(m_exportPdfBtn);
        exportLayout->addWidget(m_exportExcelBtn);
        annotationTabLayout->addLayout(exportLayout);
    }

    // 弹性空间
    annotationTabLayout->addStretch();

    // 将人工判读 Tab 添加到 infoTabWidget
    ui.infoTabWidget->addTab(m_annotationTab, QString::fromUtf8("人工判读"));

    // 将人工判读 QAction 添加到菜单和工具栏
    {
        QMenu* menuClassification = findChild<QMenu*>("menuClassification");
        if (menuClassification) {
            menuClassification->addAction(m_actionAnnotationMode);
        }
        QToolBar* mainToolBar = findChild<QToolBar*>("mainToolBar");
        if (mainToolBar) {
            mainToolBar->addAction(m_actionAnnotationMode);
        }
    }

    // 地物提取预览标签
    m_extractionDisplayLabel = new QLabel();
    m_extractionDisplayLabel->setMinimumSize(200, 150);
    m_extractionDisplayLabel->setAlignment(Qt::AlignCenter);
    m_extractionDisplayLabel->setStyleSheet("QLabel { background-color: #f0f0f0; border: 1px dashed #ccc; }");
    m_extractionDisplayLabel->setText(QString::fromUtf8("地物提取结果预览"));
    m_extractionDisplayLabel->installEventFilter(this);
    m_extractionDisplayLabel->setCursor(Qt::OpenHandCursor);

    // 地物提取导出按钮
    m_extractionExportBtn = new QPushButton(QString::fromUtf8("导出地物提取图像"));
    m_extractionExportBtn->setEnabled(false);

    // 将预览标签和导出按钮添加到地物提取 GroupBox
    {
        QLayout* extLayout = ui.extractionGroup->layout();
        if (extLayout) {
            extLayout->addWidget(m_extractionDisplayLabel);
            QHBoxLayout* btnRow = new QHBoxLayout();
            btnRow->addStretch();
            btnRow->addWidget(m_extractionExportBtn);
            QBoxLayout* box = qobject_cast<QBoxLayout*>(extLayout);
            if (box) box->addLayout(btnRow);
        }
    }

    // 地物提取视图 Tab
    m_extractionViewTab = new QWidget();
    {
        QVBoxLayout* tabLayout = new QVBoxLayout(m_extractionViewTab);
        QLabel* tabLabel = new QLabel(QString::fromUtf8("地物提取结果"));
        tabLabel->setAlignment(Qt::AlignCenter);
        tabLabel->setStyleSheet("font-size: 14px; color: #666;");
        tabLayout->addWidget(tabLabel);
    }
    ui.mainTabWidget->addTab(m_extractionViewTab, QString::fromUtf8("地物提取"));

    // 初始化工作流程状态
    updateWorkflowStep(1, false);
    updateWorkflowStep(2, false);
    updateWorkflowStep(3, false);
    updateWorkflowStep(4, false);
}

FeatureExtraction4::~FeatureExtraction4()
{
}

void FeatureExtraction4::closeEvent(QCloseEvent* event)
{
    event->accept();
}

bool FeatureExtraction4::eventFilter(QObject* obj, QEvent* event)
{
    QLabel* label = qobject_cast<QLabel*>(obj);
    if (!label) return QMainWindow::eventFilter(obj, event);

    if (event->type() == QEvent::Wheel) {
        QWheelEvent* wheelEvent = static_cast<QWheelEvent*>(event);

        // 对比面板标签：独立缩放逻辑
        if (label == ui.compareLeftLabel || label == ui.compareRightLabel) {
            double factor = (wheelEvent->angleDelta().y() > 0) ? 1.15 : 0.87;
            m_comparisonZoomLevel = qBound(0.1, m_comparisonZoomLevel * factor, 10.0);
            updateCompareLeftRight();
            return true;
        }

        // 地物提取面板标签：独立缩放逻辑
        if (label == m_extractionDisplayLabel) {
            double oldZoom = m_extractionZoomLevel;
            double factor = (wheelEvent->angleDelta().y() > 0) ? 1.15 : 0.87;
            m_extractionZoomLevel = qBound(0.1, m_extractionZoomLevel * factor, 10.0);
            QPoint mousePos = wheelEvent->position().toPoint();
            double ratio = m_extractionZoomLevel / oldZoom;
            m_extractionPanOffset = mousePos - ratio * (mousePos - m_extractionPanOffset);
            displayExtractionResult();
            return true;
        }

        double oldZoom = m_zoomLevel;
        double factor = (wheelEvent->angleDelta().y() > 0) ? 1.15 : 0.87;
        m_zoomLevel = qBound(0.1, m_zoomLevel * factor, 10.0);
        QPoint mousePos = wheelEvent->position().toPoint();
        double ratio = m_zoomLevel / oldZoom;
        m_panOffset = mousePos - ratio * (mousePos - m_panOffset);
        applyZoomPanTransform();
        return true;
    }

    if (event->type() == QEvent::MouseButtonPress) {
        QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);

        // 对比面板标签：支持拖拽平移
        if (label == ui.compareLeftLabel || label == ui.compareRightLabel) {
            if (mouseEvent->button() == Qt::LeftButton) {
                m_comparisonIsPanning = true;
                m_lastPanPos = mouseEvent->pos();
                label->setCursor(Qt::ClosedHandCursor);
                return true;
            }
        }

        // 地物提取面板标签：支持拖拽平移
        if (label == m_extractionDisplayLabel) {
            if (mouseEvent->button() == Qt::LeftButton) {
                m_extractionIsPanning = true;
                m_lastPanPos = mouseEvent->pos();
                label->setCursor(Qt::ClosedHandCursor);
                return true;
            }
        }

        // 手动采样模式：左键点击优先采样，中键用于平移
        if (mouseEvent->button() == Qt::LeftButton && m_manualSamplingActive) {
            QPoint pos = mapToImage(mouseEvent->pos());
            if (m_image.isValid() && pos.x() >= 0 && pos.x() < m_image.width &&
                pos.y() >= 0 && pos.y() < m_image.height) {
                m_manualSamplePoints.push_back(pos);
                m_manualSampleLabels.push_back(m_currentSampleClass);
                // 刷新显示以绘制采样点标记
                applyZoomPanTransform();
                QTableWidgetItem* nameItem = ui.classDefTable->item(m_currentSampleClass, 1);
                QString className = nameItem ? nameItem->text() : QString::number(m_currentSampleClass);
                statusBar()->showMessage(QString::fromUtf8("\u5DF2\u91C7\u96C6\u6837\u672C: [%1,%2] \u7C7B\u522B: %3 \u5171%4\u4E2A")
                    .arg(pos.x()).arg(pos.y()).arg(className).arg(m_manualSamplePoints.size()));
            }
            return true;
        }

        // 中键或（非采样模式下的左键）用于平移
        if (mouseEvent->button() == Qt::MiddleButton ||
            (mouseEvent->button() == Qt::LeftButton && mouseEvent->modifiers() == Qt::NoModifier)) {
            m_isPanning = true;
            m_lastPanPos = mouseEvent->pos();
            label->setCursor(Qt::ClosedHandCursor);
            return true;
        }

        // 原始影像点击：显示像素信息
        if (obj == ui.imageDisplayLabel && mouseEvent->button() == Qt::LeftButton && !m_isPanning) {
            QPoint pos = mapToImage(mouseEvent->pos());
            if (m_image.isValid() && pos.x() >= 0 && pos.x() < m_image.width &&
                pos.y() >= 0 && pos.y() < m_image.height) {
                onImageClicked(pos);
            }
        }
        return true;
    }

    if (event->type() == QEvent::MouseMove) {
        QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);
        // 对比面板拖拽
        if (m_comparisonIsPanning) {
            QPoint delta = mouseEvent->pos() - m_lastPanPos;
            m_comparisonPanOffset += delta;
            m_lastPanPos = mouseEvent->pos();
            updateCompareLeftRight();
            return true;
        }
        // 地物提取面板拖拽
        if (m_extractionIsPanning) {
            QPoint delta = mouseEvent->pos() - m_lastPanPos;
            m_extractionPanOffset += delta;
            m_lastPanPos = mouseEvent->pos();
            displayExtractionResult();
            return true;
        }
        if (m_isPanning) {
            QPoint delta = mouseEvent->pos() - m_lastPanPos;
            m_panOffset += delta;
            m_lastPanPos = mouseEvent->pos();
            applyZoomPanTransform();
            return true;
        }
    }

    if (event->type() == QEvent::MouseButtonRelease) {
        QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);
        // 对比面板释放拖拽
        if (m_comparisonIsPanning) {
            m_comparisonIsPanning = false;
            label->setCursor(Qt::OpenHandCursor);
            return true;
        }
        // 地物提取面板释放拖拽
        if (m_extractionIsPanning) {
            m_extractionIsPanning = false;
            label->setCursor(Qt::OpenHandCursor);
            return true;
        }
        if (mouseEvent->button() == Qt::MiddleButton ||
            (mouseEvent->button() == Qt::LeftButton && !m_manualSamplingActive)) {
            m_isPanning = false;
            label->setCursor(Qt::OpenHandCursor);
            return true;
        }
    }

    return QMainWindow::eventFilter(obj, event);
}

void FeatureExtraction4::setupConnections()
{
    connect(ui.actionOpenImage, &QAction::triggered, this, &FeatureExtraction4::onOpenImage);
    connect(ui.actionOpenMultiBand, &QAction::triggered, this, &FeatureExtraction4::onOpenMultiBand);
    connect(ui.actionSaveImage, &QAction::triggered, this, &FeatureExtraction4::onSaveImage);
    connect(ui.actionExportResult, &QAction::triggered, this, &FeatureExtraction4::onExportResult);
    connect(ui.actionExit, &QAction::triggered, this, &QMainWindow::close);
    connect(ui.actionRunClustering, &QAction::triggered, this, &FeatureExtraction4::onRunClustering);
    connect(ui.actionRunClassification, &QAction::triggered, this, &FeatureExtraction4::onRunClassification);
    connect(ui.actionComputeIndices, &QAction::triggered, this, &FeatureExtraction4::onComputeIndices);
    connect(ui.actionAccuracyAssessment, &QAction::triggered, this, &FeatureExtraction4::onAccuracyAssessment);
    connect(m_actionAnnotationMode, &QAction::triggered, [this](bool) { onAccuracyAnnotationMode(); });
    connect(ui.actionShowComparison, &QAction::triggered, this, &FeatureExtraction4::onShowComparison);
    connect(ui.actionExtractLandCover, &QAction::triggered, this, &FeatureExtraction4::onExtractLandCover);
    connect(ui.actionAbout, &QAction::triggered, this, &FeatureExtraction4::onAbout);
    connect(ui.actionShowParameterPanel, &QAction::triggered, [this]() {
        ui.parameterDock->setVisible(!ui.parameterDock->isVisible());
    });

    connect(ui.clusterAlgoCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &FeatureExtraction4::onClusterAlgoChanged);
    connect(ui.supervisedRadioBtn, &QRadioButton::toggled,
            this, &FeatureExtraction4::onClassificationModeChanged);
    connect(ui.unsupervisedRadioBtn, &QRadioButton::toggled,
            this, &FeatureExtraction4::onClassificationModeChanged);

    // 样本选择连接
    connect(ui.manualSampleRadioBtn, &QRadioButton::toggled,
            this, &FeatureExtraction4::onSampleModeChanged);
    connect(ui.autoSampleRadioBtn, &QRadioButton::toggled,
            this, &FeatureExtraction4::onSampleModeChanged);
    connect(ui.autoSampleMethodCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &FeatureExtraction4::onAutoSampleMethodChanged);
    connect(ui.manualSampleBtn, &QPushButton::clicked,
            this, &FeatureExtraction4::onManualStartSampling);
    connect(ui.autoGenerateBtn, &QPushButton::clicked,
            this, &FeatureExtraction4::onAutoGenerateSamples);

    // 地物类别管理连接
    connect(ui.addClassBtn, &QPushButton::clicked,
            this, &FeatureExtraction4::onAddClass);
    connect(ui.removeClassBtn, &QPushButton::clicked,
            this, &FeatureExtraction4::onRemoveClass);
    connect(ui.renameClassBtn, &QPushButton::clicked,
            this, &FeatureExtraction4::onRenameClass);
    connect(ui.classDefTable, &QTableWidget::itemSelectionChanged,
            this, &FeatureExtraction4::onClassTableSelectionChanged);
    connect(ui.extractBtn, &QPushButton::clicked,
            this, &FeatureExtraction4::onExtractLandCover);
    connect(m_extractionExportBtn, &QPushButton::clicked,
            this, &FeatureExtraction4::onExtractionExport);

    // 地物提取参数实时调整
    connect(ui.ndwiThresholdSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), [this](double) {
        if (m_extractionResult.isValid() && ui.waterCheckBox->isChecked())
            onExtractLandCover();
    });
    connect(ui.ndbiThresholdSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), [this](double) {
        if (m_extractionResult.isValid() && ui.buildingCheckBox->isChecked())
            onExtractLandCover();
    });
    connect(ui.ndviThresholdSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), [this](double) {
        if (m_extractionResult.isValid())
            onExtractLandCover();
    });
    connect(ui.minAreaSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), [this](int) {
        if (m_extractionResult.isValid())
            onExtractLandCover();
    });
    connect(ui.morphKernelSpin, QOverload<int>::of(&QSpinBox::valueChanged), [this](int) {
        if (m_extractionResult.isValid())
            onExtractLandCover();
    });

    connect(m_clusteringEngine, &ClusteringEngine::progressUpdated,
            this, &FeatureExtraction4::onClusteringProgress);
    connect(m_clusteringEngine, &ClusteringEngine::statusMessage,
            this, &FeatureExtraction4::onStatusMessage);
    connect(m_classificationEngine, &ClassificationEngine::progressUpdated,
            this, &FeatureExtraction4::onClassificationProgress);
    connect(m_classificationEngine, &ClassificationEngine::statusMessage,
            this, &FeatureExtraction4::onStatusMessage);
    connect(m_featureExtractor, &FeatureExtractor::progressUpdated,
            this, &FeatureExtraction4::onClusteringProgress);
    connect(m_exportManager, &ExportManager::progressUpdated,
            this, &FeatureExtraction4::onExportProgress);
    connect(m_exportManager, &ExportManager::statusMessage,
            this, &FeatureExtraction4::onStatusMessage);
    connect(m_exportManager, &ExportManager::exportFinished,
            this, &FeatureExtraction4::onExportFinished);
    connect(m_exportManager, &ExportManager::exportError,
            this, &FeatureExtraction4::onExportError);
    connect(m_landCoverExtractor, &LandCoverExtractor::progressUpdated,
            this, &FeatureExtraction4::onExtractionProgress);

    // 人工判读控件连接
    connect(m_createSessionBtn, &QPushButton::clicked, [this]() {
        onAccuracyAnnotationMode();
    });
    connect(m_loadSessionBtn, &QPushButton::clicked, this, &FeatureExtraction4::onAnnotationLoadSession);
    connect(m_saveSessionBtn, &QPushButton::clicked, this, &FeatureExtraction4::onAnnotationSaveSession);
    connect(m_confirmAllBtn, &QPushButton::clicked, this, &FeatureExtraction4::onAnnotationConfirmAll);
    connect(m_resetAllBtn, &QPushButton::clicked, this, &FeatureExtraction4::onAnnotationResetAll);
    connect(m_compareVersionsBtn, &QPushButton::clicked, this, &FeatureExtraction4::onAnnotationCompareVersions);
    connect(m_exportReportBtn, &QPushButton::clicked, this, &FeatureExtraction4::onAnnotationExportReport);
    connect(m_exportPdfBtn, &QPushButton::clicked, this, &FeatureExtraction4::onAnnotationExportPdf);
    connect(m_exportExcelBtn, &QPushButton::clicked, this, &FeatureExtraction4::onAnnotationExportExcel);
    connect(m_applyEditBtn, &QPushButton::clicked, this, &FeatureExtraction4::onAnnotationCommentEdited);
    connect(m_annotationRecordTable, &QTableWidget::itemSelectionChanged, this, &FeatureExtraction4::onAnnotationRecordSelected);
    connect(m_editClassCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &FeatureExtraction4::onAnnotationLabelChanged);

    connect(ui.clearLogBtn, &QPushButton::clicked, [this]() {
        ui.logTextEdit->clear();
        addLog(QString::fromUtf8("日志已清空"));
    });
}

void FeatureExtraction4::setupClassTable()
{
    ui.classDefTable->setHorizontalHeaderLabels({
        QString::fromUtf8("\u5E8F\u53F7"),
        QString::fromUtf8("\u5730\u7269\u7C7B\u578B"),
        QString::fromUtf8("\u989C\u8272")
    });
    ui.classDefTable->horizontalHeader()->setStretchLastSection(false);
    ui.classDefTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    ui.classDefTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    ui.classDefTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    ui.classDefTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui.classDefTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
}

void FeatureExtraction4::setupSampleControls()
{
    // 初始状态：手动模式
    ui.manualSampleBtn->setEnabled(true);
    ui.autoGenerateBtn->setEnabled(false);
    ui.autoSampleMethodCombo->setEnabled(false);
    ui.samplesPerClassSpinBox->setEnabled(false);
}

void FeatureExtraction4::initDefaultClasses()
{
    struct DefaultClass {
        QString name;
        QColor color;
    };

    std::vector<DefaultClass> defaults = {
        {QString::fromUtf8("\u6C34\u4F53"), QColor(0, 0, 255)},
        {QString::fromUtf8("\u690D\u88AB"), QColor(0, 255, 0)},
        {QString::fromUtf8("\u5EFA\u7B51"), QColor(255, 0, 0)},
        {QString::fromUtf8("\u9053\u8DEF"), QColor(128, 128, 128)}
    };

    ui.classDefTable->setRowCount(static_cast<int>(defaults.size()));
    for (size_t i = 0; i < defaults.size(); ++i) {
        QTableWidgetItem* seqItem = new QTableWidgetItem(QString::number(i + 1));
        seqItem->setTextAlignment(Qt::AlignCenter);
        seqItem->setFlags(seqItem->flags() & ~Qt::ItemIsEditable);
        ui.classDefTable->setItem(static_cast<int>(i), 0, seqItem);

        QTableWidgetItem* nameItem = new QTableWidgetItem(defaults[i].name);
        nameItem->setFlags(nameItem->flags() & ~Qt::ItemIsEditable);
        ui.classDefTable->setItem(static_cast<int>(i), 1, nameItem);

        QTableWidgetItem* colorItem = new QTableWidgetItem();
        colorItem->setBackground(defaults[i].color);
        colorItem->setFlags(colorItem->flags() & ~Qt::ItemIsEditable);
        ui.classDefTable->setItem(static_cast<int>(i), 2, colorItem);
    }
}

void FeatureExtraction4::refreshClassTable()
{
    // 更新序号
    for (int row = 0; row < ui.classDefTable->rowCount(); ++row) {
        QTableWidgetItem* seqItem = ui.classDefTable->item(row, 0);
        if (seqItem)
            seqItem->setText(QString::number(row + 1));
    }
}

void FeatureExtraction4::initClassLegendPreview()
{
    // 图例将在分类完成后通过 updateClassLegend 显示
}

void FeatureExtraction4::onOpenImage()
{
    QString filePath = QFileDialog::getOpenFileName(this,
        QString::fromUtf8("\u6253\u5F00\u5F71\u50CF"),
        QString(),
        QString::fromUtf8("\u5F71\u50CF\u6587\u4EF6 (*.png *.jpg *.jpeg *.bmp *.tif *.tiff *.gif);;All (*.*)"));

    if (filePath.isEmpty()) return;

    // 先用 GDAL 快速获取图像尺寸，判断是否需要降采样
    const long long LARGE_IMAGE_THRESHOLD = 100000000; // 1亿像素
    bool useDownsampled = false;

    {
        GDALDataset* probe = static_cast<GDALDataset*>(GDALOpen(filePath.toUtf8().constData(), GA_ReadOnly));
        if (probe) {
            long long totalPixels = static_cast<long long>(probe->GetRasterXSize()) * static_cast<long long>(probe->GetRasterYSize());
            GDALClose(probe);

            if (totalPixels > LARGE_IMAGE_THRESHOLD) {
                double estMem = totalPixels * 3.0 * 8.0 / (1024.0 * 1024.0 * 1024.0); // 3 波段 double ≈ GB
                QMessageBox::StandardButton reply = QMessageBox::question(this,
                    QString::fromUtf8("\u5927\u56FE\u63D0\u793A"),
                    QString::fromUtf8(
                        "\u5F71\u50CF\u8F83\u5927 (%1 \u4E07\u50CF\u7D20)\uFF0C\u5B8C\u6574\u52A0\u8F7D\u7EA6\u9700 %2 GB \u5185\u5B58\u3002\n\n"
                        "\u662F\u5426\u964D\u91C7\u6837\u52A0\u8F7D\uFF1F\n"
                        "\u2022 \u300C\u662F\u300D: \u964D\u91C7\u6837\u81F3 1 \u4EBF\u50CF\u7D20\uFF0C\u53EF\u6B63\u5E38\u8FDB\u884C\u5730\u7269\u5206\u7C7B\n"
                        "\u2022 \u300C\u5426\u300D: \u539F\u5206\u8FA8\u7387\u52A0\u8F7D\uFF0C\u53EF\u80FD\u5185\u5B58\u4E0D\u8DB3")
                        .arg(totalPixels / 10000.0, 0, 'f', 1)
                        .arg(estMem, 0, 'f', 1),
                    QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);

                useDownsampled = (reply == QMessageBox::Yes);
            }
        }
    }

    // 显示加载进度
    updateProgress(0, QString::fromUtf8("\u6B63\u5728\u52A0\u8F7D\u5F71\u50CF..."));

    bool loaded;
    if (useDownsampled) {
        loaded = m_image.loadDownsampled(filePath, LARGE_IMAGE_THRESHOLD, [this](int percent, const QString& stage) {
            updateProgress(percent, stage);
            QApplication::processEvents();
        });
    } else {
        loaded = m_image.loadFromImage(filePath, [this](int percent, const QString& stage) {
            updateProgress(percent, stage);
            QApplication::processEvents();
        });
    }

    if (loaded) {
        QImage displayImg = m_image.toDisplayImage(4096);
        m_zoomLevel = 1.0;
        m_comparisonZoomLevel = 1.0;
        m_panOffset = QPoint(0, 0);
        displayImage(displayImg, ui.imageDisplayLabel);
        ui.mainTabWidget->setCurrentIndex(0);
        updateStatusBarImageInfo();
        addLog(QString::fromUtf8("\u5F71\u50CF: %1 (%2x%3, %4\u6CE2\u6BB5)")
                   .arg(QFileInfo(filePath).fileName()).arg(m_image.width).arg(m_image.height).arg(m_image.bands));
        updateWorkflowStep(1, true);
        updateProgress(100, QString::fromUtf8("\u52A0\u8F7D\u5B8C\u6210"));
        statusBar()->showMessage(QString::fromUtf8(
            "%1 | %2x%3 | %4\u6CE2\u6BB5")
            .arg(QFileInfo(filePath).fileName()).arg(m_image.width).arg(m_image.height)
            .arg(m_image.bands));
    } else {
        updateProgress(0, QString::fromUtf8("\u5C31\u7EEA"));
        QMessageBox::warning(this, QString::fromUtf8("\u9519\u8BEF"),
                             QString::fromUtf8("\u65E0\u6CD5\u52A0\u8F7D\u5F71\u50CF: ") + filePath);
        addLog(QString::fromUtf8("\u5F71\u50CF\u52A0\u8F7D\u5931\u8D25: %1").arg(filePath));
    }
}

void FeatureExtraction4::onOpenMultiBand()
{
    QStringList files = QFileDialog::getOpenFileNames(this,
        QString::fromUtf8("\u9009\u62E9\u591A\u6CE2\u6BB5\u5F71\u50CF\u6587\u4EF6"),
        QString(),
        QString::fromUtf8("\u5F71\u50CF\u6587\u4EF6 (*.png *.jpg *.jpeg *.bmp *.tif *.tiff);;All (*.*)"));

    if (files.isEmpty()) return;

    // 显示加载进度
    updateProgress(0, QString::fromUtf8("\u6B63\u5728\u52A0\u8F7D\u591A\u6CE2\u6BB5\u5F71\u50CF..."));

    // 使用进度回调，通过 processEvents 保持 UI 响应
    bool loaded = m_image.loadMultiBand(files, [this](int percent, const QString& stage) {
        updateProgress(percent, stage);
        QApplication::processEvents();
    });

    if (loaded) {
        QImage displayImg = m_image.toDisplayImage(4096);
        m_zoomLevel = 1.0;
        m_comparisonZoomLevel = 1.0;
        m_panOffset = QPoint(0, 0);
        displayImage(displayImg, ui.imageDisplayLabel);
        ui.mainTabWidget->setCurrentIndex(0);
        updateStatusBarImageInfo();
        addLog(QString::fromUtf8("\u591A\u6CE2\u6BB5\u5F71\u50CF\u52A0\u8F7D\u6210\u529F: %1\u6CE2\u6BB5").arg(m_image.bands));
        addLog(m_image.infoSummary());
        updateWorkflowStep(1, true);
        updateProgress(100, QString::fromUtf8("\u52A0\u8F7D\u5B8C\u6210"));
        statusBar()->showMessage(QString::fromUtf8(
            "\u5DF2\u52A0\u8F7D\u591A\u6CE2\u6BB5: %1\u6CE2\u6BB5 | \u5206\u8FA8\u7387: %2x%3")
            .arg(m_image.bands).arg(m_image.width).arg(m_image.height));
    } else {
        updateProgress(0, QString::fromUtf8("\u5C31\u7EEA"));
        QMessageBox::warning(this, QString::fromUtf8("\u9519\u8BEF"),
                             QString::fromUtf8("\u65E0\u6CD5\u52A0\u8F7D\u591A\u6CE2\u6BB5\u5F71\u50CF"));
        addLog(QString::fromUtf8("\u591A\u6CE2\u6BB5\u5F71\u50CF\u52A0\u8F7D\u5931\u8D25"));
    }
}

void FeatureExtraction4::onSaveImage()
{
    if (!m_image.isValid()) {
        QMessageBox::warning(this, QString::fromUtf8("\u63D0\u793A"),
                             QString::fromUtf8("\u8BF7\u5148\u52A0\u8F7D\u5F71\u50CF"));
        return;
    }

    QString filePath = QFileDialog::getSaveFileName(this,
        QString::fromUtf8("\u4FDD\u5B58\u56FE\u50CF"),
        QString(),
        QString::fromUtf8("PNG (*.png);;JPEG (*.jpg);;BMP (*.bmp);;TIFF (*.tif)"));

    if (filePath.isEmpty()) return;

    QImage img = m_image.toQImage();
    if (img.save(filePath)) {
        statusBar()->showMessage(QString::fromUtf8("\u5DF2\u4FDD\u5B58: ") + filePath);
    } else {
        QMessageBox::warning(this, QString::fromUtf8("\u9519\u8BEF"),
                             QString::fromUtf8("\u65E0\u6CD5\u4FDD\u5B58\u56FE\u50CF"));
    }
}

void FeatureExtraction4::onExportResult()
{
    // 优先导出地物提取结果
    if (m_extractionResult.isValid()) {
        applyExportConfig();

        QString filter;
        QString formatName;
        switch (m_exportManager->config().format) {
        case ExportFormat::GeoTIFF:  filter = QString::fromUtf8("GeoTIFF (*.tif *.tiff)"); formatName = "GeoTIFF"; break;
        case ExportFormat::Shapefile: filter = QString::fromUtf8("Shapefile (*.shp)"); formatName = "Shapefile"; break;
        case ExportFormat::CSV:      filter = QString::fromUtf8("CSV (*.csv)"); formatName = "CSV"; break;
        default:                     filter = QString::fromUtf8("CSV (*.csv)"); formatName = "CSV"; break;
        }

        QString filePath = QFileDialog::getSaveFileName(this,
            QString::fromUtf8("\u5BFC\u51FA\u5730\u7269\u63D0\u53D6\u7ED3\u679C"), QString(), filter);
        if (filePath.isEmpty()) return;

        ExportConfig cfg = m_exportManager->config();
        cfg.outputPath = filePath;
        m_exportManager->setConfig(cfg);

        addLog(QString::fromUtf8("\u5BFC\u51FA\u5730\u7269\u63D0\u53D6 - %1: %2").arg(formatName).arg(filePath));
        m_exportManager->exportExtractionResult(m_extractionResult);
        return;
    }

    // 其次导出分类/聚类结果
    ClassificationResult* result = nullptr;
    if (m_classificationResult.isValid())
        result = &m_classificationResult;
    else if (m_clusteringResult.isValid())
        result = &m_clusteringResult;
    else {
        QMessageBox::warning(this, QString::fromUtf8("\u63D0\u793A"),
                             QString::fromUtf8("\u8BF7\u5148\u8FD0\u884C\u5206\u7C7B\u6216\u805A\u7C7B\u5206\u6790"));
        return;
    }

    applyExportConfig();

    QString filter;
    QString formatName;
    switch (m_exportManager->config().format) {
    case ExportFormat::GeoTIFF:  filter = QString::fromUtf8("GeoTIFF (*.tif *.tiff)"); formatName = "GeoTIFF"; break;
    case ExportFormat::Shapefile: filter = QString::fromUtf8("Shapefile (*.shp)"); formatName = "Shapefile"; break;
    case ExportFormat::CSV:      filter = QString::fromUtf8("CSV (*.csv)"); formatName = "CSV"; break;
    case ExportFormat::ENVI_BSQ: filter = QString::fromUtf8("ENVI (*.dat)"); formatName = "ENVI"; break;
    }

    QString filePath = QFileDialog::getSaveFileName(this,
        QString::fromUtf8("\u5BFC\u51FA\u5206\u7C7B\u7ED3\u679C"), QString(), filter);
    if (filePath.isEmpty()) return;

    ExportConfig cfg = m_exportManager->config();
    cfg.outputPath = filePath;
    m_exportManager->setConfig(cfg);

    addLog(QString::fromUtf8("\u5BFC\u51FA - %1: %2").arg(formatName).arg(filePath));
    m_exportManager->exportClassificationResult(*result);
}

void FeatureExtraction4::onRunClustering()
{
    if (!m_image.isValid()) {
        QMessageBox::warning(this, QString::fromUtf8("\u63D0\u793A"),
                             QString::fromUtf8("\u8BF7\u5148\u52A0\u8F7D\u5F71\u50CF"));
        return;
    }

    applyClusteringConfig();
    updateWorkflowStep(2, true);

    int algoIndex = ui.clusterAlgoCombo->currentIndex();
    QString algoName = (algoIndex == 0) ? "K-Means" : "ISODATA";
    addLog(QString::fromUtf8("\u5F00\u59CB\u805A\u7C7B\u5206\u6790 - %1").arg(algoName));

    updateProgress(0, QString::fromUtf8("\u6B63\u5728\u8FD0\u884C\u805A\u7C7B\u5206\u6790..."));

    QApplication::processEvents();

    ClusteringResult cr;

    if (algoIndex == 0) {
        cr = m_clusteringEngine->kMeans(m_image);
    } else {
        int minSize = ui.isodataMinSizeSpin->value();
        double maxStdDev = ui.isodataStdDevSpin->value();
        double minDist = ui.isodataMinDistSpin->value();
        int maxMerge = ui.isodataMaxMergeSpin->value();
        cr = m_clusteringEngine->isodata(m_image, {}, minSize, maxStdDev, minDist, maxMerge);
    }

    if (cr.labels.empty()) {
        updateProgress(0, QString());
        QMessageBox::critical(this, QString::fromUtf8("\u805A\u7C7B\u5931\u8D25"),
                              QString::fromUtf8("\u5185\u5B58\u4E0D\u8DB3\uFF0C\u65E0\u6CD5\u5206\u914D\u805A\u7C7B\u7ED3\u679C\u7A7A\u95F4\u3002\n\n"
                                                "\u8BF7\u5C1D\u8BD5\uFF1A\n"
                                                "\u2022 \u5173\u95ED\u5176\u4ED6\u7A0B\u5E8F\u91CA\u653E\u5185\u5B58\n"
                                                "\u2022 \u964D\u4F4E\u56FE\u50CF\u5206\u8FA8\u7387\u540E\u91CD\u8BD5"));
        return;
    }

    m_clusteringResult = cr.toClassificationResult(m_image.width, m_image.height);

    ui.mainTabWidget->setCurrentIndex(2);
    displayResultOnCurrentTab(m_clusteringResult);

    addLog(QString::fromUtf8("\u805A\u7C7B\u5B8C\u6210 - %1, \u8FED\u4EE3: %2").arg(algoName).arg(cr.iterations));

    updateWorkflowStep(3, true);
    statusBar()->showMessage(QString::fromUtf8("\u805A\u7C7B\u5206\u6790\u5B8C\u6210 - %1, \u8FED\u4EE3: %2, \u60EF\u6027: %3")
                                 .arg(algoName).arg(cr.iterations).arg(cr.inertia, 0, 'f', 2));

    updateStatusBarClassificationInfo();

    updateProgress(100, QString::fromUtf8("\u805A\u7C7B\u5206\u6790\u5B8C\u6210"));
    autoCreateAnnotationSession();
}

void FeatureExtraction4::onRunClassification()
{
    if (!m_image.isValid()) {
        QMessageBox::warning(this, QString::fromUtf8("\u63D0\u793A"),
                             QString::fromUtf8("\u8BF7\u5148\u52A0\u8F7D\u5F71\u50CF"));
        return;
    }

    collectTrainingSamples();

    if (!m_trainingData.isValid()) {
        QMessageBox::warning(this, QString::fromUtf8("\u63D0\u793A"),
                             QString::fromUtf8("\u8BF7\u5148\u9009\u62E9\u8BAD\u7EC3\u6837\u672C\u3002"));
        return;
    }

    applyClassifierConfig();
    updateWorkflowStep(2, true);

    addLog(QString::fromUtf8("\u5F00\u59CB\u6700\u5927\u4F3C\u7136\u6CD5\u5730\u7269\u5206\u7C7B - \u6837\u672C\u6570: %1, \u7C7B\u522B\u6570: %2")
               .arg(m_trainingData.sampleCount())
               .arg(m_trainingData.classCount));

    updateProgress(0, QString::fromUtf8("\u6B63\u5728\u8FD0\u884C\u6700\u5927\u4F3C\u7136\u6CD5\u5730\u7269\u5206\u7C7B..."));

    QApplication::processEvents();

    std::vector<int> bandIndices;
    for (int b = 0; b < m_image.bands; ++b)
        bandIndices.push_back(b);

    m_classificationResult = m_classificationEngine->classify(m_image, m_trainingData, bandIndices);

    if (!m_classificationResult.isValid()) {
        updateProgress(0, QString());
        QMessageBox::critical(this, QString::fromUtf8("\u5206\u7C7B\u5931\u8D25"),
                              QString::fromUtf8("\u5185\u5B58\u4E0D\u8DB3\uFF0C\u65E0\u6CD5\u5206\u914D\u5206\u7C7B\u7ED3\u679C\u7A7A\u95F4\u3002\n\n"
                                                "\u8BF7\u5C1D\u8BD5\uFF1A\n"
                                                "\u2022 \u5173\u95ED\u5176\u4ED6\u7A0B\u5E8F\u91CA\u653E\u5185\u5B58\n"
                                                "\u2022 \u964D\u4F4E\u56FE\u50CF\u5206\u8FA8\u7387\u540E\u91CD\u8BD5\n"
                                                "\u2022 \u4F7F\u7528\u65E0\u76D1\u7763\u5206\u7C7B\u6A21\u5F0F"));
        return;
    }

    ui.mainTabWidget->setCurrentIndex(1);
    displayResultOnCurrentTab(m_classificationResult);

    addLog(QString::fromUtf8("\u5206\u7C7B\u5B8C\u6210 - \u7C7B\u522B\u6570: %1").arg(m_classificationResult.classCount));

    updateWorkflowStep(3, true);
    statusBar()->showMessage(QString::fromUtf8("\u6700\u5927\u4F3C\u7136\u6CD5\u5206\u7C7B\u5B8C\u6210 - \u7C7B\u522B\u6570: %1")
                                 .arg(m_classificationResult.classCount));

    updateStatusBarClassificationInfo();

    updateProgress(100, QString::fromUtf8("\u5206\u7C7B\u5B8C\u6210"));
    autoCreateAnnotationSession();
}

void FeatureExtraction4::onComputeIndices()
{
    if (!m_image.isValid()) {
        QMessageBox::warning(this, QString::fromUtf8("\u63D0\u793A"),
                             QString::fromUtf8("\u8BF7\u5148\u52A0\u8F7D\u5F71\u50CF"));
        return;
    }

    updateProgress(0, QString::fromUtf8("\u6B63\u5728\u8BA1\u7B97\u5149\u8C31\u6307\u6570..."));

    QApplication::processEvents();

    SpectralIndices indices = m_featureExtractor->computeSpectralIndices(m_image);

    if (indices.isValid() && indices.ndvi.size() > 0) {
        QImage ndviImg = indices.indexToImage(indices.ndvi, -1, 1);
        displayImage(ndviImg, ui.imageDisplayLabel);
        statusBar()->showMessage(QString::fromUtf8("\u5149\u8C31\u6307\u6570\u8BA1\u7B97\u5B8C\u6210 (NDVI, NDWI, NDBI, MNDWI)"));
    }

    updateProgress(100, QString::fromUtf8("\u5149\u8C31\u6307\u6570\u8BA1\u7B97\u5B8C\u6210"));
}

void FeatureExtraction4::onAccuracyAssessment()
{
    ClassificationResult* result = nullptr;
    if (m_classificationResult.isValid())
        result = &m_classificationResult;
    else if (m_clusteringResult.isValid())
        result = &m_clusteringResult;
    else {
        QMessageBox::warning(this, QString::fromUtf8("\u63D0\u793A"),
                             QString::fromUtf8("\u8BF7\u5148\u8FD0\u884C\u5206\u7C7B\u6216\u805A\u7C7B\u5206\u6790"));
        return;
    }

    QString filePath = QFileDialog::getOpenFileName(this,
        QString::fromUtf8("\u52A0\u8F7D\u53C2\u8003\u6570\u636E"),
        QString(),
        QString::fromUtf8("CSV (*.csv);;All (*.*)"));

    std::vector<QPoint> refPoints;
    std::vector<int> refLabels;

    if (!filePath.isEmpty()) {
        QFile file(filePath);
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream in(&file);
            QString header = in.readLine();
            while (!in.atEnd()) {
                QString line = in.readLine().trimmed();
                if (line.isEmpty()) continue;
                QStringList parts = line.split(',');
                if (parts.size() >= 3) {
                    int x = parts[0].toInt();
                    int y = parts[1].toInt();
                    int label = parts[2].toInt();
                    if (x >= 0 && x < result->width && y >= 0 && y < result->height) {
                        refPoints.push_back(QPoint(x, y));
                        refLabels.push_back(label);
                    }
                }
            }
            file.close();
        }
    }

    if (refPoints.empty()) {
        statusBar()->showMessage(QString::fromUtf8("\u672A\u52A0\u8F7D\u53C2\u8003\u6570\u636E\uFF0C\u5C06\u4F7F\u7528\u5206\u7C7B\u7ED3\u679C\u81EA\u8EAB\u8FDB\u884C\u7EDF\u8BA1\u3002"));
    }

    m_accuracyMetrics = m_accuracyAssessment->computeConfusionMatrix(
        *result, refPoints, refLabels, result->classNames);

    updateAccuracyDisplay(m_accuracyMetrics);
    updateAccuracyMetricsDisplay(m_accuracyMetrics);
    updateConfusionMatrixTable(m_accuracyMetrics);
    ui.mainTabWidget->setCurrentIndex(3);  // 切换到对比分析页

    addLog(QString::fromUtf8("\u7CBE\u5EA6\u8BC4\u4F30\u5B8C\u6210 - OA: %1%, Kappa: %2")
               .arg(m_accuracyMetrics.overallAccuracy * 100, 0, 'f', 2)
               .arg(m_accuracyMetrics.kappaCoefficient, 0, 'f', 4));
    updateWorkflowStep(4, true);
}

void FeatureExtraction4::onShowComparison()
{
    if (!m_classificationResult.isValid() && !m_clusteringResult.isValid()) {
        QMessageBox::warning(this, QString::fromUtf8("\u63D0\u793A"),
                             QString::fromUtf8("\u8BF7\u5148\u8FD0\u884C\u5206\u7C7B\u6216\u805A\u7C7B\u5206\u6790"));
        return;
    }

    // 分类结果A = 聚类分析结果，分类结果B = 地物分类结果
    m_comparisonZoomLevel = 1.0;  // 重置缩放级别
    m_comparisonPanOffset = QPoint(0, 0);  // 重置平移偏移
    if (m_clusteringResult.isValid()) {
        m_cachedClusteringImage = m_clusteringResult.toThumbnailImage(4096);
        displayImage(m_cachedClusteringImage, ui.compareLeftLabel);
        ui.compareLeftLabel->setToolTip(
            QString::fromUtf8("\u5206\u7C7B\u7ED3\u679CA: \u805A\u7C7B\u5206\u6790 - %1")
                .arg(m_clusteringResult.methodName));
    }

    if (m_classificationResult.isValid()) {
        m_cachedClassificationImage = m_classificationResult.toThumbnailImage(4096);
        displayImage(m_cachedClassificationImage, ui.compareRightLabel);
        ui.compareRightLabel->setToolTip(
            QString::fromUtf8("\u5206\u7C7B\u7ED3\u679CB: \u5730\u7269\u5206\u7C7B - %1")
                .arg(m_classificationResult.methodName));
    }

    ui.mainTabWidget->setCurrentIndex(3);
}

void FeatureExtraction4::onAbout()
{
    QMessageBox::about(this, QString::fromUtf8("\u5173\u4E8E"),
        QString::fromUtf8(
            "<h3>\u878D\u5408\u5F71\u50CF\u5730\u7269\u63D0\u53D6\u7CFB\u7EDF v1.0</h3>"
            "<p>\u57FA\u4E8E\u878D\u5408\u536B\u661F\u5F71\u50CF\u7684\u5730\u7269\u63D0\u53D6\u4E0E\u5206\u7C7B\u7CFB\u7EDF</p>"
            "<p><b>\u4E3B\u8981\u529F\u80FD:</b></p>"
            "<ul>"
            "<li>\u591A\u6CE2\u6BB5\u536B\u661F\u5F71\u50CF\u52A0\u8F7D\u4E0E\u663E\u793A</li>"
            "<li>K-Means / ISODATA \u805A\u7C7B\u5206\u6790</li>"
            "<li>\u6700\u5C0F\u8DDD\u79BB / \u6700\u5927\u4F3C\u7136 / SVM / \u968F\u673A\u68EE\u6797 \u5206\u7C7B\u5668</li>"
            "<li>\u5149\u8C31\u6307\u6570\u8BA1\u7B97 (NDVI, NDWI, NDBI, MNDWI)</li>"
            "<li>\u5206\u7C7B\u7ED3\u679C\u53EF\u89C6\u5316\u4E0E\u5BF9\u6BD4\u5206\u6790</li>"
            "<li>\u7CBE\u5EA6\u8BC4\u4F30 (\u6DF7\u6DC6\u77E9\u9635\u3001Kappa\u3001OA/UA/PA)</li>"
            "<li>\u591A\u683C\u5F0F\u5BFC\u51FA (GeoTIFF, Shapefile, CSV, ENVI)</li>"
            "</ul>"
            "<p><b>\u652F\u6301\u7684\u5730\u7269\u7C7B\u578B:</b> \u6C34\u4F53\u3001\u9053\u8DEF\u3001\u5EFA\u7B51\u3001\u690D\u88AB\u7B49</p>"));
}


void FeatureExtraction4::onAddClass()
{
    int row = ui.classDefTable->rowCount();
    ui.classDefTable->insertRow(row);

    QTableWidgetItem* seqItem = new QTableWidgetItem(QString::number(row + 1));
    seqItem->setTextAlignment(Qt::AlignCenter);
    seqItem->setFlags(seqItem->flags() & ~Qt::ItemIsEditable);
    ui.classDefTable->setItem(row, 0, seqItem);

    QTableWidgetItem* nameItem = new QTableWidgetItem(
        QString::fromUtf8("\u65B0\u7C7B\u522B_") + QString::number(row + 1));
    nameItem->setFlags(nameItem->flags() & ~Qt::ItemIsEditable);
    ui.classDefTable->setItem(row, 1, nameItem);

    int hue = (row * 137) % 360;
    QTableWidgetItem* colorItem = new QTableWidgetItem();
    colorItem->setBackground(QColor::fromHsv(hue, 200, 255));
    colorItem->setFlags(colorItem->flags() & ~Qt::ItemIsEditable);
    ui.classDefTable->setItem(row, 2, colorItem);

    addLog(QString::fromUtf8("\u6DFB\u52A0\u5730\u7269\u7C7B\u522B: %1").arg(nameItem->text()));
}

void FeatureExtraction4::onRemoveClass()
{
    int row = ui.classDefTable->currentRow();
    if (row < 0) {
        QMessageBox::information(this, QString::fromUtf8("\u63D0\u793A"),
                                 QString::fromUtf8("\u8BF7\u5148\u9009\u62E9\u8981\u5220\u9664\u7684\u5730\u7269\u7C7B\u522B\u3002"));
        return;
    }
    if (ui.classDefTable->rowCount() <= 1) {
        QMessageBox::warning(this, QString::fromUtf8("\u63D0\u793A"),
                             QString::fromUtf8("\u81F3\u5C11\u4FDD\u7559\u4E00\u4E2A\u5730\u7269\u7C7B\u522B\u3002"));
        return;
    }

    QTableWidgetItem* nameItem = ui.classDefTable->item(row, 1);
    QString className = nameItem ? nameItem->text() : QString::number(row);
    ui.classDefTable->removeRow(row);
    refreshClassTable();
    addLog(QString::fromUtf8("\u5220\u9664\u5730\u7269\u7C7B\u522B: %1").arg(className));
}

void FeatureExtraction4::onRenameClass()
{
    int row = ui.classDefTable->currentRow();
    if (row < 0) {
        QMessageBox::information(this, QString::fromUtf8("\u63D0\u793A"),
                                 QString::fromUtf8("\u8BF7\u5148\u9009\u62E9\u8981\u91CD\u547D\u540D\u7684\u5730\u7269\u7C7B\u522B\u3002"));
        return;
    }

    QTableWidgetItem* nameItem = ui.classDefTable->item(row, 1);
    if (!nameItem) return;

    bool ok;
    QString newName = QInputDialog::getText(this,
        QString::fromUtf8("\u91CD\u547D\u540D\u5730\u7269\u7C7B\u522B"),
        QString::fromUtf8("\u8BF7\u8F93\u5165\u65B0\u540D\u79F0:"),
        QLineEdit::Normal, nameItem->text(), &ok);

    if (ok && !newName.trimmed().isEmpty()) {
        QString oldName = nameItem->text();
        nameItem->setText(newName.trimmed());
        addLog(QString::fromUtf8("\u91CD\u547D\u540D: %1 \u2192 %2").arg(oldName, newName.trimmed()));
    }
}

void FeatureExtraction4::onClassColorChanged()
{
    int row = ui.classDefTable->currentRow();
    if (row < 0) return;

    QTableWidgetItem* colorItem = ui.classDefTable->item(row, 2);
    if (!colorItem) return;

    QColor currentColor = colorItem->background().color();
    QColor newColor = QColorDialog::getColor(currentColor, this,
        QString::fromUtf8("\u9009\u62E9\u7C7B\u522B\u989C\u8272"));
    if (newColor.isValid()) {
        colorItem->setBackground(newColor);
    }
}

void FeatureExtraction4::onClassTableSelectionChanged()
{
    // 当用户选中一行时，如果在手动采样模式下，更新当前采样类别
    if (m_manualSamplingActive) {
        int row = ui.classDefTable->currentRow();
        if (row >= 0) {
            m_currentSampleClass = row;
            QTableWidgetItem* nameItem = ui.classDefTable->item(row, 1);
            QString className = nameItem ? nameItem->text() : QString::number(row);
            statusBar()->showMessage(QString::fromUtf8("\u5F53\u524D\u91C7\u96C6\u7C7B\u522B: %1 (\u5DF2\u91C7\u96C6 %2 \u4E2A\u6837\u672C)")
                                         .arg(className)
                                         .arg(m_manualSamplePoints.size()));
        }
    }
}

void FeatureExtraction4::onSampleModeChanged()
{
    bool isAuto = ui.autoSampleRadioBtn->isChecked();
    ui.manualSampleBtn->setEnabled(!isAuto);
    ui.autoGenerateBtn->setEnabled(isAuto);
    ui.autoSampleMethodCombo->setEnabled(isAuto);
    ui.samplesPerClassSpinBox->setEnabled(isAuto);
}

void FeatureExtraction4::onAutoSampleMethodChanged()
{
    // 自动样本方法切换时，可在此处更新UI提示
}

void FeatureExtraction4::onAutoGenerateSamples()
{
    if (!m_image.isValid()) {
        QMessageBox::warning(this, QString::fromUtf8("\u63D0\u793A"),
                             QString::fromUtf8("\u8BF7\u5148\u52A0\u8F7D\u5F71\u50CF"));
        return;
    }

    int classCount = ui.classDefTable->rowCount();
    if (classCount == 0) {
        QMessageBox::warning(this, QString::fromUtf8("\u63D0\u793A"),
                             QString::fromUtf8("\u8BF7\u5148\u6DFB\u52A0\u5730\u7269\u7C7B\u522B"));
        return;
    }

    int samplesPerClass = ui.samplesPerClassSpinBox->value();
    int method = ui.autoSampleMethodCombo->currentIndex();

    std::vector<QString> classNames;
    std::vector<QPoint> positions;
    std::vector<int> labels;

    for (int row = 0; row < classCount; ++row) {
        QTableWidgetItem* nameItem = ui.classDefTable->item(row, 1);
        if (nameItem)
            classNames.push_back(nameItem->text());
    }

    for (int ci = 0; ci < classCount; ++ci) {
        std::vector<QPoint> classSamples;
        if (method == 0) {
            classSamples = generateGridSamples(ci, samplesPerClass);
        } else {
            classSamples = generateRandomSamples(ci, samplesPerClass);
        }
        for (const auto& pt : classSamples) {
            positions.push_back(pt);
            labels.push_back(ci);
        }
    }

    std::vector<int> bandIndices;
    for (int b = 0; b < m_image.bands; ++b)
        bandIndices.push_back(b);

    m_trainingData = m_classificationEngine->createTrainingData(
        m_image, positions, labels, classNames, bandIndices);

    addLog(QString::fromUtf8("\u81EA\u52A8\u751F\u6210\u6837\u672C\u5B8C\u6210: %1 \u7C7B x %2 \u4E2A = %3 \u4E2A\u6837\u672C")
               .arg(classCount)
               .arg(samplesPerClass)
               .arg(m_trainingData.sampleCount()));

    statusBar()->showMessage(QString::fromUtf8("\u5DF2\u81EA\u52A8\u751F\u6210 %1 \u4E2A\u8BAD\u7EC3\u6837\u672C")
                                 .arg(m_trainingData.sampleCount()));
}

std::vector<QPoint> FeatureExtraction4::generateGridSamples(int classIndex, int count)
{
    std::vector<QPoint> samples;
    if (!m_image.isValid() || count <= 0) return samples;

    // 均匀网格分布，按类别偏移
    int cols = static_cast<int>(std::ceil(std::sqrt(static_cast<double>(count))));
    int rows = static_cast<int>(std::ceil(static_cast<double>(count) / cols));

    int totalClasses = ui.classDefTable->rowCount();
    double classOffsetX = (classIndex % 3) * 0.1 * m_image.width;
    double classOffsetY = (classIndex / 3) * 0.1 * m_image.height;

    for (int i = 0; i < count; ++i) {
        int c = i % cols;
        int r = i / cols;
        int x = static_cast<int>(classOffsetX + (c + 1) * (m_image.width - classOffsetX) / (cols + 1));
        int y = static_cast<int>(classOffsetY + (r + 1) * (m_image.height - classOffsetY) / (rows + 1));
        x = std::min(std::max(x, 0), m_image.width - 1);
        y = std::min(std::max(y, 0), m_image.height - 1);
        samples.push_back(QPoint(x, y));
    }
    return samples;
}

std::vector<QPoint> FeatureExtraction4::generateRandomSamples(int classIndex, int count)
{
    std::vector<QPoint> samples;
    if (!m_image.isValid() || count <= 0) return samples;

    std::mt19937 rng(static_cast<unsigned>(std::chrono::steady_clock::now().time_since_epoch().count() + classIndex * 1000));
    std::uniform_int_distribution<int> distX(0, m_image.width - 1);
    std::uniform_int_distribution<int> distY(0, m_image.height - 1);

    for (int i = 0; i < count; ++i) {
        samples.push_back(QPoint(distX(rng), distY(rng)));
    }
    return samples;
}

void FeatureExtraction4::onManualStartSampling()
{
    if (!m_image.isValid()) {
        QMessageBox::warning(this, QString::fromUtf8("\u63D0\u793A"),
                             QString::fromUtf8("\u8BF7\u5148\u52A0\u8F7D\u5F71\u50CF"));
        return;
    }

    // 切换采样模式（按钮用作开关）
    if (m_manualSamplingActive) {
        // 结束采样模式
        m_manualSamplingActive = false;
        ui.manualSampleBtn->setText(QString::fromUtf8("\u5F00\u59CB\u624B\u52A8\u91C7\u6837"));
        ui.manualSampleBtn->setStyleSheet("");

        if (m_manualSamplePoints.empty()) {
            QMessageBox::information(this, QString::fromUtf8("\u63D0\u793A"),
                                     QString::fromUtf8("\u672A\u91C7\u96C6\u4EFB\u4F55\u6837\u672C\u3002"));
            return;
        }

        std::vector<QString> classNames;
        for (int row = 0; row < ui.classDefTable->rowCount(); ++row) {
            QTableWidgetItem* nameItem = ui.classDefTable->item(row, 1);
            if (nameItem)
                classNames.push_back(nameItem->text());
        }

        std::vector<int> bandIndices;
        for (int b = 0; b < m_image.bands; ++b)
            bandIndices.push_back(b);

        m_trainingData = m_classificationEngine->createTrainingData(
            m_image, m_manualSamplePoints, m_manualSampleLabels, classNames, bandIndices);

        addLog(QString::fromUtf8("\u624B\u52A8\u91C7\u96C6\u5B8C\u6210: %1 \u4E2A\u8BAD\u7EC3\u6837\u672C")
                   .arg(m_trainingData.sampleCount()));

        statusBar()->showMessage(QString::fromUtf8("\u5DF2\u91C7\u96C6 %1 \u4E2A\u8BAD\u7EC3\u6837\u672C")
                                     .arg(m_trainingData.sampleCount()));
    } else {
        // 进入采样模式
        int classCount = ui.classDefTable->rowCount();
        if (classCount == 0) {
            QMessageBox::warning(this, QString::fromUtf8("\u63D0\u793A"),
                                 QString::fromUtf8("\u8BF7\u5148\u6DFB\u52A0\u5730\u7269\u7C7B\u522B"));
            return;
        }

        m_manualSamplePoints.clear();
        m_manualSampleLabels.clear();
        m_manualSamplingActive = true;

        // 默认选中第一个类别
        if (ui.classDefTable->currentRow() < 0)
            ui.classDefTable->selectRow(0);
        m_currentSampleClass = ui.classDefTable->currentRow();

        ui.mainTabWidget->setCurrentIndex(0);

        // 高亮按钮提示当前状态
        ui.manualSampleBtn->setText(QString::fromUtf8("\u7ED3\u675F\u91C7\u6837"));
        ui.manualSampleBtn->setStyleSheet("background-color: #e74c3c; color: white;");

        QMessageBox::information(this, QString::fromUtf8("\u624B\u52A8\u91C7\u96C6"),
            QString::fromUtf8("\u8BF7\u5728\u5730\u7269\u7C7B\u522B\u8868\u4E2D\u9009\u62E9\u5F53\u524D\u7C7B\u522B\uFF0C\u7136\u540E\u5728\u5F71\u50CF\u4E0A\u70B9\u51FB\u91C7\u96C6\u6837\u672C\u70B9\u3002\n\n"
                              "\u5207\u6362\u7C7B\u522B\u884C\u5373\u53EF\u66F4\u6539\u5F53\u524D\u91C7\u6837\u7C7B\u522B\u3002\n"
                              "\u70B9\u51FB\u300C\u7ED3\u675F\u91C7\u6837\u300D\u6309\u94AE\u5B8C\u6210\u91C7\u96C6\u3002"));

        QTableWidgetItem* nameItem = ui.classDefTable->item(m_currentSampleClass, 1);
        QString className = nameItem ? nameItem->text() : QString::fromUtf8("\u672A\u77E5");
        statusBar()->showMessage(QString::fromUtf8("\u5F53\u524D\u91C7\u96C6\u7C7B\u522B: %1 (\u5DF2\u91C7\u96C6 0 \u4E2A\u6837\u672C)")
                                     .arg(className));
    }
}

void FeatureExtraction4::onManualSampleCollected()
{
    // 此方法在 eventFilter 中的鼠标点击事件处理中调用，用于记录手动采样点
}

void FeatureExtraction4::applyClassifierConfig()
{
    ClassifierConfig config;
    config.enableMajorityFilter = true;   // 默认启用多数投票滤波
    config.filterWindowSize = 5;           // 5x5窗口，消除椒盐噪声
    config.minRegionSize = 20;             // 合并小于20像素的孤立区域
    m_classificationEngine->setConfig(config);
}

void FeatureExtraction4::collectTrainingSamples()
{
    // 如果已有训练数据，直接使用
    if (m_trainingData.isValid()) return;

    // 否则提示用户先选择样本
    QMessageBox::information(this, QString::fromUtf8("\u63D0\u793A"),
                             QString::fromUtf8(
                                 "\u8BF7\u5148\u9009\u62E9\u8BAD\u7EC3\u6837\u672C\uFF1A\n\n"
                                 "\u2022 \u624B\u52A8\u6A21\u5F0F\uFF1A\u70B9\u51FB\u300C\u5F00\u59CB\u624B\u52A8\u91C7\u6837\u300D\uFF0C\u5728\u5F71\u50CF\u4E0A\u70B9\u51FB\u91C7\u96C6\n"
                                 "\u2022 \u81EA\u52A8\u6A21\u5F0F\uFF1A\u5207\u6362\u5230\u81EA\u52A8\u6A21\u5F0F\uFF0C\u8BBE\u7F6E\u53C2\u6570\u540E\u70B9\u51FB\u300C\u81EA\u52A8\u751F\u6210\u6837\u672C\u300D"));
}

void FeatureExtraction4::onClassificationModeChanged()
{
    bool isSupervised = ui.supervisedRadioBtn->isChecked();
    ui.sampleSelectionGroup->setVisible(isSupervised);
    ui.classManagementGroup->setVisible(isSupervised);
    ui.actionRunClassification->setEnabled(true);
}

void FeatureExtraction4::onClusterAlgoChanged(int index)
{
    bool isISODATA = (index == 1);
    ui.isodataMinSizeLabel->setVisible(isISODATA);
    ui.isodataMinSizeSpin->setVisible(isISODATA);
    ui.isodataStdDevLabel->setVisible(isISODATA);
    ui.isodataStdDevSpin->setVisible(isISODATA);
    ui.isodataMinDistLabel->setVisible(isISODATA);
    ui.isodataMinDistSpin->setVisible(isISODATA);
    ui.isodataMaxMergeLabel->setVisible(isISODATA);
    ui.isodataMaxMergeSpin->setVisible(isISODATA);
    ui.clusterCountLabel->setVisible(!isISODATA);
    ui.clusterCountSpinBox->setVisible(!isISODATA);
    ui.clusterCountNoteLabel->setVisible(!isISODATA);
}

void FeatureExtraction4::onImageClicked(QPoint pos)
{
    if (!m_image.isValid()) return;

    int x = pos.x();
    int y = pos.y();

    if (x < 0 || x >= m_image.width || y < 0 || y >= m_image.height)
        return;

    QString info = QString::fromUtf8("\u5750\u6807: (%1, %2)").arg(x).arg(y);
    for (int b = 0; b < m_image.bands; ++b) {
        info += QString::fromUtf8("  B%1: %2").arg(b + 1).arg(m_image.pixelValue(b, x, y), 0, 'f', 2);
    }
    statusBar()->showMessage(info);
}

void FeatureExtraction4::onClusteringProgress(int percent)
{
    updateProgress(percent, QString::fromUtf8("\u6B63\u5728\u8FDB\u884C\u805A\u7C7B\u5206\u6790..."));
}

void FeatureExtraction4::onClassificationProgress(int percent)
{
    updateProgress(percent, QString::fromUtf8("\u6B63\u5728\u8FDB\u884C\u5730\u7269\u5206\u7C7B..."));
}

void FeatureExtraction4::onExportProgress(int percent)
{
    updateProgress(percent, QString::fromUtf8("\u6B63\u5728\u5BFC\u51FA\u7ED3\u679C..."));
}

void FeatureExtraction4::onStatusMessage(const QString& msg)
{
    statusBar()->showMessage(msg);
    ui.statusProgressLabel->setText(msg);
}

void FeatureExtraction4::onExportFinished(const QString& filePath)
{
    addLog(QString::fromUtf8("\u5BFC\u51FA\u5B8C\u6210: %1").arg(filePath));
    updateWorkflowStep(4, true);
    statusBar()->showMessage(QString::fromUtf8("\u5BFC\u51FA\u5B8C\u6210: ") + filePath);
    QMessageBox::information(this, QString::fromUtf8("\u5BFC\u51FA\u5B8C\u6210"),
                             QString::fromUtf8("\u6587\u4EF6\u5DF2\u4FDD\u5B58\u5230:\n") + filePath);
}

void FeatureExtraction4::onExportError(const QString& errorMsg)
{
    addLog(QString::fromUtf8("\u5BFC\u51FA\u5931\u8D25: %1").arg(errorMsg));
    QMessageBox::warning(this, QString::fromUtf8("\u5BFC\u51FA\u9519\u8BEF"), errorMsg);
}

void FeatureExtraction4::displayImage(const QImage& img, QLabel* label)
{
    if (img.isNull()) return;

    // 按标签分别缓存，杜绝交叉覆盖
    if (label == ui.imageDisplayLabel) {
        m_cachedOriginalImage = img;
    } else if (label == ui.clusterDisplayLabel || label == ui.compareLeftLabel) {
        m_cachedClusteringImage = img;
    } else if (label == ui.classDisplayLabel || label == ui.compareRightLabel) {
        m_cachedClassificationImage = img;
    }

    // 原始影像标签：缩放平移显示
    if (label == ui.imageDisplayLabel || label == ui.clusterDisplayLabel ||
        label == ui.classDisplayLabel) {
        applyZoomPanTransform();
        return;
    }

    // 对比面板标签：支持独立鼠标缩放，统一尺寸，支持拖拽平移
    if (label == ui.compareLeftLabel || label == ui.compareRightLabel) {
        // 统一使用左右两侧中较小的可用空间，确保两图尺寸一致
        int parentW = label->parentWidget() ? label->parentWidget()->width() - 8 : 400;
        int parentH = label->parentWidget() ? label->parentWidget()->height() - 8 : 400;
        int maxW = qMax(1, parentW / 2);  // 每个标签占一半宽度
        int maxH = qMax(1, parentH);
        m_comparisonZoomLevel = qBound(0.1, m_comparisonZoomLevel, 10.0);
        int targetW = qMax(1, static_cast<int>(maxW * m_comparisonZoomLevel));
        int targetH = qMax(1, static_cast<int>(maxH * m_comparisonZoomLevel));
        // 先缩放至统一目标尺寸
        QImage scaled = img.scaled(targetW, targetH, Qt::KeepAspectRatio, Qt::SmoothTransformation);

        // 应用拖拽平移偏移，在标签中心偏移基础上叠加
        int offsetX = (label->width() - scaled.width()) / 2 + m_comparisonPanOffset.x();
        int offsetY = (label->height() - scaled.height()) / 2 + m_comparisonPanOffset.y();
        QPixmap canvas(label->size());
        canvas.fill(Qt::transparent);
        QPainter painter(&canvas);
        painter.drawImage(offsetX, offsetY, scaled);
        painter.end();
        label->setPixmap(canvas);
        label->setMinimumSize(1, 1);
        return;
    }

    // 结果预览标签：直接做适配缩放
    int maxW = qMax(1, label->parentWidget() ? label->parentWidget()->width() - 8 : 780);
    int maxH = qMax(1, label->parentWidget() ? label->parentWidget()->height() - 8 : 580);
    QImage scaled = img.scaled(maxW, maxH, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    label->setPixmap(QPixmap::fromImage(scaled));
    label->setMinimumSize(1, 1);
}

void FeatureExtraction4::applyZoomPanTransform()
{
    QLabel* currentLabel = nullptr;
    int tabIdx = ui.mainTabWidget->currentIndex();
    if (tabIdx == 0) currentLabel = ui.imageDisplayLabel;
    else if (tabIdx == 1) currentLabel = ui.classDisplayLabel;
    else if (tabIdx == 2) currentLabel = ui.clusterDisplayLabel;
    else return;

    // 按标签页选择对应的缓存图像，严格隔离
    const QImage& sourceImage = cachedImageForTab(tabIdx);

    if (sourceImage.isNull()) {
        // 若对应缓存为空，清空标签显示
        currentLabel->clear();
        currentLabel->setText(QString::fromUtf8("无数据"));
        return;
    }

    int maxW = qMax(1, currentLabel->parentWidget() ? currentLabel->parentWidget()->width() - 4 : 800);
    int maxH = qMax(1, currentLabel->parentWidget() ? currentLabel->parentWidget()->height() - 4 : 600);

    m_zoomLevel = qBound(0.1, m_zoomLevel, 10.0);
    int scaledW = qMax(1, static_cast<int>(sourceImage.width() * m_zoomLevel));
    int scaledH = qMax(1, static_cast<int>(sourceImage.height() * m_zoomLevel));

    QImage scaled = sourceImage.scaled(scaledW, scaledH, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    if (scaledW != scaled.width() || scaledH != scaled.height()) {
        scaledW = scaled.width();
        scaledH = scaled.height();
    }

    QImage canvas(maxW, maxH, QImage::Format_ARGB32);
    canvas.fill(Qt::black);
    QPainter painter(&canvas);
    int offsetX = m_panOffset.x() + (maxW - scaledW) / 2;
    int offsetY = m_panOffset.y() + (maxH - scaledH) / 2;
    painter.drawImage(offsetX, offsetY, scaled);

    // 手动采样模式下绘制采样点标记
    if (m_manualSamplingActive && tabIdx == 0 && !m_manualSamplePoints.empty()) {
        painter.setRenderHint(QPainter::Antialiasing);
        for (size_t i = 0; i < m_manualSamplePoints.size(); ++i) {
            int imgX = m_manualSamplePoints[i].x();
            int imgY = m_manualSamplePoints[i].y();
            int labelX = offsetX + imgX * scaledW / sourceImage.width();
            int labelY = offsetY + imgY * scaledH / sourceImage.height();

            int classIdx = m_manualSampleLabels[i];
            QColor color = Qt::red;
            if (classIdx >= 0 && classIdx < ui.classDefTable->rowCount()) {
                QTableWidgetItem* colorItem = ui.classDefTable->item(classIdx, 2);
                if (colorItem) color = colorItem->background().color();
            }

            painter.setPen(QPen(Qt::white, 2));
            painter.drawLine(labelX - 6, labelY, labelX + 6, labelY);
            painter.drawLine(labelX, labelY - 6, labelX, labelY + 6);

            painter.setPen(QPen(color, 3));
            painter.drawPoint(labelX, labelY);
        }
    }

    // 人工判读模式：绘制标注点叠加层
    if (m_isAnnotationMode && (tabIdx == 1 || tabIdx == 2) && !m_annotationSession.records.empty()) {
        painter.setRenderHint(QPainter::Antialiasing);
        for (size_t i = 0; i < m_annotationSession.records.size(); ++i) {
            const auto& record = m_annotationSession.records[i];
            int imgX = record.position.x();
            int imgY = record.position.y();
            int labelX = offsetX + imgX * scaledW / sourceImage.width();
            int labelY = offsetY + imgY * scaledH / sourceImage.height();

            bool isSelected = (static_cast<int>(i) == m_selectedAnnotationIndex);

            // 确定颜色：已修正为红色，已确认为绿色，未确认为橙色
            QColor markerColor;
            if (record.isOverridden())
                markerColor = QColor("#e74c3c");  // 红色 - 已修正
            else if (record.confirmed)
                markerColor = QColor("#27ae60");  // 绿色 - 已确认
            else
                markerColor = QColor("#f39c12");  // 橙色 - 待确认

            // 选中的标注点使用高亮样式
            if (isSelected) {
                // 外圈发光效果
                painter.setPen(Qt::NoPen);
                painter.setBrush(QColor(52, 152, 219, 80));
                painter.drawEllipse(QPoint(labelX, labelY), 12, 12);
                // 外边框
                painter.setBrush(Qt::NoBrush);
                painter.setPen(QPen(QColor("#2980b9"), 2.5));
                painter.drawEllipse(QPoint(labelX, labelY), 9, 9);
                // 内填充
                painter.setBrush(markerColor);
                painter.setPen(QPen(Qt::white, 1));
                painter.drawEllipse(QPoint(labelX, labelY), 5, 5);
            } else {
                // 外圈描边
                painter.setBrush(markerColor);
                painter.setPen(QPen(Qt::white, 1.5));
                painter.drawEllipse(QPoint(labelX, labelY), 5, 5);
            }

            // 显示序号标签
            if (isSelected || record.isOverridden()) {
                painter.setPen(Qt::white);
                painter.setFont(QFont("Arial", 8, QFont::Bold));
                QString idxStr = QString::number(i + 1);
                QRect textRect(labelX - 6, labelY + 7, 12, 10);
                painter.drawText(textRect, Qt::AlignCenter, idxStr);
            }
        }
    }

    painter.end();

    currentLabel->setPixmap(QPixmap::fromImage(canvas));
    currentLabel->setMinimumSize(1, 1);
}

QPoint FeatureExtraction4::mapToImage(const QPoint& labelPos)
{
    QLabel* currentLabel = nullptr;
    int tabIdx = ui.mainTabWidget->currentIndex();
    if (tabIdx == 0) currentLabel = ui.imageDisplayLabel;
    else if (tabIdx == 1) currentLabel = ui.classDisplayLabel;
    else if (tabIdx == 2) currentLabel = ui.clusterDisplayLabel;
    else return QPoint(-1, -1);

    const QImage& sourceImage = cachedImageForTab(tabIdx);

    if (sourceImage.isNull() || !m_image.isValid()) return QPoint(-1, -1);

    int maxW = currentLabel->parentWidget() ? currentLabel->parentWidget()->width() - 4 : 800;
    int maxH = currentLabel->parentWidget() ? currentLabel->parentWidget()->height() - 4 : 600;

    int scaledW = qMax(1, static_cast<int>(sourceImage.width() * m_zoomLevel));
    int scaledH = qMax(1, static_cast<int>(sourceImage.height() * m_zoomLevel));

    int offsetX = m_panOffset.x() + (maxW - scaledW) / 2;
    int offsetY = m_panOffset.y() + (maxH - scaledH) / 2;

    int imgX = (labelPos.x() - offsetX) * sourceImage.width() / scaledW;
    int imgY = (labelPos.y() - offsetY) * sourceImage.height() / scaledH;
    return QPoint(imgX, imgY);
}

const QImage& FeatureExtraction4::cachedImageForTab(int tabIdx) const
{
    switch (tabIdx) {
    case 0: return m_cachedOriginalImage;
    case 1: return m_cachedClassificationImage;
    case 2: return m_cachedClusteringImage;
    default: return m_cachedOriginalImage;
    }
}

void FeatureExtraction4::updateCompareLeftRight()
{
    // 自动同步对比面板：左=聚类结果A，右=分类结果B
    if (!m_cachedClusteringImage.isNull()) {
        displayImage(m_cachedClusteringImage, ui.compareLeftLabel);
    }
    if (!m_cachedClassificationImage.isNull()) {
        displayImage(m_cachedClassificationImage, ui.compareRightLabel);
    }
}

void FeatureExtraction4::displayClassificationResult(const ClassificationResult& result, QLabel* targetLabel)
{
    if (!result.isValid()) return;
    QImage img = result.toThumbnailImage(4096);
    m_zoomLevel = 1.0;
    m_panOffset = QPoint(0, 0);
    displayImage(img, targetLabel);
}

void FeatureExtraction4::displayResultOnCurrentTab(const ClassificationResult& result)
{
    if (!result.isValid()) return;

    QImage img = result.toThumbnailImage(4096);
    m_zoomLevel = 1.0;
    m_comparisonZoomLevel = 1.0;
    m_panOffset = QPoint(0, 0);

    int tabIdx = ui.mainTabWidget->currentIndex();
    if (tabIdx == 0) {
        // 原始影像标签页：不显示分类结果，分类结果已迁移至专用标签页
        return;
    } else if (tabIdx == 1) {
        m_cachedClassificationImage = img;
        displayImage(img, ui.classDisplayLabel);
        updateClassLegend(result);
    } else if (tabIdx == 2) {
        m_cachedClusteringImage = img;
        displayImage(img, ui.clusterDisplayLabel);
        updateClusterLegend(result);
    }

    // 自动更新对比面板（tab 3）
    updateCompareLeftRight();
}

void FeatureExtraction4::updateClassLegend(const ClassificationResult& result)
{
    Q_UNUSED(result);
    // 图例已移除，分类颜色通过地物类别定义表显示
}

void FeatureExtraction4::updateClusterLegend(const ClassificationResult& result)
{
    Q_UNUSED(result);
    // 图例已移除，聚类颜色通过地物类别定义表显示
}

void FeatureExtraction4::updateAccuracyDisplay(const AccuracyMetrics& metrics)
{
    updateAccuracyMetricsDisplay(metrics);
    updateConfusionMatrixTable(metrics);
}

void FeatureExtraction4::updateProgress(int percent, const QString& label)
{
    ui.statusProgressBar->setValue(percent);
    ui.statusProgressBar->setVisible(true);

    if (percent <= 0) {
        m_progressTimer.start();
        m_progressLastPercent = 0;
        m_progressStageName = label;
        ui.statusProgressLabel->setText(label);

        // 创建并显示进程实时显示窗口
        if (!m_progressDialog) {
            m_progressDialog = new QProgressDialog(label, QString::fromUtf8("\u53D6\u6D88"), 0, 100, this);
            m_progressDialog->setWindowTitle(QString::fromUtf8("\u64CD\u4F5C\u8FDB\u5EA6"));
            m_progressDialog->setWindowModality(Qt::WindowModal);
            m_progressDialog->setMinimumDuration(0);  // 立即显示
            m_progressDialog->setAutoClose(false);
            m_progressDialog->setAutoReset(false);
            m_progressDialog->setValue(0);
            m_progressDialog->show();
        }
    } else if (percent >= 100) {
        // 完成时显示总耗时
        qint64 elapsed = m_progressTimer.elapsed();
        double elapsedSec = elapsed / 1000.0;
        QString timeStr;
        if (elapsedSec >= 60.0)
            timeStr = QString::fromUtf8("%1\u5206%2\u79D2").arg(static_cast<int>(elapsedSec / 60)).arg(static_cast<int>(elapsedSec) % 60);
        else
            timeStr = QString::fromUtf8("%1\u79D2").arg(elapsedSec, 0, 'f', 1);
        ui.statusProgressLabel->setText(QString::fromUtf8("\u5B8C\u6210 (\u8017\u65F6 %1)").arg(timeStr));
        updateStatusBarProgress(100, QString::fromUtf8("\u5B8C\u6210 (\u8017\u65F6 %1)").arg(timeStr));

        // 关闭进程实时显示窗口
        if (m_progressDialog) {
            m_progressDialog->setValue(100);
            m_progressDialog->setLabelText(QString::fromUtf8("\u5B8C\u6210 (\u8017\u65F6 %1)").arg(timeStr));
            m_progressDialog->close();
            m_progressDialog->deleteLater();
            m_progressDialog = nullptr;
        }
    } else {
        // 计算预估剩余时间
        qint64 elapsed = m_progressTimer.elapsed();
        if (percent > 0 && percent > m_progressLastPercent) {
            m_progressLastPercent = percent;
            double estimatedTotal = (elapsed / 1000.0) * 100.0 / percent;
            double remaining = estimatedTotal - elapsed / 1000.0;
            QString timeStr;
            if (remaining >= 60.0)
                timeStr = QString::fromUtf8("\u5269\u4F59\u7EA6 %1\u5206%2\u79D2").arg(static_cast<int>(remaining / 60)).arg(static_cast<int>(remaining) % 60);
            else
                timeStr = QString::fromUtf8("\u5269\u4F59\u7EA6 %1\u79D2").arg(static_cast<int>(remaining));
            QString statusText = QString::fromUtf8("%1 | %2% | %3").arg(label).arg(percent).arg(timeStr);
            ui.statusProgressLabel->setText(statusText);
            updateStatusBarProgress(percent, statusText);

            // 更新进程实时显示窗口
            if (m_progressDialog) {
                m_progressDialog->setValue(percent);
                m_progressDialog->setLabelText(statusText);
            }
        } else {
            QString statusText = QString::fromUtf8("%1 | %2%").arg(label).arg(percent);
            ui.statusProgressLabel->setText(statusText);
            updateStatusBarProgress(percent, statusText);

            // 更新进程实时显示窗口
            if (m_progressDialog) {
                m_progressDialog->setValue(percent);
                m_progressDialog->setLabelText(statusText);
            }
        }
    }
}

void FeatureExtraction4::applyClusteringConfig()
{
    ClusteringConfig config;
    config.numClusters = ui.clusterCountSpinBox->value();
    config.maxIterations = ui.maxIterSpinBox->value();
    config.convergenceThreshold = ui.convergeSpinBox->value();
    config.normalizeFeatures = ui.normalizeCheckBox->isChecked();
    config.positionWeight = ui.posWeightSpinBox->value();
    config.usePixelPositions = (config.positionWeight > 0.001);
    // 特征缩放因子：与归一化互斥，仅在未勾选归一化时生效
    // 0=保留原始方差，1=完全归一化，默认0.3做温和缩放
    config.featureScaleFactor = config.normalizeFeatures ? 0.0 : 0.3;
    m_clusteringEngine->setConfig(config);
}

void FeatureExtraction4::applyExportConfig()
{
    ExportConfig config;
    switch (ui.exportFormatCombo->currentIndex()) {
    case 0: config.format = ExportFormat::GeoTIFF; break;
    case 1: config.format = ExportFormat::Shapefile; break;
    case 2: config.format = ExportFormat::CSV; break;
    case 3: config.format = ExportFormat::ENVI_BSQ; break;
    default: config.format = ExportFormat::CSV; break;
    }
    config.createWorldFile = ui.worldFileCheckBox->isChecked();
    m_exportManager->setConfig(config);
}

void FeatureExtraction4::updateStatusBarClassificationInfo()
{
    int totalPixels = m_image.width * m_image.height;
    int processedPixels = 0;

    if (m_classificationResult.isValid()) {
        processedPixels = static_cast<int>(m_classificationResult.labelMap.size());
    } else if (m_clusteringResult.isValid()) {
        processedPixels = static_cast<int>(m_clusteringResult.labelMap.size());
    }

    int classCount = 0;
    if (m_classificationResult.isValid())
        classCount = m_classificationResult.classCount;
    else if (m_clusteringResult.isValid())
        classCount = m_clusteringResult.classCount;

    QString mode = ui.supervisedRadioBtn->isChecked()
        ? QString::fromUtf8("\u76D1\u7763")
        : QString::fromUtf8("\u975E\u76D1\u7763");

    double percent = totalPixels > 0 ? (100.0 * processedPixels / totalPixels) : 0;

    ui.statusClassificationLabel->setText(
        QString::fromUtf8("%1 | %2\u7C7B | %3/%4 (%5%)")
            .arg(mode).arg(classCount).arg(processedPixels).arg(totalPixels)
            .arg(percent, 0, 'f', 1));
}

void FeatureExtraction4::updateStatusBarImageInfo()
{
    if (m_image.isValid()) {
        QString sizeStr;
        if (m_image.fileSizeBytes >= 1024 * 1024)
            sizeStr = QString::number(m_image.fileSizeBytes / (1024.0 * 1024), 'f', 1) + "MB";
        else
            sizeStr = QString::number(m_image.fileSizeBytes / 1024.0, 'f', 1) + "KB";

        ui.statusImageInfoLabel->setText(
            QString::fromUtf8("%1 | %2x%3 | %4\u6CE2\u6BB5 | %5")
                .arg(m_image.imageFormat)
                .arg(m_image.width).arg(m_image.height)
                .arg(m_image.bands).arg(sizeStr));
    } else {
        ui.statusImageInfoLabel->setText(QString::fromUtf8("\u65E0\u5F71\u50CF"));
    }
}

void FeatureExtraction4::updateStatusBarProgress(int percent, const QString& status)
{
    ui.statusProgressLabel->setText(status);
    ui.statusProgressBar->setVisible(true);
    ui.statusProgressBar->setValue(percent);
}

void FeatureExtraction4::addLog(const QString& message)
{
    QString timestamp = QDateTime::currentDateTime().toString("[yyyy-MM-dd HH:mm:ss]");
    ui.logTextEdit->append(timestamp + " " + message);
    QTextCursor cursor = ui.logTextEdit->textCursor();
    cursor.movePosition(QTextCursor::End);
    ui.logTextEdit->setTextCursor(cursor);
}

void FeatureExtraction4::updateWorkflowStep(int step, bool completed)
{
    QLabel* statusLabels[] = {ui.step1Status, ui.step2Status, ui.step3Status, ui.step4Status};
    if (step >= 1 && step <= 4) {
        if (completed) {
            statusLabels[step-1]->setText("✓");
            statusLabels[step-1]->setStyleSheet("font-size: 14px; color: #4CAF50;");
        } else {
            statusLabels[step-1]->setText("○");
            statusLabels[step-1]->setStyleSheet("font-size: 14px; color: #9E9E9E;");
        }
    }
}

void FeatureExtraction4::updateAccuracyMetricsDisplay(const AccuracyMetrics& metrics)
{
    ui.oaValueEdit->setText(QString::number(metrics.overallAccuracy * 100, 'f', 2) + "%");
    ui.kappaValueEdit->setText(QString::number(metrics.kappaCoefficient, 'f', 4) +
                               QString::fromUtf8("  (F1: %1%)").arg(metrics.macroF1 * 100, 0, 'f', 2));

    // 平均 PA（生产者精度）
    if (!metrics.producerAccuracy.empty()) {
        double avgPA = 0;
        for (double v : metrics.producerAccuracy) avgPA += v;
        avgPA /= metrics.producerAccuracy.size();
        ui.paValueEdit->setText(QString::number(avgPA * 100, 'f', 2) + "%");
    } else {
        ui.paValueEdit->setText(QString::fromUtf8("N/A"));
    }

    // 平均 UA（用户精度）
    if (!metrics.userAccuracy.empty()) {
        double avgUA = 0;
        for (double v : metrics.userAccuracy) avgUA += v;
        avgUA /= metrics.userAccuracy.size();
        ui.uaValueEdit->setText(QString::number(avgUA * 100, 'f', 2) + "%");
    } else {
        ui.uaValueEdit->setText(QString::fromUtf8("N/A"));
    }
}

void FeatureExtraction4::updateConfusionMatrixTable(const AccuracyMetrics& metrics)
{
    int n = static_cast<int>(metrics.confusionMatrix.size());
    ui.confusionMatrixTable->setRowCount(n);
    ui.confusionMatrixTable->setColumnCount(n + 2);  // +总计 +F1

    QStringList headers;
    for (int i = 0; i < n; ++i)
        headers << QString::fromUtf8("\u7C7B\u522B %1").arg(i + 1);
    headers << QString::fromUtf8("\u603B\u8BA1");
    headers << "F1";
    ui.confusionMatrixTable->setHorizontalHeaderLabels(headers);

    for (int i = 0; i < n; ++i) {
        int rowSum = 0;
        for (int j = 0; j < n; ++j) {
            int val = metrics.confusionMatrix[i][j];
            rowSum += val;
            QTableWidgetItem* item = new QTableWidgetItem(QString::number(val));
            item->setTextAlignment(Qt::AlignCenter);
            if (i == j) item->setBackground(QColor(200, 255, 200));
            ui.confusionMatrixTable->setItem(i, j, item);
        }
        QTableWidgetItem* sumItem = new QTableWidgetItem(QString::number(rowSum));
        sumItem->setTextAlignment(Qt::AlignCenter);
        sumItem->setBackground(QColor(220, 220, 220));
        ui.confusionMatrixTable->setItem(i, n, sumItem);

        // F1 列
        QString f1Text = (i < static_cast<int>(metrics.f1Scores.size()))
            ? QString::number(metrics.f1Scores[i] * 100, 'f', 1) + "%"
            : "-";
        QTableWidgetItem* f1Item = new QTableWidgetItem(f1Text);
        f1Item->setTextAlignment(Qt::AlignCenter);
        double f1Val = (i < static_cast<int>(metrics.f1Scores.size())) ? metrics.f1Scores[i] : 0;
        if (f1Val >= 0.85) f1Item->setBackground(QColor(200, 255, 200));
        else if (f1Val >= 0.70) f1Item->setBackground(QColor(255, 255, 200));
        else if (f1Val > 0) f1Item->setBackground(QColor(255, 220, 220));
        ui.confusionMatrixTable->setItem(i, n + 1, f1Item);
    }
}

// ========== 地物提取 ==========

void FeatureExtraction4::onExtractLandCover()
{
    if (!m_image.isValid()) {
        QMessageBox::warning(this, QString::fromUtf8("\u63D0\u793A"),
                             QString::fromUtf8("\u8BF7\u5148\u52A0\u8F7D\u5F71\u50CF"));
        return;
    }

    updateProgress(0, QString::fromUtf8("\u6B63\u5728\u63D0\u53D6\u5730\u7269..."));

    // 读取提取参数（基于影像自适应推荐 + 用户微调）
    ExtractionParams params = LandCoverExtractor::autoParameters(m_image);

    // 用户UI覆盖（仅当与默认值不同时覆盖自动推荐值）
    if (ui.waterCheckBox->isChecked()) {
        double uiVal = ui.ndwiThresholdSpin->value();
        if (std::abs(uiVal) > 1e-6) params.ndwiThreshold = uiVal;
    }
    if (ui.buildingCheckBox->isChecked()) {
        double uiVal = ui.ndbiThresholdSpin->value();
        if (std::abs(uiVal - 0.1) > 1e-6) params.ndbiThreshold = uiVal;
    }
    if (ui.ndviThresholdSpin->value() != 0.3)
        params.ndviThreshold = ui.ndviThresholdSpin->value();
    if (ui.roadBrightSpin->value() != 0.4)
        params.roadBrightnessThreshold = ui.roadBrightSpin->value();
    params.minRegionArea = ui.minAreaSpinBox->value();
    params.useMorphology = ui.morphologyCheckBox->isChecked();
    params.morphologyKernelSize = ui.morphKernelSpin->value();

    m_landCoverExtractor->setParams(params);

    // 收集用户选择的地物类型（裸土和阴影已永久移除）
    std::vector<LandCoverType> types;
    if (ui.waterCheckBox->isChecked())     types.push_back(LandCoverType::Water);
    if (ui.vegetationCheckBox->isChecked()) types.push_back(LandCoverType::Vegetation);
    if (ui.buildingCheckBox->isChecked())   types.push_back(LandCoverType::Building);
    if (ui.roadCheckBox->isChecked())       types.push_back(LandCoverType::Road);

    if (types.empty()) {
        QMessageBox::warning(this, QString::fromUtf8("\u63D0\u793A"),
                             QString::fromUtf8("\u8BF7\u9009\u62E9\u81F3\u5C11\u4E00\u79CD\u5730\u7269\u7C7B\u578B"));
        return;
    }

    QApplication::processEvents();

    // 优先使用分类结果，否则使用原始影像
    if (m_classificationResult.isValid()) {
        m_extractionResult = m_landCoverExtractor->extractFromClassification(m_classificationResult, types);
    } else {
        m_extractionResult = m_landCoverExtractor->extractFromImage(m_image, types);
    }

    displayExtractionResult();

    // 生成报告
    QString report = m_extractionResult.toSummaryReport();
    addLog(report);

    updateProgress(100, QString::fromUtf8("\u5730\u7269\u63D0\u53D6\u5B8C\u6210"));
    statusBar()->showMessage(QString::fromUtf8("\u5730\u7269\u63D0\u53D6\u5B8C\u6210 - \u5171\u63D0\u53D6 %1 \u79CD\u5730\u7269\u7C7B\u578B")
                                 .arg(m_extractionResult.results.size()));
}

void FeatureExtraction4::onExtractionProgress(int percent)
{
    updateProgress(percent, QString::fromUtf8("\u5730\u7269\u63D0\u53D6\u4E2D..."));
}

void FeatureExtraction4::displayExtractionResult()
{
    if (!m_extractionResult.isValid()) return;

    QImage combined = m_extractionResult.toCombinedImage();
    m_cachedExtractionImage = combined;

    // 应用缩放和平移到地物提取标签页
    if (!combined.isNull() && m_extractionZoomLevel != 1.0) {
        int newW = qMax(1, qRound(combined.width() * m_extractionZoomLevel));
        int newH = qMax(1, qRound(combined.height() * m_extractionZoomLevel));
        QImage scaled = combined.scaled(newW, newH, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        // 如果缩放后小于原图，先填充空白背景再绘制偏移的图片
        if (m_extractionPanOffset.isNull()) {
            displayImage(scaled, m_extractionDisplayLabel);
        } else {
            QImage canvas(newW, newH, QImage::Format_ARGB32);
            canvas.fill(Qt::transparent);
            QPainter painter(&canvas);
            painter.drawImage(m_extractionPanOffset, scaled);
            painter.end();
            displayImage(canvas, m_extractionDisplayLabel);
        }
    } else {
        displayImage(combined, m_extractionDisplayLabel);
    }

    // 缩放时也更新图例
    updateExtractionLegend();

    // 切换到地物提取标签页
    ui.mainTabWidget->setCurrentIndex(ui.mainTabWidget->indexOf(m_extractionViewTab));

    // 启用导出按钮
    m_extractionExportBtn->setEnabled(true);
}

void FeatureExtraction4::updateExtractionLegend()
{
    // 在分类结果标签页显示地物提取图例
    QString legendHtml = QString::fromUtf8(
        "<html><body style='font-family:Microsoft YaHei; font-size:12px; padding:8px;'>"
        "<h3 style='margin:4px 0; color:#2c3e50;'>\u5730\u7269\u63D0\u53D6\u7ED3\u679C</h3>"
        "<table cellspacing='4'>");

    for (const auto& r : m_extractionResult.results) {
        QColor c = r.displayColor;
        legendHtml += QString::fromUtf8(
            "<tr><td style='width:20px; height:16px; background-color:%1; border:1px solid #999;'></td>"
            "<td style='padding-left:6px;'>%2</td>"
            "<td style='padding-left:12px; color:#666;'>%3 \u50CF\u7D20 (%4%)</td></tr>")
            .arg(c.name())
            .arg(r.typeName)
            .arg(r.pixelCount)
            .arg(r.coveragePercent, 0, 'f', 1);
    }

    legendHtml += QString::fromUtf8("</table></body></html>");

    // 使用信息面板的日志区域显示图例
    addLog(QString::fromUtf8("\u5730\u7269\u63D0\u53D6\u56FE\u4F8B\u5DF2\u751F\u6210"));
    m_extractionDisplayLabel->setToolTip(legendHtml);
}

// ========== 地物提取图像导出 ==========

void FeatureExtraction4::onExtractionExport()
{
    if (!m_extractionResult.isValid() || m_cachedExtractionImage.isNull()) {
        QMessageBox::warning(this, QString::fromUtf8("\u63D0\u793A"),
                             QString::fromUtf8("\u8BF7\u5148\u6267\u884C\u5730\u7269\u63D0\u53D6"));
        return;
    }

    // 构建默认文件名
    QString defaultName = QString::fromUtf8("landcover_extraction_%1")
                              .arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss"));

    // 文件格式过滤器
    QString filter = QString::fromUtf8(
        "PNG \u56FE\u50CF (*.png);;"
        "JPEG \u56FE\u50CF (*.jpg *.jpeg);;"
        "TIFF \u56FE\u50CF (*.tif *.tiff);;"
        "BMP \u56FE\u50CF (*.bmp);;"
        "\u6240\u6709\u6587\u4EF6 (*.*)");

    QString filePath = QFileDialog::getSaveFileName(this,
        QString::fromUtf8("\u5BFC\u51FA\u5730\u7269\u63D0\u53D6\u56FE\u50CF"),
        defaultName, filter);

    if (filePath.isEmpty()) return;

    // 根据扩展名选择保存格式
    QImage imageToSave = m_cachedExtractionImage;
    bool success = false;

    if (filePath.endsWith(".png", Qt::CaseInsensitive)) {
        success = imageToSave.save(filePath, "PNG", 100);  // 无损压缩，最高质量
    } else if (filePath.endsWith(".jpg", Qt::CaseInsensitive) ||
               filePath.endsWith(".jpeg", Qt::CaseInsensitive)) {
        success = imageToSave.save(filePath, "JPEG", 95);  // 高质量JPEG
    } else if (filePath.endsWith(".tif", Qt::CaseInsensitive) ||
               filePath.endsWith(".tiff", Qt::CaseInsensitive)) {
        success = imageToSave.save(filePath, "TIFF", 100);  // 无损TIFF
    } else if (filePath.endsWith(".bmp", Qt::CaseInsensitive)) {
        success = imageToSave.save(filePath, "BMP", 100);  // BMP无损
    } else {
        // 默认按PNG保存
        success = imageToSave.save(filePath, "PNG", 100);
    }

    if (success) {
        QFileInfo fi(filePath);
        long long fileSizeKB = fi.size() / 1024;
        addLog(QString::fromUtf8("\u5730\u7269\u63D0\u53D6\u56FE\u50CF\u5DF2\u5BFC\u51FA: %1 (%2 x %3, %4 KB)")
                   .arg(fi.fileName())
                   .arg(imageToSave.width())
                   .arg(imageToSave.height())
                   .arg(fileSizeKB));
        statusBar()->showMessage(QString::fromUtf8("\u5BFC\u51FA\u6210\u529F: %1").arg(fi.fileName()), 5000);
    } else {
        QMessageBox::critical(this, QString::fromUtf8("\u5BFC\u51FA\u5931\u8D25"),
                              QString::fromUtf8("\u65E0\u6CD5\u5BFC\u51FA\u56FE\u50CF\u5230\u6307\u5B9A\u8DEF\u5F84\uFF0C\u8BF7\u68C0\u67E5\u76EE\u6807\u76EE\u5F55\u662F\u5426\u53EF\u5199\u3002"));
    }
}

// =====================================================================
//                      人工判读功能实现
// =====================================================================

void FeatureExtraction4::onAccuracyAnnotationMode()
{
    ClassificationResult* result = nullptr;
    if (m_classificationResult.isValid())
        result = &m_classificationResult;
    else if (m_clusteringResult.isValid())
        result = &m_clusteringResult;
    else {
        QMessageBox::warning(this, QString::fromUtf8("\u63D0\u793A"),
                             QString::fromUtf8("\u8BF7\u5148\u8FD0\u884C\u5206\u7C7B\u6216\u805A\u7C7B\u5206\u6790\uFF0C\u751F\u6210\u5206\u7C7B\u7ED3\u679C\u540E\u518D\u8FDB\u5165\u4EBA\u5DE5\u5224\u8BFB\u6A21\u5F0F\u3002"));
        return;
    }

    const int TARGET_SAMPLES = 200;
    int w = result->width;
    int h = result->height;

    std::default_random_engine rng(static_cast<unsigned>(QDateTime::currentMSecsSinceEpoch()));
    std::uniform_int_distribution<int> randX(0, w - 1);
    std::uniform_int_distribution<int> randY(0, h - 1);

    // ---- 阶段一：按步长采样估算类别分布（避免遍历全部像素）----
    int sampleStep = qMax(1, qMin(w, h) / 50);  // 约 1/2500 采样率
    std::map<int, long long> classPixelEstimate;
    long long sampledCount = 0;

    for (int y = 0; y < h; y += sampleStep) {
        for (int x = 0; x < w; x += sampleStep) {
            int label = result->labelMap[y * w + x];
            if (label >= 0) {
                classPixelEstimate[label] += static_cast<long long>(sampleStep) * sampleStep;
                sampledCount += static_cast<long long>(sampleStep) * sampleStep;
            }
        }
    }

    if (classPixelEstimate.empty()) {
        QMessageBox::warning(this, QString::fromUtf8("\u63D0\u793A"),
                             QString::fromUtf8("\u5206\u7C7B\u7ED3\u679C\u4E2D\u672A\u627E\u5230\u6709\u6548\u6807\u7B7E\u3002"));
        return;
    }

    long long totalEstimate = sampledCount;
    if (totalEstimate <= 0) totalEstimate = static_cast<long long>(w) * h;

    // 按类别比例分配样本数
    std::map<int, int> allocCount;
    int allocated = 0;

    for (const auto& cp : classPixelEstimate) {
        int n = static_cast<int>(std::llround(static_cast<double>(cp.second) / totalEstimate * TARGET_SAMPLES));
        if (n < 1) n = 1;
        allocCount[cp.first] = n;
        allocated += n;
    }

    if (allocated < TARGET_SAMPLES && !classPixelEstimate.empty()) {
        int largestClass = classPixelEstimate.rbegin()->first;
        for (const auto& cp : classPixelEstimate) {
            if (cp.second > classPixelEstimate[largestClass])
                largestClass = cp.first;
        }
        allocCount[largestClass] += (TARGET_SAMPLES - allocated);
    }

    // ---- 阶段二：随机候选采样（每类随机选取所需数量）----
    std::map<int, std::vector<QPoint>> classSamples;
    int maxAttempts = TARGET_SAMPLES * 20;  // 最多尝试 4000 次
    int attempts = 0;

    while (attempts < maxAttempts) {
        int x = randX(rng);
        int y = randY(rng);
        int label = result->labelMap[y * w + x];

        if (label >= 0 && allocCount.count(label)) {
            int needed = allocCount[label];
            int current = static_cast<int>(classSamples[label].size());
            if (current < needed) {
                classSamples[label].push_back(QPoint(x, y));
            }
        }

        // 检查是否全部满足
        bool allSatisfied = true;
        for (const auto& ac : allocCount) {
            if (static_cast<int>(classSamples[ac.first].size()) < ac.second) {
                allSatisfied = false;
                break;
            }
        }
        if (allSatisfied) break;

        attempts++;
    }

    // 合并所有样本点
    std::vector<QPoint> samplePoints;
    for (auto& cs : classSamples) {
        samplePoints.insert(samplePoints.end(), cs.second.begin(), cs.second.end());
    }

    addLog(QString::fromUtf8("\u4EBA\u5DE5\u5224\u8BFB: \u5206\u5C42\u968F\u673A\u91C7\u6837 %1 \u4E2A\u70B9 (%2 \u7C7B, \u6B65\u957F=%3)")
               .arg(samplePoints.size())
               .arg(classPixelEstimate.size())
               .arg(sampleStep));

    // 创建判读会话（带自动命名）
    m_annotationSession = m_accuracyAssessment->createSession(*result, samplePoints);
    m_annotationSession.name = QString::fromUtf8("\u81EA\u52A8\u5224\u8BFB\u4F1A\u8BDD_") +
                                QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss");
    m_annotationSession.description = QString::fromUtf8("\u81EA\u52A8\u89E6\u53D1 \u00B7 %1/%2 \u6837\u672C \u00B7 %3 \u7C7B")
                                          .arg(m_annotationSession.totalCount())
                                          .arg(TARGET_SAMPLES)
                                          .arg(classPixelEstimate.size());

    // 添加到历史版本
    m_annotationHistory = AnnotationHistory();
    m_annotationHistory.addVersion(m_annotationSession);

    m_isAnnotationMode = true;
    m_selectedAnnotationIndex = -1;

    // 切换到人工判读信息面板
    for (int i = 0; i < ui.infoTabWidget->count(); ++i) {
        if (ui.infoTabWidget->tabText(i).contains(QString::fromUtf8("\u4EBA\u5DE5\u5224\u8BFB"))) {
            ui.infoTabWidget->setCurrentIndex(i);
            break;
        }
    }

    // 初始化编辑控件
    m_editClassCombo->blockSignals(true);
    m_editClassCombo->clear();
    m_editClassCombo->addItem(QString::fromUtf8("-- \u4E0D\u4FEE\u6539 --"), -1);
    for (int i = 0; i < static_cast<int>(m_annotationSession.classNames.size()); ++i) {
        m_editClassCombo->addItem(m_annotationSession.classNames[i], i);
    }
    m_editClassCombo->setCurrentIndex(0);
    m_editClassCombo->blockSignals(false);

    // 刷新UI
    refreshAnnotationRecordTable();
    updateAnnotationSessionInfo();
    updateAnnotationEditPanel();
    recalculateAnnotationMetrics();

    // 导航到分类/聚类结果标签页
    if (m_classificationResult.isValid()) {
        ui.mainTabWidget->setCurrentIndex(1);
    } else {
        ui.mainTabWidget->setCurrentIndex(2);
    }

    int conf = m_annotationSession.confirmedCount();
    addLog(QString::fromUtf8("\u4EBA\u5DE5\u5224\u8BFB\u6A21\u5F0F\u5DF2\u542F\u52A8 - \u6807\u6CE8\u70B9: %1, \u5DF2\u786E\u8BA4: %2")
               .arg(m_annotationSession.totalCount()).arg(conf));
}

// 分类/聚类完成后自动创建判读会话（无弹窗）
void FeatureExtraction4::autoCreateAnnotationSession()
{
    ClassificationResult* result = nullptr;
    if (m_classificationResult.isValid())
        result = &m_classificationResult;
    else if (m_clusteringResult.isValid())
        result = &m_clusteringResult;

    if (!result || result->labelMap.empty())
        return;

    const int TARGET_SAMPLES = 200;
    int w = result->width;
    int h = result->height;

    std::default_random_engine rng(static_cast<unsigned>(QDateTime::currentMSecsSinceEpoch()));
    std::uniform_int_distribution<int> randX(0, w - 1);
    std::uniform_int_distribution<int> randY(0, h - 1);

    // 按步长采样估算类别分布
    int sampleStep = qMax(1, qMin(w, h) / 50);
    std::map<int, long long> classPixelEstimate;
    long long sampledCount = 0;

    for (int y = 0; y < h; y += sampleStep) {
        for (int x = 0; x < w; x += sampleStep) {
            int label = result->labelMap[y * w + x];
            if (label >= 0) {
                classPixelEstimate[label] += static_cast<long long>(sampleStep) * sampleStep;
                sampledCount += static_cast<long long>(sampleStep) * sampleStep;
            }
        }
    }

    if (classPixelEstimate.empty()) return;

    long long totalEstimate = sampledCount;
    if (totalEstimate <= 0) totalEstimate = static_cast<long long>(w) * h;

    std::map<int, int> allocCount;
    int allocated = 0;

    for (const auto& cp : classPixelEstimate) {
        int n = static_cast<int>(std::llround(static_cast<double>(cp.second) / totalEstimate * TARGET_SAMPLES));
        if (n < 1) n = 1;
        allocCount[cp.first] = n;
        allocated += n;
    }

    if (allocated < TARGET_SAMPLES && !classPixelEstimate.empty()) {
        int largestClass = classPixelEstimate.rbegin()->first;
        for (const auto& cp : classPixelEstimate) {
            if (cp.second > classPixelEstimate[largestClass])
                largestClass = cp.first;
        }
        allocCount[largestClass] += (TARGET_SAMPLES - allocated);
    }

    // 随机候选采样
    std::map<int, std::vector<QPoint>> classSamples;
    int maxAttempts = TARGET_SAMPLES * 20;
    int attempts = 0;

    while (attempts < maxAttempts) {
        int x = randX(rng);
        int y = randY(rng);
        int label = result->labelMap[y * w + x];

        if (label >= 0 && allocCount.count(label)) {
            int needed = allocCount[label];
            int current = static_cast<int>(classSamples[label].size());
            if (current < needed)
                classSamples[label].push_back(QPoint(x, y));
        }

        bool allSatisfied = true;
        for (const auto& ac : allocCount) {
            if (static_cast<int>(classSamples[ac.first].size()) < ac.second) {
                allSatisfied = false;
                break;
            }
        }
        if (allSatisfied) break;
        attempts++;
    }

    std::vector<QPoint> samplePoints;
    for (auto& cs : classSamples)
        samplePoints.insert(samplePoints.end(), cs.second.begin(), cs.second.end());

    m_annotationSession = m_accuracyAssessment->createSession(*result, samplePoints);
    m_annotationSession.name = QString::fromUtf8("\u81EA\u52A8\u5224\u8BFB\u4F1A\u8BDD_") +
                                QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss");
    m_annotationSession.description = QString::fromUtf8("\u81EA\u52A8\u89E6\u53D1 \u00B7 %1/%2 \u6837\u672C \u00B7 %3 \u7C7B")
                                          .arg(m_annotationSession.totalCount())
                                          .arg(TARGET_SAMPLES)
                                          .arg(classPixelEstimate.size());

    m_annotationHistory = AnnotationHistory();
    m_annotationHistory.addVersion(m_annotationSession);
    m_isAnnotationMode = true;
    m_selectedAnnotationIndex = -1;

    // 初始化编辑控件
    m_editClassCombo->blockSignals(true);
    m_editClassCombo->clear();
    m_editClassCombo->addItem(QString::fromUtf8("-- \u4E0D\u4FEE\u6539 --"), -1);
    for (int i = 0; i < static_cast<int>(m_annotationSession.classNames.size()); ++i) {
        m_editClassCombo->addItem(m_annotationSession.classNames[i], i);
    }
    m_editClassCombo->setCurrentIndex(0);
    m_editClassCombo->blockSignals(false);

    refreshAnnotationRecordTable();
    updateAnnotationSessionInfo();
    updateAnnotationEditPanel();
    recalculateAnnotationMetrics();

    addLog(QString::fromUtf8("\u5206\u7C7B\u5B8C\u6210\uFF0C\u81EA\u52A8\u5EFA\u7ACB\u5224\u8BFB\u4F1A\u8BDD: %1 \u4E2A\u6837\u672C\u70B9 (%2 \u4E2A\u7C7B\u522B)")
               .arg(samplePoints.size()).arg(classPixelEstimate.size()));
}

void FeatureExtraction4::refreshAnnotationRecordTable()
{
    QTableWidget* table = m_annotationRecordTable;
    if (!table) return;

    table->setRowCount(0);
    if (!m_annotationSession.records.empty()) {
        table->setRowCount(static_cast<int>(m_annotationSession.records.size()));

        for (size_t i = 0; i < m_annotationSession.records.size(); ++i) {
            const auto& record = m_annotationSession.records[i];

            // 行背景色：绿=已确认, 浅红=已修正, 橙=待确认
            QColor rowBg;
            if (record.isOverridden())
                rowBg = QColor(255, 235, 235);
            else if (record.confirmed)
                rowBg = QColor(220, 255, 220);
            else
                rowBg = QColor(255, 248, 220);

            QTableWidgetItem* idxItem = new QTableWidgetItem(QString::number(i + 1));
            idxItem->setTextAlignment(Qt::AlignCenter);
            idxItem->setBackground(rowBg);
            table->setItem(static_cast<int>(i), 0, idxItem);

            QString posStr = QString("(%1,%2)").arg(record.position.x()).arg(record.position.y());
            QTableWidgetItem* posItem = new QTableWidgetItem(posStr);
            posItem->setTextAlignment(Qt::AlignCenter);
            posItem->setBackground(rowBg);
            table->setItem(static_cast<int>(i), 1, posItem);

            QString autoLabelStr = (record.autoLabel >= 0 && record.autoLabel < static_cast<int>(m_annotationSession.classNames.size()))
                ? m_annotationSession.classNames[record.autoLabel] : QString::fromUtf8("\u672A\u77E5");
            QTableWidgetItem* autoItem = new QTableWidgetItem(autoLabelStr);
            autoItem->setTextAlignment(Qt::AlignCenter);
            autoItem->setBackground(rowBg);
            table->setItem(static_cast<int>(i), 2, autoItem);

            QString manualStr = (record.manualLabel >= 0 && record.manualLabel < static_cast<int>(m_annotationSession.classNames.size()))
                ? m_annotationSession.classNames[record.manualLabel]
                : (record.manualLabel == -1 ? QString::fromUtf8("\u672A\u4FEE\u6539") : QString::fromUtf8("\u672A\u77E5"));
            QTableWidgetItem* manualItem = new QTableWidgetItem(manualStr);
            manualItem->setTextAlignment(Qt::AlignCenter);
            manualItem->setBackground(rowBg);
            if (record.isOverridden()) {
                manualItem->setForeground(QColor("#c0392b"));
                manualItem->setFont(QFont(manualItem->font().family(), -1, QFont::Bold));
            }
            table->setItem(static_cast<int>(i), 3, manualItem);

            QString status;
            if (record.confirmed) {
                status = record.isOverridden() ? QString::fromUtf8("\u5DF2\u4FEE\u6B63\u2714") : QString::fromUtf8("\u5DF2\u786E\u8BA4\u2714");
            } else {
                status = QString::fromUtf8("\u5F85\u786E\u8BA4...");
            }
            QTableWidgetItem* statusItem = new QTableWidgetItem(status);
            statusItem->setTextAlignment(Qt::AlignCenter);
            statusItem->setBackground(rowBg);
            if (record.confirmed) {
                statusItem->setForeground(record.isOverridden() ? QColor("#c0392b") : QColor("#1e8449"));
            } else {
                statusItem->setForeground(QColor("#d35400"));
            }
            table->setItem(static_cast<int>(i), 4, statusItem);
        }
    }

    // 恢复选中
    if (m_selectedAnnotationIndex >= 0 && m_selectedAnnotationIndex < table->rowCount()) {
        table->selectRow(m_selectedAnnotationIndex);
    }
}

void FeatureExtraction4::updateAnnotationEditPanel()
{
    if (m_selectedAnnotationIndex < 0 ||
        m_selectedAnnotationIndex >= static_cast<int>(m_annotationSession.records.size())) {
        m_editPosLabel->setText(QString::fromUtf8("\u672A\u9009\u4E2D"));
        m_editClassCombo->blockSignals(true);
        m_editClassCombo->setCurrentIndex(0);
        m_editClassCombo->blockSignals(false);
        m_editCommentEdit->clear();
        m_applyEditBtn->setEnabled(false);
        return;
    }

    const auto& record = m_annotationSession.records[m_selectedAnnotationIndex];
    m_applyEditBtn->setEnabled(true);

    // 显示坐标、自动标签、记录状态
    QString statusStr = record.confirmed
        ? (record.isOverridden()
            ? QString::fromUtf8("\u2705 \u5DF2\u4FEE\u6B63")
            : QString::fromUtf8("\u2705 \u5DF2\u786E\u8BA4"))
        : QString::fromUtf8("\u23F3 \u5F85\u786E\u8BA4");

    m_editPosLabel->setText(QString("(%1, %2)  [\u81EA\u52A8: %3]  [%4]")
        .arg(record.position.x())
        .arg(record.position.y())
        .arg(record.autoLabel >= 0 && record.autoLabel < static_cast<int>(m_annotationSession.classNames.size())
             ? m_annotationSession.classNames[record.autoLabel] : QString::fromUtf8("?"))
        .arg(statusStr));

    m_editClassCombo->blockSignals(true);
    int comboIdx = 0;
    if (record.manualLabel >= 0) {
        for (int i = 1; i < m_editClassCombo->count(); ++i) {
            if (m_editClassCombo->itemData(i).toInt() == record.manualLabel) {
                comboIdx = i;
                break;
            }
        }
    }
    m_editClassCombo->setCurrentIndex(comboIdx);
    m_editClassCombo->blockSignals(false);

    m_editCommentEdit->setText(record.comment);
}

void FeatureExtraction4::updateAnnotationSessionInfo()
{
    if (m_annotationSession.sessionId.isEmpty()) {
        m_sessionInfoLabel->setText(QString::fromUtf8("\u672A\u521B\u5EFA\u4F1A\u8BDD"));
        return;
    }

    int total = m_annotationSession.totalCount();
    int confirmed = m_annotationSession.confirmedCount();
    int overridden = m_annotationSession.overriddenCount();
    int pending = total - confirmed;

    // 完成进度条样式
    double pct = total > 0 ? (100.0 * confirmed / total) : 0;
    QString progressBar = QString::fromUtf8("\u2592\u2592\u2592\u2592\u2592\u2592\u2592\u2592\u2592\u2592");
    int filled = qBound(0, qRound(pct / 10.0), 10);
    QString bar = QString::fromUtf8("\u2588").repeated(filled) + progressBar.left(10 - filled);

    m_sessionInfoLabel->setText(
        QString::fromUtf8("\u4F1A\u8BDD: %1\n\u521B\u5EFA: %2  |  \u4FEE\u6539: %3\n")
            .arg(m_annotationSession.name)
            .arg(m_annotationSession.createTime.toString("MM-dd hh:mm"))
            .arg(m_annotationSession.lastModified.toString("MM-dd hh:mm"))
        + QString::fromUtf8("\u6807\u6CE8\u70B9: %1 | \u5DF2\u786E\u8BA4: %2 | \u5F85\u786E\u8BA4: %3 | \u5DF2\u4FEE\u6B63: %4\n")
            .arg(total).arg(confirmed).arg(pending).arg(overridden)
        + bar + QString("  %1%").arg(pct, 0, 'f', 1)
        + QString::fromUtf8("\n\u7248\u672C: v%1").arg(m_annotationHistory.versionCount()));
}

void FeatureExtraction4::recalculateAnnotationMetrics()
{
    if (m_annotationSession.records.empty()) return;

    m_annotationSession.currentMetrics = m_accuracyAssessment->computeFromAnnotations(
        m_annotationSession.records, m_annotationSession.classNames);
    m_annotationSession.lastModified = QDateTime::currentDateTime();

    updateAccuracyMetricsDisplay(m_annotationSession.currentMetrics);
    updateConfusionMatrixTable(m_annotationSession.currentMetrics);
}

void FeatureExtraction4::onAnnotationRecordSelected()
{
    int row = m_annotationRecordTable->currentRow();
    if (row < 0 || row >= static_cast<int>(m_annotationSession.records.size())) {
        m_selectedAnnotationIndex = -1;
    } else {
        m_selectedAnnotationIndex = row;
    }

    updateAnnotationEditPanel();

    // 刷新分类/聚类图像上的标注点叠加层，高亮选中的点
    if (m_isAnnotationMode) {
        applyZoomPanTransform();
    }
}

void FeatureExtraction4::onAnnotationLabelChanged(int classIndex)
{
    Q_UNUSED(classIndex);

    if (m_selectedAnnotationIndex < 0 ||
        m_selectedAnnotationIndex >= static_cast<int>(m_annotationSession.records.size()))
        return;

    int newLabel = m_editClassCombo->currentData().toInt();
    auto& record = m_annotationSession.records[m_selectedAnnotationIndex];

    m_accuracyAssessment->updateAnnotation(record, newLabel);

    refreshAnnotationRecordTable();
    updateAnnotationSessionInfo();
    recalculateAnnotationMetrics();

    // 刷新图像上的标注点颜色
    if (m_isAnnotationMode) {
        applyZoomPanTransform();
    }

    addLog(QString::fromUtf8("\u6807\u6CE8\u70B9(%1,%2): \u4EBA\u5DE5\u6807\u7B7E\u66F4\u6539\u4E3A %3")
               .arg(record.position.x())
               .arg(record.position.y())
               .arg(record.manualLabel >= 0 && record.manualLabel < static_cast<int>(m_annotationSession.classNames.size())
                    ? m_annotationSession.classNames[record.manualLabel] : QString::fromUtf8("\u672A\u4FEE\u6539")));
}

void FeatureExtraction4::onAnnotationCommentEdited()
{
    if (m_selectedAnnotationIndex < 0 ||
        m_selectedAnnotationIndex >= static_cast<int>(m_annotationSession.records.size()))
        return;

    auto& record = m_annotationSession.records[m_selectedAnnotationIndex];

    // 应用标签和备注修改
    int comboClass = m_editClassCombo->currentData().toInt();
    QString comment = m_editCommentEdit->text().trimmed();

    bool changed = false;

    if (comboClass != record.manualLabel) {
        m_accuracyAssessment->updateAnnotation(record, comboClass, comment);
        changed = true;
    } else if (comment != record.comment) {
        record.comment = comment;
        record.editTime = QDateTime::currentDateTime();
        record.confirmed = true;
        changed = true;
    }

    if (changed) {
        refreshAnnotationRecordTable();
        updateAnnotationSessionInfo();
        recalculateAnnotationMetrics();

        // 刷新图像上的标注点颜色
        if (m_isAnnotationMode) {
            applyZoomPanTransform();
        }

        addLog(QString::fromUtf8("\u6807\u6CE8\u70B9(%1,%2): \u4FEE\u6539\u5DF2\u5E94\u7528")
                   .arg(record.position.x())
                   .arg(record.position.y()));
        statusBar()->showMessage(QString::fromUtf8("\u4FEE\u6539\u5DF2\u5E94\u7528\u5230\u6807\u6CE8\u70B9 (%1,%2)")
                                     .arg(record.position.x()).arg(record.position.y()), 3000);
    }
}

void FeatureExtraction4::onAnnotationConfirmAll()
{
    if (m_annotationSession.records.empty()) return;

    int result = QMessageBox::question(this,
        QString::fromUtf8("\u786E\u8BA4\u64CD\u4F5C"),
        QString::fromUtf8("\u786E\u5B9A\u8981\u5C06\u6240\u6709 %1 \u4E2A\u6807\u6CE8\u70B9\u6807\u8BB0\u4E3A\u5DF2\u786E\u8BA4\u5417\uFF1F")
            .arg(m_annotationSession.totalCount()),
        QMessageBox::Yes | QMessageBox::No);

    if (result != QMessageBox::Yes) return;

    m_accuracyAssessment->confirmAll(m_annotationSession.records, true);
    m_annotationSession.lastModified = QDateTime::currentDateTime();

    refreshAnnotationRecordTable();
    updateAnnotationSessionInfo();
    recalculateAnnotationMetrics();

    // 刷新图像上的标注点颜色
    if (m_isAnnotationMode) {
        applyZoomPanTransform();
    }

    addLog(QString::fromUtf8("\u6279\u91CF\u786E\u8BA4: %1 \u4E2A\u6807\u6CE8\u70B9\u5DF2\u786E\u8BA4").arg(m_annotationSession.totalCount()));
    statusBar()->showMessage(QString::fromUtf8("\u5DF2\u6279\u91CF\u786E\u8BA4 %1 \u4E2A\u6807\u6CE8\u70B9").arg(m_annotationSession.totalCount()), 3000);
}

void FeatureExtraction4::onAnnotationResetAll()
{
    if (m_annotationSession.records.empty()) return;

    int result = QMessageBox::warning(this,
        QString::fromUtf8("\u91CD\u7F6E\u64CD\u4F5C"),
        QString::fromUtf8("\u786E\u5B9A\u8981\u6E05\u9664\u6240\u6709\u4EBA\u5DE5\u4FEE\u6B63\u548C\u5907\u6CE8\uFF0C\u6062\u590D\u5230\u81EA\u52A8\u5206\u7C7B\u7ED3\u679C\u5417\uFF1F\n\n\u6B64\u64CD\u4F5C\u4E0D\u53EF\u64A4\u9500\uFF01"),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);

    if (result != QMessageBox::Yes) return;

    m_accuracyAssessment->resetAllOverrides(m_annotationSession.records);
    m_annotationSession.lastModified = QDateTime::currentDateTime();
    m_selectedAnnotationIndex = -1;

    refreshAnnotationRecordTable();
    updateAnnotationEditPanel();
    updateAnnotationSessionInfo();
    recalculateAnnotationMetrics();

    // 刷新图像上的标注点颜色
    if (m_isAnnotationMode) {
        applyZoomPanTransform();
    }

    addLog(QString::fromUtf8("\u91CD\u7F6E\u5168\u90E8\u4EBA\u5DE5\u4FEE\u6B63\uFF0C\u6062\u590D\u81EA\u52A8\u5206\u7C7B\u7ED3\u679C"));
    statusBar()->showMessage(QString::fromUtf8("\u5DF2\u91CD\u7F6E\u6240\u6709\u4EBA\u5DE5\u4FEE\u6B63"), 3000);
}

void FeatureExtraction4::onAnnotationSaveSession()
{
    if (m_annotationSession.records.empty()) {
        QMessageBox::warning(this, QString::fromUtf8("\u63D0\u793A"),
                             QString::fromUtf8("\u6CA1\u6709\u53EF\u4FDD\u5B58\u7684\u5224\u8BFB\u4F1A\u8BDD\u3002"));
        return;
    }

    QString filePath = QFileDialog::getSaveFileName(this,
        QString::fromUtf8("\u4FDD\u5B58\u5224\u8BFB\u4F1A\u8BDD"),
        m_annotationSession.name + ".csv",
        QString::fromUtf8("CSV \u6587\u4EF6 (*.csv);;\u6240\u6709\u6587\u4EF6 (*.*)"));

    if (filePath.isEmpty()) return;

    QString data = AccuracyAssessment::serializeSession(m_annotationSession);
    QFile file(filePath);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&file);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
        out.setCodec("UTF-8");
#endif
        out << data;
        file.close();

        addLog(QString::fromUtf8("\u5224\u8BFB\u4F1A\u8BDD\u5DF2\u4FDD\u5B58: %1").arg(filePath));
        statusBar()->showMessage(QString::fromUtf8("\u4F1A\u8BDD\u5DF2\u4FDD\u5B58"), 3000);
    } else {
        QMessageBox::critical(this, QString::fromUtf8("\u4FDD\u5B58\u5931\u8D25"),
                              QString::fromUtf8("\u65E0\u6CD5\u5199\u5165\u6587\u4EF6: ") + filePath);
    }
}

void FeatureExtraction4::onAnnotationLoadSession()
{
    QString filePath = QFileDialog::getOpenFileName(this,
        QString::fromUtf8("\u52A0\u8F7D\u5224\u8BFB\u4F1A\u8BDD"),
        QString(),
        QString::fromUtf8("CSV \u6587\u4EF6 (*.csv);;\u6240\u6709\u6587\u4EF6 (*.*)"));

    if (filePath.isEmpty()) return;

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::critical(this, QString::fromUtf8("\u52A0\u8F7D\u5931\u8D25"),
                              QString::fromUtf8("\u65E0\u6CD5\u8BFB\u53D6\u6587\u4EF6: ") + filePath);
        return;
    }

    QTextStream in(&file);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    in.setCodec("UTF-8");
#endif
    QString data = in.readAll();
    file.close();

    AnnotationSession loaded = AccuracyAssessment::deserializeSession(data);
    if (loaded.sessionId.isEmpty()) {
        QMessageBox::warning(this, QString::fromUtf8("\u52A0\u8F7D\u5931\u8D25"),
                             QString::fromUtf8("\u6587\u4EF6\u683C\u5F0F\u4E0D\u6B63\u786E\u6216\u6570\u636E\u635F\u574F\u3002"));
        return;
    }

    // 重新计算当前指标
    loaded.currentMetrics = m_accuracyAssessment->computeFromAnnotations(loaded.records, loaded.classNames);

    // 保存当前版本到历史
    if (!m_annotationSession.sessionId.isEmpty()) {
        m_annotationHistory.addVersion(m_annotationSession);
    }

    m_annotationSession = loaded;
    m_annotationHistory.addVersion(loaded);
    m_isAnnotationMode = true;
    m_selectedAnnotationIndex = -1;

    // 更新类别编辑下拉框
    m_editClassCombo->blockSignals(true);
    m_editClassCombo->clear();
    m_editClassCombo->addItem(QString::fromUtf8("-- \u4E0D\u4FEE\u6539 --"), -1);
    for (int i = 0; i < static_cast<int>(m_annotationSession.classNames.size()); ++i) {
        m_editClassCombo->addItem(m_annotationSession.classNames[i], i);
    }
    m_editClassCombo->setCurrentIndex(0);
    m_editClassCombo->blockSignals(false);

    refreshAnnotationRecordTable();
    updateAnnotationEditPanel();
    updateAnnotationSessionInfo();
    recalculateAnnotationMetrics();

    // 刷新图像上的标注点叠加层
    applyZoomPanTransform();

    addLog(QString::fromUtf8("\u5224\u8BFB\u4F1A\u8BDD\u5DF2\u52A0\u8F7D: %1 (%2 \u4E2A\u6807\u6CE8\u70B9)")
               .arg(m_annotationSession.name)
               .arg(m_annotationSession.totalCount()));
    statusBar()->showMessage(QString::fromUtf8("\u4F1A\u8BDD\u5DF2\u52A0\u8F7D: %1").arg(m_annotationSession.name), 5000);
}

void FeatureExtraction4::onAnnotationCompareVersions()
{
    int totalVersions = m_annotationHistory.versionCount();
    if (totalVersions < 2) {
        QMessageBox::information(this, QString::fromUtf8("\u63D0\u793A"),
            QString::fromUtf8("\u5F53\u524D\u53EA\u6709 %1 \u4E2A\u7248\u672C\uFF0C\u65E0\u6CD5\u8FDB\u884C\u6BD4\u8F83\u3002\n\n"
                "\u8BF7\u4FDD\u5B58\u5F53\u524D\u4F1A\u8BDD\u540E\u52A0\u8F7D\u53E6\u4E00\u4E2A\u4F1A\u8BDD\u4EE5\u521B\u5EFA\u591A\u4E2A\u7248\u672C\u3002")
                .arg(totalVersions));
        return;
    }

    // 构建版本选择对话框
    QStringList versionNames;
    for (int i = 0; i < totalVersions; ++i) {
        QString entry = QString("[%1] %2 (%3)")
            .arg(i + 1)
            .arg(m_annotationHistory.versions[i].name)
            .arg(m_annotationHistory.versions[i].lastModified.toString("yyyy-MM-dd hh:mm"));
        versionNames << entry;
    }

    bool okA = false, okB = false;
    QString selA = QInputDialog::getItem(this,
        QString::fromUtf8("\u7248\u672C\u6BD4\u8F83 - \u9009\u62E9\u7248\u672CA"),
        QString::fromUtf8("\u8BF7\u9009\u62E9\u53C2\u8003\u7248\u672C (\u7248\u672CA):"),
        versionNames, totalVersions - 2, false, &okA);

    if (!okA || selA.isEmpty()) return;
    int idxA = versionNames.indexOf(selA);

    QString selB = QInputDialog::getItem(this,
        QString::fromUtf8("\u7248\u672C\u6BD4\u8F83 - \u9009\u62E9\u7248\u672CB"),
        QString::fromUtf8("\u8BF7\u9009\u62E9\u5BF9\u6BD4\u7248\u672C (\u7248\u672CB):"),
        versionNames, totalVersions - 1, false, &okB);

    if (!okB || selB.isEmpty() || idxA == versionNames.indexOf(selB)) {
        if (idxA == versionNames.indexOf(selB))
            QMessageBox::warning(this, QString::fromUtf8("\u63D0\u793A"),
                QString::fromUtf8("\u8BF7\u9009\u62E9\u4E24\u4E2A\u4E0D\u540C\u7684\u7248\u672C\u8FDB\u884C\u6BD4\u8F83\u3002"));
        return;
    }

    int idxB = versionNames.indexOf(selB);
    if (idxA < 0 || idxB < 0) return;

    auto diff = AccuracyAssessment::compareSessions(
        m_annotationHistory.versions[idxA],
        m_annotationHistory.versions[idxB]);

    QString report = AccuracyAssessment::formatSessionDiffReport(diff,
        m_annotationHistory.versions[idxA].name,
        m_annotationHistory.versions[idxB].name);

    // 保存并打开比较报告
    QString filePath = QFileDialog::getSaveFileName(this,
        QString::fromUtf8("\u4FDD\u5B58\u7248\u672C\u6BD4\u8F83\u62A5\u544A"),
        QString::fromUtf8("\u7248\u672C\u6BD4\u8F83\u62A5\u544A_%1.html")
            .arg(QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss")),
        QString::fromUtf8("HTML (*.html)"));

    if (filePath.isEmpty()) return;

    // 生成完整HTML
    QString fullHtml;
    fullHtml += "<!DOCTYPE html>\n<html lang=\"zh-CN\"><head><meta charset='UTF-8'>\n";
    fullHtml += "<title>" + QString::fromUtf8("\u7248\u672C\u6BD4\u8F83\u62A5\u544A") + "</title>\n";
    fullHtml += "<style>\n";
    fullHtml += "body { font-family: 'Microsoft YaHei', Arial, sans-serif; color: #2c3e50; "
                "background: #f0f2f5; line-height: 1.7; padding: 20px; }\n";
    fullHtml += ".card { background: #fff; padding: 20px; margin-bottom: 16px; "
                "border-radius: 8px; box-shadow: 0 1px 4px rgba(0,0,0,0.06); }\n";
    fullHtml += ".card h2 { color: #1a5276; border-bottom: 2px solid #3498db; padding-bottom: 8px; }\n";
    fullHtml += "table { border-collapse: collapse; width: 100%; margin: 8px 0; font-size: 13px; }\n";
    fullHtml += "th { background: #34495e; color: #fff; padding: 8px; text-align: center; font-size: 12px; }\n";
    fullHtml += "td { padding: 6px 8px; text-align: center; border: 1px solid #e0e0e0; }\n";
    fullHtml += "tr:nth-child(even) td { background: #fafafa; }\n";
    fullHtml += ".footer { text-align: center; color: #bdc3c7; font-size: 11px; margin-top: 20px; }\n";
    fullHtml += "</style>\n</head><body>\n";
    fullHtml += "<div style=\"background:linear-gradient(135deg,#1a5276,#2980b9);color:#fff;"
                "padding:24px 32px;border-radius:8px;margin-bottom:16px;\">\n";
    fullHtml += "<h1 style=\"margin:0;font-size:22px;\">" + QString::fromUtf8("\u4EBA\u5DE5\u5224\u8BFB\u7248\u672C\u6BD4\u8F83\u62A5\u544A") + "</h1>\n";
    fullHtml += "<p style=\"margin:4px 0 0;opacity:0.85;font-size:13px;\">"
                + QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss") + "</p>\n";
    fullHtml += "</div>\n";
    fullHtml += report;
    fullHtml += "<div class='footer'><p>" + QString::fromUtf8("\u9065\u611F\u5F71\u50CF\u5730\u7269\u63D0\u53D6\u7CFB\u7EDF - \u4EBA\u5DE5\u5224\u8BFB\u6A21\u5757") + "</p></div>\n";
    fullHtml += "</body>\n</html>";

    QFile outFile(filePath);
    if (outFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&outFile);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
        out.setCodec("UTF-8");
#endif
        out << fullHtml;
        outFile.close();

        addLog(QString::fromUtf8("\u7248\u672C\u6BD4\u8F83\u62A5\u544A\u5DF2\u4FDD\u5B58: %1").arg(filePath));
        statusBar()->showMessage(QString::fromUtf8("\u7248\u672C\u6BD4\u8F83\u62A5\u544A\u5DF2\u5BFC\u51FA"), 5000);
    }
}

void FeatureExtraction4::onAnnotationExportReport()
{
    if (m_annotationSession.records.empty()) {
        QMessageBox::warning(this, QString::fromUtf8("\u63D0\u793A"),
                             QString::fromUtf8("\u6CA1\u6709\u53EF\u5BFC\u51FA\u7684\u5224\u8BFB\u6570\u636E\u3002\u8BF7\u5148\u521B\u5EFA\u5224\u8BFB\u4F1A\u8BDD\u3002"));
        return;
    }

    QString filePath = QFileDialog::getSaveFileName(this,
        QString::fromUtf8("\u5BFC\u51FA\u7CBE\u5EA6\u8BC4\u5B9A\u4E13\u4E1A\u62A5\u544A"),
        QString::fromUtf8("\u7CBE\u5EA6\u8BC4\u5B9A\u62A5\u544A_%1.html")
            .arg(QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss")),
        QString::fromUtf8("HTML \u62A5\u544A (*.html);;\u6240\u6709\u6587\u4EF6 (*.*)"));

    if (filePath.isEmpty()) return;

    QString report = AccuracyAssessment::generateProfessionalReport(
        m_annotationSession.currentMetrics, &m_annotationSession);

    QFile file(filePath);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&file);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
        out.setCodec("UTF-8");
#endif
        out << report;
        file.close();

        addLog(QString::fromUtf8("\u7CBE\u5EA6\u8BC4\u5B9A\u4E13\u4E1A\u62A5\u544A\u5DF2\u5BFC\u51FA: %1").arg(filePath));
        statusBar()->showMessage(QString::fromUtf8("\u4E13\u4E1A\u7CBE\u5EA6\u62A5\u544A\u5DF2\u5BFC\u51FA - OA: %1%")
                                     .arg(m_annotationSession.currentMetrics.overallAccuracy * 100, 0, 'f', 2), 5000);

        QMessageBox::information(this, QString::fromUtf8("\u5BFC\u51FA\u6210\u529F"),
            QString::fromUtf8("\u7CBE\u5EA6\u8BC4\u5B9A\u4E13\u4E1A\u62A5\u544A\u5DF2\u751F\u6210\uFF1A\n\n%1\n\n"
                              "\u5305\u542B\uFF1A\n\u2022 \u7CBE\u5EA6\u6307\u6807\u6982\u89C8\n\u2022 \u7EDF\u8BA1\u5206\u6790\u6458\u8981\n"
                              "\u2022 \u5404\u7C7B\u522B\u7CBE\u5EA6\u5206\u5E03\u56FE\u8868\n\u2022 \u6DF7\u6DC6\u77E9\u9635\n\u2022 \u7ED3\u8BBA\u4E0E\u5EFA\u8BAE")
                .arg(filePath));
    } else {
        QMessageBox::critical(this, QString::fromUtf8("\u5BFC\u51FA\u5931\u8D25"),
                              QString::fromUtf8("\u65E0\u6CD5\u5199\u5165\u6587\u4EF6: ") + filePath);
    }
}

void FeatureExtraction4::onAnnotationExportPdf()
{
    if (m_annotationSession.records.empty()) {
        QMessageBox::warning(this, QString::fromUtf8("\u63D0\u793A"),
                             QString::fromUtf8("\u6CA1\u6709\u53EF\u5BFC\u51FA\u7684\u5224\u8BFB\u6570\u636E\u3002\u8BF7\u5148\u521B\u5EFA\u5224\u8BFB\u4F1A\u8BDD\u3002"));
        return;
    }

    QString filePath = QFileDialog::getSaveFileName(this,
        QString::fromUtf8("\u5BFC\u51FA PDF \u62A5\u544A"),
        QString::fromUtf8("\u7CBE\u5EA6\u8BC4\u5B9A\u62A5\u544A_%1.pdf")
            .arg(QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss")),
        QString::fromUtf8("PDF \u6587\u4EF6 (*.pdf)"));

    if (filePath.isEmpty()) return;

    QString htmlReport = AccuracyAssessment::generateProfessionalReport(
        m_annotationSession.currentMetrics, &m_annotationSession);

    if (AccuracyAssessment::exportReportToPDF(htmlReport, filePath)) {
        addLog(QString::fromUtf8("\u7CBE\u5EA6\u62A5\u544A\u5DF2\u5BFC\u51FAPDF: %1").arg(filePath));
        statusBar()->showMessage(QString::fromUtf8("PDF\u62A5\u544A\u5DF2\u5BFC\u51FA"), 5000);
        QMessageBox::information(this, QString::fromUtf8("\u5BFC\u51FA\u6210\u529F"),
                                 QString::fromUtf8("PDF\u62A5\u544A\u5DF2\u751F\u6210\uFF1A\n%1").arg(filePath));
    } else {
        QMessageBox::critical(this, QString::fromUtf8("\u5BFC\u51FA\u5931\u8D25"),
                              QString::fromUtf8("\u65E0\u6CD5\u751F\u6210PDF\u6587\u4EF6\u3002"));
    }
}

void FeatureExtraction4::onAnnotationExportExcel()
{
    if (m_annotationSession.records.empty()) {
        QMessageBox::warning(this, QString::fromUtf8("\u63D0\u793A"),
                             QString::fromUtf8("\u6CA1\u6709\u53EF\u5BFC\u51FA\u7684\u5224\u8BFB\u6570\u636E\u3002\u8BF7\u5148\u521B\u5EFA\u5224\u8BFB\u4F1A\u8BDD\u3002"));
        return;
    }

    QString filePath = QFileDialog::getSaveFileName(this,
        QString::fromUtf8("\u5BFC\u51FA Excel \u62A5\u544A"),
        QString::fromUtf8("\u7CBE\u5EA6\u8BC4\u5B9A\u62A5\u544A_%1.csv")
            .arg(QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss")),
        QString::fromUtf8("CSV \u6587\u4EF6 (*.csv)"));

    if (filePath.isEmpty()) return;

    if (AccuracyAssessment::exportReportToExcel(m_annotationSession.currentMetrics, filePath, &m_annotationSession)) {
        addLog(QString::fromUtf8("\u7CBE\u5EA6\u62A5\u544A\u5DF2\u5BFC\u51FAExcel: %1").arg(filePath));
        statusBar()->showMessage(QString::fromUtf8("Excel\u62A5\u544A\u5DF2\u5BFC\u51FA"), 5000);
        QMessageBox::information(this, QString::fromUtf8("\u5BFC\u51FA\u6210\u529F"),
                                 QString::fromUtf8("Excel\u62A5\u544A\u5DF2\u751F\u6210\uFF1A\n%1\n\n"
                                                   "\u53EF\u7528Excel\u76F4\u63A5\u6253\u5F00\u67E5\u770B\u3002").arg(filePath));
    } else {
        QMessageBox::critical(this, QString::fromUtf8("\u5BFC\u51FA\u5931\u8D25"),
                              QString::fromUtf8("\u65E0\u6CD5\u751F\u6210Excel\u6587\u4EF6\u3002"));
    }
}

// ========== 框架集成接口 ==========

void FeatureExtraction4::setOutputDir(const QString &dir)
{
    m_outputDir = dir;
    m_resultPaths.clear();
}

QStringList FeatureExtraction4::getResultPaths() const
{
    return m_resultPaths;
}

void FeatureExtraction4::displayImage(const QString& filePath)
{
    QImage img(filePath);
    if (!img.isNull()) {
        displayImage(img, ui.imageDisplayLabel);
    }
}