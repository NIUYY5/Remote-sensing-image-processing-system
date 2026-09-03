#include "stitch_dialog.h"

#include <cstdint>

#include <QtWidgets/QFileDialog>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QApplication>
#include <QtGui/QImageReader>
#include <QtGui/QPixmap>
#include <QtGui/QWheelEvent>
#include <QtCore/QElapsedTimer>
#include <QtCore/QDateTime>
#include <QtCore/QFileInfo>
#include <QtCore/QFile>
#include <QtCore/QTimer>

const double StitchDialog::ZoomStep = 1.25;
const double StitchDialog::MinZoom = 0.01;
const double StitchDialog::MaxZoom = 50.0;

StitchDialog::StitchDialog(QWidget *parent)
    : QDialog(parent)
    , m_img1Scene(nullptr)
    , m_img2Scene(nullptr)
    , m_resultScene(nullptr)
    , m_outputSaved(false)
{
    ui.setupUi(this);

    createPreviewScenes();

    connect(ui.img1BrowseBtn, &QPushButton::clicked, this, &StitchDialog::onBrowseImage1);
    connect(ui.img2BrowseBtn, &QPushButton::clicked, this, &StitchDialog::onBrowseImage2);
    connect(ui.outputBrowseBtn, &QPushButton::clicked, this, &StitchDialog::onBrowseOutput);
    connect(ui.algorithmCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &StitchDialog::onAlgorithmChanged);

    connect(ui.saveBtn, &QPushButton::clicked, this, &StitchDialog::onSaveResult);

    connect(ui.geoCoordinateCheckBox, &QCheckBox::toggled, this, &StitchDialog::onGeoCoordinateToggled);
    connect(ui.rpcModeCheckBox, &QCheckBox::toggled, this, &StitchDialog::onRPCModeToggled);

    connect(ui.img1RpcBrowseBtn, &QPushButton::clicked, this, &StitchDialog::onBrowseRpc1);
    connect(ui.img2RpcBrowseBtn, &QPushButton::clicked, this, &StitchDialog::onBrowseRpc2);

    connect(ui.executeBtn, &QPushButton::clicked, this, &StitchDialog::onExecuteStitch);

    connect(ui.zoomInBtn, &QToolButton::clicked, this, &StitchDialog::onZoomIn);
    connect(ui.zoomOutBtn, &QToolButton::clicked, this, &StitchDialog::onZoomOut);
    connect(ui.zoomFitBtn, &QToolButton::clicked, this, &StitchDialog::onZoomFit);
    connect(ui.zoomOriginalBtn, &QToolButton::clicked, this, &StitchDialog::onZoomOriginal);

    connect(ui.previewSubTab, &QTabWidget::currentChanged, this, [this]() {
        updateZoomLabel(currentPreviewView());
    });

    ui.img1PreviewView->viewport()->installEventFilter(this);
    ui.img2PreviewView->viewport()->installEventFilter(this);
    ui.resultPreviewView->viewport()->installEventFilter(this);

    connect(ui.closeBtn, &QPushButton::clicked, this, &QDialog::close);

    collectConfig();
    appendLog(QString::fromUtf8("影像拼接模块已就绪"));
}

StitchDialog::~StitchDialog()
{
    delete m_img1Scene;
    delete m_img2Scene;
    delete m_resultScene;
}

bool StitchDialog::eventFilter(QObject *obj, QEvent *event)
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

void StitchDialog::setImagePath(int index, const QString &path)
{
    if (index == 0)
    {
        ui.img1PathEdit->setText(path);
        m_img1Path = path;
        loadImage1();
    }
    else
    {
        ui.img2PathEdit->setText(path);
        m_img2Path = path;
        loadImage2();
    }
}

void StitchDialog::setOutputPath(const QString &path)
{
    ui.outputPathEdit->setText(path);
    m_outputPath = path;
}

void StitchDialog::createPreviewScenes()
{
    QColor bgColor(45, 45, 48);

    m_img1Scene = new QGraphicsScene(this);
    m_img1Scene->setBackgroundBrush(QBrush(bgColor));
    ui.img1PreviewView->setScene(m_img1Scene);
    ui.img1PreviewView->setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    ui.img1PreviewView->setDragMode(QGraphicsView::ScrollHandDrag);

    m_img2Scene = new QGraphicsScene(this);
    m_img2Scene->setBackgroundBrush(QBrush(bgColor));
    ui.img2PreviewView->setScene(m_img2Scene);
    ui.img2PreviewView->setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    ui.img2PreviewView->setDragMode(QGraphicsView::ScrollHandDrag);

    m_resultScene = new QGraphicsScene(this);
    m_resultScene->setBackgroundBrush(QBrush(bgColor));
    ui.resultPreviewView->setScene(m_resultScene);
    ui.resultPreviewView->setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    ui.resultPreviewView->setDragMode(QGraphicsView::ScrollHandDrag);
    ui.resultPreviewView->setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    ui.resultPreviewView->setResizeAnchor(QGraphicsView::AnchorViewCenter);
    ui.resultPreviewView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    ui.resultPreviewView->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
}

