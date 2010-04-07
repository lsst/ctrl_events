// -*- lsst-c++ -*-
/** \file EventTransmitter.cc
  *
  * \brief Objects to send Events to the specified event bus
  *
  * \ingroup ctrl/events
  *
  * \author Stephen R. Pietrowicz, NCSA
  *
  */
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <limits>
#include <cstring>
#include <time.h>

#include "lsst/ctrl/events/Event.h"
#include "lsst/ctrl/events/EventTransmitter.h"
#include "lsst/ctrl/events/EventSystem.h"
#include "lsst/daf/base/DateTime.h"
#include "lsst/daf/base/PropertySet.h"
#include "lsst/pex/exceptions.h"
#include "lsst/pex/logging/Component.h"
#include "lsst/pex/policy/Policy.h"
#include "lsst/pex/logging/LogRecord.h"
#include <sys/socket.h>
#include <sys/un.h>
#include "lsst/ctrl/events/EventLibrary.h"
#include "lsst/ctrl/events/EventBroker.h"

#include <activemq/core/ActiveMQConnectionFactory.h>

namespace pexExceptions = lsst::pex::exceptions;
namespace pexLogging = lsst::pex::logging;


using namespace std;
using std::numeric_limits;

namespace lsst {
namespace ctrl {
namespace events {

/** \brief Configures an EventTransmitter based on Policy contents.
  *
  * The Policy object is checked for four keywords:
  *
  * topicName - the name of the topic to send to       
  * hostName - the name of the host hosting the event broker
  * hostPort - the port the event broker is listening on 
  * turnEventsOff - turn off event transmission
  *
  * Defaults are:
  *
  * turnEventsOff = false
  * hostPort = EventSystem::DEFAULTHOSTPORT
  * 
  * The dependencies for these keywords are as follows:
  *
  * 1) If turnEventsOff is specified as true, no further checking is done, and 
  * no events will be transmitted.
  *
  * 2) If no topicName is specified, a NotFound exception is thrown
  *
  * \param policy the policy object to use when building the receiver
  * \throw throws NotFoundException if expected keywords are missing in Policy object
  * \throw throws RuntimeErrorException if connection to transport mechanism fails
  */
EventTransmitter::EventTransmitter( const pexPolicy::Policy& policy) {
    int hostPort;

    EventLibrary().initializeLibrary();

    try {
        _turnEventsOff = policy.getBool("turnEventsoff");
    } catch (pexPolicy::NameNotFound& e) {
        _turnEventsOff = false;
    }
    if (_turnEventsOff == true)
        return;

    if (!policy.exists("topicName")) {
        throw LSST_EXCEPT(pexExceptions::NotFoundException, "topicName not found in policy");
    }
    _topicName = policy.getString("topicName");

    std::string hostName;
    try {
        hostName = policy.getString("hostName");
    } catch (pexPolicy::NameNotFound& e) {
        throw LSST_EXCEPT(pexExceptions::NotFoundException, "hostName not found in policy");
    }

    try {
        hostPort = policy.getInt("hostPort");
    } catch (pexPolicy::NameNotFound& e) {
        hostPort = EventBroker::DEFAULTHOSTPORT;
    }
    init(hostName, _topicName, hostPort);
}

/** \brief Transmits events to the specified host and topic
  *
  * \param hostName the machine hosting the message broker
  * \param topicName the topic to transmit events to
  * \param hostPort the port number which the message broker is listening to
  * \throw throws RuntimeErrorException if local socket can't be created
  * \throw throws RuntimeErrorException if connect to local socket fails
  * \throw throws RuntimeErrorException if connect to remote ActiveMQ host fails
  */
EventTransmitter::EventTransmitter( const std::string& hostName, const std::string& topicName, int hostPort) {
    EventLibrary().initializeLibrary();

    _turnEventsOff = false;
    init(hostName, topicName, hostPort);
}

/** private initialization method for configuring EventTransmitter
  */
void EventTransmitter::init( const std::string& hostName, const std::string& topicName, int hostPort) {
    _connection = NULL;
    _session = NULL;
    // _destination = NULL;
    _producer = NULL;
    _topicName = topicName;
    _topic = NULL;

    if (_turnEventsOff == true)
        return;

    // set up a connection to the ActiveMQ server for message transmission
    try {
        std::stringstream ss;

        ss << hostPort;

        /*
         * Create a ConnectionFactory to connect to hostName, and
         * create a topic for this.
         */
        string brokerUri = "tcp://"+hostName+":"+ss.str()+"?wireFormat=openwire&transport.useAsyncSend=true";

        activemq::core::ActiveMQConnectionFactory* connectionFactory =
            new activemq::core::ActiveMQConnectionFactory( brokerUri );

        _connection = 0;
        try {
            _connection = connectionFactory->createConnection();
            _connection->start();
            delete connectionFactory;
        }
        catch (cms::CMSException& e) {
            delete connectionFactory;
            std::string msg("Failed to connect to broker: ");
            msg += e.getMessage();
            msg += " (is broker running?)";
            throw LSST_EXCEPT(pexExceptions::RuntimeErrorException, msg);
        }

        _session = _connection->createSession( cms::Session::AUTO_ACKNOWLEDGE );
       
        // Create the destination topic
        //_destination = _session->createTopic( topicName );

        // Create Topic
        _topic = new activemq::commands::ActiveMQTopic(_topicName);
        

        // Create a MessageProducer from the Session to the Topic or Queue
        _producer = _session->createProducer(NULL);
        _producer->setDeliveryMode( cms::DeliveryMode::NON_PERSISTENT );
    } catch ( cms::CMSException& e ) {
        throw LSST_EXCEPT(pexExceptions::RuntimeErrorException, std::string("Trouble creating EventTransmitter: ") + e.getMessage());
    }
}

void EventTransmitter::publish(const PropertySet::Ptr& psp) {
    publish(*psp);
}

void EventTransmitter::publish(const PropertySet& ps) {
    if (_turnEventsOff == true)
        return;

    publish("event", ps);
}

/** \brief publish an event of "type"        
  *
  * \param rec The LogRecord to send.  This is used internally by the logging
  *            subsystem and is exposed here to send LogRecord through event
  *            channels.
  * \throw throws Runtime exception if publish fails
  */
/*
void EventTransmitter::publish(const pexLogging::LogRecord& rec) {

    if (_turnEventsOff == true)
        return;

    const PropertySet& ps = rec.getProperties();

    publish("logging", ps);
}
*/
void EventTransmitter::publish(const pexLogging::LogRecord& rec) {

    if (_turnEventsOff == true)
        return;

    const PropertySet& ps = rec.getProperties();

    std::cout << "In EventTransmitter::publish, the rec looks like" << std::endl;
    std::cout << ps.toString() << std::endl;
    if (!ps.exists(Event::RUNID)) {
        LogEvent event("unspecified",rec);
        publishEvent(event);
    } else {
        std::string runid = ps.get<std::string>(Event::RUNID);
        LogEvent event(runid,rec);
        publishEvent(event);
    }
}

void EventTransmitter::publishEvent(const Event& event) {
    PropertySet::Ptr psp;
    time_t _pubtime;
    long pubtime;
    cms::TextMessage* message = _session->createTextMessage();

    // since we can only create TextMessage objects via a Session,
    // create the object, and pass it to the Event to be populated.
    // The event, knowing the type that it is, can populate the
    // message properly itself.

    event.populateHeader(message);

    message->setStringProperty("TOPIC", _topicName);
    
    pubtime = time(&_pubtime);
    message->setLongProperty("PUBTIME", pubtime);

    psp = event.getCustomPropertySet();
    std::string payload = marshall(*psp);
    message->setText(payload);

    _producer->send(_topic, message);
    delete message;
}

/** private method used to send event out on the wire.
  */
void EventTransmitter::publish(const std::string& type, const PropertySet& ps) {

    if (_session == 0)
        throw LSST_EXCEPT(pexExceptions::RuntimeErrorException, "Not connected to event server");

    // Sending the message to the ActiveMQ server
    cms::TextMessage* message = _session->createTextMessage();
    message->setCMSType(type);
    std::string payload = marshall(ps);

    message->setText(payload);
    _producer->send(_topic, message );

    delete message;
}

std::string EventTransmitter::marshall(const PropertySet& ps) {
    std::vector<std::string> v = ps.paramNames(false);

    // TODO: optimize this to get use getArray only when necessary
    std::ostringstream payload;
    unsigned int i;
    payload << "nodelist||nodelist||" << (v.size()) << "~~";
    for (i = 0; i < v.size(); i++) {
        std::string name = v[i];
        if (ps.typeOf(name) == typeid(bool)) {
            std::vector<bool> vec  = ps.getArray<bool>(name);
            std::vector<bool>::iterator iter;
            for (iter = vec.begin(); iter != vec.end(); iter++) {
                payload << "bool||"<< name << "||" << *iter << "~~";
            }
        } else if (ps.typeOf(name) == typeid(long)) {
            std::vector<long> vec  = ps.getArray<long>(name);
            std::vector<long>::iterator iter;
            for (iter = vec.begin(); iter != vec.end(); iter++) {
                payload << "long||" << name << "||"<< *iter << "~~";
            }
        } else if (ps.typeOf(name) == typeid(int)) {
            std::vector<int> vec  = ps.getArray<int>(name);
            std::vector<int>::iterator iter;
            for (iter = vec.begin(); iter != vec.end(); iter++) {
                payload << "int||" << name << "||"<< *iter << "~~";
            }
        } else if (ps.typeOf(name) == typeid(float)) {
            std::vector<float> vec  = ps.getArray<float>(name);
            std::vector<float>::iterator iter;
            payload.precision(numeric_limits<float>::digits10+1);
            for (iter = vec.begin(); iter != vec.end(); iter++) {
                payload << "float||"<< name << "||"<< *iter << "~~";
            }
        } else if (ps.typeOf(name) == typeid(double)) {
            std::vector<double> vec  = ps.getArray<double>(name);
            std::vector<double>::iterator iter;
            payload.precision(numeric_limits<double>::digits10+1);
            for (iter = vec.begin(); iter != vec.end(); iter++) {
                payload << "double||"<< name << "||"<< *iter << "~~";
            }
        } else if (ps.typeOf(name) == typeid(std::string)) {
            std::vector<std::string> vec  = ps.getArray<std::string>(name);
            std::vector<std::string>::iterator iter;
            for (iter = vec.begin(); iter != vec.end(); iter++) {
                payload << "string||" << name << "||"<< *iter << "~~";
            }
        } else if (ps.typeOf(name) == typeid(lsst::daf::base::DateTime)) {
            std::vector<lsst::daf::base::DateTime> vec  = ps.getArray<lsst::daf::base::DateTime>(name);
            std::vector<lsst::daf::base::DateTime>::iterator iter;
            for (iter = vec.begin(); iter != vec.end(); iter++) {
                payload << "datetime||" << name << "||"<< (*iter).nsecs() << "~~";
            }
        } else {
            std::cout << "Couldn't marshall "<< name << std::endl;
        }
    }
    return payload.str();
}

/** \brief get the topic name of this EventTransmitter
  */
std::string EventTransmitter::getTopicName() {
    return _topicName;
}

/** \brief Destructor for EventTransmitter
  */
EventTransmitter::~EventTransmitter() {

/*
    // Destroy resources.
    try {
        if( _destination != NULL )
            delete _destination;
    } catch ( cms::CMSException& e ) {
        e.printStackTrace();
    }
    _destination = NULL;
*/

    try {
        if( _producer != NULL )
            delete _producer;
    } catch ( cms::CMSException& e ) {
        e.printStackTrace();
    }
    _producer = NULL;

    // Close open resources.
    try {
        if( _session != NULL )
            _session->close();
        if( _connection != NULL )
            _connection->close();
    } catch ( cms::CMSException& e ) {
        e.printStackTrace();
    }

    try {
        if( _session != NULL )
            delete _session;
    } catch ( cms::CMSException& e ) {
        e.printStackTrace();
    }
    _session = NULL;

    try {
        if( _connection != NULL )
            delete _connection;
    } catch ( cms::CMSException& e ) {
        e.printStackTrace();
    }
    _connection = NULL;
}

}
}
}
