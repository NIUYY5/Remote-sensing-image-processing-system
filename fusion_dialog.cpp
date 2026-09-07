#include "fusion_dialog.h"

#include <cstdint>
#include <new>

#include <QtWidgets/QFileDialog>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QInputDialog>
#include <QtWidgets/QSplitter>
#include <QtWidgets/QTabWidget>
#include <QtGui/QImageReader>
#include <QtGui/QPixmap>
#include <QtCore/QElapsedTimer>
#include <QtCore/QDateTime>
#include <QtCore/QSettings>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonArray>
#include <QtCore/QStandardPaths>
#include <QtCore/QDir>
#include <QtCore/QFileInfo>
#include <QtCore/QFile>
#include <QtCore/QTextStream>
#include <QtGui/QImage>
#include <QtGui/QWheelEvent>
#include <QtPrintSupport/QPrinter>
#include <QtPrintSupport/QPrintDialog>

const double FusionDialog::ZoomStep = 1.25;
const double FusionDialog::MinZoom = 0.01;
const double FusionDialog::MaxZoom = 50.0;

FusionDialog::FusionDialog(QWidget *parent)
    : QDialog(parent)
    , m_panScene(nullptr)
    , m_msScene(nullptr)
    , m_fusionScene(nullptr)
    , m_outputSaved(false)
{
    ui.setupUi(this);

    m_presetFilePath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                       + "/fusion_presets.json";

    createPreviewScenes();

    connect(ui.panBrowseBtn, &QPushButton::clicked, this, &FusionDialog::onBrowsePan);
    connect(ui.msBrowseBtn, &QPushButton::clicked, this, &FusionDialog::onBrowseMs);
    connect(ui.outputBrowseBtn, &QPushButton::clicked, this, &FusionDialog::onBrowseOutput);
    connect(ui.algorithmCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &FusionDialog::onAlgorithmChanged);
    connect(ui.weightSlider, &QSlider::valueChanged, this, &FusionDialog::onWeightSliderChanged);
    connect(ui.weightSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &FusionDialog::onWeightSpinBoxChanged);
    connect(ui.presetCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &FusionDialog::onPresetChanged);
    connect(ui.savePresetBtn, &QPushButton::clicked, this, &FusionDialog::onSavePreset);
    connect(ui.deletePresetBtn, &QPushButton::clicked, this, &FusionDialog::onDeletePreset);

    connect(ui.executeBtn, &QPushButton::clicked, this, &FusionDialog::onExecuteFusion);
    connect(ui.previewBtn, &QPushButton::clicked, this, &FusionDialog::onPreview);

    connect(ui.exportCsvBtn, &QPushButton::clicked, this, &FusionDialog::onExportCsv);
    connect(ui.exportHtmlBtn, &QPushButton::clicked, this, &FusionDialog::onExportHtml);
    connect(ui.exportPdfBtn, &QPushButton::clicked, this, &FusionDialog::onExportPdf);

    connect(ui.zoomInBtn, &QToolButton::clicked, this, &FusionDialog::onZoomIn);
    connect(ui.zoomOutBtn, &QToolButton::clicked, this, &FusionDialog::onZoomOut);
    connect(ui.zoomFitBtn, &QToolButton::clicked, this, &FusionDialog::onZoomFit);
    connect(ui.zoomOriginalBtn, &QToolButton::clicked, this, &FusionDialog::onZoomOriginal);

    connect(ui.previewSubTab, &QTabWidget::currentChanged, this, [this]() {
        updateZoomLabel(currentPreviewView());
    });

    ui.panPreviewView->viewport()->installEventFilter(this);
    ui.msPreviewView->viewport()->installEventFilter(this);
    ui.fusionPreviewView->viewport()->installEventFilter(this);

    connect(ui.closeBtn, &QPushButton::clicked, this, &QDialog::close);

    loadPresets();
    appendLog(QString::fromUtf8("影像融合模块已就绪"));
}

FusionDialog::~FusionDialog()
{
    delete m_panScene;
    delete m_msScene;
    delete m_fusionScene;
}

bool FusionDialog::eventFilter(QObject *obj, QEvent *event)
{
    if (event->type() == QEvent::Wheel)
    {
        QWheelEvent *wheelEvent = static_cast<QWheelEvent *>(event);
        if (wheelEvent->modifiers() & Qt::ControlModifier)
        {
            QGraphicsView *view = currentPreviewView();
            if (view && obj == view->viewport())
            {
                const double angle = wheelEvent->angleDelta().y();
                if (angle > 0)
                    applyZoomToView(view, ZoomStep);
                else
                    applyZoomToView(view, 1.0 / ZoomStep);
                return true;
            }
        }
    }
    return QDialog::eventFilter(obj, event);
}