void StitchDialog::updatePreviewTabs()
{
    if (m_image1.isValid())
    {
        QImage img = (m_image1.bands >= 3) ? m_image1.toQImageRGB() : m_image1.toQImageGrayscale();
        m_img1Scene->clear();
        m_img1Scene->addPixmap(QPixmap::fromImage(img));
        m_img1Scene->setSceneRect(img.rect());
        ui.img1PreviewView->fitInView(m_img1Scene->sceneRect(), Qt::KeepAspectRatio);
    }

    if (m_image2.isValid())
    {
        QImage img = (m_image2.bands >= 3) ? m_image2.toQImageRGB() : m_image2.toQImageGrayscale();
        m_img2Scene->clear();
        m_img2Scene->addPixmap(QPixmap::fromImage(img));
        m_img2Scene->setSceneRect(img.rect());
        ui.img2PreviewView->fitInView(m_img2Scene->sceneRect(), Qt::KeepAspectRatio);
    }
}

QGraphicsView* StitchDialog::currentPreviewView() const
{
    QWidget* current = ui.previewSubTab->currentWidget();
    if (current == ui.img1PreviewTab)   return ui.img1PreviewView;
    if (current == ui.img2PreviewTab)   return ui.img2PreviewView;
    if (current == ui.resultPreviewTab) return ui.resultPreviewView;
    return ui.img1PreviewView;
}

void StitchDialog::applyZoomToView(QGraphicsView* view, double factor)
{
    if (!view) return;

    double currentZoom = view->transform().m11();
    double newZoom = currentZoom * factor;

    if (factor > 1.0 && currentZoom >= MaxZoom) return;
    if (factor < 1.0 && currentZoom <= MinZoom) return;

    view->scale(factor, factor);
    updateZoomLabel(view);
}

void StitchDialog::updateZoomLabel(QGraphicsView* view)
{
    if (!view) return;
    int pct = static_cast<int>(view->transform().m11() * 100);
    ui.zoomLevelLabel->setText(QString::fromUtf8("%1%").arg(pct));
}

void StitchDialog::onZoomIn()
{
    applyZoomToView(currentPreviewView(), ZoomStep);
}

void StitchDialog::onZoomOut()
{
    applyZoomToView(currentPreviewView(), 1.0 / ZoomStep);
}

void StitchDialog::onZoomFit()
{
    QGraphicsView* view = currentPreviewView();
    if (!view || !view->scene()) return;
    view->fitInView(view->scene()->sceneRect(), Qt::KeepAspectRatio);
    updateZoomLabel(view);
}

void StitchDialog::onZoomOriginal()
{
    QGraphicsView* view = currentPreviewView();
    if (!view) return;
    view->resetTransform();
    updateZoomLabel(view);
}

void StitchDialog::collectConfig()
{
    m_config.algorithmType = ui.algorithmCombo->currentIndex();
    m_config.featureDetector = ui.featureCombo->currentIndex();
    m_config.matchMethod = ui.matchCombo->currentIndex();
    m_config.ransacThreshold = ui.ransacSpinBox->value();
    m_config.blendMethod = ui.blendCombo->currentIndex();
    m_config.blendWidth = ui.blendWidthSpinBox->value();
    m_config.useGeoCoordinates = ui.geoCoordinateCheckBox->isChecked();
    m_config.enableRPCMode = ui.rpcModeCheckBox->isChecked();
    m_config.rpcHeight = ui.rpcHeightSpinBox->value();
    m_config.rpcOutputResolution = ui.rpcResolutionSpinBox->value();
}

void StitchDialog::appendLog(const QString &message)
{
    QString timestamp = QDateTime::currentDateTime().toString("[hh:mm:ss] ");
    ui.logTextEdit->append(timestamp + message);
}

bool StitchDialog::loadImage1()
{
    if (m_img1Path.isEmpty()) return false;
    bool ok = loadImageFromFile(m_img1Path, m_image1);
    if (ok)
    {
        appendLog(QString::fromUtf8("左/上影像加载成功: %1 x %2 x %3波段")
            .arg(m_image1.width).arg(m_image1.height).arg(m_image1.bands));

        m_geoMeta1 = loadImageGeoMetadataWithRPC(m_img1Path);
        if (m_geoMeta1.valid)
        {
            m_geoMeta1.imageWidth = m_image1.width;
            m_geoMeta1.imageHeight = m_image1.height;
        }
        if (m_geoMeta1.hasRPC)
        {
            m_rpcModel1 = m_geoMeta1.rpcModel;
            appendLog(QString::fromUtf8("  嵌入RPC: 已从影像元数据加载"));
            ui.img1RpcPathEdit->setText(QString::fromUtf8("(从影像元数据嵌入)"));
        }
        else
        {
            m_rpcModel1 = findAndLoadRPCModel(m_img1Path);
            if (m_rpcModel1.valid)
            {
                appendLog(QString::fromUtf8("  外部RPC: 已加载 %1").arg(m_rpcModel1.sourceFilePath()));
                ui.img1RpcPathEdit->setText(m_rpcModel1.sourceFilePath());
            }
            else
            {
                appendLog(QString::fromUtf8("  RPC: 未找到RPC文件 (搜索范围: *.RPB/*.RPC/*.rpc/*_RPC.TXT/*.txt, rpc/子目录)"));
                ui.img1RpcPathEdit->clear();
            }
        }
    }
    else
    {
        appendLog(QString::fromUtf8("左/上影像加载失败: %1").arg(m_img1Path));
    }
    updatePreviewTabs();
    updateGeoInfoLabel();
    return ok;
}

