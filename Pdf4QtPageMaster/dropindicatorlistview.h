// PDF4QT-Opus: Custom QListView with visible drop indicator
// Paints a clear vertical indicator during drag-drop operations

#ifndef DROPINDICATORLISTVIEW_H
#define DROPINDICATORLISTVIEW_H

#include <QDragMoveEvent>
#include <QListView>
#include <QPainter>

namespace pdfpagemaster {

/// Custom QListView that paints a highly visible vertical drop indicator
class DropIndicatorListView : public QListView {
  Q_OBJECT

public:
  explicit DropIndicatorListView(QWidget *parent = nullptr)
      : QListView(parent), m_dropIndex(-1), m_insertBefore(true) {
    setAcceptDrops(true);
    setDropIndicatorShown(false); // We'll paint our own
    setDragDropMode(QAbstractItemView::InternalMove);
    setDefaultDropAction(Qt::MoveAction);
  }

protected:
  void dragMoveEvent(QDragMoveEvent *event) override {
    QListView::dragMoveEvent(event);

    QPoint pos = event->position().toPoint();
    QModelIndex index = indexAt(pos);

    if (index.isValid()) {
      QRect rect = visualRect(index);
      m_dropIndex = index.row();
      m_targetRect = rect;
      // Check X position: left half = before, right half = after
      m_insertBefore = (pos.x() < rect.center().x());
    } else {
      m_dropIndex = model() ? model()->rowCount() : 0;
      m_insertBefore = false;
      if (model() && model()->rowCount() > 0) {
        QModelIndex lastIdx = model()->index(model()->rowCount() - 1, 0);
        m_targetRect = visualRect(lastIdx);
      }
    }
    viewport()->update();
  }

  void dragLeaveEvent(QDragLeaveEvent *event) override {
    QListView::dragLeaveEvent(event);
    m_dropIndex = -1;
    viewport()->update();
  }

  void dropEvent(QDropEvent *event) override {
    QListView::dropEvent(event);
    m_dropIndex = -1;
    viewport()->update();
  }

  void paintEvent(QPaintEvent *event) override {
    QListView::paintEvent(event);

    // Paint vertical drop indicator only
    if (m_dropIndex >= 0 && model()) {
      QPainter painter(viewport());
      painter.setRenderHint(QPainter::Antialiasing);

      const int thickness = 6;
      QRect indicatorRect;

      if (m_insertBefore) {
        // Vertical line on LEFT side of target item
        indicatorRect =
            QRect(m_targetRect.left() - thickness / 2, m_targetRect.top(),
                  thickness, m_targetRect.height());
      } else {
        // Vertical line on RIGHT side of target item
        indicatorRect =
            QRect(m_targetRect.right() - thickness / 2, m_targetRect.top(),
                  thickness, m_targetRect.height());
      }

      // Draw bright orange vertical bar
      painter.fillRect(indicatorRect, QColor(255, 102, 0));
      painter.setPen(QPen(QColor(255, 0, 0), 2));
      painter.drawRect(indicatorRect);
    }
  }

private:
  int m_dropIndex;
  bool m_insertBefore;
  QRect m_targetRect;
};

} // namespace pdfpagemaster

#endif // DROPINDICATORLISTVIEW_H
