#include "RgbControl.h"

#include "Settings.h"

#include <QDebug>

#include <utility>

namespace casdap {

namespace {

constexpr int processTimeoutMs = 5000;

QString binary(const char *env, const QString &fallback)
{
    const QString value = qEnvironmentVariable(env);
    return value.isEmpty() ? fallback : value;
}

QString huePrefix()
{
    return QStringLiteral("rgb-hue-");
}

bool isHueWord(const QString &word)
{
    return word.startsWith(huePrefix());
}

int wordHue(const QString &word)
{
    return isHueWord(word) ? word.mid(huePrefix().size()).toInt() : -1;
}

QString sleepWord()
{
    return QStringLiteral("rgb-sleep");
}

QString wakeWord()
{
    return QStringLiteral("rgb-wake");
}

bool keepsHue(const QString &word)
{
    return word == sleepWord() || word == wakeWord();
}

}

RgbControl::RgbControl(QObject *parent)
    : QObject(parent)
    , m_mode(QStringLiteral("armada"))
{
    m_hueTimer.setSingleShot(true);
    m_hueTimer.setInterval(hueDelayMs);
    connect(&m_hueTimer, &QTimer::timeout, this, &RgbControl::sendHue);
}

RgbControl::~RgbControl()
{
    dropProcess(m_probe);
    dropProcess(m_word);
}

QString RgbControl::rgbPath()
{
    return binary("CASDAP_RGB_BIN", QStringLiteral("/usr/libexec/armada/armada-rgb-env"));
}

QString RgbControl::systemctlPath()
{
    return binary("CASDAP_SYSTEMCTL_BIN", QStringLiteral("systemctl"));
}

QString RgbControl::hueWord(int hue)
{
    return huePrefix() + QString::number(Settings::normalizeHue(hue));
}

QString RgbControl::unitFor(const QString &word)
{
    return QStringLiteral("casdap-priv@") + word + QStringLiteral(".service");
}

bool RgbControl::supported() const
{
    return m_supported;
}

bool RgbControl::probed() const
{
    return m_probed;
}

bool RgbControl::busy() const
{
    return m_busy;
}

QString RgbControl::mode() const
{
    return m_mode;
}

int RgbControl::hue() const
{
    return m_hue;
}

bool RgbControl::screenOff() const
{
    return m_screenOff;
}

bool RgbControl::saver() const
{
    return m_saver;
}

int RgbControl::sentHue() const
{
    return m_sentHue;
}

bool RgbControl::ready() const
{
    return m_probed && m_supported;
}

void RgbControl::start(const QString &mode, int hue)
{
    const QString m = mode.toLower();
    if (!Settings::rgbModes().contains(m))
        qWarning() << "casdap rgb: unknown mode" << mode;
    else if (m_mode != m) {
        m_mode = m;
        emit modeChanged();
    }
    const int h = Settings::normalizeHue(hue);
    if (m_hue != h) {
        m_hue = h;
        emit hueChanged();
    }
    probe();
}

void RgbControl::setMode(const QString &mode)
{
    const QString m = mode.toLower();
    if (!Settings::rgbModes().contains(m)) {
        qWarning() << "casdap rgb: unknown mode" << mode;
        return;
    }
    if (m_mode == m)
        return;
    m_mode = m;
    emit modeChanged();
    if (ready())
        applyMode();
}

void RgbControl::setHue(int hue)
{
    const int h = Settings::normalizeHue(hue);
    if (m_hue == h)
        return;
    m_hue = h;
    emit hueChanged();
    if (ready() && m_mode == QLatin1StringView("accent"))
        m_hueTimer.start();
}

void RgbControl::setScreenOff(bool off)
{
    if (m_screenOff == off)
        return;
    m_screenOff = off;
    emit screenOffChanged();
    if (off) {
        m_asleep = false;
        m_hueTimer.stop();
        m_words.removeIf(isHueWord);
        updateBusy();
        return;
    }
    if (m_saver) {
        sleepLights();
        updateBusy();
        return;
    }
    queueHue();
}

void RgbControl::setSaver(bool on)
{
    if (m_saver == on)
        return;
    m_saver = on;
    emit saverChanged();
    if (on) {
        m_hueTimer.stop();
        m_words.removeIf(isHueWord);
        if (!m_screenOff)
            sleepLights();
        updateBusy();
        return;
    }
    if (m_asleep) {
        m_asleep = false;
        startWords({wakeWord()});
    }
    queueHue();
}

void RgbControl::sleepLights()
{
    if (!ready() || m_asleep || m_mode == QLatin1StringView("off"))
        return;
    m_asleep = true;
    startWords({sleepWord()});
}

void RgbControl::probe()
{
    dropProcess(m_probe);
    auto *process = new QProcess(this);
    m_probe = process;
    auto done = [this, process](bool ok) {
        if (m_probe == process)
            m_probe = nullptr;
        process->deleteLater();
        const bool changed = m_supported != ok;
        m_supported = ok;
        m_probed = true;
        qInfo() << "casdap rgb: supported" << ok;
        if (changed)
            emit supportedChanged();
        emit probedChanged();
        updateBusy();
        if (!ok)
            return;
        startWords({QStringLiteral("rgb-restore")});
        if (m_mode != QLatin1StringView("armada"))
            applyMode();
        else if (m_saver && !m_screenOff)
            sleepLights();
    };
    connect(process, &QProcess::finished, this,
            [done](int code, QProcess::ExitStatus status) { done(status == QProcess::NormalExit && code == 0); });
    connect(process, &QProcess::errorOccurred, this, [done, process](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        qWarning() << "casdap rgb: cannot start" << process->program();
        done(false);
    });
    QTimer::singleShot(processTimeoutMs, process, [process] {
        if (process->state() != QProcess::NotRunning)
            process->kill();
    });
    process->start(rgbPath(), {QStringLiteral("supported")});
    updateBusy();
}

void RgbControl::applyMode()
{
    m_hueTimer.stop();
    if (m_mode == QLatin1StringView("accent")) {
        queueHue();
    } else {
        m_words.removeIf(isHueWord);
        startWords({m_mode == QLatin1StringView("off") ? QStringLiteral("rgb-off") : QStringLiteral("rgb-restore")});
    }
    if (m_mode == QLatin1StringView("off")) {
        m_asleep = false;
    } else if (m_saver && !m_screenOff) {
        m_asleep = false;
        sleepLights();
    }
}

void RgbControl::sendHue()
{
    if (m_mode == QLatin1StringView("accent"))
        queueHue();
}

void RgbControl::queueHue()
{
    if (!ready() || m_screenOff || m_saver || m_mode != QLatin1StringView("accent"))
        return;
    m_words.removeIf(isHueWord);
    int tail = m_sentHue;
    for (const QString &word : std::as_const(m_words)) {
        if (!keepsHue(word))
            tail = wordHue(word);
    }
    if (tail == m_hue) {
        updateBusy();
        return;
    }
    startWords({hueWord(m_hue)});
}

void RgbControl::startWords(const QStringList &words)
{
    m_words.append(words);
    if (!m_word)
        nextWord();
    updateBusy();
}

void RgbControl::nextWord()
{
    if (m_words.isEmpty()) {
        updateBusy();
        return;
    }
    const QString word = m_words.takeFirst();
    if (!keepsHue(word))
        m_sentHue = wordHue(word);
    qInfo() << "casdap rgb: start" << unitFor(word);
    auto *process = new QProcess(this);
    m_word = process;
    auto done = [this, process, word](bool ok) {
        if (m_word == process)
            m_word = nullptr;
        process->deleteLater();
        if (!ok) {
            qWarning() << "casdap rgb:" << word << "failed";
            if (isHueWord(word) && m_sentHue == wordHue(word))
                m_sentHue = -1;
        }
        qInfo() << "casdap rgb: applied" << word << ok;
        emit applied(word, ok);
        nextWord();
    };
    connect(process, &QProcess::finished, this,
            [done](int code, QProcess::ExitStatus status) { done(status == QProcess::NormalExit && code == 0); });
    connect(process, &QProcess::errorOccurred, this, [done, process](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        qWarning() << "casdap rgb: cannot start" << process->program();
        done(false);
    });
    QTimer::singleShot(processTimeoutMs, process, [process] {
        if (process->state() != QProcess::NotRunning)
            process->kill();
    });
    process->start(systemctlPath(), {QStringLiteral("start"), unitFor(word)});
}

void RgbControl::updateBusy()
{
    const bool busy = m_probe != nullptr || m_word != nullptr || !m_words.isEmpty();
    if (m_busy == busy)
        return;
    m_busy = busy;
    emit busyChanged();
}

void RgbControl::dropProcess(QProcess *&slot)
{
    if (!slot)
        return;
    QProcess *process = slot;
    slot = nullptr;
    process->disconnect(this);
    if (process->state() != QProcess::NotRunning) {
        process->kill();
        process->waitForFinished(1000);
    }
    process->deleteLater();
}

}
