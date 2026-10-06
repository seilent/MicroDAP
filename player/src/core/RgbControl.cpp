#include "RgbControl.h"

#include "Settings.h"

#include <QDebug>
#include <QTimer>

namespace casdap {

namespace {

constexpr int processTimeoutMs = 5000;

QString binary(const char *env, const QString &fallback)
{
    const QString value = qEnvironmentVariable(env);
    return value.isEmpty() ? fallback : value;
}

}

RgbControl::RgbControl(QObject *parent)
    : QObject(parent)
    , m_mode(QStringLiteral("armada"))
{
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

QStringList RgbControl::accentHexes()
{
    return Settings::accents();
}

QStringList RgbControl::accentNames()
{
    return {QStringLiteral("pink"), QStringLiteral("orange"), QStringLiteral("cyan"),
            QStringLiteral("lime"), QStringLiteral("purple")};
}

QString RgbControl::accentName(const QString &hex)
{
    const int at = accentHexes().indexOf(hex.toUpper());
    return at < 0 ? QString() : accentNames().at(at);
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

QString RgbControl::accent() const
{
    return m_accent;
}

void RgbControl::start(const QString &mode, const QString &accent)
{
    const QString m = mode.toLower();
    if (!Settings::rgbModes().contains(m))
        qWarning() << "casdap rgb: unknown mode" << mode;
    else if (m_mode != m) {
        m_mode = m;
        emit modeChanged();
    }
    const QString a = accent.toUpper();
    if (m_accent != a) {
        m_accent = a;
        emit accentChanged();
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
    if (m_probed && m_supported)
        applyMode();
}

void RgbControl::setAccent(const QString &accent)
{
    const QString a = accent.toUpper();
    if (m_accent == a)
        return;
    m_accent = a;
    emit accentChanged();
    if (m_probed && m_supported && m_mode == QLatin1StringView("accent"))
        applyMode();
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
    if (m_mode == QLatin1StringView("off")) {
        startWords({QStringLiteral("rgb-off")});
        return;
    }
    if (m_mode == QLatin1StringView("armada")) {
        startWords({QStringLiteral("rgb-restore")});
        return;
    }
    const QString name = accentName(m_accent);
    if (name.isEmpty()) {
        qWarning() << "casdap rgb: no accent word for" << m_accent;
        return;
    }
    startWords({QStringLiteral("rgb-accent-") + name});
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
    qInfo() << "casdap rgb: start" << unitFor(word);
    auto *process = new QProcess(this);
    m_word = process;
    auto done = [this, process, word](bool ok) {
        if (m_word == process)
            m_word = nullptr;
        process->deleteLater();
        if (!ok)
            qWarning() << "casdap rgb:" << word << "failed";
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