bool StitchDialog::loadImage2()
{
    if (m_img2Path.isEmpty()) return false;
    bool ok = loadImageFromFile(m_img2Path, m_image2);
    if (ok)
    {
        appendLog(QString::fromUtf8("右/下影像加载成功: %1 x %2 x %3波段")
            .arg(m_image2.width).arg(m_image2.height).arg(m_image2.bands));

        m_geoMeta2 = loadImageGeoMetadataWithRPC(m_img2Path);
        if (m_geoMeta2.valid)
        {
            m_geoMeta2.imageWidth = m_image2.width;
            m_geoMeta2.imageHeight = m_image2.height;
        }
        if (m_geoMeta2.hasRPC)
        {
            m_rpcModel2 = m_geoMeta2.rpcModel;
            appendLog(QString::fromUtf8("  嵌入RPC: 已从影像元数据加载"));
            ui.img2RpcPathEdit->setText(QString::fromUtf8("(从影像元数据嵌入)"));
        }
        else
        {
            m_rpcModel2 = findAndLoadRPCModel(m_img2Path);
            if (m_rpcModel2.valid)
            {
                appendLog(QString::fromUtf8("  外部RPC: 已加载 %1").arg(m_rpcModel2.sourceFilePath()));
                ui.img2RpcPathEdit->setText(m_rpcModel2.sourceFilePath());
            }
            else
            {
                appendLog(QString::fromUtf8("  RPC: 未找到RPC文件 (搜索范围: *.RPB/*.RPC/*.rpc/*_RPC.TXT/*.txt, rpc/子目录)"));
                ui.img2RpcPathEdit->clear();
            }
        }
    }
    else
    {
        appendLog(QString::fromUtf8("右/下影像加载失败: %1").arg(m_img2Path));
    }
    updatePreviewTabs();
    updateGeoInfoLabel();
    return ok;
}

bool StitchDialog::loadImageFromFile(const QString &path, MultiBandImage &image)
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
                    image.pixel(x, y, 0) = static_cast<double>(line[x]);
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
                    image.pixel(x, y, 0) = static_cast<double>(line[x]);
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
                    image.pixel(x, y, 0) = static_cast<double>(qRed(pixel));
                    image.pixel(x, y, 1) = static_cast<double>(qGreen(pixel));
                    image.pixel(x, y, 2) = static_cast<double>(qBlue(pixel));
                }
                if (channels >= 4)
                    image.pixel(x, y, 3) = static_cast<double>(qAlpha(pixel));
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
                    image.pixel(x, y, 0) = static_cast<double>(line[x]);
            }
            return true;
        }

        img = img.convertToFormat(QImage::Format_RGB32);
        image.create(img.width(), img.height(), 1);
        for (int y = 0; y < img.height(); ++y)
        {
            const QRgb *line = reinterpret_cast<const QRgb *>(img.constScanLine(y));
            for (int x = 0; x < img.width(); ++x)
                image.pixel(x, y, 0) = static_cast<double>(qGray(line[x]));
        }
        return true;
    }

    return false;
}

void StitchDialog::onBrowseImage1()
{
    QStringList filters;
    filters << QString::fromUtf8("影像文件 (*.tif *.tiff *.jpg *.jpeg *.png *.bmp *.jp2 *.j2k *.dat *.img)");
    filters << QString::fromUtf8("TIFF文件 (*.tif *.tiff)");
    filters << QString::fromUtf8("JPEG2000文件 (*.jp2 *.j2k)");
    filters << QString::fromUtf8("所有文件 (*.*)");

    QString path = QFileDialog::getOpenFileName(this,
        QString::fromUtf8("选择左/上影像"), QString(), filters.join(";;"));

    if (!path.isEmpty())
        setImagePath(0, path);
}

void StitchDialog::onBrowseImage2()
{
    QStringList filters;
    filters << QString::fromUtf8("影像文件 (*.tif *.tiff *.jpg *.jpeg *.png *.bmp *.jp2 *.j2k *.dat *.img)");
    filters << QString::fromUtf8("TIFF文件 (*.tif *.tiff)");
    filters << QString::fromUtf8("JPEG2000文件 (*.jp2 *.j2k)");
    filters << QString::fromUtf8("所有文件 (*.*)");

    QString path = QFileDialog::getOpenFileName(this,
        QString::fromUtf8("选择右/下影像"), QString(), filters.join(";;"));

    if (!path.isEmpty())
        setImagePath(1, path);
}

