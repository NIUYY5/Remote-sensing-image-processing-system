 // ============================================================================
// 文件: accuracydialog.cpp
// 功能: 几何配准精度评定报告对话框 — 显示平差结果、残差分析、粗差检测
// ============================================================================

 #include "accuracydialog.h"
#include "geomodel.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QFileDialog>
#include <QFile>
#include <QTextStream>
#include <QMessageBox>
#include <cmath>

// ============================================================================
// 构造函数 — 接收平差结果数据
// 接收几何模型类型、控制点列表和平差结果，初始化对话框界面
// ============================================================================
AccuracyReportDialog::AccuracyReportDialog(GeoModelType modelType,
                                             const QVector<ControlPoint>& points,
                                             const AdjustmentResult& result,
                                             QWidget* parent)
    : QDialog(parent)
    , m_modelType(modelType)
    , m_points(points)
    , m_result(result)
{
    setWindowTitle(QString::fromUtf8("几何配准精度评定报告"));
    resize(780, 620);
    setupUI();
}

// ============================================================================
// setupUI — 界面布局（摘要标签 + 残差表格 + 详细报告 + 导出/关闭按钮）
// 顶部：摘要标签显示模型类型、控制点数量和总体 RMSE
// 中部：控制点残差表格，粗差点以红色背景高亮
// 下部：只读的详细评定报告文本区
// 底部：导出报告和关闭按钮
// ============================================================================
void AccuracyReportDialog::setupUI()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);

    // 顶部摘要
    m_summaryLabel = new QLabel();
    m_summaryLabel->setStyleSheet(
        "QLabel { font-size: 14px; font-weight: bold; color: #0078D4; "
        "padding: 8px; background: #E8F4FD; border-radius: 4px; }");
    m_summaryLabel->setText(
        QString::fromUtf8("模型: %1 | 控制点: %2 | 总体 RMSE: %3 像素")
            .arg(m_modelType == GeoModelType::Affine
                 ? QString::fromUtf8("仿射变换 (6参数)")
                 : QString::fromUtf8("二次多项式 (12参数)"))
            .arg(m_points.size())
            .arg(m_result.rmseTotal, 0, 'f', 4));
    mainLayout->addWidget(m_summaryLabel);

    // 控制点表格
    m_table = new QTableWidget(m_points.size(), 7);
    m_table->setHorizontalHeaderLabels({
        QString::fromUtf8("ID"),
        QString::fromUtf8("源X"),
        QString::fromUtf8("源Y"),
        QString::fromUtf8("参考X"),
        QString::fromUtf8("参考Y"),
        QString::fromUtf8("残差X(px)"),
        QString::fromUtf8("残差Y(px)")
    });
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);

    for (int i = 0; i < m_points.size(); ++i) {
        const auto& cp = m_points[i];
        m_table->setItem(i, 0, new QTableWidgetItem(QString::number(cp.id)));
        m_table->setItem(i, 1, new QTableWidgetItem(QString::number(cp.srcX, 'f', 4)));
        m_table->setItem(i, 2, new QTableWidgetItem(QString::number(cp.srcY, 'f', 4)));
        m_table->setItem(i, 3, new QTableWidgetItem(QString::number(cp.refX, 'f', 4)));
        m_table->setItem(i, 4, new QTableWidgetItem(QString::number(cp.refY, 'f', 4)));
        m_table->setItem(i, 5, new QTableWidgetItem(QString::number(cp.resX, 'f', 4)));
        m_table->setItem(i, 6, new QTableWidgetItem(QString::number(cp.resY, 'f', 4)));

        // 粗差点高亮 (残差 > 3σ)
        double err = std::sqrt(cp.resX * cp.resX + cp.resY * cp.resY);
        double threshold = 3.0 * m_result.sigma0;
        if (err > threshold) {
            for (int col = 0; col < 7; ++col)
                m_table->item(i, col)->setBackground(QColor(255, 230, 230));
        }
    }
    mainLayout->addWidget(m_table);

    // 详细报告
    QLabel* detailLabel = new QLabel(QString::fromUtf8("详细评定报告:"));
    detailLabel->setStyleSheet("font-weight: bold; margin-top: 8px;");
    mainLayout->addWidget(detailLabel);

    m_reportText = new QTextEdit();
    m_reportText->setReadOnly(true);
    m_reportText->setStyleSheet("font-family: Consolas, monospace; font-size: 13px;");
    m_reportText->setPlainText(buildReport());
    mainLayout->addWidget(m_reportText);

    // 底部按钮
    QHBoxLayout* btnLayout = new QHBoxLayout();
    btnLayout->addStretch();

    m_exportBtn = new QPushButton(QString::fromUtf8("导出报告"));
    m_exportBtn->setStyleSheet(
        "QPushButton { background-color: #0078D4; color: white; font-weight: bold; "
        "padding: 8px 20px; border-radius: 4px; }"
        "QPushButton:hover { background-color: #106EBE; }");
    connect(m_exportBtn, &QPushButton::clicked, this, &AccuracyReportDialog::onExport);
    btnLayout->addWidget(m_exportBtn);

    QPushButton* closeBtn = new QPushButton(QString::fromUtf8("关闭"));
    closeBtn->setStyleSheet("QPushButton { padding: 8px 20px; }");
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    btnLayout->addWidget(closeBtn);

    mainLayout->addLayout(btnLayout);
}

