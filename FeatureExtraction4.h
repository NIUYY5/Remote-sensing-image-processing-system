#pragma once

#include <QtWidgets/QMainWindow>
#include <QtWidgets/QLabel>
#include <QtWidgets/QProgressDialog>
#include <QtWidgets/QTableWidget>
#include <QtWidgets/QTextEdit>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QDoubleSpinBox>
#include <QPointer>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QDockWidget>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QToolBar>
#include <QtWidgets/QScrollArea>
#include <QtWidgets/QScrollBar>
#include <QtWidgets/QFormLayout>
#include <QCloseEvent>
#include <QImage>
#include <QPixmap>
#include <QWheelEvent>
#include <QRadioButton>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QLineEdit>
#include <QTableWidget>
#include <QDialog>
#include <QSpinBox>
#include <QHBoxLayout>
#include <QDateTime>
#include <QElapsedTimer>
#include <QTextCursor>

#include "ui_FeatureExtraction4.h"
#include "GeoImageData.h"
#include "ClusteringEngine.h"
#include "ClassificationEngine.h"
#include "FeatureExtractor.h"
#include "AccuracyAssessment.h"
#include "ExportManager.h"
#include "LandCoverExtractor.h"

class FeatureExtraction4 : public QMainWindow
{
    Q_OBJECT

public:
    FeatureExtraction4(QWidget* parent = nullptr);
    ~FeatureExtraction4();

    void setOutputDir(const QString &dir);
    QStringList getResultPaths() const;

signals:
    void resultReady(const QString &type, const QStringList &paths);

protected:
    void closeEvent(QCloseEvent* event) override;
    bool eventFilter(QObject* obj, QEvent* event) override;

private slots:
    void onOpenImage();
    void onOpenMultiBand();
    void onSaveImage();
    void onExportResult();
    void onRunClustering();
    void onRunClassification();
    void onComputeIndices();
    void onAccuracyAssessment();
    void onAccuracyAnnotationMode();    // 进入人工判读模式
    void onAnnotationRecordSelected();  // 选中标注记录
    void onAnnotationLabelChanged(int classIndex);  // 标注标签变更
    void onAnnotationCommentEdited();   // 标注备注编辑
    void onAnnotationConfirmAll();      // 批量确认
    void onAnnotationResetAll();        // 重置全部修正
    void onAnnotationSaveSession();     // 保存判读会话
    void onAnnotationLoadSession();     // 加载判读会话
    void onAnnotationCompareVersions(); // 版本对比分析
    void onAnnotationExportReport();    // 导出专业精度报告
    void onAnnotationExportPdf();       // 导出PDF报告
    void onAnnotationExportExcel();     // 导出Excel报告
    void autoCreateAnnotationSession(); // 分类/聚类后自动创建判读会话
    void onShowComparison();           // 对比分析
    void onAbout();
    void onExtractLandCover();      // 地物提取
    void onExtractionExport();      // 导出地物提取图像
    void onExtractionProgress(int percent);

    // 地物类别管理
    void onAddClass();
    void onRemoveClass();
    void onRenameClass();
    void onClassColorChanged();
    void onClassTableSelectionChanged();

    // 样本选择
    void onSampleModeChanged();
    void onAutoSampleMethodChanged();
    void onAutoGenerateSamples();
    void onManualStartSampling();
    void onManualSampleCollected();

    void onClusterAlgoChanged(int index);
    void onClassificationModeChanged();
    void onImageClicked(QPoint pos);

    void onClusteringProgress(int percent);
    void onClassificationProgress(int percent);
    void onExportProgress(int percent);
    void onStatusMessage(const QString& msg);
    void onExportFinished(const QString& filePath);
    void onExportError(const QString& errorMsg);

private:
    Ui::FeatureExtraction4Class ui;

    GeoImageData m_image;
    ClassificationResult m_classificationResult;
    ClassificationResult m_clusteringResult;
    ClassificationResult m_comparisonResult;
    ExtractionResult m_extractionResult;         // 地物提取结果
    TrainingData m_trainingData;
    AccuracyMetrics m_accuracyMetrics;

    ClusteringEngine* m_clusteringEngine;
    ClassificationEngine* m_classificationEngine;
    FeatureExtractor* m_featureExtractor;
    AccuracyAssessment* m_accuracyAssessment;
    ExportManager* m_exportManager;
    LandCoverExtractor* m_landCoverExtractor;   // 地物提取引擎

    bool m_manualSamplingActive = false;
    int m_currentSampleClass = 0;
    std::vector<QPoint> m_manualSamplePoints;
    std::vector<int> m_manualSampleLabels;