void StitchDialog::onBrowseOutput()
{
    QString defaultDir = m_outputPath.isEmpty()
        ? (m_img1Path.isEmpty() ? QString() : QFileInfo(m_img1Path).absolutePath())
        : QFileInfo(m_outputPath).absolutePath();

    QString defaultName = "stitched_result.tif";
    if (!m_img1Path.isEmpty())
    {
        QFileInfo fi(m_img1Path);
        defaultName = fi.completeBaseName() + "_stitched.tif";
    }

    QString path = QFileDialog::getSaveFileName(this,
        QString::fromUtf8("选择拼接结果保存路径"),
        defaultDir.isEmpty() ? defaultName : defaultDir + "/" + defaultName,
        QString::fromUtf8("TIFF文件 (*.tif *.tiff);;JPEG文件 (*.jpg *.jpeg);;PNG文件 (*.png);;所有文件 (*.*)"));

    if (!path.isEmpty())
    {
        setOutputPath(path);
    }
}

void StitchDialog::onAlgorithmChanged(int index)
{
    collectConfig();
    appendLog(QString::fromUtf8("切换算法: %1")
        .arg(index == 0 ? "传统拼接" : "深度学习拼接"));
}

void StitchDialog::onExecuteStitch()
{
    if (!m_image1.isValid())
    {
        QMessageBox::warning(this, QString::fromUtf8("错误"),
            QString::fromUtf8("请先加载左/上影像。"));
        return;
    }

    if (!m_image2.isValid())
    {
        QMessageBox::warning(this, QString::fromUtf8("错误"),
            QString::fromUtf8("请先加载右/下影像。"));
        return;
    }

    collectConfig();

    appendLog(QString::fromUtf8("开始执行影像拼接..."));
    ui.progressBar->setValue(10);
    ui.resultTabWidget->setCurrentIndex(1);

    bool useGeo = m_config.useGeoCoordinates;
    bool useRPC = m_config.enableRPCMode;

    if (useRPC)
    {
        appendLog(QString::fromUtf8("算法: RPC有理多项式精确拼接, 融合: %1, 高程: %2m")
            .arg(m_config.blendMethod == 0 ? "线性渐变" : (m_config.blendMethod == 1 ? "简单平均" : "多频带"))
            .arg(m_config.rpcHeight, 0, 'f', 1));

        std::string warning;
        if (!validateRPCMetadataForStitch(m_rpcModel1, m_rpcModel2, warning))
        {
            appendLog(QString::fromUtf8("RPC验证失败: %1").arg(QString::fromStdString(warning)));
            QMessageBox::warning(this, QString::fromUtf8("RPC拼接错误"),
                QString::fromUtf8("RPC元数据验证失败:\n%1").arg(QString::fromStdString(warning)));
            emit stitchFailed(QString::fromUtf8("RPC元数据无效"));
            return;
        }

        if (!warning.empty())
        {
            appendLog(QString::fromUtf8("警告: %1").arg(QString::fromStdString(warning)));
        }

        double minLon, maxLon, minLat, maxLat;
        m_rpcModel1.computeGeoBounds(m_config.rpcHeight, minLon, maxLon, minLat, maxLat);
        appendLog(QString::fromUtf8("影像1地理范围: 经度[%1, %2], 纬度[%3, %4]")
            .arg(minLon, 0, 'f', 6).arg(maxLon, 0, 'f', 6)
            .arg(minLat, 0, 'f', 6).arg(maxLat, 0, 'f', 6));

        m_rpcModel2.computeGeoBounds(m_config.rpcHeight, minLon, maxLon, minLat, maxLat);
        appendLog(QString::fromUtf8("影像2地理范围: 经度[%1, %2], 纬度[%3, %4]")
            .arg(minLon, 0, 'f', 6).arg(maxLon, 0, 'f', 6)
            .arg(minLat, 0, 'f', 6).arg(maxLat, 0, 'f', 6));
    }
    else if (!useGeo)
    {
        QString algoName = m_config.algorithmType == 0 ? "传统拼接" : "深度学习拼接";
        appendLog(QString::fromUtf8("算法: %1, 特征: %2, 融合: %3")
            .arg(algoName)
            .arg(m_config.featureDetector == 0 ? "SIFT-like" : (m_config.featureDetector == 1 ? "FAST" : "Harris"))
            .arg(m_config.blendMethod == 0 ? "线性渐变" : (m_config.blendMethod == 1 ? "简单平均" : "多频带")));
    }
    else
    {
        appendLog(QString::fromUtf8("算法: 地理坐标精确拼接, 融合: %1")
            .arg(m_config.blendMethod == 0 ? "线性渐变" : (m_config.blendMethod == 1 ? "简单平均" : "多频带")));

        std::string warning;
        if (!validateGeoMetadataForStitch(m_geoMeta1, m_geoMeta2, warning))
        {
            appendLog(QString::fromUtf8("地理坐标验证失败: %1").arg(QString::fromStdString(warning)));
            QMessageBox::warning(this, QString::fromUtf8("地理坐标拼接错误"),
                QString::fromUtf8("地理坐标元数据验证失败:\n%1").arg(QString::fromStdString(warning)));
            emit stitchFailed(QString::fromUtf8("地理坐标元数据无效"));
            return;
        }

        if (!warning.empty())
        {
            appendLog(QString::fromUtf8("警告: %1").arg(QString::fromStdString(warning)));
        }
    }

    ui.progressBar->setValue(20);
    QApplication::processEvents();

    double elapsedMs = 0.0;
    if (useRPC)
    {
        m_resultImage = stitchImagesRPC(m_image1, m_image2, m_rpcModel1, m_rpcModel2,
                                         m_config, &m_outputMetadata, &m_validationResult, &elapsedMs);
    }
    else if (useGeo)
    {
        m_resultImage = stitchImagesGeo(m_image1, m_image2, m_geoMeta1, m_geoMeta2,
                                         m_config, &m_resultGeoMeta, &elapsedMs);
    }
    else
    {
        m_resultImage = stitchImages(m_image1, m_image2, m_config, &elapsedMs);
    }

    ui.progressBar->setValue(80);
    QApplication::processEvents();

    if (!m_resultImage.isValid())
    {
        appendLog(QString::fromUtf8("拼接失败，请检查输入数据。"));
        emit stitchFailed(QString::fromUtf8("拼接算法执行失败"));
        return;
    }

    appendLog(QString::fromUtf8("拼接完成: %1 x %2 x %3波段, 耗时 %4 ms")
        .arg(m_resultImage.width).arg(m_resultImage.height)
        .arg(m_resultImage.bands).arg(elapsedMs, 0, 'f', 1));

    if (useGeo && m_resultGeoMeta.valid)
    {
        appendLog(QString::fromUtf8("输出地理坐标: 原点(%1, %2), 分辨率(%3, %4), EPSG:%5")
            .arg(m_resultGeoMeta.geoTransform.originX(), 0, 'f', 6)
            .arg(m_resultGeoMeta.geoTransform.originY(), 0, 'f', 6)
            .arg(m_resultGeoMeta.pixelWidth(), 0, 'f', 6)
            .arg(m_resultGeoMeta.pixelHeight(), 0, 'f', 6)
            .arg(m_resultGeoMeta.epsgCode));
    }

    if (useRPC && m_outputMetadata.hasRPC)
    {
        appendLog(QString::fromUtf8("输出RPC地理范围: 经度[%1, %2], 纬度[%3, %4], 尺寸:%5x%6")
            .arg(m_outputMetadata.minLon, 0, 'f', 6).arg(m_outputMetadata.maxLon, 0, 'f', 6)
            .arg(m_outputMetadata.minLat, 0, 'f', 6).arg(m_outputMetadata.maxLat, 0, 'f', 6)
            .arg(m_outputMetadata.outputWidth).arg(m_outputMetadata.outputHeight));
        appendValidationReport(m_validationResult);
    }

    ui.progressBar->setValue(60);
    QApplication::processEvents();

    appendLog(QString::fromUtf8("正在验证拼接图像完整性..."));

    int w1 = m_image1.width, h1 = m_image1.height;
    int w2 = m_image2.width, h2 = m_image2.height;
    int offsetX = 0, offsetY = 0;
    m_integrityReport = validateStitchIntegrity(m_resultImage, w1, h1, w2, h2, offsetX, offsetY);

    appendLog(QString::fromStdString(m_integrityReport.summary));
    if (!m_integrityReport.passed)
    {
        appendLog(QString::fromUtf8("警告: 拼接结果完整性检查未通过，可能存在拼接错位或内容缺失"));
    }

    ui.progressBar->setValue(75);
    QApplication::processEvents();

    ui.resultTabWidget->setCurrentIndex(0);
    ui.previewSubTab->setCurrentIndex(2);
    QApplication::processEvents();

    displayResultImage();

    ui.saveBtn->setEnabled(true);

    if (!m_outputPath.isEmpty())
    {
        QImage outputImage;
        if (m_resultImage.bands >= 3)
            outputImage = m_resultImage.toQImageRGB();
        else
            outputImage = m_resultImage.toQImageGrayscale();

        m_outputSaved = outputImage.save(m_outputPath);
        if (m_outputSaved)
            appendLog(QString::fromUtf8("拼接结果已自动保存: %1").arg(m_outputPath));
        else
            appendLog(QString::fromUtf8("拼接结果自动保存失败: %1").arg(m_outputPath));
    }

    ui.progressBar->setValue(100);

    emit stitchCompleted(m_resultImage);
}

