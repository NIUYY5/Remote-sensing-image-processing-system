#include "AccuracyAssessment.h"
#include <cmath>
#include <algorithm>
#include <set>
#include <sstream>
#include <QDateTime>
#include <QFile>
#include <QTextStream>
#include <QtPrintSupport/QPrinter>
#include <QTextDocument>
#include <QPageSize>

AccuracyAssessment::AccuracyAssessment(QObject* parent)
    : QObject(parent)
{
}

AccuracyAssessment::~AccuracyAssessment()
{
}

AccuracyMetrics AccuracyAssessment::computeConfusionMatrix(
    const ClassificationResult& classification,
    const std::vector<QPoint>& referencePoints,
    const std::vector<int>& referenceLabels,
    const std::vector<QString>& classNames)
{
    AccuracyMetrics metrics;
    metrics.classNames = classNames;

    int k = static_cast<int>(classNames.size());
    if (k == 0) return metrics;

    metrics.confusionMatrix.assign(k, std::vector<int>(k, 0));
    metrics.classTotalReference.assign(k, 0);
    metrics.classTotalClassified.assign(k, 0);

    int totalValid = 0;

    for (size_t i = 0; i < referencePoints.size() && i < referenceLabels.size(); ++i) {
        int x = referencePoints[i].x();
        int y = referencePoints[i].y();
        if (x < 0 || x >= classification.width || y < 0 || y >= classification.height)
            continue;

        int refClass = referenceLabels[i];
        if (refClass < 0 || refClass >= k) continue;

        int idx = y * classification.width + x;
        int clsClass = classification.labelMap[idx];
        if (clsClass < 0 || clsClass >= k) continue;

        metrics.confusionMatrix[refClass][clsClass]++;
        metrics.classTotalReference[refClass]++;
        metrics.classTotalClassified[clsClass]++;
        totalValid++;
    }

    metrics.totalSamples = totalValid;
    metrics.overallAccuracy = computeOverallAccuracy(metrics.confusionMatrix, totalValid);
    metrics.kappaCoefficient = computeKappaCoefficient(metrics.confusionMatrix, totalValid);
    metrics.producerAccuracy = computeProducerAccuracy(metrics.confusionMatrix, metrics.classTotalReference);
    metrics.userAccuracy = computeUserAccuracy(metrics.confusionMatrix, metrics.classTotalClassified);
    metrics.f1Scores = computeF1Scores(metrics.producerAccuracy, metrics.userAccuracy);
    metrics.macroF1 = computeMacroF1(metrics.f1Scores);

    return metrics;
}

AccuracyMetrics AccuracyAssessment::computeFromLabelMap(
    const ClassificationResult& classification,
    const std::vector<int>& referenceMap,
    const std::vector<QString>& classNames)
{
    AccuracyMetrics metrics;
    metrics.classNames = classNames;

    int k = static_cast<int>(classNames.size());
    if (k == 0) return metrics;

    metrics.confusionMatrix.assign(k, std::vector<int>(k, 0));
    metrics.classTotalReference.assign(k, 0);
    metrics.classTotalClassified.assign(k, 0);

    int n = std::min(static_cast<int>(referenceMap.size()),
                     static_cast<int>(classification.labelMap.size()));
    int totalValid = 0;

    for (int i = 0; i < n; ++i) {
        int refClass = referenceMap[i];
        int clsClass = classification.labelMap[i];
        if (refClass < 0 || refClass >= k || clsClass < 0 || clsClass >= k)
            continue;

        metrics.confusionMatrix[refClass][clsClass]++;
        metrics.classTotalReference[refClass]++;
        metrics.classTotalClassified[clsClass]++;
        totalValid++;
    }

    metrics.totalSamples = totalValid;
    metrics.overallAccuracy = computeOverallAccuracy(metrics.confusionMatrix, totalValid);
    metrics.kappaCoefficient = computeKappaCoefficient(metrics.confusionMatrix, totalValid);
    metrics.producerAccuracy = computeProducerAccuracy(metrics.confusionMatrix, metrics.classTotalReference);
    metrics.userAccuracy = computeUserAccuracy(metrics.confusionMatrix, metrics.classTotalClassified);
    metrics.f1Scores = computeF1Scores(metrics.producerAccuracy, metrics.userAccuracy);
    metrics.macroF1 = computeMacroF1(metrics.f1Scores);

    return metrics;
}

double AccuracyAssessment::computeKappaCoefficient(
    const std::vector<std::vector<int>>& confusionMatrix, int totalSamples) const
{
    if (totalSamples == 0) return 0;

    int k = static_cast<int>(confusionMatrix.size());
    double po = computeOverallAccuracy(confusionMatrix, totalSamples);

    double pe = 0;
    std::vector<int> rowSum(k, 0), colSum(k, 0);
    for (int i = 0; i < k; ++i) {
        for (int j = 0; j < k; ++j) {
            rowSum[i] += confusionMatrix[i][j];
            colSum[j] += confusionMatrix[i][j];
        }
    }

    for (int i = 0; i < k; ++i)
        pe += static_cast<double>(rowSum[i]) * colSum[i] / (totalSamples * totalSamples);

    if (std::abs(1 - pe) < 1e-10) return 1;
    return (po - pe) / (1 - pe);
}

double AccuracyAssessment::computeOverallAccuracy(
    const std::vector<std::vector<int>>& confusionMatrix, int totalSamples) const
{
    if (totalSamples == 0) return 0;
    int correct = 0;
    for (size_t i = 0; i < confusionMatrix.size(); ++i)
        correct += confusionMatrix[i][i];
    return static_cast<double>(correct) / totalSamples;
}

std::vector<double> AccuracyAssessment::computeProducerAccuracy(
    const std::vector<std::vector<int>>& confusionMatrix,
    const std::vector<int>& classTotalReference) const
{
    int k = static_cast<int>(confusionMatrix.size());
    std::vector<double> pa(k, 0);
    for (int i = 0; i < k; ++i) {
        if (classTotalReference[i] > 0)
            pa[i] = static_cast<double>(confusionMatrix[i][i]) / classTotalReference[i];
    }
    return pa;
}

std::vector<double> AccuracyAssessment::computeUserAccuracy(
    const std::vector<std::vector<int>>& confusionMatrix,
    const std::vector<int>& classTotalClassified) const
{
    int k = static_cast<int>(confusionMatrix.size());
    std::vector<double> ua(k, 0);
    for (int j = 0; j < k; ++j) {
        if (classTotalClassified[j] > 0)
            ua[j] = static_cast<double>(confusionMatrix[j][j]) / classTotalClassified[j];
    }
    return ua;
}

std::vector<double> AccuracyAssessment::computeF1Scores(
    const std::vector<double>& producerAccuracy,
    const std::vector<double>& userAccuracy) const
{
    int k = static_cast<int>(producerAccuracy.size());
    std::vector<double> f1(k, 0);
    for (int i = 0; i < k; ++i) {
        double pa = producerAccuracy[i];
        double ua = userAccuracy[i];
        if (pa + ua > 0)
            f1[i] = 2.0 * pa * ua / (pa + ua);
    }
    return f1;
}

double AccuracyAssessment::computeMacroF1(const std::vector<double>& f1Scores) const
{
    if (f1Scores.empty()) return 0;
    double sum = 0;
    for (double f1 : f1Scores) sum += f1;
    return sum / f1Scores.size();
}

QString AccuracyAssessment::formatAccuracyReport(const AccuracyMetrics& metrics)
{
    QString report;
    report += QString::fromUtf8("====== \u7CBE\u5EA6\u8BC4\u4F30\u62A5\u544A ======\n\n");
    report += QString::fromUtf8("\u603B\u4F53\u7CBE\u5EA6 (Overall Accuracy): %1%\n")
                  .arg(metrics.overallAccuracy * 100, 0, 'f', 2);
    report += QString::fromUtf8("Kappa \u7CFB\u6570: %1\n").arg(metrics.kappaCoefficient, 0, 'f', 4);
    report += QString::fromUtf8("\u5B8F\u5E73\u5747 F1: %1%\n\n")
                  .arg(metrics.macroF1 * 100, 0, 'f', 2);

    report += QString::fromUtf8("\u6DF7\u6DC6\u77E9\u9635:\n");
    report += QString::fromUtf8("%1\t").arg(QString::fromUtf8("\u7C7B\u522B"));
    for (const auto& name : metrics.classNames)
        report += QString("%1\t").arg(name);
    report += QString::fromUtf8("\u5408\u8BA1\tUA(%)\n");

    for (int i = 0; i < static_cast<int>(metrics.classNames.size()); ++i) {
        report += QString("%1\t").arg(metrics.classNames[i]);
        int rowSum = 0;
        for (int j = 0; j < static_cast<int>(metrics.classNames.size()); ++j) {
            report += QString("%1\t").arg(metrics.confusionMatrix[i][j]);
            rowSum += metrics.confusionMatrix[i][j];
        }
        report += QString("%1\t%2%\n")
                      .arg(rowSum)
                      .arg(metrics.userAccuracy[i] * 100, 0, 'f', 2);
    }

    report += QString::fromUtf8("\u5408\u8BA1\t");
    for (int j = 0; j < static_cast<int>(metrics.classNames.size()); ++j)
        report += QString("%1\t").arg(metrics.classTotalClassified[j]);
    report += QString("%1\n\n").arg(metrics.totalSamples);

    report += QString::fromUtf8("PA(%)\t");
    for (int j = 0; j < static_cast<int>(metrics.classNames.size()); ++j)
        report += QString("%1%\t").arg(metrics.producerAccuracy[j] * 100, 0, 'f', 2);
    report += "\n";

    return report;
}

