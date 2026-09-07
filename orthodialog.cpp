// ============================================================================
// 文件: orthodialog.cpp
// 功能: 正射校正对话框 — 基于 GDALWarp 的 RPC+DEM 正射校正
//
// 核心流程:
//   1. 用户选择含 RPC 的卫星影像和可选的 DEM
//   2. 配置输出参数 (坐标系、分辨率、重采样方法、数据类型、压缩)
//   3. GDALAutoCreateWarpedVRT 创建正射校正 VRT
//      - 自动处理 RPC 有理多项式模型 → 地理坐标
//      - DEM 高程校正消除地形引起的投影差
//      - 输出投影变换到用户指定的坐标系
//   4. CreateCopy 写入压缩的 GeoTIFF
//
// 支持的插值方法: 最近邻、双线性、三次卷积、三次样条、Lanczos
// ============================================================================

#include "orthodialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QFileDialog>
#include <QMessageBox>
#include <QApplication>
#include <QDebug>
#include <algorithm>
#include <cmath>

#include "gdal_priv.h"
#include "gdalwarper.h"
#include "cpl_conv.h"
#include "cpl_string.h"
#include "ogr_srs_api.h"

// ============================================================================
// 构造函数 — UI 布局（输入输出组、输出参数组、进度条、执行按钮）
// 界面分组：
//   输入/输出组：源影像、DEM、输出路径
//   输出参数组：坐标系(EPSG)、分辨率、重采样方法、数据类型、压缩选项
//   进度条和底部执行按钮
// ============================================================================
OrthoDialog::OrthoDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(QString::fromUtf8("正射校正 (RPC + DEM)"));
    setMinimumSize(650, 520);

    auto* mainLayout = new QVBoxLayout(this);

    // ---- 输入/输出组 ----
    auto* ioGroup = new QGroupBox(QString::fromUtf8("输入/输出"));
    auto* ioForm  = new QFormLayout(ioGroup);

    auto* inputRow = new QHBoxLayout();
    m_inputEdit = new QLineEdit();
    m_inputEdit->setPlaceholderText(QString::fromUtf8("选择待正射校正的卫星影像 (含RPC信息)"));
    auto* btnInput = new QPushButton(QString::fromUtf8("浏览..."));
    connect(btnInput, &QPushButton::clicked, this, &OrthoDialog::onBrowseInput);
    inputRow->addWidget(m_inputEdit, 1);
    inputRow->addWidget(btnInput);
    ioForm->addRow(QString::fromUtf8("输入影像:"), inputRow);

    m_useSrcChk = new QCheckBox(QString::fromUtf8("使用当前已加载的源影像"));
    ioForm->addRow("", m_useSrcChk);

    auto* demRow = new QHBoxLayout();
    m_demEdit = new QLineEdit();
    m_demEdit->setPlaceholderText(QString::fromUtf8("选择DEM数字高程模型 (强烈推荐, 消除地形误差)"));
    auto* btnDEM = new QPushButton(QString::fromUtf8("浏览..."));
    connect(btnDEM, &QPushButton::clicked, this, &OrthoDialog::onBrowseDEM);
    demRow->addWidget(m_demEdit, 1);
    demRow->addWidget(btnDEM);
    ioForm->addRow(QString::fromUtf8("DEM文件:"), demRow);

    auto* outRow = new QHBoxLayout();
    m_outputEdit = new QLineEdit();
    m_outputEdit->setPlaceholderText(QString::fromUtf8("选择输出路径和文件名"));
    auto* btnOut = new QPushButton(QString::fromUtf8("浏览..."));
    connect(btnOut, &QPushButton::clicked, this, &OrthoDialog::onBrowseOutput);
    outRow->addWidget(m_outputEdit, 1);
    outRow->addWidget(btnOut);
    ioForm->addRow(QString::fromUtf8("输出影像:"), outRow);

    mainLayout->addWidget(ioGroup);

    // ---- 输出参数组 ----
    auto* paramGroup = new QGroupBox(QString::fromUtf8("输出参数"));
    auto* paramForm  = new QFormLayout(paramGroup);

    // 输出坐标系
    m_epsgEdit = new QLineEdit("4326");
    m_epsgEdit->setToolTip(QString::fromUtf8("目标坐标系EPSG代码\n4326=WGS84地理坐标, 3857=Web墨卡托\n"
        "或输入形如 '+proj=utm +zone=50 +datum=WGS84' 的PROJ字符串"));
    paramForm->addRow(QString::fromUtf8("输出坐标系(EPSG):"), m_epsgEdit);

    // 输出分辨率
    m_resSpin = new QDoubleSpinBox();
    m_resSpin->setRange(0.0001, 100000.0);
    m_resSpin->setDecimals(6);
    m_resSpin->setValue(0.0);   // 0 = 自动使用源影像分辨率
    m_resSpin->setToolTip(QString::fromUtf8("输出像素分辨率。\n"
        "地理坐标系(4326): 单位=度, 典型值 0.00001~0.001\n"
        "投影坐标系(UTM等): 单位=米, 典型值 0.3~30\n"
        "设为0则自动计算"));
    auto* resRow = new QHBoxLayout();
    resRow->addWidget(m_resSpin, 1);
    auto* autoResBtn = new QPushButton(QString::fromUtf8("自动"));
    connect(autoResBtn, &QPushButton::clicked, this, [this]() { m_resSpin->setValue(0.0); });
    resRow->addWidget(autoResBtn);
    paramForm->addRow(QString::fromUtf8("输出分辨率:"), resRow);

    // 重采样方法
    m_resampleCombo = new QComboBox();
    m_resampleCombo->addItem(QString::fromUtf8("最近邻 (最快, 适用于分类图)"),     static_cast<int>(GRA_NearestNeighbour));
    m_resampleCombo->addItem(QString::fromUtf8("双线性 (较快, 适用于连续值)"),       static_cast<int>(GRA_Bilinear));
    m_resampleCombo->addItem(QString::fromUtf8("三次卷积 (高质量, 推荐)"),            static_cast<int>(GRA_Cubic));
    m_resampleCombo->addItem(QString::fromUtf8("三次样条 (更平滑)"),                  static_cast<int>(GRA_CubicSpline));
    m_resampleCombo->addItem(QString::fromUtf8("Lanczos (最高质量, 较慢)"),          static_cast<int>(GRA_Lanczos));
    m_resampleCombo->setCurrentIndex(2);  // 默认 Cubic
    paramForm->addRow(QString::fromUtf8("重采样方法:"), m_resampleCombo);

    // 输出数据类型
    m_dtypeCombo = new QComboBox();
    m_dtypeCombo->addItem(QString::fromUtf8("保持原始类型 (推荐)"), 0);
    m_dtypeCombo->addItem(QString::fromUtf8("Byte (0-255)"),       1);
    m_dtypeCombo->addItem(QString::fromUtf8("UInt16 (0-65535)"),   2);
    m_dtypeCombo->addItem(QString::fromUtf8("Int16"),               3);
    m_dtypeCombo->addItem(QString::fromUtf8("Float32"),             4);
    m_dtypeCombo->setCurrentIndex(0);
    paramForm->addRow(QString::fromUtf8("输出数据类型:"), m_dtypeCombo);

    // 压缩选项
    m_compressChk = new QCheckBox(QString::fromUtf8("LZW无损压缩 + 分块存储 (推荐)"));
    m_compressChk->setChecked(true);
    paramForm->addRow("", m_compressChk);

    mainLayout->addWidget(paramGroup);

    // ---- 进度条 ----
    m_progressBar = new QProgressBar();
    m_progressBar->setVisible(false);
    m_progressBar->setRange(0, 100);
    mainLayout->addWidget(m_progressBar);

    // ---- 底部按钮 ----
    auto* bottomLayout = new QHBoxLayout();
    m_statusLabel = new QLabel();
    bottomLayout->addWidget(m_statusLabel, 1);

    m_execBtn = new QPushButton(QString::fromUtf8("执行正射校正"));
    m_execBtn->setStyleSheet(
        "QPushButton { background-color: #0078D4; color: white; font-weight: bold; "
        "padding: 8px 20px; border-radius: 4px; }"
        "QPushButton:hover { background-color: #106EBE; }");
    m_execBtn->setMinimumHeight(36);
    connect(m_execBtn, &QPushButton::clicked, this, &OrthoDialog::onExecute);
    bottomLayout->addWidget(m_execBtn);
    mainLayout->addLayout(bottomLayout);
}

