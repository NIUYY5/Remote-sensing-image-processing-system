#pragma once

#include <QtWidgets/QDialog>
#include <QtWidgets/QGraphicsScene>
#include <QtWidgets/QGraphicsView>
#include <QtWidgets/QTextEdit>
#include <QtWidgets/QProgressBar>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QSlider>
#include <QtWidgets/QDoubleSpinBox>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QToolButton>
#include <QtWidgets/QLabel>

#include "ui_fusion_dialog.h"
#include "fusion_core.h"

class FusionDialog : public QDialog
{
    Q_OBJECT

public:
    explicit FusionDialog(QWidget *parent = nullptr);
    ~FusionDialog();

    bool eventFilter(QObject *obj, QEvent *event) override;

    void setPanImagePath(const QString &path);
    void setMsImagePath(const QString &path);
    void setOutputPath(const QString &path);
    QString getOutputPath() const { return m_outputPath; }
    bool isOutputSaved() const { return m_outputSaved; }

    MultiBandImage getFusedImage() const { return m_fusedImage; }
    FusionParameters getParameters() const { return m_params; }
    EvaluationResult getEvaluation() const { return m_evaluationResult; }

signals:
    void fusionCompleted(const MultiBandImage &result);
    void fusionFailed(const QString &error);

private slots:
    void onBrowsePan();
    void onBrowseMs();
    void onBrowseOutput();
    void onAlgorithmChanged(int index);
    void onWeightSliderChanged(int value);
    void onWeightSpinBoxChanged(double value);
    void onPresetChanged(int index);
    void onSavePreset();
    void onDeletePreset();

    void onExecuteFusion();
    void onPreview();

    void onExportCsv();
    void onExportHtml();
    void onExportPdf();

    void onZoomIn();
    void onZoomOut();
    void onZoomFit();
    void onZoomOriginal();

private:
    void createPreviewScenes();
    void updatePreviewTabs();
    QGraphicsView* currentPreviewView() const;
    void applyZoomToView(QGraphicsView* view, double factor);
    void updateZoomLabel(QGraphicsView* view);
    void collectParameters();
    void applyPreset(const FusionParameters &preset);
    void loadPresets();
    void savePresets();
    void appendLog(const QString &message);
    void displayEvaluationResults();

    bool loadPanImage();
    bool loadMsImage();
    bool loadImageFromFile(const QString &path, MultiBandImage &image);

    Ui::FusionDialog ui;

    QGraphicsScene *m_panScene;
    QGraphicsScene *m_msScene;
    QGraphicsScene *m_fusionScene;

    MultiBandImage m_panImage;
    MultiBandImage m_msImage;
    MultiBandImage m_fusedImage;

    FusionParameters m_params;
    EvaluationResult m_evaluationResult;

    QString m_panPath;
    QString m_msPath;
    QString m_outputPath;
    bool m_outputSaved;

    QList<QPair<QString, FusionParameters>> m_presets;
    QString m_presetFilePath;

    static const double ZoomStep;
    static const double MinZoom;
    static const double MaxZoom;
};