QString AccuracyAssessment::confusionMatrixTableHTML(const AccuracyMetrics& metrics)
{
    QString html;
    // 混淆矩阵表格
    html += "<table>\n<tr><th>" + QString::fromUtf8("\u7C7B\u522B") + "</th>";
    for (const auto& name : metrics.classNames)
        html += "<th>" + name + "</th>";
    html += "<th>" + QString::fromUtf8("\u5408\u8BA1") + "</th><th>UA(%)</th></tr>\n";

    for (int i = 0; i < static_cast<int>(metrics.classNames.size()); ++i) {
        html += "<tr><td><b>" + metrics.classNames[i] + "</b></td>";
        int rowSum = 0;
        for (int j = 0; j < static_cast<int>(metrics.classNames.size()); ++j) {
            int v = metrics.confusionMatrix[i][j];
            QString cellStyle = (i == j) ? " class='diag'" : "";
            html += "<td" + cellStyle + ">" + QString::number(v) + "</td>";
            rowSum += v;
        }
        html += "<td><b>" + QString::number(rowSum) + "</b></td>";
        html += "<td>" + QString::number(metrics.userAccuracy[i] * 100, 'f', 2) + "</td></tr>\n";
    }

    html += "<tr><td><b>" + QString::fromUtf8("\u5408\u8BA1") + "</b></td>";
    for (int j = 0; j < static_cast<int>(metrics.classNames.size()); ++j)
        html += "<td><b>" + QString::number(metrics.classTotalClassified[j]) + "</b></td>";
    html += "<td><b>" + QString::number(metrics.totalSamples) + "</b></td><td></td></tr>\n";

    html += "<tr><td><b>PA(%)</b></td>";
    for (int j = 0; j < static_cast<int>(metrics.classNames.size()); ++j)
        html += "<td><b>" + QString::number(metrics.producerAccuracy[j] * 100, 'f', 2) + "</b></td>";
    html += "<td></td><td></td></tr>\n";
    html += "</table>\n";

    // 每类详细统计表格
    html += "<table>\n<tr><th>" + QString::fromUtf8("\u7C7B\u522B") + "</th>"
            + "<th>" + QString::fromUtf8("\u751F\u4EA7\u8005\u7CBE\u5EA6(PA%)") + "</th>"
            + "<th>" + QString::fromUtf8("\u7528\u6237\u7CBE\u5EA6(UA%)") + "</th>"
            + "<th>F1-Score</th>"
            + "<th>" + QString::fromUtf8("\u53C2\u8003\u6837\u672C") + "</th>"
            + "<th>" + QString::fromUtf8("\u5206\u7C7B\u6837\u672C") + "</th></tr>\n";

    for (int i = 0; i < static_cast<int>(metrics.classNames.size()); ++i) {
        double pa = metrics.producerAccuracy[i];
        double ua = metrics.userAccuracy[i];
        double f1 = (pa + ua > 0) ? 2 * pa * ua / (pa + ua) : 0;

        html += "<tr><td><b>" + metrics.classNames[i] + "</b></td>"
                + "<td>" + QString::number(pa * 100, 'f', 2) + "</td>"
                + "<td>" + QString::number(ua * 100, 'f', 2) + "</td>"
                + "<td>" + QString::number(f1 * 100, 'f', 2) + "</td>"
                + "<td>" + QString::number(metrics.classTotalReference[i]) + "</td>"
                + "<td>" + QString::number(metrics.classTotalClassified[i]) + "</td></tr>\n";
    }
    html += "</table>\n";
    return html;
}

QString AccuracyAssessment::confusionMatrixToHTML(const AccuracyMetrics& metrics)
{
    QString html;
    html += "<!DOCTYPE html>\n<html><head><meta charset='UTF-8'>\n";
    html += "<style>\n";
    html += "body { font-family: 'Microsoft YaHei', 'Segoe UI', Arial, sans-serif; "
            "margin: 20px; color: #333; background: #f5f6fa; }\n";
    html += ".container { max-width: 900px; margin: 0 auto; background: #fff; "
            "padding: 20px 30px; border-radius: 8px; box-shadow: 0 2px 12px rgba(0,0,0,0.08); }\n";
    html += "h2 { color: #2c3e50; border-bottom: 2px solid #3498db; padding-bottom: 8px; }\n";
    html += "h3 { color: #2980b9; margin-top: 20px; }\n";
    html += ".summary { background: #eaf2f8; padding: 12px 16px; border-radius: 6px; "
            "margin: 12px 0; border-left: 4px solid #3498db; }\n";
    html += ".summary b { color: #2c3e50; }\n";
    html += ".summary .good { color: #27ae60; }\n";
    html += ".summary .warn { color: #e67e22; }\n";
    html += ".summary .bad { color: #e74c3c; }\n";
    html += "table { border-collapse: collapse; width: 100%; margin: 12px 0; font-size: 12px; }\n";
    html += "th { background: #2c3e50; color: #fff; padding: 8px 6px; text-align: center; "
            "font-weight: 600; }\n";
    html += "td { padding: 6px; text-align: center; border: 1px solid #ddd; }\n";
    html += "tr:nth-child(even) { background: #f9f9f9; }\n";
    html += "tr:hover { background: #eaf2f8; }\n";
    html += ".diag { background: #d5f5e3; font-weight: bold; }\n";
    html += ".footer { text-align: center; color: #999; font-size: 11px; "
            "margin-top: 20px; padding-top: 10px; border-top: 1px solid #eee; }\n";
    html += "</style>\n</head><body>\n<div class='container'>\n";

    html += "<h2>" + QString::fromUtf8("\u7CBE\u5EA6\u8BC4\u4F30\u62A5\u544A") + "</h2>\n";

    // 精度评级
    double oa = metrics.overallAccuracy;
    QString oaClass, oaColor;
    if (oa >= 0.90) { oaClass = "good"; oaColor = "#27ae60"; }
    else if (oa >= 0.75) { oaClass = "warn"; oaColor = "#e67e22"; }
    else { oaClass = "bad"; oaColor = "#e74c3c"; }

    QString kappaClass;
    double kappa = metrics.kappaCoefficient;
    if (kappa >= 0.80) kappaClass = "good";
    else if (kappa >= 0.60) kappaClass = "warn";
    else kappaClass = "bad";

    html += "<div class='summary'>";
    html += "<p><b>" + QString::fromUtf8("\u603B\u4F53\u7CBE\u5EA6 (OA): ") + "</b>"
            + "<span class='" + oaClass + "' style='font-size:18px;'>"
            + QString::number(oa * 100, 'f', 2) + "%</span></p>";
    html += "<p><b>Kappa " + QString::fromUtf8("\u7CFB\u6570: ") + "</b>"
            + "<span class='" + kappaClass + "'>"
            + QString::number(kappa, 'f', 4) + "</span></p>";
    html += "<p><b>" + QString::fromUtf8("\u9A8C\u8BC1\u6837\u672C\u6570: ") + "</b>"
            + QString::number(metrics.totalSamples) + "</p>";
    html += "<p><b>" + QString::fromUtf8("\u5730\u7269\u7C7B\u522B\u6570: ") + "</b>"
            + QString::number(metrics.classNames.size()) + "</p>";
    html += "</div>\n";

    // 混淆矩阵
    html += "<h3>" + QString::fromUtf8("\u6DF7\u6DC6\u77E9\u9635") + "</h3>\n";
    html += confusionMatrixTableHTML(metrics);
    html += "<h3>" + QString::fromUtf8("\u5206\u7C7B\u7CBE\u5EA6\u7EDF\u8BA1") + "</h3>\n";

    // 页脚
    html += "<div class='footer'>";
    html += "<p>" + QString::fromUtf8("\u62A5\u544A\u751F\u6210\u65F6\u95F4: ") 
            + QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss") + "</p>";
    html += "<p>" + QString::fromUtf8("\u9065\u611F\u5F71\u50CF\u5206\u6790\u7CFB\u7EDF - \u7CBE\u5EA6\u8BC4\u4F30\u6A21\u5757") + "</p>";
    html += "</div>\n";

    html += "</div>\n</body>\n</html>";
    return html;
}

// =====================================================================
//                         人工判读功能实现
// =====================================================================