void FusionDialog::setPanImagePath(const QString &path)
{
    ui.panPathEdit->setText(path);
    m_panPath = path;
    loadPanImage();
}

void FusionDialog::setMsImagePath(const QString &path)
{
    ui.msPathEdit->setText(path);
    m_msPath = path;
    loadMsImage();
}

void FusionDialog::setOutputPath(const QString &path)
{
    ui.outputPathEdit->setText(path);
    m_outputPath = path;
}

void FusionDialog::createPreviewScenes()
{
    QColor bgColor(45, 45, 48);

    m_panScene = new QGraphicsScene(this);
    m_panScene->setBackgroundBrush(QBrush(bgColor));
    ui.panPreviewView->setScene(m_panScene);
    ui.panPreviewView->setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    ui.panPreviewView->setDragMode(QGraphicsView::ScrollHandDrag);

    m_msScene = new QGraphicsScene(this);
    m_msScene->setBackgroundBrush(QBrush(bgColor));
    ui.msPreviewView->setScene(m_msScene);
    ui.msPreviewView->setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    ui.msPreviewView->setDragMode(QGraphicsView::ScrollHandDrag);

    m_fusionScene = new QGraphicsScene(this);
    m_fusionScene->setBackgroundBrush(QBrush(bgColor));
    ui.fusionPreviewView->setScene(m_fusionScene);
    ui.fusionPreviewView->setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    ui.fusionPreviewView->setDragMode(QGraphicsView::ScrollHandDrag);
}

void FusionDialog::updatePreviewTabs()
{
    if (m_panImage.isValid())
    {
        QImage img = m_panImage.toQImageGrayscale();
        m_panScene->clear();
        m_panScene->addPixmap(QPixmap::fromImage(img));
        m_panScene->setSceneRect(img.rect());
        ui.panPreviewView->fitInView(m_panScene->sceneRect(), Qt::KeepAspectRatio);
    }

    if (m_msImage.isValid())
    {
        QImage img = m_msImage.toQImageRGB();
        m_msScene->clear();
        m_msScene->addPixmap(QPixmap::fromImage(img));
        m_msScene->setSceneRect(img.rect());
        ui.msPreviewView->fitInView(m_msScene->sceneRect(), Qt::KeepAspectRatio);
    }
}

QGraphicsView* FusionDialog::currentPreviewView() const
{
    QWidget* current = ui.previewSubTab->currentWidget();
    if (current == ui.panPreviewTab)  return ui.panPreviewView;
    if (current == ui.msPreviewTab)   return ui.msPreviewView;
    if (current == ui.fusionPreviewTab) return ui.fusionPreviewView;
    return ui.panPreviewView;
}

void FusionDialog::applyZoomToView(QGraphicsView* view, double factor)
{
    if (!view) return;

    double currentZoom = view->transform().m11();
    double newZoom = currentZoom * factor;

    if (factor > 1.0 && currentZoom >= MaxZoom) return;
    if (factor < 1.0 && currentZoom <= MinZoom) return;

    view->scale(factor, factor);
    updateZoomLabel(view);
}

void FusionDialog::updateZoomLabel(QGraphicsView* view)
{
    if (!view) return;
    int pct = static_cast<int>(view->transform().m11() * 100);
    ui.zoomLevelLabel->setText(QString::fromUtf8("%1%").arg(pct));
}

void FusionDialog::onZoomIn()
{
    applyZoomToView(currentPreviewView(), ZoomStep);
}

void FusionDialog::onZoomOut()
{
    applyZoomToView(currentPreviewView(), 1.0 / ZoomStep);
}

void FusionDialog::onZoomFit()
{
    QGraphicsView* view = currentPreviewView();
    if (!view || !view->scene()) return;
    view->fitInView(view->scene()->sceneRect(), Qt::KeepAspectRatio);
    updateZoomLabel(view);
}

void FusionDialog::onZoomOriginal()
{
    QGraphicsView* view = currentPreviewView();
    if (!view) return;
    view->resetTransform();
    updateZoomLabel(view);
}

void FusionDialog::collectParameters()
{
    m_params.algorithmType = ui.algorithmCombo->currentIndex();
    m_params.weightCoefficient = ui.weightSpinBox->value();
    m_params.interpolationMethod = ui.interpCombo->currentIndex();
    m_params.stretchPanHistogram = ui.histCheckBox->isChecked();
    m_params.useAdaptiveFilter = ui.adaptiveCheckBox->isChecked();
}

