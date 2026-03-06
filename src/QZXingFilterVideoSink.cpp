#include "QZXingFilterVideoSink.h"
#include <QDebug>
#include "QZXingImageProvider.h"

#ifndef Q_OS_WASM
#include <QtConcurrent/QtConcurrent>
#endif

QZXingFilter::QZXingFilter(QObject *parent)
    : QObject(parent)
    , decoder(QZXing::DecoderFormat_QR_CODE)
    , decoding(false)
    , m_videoSink(nullptr)
{
    /// Connecting signals to handlers that will send signals to QML
    connect(&decoder, &QZXing::decodingStarted,
            this, &QZXingFilter::handleDecodingStarted);
    connect(&decoder, &QZXing::decodingFinished,
            this, &QZXingFilter::handleDecodingFinished);

#ifdef Q_OS_WASM
    m_frameTimer.start();
#endif
}

QZXingFilter::~QZXingFilter()
{
#ifndef Q_OS_WASM
    if (!processThread.isFinished()) {
        processThread.cancel();
        processThread.waitForFinished();
    }
#endif
}

void QZXingFilter::handleDecodingStarted()
{
    decoding = true;
    emit decodingStarted();
    emit isDecodingChanged();
}

void QZXingFilter::handleDecodingFinished(bool succeeded)
{
    // qDebug() << "QZXingFilter::handleDecodingFinished: succeeded =" << succeeded;
    decoding = false;
    emit decodingFinished(succeeded, decoder.getProcessTimeOfLastDecoding());
    emit isDecodingChanged();
}

void QZXingFilter::setOrientation(int orientation)
{
    if (orientation_ == orientation)
        return;
    orientation_ = orientation;
    emit orientationChanged(orientation_);
}

int QZXingFilter::orientation() const
{
    return orientation_;
}

void QZXingFilter::setVideoSink(QObject *videoSink)
{
    if (m_videoSink == videoSink)
        return;

    if (m_videoSink)
        disconnect(m_videoSink, &QVideoSink::videoFrameChanged, this, &QZXingFilter::processFrame);

    m_videoSink = qobject_cast<QVideoSink*>(videoSink);

    if (m_videoSink) {
        connect(m_videoSink, &QVideoSink::videoFrameChanged, this, &QZXingFilter::processFrame, Qt::QueuedConnection);
        // qDebug() << "QZXingFilter: videoSink connected";
    } //else {
        // qDebug() << "QZXingFilter: videoSink is null or invalid";
    //}
}

void QZXingFilter::processFrame(const QVideoFrame &frame)
{
    if (!m_videoSink) {
        // qDebug() << "[SKIP] No videoSink";
        return;
    }

    if (decoder.getEnabledFormats() == QZXing::DecoderFormat_None) {
        // qDebug() << "[SKIP] No decoder formats enabled";
        return;
    }

    if (decoding) {
        // Не логируем каждый пропущенный кадр — слишком много
        return;
    }

#ifdef Q_OS_WASM
    // === Синхронный путь для WebAssembly ===

    // Ограничиваем частоту
    qint64 elapsed = m_frameTimer.elapsed();
    if (elapsed < MIN_FRAME_INTERVAL_MS)
        return;

    // qDebug() << "=== WASM processFrame ===" << "elapsed:" << elapsed << "ms";
    m_frameTimer.restart();

    // Проверяем сам фрейм
    // qDebug() << "  Frame valid:" << frame.isValid()
    //         << "size:" << frame.size()
    //         << "pixelFormat:" << frame.pixelFormat();

    if (!frame.isValid()) {
        // qDebug() << "  [SKIP] Frame is not valid";
        return;
    }

    decoding = true;

    // Конвертируем в QImage
    QVideoFrame f(frame);
    QImage image = f.toImage();

    // qDebug() << "  Image null:" << image.isNull()
    //         << "size:" << image.size()
    //         << "format:" << image.format()
    //         << "byteCount:" << image.sizeInBytes();

    if (image.isNull()) {
        // qDebug() << "  [ERROR] Cannot create image from frame!";
        decoding = false;
        return;
    }

    // Обрезка по captureRect
    if (captureRect.isValid()) {
        const QRect rect = captureRect.toRect();
        if (image.size() != rect.size()) {
            // qDebug() << "  Cropping to captureRect:" << rect;
            image = image.copy(rect);
        }
    }

    // Масштабируем обратно
    image = image.scaled(
        QSize(640, 640),
        Qt::KeepAspectRatio,
        Qt::SmoothTransformation  // ВАЖНО: SmoothTransformation!
        );

    // Поворот
    if (orientation_) {
        // qDebug() << "  Rotating by" << -orientation_ << "degrees";
        QTransform transformation;
        transformation.translate(image.rect().center().x(), image.rect().center().y());
        transformation.rotate(-orientation_);
        image = image.transformed(transformation);
    }

    // qDebug() << "  Final image for decode: size=" << image.size()
    //          << "format=" << image.format();

    // Синхронное декодирование
    QString result = decoder.decodeImage(image, image.width(), image.height());

    // qDebug() << "  Decode result:" << (result.isEmpty() ? "(empty)" : result);

    // decoding будет сброшен через handleDecodingFinished (сигнал)
    // Но на всякий случай, если сигнал не пришёл:
    if (decoding) {
        // qDebug() << "  [WARN] decoding flag still true after decode, resetting";
        decoding = false;
    }

#else
    // === Асинхронный путь для Android/Desktop ===

#if defined(Q_OS_ANDROID) && QT_VERSION < QT_VERSION_CHECK(6, 5, 0)
    m_videoSink->setRhi(nullptr);
    QVideoFrame f(frame);
    f.map(QVideoFrame::ReadOnly);
#else
    const QVideoFrame &f = frame;
#endif

    if (!isDecoding() && processThread.isFinished()) {
        decoding = true;

        QImage image = f.toImage();
        processThread = QtConcurrent::run([=]() {
            if (image.isNull()) {
                // qDebug() << "QZXingFilter error: Cant create image file to process.";
                decoding = false;
                return;
            }

            QImage frameToProcess(image);
            const QRect &rect = captureRect.toRect();

            if (captureRect.isValid() && frameToProcess.size() != rect.size()) {
                frameToProcess = image.copy(rect);
            }

            if (!orientation_) {
                decoder.decodeImage(frameToProcess);
            } else {
                QTransform transformation;
                transformation.translate(frameToProcess.rect().center().x(),
                                         frameToProcess.rect().center().y());
                transformation.rotate(-orientation_);
                QImage translatedImage = frameToProcess.transformed(transformation);
                decoder.decodeImage(translatedImage);
            }

            decoder.decodeImage(frameToProcess, frameToProcess.width(), frameToProcess.height());
        });
    }

#if defined(Q_OS_ANDROID) && QT_VERSION < QT_VERSION_CHECK(6, 5, 0)
    f.unmap();
#endif

#endif // Q_OS_WASM
}