// ============================================================================
// buildReport — 构建详细的文本报告（模型信息、变换参数、精度指标、粗差检测、控制点明细）
// 返回格式化的多行文本，包含：
//   - 模型类型和控制点统计
//   - 变换参数 (仿射或二次多项式系数)
//   - 精度评定 (单位权中误差、RMSE、最大残差)
//   - 3σ 粗差检测结果
//   - 每个控制点的坐标和残差明细
// ============================================================================
QString AccuracyReportDialog::buildReport() const
{
    QString report;
    report += QString::fromUtf8("═══════ 几何配准精度评定 ═══════\n\n");

    report += QString::fromUtf8("【模型信息】\n");
    report += QString::fromUtf8("  模型类型: %1\n")
        .arg(m_modelType == GeoModelType::Affine
             ? QString::fromUtf8("仿射变换 (6参数)")
             : QString::fromUtf8("二次多项式 (12参数)"));
    report += QString::fromUtf8("  控制点数: %1\n").arg(m_points.size());
    int paramCount = (m_modelType == GeoModelType::Affine) ? 6 : 12;
    report += QString::fromUtf8("  多余观测数: %1\n")
        .arg(2 * m_points.size() - paramCount);

    report += QString::fromUtf8("\n【变换参数】\n");
    if (m_modelType == GeoModelType::Affine) {
        report += QString::fromUtf8(
            "  X' = a0 + a1·x + a2·y\n"
            "  Y' = b0 + b1·x + b2·y\n\n");
        report += QString::fromUtf8("  a0 = %1    b0 = %2\n")
            .arg(m_result.parameters[0], 10, 'f', 6)
            .arg(m_result.parameters[3], 10, 'f', 6);
        report += QString::fromUtf8("  a1 = %1    b1 = %2\n")
            .arg(m_result.parameters[1], 10, 'f', 6)
            .arg(m_result.parameters[4], 10, 'f', 6);
        report += QString::fromUtf8("  a2 = %1    b2 = %2\n")
            .arg(m_result.parameters[2], 10, 'f', 6)
            .arg(m_result.parameters[5], 10, 'f', 6);
    } else {
        report += QString::fromUtf8(
            "  X' = a0 + a1·x + a2·y + a3·x² + a4·xy + a5·y²\n"
            "  Y' = b0 + b1·x + b2·y + b3·x² + b4·xy + b5·y²\n\n");
        for (int i = 0; i < 6; ++i) {
            report += QString::fromUtf8("  a%1 = %2    b%3 = %4\n")
                .arg(i).arg(m_result.parameters[i], 10, 'f', 6)
                .arg(i).arg(m_result.parameters[i+6], 10, 'f', 6);
        }
    }

    report += QString::fromUtf8("\n【精度评定】\n");
    report += QString::fromUtf8("  单位权中误差 = %1 像素\n")
        .arg(m_result.sigma0, 0, 'f', 4);
    report += QString::fromUtf8("  X方向 RMSE   = %1 像素\n")
        .arg(m_result.rmseX, 0, 'f', 4);
    report += QString::fromUtf8("  Y方向 RMSE   = %1 像素\n")
        .arg(m_result.rmseY, 0, 'f', 4);
    report += QString::fromUtf8("  总体 RMSE    = %1 像素\n")
        .arg(m_result.rmseTotal, 0, 'f', 4);
    report += QString::fromUtf8("  X方向最大残差 = %1 像素\n")
        .arg(m_result.maxErrorX, 0, 'f', 4);
    report += QString::fromUtf8("  Y方向最大残差 = %1 像素\n")
        .arg(m_result.maxErrorY, 0, 'f', 4);

    double threshold = 3.0 * m_result.sigma0;
    report += QString::fromUtf8("\n【粗差检测 (3σ准则, 阈值=%1像素)】\n")
        .arg(threshold, 0, 'f', 4);
    bool hasOutlier = false;
    for (const auto& cp : m_points) {
        double err = std::sqrt(cp.resX * cp.resX + cp.resY * cp.resY);
        if (err > threshold) {
            report += QString::fromUtf8("  ⚠ 点 #%1: 残差 %2 px > 阈值\n")
                .arg(cp.id).arg(err, 0, 'f', 4);
            hasOutlier = true;
        }
    }
    if (!hasOutlier)
        report += QString::fromUtf8("  未检测到粗差点\n");

    report += QString::fromUtf8("\n【控制点明细】\n");
    report += QString::fromUtf8(
        "  ID   源X        源Y        参考X      参考Y      残差X      残差Y\n");
    for (const auto& cp : m_points) {
        report += QString::fromUtf8("  %1    %2    %3    %4    %5    %6    %7\n")
            .arg(cp.id, 3)
            .arg(cp.srcX, 10, 'f', 4).arg(cp.srcY, 10, 'f', 4)
            .arg(cp.refX, 10, 'f', 4).arg(cp.refY, 10, 'f', 4)
            .arg(cp.resX, 10, 'f', 4).arg(cp.resY, 10, 'f', 4);
    }

    return report;
}

// ============================================================================
// onExport — 导出报告到文本文件
// 弹出文件保存对话框，将详细报告内容写入用户指定的文本文件中
// ============================================================================
void AccuracyReportDialog::onExport()
{
    QString path = QFileDialog::getSaveFileName(this,
        QString::fromUtf8("导出精度评定报告"),
        QString(),
        QString::fromUtf8("文本文件 (*.txt);;所有文件 (*.*)"));
    if (path.isEmpty()) return;

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, QString::fromUtf8("错误"),
            QString::fromUtf8("无法写入文件: %1").arg(path));
        return;
    }

    QTextStream stream(&file);
    stream << m_reportText->toPlainText();
    file.close();
}
