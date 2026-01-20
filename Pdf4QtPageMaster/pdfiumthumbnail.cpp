// PDF4QT-Opus: PDFium-based fast thumbnail renderer
// Uses Google's PDFium library for high-performance PDF page rendering

#include "pdfiumthumbnail.h"

#include <QDebug>
#include <QFile>


// PDFium headers
#include "fpdf_thumbnail.h"
#include "fpdfview.h"


namespace pdfpagemaster {

bool PdfiumThumbnail::s_initialized = false;
QMutex PdfiumThumbnail::s_mutex;

bool PdfiumThumbnail::initialize() {
  QMutexLocker lock(&s_mutex);
  if (s_initialized) {
    return true;
  }

  FPDF_LIBRARY_CONFIG config;
  config.version = 2;
  config.m_pUserFontPaths = nullptr;
  config.m_pIsolate = nullptr;
  config.m_v8EmbedderSlot = 0;
  config.m_pPlatform = nullptr;

  FPDF_InitLibraryWithConfig(&config);
  s_initialized = true;
  qDebug() << "PDFium initialized for fast thumbnails";
  return true;
}

void PdfiumThumbnail::shutdown() {
  QMutexLocker lock(&s_mutex);
  if (s_initialized) {
    FPDF_DestroyLibrary();
    s_initialized = false;
  }
}

bool PdfiumThumbnail::isAvailable() { return s_initialized; }

QImage PdfiumThumbnail::getEmbeddedThumbnail(const QString &pdfPath,
                                             int pageIndex) {
  if (!s_initialized) {
    return QImage();
  }

  // Load PDF document
  QByteArray pathUtf8 = pdfPath.toUtf8();
  FPDF_DOCUMENT doc = FPDF_LoadDocument(pathUtf8.constData(), nullptr);
  if (!doc) {
    return QImage();
  }

  // Load page
  FPDF_PAGE page = FPDF_LoadPage(doc, pageIndex);
  if (!page) {
    FPDF_CloseDocument(doc);
    return QImage();
  }

  // Try to get embedded thumbnail
  FPDF_BITMAP thumbBitmap = FPDFPage_GetThumbnailAsBitmap(page);
  QImage result;

  if (thumbBitmap) {
    int width = FPDFBitmap_GetWidth(thumbBitmap);
    int height = FPDFBitmap_GetHeight(thumbBitmap);
    int stride = FPDFBitmap_GetStride(thumbBitmap);
    void *buffer = FPDFBitmap_GetBuffer(thumbBitmap);

    // Create QImage from PDFium bitmap (BGRA format)
    result = QImage(static_cast<uchar *>(buffer), width, height, stride,
                    QImage::Format_ARGB32)
                 .copy();

    FPDFBitmap_Destroy(thumbBitmap);
  }

  FPDF_ClosePage(page);
  FPDF_CloseDocument(doc);

  return result;
}

QImage PdfiumThumbnail::renderPage(const QString &pdfPath, int pageIndex,
                                   QSize targetSize) {
  if (!s_initialized) {
    return QImage();
  }

  // Load PDF document
  QByteArray pathUtf8 = pdfPath.toUtf8();
  FPDF_DOCUMENT doc = FPDF_LoadDocument(pathUtf8.constData(), nullptr);
  if (!doc) {
    return QImage();
  }

  // Load page
  FPDF_PAGE page = FPDF_LoadPage(doc, pageIndex);
  if (!page) {
    FPDF_CloseDocument(doc);
    return QImage();
  }

  // Get page dimensions and calculate target size maintaining aspect ratio
  double pageWidth = FPDF_GetPageWidth(page);
  double pageHeight = FPDF_GetPageHeight(page);

  double scaleX = targetSize.width() / pageWidth;
  double scaleY = targetSize.height() / pageHeight;
  double scale = qMin(scaleX, scaleY);

  int renderWidth = static_cast<int>(pageWidth * scale);
  int renderHeight = static_cast<int>(pageHeight * scale);

  // Create bitmap for rendering
  FPDF_BITMAP bitmap = FPDFBitmap_Create(renderWidth, renderHeight, 0);
  if (!bitmap) {
    FPDF_ClosePage(page);
    FPDF_CloseDocument(doc);
    return QImage();
  }

  // Fill with white background
  FPDFBitmap_FillRect(bitmap, 0, 0, renderWidth, renderHeight, 0xFFFFFFFF);

  // Render page to bitmap
  FPDF_RenderPageBitmap(bitmap, page, 0, 0, renderWidth, renderHeight, 0,
                        FPDF_ANNOT | FPDF_LCD_TEXT);

  // Create QImage from PDFium bitmap
  int stride = FPDFBitmap_GetStride(bitmap);
  void *buffer = FPDFBitmap_GetBuffer(bitmap);

  QImage result = QImage(static_cast<uchar *>(buffer), renderWidth,
                         renderHeight, stride, QImage::Format_ARGB32)
                      .copy();

  // Cleanup
  FPDFBitmap_Destroy(bitmap);
  FPDF_ClosePage(page);
  FPDF_CloseDocument(doc);

  return result;
}

} // namespace pdfpagemaster
