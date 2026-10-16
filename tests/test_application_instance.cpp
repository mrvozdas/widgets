#include "platform/windows/ApplicationInstance.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSaveFile>
#include <QStringList>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QUuid>

#include <iostream>
#include <optional>
#include <utility>

namespace {

using w11widget::windows::ApplicationInstance;
using StartResult = ApplicationInstance::StartResult;
using Activation = ApplicationInstance::Activation;

struct ActivationCounts {
    int settings = 0;
    int panel = 0;
    bool operator==(const ActivationCounts&) const = default;
};

QString fixtureFile(const QString& directory, const char* name) {
    return directory + QLatin1Char('/') + QString::fromLatin1(name);
}

bool writeFile(const QString& path, const QByteArray& contents) {
    QSaveFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(contents) == contents.size() && file.commit();
}

bool writeCounts(const QString& directory, const ActivationCounts& counts) {
    const QJsonObject object{{QStringLiteral("Settings"), counts.settings},
                             {QStringLiteral("Panel"), counts.panel}};
    return writeFile(fixtureFile(directory, "activations"), QJsonDocument(object).toJson(QJsonDocument::Compact));
}

std::optional<ActivationCounts> activationCounts(const QString& directory) {
    QFile file(fixtureFile(directory, "activations"));
    if (!file.open(QIODevice::ReadOnly)) return std::nullopt;
    const auto object = QJsonDocument::fromJson(file.readAll()).object();
    const ActivationCounts counts{object.value(QStringLiteral("Settings")).toInt(-1),
                                  object.value(QStringLiteral("Panel")).toInt(-1)};
    return counts.settings >= 0 && counts.panel >= 0 ? std::optional<ActivationCounts>(counts) : std::nullopt;
}

const char* resultName(StartResult result) {
    switch (result) {
    case StartResult::Primary: return "primary";
    case StartResult::ActivatedExisting: return "existing";
    case StartResult::Error: return "error";
    }
    return "unknown";
}

int residentHelper(QCoreApplication& application, const QString& scope,
                   const QString& directory, bool holdExisting, bool deferHandlers = false) {
    ApplicationInstance instance;
    QString error;
    const auto result = instance.start(scope, error, !holdExisting);
    const auto expected = holdExisting ? StartResult::ActivatedExisting : StartResult::Primary;
    if (result != expected) {
        std::cerr << "Resident expected " << resultName(expected) << ", got " << resultName(result)
                  << ": " << error.toStdString() << '\n';
        return 2;
    }

    ActivationCounts activations;
    const auto handleActivation = [&](Activation action) {
        if (QThread::currentThread() != application.thread()) {
            std::cerr << "Activation handler ran outside the Qt application thread.\n";
            application.exit(3);
            return;
        }
        if (action == Activation::Panel) ++activations.panel;
        else ++activations.settings;
        if (!writeCounts(directory, activations)) {
            std::cerr << "Could not publish the activation count.\n";
            application.exit(3);
        }
    };
    bool settingsInstalled = false;
    bool panelInstalled = false;
    const auto installHandlers = [&] {
        if (!settingsInstalled && (!deferHandlers || QFile::exists(fixtureFile(directory, "install-settings")))) {
            settingsInstalled = true;
            instance.setActivationHandler([&] { handleActivation(Activation::Settings); });
        }
        if (!panelInstalled && (!deferHandlers || QFile::exists(fixtureFile(directory, "install-panel")))) {
            panelInstalled = true;
            instance.setPanelActivationHandler([&] { handleActivation(Activation::Panel); });
        }
    };
    installHandlers();
    if (!writeCounts(directory, activations)
        || !writeFile(fixtureFile(directory, "ready"), "ready\n")) {
        std::cerr << "Could not publish resident readiness.\n";
        return 4;
    }

    QTimer stopTimer;
    QObject::connect(&stopTimer, &QTimer::timeout, &application, [&] {
        installHandlers();
        if (QFile::exists(fixtureFile(directory, "stop"))) application.quit();
    });
    stopTimer.start(20);
    QTimer::singleShot(8000, &application, [&] {
        std::cerr << "Resident helper exceeded its test deadline.\n";
        application.exit(5);
    });
    return application.exec();
}

int clientHelper(const QString& scope, const QString& expectedName, bool activateExisting, Activation action) {
    ApplicationInstance instance;
    QString error;
    // Exercise the original API defaults for settings clients.
    const auto result = action == Activation::Settings ? instance.start(scope, error, activateExisting)
        : instance.start(scope, error, activateExisting, action);
    if (expectedName != QString::fromLatin1(resultName(result))) {
        std::cerr << "Client expected " << expectedName.toStdString() << ", got " << resultName(result)
                  << ": " << error.toStdString() << '\n';
        return 2;
    }
    return 0;
}

// Every process in this fixture was started by this exact QProcess object.
// Cleanup never searches for, signals or terminates an application by name.
class Child final {
public:
    explicit Child(QString directory = {}) : directory_(std::move(directory)) {}
    ~Child() {
        if (process_.state() == QProcess::NotRunning) return;
        if (!directory_.isEmpty()) {
            writeFile(fixtureFile(directory_, "stop"), "stop\n");
            if (process_.waitForFinished(300)) return;
        }
        process_.kill();
        process_.waitForFinished(1000);
    }

