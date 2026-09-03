#pragma once

#include <QDialog>
#include <QTableWidget>
#include <QTextEdit>
#include <QPushButton>
#include <QLabel>

#include "controlpoint.h"

/**
 * @brief 精度评定对话框
 *
 * 显示配准精度评定报告的对话框，包含：
 * - 每个控制点的残差明细表格（点号、源坐标、参考坐标、残差）
 * - 精度汇总文字报告（单位权中误差、RMSE、最大残差等）
 * - 支持将报告导出为文本文件
 *
 * 通常在配准解算完成后自动弹出，也可在菜单中手动调出。
 */
class AccuracyReportDialog : public QDialog
{
    Q_OBJECT

public:
    /**
     * @brief 构造函数
     * @param modelType 使用的几何变换模型类型
     * @param points 控制点列表（含解算后回填的残差）
     * @param result 平差解算结果
     * @param parent 父窗口
     */
    AccuracyReportDialog(GeoModelType modelType,
                          const QVector<ControlPoint>& points,
                          const AdjustmentResult& result,
                          QWidget* parent = nullptr);

private slots:
    void onExport();  // 导出精度报告到文本文件

private:
    void setupUI();                      // 初始化界面布局
    QString buildReport() const;         // 构建精度报告的文本内容

    GeoModelType         m_modelType;    // 使用的几何变换模型类型
    QVector<ControlPoint> m_points;      // 控制点列表（含残差数据）
    AdjustmentResult     m_result;       // 平差解算结果

    QTableWidget* m_table;              // 控制点残差明细表格
    QTextEdit*    m_reportText;          // 精度报告文本显示区
    QPushButton*  m_exportBtn;           // "导出报告"按钮
    QLabel*       m_summaryLabel;        // 精度概览摘要标签（如 RMSE 汇总）
};