void FusionDialog::applyPreset(const FusionParameters &preset)
{
    ui.algorithmCombo->setCurrentIndex(preset.algorithmType);

    ui.weightSpinBox->blockSignals(true);
    ui.weightSlider->blockSignals(true);
    ui.weightSpinBox->setValue(preset.weightCoefficient);
    ui.weightSlider->setValue(static_cast<int>(preset.weightCoefficient * 100));
    ui.weightSpinBox->blockSignals(false);
    ui.weightSlider->blockSignals(false);

    ui.interpCombo->setCurrentIndex(preset.interpolationMethod);
    ui.histCheckBox->setChecked(preset.stretchPanHistogram);
    ui.adaptiveCheckBox->setChecked(preset.useAdaptiveFilter);
    m_params = preset;
}

void FusionDialog::loadPresets()
{
    m_presets.clear();
    ui.presetCombo->clear();

    FusionParameters defaultParams;
    defaultParams.setDefaults();
    m_presets.append(qMakePair(QString::fromUtf8("默认参数"), defaultParams));

    FusionParameters highSpatial;
    highSpatial.setDefaults();
    highSpatial.algorithmType = 1;
    highSpatial.weightCoefficient = 1.0;
    highSpatial.stretchPanHistogram = true;
    highSpatial.interpolationMethod = 2;
    m_presets.append(qMakePair(QString::fromUtf8("高空间细节"), highSpatial));

    FusionParameters highSpectral;
    highSpectral.setDefaults();
    highSpectral.algorithmType = 0;
    highSpectral.weightCoefficient = 0.5;
    highSpectral.stretchPanHistogram = true;
    highSpectral.interpolationMethod = 1;
    m_presets.append(qMakePair(QString::fromUtf8("高光谱保真"), highSpectral));

    FusionParameters balanced;
    balanced.setDefaults();
    balanced.algorithmType = 0;
    balanced.weightCoefficient = 0.75;
    balanced.stretchPanHistogram = true;
    balanced.interpolationMethod = 1;
    m_presets.append(qMakePair(QString::fromUtf8("均衡模式"), balanced));

    QFile file(m_presetFilePath);
    if (file.open(QIODevice::ReadOnly))
    {
        QByteArray data = file.readAll();
        file.close();
        QJsonDocument doc = QJsonDocument::fromJson(data);
        if (doc.isObject())
        {
            QJsonObject root = doc.object();
            QJsonArray presets = root["presets"].toArray();
            for (const QJsonValue &val : presets)
            {
                QJsonObject presetObj = val.toObject();
                QString name = presetObj["name"].toString();
                FusionParameters fp;
                fp.algorithmType = presetObj["algorithmType"].toInt(0);
                fp.panBandIndex = presetObj["panBandIndex"].toInt(0);
                fp.stretchPanHistogram = presetObj["stretchPanHistogram"].toBool(true);
                fp.weightCoefficient = presetObj["weightCoefficient"].toDouble(1.0);
                fp.interpolationMethod = presetObj["interpolationMethod"].toInt(1);
                fp.useAdaptiveFilter = presetObj["useAdaptiveFilter"].toBool(false);
                fp.filterSigma = presetObj["filterSigma"].toDouble(1.0);
                m_presets.append(qMakePair(name, fp));
            }
        }
    }

    for (const auto &p : m_presets)
        ui.presetCombo->addItem(p.first);

    ui.presetCombo->setCurrentIndex(0);
    applyPreset(m_presets[0].second);
}

void FusionDialog::savePresets()
{
    QJsonObject root;
    QJsonArray presets;

    for (int i = 4; i < m_presets.size(); ++i)
    {
        QJsonObject presetObj;
        presetObj["name"] = m_presets[i].first;
        presetObj["algorithmType"] = m_presets[i].second.algorithmType;
        presetObj["panBandIndex"] = m_presets[i].second.panBandIndex;
        presetObj["stretchPanHistogram"] = m_presets[i].second.stretchPanHistogram;
        presetObj["weightCoefficient"] = m_presets[i].second.weightCoefficient;
        presetObj["interpolationMethod"] = m_presets[i].second.interpolationMethod;
        presetObj["useAdaptiveFilter"] = m_presets[i].second.useAdaptiveFilter;
        presetObj["filterSigma"] = m_presets[i].second.filterSigma;
        presets.append(presetObj);
    }

    root["presets"] = presets;

    QDir().mkpath(QFileInfo(m_presetFilePath).absolutePath());
    QFile file(m_presetFilePath);
    if (file.open(QIODevice::WriteOnly))
    {
        QJsonDocument doc(root);
        file.write(doc.toJson(QJsonDocument::Indented));
        file.close();
    }
}

