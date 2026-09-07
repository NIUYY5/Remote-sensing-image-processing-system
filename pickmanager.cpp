// ============================================================================
// 文件: pickmanager.cpp
// 功能: 刺点管理器 — 控制手动刺点流程的状态机
// 状态流转: Idle → WaitSrc (等待源影像刺点) → WaitRef (等待参考影像刺同名点) → Idle
// 每个完整周期产生一对 ControlPoint 并发射 pairReady 信号
// ============================================================================

#include "pickmanager.h"

// ============================================================================
// 构造函数 — 注册两个 ImageView 的 pointPicked 信号
// 将源影像和参考影像的刺点信号分别关联到 onSrcPicked 和 onRefPicked 槽函数
// ============================================================================
PickManager::PickManager(ImageView* srcView, ImageView* refView,
                          QStatusBar* statusBar, QAction* pickAction,
                          QObject* parent)
    : QObject(parent)
    , m_srcView(srcView)
    , m_refView(refView)
    , m_statusBar(statusBar)
    , m_pickAction(pickAction)
    , m_state(Idle)
    , m_tempSrcX(0)
    , m_tempSrcY(0)
    , m_hasTempSrc(false)
{
    connect(m_srcView, &ImageView::pointPicked,
            this, &PickManager::onSrcPicked);
    connect(m_refView, &ImageView::pointPicked,
            this, &PickManager::onRefPicked);
}

// ============================================================================
// startPicking — 进入刺点模式，状态 → WaitSrc
// 检查两幅影像均已加载，然后将两个 ImageView 切换为刺点模式，
// 状态机进入 WaitSrc，等待用户在源影像上刺点
// ============================================================================
void PickManager::startPicking()
{
    if (!m_srcView->isLoaded() || !m_refView->isLoaded()) {
        if (m_pickAction) m_pickAction->setChecked(false);
        return;
    }

    m_srcView->setPickMode(ImageView::Picking);
    m_refView->setPickMode(ImageView::Picking);
    m_state = WaitSrc;
    m_hasTempSrc = false;
    emit statusChanged(QString::fromUtf8(
        "◉ 刺点模式: 请在源影像上左键定位十字丝、右键确认"));
}

// ============================================================================
// stopPicking — 退出刺点模式，状态 → Idle
// 将两个 ImageView 恢复为普通浏览模式，清空临时刺点数据
// ============================================================================
void PickManager::stopPicking()
{
    m_state = Idle;
    m_hasTempSrc = false;
    m_srcView->setPickMode(ImageView::Normal);
    m_refView->setPickMode(ImageView::Normal);
    emit statusChanged(QString::fromUtf8("● 浏览模式"));
}

// ============================================================================
// togglePicking — 切换刺点/浏览模式
// 根据当前状态在刺点模式和浏览模式之间切换，由外部 Action 触发
// ============================================================================
void PickManager::togglePicking()
{
    if (m_state == Idle) {
        if (!m_srcView->isLoaded() || !m_refView->isLoaded()) {
            if (m_pickAction) m_pickAction->setChecked(false);
            emit statusChanged(QString::fromUtf8("请先加载源影像和参考影像"));
            return;
        }
        startPicking();
    } else {
        stopPicking();
    }
}

// ============================================================================
// onSrcPicked — 接收源影像刺点信号，保存临时坐标，状态 → WaitRef
// 在状态为 WaitSrc 时接收源影像的刺点坐标，保存到临时变量，
// 然后将状态切换到 WaitRef，提示用户在参考影像上刺同名点
// ============================================================================
void PickManager::onSrcPicked(QPointF imgCoord)
{
    if (m_state != WaitSrc) return;

    m_tempSrcX = imgCoord.x();
    m_tempSrcY = imgCoord.y();
    m_hasTempSrc = true;
    m_state = WaitRef;

    if (m_statusBar) {
        m_statusBar->showMessage(
            QString::fromUtf8("已在源影像刺点 (%1, %2)，请在参考影像刺同名点 (左键定位/右键确认)")
                .arg(imgCoord.x(), 0, 'f', 3).arg(imgCoord.y(), 0, 'f', 3));
    }

    emit statusChanged(QString::fromUtf8(
        "◉ 刺点模式: 请在参考影像上左键定位十字丝、右键确认"));
}

// ============================================================================
// onRefPicked — 接收参考影像刺点信号，构造 ControlPoint，发射 pairReady，状态 → WaitSrc
// 在状态为 WaitRef 且有临时源坐标时，用源坐标和参考坐标构造 ControlPoint，
// 发射 pairReady 信号供外部收集，然后回到 WaitSrc 等待下一对刺点
// ============================================================================
void PickManager::onRefPicked(QPointF imgCoord)
{
    if (m_state != WaitRef || !m_hasTempSrc) return;

    ControlPoint cp(0, m_tempSrcX, m_tempSrcY, imgCoord.x(), imgCoord.y());
    m_hasTempSrc = false;
    m_state = WaitSrc;

    emit pairReady(cp);
    emit statusChanged(QString::fromUtf8(
        "◉ 刺点模式: 请在源影像上左键定位十字丝、右键确认"));
}
