#pragma once

#include <QDialog>
#include <QLineEdit>
#include <QPushButton>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QProgressBar>
#include <QLabel>
#include <QCheckBox>
#include <QGroupBox>
#include <QRadioButton>

#include "gdal_priv.h"

/**
 * @brief 正射校正对话框
 *
 * 基于 GDALWarp 的正射校正功能对话框，支持 RPC 有理多项式模型
 * 和 DEM 高程校正。提供简洁的操作界面用于设置输入/输出参数。
 *
 * 功能特性：
 * - 支持含 RPC 的卫星影像正射校正
 * - 支持 DEM 高程文件参与校正
 * - 多种重采样方法可选（最近邻、双线性、三次卷积等）
 * - 输出数据类型和压缩选项可配置
 * - 自动处理多波段影像的色彩信息
 */
class OrthoDialog : public QDialog
{
    Q_OBJECT

public:
    explicit OrthoDialog(QWidget* parent = nullptr);
    ~OrthoDialog();

    void setCurrentSourcePath(const QString& path);  // 设置当前源影像路径

private slots:
    void onBrowseInput();      // 浏览选择输入影像文件
    void onBrowseDEM();        // 浏览选择 DEM 高程文件
    void onBrowseOutput();     // 浏览选择输出文件路径
    void onExecute();          // 执行正射校正

private:
    /**
     * @brief 执行 GDALWarp 正射校正
     *
     * 构造 GDALWarpOptions，调用 GDALAutoCreateWarpedVRT 实现重采样，
     * 最终输出到文件。过程中更新进度条。
     */
    bool executeWarp(const QString& inputPath, const QString& demPath,
                     const QString& outputPath);

    // ==================== UI 控件 ====================

    // --- 输入输出组 ---
    QLineEdit*    m_inputEdit;       // 输入影像文件路径编辑框
    QLineEdit*    m_demEdit;         // DEM 高程文件路径编辑框
    QLineEdit*    m_outputEdit;      // 输出正射影像文件路径编辑框
    QCheckBox*    m_useSrcChk;       // "使用源影像范围"复选框

    // --- 输出参数组 ---
    QLineEdit*       m_epsgEdit;        // 输出坐标系 EPSG 代码
    QDoubleSpinBox*  m_resSpin;         // 输出分辨率
    QComboBox*       m_resampleCombo;   // 重采样方法下拉框
    QComboBox*       m_dtypeCombo;      // 输出数据类型下拉框
    QCheckBox*       m_compressChk;     // 是否启用 LZW 压缩

    // --- 进度和状态 ---
    QProgressBar* m_progressBar;      // 处理进度条
    QLabel*       m_statusLabel;      // 状态文字提示标签
    QPushButton*  m_execBtn;          // "执行"按钮
};