void FusionDialog::appendLog(const QString &message)
{
    QString timestamp = QDateTime::currentDateTime().toString("[hh:mm:ss] ");
    ui.logTextEdit->append(timestamp + message);
}

void FusionDialog::displayEvaluationResults()
{
    QString text;
    text += QString::fromUtf8("========================================\n");
    text += QString::fromUtf8("        影像融合质量评估报告\n");
    text += QString::fromUtf8("========================================\n\n");

    text += QString::fromUtf8("融合算法: %1\n")
        .arg(m_params.algorithmType == 0 ? "PCA (主成分分析)" : "HIS (色度-亮度-饱和度)");
    text += QString::fromUtf8("权重系数: %1\n").arg(m_params.weightCoefficient, 0, 'f', 2);
    text += QString::fromUtf8("插值方法: %1\n")
        .arg(m_params.interpolationMethod == 0 ? "最近邻" : (m_params.interpolationMethod == 1 ? "双线性" : "双三次"));
    text += QString::fromUtf8("处理耗时: %1 ms\n\n").arg(m_evaluationResult.processingTimeMs, 0, 'f', 1);

    text += QString::fromUtf8("--- 综合指标 ---\n");
    text += QString::fromUtf8("  光谱扭曲度:           %1\n").arg(m_evaluationResult.spectralDistortion, 0, 'f', 6);
    text += QString::fromUtf8("  空间细节保持度:        %1\n").arg(m_evaluationResult.spatialDetailPreservation, 0, 'f', 4);
    text += QString::fromUtf8("  信息熵:               %1\n").arg(m_evaluationResult.informationEntropy, 0, 'f', 4);
    text += QString::fromUtf8("  峰值信噪比(PSNR):     %1 dB\n\n").arg(m_evaluationResult.psnr, 0, 'f', 2);

    text += QString::fromUtf8("--- 逐波段评估 ---\n");
    text += QString::fromUtf8("  波段  |  信息熵  |  PSNR(dB)\n");
    text += QString::fromUtf8("  ------|----------|----------\n");

    int minBands = std::min(m_msImage.bands, m_fusedImage.bands);
    for (int b = 0; b < minBands; ++b)
    {
        QString entropyStr = b < (int)m_evaluationResult.perBandEntropy.size()
            ? QString::number(m_evaluationResult.perBandEntropy[b], 'f', 4) : "N/A";
        QString psnrStr = b < (int)m_evaluationResult.perBandPSNR.size()
            ? QString::number(m_evaluationResult.perBandPSNR[b], 'f', 2) : "N/A";
        text += QString::fromUtf8("  %1     |  %2  |  %3\n")
            .arg(b + 1, 1).arg(entropyStr, 7).arg(psnrStr, 7);
    }

    text += QString::fromUtf8("\n--- 影像信息 ---\n");
    text += QString::fromUtf8("  全色影像: %1 x %2\n")
        .arg(m_panImage.width).arg(m_panImage.height);
    text += QString::fromUtf8("  多光谱影像: %1 x %2 (%3波段)\n")
        .arg(m_msImage.width).arg(m_msImage.height).arg(m_msImage.bands);
    text += QString::fromUtf8("  融合结果: %1 x %2 (%3波段)\n")
        .arg(m_fusedImage.width).arg(m_fusedImage.height).arg(m_fusedImage.bands);

    ui.evaluationTextEdit->setPlainText(text);
}

bool FusionDialog::loadPanImage()
{
    if (m_panPath.isEmpty()) return false;
    bool ok = loadImageFromFile(m_panPath, m_panImage);
    if (ok)
    {
        appendLog(QString::fromUtf8("全色影像加载成功: %1 x %2")
            .arg(m_panImage.width).arg(m_panImage.height));
    }
    else
    {
        appendLog(QString::fromUtf8("全色影像加载失败: %1").arg(m_panPath));
    }
    updatePreviewTabs();
    return ok;
}

bool FusionDialog::loadMsImage()
{
    if (m_msPath.isEmpty()) return false;
    bool ok = loadImageFromFile(m_msPath, m_msImage);
    if (ok)
    {
        appendLog(QString::fromUtf8("多光谱影像加载成功: %1 x %2 x %3波段")
            .arg(m_msImage.width).arg(m_msImage.height).arg(m_msImage.bands));
    }
    else
    {
        appendLog(QString::fromUtf8("多光谱影像加载失败: %1").arg(m_msPath));
    }
    updatePreviewTabs();
    return ok;
}

