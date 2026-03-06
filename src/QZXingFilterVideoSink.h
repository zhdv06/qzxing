/*
 * Copyright 2017 QZXing authors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef QZXingFilter_H
#define QZXingFilter_H

#include <QObject>
#include <QPointer>
#include <QVideoSink>
#include <QVideoFrame>
#include <QElapsedTimer>

#ifndef Q_OS_WASM
#include <QFuture>
#endif

#include "QZXing.h"

class QZXingFilter : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool decoding READ isDecoding NOTIFY isDecodingChanged)
    Q_PROPERTY(QObject* videoSink READ getVideoSink WRITE setVideoSink)
    Q_PROPERTY(QZXing* decoder READ getDecoder)
    Q_PROPERTY(QRectF captureRect MEMBER captureRect NOTIFY captureRectChanged)
    Q_PROPERTY(int orientation READ orientation WRITE setOrientation NOTIFY orientationChanged)

public:
    explicit QZXingFilter(QObject *parent = nullptr);
    ~QZXingFilter();

    bool isDecoding() const { return decoding; }
    QZXing* getDecoder() { return &decoder; }
    QObject* getVideoSink() const { return m_videoSink; }
    void setVideoSink(QObject *videoSink);

    int orientation() const;
    void setOrientation(int orientation);

signals:
    void isDecodingChanged();
    void decodingStarted();
    void decodingFinished(bool succeeded, int processingTime);
    void captureRectChanged();
    void orientationChanged(int orientation);

private slots:
    void handleDecodingStarted();
    void handleDecodingFinished(bool succeeded);

private:
    void processFrame(const QVideoFrame &frame);

    QZXing decoder;
    bool decoding;
    QPointer<QVideoSink> m_videoSink;
    QRectF captureRect;
    int orientation_ = 0;

#ifdef Q_OS_WASM
    QElapsedTimer m_frameTimer;
    static constexpr qint64 MIN_FRAME_INTERVAL_MS = 500;
#else
    QFuture<void> processThread;
#endif
};

#endif // QZXingFilter_H