AnnotationSession AccuracyAssessment::createSession(
    const ClassificationResult& classification,
    const std::vector<QPoint>& samplePoints) const
{
    AnnotationSession session;
    session.sessionId = QString::fromUtf8("SESSION_%1")
                            .arg(QDateTime::currentDateTime().toString("yyyyMMddHHmmsszzz"));
    session.name = QString::fromUtf8("\u7CBE\u5EA6\u8BC4\u5B9A\u4F1A\u8BDD_%1")
                      .arg(QDateTime::currentDateTime().toString("MM-dd hh:mm"));
    session.createTime = QDateTime::currentDateTime();
    session.lastModified = session.createTime;
    session.description = QString::fromUtf8("\u57FA\u4E8E\u5206\u7C7B\u7ED3\u679C\u7684\u4EBA\u5DE5\u5224\u8BFB\u4F1A\u8BDD");
    session.classNames = classification.classNames;
    session.sourceResultPath = classification.sourceImagePath;

    int droppedCount = 0;
    int duplicateCount = 0;
    std::set<std::pair<int, int>> seenPositions;

    for (const auto& pt : samplePoints) {
        // 边界检测
        if (pt.x() < 0 || pt.x() >= classification.width ||
            pt.y() < 0 || pt.y() >= classification.height) {
            droppedCount++;
            continue;
        }

        // 去重检测
        auto pos = std::make_pair(pt.x(), pt.y());
        if (seenPositions.count(pos)) {
            duplicateCount++;
            continue;
        }
        seenPositions.insert(pos);

        AnnotationRecord record;
        record.position = pt;
        int idx = pt.y() * classification.width + pt.x();
        record.autoLabel = (idx >= 0 && idx < static_cast<int>(classification.labelMap.size()))
                               ? classification.labelMap[idx] : -1;
        if (record.autoLabel < 0 || record.autoLabel >= static_cast<int>(classification.classNames.size()))
            record.autoLabel = -1;
        record.manualLabel = -1;
        record.confirmed = false;
        record.editTime = QDateTime::currentDateTime();
        record.version = 1;
        session.records.push_back(record);
    }

    // 记录边界/去重信息
    if (droppedCount > 0 || duplicateCount > 0) {
        session.description += QString::fromUtf8("  [%1\u8D8A\u754C/%2\u91CD\u590D]")
                                   .arg(droppedCount).arg(duplicateCount);
    }

    session.currentMetrics = computeFromAnnotations(session.records, session.classNames);
    return session;
}

// =====================================================================
//                    PDF / Excel 导出
// =====================================================================

bool AccuracyAssessment::exportReportToPDF(const QString& htmlReport, const QString& filePath)
{
    QTextDocument doc;
    doc.setHtml(htmlReport);
    doc.setPageSize(QSizeF(210, 297));  // A4 mm

    QPrinter printer(QPrinter::HighResolution);
    printer.setOutputFormat(QPrinter::PdfFormat);
    printer.setOutputFileName(filePath);
    printer.setPageSize(QPageSize(QPageSize::A4));
    printer.setPageMargins(QMarginsF(15, 15, 15, 15), QPageLayout::Millimeter);

    doc.print(&printer);
    return true;
}

bool AccuracyAssessment::exportReportToExcel(const AccuracyMetrics& metrics, const QString& filePath,
                                              const AnnotationSession* session)
{
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;

    QTextStream out(&file);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    out.setCodec("UTF-8");
#endif

    // 写入 BOM 以确保 Excel 正确识别 UTF-8
    out << "\xEF\xBB\xBF";

    // 表头：精度评估报告
    out << QString::fromUtf8("\u7CBE\u5EA6\u8BC4\u4F30\u62A5\u544A\n");

    // 总体指标
    out << QString::fromUtf8("\u6307\u6807,\u503C\n");
    out << QString::fromUtf8("\u603B\u4F53\u7CBE\u5EA6 (OA),") << QString::number(metrics.overallAccuracy * 100, 'f', 2) << "%\n";
    out << QString::fromUtf8("Kappa \u7CFB\u6570,") << QString::number(metrics.kappaCoefficient, 'f', 4) << "\n";
    out << QString::fromUtf8("\u5B8F\u5E73\u5747 F1,") << QString::number(metrics.macroF1 * 100, 'f', 2) << "%\n";
    out << QString::fromUtf8("\u9A8C\u8BC1\u6837\u672C\u6570,") << metrics.totalSamples << "\n\n";

    // 各类别精度
    out << QString::fromUtf8("\u7C7B\u522B,PA(\u53EC\u56DE\u7387)%,UA(\u7CBE\u786E\u7387)%,F1 \u5206\u6570,"
                              "\u53C2\u8003\u6837\u672C,\u5206\u7C7B\u6837\u672C\n");
    for (size_t i = 0; i < metrics.classNames.size(); ++i) {
        out << metrics.classNames[i] << ","
            << QString::number(metrics.producerAccuracy[i] * 100, 'f', 2) << "%,"
            << QString::number(metrics.userAccuracy[i] * 100, 'f', 2) << "%,"
            << QString::number(metrics.f1Scores[i] * 100, 'f', 2) << "%,"
            << (i < metrics.classTotalReference.size() ? metrics.classTotalReference[i] : 0) << ","
            << (i < metrics.classTotalClassified.size() ? metrics.classTotalClassified[i] : 0) << "\n";
    }
    out << "\n";

    // 混淆矩阵
    out << QString::fromUtf8("\u6DF7\u6DC6\u77E9\u9635\n");
    out << ",";
    for (const auto& name : metrics.classNames)
        out << name << ",";
    out << QString::fromUtf8("\u5408\u8BA1\n");

    int k = static_cast<int>(metrics.confusionMatrix.size());
    for (int i = 0; i < k; ++i) {
        out << metrics.classNames[i] << ",";
        int rowSum = 0;
        for (int j = 0; j < k; ++j) {
            out << metrics.confusionMatrix[i][j] << ",";
            rowSum += metrics.confusionMatrix[i][j];
        }
        out << rowSum << "\n";
    }
    out << "\n";

    // 判读会话信息
    if (session && !session->sessionId.isEmpty()) {
        out << QString::fromUtf8("\u5224\u8BFB\u4F1A\u8BDD\u4FE1\u606F\n");
        out << QString::fromUtf8("\u4F1A\u8BDD\u540D\u79F0,") << session->name << "\n";
        out << QString::fromUtf8("\u521B\u5EFA\u65F6\u95F4,") << session->createTime.toString("yyyy-MM-dd hh:mm:ss") << "\n";
        out << QString::fromUtf8("\u4FEE\u6539\u65F6\u95F4,") << session->lastModified.toString("yyyy-MM-dd hh:mm:ss") << "\n";
        out << QString::fromUtf8("\u6807\u6CE8\u70B9\u603B\u6570,") << session->totalCount() << "\n";
        out << QString::fromUtf8("\u5DF2\u786E\u8BA4\u6570,") << session->confirmedCount() << "\n";
        out << QString::fromUtf8("\u5DF2\u4FEE\u6B63\u6570,") << session->overriddenCount() << "\n";

        out << QString::fromUtf8("\n\u6807\u6CE8\u8BB0\u5F55\u8BE6\u60C5\n");
        out << QString::fromUtf8("\u5E8F\u53F7,\u5750\u6807X,\u5750\u6807Y,\u81EA\u52A8\u6807\u7B7E,\u4EBA\u5DE5\u6807\u7B7E,"
                                  "\u786E\u8BA4\u72B6\u6001,\u5907\u6CE8\n");
        for (size_t i = 0; i < session->records.size(); ++i) {
            const auto& r = session->records[i];
            out << (i + 1) << ","
                << r.position.x() << ","
                << r.position.y() << ","
                << r.autoLabel << ","
                << r.manualLabel << ","
                << (r.confirmed ? QString::fromUtf8("\u5DF2\u786E\u8BA4") : QString::fromUtf8("\u5F85\u786E\u8BA4")) << ","
                << r.comment << "\n";
        }
    }

    file.close();
    return true;
}

// 编译标记：验证源文件版本（第二次尝试）
namespace { const volatile int _g_accuracyAssessmentMarker = 42; }

AccuracyMetrics AccuracyAssessment::computeFromAnnotations(
    const std::vector<AnnotationRecord>& records,
    const std::vector<QString>& classNames) const
{
    AccuracyMetrics metrics;
    metrics.classNames = classNames;

    int k = static_cast<int>(classNames.size());
    if (k == 0) return metrics;

    metrics.confusionMatrix.assign(k, std::vector<int>(k, 0));
    metrics.classTotalReference.assign(k, 0);
    metrics.classTotalClassified.assign(k, 0);

    int totalValid = 0;
    for (const auto& record : records) {
        int refLabel = record.effectiveLabel();  // 有效参考标签（人工优先）
        int clsLabel = record.autoLabel;          // 原始分类标签

        if (refLabel < 0 || refLabel >= k || clsLabel < 0 || clsLabel >= k)
            continue;

        metrics.confusionMatrix[refLabel][clsLabel]++;
        metrics.classTotalReference[refLabel]++;
        metrics.classTotalClassified[clsLabel]++;
        totalValid++;
    }

    metrics.totalSamples = totalValid;
    metrics.overallAccuracy = computeOverallAccuracy(metrics.confusionMatrix, totalValid);
    metrics.kappaCoefficient = computeKappaCoefficient(metrics.confusionMatrix, totalValid);
    metrics.producerAccuracy = computeProducerAccuracy(metrics.confusionMatrix, metrics.classTotalReference);
    metrics.userAccuracy = computeUserAccuracy(metrics.confusionMatrix, metrics.classTotalClassified);
    metrics.f1Scores = computeF1Scores(metrics.producerAccuracy, metrics.userAccuracy);
    metrics.macroF1 = computeMacroF1(metrics.f1Scores);

    return metrics;
}