bool FusionDialog::loadImageFromFile(const QString &path, MultiBandImage &image)
{
    QImageReader reader(path);
    reader.setAutoTransform(true);

    if (reader.canRead())
    {
        QImage img = reader.read();
        if (img.isNull()) return false;

        QImage::Format fmt = img.format();

        if (fmt == QImage::Format_Grayscale16)
        {
            image.create(img.width(), img.height(), 1);
            for (int y = 0; y < img.height(); ++y)
            {
                const uint16_t *line = reinterpret_cast<const uint16_t *>(img.constScanLine(y));
                for (int x = 0; x < img.width(); ++x)
                    image.pixel(x, y, 0) = static_cast<float>(line[x]);
            }
            return true;
        }

        if (fmt == QImage::Format_Grayscale8 || img.isGrayscale())
        {
            img = img.convertToFormat(QImage::Format_Grayscale8);
            image.create(img.width(), img.height(), 1);
            for (int y = 0; y < img.height(); ++y)
            {
                const uchar *line = img.constScanLine(y);
                for (int x = 0; x < img.width(); ++x)
                    image.pixel(x, y, 0) = static_cast<float>(line[x]);
            }
            return true;
        }

        img = img.convertToFormat(QImage::Format_RGB32);
        int channels = img.hasAlphaChannel() ? 4 : 3;
        image.create(img.width(), img.height(), channels);

        for (int y = 0; y < img.height(); ++y)
        {
            const QRgb *line = reinterpret_cast<const QRgb *>(img.constScanLine(y));
            for (int x = 0; x < img.width(); ++x)
            {
                QRgb pixel = line[x];
                if (channels >= 3)
                {
                    image.pixel(x, y, 0) = static_cast<float>(qRed(pixel));
                    image.pixel(x, y, 1) = static_cast<float>(qGreen(pixel));
                    image.pixel(x, y, 2) = static_cast<float>(qBlue(pixel));
                }
                if (channels >= 4)
                    image.pixel(x, y, 3) = static_cast<float>(qAlpha(pixel));
            }
        }
        return true;
    }

    QImage img(path);
    if (!img.isNull())
    {
        QImage::Format fmt = img.format();

        if (fmt == QImage::Format_Grayscale16)
        {
            image.create(img.width(), img.height(), 1);
            for (int y = 0; y < img.height(); ++y)
            {
                const uint16_t *line = reinterpret_cast<const uint16_t *>(img.constScanLine(y));
                for (int x = 0; x < img.width(); ++x)
                    image.pixel(x, y, 0) = static_cast<float>(line[x]);
            }
            return true;
        }

        img = img.convertToFormat(QImage::Format_RGB32);
        image.create(img.width(), img.height(), 1);
        for (int y = 0; y < img.height(); ++y)
        {
            const QRgb *line = reinterpret_cast<const QRgb *>(img.constScanLine(y));
            for (int x = 0; x < img.width(); ++x)
                image.pixel(x, y, 0) = static_cast<float>(qGray(line[x]));
        }
        return true;
    }

    return false;
}

void FusionDialog::onBrowsePan()
{
    QStringList filters;
    filters << QString::fromUtf8("影像文件 (*.tif *.tiff *.jpg *.jpeg *.png *.bmp *.jp2 *.j2k *.dat *.img)");
    filters << QString::fromUtf8("TIFF文件 (*.tif *.tiff)");
    filters << QString::fromUtf8("JPEG2000文件 (*.jp2 *.j2k)");
    filters << QString::fromUtf8("所有文件 (*.*)");

    QString path = QFileDialog::getOpenFileName(this,
        QString::fromUtf8("选择全色影像"), QString(), filters.join(";;"));

    if (!path.isEmpty())
    {
        setPanImagePath(path);
    }
}

void FusionDialog::onBrowseMs()
{
    QStringList filters;
    filters << QString::fromUtf8("影像文件 (*.tif *.tiff *.jpg *.jpeg *.png *.bmp *.jp2 *.j2k *.dat *.img)");
    filters << QString::fromUtf8("TIFF文件 (*.tif *.tiff)");
    filters << QString::fromUtf8("JPEG2000文件 (*.jp2 *.j2k)");
    filters << QString::fromUtf8("所有文件 (*.*)");

    QString path = QFileDialog::getOpenFileName(this,
        QString::fromUtf8("选择多光谱影像"), QString(), filters.join(";;"));

    if (!path.isEmpty())
    {
        setMsImagePath(path);
    }
}

