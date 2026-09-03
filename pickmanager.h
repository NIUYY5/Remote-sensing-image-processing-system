#pragma once

#include <QObject>
#include <QPointF>
#include <QAction>
#include <QStatusBar>

#include "controlpoint.h"
#include "imageview.h"

/**
 * @brief 刺点管理器
 *
 * 管理人工刺点交互流程的状态机，控制用户在源影像和参考影像上
 * 依次刺取同名点的完整流程。
 *
 * 工作流程：
 *   1. Idle（空闲态）：等待用户点击"刺点模式"按钮
 *   2. WaitSrc（等待源影像刺点）：用户在源影像上点击选取一个点
 *   3. WaitRef（等待参考影像刺点）：自动切换到参考影像，用户点击选取同名点
 *   4. 一对同名点刺取完成 → 发射 pairReady 信号 → 回到 Idle
 *      或自动进入下一轮 WaitSrc（继续刺点模式）
 *
 * 该流程确保用户每次先在源影像上刺点，然后在参考影像上刺取同名点，
 * 保证配对的一致性。
 */
class PickManager : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief 刺点状态机枚举
     *
     * 定义刺点流程的三个状态：
     * - Idle:    空闲状态，不响应鼠标点击，影像视图为 Normal 模式
     * - WaitSrc: 等待用户在源影像上点击选取一个点
     * - WaitRef: 等待用户在参考影像上点击选取同名点
     */
    enum State { Idle, WaitSrc, WaitRef };

    /**
     * @brief 构造函数
     * @param srcView  源影像视图指针
     * @param refView  参考影像视图指针
     * @param statusBar 状态栏指针（用于显示提示信息）
     * @param pickAction 刺点模式切换动作（用于同步工具栏按钮状态）
     * @param parent QObject 父对象
     */
    PickManager(ImageView* srcView, ImageView* refView,
                QStatusBar* statusBar, QAction* pickAction,
                QObject* parent = nullptr);

    State currentState() const { return m_state; }  // 获取当前状态
    bool isPicking() const { return m_state != Idle; }  // 是否处于刺点流程中

public slots:
    void startPicking();   // 开始刺点（进入 WaitSrc 状态）
    void stopPicking();    // 停止刺点（回到 Idle 状态）
    void togglePicking();  // 切换刺点模式（开始/停止切换）

signals:
    // 一对同名点刺取完成信号：包含源/参考坐标的 ControlPoint
    void pairReady(ControlPoint cp);
    // 刺点状态变化信号：用于更新状态栏提示文字
    void statusChanged(QString message);

private slots:
    void onSrcPicked(QPointF imgCoord);  // 源影像刺点回调（WaitSrc→WaitRef）
    void onRefPicked(QPointF imgCoord);  // 参考影像刺点回调（WaitRef→Idle）

private:
    ImageView*     m_srcView;      // 源影像视图指针
    ImageView*     m_refView;      // 参考影像视图指针
    QStatusBar*    m_statusBar;    // 状态栏指针（用于显示"请刺取源/参考影像"提示）
    QAction*       m_pickAction;   // 工具栏"刺点模式"动作（同步选中/取消状态）

    State          m_state;        // 当前状态机状态
    double         m_tempSrcX;     // 临时存储当前配对的源影像 X 坐标
    double         m_tempSrcY;     // 临时存储当前配对的源影像 Y 坐标
    bool           m_hasTempSrc;   // 是否已在源影像上刺取到临时点
};
