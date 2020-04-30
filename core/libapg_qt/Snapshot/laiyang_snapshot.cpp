#include "laiyang_snapshot.h"

// Qt includes
#include <QDebug>

// libapg include
#include "vector_clock.h"

namespace AirPlug
{

class Q_DECL_HIDDEN LaiYangSnapshot::Private
{
public:

    Private()
        : recorded(false),
          initiator(false),
          msgCounter(0),
          nbApp(0)
    {
    }

    ~Private()
    {
    }

public:


    /**
     * @brief validateState :  verify if a state is valide to record
     * @param state
     * @return
     */
    bool validateState(const QJsonObject& state) const;

    /**
     * @brief collectState: collect a local state
     * @param state
     *
     * A State object should have the form of :
     * {
     *     siteID : QString
     *     clock  : vector clock
     *     state  : {
     *                  options       : application option
     *                  local varable : jsonObject
     *              }
     * }
     *
     */
    void collectState(const QJsonObject& state);

    /**
     * @brief verifyPrepost : reverify if a message is a prepost message becore recording snapshot
     * @param content
     * @return
     */
    bool verifyPrepost(const QJsonObject& content, QString& sender) const;

    /**
     * @brief allStateColltected : check if all state are collected
     * @return
     */
    bool allStateColltected() const;

    /**
     * @brief allPrepostCollected : verify if all prepost messages are collected
     * @return
     */
    bool allPrepostCollected() const;

    /**
     * @brief nbCollectedPrepost : return number of collected prepost message
     * @return
     */
    int nbCollectedPrepost() const;

public:

    bool recorded;
    bool initiator;

    int  msgCounter;
    int  nbApp;

    // System state will be encoded in Json object
    QHash<QString, QJsonObject> states;