void FusionDialog::onBrowseOutput()
{
    QString defaultDir = m_outputPath.isEmpty()
        ? (m_msPath.isEmpty() ? QString() : QFileInfo(m_msPath).absolutePath())
        : QFileInfo(m_outputPath).absolutePath();

    QString defaultName = "fused_result.tif";
    if (!m_panPath.isEmpty())
    {
        QFileInfo fi(m_panPath);
        defaultName = fi.completeBaseName() + "_fused.tif";
    }

    QString path = QFileDialog::getSaveFileName(this,
        QString::fromUtf8("选择融合结果保存路径"),
        defaultDir.isEmpty() ? defaultName : defaultDir + "/" + defaultName,
        QString::fromUtf8("TIFF文件 (*.tif *.tiff);;JPEG文件 (*.jpg *.jpeg);;PNG文件 (*.png);;所有文件 (*.*)"));

    if (!path.isEmpty())
    {
        setOutputPath(path);
    }
}

void FusionDialog::onAlgorithmChanged(int index)
{
    collectParameters();
    appendLog(QString::fromUtf8("切换算法: %1")
        .arg(index == 0 ? "PCA (主成分分析)" : "HIS (色度-亮度-饱和度)"));
}

void FusionDialog::onWeightSliderChanged(int value)
{
    double dvalue = value / 100.0;
    ui.weightSpinBox->blockSignals(true);
    ui.weightSpinBox->setValue(dvalue);
    ui.weightSpinBox->blockSignals(false);
    collectParameters();
}

void FusionDialog::onWeightSpinBoxChanged(double value)
{
    ui.weightSlider->blockSignals(true);
    ui.weightSlider->setValue(static_cast<int>(value * 100));
    ui.weightSlider->blockSignals(false);
    collectParameters();
}

void FusionDialog::onPresetChanged(int index)
{
    if (index < 0 || index >= m_presets.size()) return;
    applyPreset(m_presets[index].second);
    appendLog(QString::fromUtf8("加载参数预设: %1").arg(m_presets[index].first));
}

void FusionDialog::onSavePreset()
{
    collectParameters();

    bool ok;
    QString name = QInputDialog::getText(this,
        QString::fromUtf8("保存参数预设"),
        QString::fromUtf8("请输入预设名称:"),
        QLineEdit::Normal,
        QString::fromUtf8("自定义预设"), &ok);

    if (!ok || name.trimmed().isEmpty()) return;

    for (int i = 0; i < m_presets.size(); ++i)
    {
        if (m_presets[i].first == name)
        {
            m_presets[i].second = m_params;
            appendLog(QString::fromUtf8("参数预设已更新: %1").arg(name));
            savePresets();
            return;
        }
    }

    m_presets.append(qMakePair(name, m_params));
    ui.presetCombo->addItem(name);
    ui.presetCombo->setCurrentIndex(ui.presetCombo->count() - 1);

    savePresets();
    appendLog(QString::fromUtf8("参数预设已保存: %1").arg(name));
}

void FusionDialog::onDeletePreset()
{
    int index = ui.presetCombo->currentIndex();
    if (index < 4)
    {
        QMessageBox::information(this, QString::fromUtf8("提示"),
            QString::fromUtf8("内置预设不可删除。"));
        return;
    }

    QString name = m_presets[index].first;
    m_presets.removeAt(index);
    ui.presetCombo->removeItem(index);
    ui.presetCombo->setCurrentIndex(0);
    applyPreset(m_presets[0].second);

    savePresets();
    appendLog(QString::fromUtf8("参数预设已删除: %1").arg(name));
}

