#pragma once

#include <QtWidgets/QDialog>
#include <QtWidgets/QGraphicsScene>
#include <QtWidgets/QGraphicsView>
#include <QtWidgets/QTextEdit>
#include <QtWidgets/QProgressBar>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QDoubleSpinBox>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QToolButton>
#include <QtWidgets/QLabel>

#include "ui_stitch_dialog.h"
#include "stitch_core.h"
#include "geo_metadata.h"
#include "rpc_model.h"

class StitchDialog : public QDialog
{
    Q_OBJECT

public:
    explicit StitchDialog(QWidget *parent = nullptr);
    ~StitchDialog();

    void setImagePath(int index, const QString &path);
    void setOutputPath(const QString &path);
    QString getOutputPath() const { return m_outputPath; }
    bool isOutputSaved() const { return m_outputSaved; }

    MultiBandImage getResultImage() const { return m_resultImage; }
    StitchConfig getConfig() const { return m_config; }
    GeoMetadata getResultGeoMetadata() const { return m_resultGeoMeta; }
    StitchValidationResult getValidationResult() const { return m_validationResult; }

signals:
    void stitchCompleted(const MultiBandImage &result);
    void stitchFailed(const QString &error);

private slots:
    void onBrowseImage1();
    void onBrowseImage2();
    void onBrowseOutput();
    void onAlgorithmChanged(int index);

    void onExecuteStitch();
    void onSaveResult();

    void onGeoCoordinateToggled(bool checked);
    void onRPCModeToggled(bool checked);

    void onBrowseRpc1();
    void onBrowseRpc2();

    void onZoomIn();
    void onZoomOut();
    void onZoomFit();
    void onZoomOriginal();

private:
    bool eventFilter(QObject *obj, QEvent *event) override;

    void createPreviewScenes();
    void updatePreviewTabs();
    void displayResultImage();
    QGraphicsView* currentPreviewView() const;
    void applyZoomToView(QGraphicsView* view, double factor);
    void updateZoomLabel(QGraphicsView* view);
    void collectConfig();
    void appendLog(const QString &message);
    void updateGeoInfoLabel();

    bool loadImage1();
    bool loadImage2();
    bool loadImageFromFile(const QString &path, MultiBandImage &image);
    void loadRPCMetadata();
    void appendValidationReport(const StitchValidationResult& result);

    Ui::StitchDialog ui;

    QGraphicsScene *m_img1Scene;
    QGraphicsScene *m_img2Scene;
    QGraphicsScene *m_resultScene;

    MultiBandImage m_image1;
    MultiBandImage m_image2;
    MultiBandImage m_resultImage;

    StitchConfig m_config;

    GeoMetadata m_geoMeta1;
    GeoMetadata m_geoMeta2;
    GeoMetadata m_resultGeoMeta;

    RPCModel m_rpcModel1;
    RPCModel m_rpcModel2;

    StitchOutputMetadata m_outputMetadata;
    StitchValidationResult m_validationResult;
    StitchIntegrityReport m_integrityReport;

    QString m_img1Path;
    QString m_img2Path;
    QString m_outputPath;
    bool m_outputSaved;

    static const double ZoomStep;
    static const double MinZoom;
    static const double MaxZoom;
};
