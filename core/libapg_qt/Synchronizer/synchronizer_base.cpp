#include "synchronizer_base.h"

namespace AirPlug
{
class SynchronizerBase::Private
{
public:

    explicit Q_DECL_HIDDEN Private(const QString& siteID)
        : siteID(siteID),
          isInitiator(false)
    {
    }

    ~Private()
    {
    }

    bool filterACKMessage(QJsonObject content)
    {
        if (content[QLatin1String("forInitiator")].toBool())
        {
            if (content[QLatin1String("initiator")].toString() == siteID)
            {
                isInitiator = true;
            }

            return isInitiator;
        }

        return (! isInitiator);
    }

public:

    QString siteID;
    bool isInitiator;
};

SynchronizerBase::SynchronizerBase(const QString& siteID)
    : QObject(),
      d(new Private(siteID))
{
}

SynchronizerBase::~SynchronizerBase()
{
    delete d;
}

void SynchronizerBase::init()
{
    ACLMessage message(ACLMessage::SYNC);
    QJsonObject content;
    content[QLatin1String("siteID")] = d->siteID;
    content[QLatin1String("init")]   = true;
    message.setContent(content);

    // Dummy message doesn't need to have a clock
    emit signalSendMessage(message);
}

void SynchronizerBase::processSYNCMessage(ACLMessage& message)
{
    // TODO avoid roundback message
    QJsonObject content = message.getContent();

    if (content[QLatin1String("fromInitiator")].toBool())
    {
        ACLMessage newMessage(ACLMessage::SYNC);

        emit signalSendState(newMessage);
    }
}

void SynchronizerBase::processACKMessage(ACLMessage& message)
{
    // Process message from NET,
    // then emit signalDoStep() to inform application to start the next cycle.
    // The application will use this envelop to send the message in the next cycle

    if (message.getPerformative() != ACLMessage::SYNC_ACK)
    {
        return;
    }

    QJsonObject content = message.getContent();

    if(d->filterACKMessage(content))
    {
        emit signalDoStep();

        if (d->isInitiator)
        {
            ACLMessage newMessage(ACLMessage::SYNC);

            QJsonObject newContent;
            newContent[QLatin1String("fromInitiator")] = true;
            newMessage.setContent(newContent);

            emit signalSendState(newMessage);
        }
    }
}

}