void FusionDialog::onExecuteFusion()
{
    if (!m_panImage.isValid())
    {
        QMessageBox::warning(this, QString::fromUtf8("错误"),
            QString::fromUtf8("请先加载全色影像。"));
        return;
    }

    if (!m_msImage.isValid())
    {
        QMessageBox::warning(this, QString::fromUtf8("错误"),
            QString::fromUtf8("请先加载多光谱影像。"));
        return;
    }

    collectParameters();

    appendLog(QString::fromUtf8("开始执行影像融合..."));
    ui.progressBar->setValue(10);
    ui.resultTabWidget->setCurrentIndex(2);

    QString algoName = m_params.algorithmType == 0 ? "PCA" : "HIS";
    appendLog(QString::fromUtf8("算法: %1, 权重: %2, 插值: %3")
        .arg(algoName)
        .arg(m_params.weightCoefficient, 0, 'f', 2)
        .arg(m_params.interpolationMethod == 0 ? "最近邻" : (m_params.interpolationMethod == 1 ? "双线性" : "双三次")));

    ui.progressBar->setValue(30);
    QApplication::processEvents();

    size_t pixelCount = static_cast<size_t>(m_panImage.width) * m_panImage.height;
    size_t estimatedBytes = pixelCount * m_msImage.bands * sizeof(double) * 2;
    if (estimatedBytes > 2147483648ULL)
    {
        appendLog(QString::fromUtf8("警告: 估算内存需求约 %1 GB，可能超出可用内存")
            .arg(estimatedBytes / 1073741824.0, 0, 'f', 1));
    }

    double elapsedMs = 0.0;

    try
    {
        if (m_params.algorithmType == 0)
        {
            m_fusedImage = pcaFusion(m_panImage, m_msImage, m_params, &elapsedMs);
        }
        else
        {
            m_fusedImage = hisFusion(m_panImage, m_msImage, m_params, &elapsedMs);
        }
    }
    catch (const std::bad_alloc&)
    {
        appendLog(QString::fromUtf8("融合失败: 内存不足"));
        QMessageBox::critical(this, QString::fromUtf8("错误"),
            QString::fromUtf8("影像融合失败：内存不足。\n\n"
                               "请尝试以下方法：\n"
                               "1. 缩小输入影像尺寸\n"
                               "2. 关闭其他应用程序释放内存\n"
                               "3. 使用 64 位编译版本处理大影像"));
        ui.progressBar->setValue(0);
        return;
    }

    ui.progressBar->setValue(70);
    QApplication::processEvents();

    if (!m_fusedImage.isValid())
    {
        appendLog(QString::fromUtf8("融合失败，请检查输入数据。"));
        emit fusionFailed(QString::fromUtf8("融合算法执行失败"));
        return;
    }

    appendLog(QString::fromUtf8("融合完成: %1 x %2 x %3波段, 耗时 %4 ms")
        .arg(m_fusedImage.width).arg(m_fusedImage.height)
        .arg(m_fusedImage.bands).arg(elapsedMs, 0, 'f', 1));

    m_fusionScene->clear();
    QImage fusedImg = m_fusedImage.toQImageRGB();
    m_fusionScene->addPixmap(QPixmap::fromImage(fusedImg));
    m_fusionScene->setSceneRect(fusedImg.rect());
    ui.fusionPreviewView->fitInView(m_fusionScene->sceneRect(), Qt::KeepAspectRatio);

    ui.progressBar->setValue(85);
    QApplication::processEvents();

    appendLog(QString::fromUtf8("正在计算质量评估指标..."));

    try
    {
        MultiBandImage msUpsampled = m_msImage;
        if (m_msImage.width != m_fusedImage.width || m_msImage.height != m_fusedImage.height)
        {
            msUpsampled = upsampleMS(m_msImage, m_fusedImage.width, m_fusedImage.height,
                                      m_params.interpolationMethod);
        }

        m_evaluationResult = evaluateFusion(msUpsampled, m_fusedImage, m_panImage);
        m_evaluationResult.processingTimeMs = elapsedMs;
    }
    catch (const std::bad_alloc&)
    {
        appendLog(QString::fromUtf8("评估失败: 内存不足"));
        appendLog(QString::fromUtf8("融合结果已生成，但质量评估未能完成"));
        m_evaluationResult.clear();
    }

    displayEvaluationResults();

    ui.progressBar->setValue(100);
    ui.resultTabWidget->setCurrentIndex(0);
    ui.previewSubTab->setCurrentIndex(2);

    appendLog(QString::fromUtf8("评估完成: 光谱扭曲度=%1, 空间细节=%2, 信息熵=%3, PSNR=%4 dB")
        .arg(m_evaluationResult.spectralDistortion, 0, 'f', 4)
        .arg(m_evaluationResult.spatialDetailPreservation, 0, 'f', 4)
        .arg(m_evaluationResult.informationEntropy, 0, 'f', 4)
        .arg(m_evaluationResult.psnr, 0, 'f', 2));

    if (!m_outputPath.isEmpty())
    {
        QImage outputImage;
        if (m_fusedImage.bands >= 3)
            outputImage = m_fusedImage.toQImageRGB();
        else
            outputImage = m_fusedImage.toQImageGrayscale();

        m_outputSaved = outputImage.save(m_outputPath);
        if (m_outputSaved)
            appendLog(QString::fromUtf8("融合结果已自动保存: %1").arg(m_outputPath));
        else
            appendLog(QString::fromUtf8("融合结果自动保存失败: %1").arg(m_outputPath));
    }

    emit fusionCompleted(m_fusedImage);
}