void StitchDialog::displayResultImage()
{
    m_resultScene->clear();

    QImage stitchedImg;
    if (m_resultImage.bands >= 3)
        stitchedImg = m_resultImage.toQImageRGB();
    else
        stitchedImg = m_resultImage.toQImageGrayscale();

    if (stitchedImg.isNull()) return;

    QPixmap pix = QPixmap::fromImage(stitchedImg);
    if (pix.isNull()) return;

    m_resultScene->addPixmap(pix);
    m_resultScene->setSceneRect(stitchedImg.rect());
    ui.resultPreviewView->fitInView(m_resultScene->sceneRect(), Qt::KeepAspectRatio);

    QTimer::singleShot(0, this, [this]() {
        if (m_resultScene && !m_resultScene->sceneRect().isEmpty()
            && ui.resultPreviewView->viewport()->width() > 0
            && ui.resultPreviewView->viewport()->height() > 0)
        {
            ui.resultPreviewView->fitInView(m_resultScene->sceneRect(), Qt::KeepAspectRatio);
            updateZoomLabel(ui.resultPreviewView);
        }
    });
}

void StitchDialog::onSaveResult()
{
    if (!m_resultImage.isValid())
    {
        QMessageBox::warning(this, QString::fromUtf8("提示"),
            QString::fromUtf8("没有可保存的拼接结果，请先执行拼接操作。"));
        return;
    }

    QString defaultDir = m_outputPath.isEmpty()
        ? (m_img1Path.isEmpty() ? QString() : QFileInfo(m_img1Path).absolutePath())
        : QFileInfo(m_outputPath).absolutePath();

    QString defaultName = "stitched_result.tif";
    if (!m_img1Path.isEmpty())
    {
        QFileInfo fi(m_img1Path);
        defaultName = fi.completeBaseName() + "_stitched.tif";
    }

    QStringList filters;
    filters << QString::fromUtf8("TIFF文件 (*.tif *.tiff)");
    filters << QString::fromUtf8("PNG文件 (*.png)");
    filters << QString::fromUtf8("JPEG文件 (*.jpg *.jpeg)");
    filters << QString::fromUtf8("BMP文件 (*.bmp)");

    QString selectedFilter = filters.first();
    QString filePath = QFileDialog::getSaveFileName(this,
        QString::fromUtf8("保存拼接结果"),
        defaultDir.isEmpty() ? defaultName : defaultDir + "/" + defaultName,
        filters.join(";;"), &selectedFilter);

    if (filePath.isEmpty()) return;

    QImage outputImage;
    if (m_resultImage.bands >= 3)
        outputImage = m_resultImage.toQImageRGB();
    else
        outputImage = m_resultImage.toQImageGrayscale();

    if (outputImage.isNull())
    {
        QMessageBox::critical(this, QString::fromUtf8("错误"),
            QString::fromUtf8("无法生成输出图像。"));
        appendLog(QString::fromUtf8("保存失败: 图像生成错误"));
        return;
    }

    const char* format = nullptr;
    if (selectedFilter.contains("TIFF") || filePath.endsWith(".tif", Qt::CaseInsensitive) || filePath.endsWith(".tiff", Qt::CaseInsensitive))
        format = "TIFF";
    else if (selectedFilter.contains("PNG") || filePath.endsWith(".png", Qt::CaseInsensitive))
        format = "PNG";
    else if (selectedFilter.contains("JPEG") || filePath.endsWith(".jpg", Qt::CaseInsensitive) || filePath.endsWith(".jpeg", Qt::CaseInsensitive))
        format = "JPEG";
    else if (selectedFilter.contains("BMP") || filePath.endsWith(".bmp", Qt::CaseInsensitive))
        format = "BMP";

    bool saved = outputImage.save(filePath, format, 95);
    if (saved)
    {
        m_outputSaved = true;
        appendLog(QString::fromUtf8("拼接结果已保存: %1 (%2x%3, 格式:%4)")
            .arg(filePath).arg(outputImage.width()).arg(outputImage.height())
            .arg(format ? format : "自动"));
    }
    else
    {
        QMessageBox::critical(this, QString::fromUtf8("错误"),
            QString::fromUtf8("保存失败，请检查磁盘空间和文件路径。"));
        appendLog(QString::fromUtf8("保存失败: %1").arg(filePath));
    }
}