    bool start(const QStringList& arguments) {
        process_.setProgram(QCoreApplication::applicationFilePath());
        process_.setArguments(arguments);
        process_.setProcessChannelMode(QProcess::MergedChannels);
        process_.start();
        return process_.waitForStarted(1500);
    }

    bool awaitReady() {
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < 2000) {
            if (QFile::exists(fixtureFile(directory_, "ready"))) return true;
            if (process_.waitForFinished(0)) return false;
            QThread::msleep(10);
        }
        return false;
    }

    bool awaitSuccess() {
        if (process_.state() != QProcess::NotRunning && !process_.waitForFinished(2000)) return false;
        return process_.exitStatus() == QProcess::NormalExit && process_.exitCode() == 0;
    }

    bool stop() {
        return writeFile(fixtureFile(directory_, "stop"), "stop\n") && awaitSuccess();
    }

    bool crash() {
        process_.kill();
        return process_.waitForFinished(1000) && process_.exitStatus() == QProcess::CrashExit;
    }

    bool rejectsInvalidActivation() {
        HWND endpoint = nullptr;
        while ((endpoint = FindWindowExW(HWND_MESSAGE, endpoint, L"w11widget.ApplicationInstance.v2", nullptr))) {
            DWORD owner = 0;
            GetWindowThreadProcessId(endpoint, &owner);
            if (owner != static_cast<DWORD>(process_.processId())) continue;
            DWORD_PTR response = 1;
            return SendMessageTimeoutW(endpoint, WM_APP + 0x251, 2, 0,
                                       SMTO_ABORTIFHUNG | SMTO_BLOCK, 1000, &response) && response == 0;
        }
        return false;
    }

    QString diagnostics() {
        return process_.errorString() + QLatin1Char('\n') + QString::fromLocal8Bit(process_.readAll());
    }

private:
    QString directory_;
    QProcess process_;
};

bool check(bool passed, const char* description, const QString& details = {}) {
    if (passed) return true;
    std::cerr << "FAIL: " << description << '\n';
    if (!details.isEmpty()) std::cerr << details.toStdString() << '\n';
    return false;
}

bool runClient(const QString& scope, StartResult expected, bool activateExisting = true,
               Activation action = Activation::Settings) {
    Child client;
    const bool passed = client.start({action == Activation::Panel ? QStringLiteral("--panel-client")
                                                                : QStringLiteral("--client"), scope,
                                     QString::fromLatin1(resultName(expected)),
                                     activateExisting ? QStringLiteral("activate") : QStringLiteral("quiet")})
        && client.awaitSuccess();
    return check(passed, "separate client returned the expected start result", passed ? QString{} : client.diagnostics());
}

bool awaitCounts(const QString& directory, ActivationCounts expected) {
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 1000) {
        const auto actual = activationCounts(directory);
        if (actual && *actual == expected) return true;
        if (actual && (actual->settings > expected.settings || actual->panel > expected.panel)) break;
        QThread::msleep(10);
    }
    const auto actual = activationCounts(directory);
    return check(false, "primary received exactly one activation per interactive client",
                 QStringLiteral("Expected Settings=%1, Panel=%2; read %3.").arg(expected.settings).arg(expected.panel)
                     .arg(actual ? QStringLiteral("Settings=%1, Panel=%2").arg(actual->settings).arg(actual->panel)
                                 : QStringLiteral("no valid counter")));
}

bool countsStayAt(const QString& directory, ActivationCounts expected) {
    QElapsedTimer timer;
    timer.start();
    do {
        if (!check(activationCounts(directory) == std::optional<ActivationCounts>(expected),
                   "quiet or differently scoped client does not activate the primary")) return false;
        QThread::msleep(10);
    } while (timer.elapsed() < 100);
    return true;
}