bool AccuracyAssessment::updateAnnotation(AnnotationRecord& record, int newManualLabel,
                                           const QString& comment)
{
    if (newManualLabel == record.manualLabel && comment == record.comment)
        return false;

    record.manualLabel = newManualLabel;
    if (!comment.isEmpty())
        record.comment = comment;
    record.editTime = QDateTime::currentDateTime();
    record.version++;
    record.confirmed = true;  // 人工修正后自动标记为已确认
    return true;
}

void AccuracyAssessment::confirmAll(std::vector<AnnotationRecord>& records, bool confirmed)
{
    for (auto& r : records) {
        r.confirmed = confirmed;
        r.editTime = QDateTime::currentDateTime();
    }
}

void AccuracyAssessment::resetAllOverrides(std::vector<AnnotationRecord>& records)
{
    for (auto& r : records) {
        r.manualLabel = -1;
        r.comment.clear();
        r.confirmed = false;
        r.editTime = QDateTime::currentDateTime();
        r.version = 1;
    }
}

int AccuracyAssessment::findRecordByPosition(
    const std::vector<AnnotationRecord>& records, const QPoint& pos) const
{
    for (size_t i = 0; i < records.size(); ++i) {
        if (records[i].position == pos)
            return static_cast<int>(i);
    }
    return -1;
}

// =====================================================================
//                      专业精度评定报告生成
// =====================================================================

QString AccuracyAssessment::generateProfessionalReport(const AccuracyMetrics& metrics,
                                                        const AnnotationSession* session)
{
    QString html;
    html += "<!DOCTYPE html>\n<html lang=\"zh-CN\"><head><meta charset='UTF-8'>\n";
    html += "<title>" + QString::fromUtf8("\u7CBE\u5EA6\u8BC4\u5B9A\u4E13\u4E1A\u62A5\u544A") + "</title>\n";
    html += "<style>\n";
    html += "* { box-sizing: border-box; margin: 0; padding: 0; }\n";
    html += "body { font-family: 'Microsoft YaHei', 'SimSun', 'Segoe UI', Arial, sans-serif; "
            "color: #2c3e50; background: #f0f2f5; line-height: 1.7; }\n";
    html += ".container { max-width: 1000px; margin: 0 auto; padding: 24px; }\n";
    html += ".header { background: linear-gradient(135deg, #1a5276, #2980b9); color: #fff; "
            "padding: 32px 40px; border-radius: 10px 10px 0 0; }\n";
    html += ".header h1 { font-size: 26px; font-weight: 700; margin-bottom: 6px; }\n";
    html += ".header .subtitle { font-size: 14px; opacity: 0.85; }\n";
    html += ".card { background: #fff; padding: 24px 28px; margin-bottom: 16px; "
            "border-radius: 8px; box-shadow: 0 1px 4px rgba(0,0,0,0.06); "
            "border: 1px solid #e8ecf0; }\n";
    html += ".card h2 { font-size: 18px; color: #1a5276; border-bottom: 2px solid #3498db; "
            "padding-bottom: 8px; margin-bottom: 16px; }\n";
    html += ".card h3 { font-size: 15px; color: #2c3e50; margin: 12px 0 8px; }\n";
    html += ".summary-grid { display: flex; gap: 16px; flex-wrap: wrap; margin-bottom: 16px; }\n";
    html += ".summary-item { flex: 1; min-width: 180px; background: #f8fafc; "
            "border-radius: 8px; padding: 16px; text-align: center; border: 1px solid #e8ecf0; }\n";
    html += ".summary-item .value { font-size: 28px; font-weight: 700; margin-bottom: 4px; }\n";
    html += ".summary-item .label { font-size: 12px; color: #7f8c8d; text-transform: uppercase; }\n";
    html += ".score-excellent { color: #27ae60; } .score-good { color: #2980b9; }\n";
    html += ".score-average { color: #f39c12; } .score-poor { color: #e74c3c; }\n";
    html += "table { border-collapse: collapse; width: 100%; margin: 10px 0; font-size: 13px; }\n";
    html += "th { background: #34495e; color: #fff; padding: 10px 8px; text-align: center; "
            "font-weight: 600; font-size: 12px; }\n";
    html += "td { padding: 8px; text-align: center; border: 1px solid #e0e0e0; }\n";
    html += "tr:nth-child(even) td { background: #fafafa; }\n";
    html += "tr:hover td { background: #eaf2f8; }\n";
    html += ".diag { background: #d5f5e3 !important; font-weight: bold; }\n";
    html += ".bar-container { display: flex; align-items: center; gap: 8px; margin: 4px 0; }\n";
    html += ".bar-label { width: 70px; text-align: right; font-size: 12px; font-weight: 600; }\n";
    html += ".bar-track { flex: 1; height: 22px; background: #ecf0f1; border-radius: 4px; "
            "overflow: hidden; position: relative; }\n";
    html += ".bar-fill { height: 100%; border-radius: 4px; display: flex; align-items: center; "
            "justify-content: flex-end; padding-right: 6px; font-size: 11px; color: #fff; "
            "font-weight: 600; min-width: 40px; }\n";
    html += ".bar-fill.ua { background: linear-gradient(90deg, #3498db, #2980b9); }\n";
    html += ".bar-fill.pa { background: linear-gradient(90deg, #2ecc71, #27ae60); }\n";
    html += ".meta-info { display: flex; justify-content: space-between; font-size: 12px; "
            "color: #7f8c8d; margin-top: 8px; }\n";
    html += ".conclusion-box { background: #fef9e7; border-left: 4px solid #f39c12; "
            "padding: 12px 16px; border-radius: 4px; margin: 8px 0; }\n";
    html += ".conclusion-box.good { background: #eafaf1; border-left-color: #27ae60; }\n";
    html += ".footer { text-align: center; color: #bdc3c7; font-size: 11px; "
            "margin-top: 20px; padding-top: 16px; border-top: 1px solid #eee; }\n";
    html += "</style>\n</head><body>\n<div class='container'>\n";

    // 标题区
    html += "<div class='header'>\n";
    html += "<h1>" + QString::fromUtf8("\u9065\u611F\u5F71\u50CF\u5206\u7C7B\u7CBE\u5EA6\u8BC4\u5B9A\u4E13\u4E1A\u62A5\u544A") + "</h1>\n";
    html += "<div class='subtitle'>Generated by FeatureExtraction4 | "
            + QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss") + "</div>\n";
    if (session) {
        html += "<div class='subtitle'>" + QString::fromUtf8("\u4F1A\u8BDD: ") + session->name
                + " | " + QString::fromUtf8("\u6837\u672C\u6570: ") + QString::number(session->totalCount())
                + " | " + QString::fromUtf8("\u5DF2\u786E\u8BA4: ") + QString::number(session->confirmedCount()) + "</div>\n";
    }
    html += "</div>\n";

    // 精度评分卡片
    double oa = metrics.overallAccuracy;
    double kappa = metrics.kappaCoefficient;
    QString oaScoreClass, kappaScoreClass;
    if (oa >= 0.90) oaScoreClass = "score-excellent";
    else if (oa >= 0.75) oaScoreClass = "score-good";
    else if (oa >= 0.60) oaScoreClass = "score-average";
    else oaScoreClass = "score-poor";

    if (kappa >= 0.80) kappaScoreClass = "score-excellent";
    else if (kappa >= 0.60) kappaScoreClass = "score-good";
    else if (kappa >= 0.40) kappaScoreClass = "score-average";
    else kappaScoreClass = "score-poor";

    QString f1ScoreClass;
    double macroF1 = metrics.macroF1;
    if (macroF1 >= 0.85) f1ScoreClass = "score-excellent";
    else if (macroF1 >= 0.70) f1ScoreClass = "score-good";
    else if (macroF1 >= 0.50) f1ScoreClass = "score-average";
    else f1ScoreClass = "score-poor";

    html += "<div class='card'>\n";
    html += "<h2>" + QString::fromUtf8("\u7CBE\u5EA6\u6307\u6807\u6982\u89C8") + "</h2>\n";
    html += "<div class='summary-grid'>\n";
    html += "<div class='summary-item'><div class='value " + oaScoreClass + "'>"
            + QString::number(oa * 100, 'f', 2) + "%</div><div class='label'>"
            + QString::fromUtf8("\u603B\u4F53\u7CBE\u5EA6 (OA)") + "</div></div>\n";
    html += "<div class='summary-item'><div class='value " + kappaScoreClass + "'>"
            + QString::number(kappa, 'f', 4) + "</div><div class='label'>Kappa "
            + QString::fromUtf8("\u7CFB\u6570") + "</div></div>\n";
    html += "<div class='summary-item'><div class='value'>"
            + QString::number(metrics.totalSamples) + "</div><div class='label'>"
            + QString::fromUtf8("\u9A8C\u8BC1\u6837\u672C\u6570") + "</div></div>\n";
    html += "<div class='summary-item'><div class='value ' + f1ScoreClass + \">'"
            + QString::number(metrics.macroF1 * 100, 'f', 2) + "%</div><div class='label'>"
            + QString::fromUtf8("\u5B8F\u5E73\u5747 F1") + "</div></div>\n";
    html += "<div class='summary-item'><div class='value'>"
            + QString::number(metrics.classNames.size()) + "</div><div class='label'>"
            + QString::fromUtf8("\u5730\u7269\u7C7B\u522B\u6570") + "</div></div>\n";
    html += "</div>\n";

    // 统计分析摘要
    html += generateStatisticalSummary(metrics);
    html += "</div>\n";

    // 判读会话摘要（如有）
    if (session) {
        html += "<div class='card'>\n";
        html += "<h2>" + QString::fromUtf8("\u4EBA\u5DE5\u5224\u8BFB\u4F1A\u8BDD\u6458\u8981") + "</h2>\n";
        html += "<div class='summary-grid'>\n";
        html += "<div class='summary-item'><div class='value'>"
                + QString::number(session->totalCount()) + "</div><div class='label'>"
                + QString::fromUtf8("\u6807\u6CE8\u70B9\u603B\u6570") + "</div></div>\n";
        html += "<div class='summary-item'><div class='value'>"
                + QString::number(session->confirmedCount()) + "</div><div class='label'>"
                + QString::fromUtf8("\u5DF2\u786E\u8BA4\u6570") + "</div></div>\n";
        html += "<div class='summary-item'><div class='value'>"
                + QString::number(session->overriddenCount()) + "</div><div class='label'>"
                + QString::fromUtf8("\u4EBA\u5DE5\u4FEE\u6B63\u6570") + "</div></div>\n";
        double confirmRate = session->totalCount() > 0
            ? static_cast<double>(session->confirmedCount()) / session->totalCount() * 100 : 0;
        QString confirmColor = confirmRate >= 90 ? "#27ae60" : (confirmRate >= 70 ? "#f39c12" : "#e74c3c");
        html += "<div class='summary-item'><div class='value' style='color:" + confirmColor + ";'>"
                + QString::number(confirmRate, 'f', 1) + "%</div><div class='label'>"
                + QString::fromUtf8("\u786E\u8BA4\u7387") + "</div></div>\n";
        html += "</div>\n";

        // 各类别修正统计
        std::vector<int> perClassOverride(session->classNames.size(), 0);
        std::vector<int> perClassConfirmed(session->classNames.size(), 0);
        for (const auto& r : session->records) {
            if (r.autoLabel >= 0 && r.autoLabel < static_cast<int>(session->classNames.size())) {
                if (r.isOverridden()) perClassOverride[r.autoLabel]++;
                if (r.confirmed) perClassConfirmed[r.autoLabel]++;
            }
        }
        html += "<table>\n<tr><th>" + QString::fromUtf8("\u7C7B\u522B") + "</th><th>"
                + QString::fromUtf8("\u4EBA\u5DE5\u4FEE\u6B63\u6570") + "</th><th>"
                + QString::fromUtf8("\u5DF2\u786E\u8BA4\u6570") + "</th><th>"
                + QString::fromUtf8("\u4FEE\u6B63\u7387") + "</th></tr>\n";
        for (size_t i = 0; i < session->classNames.size(); ++i) {
            // 统计该类别有多少标注点
            int classTotal = 0;
            for (const auto& r : session->records) {
                if (r.autoLabel == static_cast<int>(i)) classTotal++;
            }
            double overrideRate = classTotal > 0 ? perClassOverride[i] * 100.0 / classTotal : 0;
            QString rateColor = overrideRate > 30 ? "#e74c3c" : (overrideRate > 10 ? "#f39c12" : "#27ae60");
            html += "<tr><td><b>" + session->classNames[i] + "</b></td><td>"
                    + QString::number(perClassOverride[i]) + "</td><td>"
                    + QString::number(perClassConfirmed[i]) + "</td><td style='color:" + rateColor + ";font-weight:bold;'>"
                    + QString::number(overrideRate, 'f', 1) + "%</td></tr>\n";
        }
        html += "</table>\n";
        html += "</div>\n";
    }

    // 各类别精度柱状图
    html += "<div class='card'>\n";
    html += "<h2>" + QString::fromUtf8("\u5404\u7C7B\u522B\u7CBE\u5EA6\u5206\u5E03") + "</h2>\n";
    html += generatePerClassBarChart(metrics);
    html += "</div>\n";

    // 混淆矩阵
    html += "<div class='card'>\n";
    html += "<h2>" + QString::fromUtf8("\u6DF7\u6DC6\u77E9\u9635\u4E0E\u5206\u7C7B\u7CBE\u5EA6\u7EDF\u8BA1") + "</h2>\n";
    html += confusionMatrixTableHTML(metrics);
    html += "</div>\n";

    // 结论与建议
    html += "<div class='card'>\n";
    html += "<h2>" + QString::fromUtf8("\u7ED3\u8BBA\u4E0E\u5EFA\u8BAE") + "</h2>\n";
    html += generateConclusions(metrics);
    html += "</div>\n";

    // 页脚
    html += "<div class='footer'>\n";
    html += "<p>" + QString::fromUtf8("\u62A5\u544A\u751F\u6210\u65F6\u95F4: ")
            + QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss") + "</p>\n";
    html += "<p>" + QString::fromUtf8("\u9065\u611F\u5F71\u50CF\u5730\u7269\u63D0\u53D6\u7CFB\u7EDF FeatureExtraction4 - \u7CBE\u5EA6\u8BC4\u5B9A\u6A21\u5757") + "</p>\n";
    if (session) {
        html += "<p>" + QString::fromUtf8("\u5224\u8BFB\u4F1A\u8BDD: ") + session->sessionId + "</p>\n";
    }
    html += "</div>\n</div>\n</body>\n</html>";
    return html;
}

