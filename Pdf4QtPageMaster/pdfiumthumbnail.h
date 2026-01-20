// PDF4QT-Opus: PDFium-based fast thumbnail renderer
// Uses Google's PDFium library for high-performance PDF page rendering

#ifndef PDFIUMTHUMBNAIL_H
#define PDFIUMTHUMBNAIL_H

#include <QImage>
#include <QMutex>
#include <QString>


namespace pdfpagemaster {

/// Fast PDF thumbnail renderer using PDFium (Google Chrome's PDF engine)
/// This provides NAPS2-level performance for thumbnail generation
class PdfiumThumbnail {
public:
  /// Initialize PDFium library (call once at startup)
  static bool initialize();

  /// Shutdown PDFium library (call at app exit)
  static void shutdown();

  /// Check if PDFium is available
  static bool isAvailable();

  /// Render a page thumbnail using PDFium
  /// @param pdfPath Path to PDF file
  /// @param pageIndex 0-based page index
  /// @param targetSize Target thumbnail size
  /// @return Rendered thumbnail, or null image if failed
  static QImage renderPage(const QString &pdfPath, int pageIndex,
                           QSize targetSize);

  /// Try to get embedded thumbnail from PDF (fastest option)
  /// @param pdfPath Path to PDF file
  /// @param pageIndex 0-based page index
  /// @return Embedded thumbnail bitmap, or null if not available
  static QImage getEmbeddedThumbnail(const QString &pdfPath, int pageIndex);

private:
  static bool s_initialized;
  static QMutex s_mutex;
};

} // namespace pdfpagemaster

#endif // PDFIUMTHUMBNAIL_H