void StitchDialog::onGeoCoordinateToggled(bool checked)
{
    collectConfig();
    if (checked)
    {
        appendLog(QString::fromUtf8("已启用地理坐标精确拼接模式"));
        if (!m_geoMeta1.valid || !m_geoMeta2.valid)
        {
            appendLog(QString::fromUtf8("注意: 部分影像缺少地理坐标元数据，请重新加载影像"));
        }
    }
    else
    {
        appendLog(QString::fromUtf8("已切换到特征匹配拼接模式"));
    }
    updateGeoInfoLabel();
}

void StitchDialog::updateGeoInfoLabel()
{
    QStringList info;

    if (m_geoMeta1.valid)
    {
        info << QString::fromUtf8(
            "左/上影像 | 原点(%1, %2) | 分辨率(%3, %4) | EPSG:%5 | 尺寸:%6x%7")
            .arg(m_geoMeta1.geoTransform.originX(), 0, 'f', 3)
            .arg(m_geoMeta1.geoTransform.originY(), 0, 'f', 3)
            .arg(m_geoMeta1.pixelWidth(), 0, 'f', 6)
            .arg(m_geoMeta1.pixelHeight(), 0, 'f', 6)
            .arg(m_geoMeta1.epsgCode > 0 ? QString::number(m_geoMeta1.epsgCode) : "未知")
            .arg(m_geoMeta1.imageWidth)
            .arg(m_geoMeta1.imageHeight);
    }
    else
    {
        info << QString::fromUtf8("左/上影像: 无地理坐标信息");
    }

    if (m_geoMeta2.valid)
    {
        info << QString::fromUtf8(
            "右/下影像 | 原点(%1, %2) | 分辨率(%3, %4) | EPSG:%5 | 尺寸:%6x%7")
            .arg(m_geoMeta2.geoTransform.originX(), 0, 'f', 3)
            .arg(m_geoMeta2.geoTransform.originY(), 0, 'f', 3)
            .arg(m_geoMeta2.pixelWidth(), 0, 'f', 6)
            .arg(m_geoMeta2.pixelHeight(), 0, 'f', 6)
            .arg(m_geoMeta2.epsgCode > 0 ? QString::number(m_geoMeta2.epsgCode) : "未知")
            .arg(m_geoMeta2.imageWidth)
            .arg(m_geoMeta2.imageHeight);
    }
    else
    {
        info << QString::fromUtf8("右/下影像: 无地理坐标信息");
    }

    if (m_geoMeta1.valid && m_geoMeta2.valid)
    {
        if (m_geoMeta1.epsgCode > 0 && m_geoMeta2.epsgCode > 0 &&
            m_geoMeta1.epsgCode != m_geoMeta2.epsgCode)
        {
            info << QString::fromUtf8("警告: 两幅影像使用不同的坐标参考系统 (EPSG:%1 vs EPSG:%2)")
                .arg(m_geoMeta1.epsgCode).arg(m_geoMeta2.epsgCode);
        }
    }

    if (m_rpcModel1.valid)
    {
        double minLon, maxLon, minLat, maxLat;
        m_rpcModel1.computeGeoBounds(m_config.rpcHeight, minLon, maxLon, minLat, maxLat);
        info << QString::fromUtf8(
            "RPC(左/上) | 经度[%1~%2] | 纬度[%3~%4] | %5x%6")
            .arg(minLon, 0, 'f', 6).arg(maxLon, 0, 'f', 6)
            .arg(minLat, 0, 'f', 6).arg(maxLat, 0, 'f', 6)
            .arg(m_rpcModel1.imageWidth).arg(m_rpcModel1.imageHeight);
    }

    if (m_rpcModel2.valid)
    {
        double minLon, maxLon, minLat, maxLat;
        m_rpcModel2.computeGeoBounds(m_config.rpcHeight, minLon, maxLon, minLat, maxLat);
        info << QString::fromUtf8(
            "RPC(右/下) | 经度[%1~%2] | 纬度[%3~%4] | %5x%6")
            .arg(minLon, 0, 'f', 6).arg(maxLon, 0, 'f', 6)
            .arg(minLat, 0, 'f', 6).arg(maxLat, 0, 'f', 6)
            .arg(m_rpcModel2.imageWidth).arg(m_rpcModel2.imageHeight);
    }

    ui.geoInfoLabel->setText(info.join("\n"));
}

