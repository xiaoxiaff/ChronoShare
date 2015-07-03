#ifndef QASIOEVENTDISPATCHER_H
#define QASIOEVENTDISPATCHER_H

#include "QtCore/qabstracteventdispatcher.h"
#include <QAbstractEventDispatcher>

class QAsioEventDispatcherPrivate;

namespace boost {
namespace asio {
class io_service;
}
}

class Q_CORE_EXPORT QAsioEventDispatcher : public QAbstractEventDispatcher
{
    Q_OBJECT
    Q_DECLARE_PRIVATE(QAsioEventDispatcher)

public:
    explicit QAsioEventDispatcher(boost::asio::io_service &io_service, QObject *parent = 0);
    ~QAsioEventDispatcher();

    bool processEvents(QEventLoop::ProcessEventsFlags flags);
    bool hasPendingEvents();
    void registerSocketNotifier(QSocketNotifier *notifier) Q_DECL_FINAL;
    void unregisterSocketNotifier(QSocketNotifier *notifier) Q_DECL_FINAL;

    void registerTimer(int timerId, int interval, Qt::TimerType timerType, QObject *object) Q_DECL_FINAL;
    bool unregisterTimer(int timerId) Q_DECL_FINAL;
    bool unregisterTimers(QObject *object) Q_DECL_FINAL;
    QList<TimerInfo> registeredTimers(QObject *object) const Q_DECL_FINAL;
    int remainingTime(int timerId) Q_DECL_FINAL;

    void wakeUp() Q_DECL_FINAL;
    void interrupt() Q_DECL_FINAL;
    //TODO:
    void flush();

protected:
    QAsioEventDispatcher(QAsioEventDispatcherPrivate &dd, QObject *parent = 0);

};

#endif // QASIOEVENTDISPATCHER_H