QString AccuracyAssessment::generatePerClassBarChart(const AccuracyMetrics& metrics)
{
    QString html;
    html += "<div style=\"margin: 8px 0;\">\n";
    html += "<h3 style=\"font-size:14px; margin:8px 0;\">"
            + QString::fromUtf8("\u751F\u4EA7\u8005\u7CBE\u5EA6 (PA) - \u6F0F\u5206\u8BCA\u65AD") + "</h3>\n";
    for (size_t i = 0; i < metrics.classNames.size(); ++i) {
        double pa = (i < metrics.producerAccuracy.size()) ? metrics.producerAccuracy[i] : 0;
        int pct = qBound(0, qRound(pa * 100), 100);
        QString barColor = (pa >= 0.90) ? "#27ae60" : (pa >= 0.75) ? "#3498db" : (pa >= 0.60) ? "#f39c12" : "#e74c3c";
        html += "<div class=\"bar-container\">\n";
        html += "<div class=\"bar-label\">" + metrics.classNames[i] + "</div>\n";
        html += "<div class=\"bar-track\"><div class=\"bar-fill pa\" style=\"width:"
                + QString::number(pct) + "%; background:" + barColor + ";\">"
                + QString::number(pct) + "%</div></div>\n";
        html += "</div>\n";
    }

    html += "<h3 style=\"font-size:14px; margin:16px 0 8px;\">"
            + QString::fromUtf8("\u7528\u6237\u7CBE\u5EA6 (UA) - \u9519\u5206\u8BCA\u65AD") + "</h3>\n";
    for (size_t i = 0; i < metrics.classNames.size(); ++i) {
        double ua = (i < metrics.userAccuracy.size()) ? metrics.userAccuracy[i] : 0;
        int pct = qBound(0, qRound(ua * 100), 100);
        QString barColor = (ua >= 0.90) ? "#27ae60" : (ua >= 0.75) ? "#3498db" : (ua >= 0.60) ? "#f39c12" : "#e74c3c";
        html += "<div class=\"bar-container\">\n";
        html += "<div class=\"bar-label\">" + metrics.classNames[i] + "</div>\n";
        html += "<div class=\"bar-track\"><div class=\"bar-fill ua\" style=\"width:"
                + QString::number(pct) + "%; background:" + barColor + ";\">"
                + QString::number(pct) + "%</div></div>\n";
        html += "</div>\n";
    }

    html += "<h3 style=\"font-size:14px; margin:16px 0 8px;\">"
            + QString::fromUtf8("F1 \u5206\u6570 - \u7EFC\u5408\u6027\u80FD\u6307\u6807") + "</h3>\n";
    for (size_t i = 0; i < metrics.classNames.size(); ++i) {
        double f1 = (i < metrics.f1Scores.size()) ? metrics.f1Scores[i] : 0;
        int pct = qBound(0, qRound(f1 * 100), 100);
        QString barColor = (f1 >= 0.85) ? "#27ae60" : (f1 >= 0.70) ? "#3498db" : (f1 >= 0.50) ? "#f39c12" : "#e74c3c";
        html += "<div class=\"bar-container\">\n";
        html += "<div class=\"bar-label\">" + metrics.classNames[i] + "</div>\n";
        html += "<div class=\"bar-track\"><div class=\"bar-fill\" style=\"width:"
                + QString::number(pct) + "%; background:" + barColor + ";\">"
                + QString::number(pct) + "%</div></div>\n";
        html += "</div>\n";
    }
    html += "</div>\n";
    return html;
}