    // Map of sender and its prepost messages
    QHash<QString, QVector<QJsonObject> > prepostMessages;
};



bool LaiYangSnapshot::Private::validateState(const QJsonObject& state) const
{
    QJsonObject timestamp = state;
    timestamp.remove(QLatin1String("state"));

    QString siteID = timestamp[QLatin1String("siteID")].toString();

    VectorClock newClock(timestamp);

    // verify conference of snapshot by coherence property of a cut
    for (QHash<QString, QJsonObject>::const_iterator iter  = states.cbegin();
                                                     iter != states.cend();
                                                     ++iter)
    {
        QJsonObject currentTimestamp = iter.value();
        currentTimestamp.remove(QLatin1String("state"));

        QString currentSiteID = currentTimestamp[QLatin1String("siteID")].toString();

        VectorClock currentClock(currentTimestamp);

        // every clock must be the most recent clock of its site
        if ( (newClock.getValue(siteID)        < currentClock.getValue(siteID))         ||
             (newClock.getValue(currentSiteID) > currentClock.getValue(currentSiteID)) )
        {
            return false;
        }
    }

    return true;
}

void LaiYangSnapshot::Private::collectState(const QJsonObject& state)
{
    if (validateState(state))
    {
        QString siteID = state[QLatin1String("siteID")].toString();

        states[siteID] = state;
    }
    else
    {
        qWarning() << "Inconherent Snapshot detected ---> drop state.";
    }
}


bool LaiYangSnapshot::Private::verifyPrepost(const QJsonObject& content, QString& sender) const
{
    if (! content.contains(QLatin1String("receiver")) ||
        ! content.contains(QLatin1String("message")))
    {
        qWarning() << "Snapshot: prepost message has incorrect format";

        return false;
    }

    ACLMessage originalMessage(content[QLatin1String("message")].toString());

    VectorClock* messageClock = originalMessage.getTimeStamp();

    if (!messageClock)
    {
        qWarning() << "Snapshot: prepost message don't have clock --> drop.";

        return false;
    }

   sender = messageClock->getSiteID();

    // verify pre-message, avoid post-post or post-pre for a conherent snapshot

    if (states.contains(sender))
    {
        QJsonObject jsonClock = states[sender];

        jsonClock.remove(QLatin1String("state"));

        VectorClock* senderClock = new VectorClock(jsonClock);

        // if clock of sender at the moment of taking snapshot is smaller than clock of the message ==> it's not a pre-message
        if ((*senderClock) < (*messageClock))
        {
            qWarning() << "Snapshot: not a pre-message --> drop";

            return false;
        }
    }

    return true;
}

bool LaiYangSnapshot::Private::allStateColltected() const
{
    if (states.size() == nbApp)
    {
        return true;
    }

    return false;
}

bool LaiYangSnapshot::Private::allPrepostCollected() const
{
    // TODO

    return false;
}

int LaiYangSnapshot::Private::nbCollectedPrepost() const
{
    int nbPrepost = 0;

    for (QHash<QString, QVector<QJsonObject> >::const_iterator iter  = prepostMessages.cbegin();
                                                               iter != prepostMessages.cend();
                                                               ++iter)
    {
        nbPrepost += iter.value().size();
    }

    return nbPrepost;
}

/* ----------------------------------------------------------------------- LaiYangSnapshot main functions --------------------------------------------------------------------------------------*/

LaiYangSnapshot::LaiYangSnapshot()
    : QObject(nullptr),
      d(new Private())
{
}

LaiYangSnapshot::~LaiYangSnapshot()
{
    delete d;
}

void LaiYangSnapshot::init()
{
    d->initiator = true;

    if (!d->recorded)
    {
        requestSnapshot();
    }
}


void LaiYangSnapshot::colorMessage(QJsonObject& messageContent)
{
    // append color field to the content of the message
    messageContent[QLatin1String("snapshotted")] = d->recorded;
}

bool LaiYangSnapshot::getColor(QJsonObject& messageContent)
{
    bool snapshotted = messageContent[QLatin1String("snapshotted")].toBool();
    messageContent.remove(QLatin1String("snapshotted"));

    if (snapshotted && !d->recorded)
    {
        requestSnapshot();
    }
    else if (!snapshotted && d->recorded)
    {
        // prepost detected
        return true;
    }

    return false;
}

bool LaiYangSnapshot::processStateMessage(const ACLMessage& message, bool fromLocal)
{
    VectorClock* timestamp = message.getTimeStamp();

    if (!timestamp)
    {
        qWarning() << "INFORM_STATE message doesn't contain timestamp.";

        return false;
    }

    QJsonObject state = timestamp->convertToJson();

    QJsonObject content = message.getContent();

    state[QLatin1String("state")] = content;

    if (d->initiator)
    {
        d->collectState(state);

        qDebug() << "Initiator receives state : " << QJsonDocument(state).toJson(QJsonDocument::Compact);

        // TODO check nb prepost message
        if (d->allStateColltected())
        {
            qDebug() << "Snapshot finish";

            d->recorded = false;
        }

        return false;
    }
    else if (fromLocal)
    {
        // Record State from local application
        d->collectState(state);
    }

    // Forward to initiator
    return true;
}



bool LaiYangSnapshot::processPrePostMessage(ACLMessage& message)
{
    QJsonObject prepostContent = message.getContent();

    QString sender;

    if (d->initiator)
    {
        if (d->verifyPrepost(prepostContent, sender))
        {
            d->prepostMessages[sender].append(prepostContent);

            qDebug() << "Initiator receives prepost : " << QJsonDocument(prepostContent).toJson(QJsonDocument::Compact);
        }

        return false;
    }

    return true;
}

void LaiYangSnapshot::requestSnapshot()
{
    d->recorded = true;

    // Chandy-Lamport marker
    ACLMessage* marker = new ACLMessage(ACLMessage::REQUEST_SNAPSHOT);

    emit signalRequestSnapshot(marker);
}

void LaiYangSnapshot::setNbOfApp(int nbApp)
{
    d->nbApp = nbApp;
}

}