void StitchDialog::loadRPCMetadata()
{
    m_rpcModel1 = findAndLoadRPCModel(m_img1Path);
    m_rpcModel2 = findAndLoadRPCModel(m_img2Path);

    if (m_rpcModel1.valid)
    {
        appendLog(QString::fromUtf8("RPC刷新: 影像1已加载 %1").arg(m_rpcModel1.sourceFilePath()));
        ui.img1RpcPathEdit->setText(m_rpcModel1.sourceFilePath());
    }
    else
    {
        ui.img1RpcPathEdit->clear();
    }
    if (m_rpcModel2.valid)
    {
        appendLog(QString::fromUtf8("RPC刷新: 影像2已加载 %1").arg(m_rpcModel2.sourceFilePath()));
        ui.img2RpcPathEdit->setText(m_rpcModel2.sourceFilePath());
    }
    else
    {
        ui.img2RpcPathEdit->clear();
    }
}

void StitchDialog::onRPCModeToggled(bool checked)
{
    collectConfig();
    if (checked)
    {
        appendLog(QString::fromUtf8("已启用RPC有理多项式精确拼接模式"));
        if (!m_rpcModel1.valid && !m_rpcModel2.valid)
        {
            appendLog(QString::fromUtf8("注意: 未检测到RPC参数文件，请确保影像目录中包含.RPB/.RPC/_RPC.TXT文件"));
        }
        if (ui.geoCoordinateCheckBox->isChecked())
        {
            ui.geoCoordinateCheckBox->setChecked(false);
        }
    }
    else
    {
        appendLog(QString::fromUtf8("已关闭RPC拼接模式"));
    }
    updateGeoInfoLabel();
}