QString AccuracyAssessment::generateStatisticalSummary(const AccuracyMetrics& metrics)
{
    int k = static_cast<int>(metrics.classNames.size());
    if (k == 0) return "";

    // 计算平均PA、平均UA、平均F1
    double avgPA = 0, avgUA = 0;
    double minPA = 1.0, maxPA = 0, minUA = 1.0, maxUA = 0;
    double minF1 = 1.0, maxF1 = 0;
    int worstPAIdx = 0, bestPAIdx = 0, worstUAIdx = 0, bestUAIdx = 0;
    int worstF1Idx = 0, bestF1Idx = 0;

    for (int i = 0; i < k; ++i) {
        double pa = metrics.producerAccuracy[i];
        double ua = metrics.userAccuracy[i];
        double f1 = (i < static_cast<int>(metrics.f1Scores.size())) ? metrics.f1Scores[i] : 0;
        avgPA += pa; avgUA += ua;
        if (pa < minPA) { minPA = pa; worstPAIdx = i; }
        if (pa > maxPA) { maxPA = pa; bestPAIdx = i; }
        if (ua < minUA) { minUA = ua; worstUAIdx = i; }
        if (ua > maxUA) { maxUA = ua; bestUAIdx = i; }
        if (f1 < minF1) { minF1 = f1; worstF1Idx = i; }
        if (f1 > maxF1) { maxF1 = f1; bestF1Idx = i; }
    }
    avgPA /= k; avgUA /= k;

    QString html;
    html += "<h3>" + QString::fromUtf8("\u7EDF\u8BA1\u5206\u6790\u6458\u8981") + "</h3>\n";
    html += "<table>\n";
    html += "<tr><th>" + QString::fromUtf8("\u6307\u6807") + "</th><th>"
            + QString::fromUtf8("\u503C") + "</th><th>" + QString::fromUtf8("\u8BF4\u660E") + "</th></tr>\n";
    html += "<tr><td>" + QString::fromUtf8("\u5E73\u5747\u751F\u4EA7\u8005\u7CBE\u5EA6") + "</td><td>"
            + QString::number(avgPA * 100, 'f', 2) + "%</td><td>" + QString::fromUtf8("\u6240\u6709\u7C7B\u522BPA\u7684\u7B97\u672F\u5E73\u5747") + "</td></tr>\n";
    html += "<tr><td>" + QString::fromUtf8("\u5E73\u5747\u7528\u6237\u7CBE\u5EA6") + "</td><td>"
            + QString::number(avgUA * 100, 'f', 2) + "%</td><td>" + QString::fromUtf8("\u6240\u6709\u7C7B\u522BUA\u7684\u7B97\u672F\u5E73\u5747") + "</td></tr>\n";
    html += "<tr><td>" + QString::fromUtf8("\u5B8F\u5E73\u5747 F1") + "</td><td>"
            + QString::number(metrics.macroF1 * 100, 'f', 2) + "%</td><td>" + QString::fromUtf8("\u5404\u7C7B F1 \u5206\u6570\u7684\u5E73\u5747") + "</td></tr>\n";
    html += "<tr><td style=\"color:#e74c3c;\">" + QString::fromUtf8("\u6700\u4F4EPA\u7C7B\u522B") + "</td><td style=\"color:#e74c3c;\">"
            + metrics.classNames[worstPAIdx] + " (" + QString::number(minPA * 100, 'f', 1) + "%)</td><td>"
            + QString::fromUtf8("\u6F0F\u5206\u6700\u4E25\u91CD\u7684\u7C7B\u522B") + "</td></tr>\n";
    html += "<tr><td style=\"color:#27ae60;\">" + QString::fromUtf8("\u6700\u9AD8PA\u7C7B\u522B") + "</td><td style=\"color:#27ae60;\">"
            + metrics.classNames[bestPAIdx] + " (" + QString::number(maxPA * 100, 'f', 1) + "%)</td><td>"
            + QString::fromUtf8("\u6F0F\u5206\u6700\u5C11\u7684\u7C7B\u522B") + "</td></tr>\n";
    html += "<tr><td style=\"color:#e74c3c;\">" + QString::fromUtf8("\u6700\u4F4EUA\u7C7B\u522B") + "</td><td style=\"color:#e74c3c;\">"
            + metrics.classNames[worstUAIdx] + " (" + QString::number(minUA * 100, 'f', 1) + "%)</td><td>"
            + QString::fromUtf8("\u9519\u5206\u6700\u4E25\u91CD\u7684\u7C7B\u522B") + "</td></tr>\n";
    html += "<tr><td style=\"color:#27ae60;\">" + QString::fromUtf8("\u6700\u9AD8UA\u7C7B\u522B") + "</td><td style=\"color:#27ae60;\">"
            + metrics.classNames[bestUAIdx] + " (" + QString::number(maxUA * 100, 'f', 1) + "%)</td><td>"
            + QString::fromUtf8("\u9519\u5206\u6700\u5C11\u7684\u7C7B\u522B") + "</td></tr>\n";
    html += "<tr><td style=\"color:#e74c3c;\">" + QString::fromUtf8("\u6700\u4F4EF1\u7C7B\u522B") + "</td><td style=\"color:#e74c3c;\">"
            + metrics.classNames[worstF1Idx] + " (" + QString::number(minF1 * 100, 'f', 1) + "%)</td><td>"
            + QString::fromUtf8("\u7EFC\u5408\u6027\u80FD\u6700\u5DEE\u7684\u7C7B\u522B") + "</td></tr>\n";
    html += "<tr><td style=\"color:#27ae60;\">" + QString::fromUtf8("\u6700\u9AD8F1\u7C7B\u522B") + "</td><td style=\"color:#27ae60;\">"
            + metrics.classNames[bestF1Idx] + " (" + QString::number(maxF1 * 100, 'f', 1) + "%)</td><td>"
            + QString::fromUtf8("\u7EFC\u5408\u6027\u80FD\u6700\u597D\u7684\u7C7B\u522B") + "</td></tr>\n";
    html += "</table>\n";
    return html;
}

