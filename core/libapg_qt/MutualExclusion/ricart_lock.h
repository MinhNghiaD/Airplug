/* ============================================================
 *
 * This file is a part of Airplug project
 *
 * Date        : 2020-5-2
 * Description : implementation of Ricart-Agrawala's mutual exclusion algorithm
 *
 * 2020 by Nghia Duong <minhnghiaduong997 at gmail dot com>
 *
 * ============================================================ */

#ifndef RICART_LOCK_H
#define RICART_LOCK_H

// Qt include
#include <QObject>

// libapg include
#include "vector_clock.h"
#include "aclmessage.h"

namespace AirPlug
{

class RicartLock : public QObject
{
    Q_OBJECT
public:

    explicit RicartLock();
    ~RicartLock();

public:

    void trylock(const VectorClock& requesterClock);

    /**
     * @brief lock : enter critical section
     */
    void lock();



    void restart();

    void receiveExternalRequest(const VectorClock& requesterClock);

    QJsonArray getPendingQueue() const;

public:

    /**
     * @brief unlock : exit critical section
     */
    Q_SLOT void unlock();

public:

    Q_SIGNAL void signalResponse(const ACLMessage& request);
    Q_SIGNAL void signalEnterRaceCondition();

private:

    class Private;
    Private* d;
};


}
#endif // RICART_LOCK_H
