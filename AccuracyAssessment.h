#pragma once

#include "GeoImageData.h"
#include <QObject>

class AccuracyAssessment : public QObject
{
    Q_OBJECT

public:
    explicit AccuracyAssessment(QObject* parent = nullptr);
    ~AccuracyAssessment();

    // ----- 基础混淆矩阵计算 -----

    AccuracyMetrics computeConfusionMatrix(const ClassificationResult& classification,
                                            const std::vector<QPoint>& referencePoints,
                                            const std::vector<int>& referenceLabels,
                                            const std::vector<QString>& classNames);

    AccuracyMetrics computeFromLabelMap(const ClassificationResult& classification,
                                         const std::vector<int>& referenceMap,
                                         const std::vector<QString>& classNames);

    double computeKappaCoefficient(const std::vector<std::vector<int>>& confusionMatrix,
                                    int totalSamples) const;

    double computeOverallAccuracy(const std::vector<std::vector<int>>& confusionMatrix,
                                   int totalSamples) const;

    std::vector<double> computeProducerAccuracy(const std::vector<std::vector<int>>& confusionMatrix,
                                                  const std::vector<int>& classTotalReference) const;

    std::vector<double> computeUserAccuracy(const std::vector<std::vector<int>>& confusionMatrix,
                                              const std::vector<int>& classTotalClassified) const;

    std::vector<double> computeF1Scores(const std::vector<double>& producerAccuracy,
                                         const std::vector<double>& userAccuracy) const;

    double computeMacroF1(const std::vector<double>& f1Scores) const;

    // ----- 进阶导出 -----
    static bool exportReportToPDF(const QString& htmlReport, const QString& filePath);
    static bool exportReportToExcel(const AccuracyMetrics& metrics, const QString& filePath,
                                     const AnnotationSession* session = nullptr);

    // ----- 人工判读功能 -----

    //  从分类结果和自动标签构建判读会话
    AnnotationSession createSession(const ClassificationResult& classification,
                                     const std::vector<QPoint>& samplePoints) const;

    //  基于判读记录重新计算精度指标（实时更新）
    AccuracyMetrics computeFromAnnotations(const std::vector<AnnotationRecord>& records,
                                            const std::vector<QString>& classNames) const;

    //  更新单个标注点并返回值是否改变了指标
    bool updateAnnotation(AnnotationRecord& record, int newManualLabel,
                          const QString& comment = QString());

    //  批量确认/取消确认
    void confirmAll(std::vector<AnnotationRecord>& records, bool confirmed = true);

    //  重置所有人工修正
    void resetAllOverrides(std::vector<AnnotationRecord>& records);

    //  通过坐标查找标注记录（返回索引，-1表示未找到）
    int findRecordByPosition(const std::vector<AnnotationRecord>& records,
                             const QPoint& pos) const;

    // ----- 专业报表生成 -----

    //  生成专业精度评定报告（HTML格式，含统计、图表、结论建议）
    static QString generateProfessionalReport(const AccuracyMetrics& metrics,
                                               const AnnotationSession* session = nullptr);

    //  生成精度分布柱状图HTML（内联SVG/CSS条形图）
    static QString generatePerClassBarChart(const AccuracyMetrics& metrics);

    //  生成统计分析摘要HTML
    static QString generateStatisticalSummary(const AccuracyMetrics& metrics);

    //  生成结论与建议HTML
    static QString generateConclusions(const AccuracyMetrics& metrics);

    // ----- 会话版本比较 -----

    //  比较两个会话的差异
    struct SessionDiff
    {
        int totalRecordsA = 0;
        int totalRecordsB = 0;
        int changedCount = 0;        // 标签发生变更的记录数
        int newlyConfirmed = 0;      // 新确认的记录数
        double oaChange = 0;         // OA变化量
        double kappaChange = 0;      // Kappa变化量
        double oaA = 0;              // 会话A的OA
        double oaB = 0;              // 会话B的OA
        double kappaA = 0;           // 会话A的Kappa
        double kappaB = 0;           // 会话B的Kappa
        std::vector<QString> perClassOaChange;  // 各类别精度变化
        std::vector<double> perClassPaChange;   // 各类别PA变化
        std::vector<double> perClassUaChange;   // 各类别UA变化
        std::vector<QString> classNames;         // 类别名称（共享）
    };

    static SessionDiff compareSessions(const AnnotationSession& sessionA,
                                        const AnnotationSession& sessionB);

    //  格式化比较报告为HTML
    static QString formatSessionDiffReport(const SessionDiff& diff,
                                            const QString& nameA,
                                            const QString& nameB);

    // ----- 静态格式化工具 -----

    static QString formatAccuracyReport(const AccuracyMetrics& metrics);
    static QString confusionMatrixToHTML(const AccuracyMetrics& metrics);
    static QString confusionMatrixTableHTML(const AccuracyMetrics& metrics);

    //  生成会话保存字符串（CSV格式，含所有标注信息）
    static QString serializeSession(const AnnotationSession& session);
    //  从CSV字符串反序列化会话
    static AnnotationSession deserializeSession(const QString& data);

signals:
    void statusMessage(const QString& msg);
    void annotationChanged(int recordIndex);  // 单条标注变更信号
    void metricsRecalculated(const AccuracyMetrics& metrics);  // 指标重新计算信号
};