QString AccuracyAssessment::generateConclusions(const AccuracyMetrics& metrics)
{
    int k = static_cast<int>(metrics.classNames.size());
    if (k == 0) return "";

    QString html;
    double oa = metrics.overallAccuracy;
    double kappa = metrics.kappaCoefficient;

    // 总体结论
    QString oaLevel, kappaLvl;
    if (oa >= 0.90) oaLevel = QString::fromUtf8("\u4F18\u79C0");
    else if (oa >= 0.75) oaLevel = QString::fromUtf8("\u826F\u597D");
    else if (oa >= 0.60) oaLevel = QString::fromUtf8("\u4E00\u822C");
    else oaLevel = QString::fromUtf8("\u8F83\u4F4E");

    if (kappa >= 0.80) kappaLvl = QString::fromUtf8("\u51E0\u4E4E\u5B8C\u7F8E\u4E00\u81F4");
    else if (kappa >= 0.60) kappaLvl = QString::fromUtf8("\u9AD8\u5EA6\u4E00\u81F4");
    else if (kappa >= 0.40) kappaLvl = QString::fromUtf8("\u4E2D\u7B49\u4E00\u81F4");
    else kappaLvl = QString::fromUtf8("\u4E00\u81F4\u6027\u4E0D\u8DB3");

    QString boxClass = (oa >= 0.75) ? "good" : "";

    html += "<div class=\"conclusion-box " + boxClass + "\">\n";
    html += "<p><b>" + QString::fromUtf8("\u7EFC\u5408\u8BC4\u4EF7: ") + "</b>";
    html += QString::fromUtf8("\u603B\u4F53\u7CBE\u5EA6\u4E3A %1%\uFF0C\u8FBE\u5230<b>%2</b>\u6C34\u5E73\uFF1B")
                .arg(oa * 100, 0, 'f', 2).arg(oaLevel);
    html += QString::fromUtf8("Kappa\u7CFB\u6570\u4E3A %1\uFF0C\u8868\u660E\u5206\u7C7B\u7ED3\u679C\u4E0E\u53C2\u8003\u6570\u636E\u4E4B\u95F4\u5B58\u5728<b>%2</b>\u3002")
                .arg(kappa, 0, 'f', 4).arg(kappaLvl);
    html += QString::fromUtf8("\u5B8F\u5E73\u5747F1\u4E3A %1%\u3002")
                .arg(metrics.macroF1 * 100, 0, 'f', 2);
    html += "</p></div>\n";

    // 混淆分析：找出最容易混淆的类别对
    html += "<h3>" + QString::fromUtf8("\u6DF7\u6DC6\u5206\u6790") + "</h3>\n";

    // 收集前5个off-diagonal混淆对
    struct ConfusionPair {
        int rowIdx, colIdx, count;
        bool operator<(const ConfusionPair& o) const { return count > o.count; }
    };
    std::vector<ConfusionPair> confusionPairs;
    for (int i = 0; i < k; ++i) {
        for (int j = 0; j < k; ++j) {
            if (i != j) {
                int cnt = metrics.confusionMatrix[i][j];
                if (cnt > 0)
                    confusionPairs.push_back({i, j, cnt});
            }
        }
    }
    std::sort(confusionPairs.begin(), confusionPairs.end());
    int topN = std::min(5, static_cast<int>(confusionPairs.size()));

    if (topN > 0) {
        html += "<table>\n<tr><th>" + QString::fromUtf8("\u53C2\u8003\u7C7B\u522B") + "</th><th>\u2192</th><th>"
                + QString::fromUtf8("\u5206\u7C7B\u7C7B\u522B") + "</th><th>"
                + QString::fromUtf8("\u9519\u5206\u6837\u672C\u6570") + "</th><th>"
                + QString::fromUtf8("\u95EE\u9898\u7C7B\u578B") + "</th></tr>\n";
        for (int n = 0; n < topN; ++n) {
            const auto& cp = confusionPairs[n];
            int refIdx = cp.rowIdx;
            int clsIdx = cp.colIdx;
            QString issueType;
            // 判断问题类型：光谱相似 vs 边界混合 vs 特征重叠
            if (refIdx < k && clsIdx < k) {
                issueType = QString::fromUtf8("\u5149\u8C31/\u7279\u5F81\u76F8\u4F3C\u5BFC\u81F4\u9519\u5206");
            }
            html += "<tr><td><b>" + metrics.classNames[refIdx] + "</b></td><td>\u2192</td><td><b>"
                    + metrics.classNames[clsIdx] + "</b></td><td style=\"color:#e74c3c;\">"
                    + QString::number(cp.count) + "</td><td>" + issueType + "</td></tr>\n";
        }
        html += "</table>\n";

        // 主要混淆对分析
        html += "<p style=\"margin:10px 0;\">" + QString::fromUtf8("\u4E3B\u8981\u6DF7\u6DC6\u6765\u6E90: ")
                + metrics.classNames[confusionPairs[0].rowIdx] + " \u2192 "
                + metrics.classNames[confusionPairs[0].colIdx] + QString::fromUtf8(" (%1 \u4E2A\u6837\u672C\u9519\u5206)")
                    .arg(confusionPairs[0].count)
                + QString::fromUtf8("\uFF0C\u5EFA\u8BAE\u91C7\u7528\u591A\u7279\u5F81\u8054\u5408\u5224\u5B9A\u6216\u589E\u52A0\u7C7B\u522B\u7279\u5F81\u7EF4\u5EA6\u4EE5\u63D0\u9AD8\u533A\u5206\u80FD\u529B\u3002")
                + "</p>\n";
    } else {
        html += "<p>" + QString::fromUtf8("\u672A\u68C0\u6D4B\u5230\u663E\u8457\u7684\u7C7B\u522B\u6DF7\u6DC6\u73B0\u8C61\u3002") + "</p>\n";
    }

    // 分类建议
    html += "<h3>" + QString::fromUtf8("\u5206\u7C7B\u5EFA\u8BAE") + "</h3>\n";
    html += "<ul style=\"padding-left: 20px; line-height: 1.8;\">\n";

    for (int i = 0; i < k; ++i) {
        double pa = metrics.producerAccuracy[i];
        double ua = metrics.userAccuracy[i];

        if (pa < 0.75 && ua < 0.75) {
            html += "<li style=\"color:#e74c3c;\"><b>" + metrics.classNames[i] + "</b>: "
                    + QString::fromUtf8("PA=%1%\u3001UA=%2%\uFF0C\u6F0F\u5206\u4E0E\u9519\u5206\u5747\u4E25\u91CD\u3002\u5EFA\u8BAE\u91CD\u65B0\u8BBE\u8BA1\u8A13\u7EC3\u6837\u672C\u7B56\u7565\uFF0C\u589E\u52A0\u8BE5\u7C7B\u522B\u7279\u5F81\u7EF4\u5EA6\uFF0C\u5E76\u8003\u8651\u91C7\u7528\u591A\u65F6\u76F8/\u591A\u6CE2\u6BB5\u6570\u636E\u8FDB\u884C\u8054\u5408\u5206\u7C7B\u3002")
                        .arg(pa * 100, 0, 'f', 1).arg(ua * 100, 0, 'f', 1)
                    + "</li>\n";
        } else if (pa < 0.75) {
            html += "<li style=\"color:#e67e22;\"><b>" + metrics.classNames[i] + "</b>: "
                    + QString::fromUtf8("\u751F\u4EA7\u8005\u7CBE\u5EA6\u4EC5 %1%\uFF0C\u5B58\u5728\u663E\u8457\u6F0F\u5206\u73B0\u8C61\u3002\u5EFA\u8BAE\u589E\u52A0\u8A13\u7EC3\u6837\u672C\u6570\u91CF\u3001\u4F18\u5316\u7279\u5F81\u7A7A\u95F4\u9009\u62E9\uFF0C\u6216\u5F15\u5165\u7A7A\u95F4\u4E0A\u4E0B\u6587\u7279\u5F81\u3002")
                        .arg(pa * 100, 0, 'f', 1)
                    + "</li>\n";
        } else if (ua < 0.75) {
            html += "<li style=\"color:#e67e22;\"><b>" + metrics.classNames[i] + "</b>: "
                    + QString::fromUtf8("\u7528\u6237\u7CBE\u5EA6\u4EC5 %1%\uFF0C\u5B58\u5728\u663E\u8457\u9519\u5206\u73B0\u8C61\u3002\u5EFA\u8BAE\u589E\u52A0\u8D1F\u6837\u672C\u8BAD\u7EC3\u3001\u8C03\u6574\u5206\u7C7B\u9608\u503C\u6216\u5F15\u5165\u5206\u7C7B\u540E\u5904\u7406\u89C4\u5219\u3002")
                        .arg(ua * 100, 0, 'f', 1)
                    + "</li>\n";
        } else {
            html += "<li style=\"color:#27ae60;\"><b>" + metrics.classNames[i] + "</b>: "
                    + QString::fromUtf8("PA=%1%\u3001UA=%2%\uFF0C\u5206\u7C7B\u7CBE\u5EA6\u826F\u597D\uFF0C\u53EF\u4FDD\u6301\u5F53\u524D\u5206\u7C7B\u7B56\u7565\u3002")
                        .arg(pa * 100, 0, 'f', 1).arg(ua * 100, 0, 'f', 1)
                    + "</li>\n";
        }
    }

    // 总体建议
    html += QString::fromUtf8("<li>\u603B\u4F53\u800C\u8A00\uFF0C\u5F53\u524D\u5206\u7C7B\u7ED3\u679C");
    if (oa >= 0.90)
        html += QString::fromUtf8("\u7CBE\u5EA6\u4F18\u79C0\uFF0C\u53EF\u76F4\u63A5\u5E94\u7528\u4E8E\u751F\u4EA7\u73AF\u5883\uFF0C\u5EFA\u8BAE\u5B9A\u671F\u590D\u6838\u7CBE\u5EA6\u4EE5\u786E\u4FDD\u7A33\u5B9A\u6027\u3002");
    else if (oa >= 0.75)
        html += QString::fromUtf8("\u826F\u597D\uFF0C\u53EF\u7528\u4E8E\u5927\u591A\u6570\u5E94\u7528\u573A\u666F\uFF0C\u5EFA\u8BAE\u5BF9\u8F83\u4F4E\u7CBE\u5EA6\u7C7B\u522B\u8FDB\u884C\u91CD\u70B9\u4F18\u5316\u3002");
    else
        html += QString::fromUtf8("\u6709\u8F83\u5927\u63D0\u5347\u7A7A\u95F4\uFF0C\u5EFA\u8BAE\u91C7\u7528\u591A\u7279\u5F81\u8054\u5408\u5224\u5B9A\u6216\u5F15\u5165\u6DF1\u5EA6\u5B66\u4E60\u65B9\u6CD5\u8FDB\u884C\u6539\u8FDB\u3002");
    html += "</li>\n";

    html += "</ul>\n";
    return html;
}

// =====================================================================
//                      会话版本比较与序列化
// =====================================================================

AccuracyAssessment::SessionDiff AccuracyAssessment::compareSessions(
    const AnnotationSession& sessionA, const AnnotationSession& sessionB)
{
    SessionDiff diff;
    diff.totalRecordsA = sessionA.totalCount();
    diff.totalRecordsB = sessionB.totalCount();
    diff.oaA = sessionA.currentMetrics.overallAccuracy;
    diff.oaB = sessionB.currentMetrics.overallAccuracy;
    diff.kappaA = sessionA.currentMetrics.kappaCoefficient;
    diff.kappaB = sessionB.currentMetrics.kappaCoefficient;

    // 共享的类别名称
    diff.classNames = sessionA.classNames;

    // 按位置匹配记录
    for (size_t i = 0; i < sessionA.records.size(); ++i) {
        const auto& ra = sessionA.records[i];
        for (size_t j = 0; j < sessionB.records.size(); ++j) {
            if (sessionB.records[j].position == ra.position) {
                if (ra.effectiveLabel() != sessionB.records[j].effectiveLabel())
                    diff.changedCount++;
                if (!ra.confirmed && sessionB.records[j].confirmed)
                    diff.newlyConfirmed++;
                break;
            }
        }
    }

    // 精度变化
    diff.oaChange = diff.oaB - diff.oaA;
    diff.kappaChange = diff.kappaB - diff.kappaA;

    int k = static_cast<int>(sessionA.classNames.size());
    for (int i = 0; i < k && i < static_cast<int>(sessionA.currentMetrics.producerAccuracy.size())
                       && i < static_cast<int>(sessionB.currentMetrics.producerAccuracy.size()); ++i) {
        double paDiff = sessionB.currentMetrics.producerAccuracy[i]
                        - sessionA.currentMetrics.producerAccuracy[i];
        double uaDiff = sessionB.currentMetrics.userAccuracy[i]
                        - sessionA.currentMetrics.userAccuracy[i];
        diff.perClassOaChange.push_back(
            sessionA.classNames[i] + ": " + QString::number(paDiff * 100, 'f', 2) + "% (PA) / "
            + QString::number(uaDiff * 100, 'f', 2) + "% (UA)");
        diff.perClassPaChange.push_back(paDiff * 100);
        diff.perClassUaChange.push_back(uaDiff * 100);
    }

    return diff;
}