int runTests() {
    QTemporaryDir primaryFiles;
    QTemporaryDir recoveryFiles;
    QTemporaryDir holderFiles;
    QTemporaryDir deferredFiles;
    if (!check(primaryFiles.isValid() && recoveryFiles.isValid() && holderFiles.isValid() && deferredFiles.isValid(),
               "temporary process fixtures are available")) return 1;
    const auto scope = QStringLiteral("w11widget-test-ApplicationInstance-")
        + QUuid::createUuid().toString(QUuid::WithoutBraces);

    Child primary(primaryFiles.path());
    const bool started = primary.start({QStringLiteral("--server"), scope, primaryFiles.path()})
        && primary.awaitReady();
    if (!check(started, "first process becomes a ready primary", started ? QString{} : primary.diagnostics())) return 1;
    if (!awaitCounts(primaryFiles.path(), {})) return 1;

    // A Win32 mutex is recursive for its owning thread, so every contender must
    // run in a separate process rather than constructing a second local object.
    for (int count = 1; count <= 3; ++count) {
        if (!runClient(scope, StartResult::ActivatedExisting)
            || !awaitCounts(primaryFiles.path(), {count, 0})) return 1;
    }
    // The normal panel action must never invoke the settings callback.
    for (int count = 1; count <= 3; ++count) {
        if (!runClient(scope, StartResult::ActivatedExisting, true, Activation::Panel)
            || !awaitCounts(primaryFiles.path(), {3, count})) return 1;
    }
    if (!runClient(scope, StartResult::ActivatedExisting, false)
        || !runClient(scope, StartResult::ActivatedExisting, false, Activation::Panel)
        || !countsStayAt(primaryFiles.path(), {3, 3})) return 1;
    if (!runClient(scope, StartResult::ActivatedExisting)
        || !awaitCounts(primaryFiles.path(), {4, 3})
        || !runClient(scope, StartResult::ActivatedExisting, true, Activation::Panel)
        || !awaitCounts(primaryFiles.path(), {4, 4})) return 1;
    if (!check(primary.rejectsInvalidActivation(), "native endpoint rejects an unknown action without acknowledgement")
        || !countsStayAt(primaryFiles.path(), {4, 4})) return 1;

    const auto otherScope = scope + QStringLiteral("-independent");
    if (!runClient(otherScope, StartResult::Primary, true, Activation::Panel)
        || !countsStayAt(primaryFiles.path(), {4, 4})) return 1;
    const bool stopped = primary.stop();
    if (!check(stopped, "primary exits on its own fixture stop file", stopped ? QString{} : primary.diagnostics())
        || !runClient(scope, StartResult::Primary)) return 1;

    Child recovery(recoveryFiles.path());
    const bool recoveryStarted = recovery.start({QStringLiteral("--server"), scope, recoveryFiles.path()})
        && recovery.awaitReady();
    if (!check(recoveryStarted, "recovery fixture becomes primary",
               recoveryStarted ? QString{} : recovery.diagnostics())) return 1;

    // Keep a nonowning mutex handle alive in another process. Killing the owner
    // then leaves an abandoned mutex, rather than deleting the named object.
    Child holder(holderFiles.path());
    const bool holderStarted = holder.start({QStringLiteral("--holder"), scope, holderFiles.path()})
        && holder.awaitReady();
    if (!check(holderStarted, "quiet secondary retains the existing mutex handle",
               holderStarted ? QString{} : holder.diagnostics())) return 1;
    if (!countsStayAt(recoveryFiles.path(), {})) return 1;
    if (!check(recovery.crash(), "only the owned primary fixture is killed for abandonment recovery")
        || !runClient(scope, StartResult::Primary)) return 1;
    const bool holderStopped = holder.stop();
    if (!check(holderStopped, "nonowning fixture exits cleanly",
               holderStopped ? QString{} : holder.diagnostics())) return 1;

    Child deferred(deferredFiles.path());
    const bool deferredStarted = deferred.start({QStringLiteral("--deferred-server"), scope, deferredFiles.path()})
        && deferred.awaitReady();
    if (!check(deferredStarted, "primary publishes its endpoint before handlers are installed",
               deferredStarted ? QString{} : deferred.diagnostics())) return 1;
    if (!runClient(scope, StartResult::ActivatedExisting)
        || !runClient(scope, StartResult::ActivatedExisting, true, Activation::Panel)
        || !countsStayAt(deferredFiles.path(), {})) return 1;
    if (!check(writeFile(fixtureFile(deferredFiles.path(), "install-panel"), "install\n"),
               "panel callback installation is requested")
        || !awaitCounts(deferredFiles.path(), {0, 1})) return 1;
    if (!check(writeFile(fixtureFile(deferredFiles.path(), "install-settings"), "install\n"),
               "settings callback installation is requested")
        || !awaitCounts(deferredFiles.path(), {1, 1})) return 1;
    const bool deferredStopped = deferred.stop();
    if (!check(deferredStopped, "deferred-handler fixture exits cleanly",
               deferredStopped ? QString{} : deferred.diagnostics())) return 1;

    std::cout << "APPLICATION INSTANCE TEST: PASS\n";
    return 0;
}

} // namespace

int main(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);
    const auto arguments = application.arguments();
    if (arguments.size() == 4 && arguments[1] == QStringLiteral("--server"))
        return residentHelper(application, arguments[2], arguments[3], false);
    if (arguments.size() == 4 && arguments[1] == QStringLiteral("--holder"))
        return residentHelper(application, arguments[2], arguments[3], true);
    if (arguments.size() == 4 && arguments[1] == QStringLiteral("--deferred-server"))
        return residentHelper(application, arguments[2], arguments[3], false, true);
    if (arguments.size() == 5 && arguments[1] == QStringLiteral("--client"))
        return clientHelper(arguments[2], arguments[3], arguments[4] == QStringLiteral("activate"), Activation::Settings);
    if (arguments.size() == 5 && arguments[1] == QStringLiteral("--panel-client"))
        return clientHelper(arguments[2], arguments[3], arguments[4] == QStringLiteral("activate"), Activation::Panel);
    if (arguments.size() != 1) {
        std::cerr << "Unexpected application-instance test arguments.\n";
        return 1;
    }
    return runTests();
}