// ============================================================================
// 析构函数 — 关闭 GDAL 数据集
// ============================================================================
OrthoDialog::~OrthoDialog()
{
}

// ============================================================================
// setCurrentSourcePath — 设置当前源影像路径
// 当外部已加载源影像时调用此函数，自动填入路径编辑框
// ============================================================================
void OrthoDialog::setCurrentSourcePath(const QString& path)
{
    if (!path.isEmpty()) {
        m_useSrcChk->setChecked(true);
        m_inputEdit->setText(path);
    }
}

// ============================================================================
// onBrowseInput — 浏览选择输入影像文件
// 弹出文件对话框选择含 RPC 的卫星影像
// ============================================================================
void OrthoDialog::onBrowseInput()
{
    QString path = QFileDialog::getOpenFileName(this,
        QString::fromUtf8("选择待校正影像"), "",
        QString::fromUtf8("栅格影像 (*.tif *.tiff *.img *.ntf *.jp2);;所有文件 (*.*)"));
    if (path.isEmpty()) return;
    m_inputEdit->setText(path);
    m_useSrcChk->setChecked(false);
}

// ============================================================================
// onBrowseDEM — 浏览选择 DEM 文件
// ============================================================================
void OrthoDialog::onBrowseDEM()
{
    QString path = QFileDialog::getOpenFileName(this,
        QString::fromUtf8("选择DEM文件"), "",
        QString::fromUtf8("DEM文件 (*.tif *.tiff *.img *.dem);;所有文件 (*.*)"));
    if (!path.isEmpty()) m_demEdit->setText(path);
}