void StitchDialog::appendValidationReport(const StitchValidationResult& result)
{
    if (result.samplePointCount <= 0)
    {
        appendLog(QString::fromUtf8("精度验证: 无有效样本点"));
        return;
    }

    appendLog(QString::fromUtf8("=== 拼接精度验证报告 ==="));
    appendLog(QString::fromStdString(result.toReportSummary()));
    appendLog(QString::fromUtf8("样本点坐标对详情:"));

    for (int i = 0; i < result.samplePointCount && i < 10; ++i)
    {
        appendLog(QString::fromUtf8("  #%1 行%2列%3(%.6f,%.6f) <-> 行%4列%5(%.6f,%.6f) 距离:%6m")
            .arg(i + 1)
            .arg(result.point1Line[i], 0, 'f', 1).arg(result.point1Samp[i], 0, 'f', 1)
            .arg(result.point1Lon[i], 0, 'f', 6).arg(result.point1Lat[i], 0, 'f', 6)
            .arg(result.point2Line[i], 0, 'f', 1).arg(result.point2Samp[i], 0, 'f', 1)
            .arg(result.point2Lon[i], 0, 'f', 6).arg(result.point2Lat[i], 0, 'f', 6)
            .arg(result.geoDistanceMeters[i], 0, 'f', 2));
    }

    if (result.samplePointCount > 10)
    {
        appendLog(QString::fromUtf8("  ... (共%1个样本点)").arg(result.samplePointCount));
    }

    if (result.passed)
        appendLog(QString::fromUtf8("精度验证: 通过 (RMS: %1m <= 阈值: %2m)")
            .arg(result.rmsErrorMeters, 0, 'f', 3).arg(result.thresholdMeters, 0, 'f', 1));
    else
        appendLog(QString::fromUtf8("精度验证: 未通过 (RMS: %1m > 阈值: %2m)")
            .arg(result.rmsErrorMeters, 0, 'f', 3).arg(result.thresholdMeters, 0, 'f', 1));
}

void StitchDialog::onBrowseRpc1()
{
    QString filePath = QFileDialog::getOpenFileName(this,
        QString::fromUtf8("选择影像1的RPC参数文件"),
        m_img1Path.isEmpty() ? QString() : QFileInfo(m_img1Path).absolutePath(),
        QString::fromUtf8(
            "RPC文件 (*.RPB *.rpb *.RPC *.rpc *.TXT *.txt);;"
            "所有RPC (*.RPB *.rpb *.RPC *.rpc *_RPC.TXT *_rpc.txt *rpc*.txt);;"
            "所有文件 (*.*)"));

    if (filePath.isEmpty())
        return;

    RPCModel model = parseRPCFile(filePath);
    if (model.valid)
    {
        model.setSourceFilePath(filePath);
        m_rpcModel1 = model;
        ui.img1RpcPathEdit->setText(filePath);
        appendLog(QString::fromUtf8("手动加载影像1 RPC: %1").arg(filePath));
        updateGeoInfoLabel();
    }
    else
    {
        QMessageBox::warning(this, QString::fromUtf8("RPC解析失败"),
            QString::fromUtf8("无法解析RPC文件:\n%1\n\n请确认文件包含有效的LINE_OFF/SAMP_OFF等归一化参数和至少一组多项式系数。").arg(filePath));
        appendLog(QString::fromUtf8("RPC解析失败: %1 (格式不识别)").arg(filePath));
    }
}

void StitchDialog::onBrowseRpc2()
{
    QString filePath = QFileDialog::getOpenFileName(this,
        QString::fromUtf8("选择影像2的RPC参数文件"),
        m_img2Path.isEmpty() ? QString() : QFileInfo(m_img2Path).absolutePath(),
        QString::fromUtf8(
            "RPC文件 (*.RPB *.rpb *.RPC *.rpc *.TXT *.txt);;"
            "所有RPC (*.RPB *.rpb *.RPC *.rpc *_RPC.TXT *_rpc.txt *rpc*.txt);;"
            "所有文件 (*.*)"));

    if (filePath.isEmpty())
        return;

    RPCModel model = parseRPCFile(filePath);
    if (model.valid)
    {
        model.setSourceFilePath(filePath);
        m_rpcModel2 = model;
        ui.img2RpcPathEdit->setText(filePath);
        appendLog(QString::fromUtf8("手动加载影像2 RPC: %1").arg(filePath));
        updateGeoInfoLabel();
    }
    else
    {
        QMessageBox::warning(this, QString::fromUtf8("RPC解析失败"),
            QString::fromUtf8("无法解析RPC文件:\n%1\n\n请确认文件包含有效的LINE_OFF/SAMP_OFF等归一化参数和至少一组多项式系数。").arg(filePath));
        appendLog(QString::fromUtf8("RPC解析失败: %1 (格式不识别)").arg(filePath));
    }
}
