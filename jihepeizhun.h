#pragma once

#include <QDialog>
#include <QSplitter>
#include <QTableWidget>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QAction>
#include <QMenu>

class QMenuBar;
class QStatusBar;
#include <QToolBar>

#include "imageview.h"
#include "controlpoint.h"
#include "geomodel.h"
#include "pickmanager.h"
#include "automatch.h"

/**
 * @brief 几何配准对话框
 *
 * 应用程序的主界面，负责统筹影像加载、人工刺点、自动匹配、
 * 几何配准解算、正射校正及结果导出等完整工作流。
 * 采用双视图布局（源影像 + 参考影像），通过 PickManager
 * 管理刺点交互，通过 GeoModel 执行最小二乘平差解算。
 */
class jihepeizhun : public QDialog
{
    Q_OBJECT

public:
    jihepeizhun(QWidget* parent = nullptr);
    ~jihepeizhun();

    void setOutputDir(const QString &dir);
    QStringList getResultPaths() const;

signals:
    void resultReady(const QString &type, const QStringList &paths);

private slots:
    // ==================== 文件操作 ====================
    void onOpenSourceImage();       // 打开源影像（待配准影像）
    void onOpenRefImage();          // 打开参考影像
    void onSaveRegisteredImage();   // 保存配准后的影像
    void onExportControlPoints();   // 导出控制点坐标到文件
    void onImportControlPoints();   // 从文件导入控制点坐标

    // ==================== 配准操作 ====================
    void onTogglePickMode();        // 切换刺点模式（开始/结束刺点）
    void onClearControlPoints();    // 清空所有控制点
    void onDeleteLastPoint();       // 删除最后一个控制点
    void onDeleteSelectedPoint();   // 删除表格中选中的控制点
    void onAutoMatch();             // 执行自动匹配（Harris + NCC + RANSAC）
    void onRunRegistration();       // 执行配准解算（最小二乘平差）
    void onControlPointClicked(int row, int column);  // 点击表格行跳转到对应刺点位置

    // PickManager 回调：接收到一对完整的同名点后的处理
    void onPickPairReady(ControlPoint cp);

    // ==================== 视图操作 ====================
    void onFitToWindow();           // 适应窗口大小显示影像
    void onZoomIn();                // 放大影像
    void onZoomOut();               // 缩小影像

    // ==================== 状态栏更新 ====================
    void onSrcMouseMoved(QPointF imgCoord);  // 源影像鼠标坐标更新
    void onRefMouseMoved(QPointF imgCoord);  // 参考影像鼠标坐标更新

    void onAbout();                 // 显示"关于"对话框
    void onOrthoCorrection();       // 打开正射校正对话框

    // ==================== 重叠区域/特征点操作 ====================
    void onComputeOverlap();        // 计算重叠区域
    void onExportOverlap();         // 导出重叠区域
    void onExtractFeatures();       // 提取并显示特征点

private:
    void setupUI();                 // 初始化界面布局
    void setupMenuBar();            // 设置菜单栏
    void setupToolBar();            // 设置工具栏
    void setupStatusBar();          // 设置状态栏
    void setupControlPanel();       // 设置右侧控制面板（dock）
    void setupConnections();        // 连接信号与槽

    void updateControlPointTable(); // 刷新控制点表格显示
    void updatePickStatus();        // 更新刺点状态提示
    void showFeaturePoints();       // 在影像视图中显示特征点

private:
    // ==================== 影像视图 ====================
    ImageView*    m_srcView;   // 源影像视图（显示待配准影像）
    ImageView*    m_refView;   // 参考影像视图（显示参考基准影像）
    QSplitter*    m_splitter;  // 水平分割器，左右排列源/参考视图

    // ==================== 刺点管理器 ====================
    PickManager*  m_pickManager;  // 刺点交互管理器（状态机：Idle→WaitSrc→WaitRef）

    // ==================== 菜单栏 / 工具栏 / 状态栏 ====================
    QMenuBar*     m_menuBar;      // 菜单栏
    QToolBar*     m_toolBar;      // 工具栏
    QStatusBar*   m_statusBar;    // 状态栏

    // ==================== 控制面板 ====================
    QWidget*      m_controlPanel;     // 控制面板
    QWidget*      m_controlContainer; // 控制面板容器
    QTableWidget* m_cpTable;      // 控制点列表表格
    QComboBox*    m_modelCombo;   // 几何模型选择下拉框（仿射/二次多项式）
    QPushButton*  m_runBtn;       // "执行配准"按钮
    QPushButton*  m_clearBtn;     // "清空控制点"按钮
    QPushButton*  m_importBtn;    // "导入控制点"按钮
    QLabel*       m_statusLabel;  // 配准状态/精度信息标签

    // ==================== 工具栏按钮 ====================
    QAction*      m_pickAction;   // "刺点模式"切换动作（可选中/取消）
    QAction*      m_runAction;    // "执行配准"动作

    // ==================== 状态栏标签 ====================
    QLabel*       m_srcCoordLabel;  // 源影像鼠标坐标显示
    QLabel*       m_refCoordLabel;  // 参考影像鼠标坐标显示

    // ==================== 数据 ====================
    QVector<ControlPoint> m_controlPoints;  // 所有控制点列表
    GeoModel      m_geoModel;    // 几何变换模型（含平差解算功能）
    bool          m_isRegistered;  // 是否已完成配准解算
    double        m_lastRMSE = 0.0;  // 最近一次配准的总体均方根误差
    OverlapResult m_overlapResult;  // 缓存的重叠区域分析结果（供导出使用）

    QString       m_outputDir;    // 输出目录
    QStringList   m_resultPaths;  // 结果文件路径列表
};
