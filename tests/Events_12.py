
#!/usr/bin/env python

import time
import threading
import lsst.ctrl.events as events
from lsst.daf.base import PropertySet

#
# Send an event
#
def sendEvent(hostName, topic):
    trans = events.EventTransmitter(hostName, topic)
    
    root = PropertySet()
    root.set("TOPIC",topic)
    root.set("myname","myname")
    root.set("STATUS", "my special status")
    root.set("RUNID","srptestrun")
    root.set("bazinga", "sheldon")
    
    eventSystem = events.EventSystem.getDefaultEventSystem()
    originatorId = eventSystem.createOriginatorId()
    event = events.StatusEvent("srptestrun", originatorId, root)

    # ok...now publish it
    trans.publishEvent(event)

if __name__ == "__main__":
    host = "lsst8.ncsa.uiuc.edu"
    topicA = "PIPELINE.A"

    yC = events.EventReceiver(host, topicA)

    #
    # send a test event, and wait to receive it
    #
    sendEvent(host, topicA)

    val = yC.receiveEvent()
    assert val != None
    print "custom property names"
    print val.getCustomPropertyNames()
    print "Custom PropertySet"
    ps = val.getCustomPropertySet()
    print ps.toString()
    print
    print "filterable property names"
    print val.getFilterablePropertyNames()

    print "PropertySet"
    ps = val.getPropertySet()
    print ps.toString()

    eventSystem = events.EventSystem().getDefaultEventSystem()
    statusevent = eventSystem.castToStatusEvent(val)
    print "OriginatorId"
    print statusevent.getOriginatorId()