void FusionDialog::onPreview()
{
    if (!m_panImage.isValid() || !m_msImage.isValid())
    {
        QMessageBox::warning(this, QString::fromUtf8("提示"),
            QString::fromUtf8("请先加载全色影像和多光谱影像。"));
        return;
    }

    collectParameters();
    appendLog(QString::fromUtf8("正在生成参数预览..."));

    double elapsedMs = 0.0;
    MultiBandImage previewImage;

    try
    {
        if (m_params.algorithmType == 0)
            previewImage = pcaFusion(m_panImage, m_msImage, m_params, &elapsedMs);
        else
            previewImage = hisFusion(m_panImage, m_msImage, m_params, &elapsedMs);
    }
    catch (const std::bad_alloc&)
    {
        appendLog(QString::fromUtf8("预览失败: 内存不足"));
        QMessageBox::warning(this, QString::fromUtf8("错误"),
            QString::fromUtf8("预览生成失败：内存不足。"));
        return;
    }

    if (!previewImage.isValid())
    {
        appendLog(QString::fromUtf8("预览生成失败。"));
        return;
    }

    m_fusionScene->clear();
    QImage fusedImg = previewImage.toQImageRGB();
    m_fusionScene->addPixmap(QPixmap::fromImage(fusedImg));
    m_fusionScene->setSceneRect(fusedImg.rect());
    ui.fusionPreviewView->fitInView(m_fusionScene->sceneRect(), Qt::KeepAspectRatio);

    ui.resultTabWidget->setCurrentIndex(0);
    ui.previewSubTab->setCurrentIndex(2);

    appendLog(QString::fromUtf8("预览生成完成，耗时 %1 ms").arg(elapsedMs, 0, 'f', 1));
}

void FusionDialog::onExportCsv()
{
    if (!m_fusedImage.isValid())
    {
        QMessageBox::warning(this, QString::fromUtf8("提示"),
            QString::fromUtf8("请先执行融合操作。"));
        return;
    }

    QString filePath = QFileDialog::getSaveFileName(this,
        QString::fromUtf8("导出CSV评估报告"), QString(),
        QString::fromUtf8("CSV文件 (*.csv);;所有文件 (*.*)"));

    if (filePath.isEmpty()) return;

    std::string csv = evaluationReportCSV(m_msImage, m_fusedImage, m_panImage,
                                           m_evaluationResult, m_params);

    QFile file(filePath);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        QTextStream stream(&file);
        stream << QString::fromStdString(csv);
        file.close();
        appendLog(QString::fromUtf8("CSV报告已导出: %1").arg(filePath));
    }
    else
    {
        QMessageBox::critical(this, QString::fromUtf8("错误"),
            QString::fromUtf8("无法写入文件:\n%1").arg(file.errorString()));
    }
}

void FusionDialog::onExportHtml()
{
    if (!m_fusedImage.isValid())
    {
        QMessageBox::warning(this, QString::fromUtf8("提示"),
            QString::fromUtf8("请先执行融合操作。"));
        return;
    }

    QString filePath = QFileDialog::getSaveFileName(this,
        QString::fromUtf8("导出HTML评估报告"), QString(),
        QString::fromUtf8("HTML文件 (*.html *.htm);;所有文件 (*.*)"));

    if (filePath.isEmpty()) return;

    std::string html = evaluationReportHTML(m_msImage, m_fusedImage, m_panImage,
                                              m_evaluationResult, m_params);

    QFile file(filePath);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        QTextStream stream(&file);
        stream << QString::fromStdString(html);
        file.close();
        appendLog(QString::fromUtf8("HTML报告已导出: %1").arg(filePath));
    }
    else
    {
        QMessageBox::critical(this, QString::fromUtf8("错误"),
            QString::fromUtf8("无法写入文件:\n%1").arg(file.errorString()));
    }
}

void FusionDialog::onExportPdf()
{
    if (!m_fusedImage.isValid())
    {
        QMessageBox::warning(this, QString::fromUtf8("提示"),
            QString::fromUtf8("请先执行融合操作。"));
        return;
    }

    QString filePath = QFileDialog::getSaveFileName(this,
        QString::fromUtf8("导出PDF评估报告"), QString(),
        QString::fromUtf8("PDF文件 (*.pdf);;所有文件 (*.*)"));

    if (filePath.isEmpty()) return;

    QPrinter printer(QPrinter::HighResolution);
    printer.setOutputFormat(QPrinter::PdfFormat);
    printer.setOutputFileName(filePath);
    printer.setPageSize(QPageSize(QPageSize::A4));

    QTextDocument doc;
    doc.setHtml(QString::fromStdString(
        evaluationReportHTML(m_msImage, m_fusedImage, m_panImage,
                              m_evaluationResult, m_params)));

    doc.print(&printer);

    appendLog(QString::fromUtf8("PDF报告已导出: %1").arg(filePath));
}
