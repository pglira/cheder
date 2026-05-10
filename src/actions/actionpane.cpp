#include "actionpane.h"

#include "action.h"
#include "actionbar.h"
#include "actionlogger.h"

#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QPalette>
#include <QResizeEvent>
#include <QTextCursor>
#include <QTextEdit>
#include <QTime>
#include <QToolButton>
#include <QVBoxLayout>

ActionPane::ActionPane(ActionRegistry *registry, QWidget *parent)
    : QWidget(parent),
      m_logger(new ActionLogger(this)),
      m_bar(new ActionBar(registry, this)),
      m_logStrip(new QWidget(this)),
      m_logLatest(new QLabel(m_logStrip)),
      m_logToggle(new QToolButton(m_logStrip)),
      m_logHistory(new QTextEdit(this)) {

    m_logHistory->setReadOnly(true);
    m_logHistory->setLineWrapMode(QTextEdit::NoWrap);
    m_logHistory->setMaximumHeight(180);
    m_logHistory->setVisible(false);

    m_logLatest->setTextFormat(Qt::PlainText);
    m_logLatest->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_logLatest->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_logLatest->setMinimumWidth(0);  // allow shrink for elision

    m_logToggle->setText(QStringLiteral("▴"));  // up-pointing triangle
    m_logToggle->setAutoRaise(true);
    m_logToggle->setCursor(Qt::PointingHandCursor);
    m_logToggle->setToolTip("Show log history");

    auto *stripLay = new QHBoxLayout(m_logStrip);
    stripLay->setContentsMargins(6, 0, 4, 0);
    stripLay->setSpacing(4);
    stripLay->addWidget(m_logLatest, 1);
    stripLay->addWidget(m_logToggle);
    m_logStrip->setVisible(false);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addWidget(m_logHistory);
    root->addWidget(m_logStrip);
    root->addWidget(m_bar);

    connect(m_logger,    &ActionLogger::logged, this, &ActionPane::appendLog);
    connect(m_logToggle, &QToolButton::clicked, this, &ActionPane::toggleHistory);
    connect(m_bar, &ActionBar::actionInvoked, this, &ActionPane::actionInvoked);
    connect(m_bar, &ActionBar::exitRequested, this, &ActionPane::exitRequested);
}

void ActionPane::focusInput()       { m_bar->focusInput(); }
void ActionPane::resetState()       { m_bar->resetState(); }
bool ActionPane::isInputFocused() const { return m_bar->isInputFocused(); }

void ActionPane::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    updateStripText();
}

void ActionPane::appendLog(int level, const QString &message) {
    const QString ts = QTime::currentTime().toString("hh:mm:ss");

    // History: HTML so colors per entry survive in scrollback.
    QString color;
    switch (level) {
    case ActionLogger::Error: color = "#d33"; break;
    case ActionLogger::Warn:  color = "#c80"; break;
    default:                  color.clear();
    }
    const QString safe = message.toHtmlEscaped();
    const QString html = color.isEmpty()
        ? QString("<span>%1&nbsp;&nbsp;%2</span>").arg(ts, safe)
        : QString("<span style=\"color:%1;\">%2&nbsp;&nbsp;%3</span>").arg(color, ts, safe);
    m_logHistory->append(html);

    // Strip: plain text with palette-driven color so we can elide cleanly.
    m_lastFull  = ts + "  " + message;
    m_lastLevel = level;
    QPalette pal = m_logLatest->palette();
    QColor c;
    switch (level) {
    case ActionLogger::Error: c = QColor("#d33"); break;
    case ActionLogger::Warn:  c = QColor("#c80"); break;
    default:                  c = m_logStrip->palette().color(QPalette::WindowText);
    }
    pal.setColor(QPalette::WindowText, c);
    m_logLatest->setPalette(pal);

    m_logStrip->setVisible(true);
    updateStripText();
}

void ActionPane::updateStripText() {
    if (m_lastFull.isEmpty()) return;
    const QFontMetrics fm(m_logLatest->font());
    const int avail = std::max(0, m_logLatest->width() - 4);
    m_logLatest->setText(fm.elidedText(m_lastFull, Qt::ElideRight, avail));
}

void ActionPane::toggleHistory() {
    const bool expand = !m_logHistory->isVisible();
    m_logHistory->setVisible(expand);
    m_logToggle->setText(expand ? QStringLiteral("▾")   // down-pointing
                                : QStringLiteral("▴")); // up-pointing
    m_logToggle->setToolTip(expand ? "Hide log history" : "Show log history");
    if (expand) {
        QTextCursor c = m_logHistory->textCursor();
        c.movePosition(QTextCursor::End);
        m_logHistory->setTextCursor(c);
    }
}