    double m_zoomLevel = 1.0;
    double m_comparisonZoomLevel = 1.0;  // 对比面板独立缩放级别
    double m_extractionZoomLevel = 1.0;  // 地物提取面板独立缩放级别
    QPoint m_panOffset;
    QPoint m_comparisonPanOffset;        // 对比面板独立平移偏移
    QPoint m_extractionPanOffset;         // 地物提取面板独立平移偏移
    bool m_isPanning = false;
    bool m_comparisonIsPanning = false;  // 对比面板拖拽状态
    bool m_extractionIsPanning = false;   // 地物提取面板拖拽状态
    QPoint m_lastPanPos;

    // 人工判读状态
    AnnotationSession m_annotationSession;
    AnnotationHistory m_annotationHistory;
    bool m_isAnnotationMode = false;
    int m_selectedAnnotationIndex = -1;
    QImage m_cachedOriginalImage;     // 原始影像缓存，严格隔离聚类/分类结果
    QImage m_cachedClusteringImage;   // 聚类分析结果缓存
    QImage m_cachedClassificationImage; // 地物分类结果缓存
    QImage m_cachedExtractionImage;    // 地物提取结果缓存

    // 输出管理
    QString m_outputDir;
    QStringList m_resultPaths;

    // 进度计时
    QElapsedTimer m_progressTimer;
    qint64 m_progressLastPercent = 0;
    QString m_progressStageName;
    QPointer<QProgressDialog> m_progressDialog;  // 进程实时显示窗口（QPointer防悬空指针）

    // 代码创建的 UI 控件（不在 .ui 文件中）
    QAction* m_actionAnnotationMode = nullptr;

    // 人工判读 Tab 控件
    QWidget* m_annotationTab = nullptr;
    QPushButton* m_createSessionBtn = nullptr;
    QPushButton* m_loadSessionBtn = nullptr;
    QPushButton* m_saveSessionBtn = nullptr;
    QLabel* m_sessionInfoLabel = nullptr;
    QTableWidget* m_annotationRecordTable = nullptr;
    QGroupBox* m_annotationEditGroup = nullptr;
    QLabel* m_editPosLabel = nullptr;
    QComboBox* m_editClassCombo = nullptr;
    QLineEdit* m_editCommentEdit = nullptr;
    QPushButton* m_applyEditBtn = nullptr;
    QPushButton* m_confirmAllBtn = nullptr;
    QPushButton* m_resetAllBtn = nullptr;
    QPushButton* m_compareVersionsBtn = nullptr;
    QPushButton* m_exportReportBtn = nullptr;
    QPushButton* m_exportPdfBtn = nullptr;
    QPushButton* m_exportExcelBtn = nullptr;

    // 地物提取控件
    QLabel* m_extractionDisplayLabel = nullptr;
    QPushButton* m_extractionExportBtn = nullptr;
    QWidget* m_extractionViewTab = nullptr;

    void setupConnections();
    void setupClassTable();
    void setupSampleControls();
    void initDefaultClasses();
    void initClassLegendPreview();
    void refreshClassTable();
    void displayImage(const QImage& img, QLabel* label);
    void displayImage(const QString& filePath);  // 兼容框架的字符串路径版本
    void displayClassificationResult(const ClassificationResult& result, QLabel* targetLabel);
    void displayResultOnCurrentTab(const ClassificationResult& result);
    void updateClassLegend(const ClassificationResult& result);
    void updateClusterLegend(const ClassificationResult& result);
    void updateAccuracyDisplay(const AccuracyMetrics& metrics);
    void updateAccuracyMetricsDisplay(const AccuracyMetrics& metrics);
    void updateConfusionMatrixTable(const AccuracyMetrics& metrics);
    void updateProgress(int percent, const QString& label);
    void updateStatusBarClassificationInfo();
    void updateStatusBarImageInfo();
    void updateStatusBarProgress(int percent, const QString& status);
    void addLog(const QString& message);
    void updateWorkflowStep(int step, bool completed);
    void applyClusteringConfig();
    void applyClassifierConfig();
    void applyExportConfig();
    void collectTrainingSamples();
    std::vector<QPoint> generateGridSamples(int classIndex, int count);
    std::vector<QPoint> generateRandomSamples(int classIndex, int count);
    void applyZoomPanTransform();
    QPoint mapToImage(const QPoint& labelPos);
    const QImage& cachedImageForTab(int tabIdx) const;
    void updateCompareLeftRight();  // 自动同步对比面板
    void displayExtractionResult();     // 显示地物提取结果
    void updateExtractionLegend();      // 更新地物提取图例
    void refreshAnnotationRecordTable();  // 刷新标注记录表
    void updateAnnotationEditPanel();     // 刷新标注编辑面板
    void updateAnnotationSessionInfo();   // 刷新会话信息
    void recalculateAnnotationMetrics();  // 重新计算并显示精度指标
};