// ============================================================================
// onBrowseOutput — 浏览选择输出路径
// ============================================================================
void OrthoDialog::onBrowseOutput()
{
    QString path = QFileDialog::getSaveFileName(this,
        QString::fromUtf8("选择输出影像"), "",
        QString::fromUtf8("GeoTIFF (*.tif *.tiff);;所有文件 (*.*)"));
    if (!path.isEmpty()) m_outputEdit->setText(path);
}

// ============================================================================
// onExecute — 执行入口（验证输入 → 调用 executeWarp）
// 检查输入/输出路径是否有效，显示进度条并调用核心处理函数，
// 处理完成后显示成功或失败提示
// ============================================================================
void OrthoDialog::onExecute()
{
    QString inputPath = m_inputEdit->text();
    if (inputPath.isEmpty()) {
        QMessageBox::warning(this, QString::fromUtf8("提示"),
            QString::fromUtf8("请先选择输入影像"));
        return;
    }
    QString demPath   = m_demEdit->text();
    QString outputPath = m_outputEdit->text();

    if (outputPath.isEmpty()) {
        QMessageBox::warning(this, QString::fromUtf8("提示"),
            QString::fromUtf8("请选择输出路径"));
        return;
    }

    m_execBtn->setEnabled(false);
    m_progressBar->setVisible(true);
    m_progressBar->setValue(0);
    m_statusLabel->setText(QString::fromUtf8("正在执行正射校正..."));
    QApplication::processEvents();

    bool success = executeWarp(inputPath, demPath, outputPath);

    m_execBtn->setEnabled(true);
    m_progressBar->setVisible(false);

    if (success) {
        m_statusLabel->setText(QString::fromUtf8(
            "<span style='color:green'>正射校正完成!</span>"));
        QMessageBox::information(this, QString::fromUtf8("完成"),
            QString::fromUtf8("正射校正已成功完成!\n\n输出文件: %1\n\n"
                "建议: 在GIS软件中与标准DOM叠置检查精度。").arg(outputPath));
    }
}