QString AccuracyAssessment::formatSessionDiffReport(const SessionDiff& diff,
                                                     const QString& nameA, const QString& nameB)
{
    QString html;
    html += "<div class='card'>\n";
    html += "<h2>" + QString::fromUtf8("\u7248\u672C\u5BF9\u6BD4\u62A5\u544A") + "</h2>\n";
    html += "<p style=\"font-size:13px; color:#7f8c8d;\">"
            + QString::fromUtf8("\u6BD4\u8F83: <b>") + nameA + "</b> vs <b>" + nameB + "</b></p>\n";

    // 总体指标对比
    html += "<h3>" + QString::fromUtf8("\u603B\u4F53\u6307\u6807\u5BF9\u6BD4") + "</h3>\n";
    html += "<table>\n";
    html += "<tr><th>" + QString::fromUtf8("\u6307\u6807") + "</th><th>"
            + nameA + "</th><th>" + nameB + "</th><th>" + QString::fromUtf8("\u53D8\u5316") + "</th></tr>\n";

    auto numFmt = [](double v, int prec) -> QString {
        if (std::abs(v) < 0.0001) return "0";
        return QString::number(v, 'f', prec);
    };

    auto changeFmt = [](double v) -> QString {
        if (std::abs(v) < 0.0001)
            return "<span style='color:#7f8c8d;'>\u2014</span>";
        if (v > 0)
            return "<span style='color:#27ae60;font-weight:bold;'>+" + QString::number(v, 'f', 4) + "</span>";
        return "<span style='color:#e74c3c;font-weight:bold;'>" + QString::number(v, 'f', 4) + "</span>";
    };

    html += "<tr><td>" + QString::fromUtf8("\u603B\u4F53\u7CBE\u5EA6 (OA)") + "</td><td>"
            + QString::number(diff.oaA * 100, 'f', 2) + "%</td><td>"
            + QString::number(diff.oaB * 100, 'f', 2) + "%</td><td>"
            + changeFmt(diff.oaChange * 100) + "%</td></tr>\n";
    html += "<tr><td>Kappa " + QString::fromUtf8("\u7CFB\u6570") + "</td><td>"
            + numFmt(diff.kappaA, 4) + "</td><td>"
            + numFmt(diff.kappaB, 4) + "</td><td>"
            + changeFmt(diff.kappaChange) + "</td></tr>\n";
    html += "<tr><td>" + QString::fromUtf8("\u6807\u6CE8\u70B9\u603B\u6570") + "</td><td>"
            + QString::number(diff.totalRecordsA) + "</td><td>"
            + QString::number(diff.totalRecordsB) + "</td><td>"
            + QString::number(diff.totalRecordsB - diff.totalRecordsA) + "</td></tr>\n";
    html += "<tr><td>" + QString::fromUtf8("\u6807\u7B7E\u53D8\u66F4\u6570") + "</td><td colspan='2' style='text-align:center;'>"
            + QString::number(diff.changedCount) + " " + QString::fromUtf8("\u4E2A\u6807\u6CE8\u70B9\u53D1\u751F\u6807\u7B7E\u53D8\u66F4") + "</td><td></td></tr>\n";
    html += "<tr><td>" + QString::fromUtf8("\u65B0\u786E\u8BA4\u6570") + "</td><td colspan='2' style='text-align:center;'>"
            + QString::number(diff.newlyConfirmed) + " " + QString::fromUtf8("\u4E2A\u6807\u6CE8\u70B9\u65B0\u786E\u8BA4") + "</td><td></td></tr>\n";
    html += "</table>\n";

    // 各类别精度详细对比
    if (!diff.classNames.empty() && !diff.perClassPaChange.empty()) {
        html += "<h3>" + QString::fromUtf8("\u5404\u7C7B\u522B\u7CBE\u5EA6\u53D8\u5316") + "</h3>\n";
        html += "<table>\n";
        html += "<tr><th>" + QString::fromUtf8("\u7C7B\u522B") + "</th><th>"
                + QString::fromUtf8("PA\u53D8\u5316(%)") + "</th><th>"
                + QString::fromUtf8("UA\u53D8\u5316(%)") + "</th><th>"
                + QString::fromUtf8("\u8BC4\u4F30") + "</th></tr>\n";
        for (size_t i = 0; i < diff.classNames.size() && i < diff.perClassPaChange.size(); ++i) {
            double paChg = diff.perClassPaChange[i];
            double uaChg = diff.perClassUaChange[i];

            auto pctFmt = [](double v) -> QString {
                if (std::abs(v) < 0.01)
                    return "<span style='color:#7f8c8d;'>\u2014</span>";
                if (v > 0)
                    return "<span style='color:#27ae60;'>+" + QString::number(v, 'f', 2) + "</span>";
                return "<span style='color:#e74c3c;'>" + QString::number(v, 'f', 2) + "</span>";
            };

            // 综合评估
            QString assessment;
            double totalChg = paChg + uaChg;
            if (totalChg > 2.0)
                assessment = "<span style='color:#27ae60;'>" + QString::fromUtf8("\u663E\u8457\u63D0\u5347") + "</span>";
            else if (totalChg > 0.5)
                assessment = "<span style='color:#2980b9;'>" + QString::fromUtf8("\u7565\u6709\u63D0\u5347") + "</span>";
            else if (totalChg < -2.0)
                assessment = "<span style='color:#e74c3c;'>" + QString::fromUtf8("\u660E\u663E\u4E0B\u964D") + "</span>";
            else if (totalChg < -0.5)
                assessment = "<span style='color:#e67e22;'>" + QString::fromUtf8("\u7565\u6709\u4E0B\u964D") + "</span>";
            else
                assessment = "<span style='color:#7f8c8d;'>" + QString::fromUtf8("\u57FA\u672C\u4E0D\u53D8") + "</span>";

            html += "<tr><td><b>" + diff.classNames[i] + "</b></td><td>"
                    + pctFmt(paChg) + "</td><td>"
                    + pctFmt(uaChg) + "</td><td>"
                    + assessment + "</td></tr>\n";
        }
        html += "</table>\n";
    }

    html += "</div>\n";
    return html;
}

QString AccuracyAssessment::serializeSession(const AnnotationSession& session)
{
    // CSV 格式: Header + 每条记录一行
    // 格式: sessionId,name,createTime,lastModified,description,x,y,autoLabel,manualLabel,confirmed,comment,version,className...
    QStringList lines;
    // Header行：会话元数据
    QString header = "##SESSION_META##," + session.sessionId + "," + session.name + ","
                     + session.createTime.toString(Qt::ISODate) + ","
                     + session.lastModified.toString(Qt::ISODate) + ","
                     + session.description + ","
                     + QString::number(session.classNames.size()) + ","
                     + QStringList(session.classNames.begin(), session.classNames.end()).join("|");
    lines << header;

    // 记录行：每条标注
    lines << "##RECORDS_START##";
    for (const auto& r : session.records) {
        QString escapedComment = r.comment;
        escapedComment.replace(",", "\\c").replace("\n", "\\n");
        QString line = QString("%1,%2,%3,%4,%5,%6,%7")
                           .arg(r.position.x())
                           .arg(r.position.y())
                           .arg(r.autoLabel)
                           .arg(r.manualLabel)
                           .arg(r.confirmed ? 1 : 0)
                           .arg(r.version)
                           .arg(escapedComment);
        lines << line;
    }
    lines << "##RECORDS_END##";
    return lines.join("\n");
}

AnnotationSession AccuracyAssessment::deserializeSession(const QString& data)
{
    AnnotationSession session;
    QStringList lines = data.split('\n');
    bool inRecords = false;

    for (const QString& line : lines) {
        if (line.startsWith("##SESSION_META##")) {
            QStringList parts = line.split(',');
            if (parts.size() >= 7) {
                session.sessionId = parts.value(1);
                session.name = parts.value(2);
                session.createTime = QDateTime::fromString(parts.value(3), Qt::ISODate);
                session.lastModified = QDateTime::fromString(parts.value(4), Qt::ISODate);
                session.description = parts.value(5);
                QString cnames = parts.value(7);
                QStringList cnSplit = cnames.split('|');
                session.classNames.assign(cnSplit.begin(), cnSplit.end());
            }
        } else if (line.startsWith("##RECORDS_START##")) {
            inRecords = true;
        } else if (line.startsWith("##RECORDS_END##")) {
            inRecords = false;
        } else if (inRecords) {
            QStringList parts = line.split(',');
            if (parts.size() >= 6) {
                AnnotationRecord r;
                r.position = QPoint(parts[0].toInt(), parts[1].toInt());
                r.autoLabel = parts[2].toInt();
                r.manualLabel = parts[3].toInt();
                r.confirmed = (parts[4].toInt() != 0);
                r.version = parts[5].toInt();
                if (parts.size() >= 7)
                    r.comment = parts[6].replace("\\c", ",").replace("\\n", "\n");
                session.records.push_back(r);
            }
        }
    }

    return session;
}