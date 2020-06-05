#include "router_p.h"

// Qt includes
#include <QDebug>

namespace AirPlug
{

/*------------------------------------------------------------ Router main functions ----------------------------------------------------------------------------------------*/


Router::Router(CommunicationManager* communication, const QString& siteID)
    : QObject(nullptr),
      d(new Private())
{
    if (!communication)
    {
        qFatal("Router: Communication Manager is null");
    }

    setObjectName(QLatin1String("Router"));

    d->communicationMngr = communication;
    d->siteID            = siteID;
    d->watchdog          = new Watchdog(siteID);
    d->electionMng       = new ElectionManager(siteID);
    d->synchronizer      = new SynchronizerControl(d->siteID);

    connect(d->communicationMngr, SIGNAL(signalMessageReceived(Header, Message)),
            this,                 SLOT(slotReceiveMessage(Header, Message)), Qt::DirectConnection);

    connect(d->watchdog, &Watchdog::signalPingLocalApps,
            this,        &Router::slotBroadcastLocal, Qt::DirectConnection);

    connect(d->watchdog, &Watchdog::signalSendInfo,
            this,        &Router::slotBroadcastNetwork, Qt::DirectConnection);

    connect(d->watchdog, &Watchdog::signalNetworkChanged,
            this,        &Router::slotUpdateNbApps, Qt::DirectConnection);


    connect(d->synchronizer, &SynchronizerControl::signalSendToNet,
            this,            &Router::slotBroadcastNetwork, Qt::DirectConnection);

    connect(d->synchronizer, &SynchronizerControl::signalSendToApp,
            this,            &Router::slotBroadcastLocal, Qt::DirectConnection);

    connect(d->synchronizer, &SynchronizerControl::signalRequestElection,
            this,            &Router::slotRequestElection, Qt::DirectConnection);

    // connect signal signalSendElectionMessage from electionMng to slot slotBroadcastNetwork
    connect(d->electionMng, &ElectionManager::signalSendElectionMessage,
            this,           &Router::slotBroadcastNetwork, Qt::DirectConnection);


    connect(d->electionMng, &ElectionManager::signalWinElection,
            this,           &Router::slotWinElection, Qt::DirectConnection);
}

Router::~Router()
{
    delete d;
}

bool Router::addSnapshot(LaiYangSnapshot* snapshot)
{
    if (!snapshot)
    {
        return false;
    }

    d->snapshot = snapshot;

    connect(d->snapshot, &LaiYangSnapshot::signalRequestSnapshot,
            this,        &Router::slotBroadcastLocal, Qt::DirectConnection);

    connect(d->snapshot, &LaiYangSnapshot::signalSendSnapshotMessage,
            this,        &Router::slotBroadcastNetwork, Qt::DirectConnection);

    connect(d->snapshot, &LaiYangSnapshot::signalRequestElection,
            this,        &Router::slotRequestElection, Qt::DirectConnection);

    connect(d->snapshot, &LaiYangSnapshot::signalFinishElection,
            this,        &Router::slotFinishElection, Qt::DirectConnection);

    return true;
}


// Main event handler
void Router::slotReceiveMessage(Header header, Message message)
{
    // cast message to ACL format
    ACLMessage aclMessage(*(static_cast<ACLMessage*>(&message)));

    if (header.what() == QLatin1String("NET"))
    {
        // Message From NET
        if (d->isOldMessage(aclMessage))
        {
            return;
        }

        switch (aclMessage.getPerformative())
        {
            case ACLMessage::INFORM_STATE:
                d->forwardStateMessage(aclMessage, false);
                break;

            case ACLMessage::PREPOST_MESSAGE:
                d->forwardPrepost(aclMessage);
                break;

            case ACLMessage::SNAPSHOT_RECOVER:
                d->forwardRecover(aclMessage);
                break;

            case ACLMessage::READY_SNAPSHOT:
                d->forwardReady(aclMessage);
                break;

            case ACLMessage::PING:
                d->forwardPing(aclMessage);
                break;

            case ACLMessage::PONG:
                d->forwardPong(aclMessage, false);
                break;

            case ACLMessage::REQUEST_MUTEX:
                d->receiveMutexRequest(aclMessage, false);
                break;

            case ACLMessage::ACCEPT_MUTEX:
                d->receiveMutexApproval(aclMessage, false);
                break;

            case ACLMessage::ELECTION:
                d->receiveElectionMsg(aclMessage);
                break;

            case ACLMessage::FINISH_ELECTION:
                d->receiveElectionMsg(aclMessage);
                break;

            case ACLMessage::ACK_ELECTION:
                d->receiveElectionMsg(aclMessage);
                break;

            default:
                d->forwardNetToApp(header, aclMessage);
                break;
        }
    }
    else
    {
        // Message From Base
        switch (aclMessage.getPerformative())
        {
            case ACLMessage::INFORM_STATE:
                // receive local snapshot state
                d->forwardStateMessage(aclMessage, true);
                break;

            case ACLMessage::PONG:
                d->forwardPong(aclMessage, true);
                break;

            case ACLMessage::REQUEST_MUTEX:
                d->receiveMutexRequest(aclMessage, true);
                break;

            case ACLMessage::ACCEPT_MUTEX:
                d->receiveMutexApproval(aclMessage, true);
                break;

            default:
                d->forwardAppToNet(header, aclMessage);
                break;
        }
    }
}

void Router::slotBroadcastLocal(const Message& message)
{
    d->communicationMngr->send(message, QLatin1String("NET"), Header::allApp, Header::localHost);
}

void Router::slotBroadcastNetwork(ACLMessage& message)
{
    message.setSender(d->siteID);
    message.setNbSequence(++d->nbSequence);

    d->communicationMngr->send(message, QLatin1String("NET"), QLatin1String("NET"), Header::allHost);
}

void Router::slotUpdateNbApps(int nbSites, int nbApp)
{
    if (nbApp != d->nbApp)
    {
        qDebug() << d->siteID << ": network changed => restart all mutex";
        // In case the network structure changed, it should restart the process of mutex in order to avoid deadlock
        d->refuseAllPendingRequest();
    }

    d->nbApp = nbApp;

    d->snapshot->setNbOfApp(nbApp);
    d->snapshot->setNbOfNeighbor(nbSites - 1);
    d->synchronizer->setNbOfApp(nbApp);
    d->electionMng->setNbOfNeighbor(nbSites - 1);
}

void Router::slotRequestElection()
{
    // find who is sender and request Election to electionMng with correspondant reason
    if (dynamic_cast<LaiYangSnapshot*>(sender()) != nullptr)
    {
        qDebug() << "call election for snapshot from" << d->siteID;
        d->electionMng->initElection(ElectionManager::Snapshot);
    }
    else if(dynamic_cast<SynchronizerControl*>(sender()) != nullptr)
    {
        qDebug() << "call election for synchronizer from" << d->siteID;
        d->electionMng->initElection(ElectionManager::Synchronizer);
    }
}

void Router::slotWinElection(ElectionManager::ElectionReason reason)
{
    switch (reason)
    {
        case ElectionManager::Snapshot:
            d->snapshot->init();
            break;
        case ElectionManager::Synchronizer:
            d->synchronizer->init(d->siteID);
            break;
        default:
            break;
    }
}

void Router::slotFinishElection()
{
    if (dynamic_cast<LaiYangSnapshot*>(sender()) != nullptr)
    {
        d->electionMng->finishElection(ElectionManager::Snapshot);
    }
    else if(dynamic_cast<SynchronizerControl*>(sender()) != nullptr)
    {
        d->electionMng->finishElection(ElectionManager::Synchronizer);
    }
}

}