// ============================================================================
// executeWarp — 核心处理函数（基于 GDALAutoCreateWarpedVRT + CreateCopy）
//
// 处理步骤:
//   1. 打开源影像，检查波段数和 RPC 信息
//   2. 解析输出坐标系（EPSG 代码或 WKT/PROJ 字符串）
//   3. 构建 WarpOptions — 配置 RPC/DEM/重采样方法/分辨率
//   4. GDALAutoCreateWarpedVRT 创建正射校正 VRT
//      - 自动处理 RPC 有理多项式模型 → 地理坐标
//      - DEM 高程校正消除地形引起的投影差
//      - 输出投影变换到用户指定的坐标系
//   5. CreateCopy 写入 LZW 压缩的 GeoTIFF（分块存储）
//   6. 关闭所有数据集，完成清理
// ============================================================================
bool OrthoDialog::executeWarp(const QString& inputPath, const QString& demPath,
                         const QString& outputPath)
{
    // ---- 1. 打开源影像 ----
    GDALDataset* srcDS = static_cast<GDALDataset*>(
        GDALOpenEx(inputPath.toUtf8().constData(), GDAL_OF_RASTER,
                    nullptr, nullptr, nullptr));
    if (!srcDS) {
        QMessageBox::warning(this, QString::fromUtf8("错误"),
            QString::fromUtf8("无法打开输入影像:\n%1\n\nGDAL错误: %2")
                .arg(inputPath, QString::fromUtf8(CPLGetLastErrorMsg())));
        return false;
    }

    int srcBands = srcDS->GetRasterCount();
    if (srcBands < 1) {
        GDALClose(srcDS);
        QMessageBox::warning(this, QString::fromUtf8("错误"),
            QString::fromUtf8("输入影像没有有效波段"));
        return false;
    }

    bool hasRPC = (srcDS->GetMetadata("RPC") != nullptr);

    // ---- 2. 确定输出坐标系 ----
    QString epsgText = m_epsgEdit->text().trimmed();
    OGRSpatialReference dstSRS;
    QByteArray dstWkt;

    bool isEpsg = false;
    int epsgCode = epsgText.toInt(&isEpsg);
    if (isEpsg && epsgCode > 0) {
        if (dstSRS.importFromEPSG(epsgCode) != OGRERR_NONE) {
            GDALClose(srcDS);
            QMessageBox::warning(this, QString::fromUtf8("错误"),
                QString::fromUtf8("无效的EPSG代码: %1\n请输入有效数字, 例如 4326").arg(epsgText));
            return false;
        }
    } else {
        char* tmp = CPLStrdup(epsgText.toUtf8().constData());
        if (dstSRS.importFromWkt(&tmp) != OGRERR_NONE &&
            dstSRS.importFromProj4(epsgText.toUtf8().constData()) != OGRERR_NONE) {
            CPLFree(tmp);
            GDALClose(srcDS);
            QMessageBox::warning(this, QString::fromUtf8("错误"),
                QString::fromUtf8("无法解析输出坐标系:\n%1\n\n"
                    "支持格式: EPSG数字代码 (如4326) 或 PROJ字符串").arg(epsgText));
            return false;
        }
        CPLFree(tmp);
    }

    char* dstWktRaw = nullptr;
    dstSRS.exportToWkt(&dstWktRaw);
    dstWkt = QByteArray(dstWktRaw);
    CPLFree(dstWktRaw);

    // ---- 3. 构建 VRT/Warp 选项 ----
    char** papszOptions = nullptr;

    if (hasRPC) {
        papszOptions = CSLSetNameValue(papszOptions, "SRC_METHOD", "RPC");
    }

    if (!demPath.isEmpty()) {
        papszOptions = CSLSetNameValue(papszOptions, "RPC_DEM",
                                        demPath.toUtf8().constData());
        papszOptions = CSLSetNameValue(papszOptions, "RPC_DEMINTERPOLATION", "bilinear");
    }

    // 重采样方法映射
    static const char* resampleNames[] = {
        "NEAREST", "BILINEAR", "CUBIC", "CUBICSPLINE", "LANCZOS"
    };
    int resampleIdx = m_resampleCombo->currentIndex();
    if (resampleIdx >= 0 && resampleIdx < 5)
        papszOptions = CSLSetNameValue(papszOptions, "RESAMPLING", resampleNames[resampleIdx]);

    // 用户分辨率覆盖
    double userRes = m_resSpin->value();
    if (userRes > 0.0) {
        QString resStr = QString::number(userRes, 'f', 10);
        papszOptions = CSLSetNameValue(papszOptions, "XRES", resStr.toUtf8().constData());
        papszOptions = CSLSetNameValue(papszOptions, "YRES", resStr.toUtf8().constData());
    }

    // ---- 4. 创建 Warped VRT (自动处理RPC+DEM+投影变换) ----
    m_progressBar->setValue(10);
    m_statusLabel->setText(QString::fromUtf8("正在创建变换模型..."));
    QApplication::processEvents();

    GDALWarpOptions* psWO = GDALCreateWarpOptions();
    psWO->papszWarpOptions = papszOptions;
    papszOptions = nullptr;

    GDALDataset* vrtDS = static_cast<GDALDataset*>(
        GDALAutoCreateWarpedVRT(srcDS, nullptr, dstWkt.constData(),
                                 static_cast<GDALResampleAlg>(
                                     m_resampleCombo->currentData().toInt()),
                                 0.125, psWO));
    GDALDestroyWarpOptions(psWO);

    if (!vrtDS) {
        GDALClose(srcDS);
        QMessageBox::warning(this, QString::fromUtf8("错误"),
            QString::fromUtf8("无法创建正射校正模型。\n请检查:\n"
                "  - 影像是否含有效RPC信息\n"
                "  - DEM覆盖范围是否与影像匹配\n"
                "  - 输出坐标系是否有效\n\nGDAL错误: %1")
                .arg(QString::fromUtf8(CPLGetLastErrorMsg())));
        return false;
    }

    // ---- 5. 输出到 GeoTIFF ----
    m_progressBar->setValue(20);
    m_statusLabel->setText(QString::fromUtf8("正在写入正射校正结果..."));
    QApplication::processEvents();

    GDALDriver* drv = GetGDALDriverManager()->GetDriverByName("GTiff");
    if (!drv) {
        GDALClose(vrtDS);
        GDALClose(srcDS);
        QMessageBox::warning(this, QString::fromUtf8("错误"),
            QString::fromUtf8("无法获取GeoTIFF驱动"));
        return false;
    }

    char** papszCreateOpts = nullptr;
    if (m_compressChk->isChecked()) {
        papszCreateOpts = CSLSetNameValue(papszCreateOpts, "COMPRESS", "LZW");
        papszCreateOpts = CSLSetNameValue(papszCreateOpts, "PREDICTOR", "2");
        papszCreateOpts = CSLSetNameValue(papszCreateOpts, "BIGTIFF", "IF_SAFER");
        papszCreateOpts = CSLSetNameValue(papszCreateOpts, "TILED", "YES");
        papszCreateOpts = CSLSetNameValue(papszCreateOpts, "BLOCKXSIZE", "256");
        papszCreateOpts = CSLSetNameValue(papszCreateOpts, "BLOCKYSIZE", "256");
        if (vrtDS->GetRasterCount() == 3)
            papszCreateOpts = CSLSetNameValue(papszCreateOpts, "PHOTOMETRIC", "RGB");
    }

    GDALDataset* dstDS = drv->CreateCopy(outputPath.toUtf8().constData(),
                                          vrtDS, FALSE, papszCreateOpts,
                                          GDALTermProgress, nullptr);
    CSLDestroy(papszCreateOpts);

    if (!dstDS) {
        GDALClose(vrtDS);
        GDALClose(srcDS);
        QMessageBox::warning(this, QString::fromUtf8("错误"),
            QString::fromUtf8("无法写入输出文件:\n%1\n\nGDAL错误: %2")
                .arg(outputPath, QString::fromUtf8(CPLGetLastErrorMsg())));
        return false;
    }

    // 为输出 GeoTIFF 所有波段设置 NoData=0
    int dstBands = dstDS->GetRasterCount();
    for (int b = 0; b < dstBands; ++b)
        dstDS->GetRasterBand(b + 1)->SetNoDataValue(0);

    m_progressBar->setValue(90);
    QApplication::processEvents();

    // ---- 6. 清理 ----
    GDALClose(dstDS);
    GDALClose(vrtDS);
    GDALClose(srcDS);

    m_progressBar->setValue(100);

    return true;
}