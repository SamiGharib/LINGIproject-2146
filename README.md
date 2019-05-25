# LINGI2146: Group Project

## Description

This repository contains the files necessary to create and run an IoT network with sensor nodes that produce data which will be send to MQTT subscribers.

### Sensor Network

#### Tree-based Routing Protocol

There are two kind of nodes in the sensor network:

* A unique root node which doesn't compute any sensor data but is responsible for the communication between the sensor nodes and the gateway (PC)
* An unlimited amount of sensor nodes that compute data and communicate with each other in a tree based topology

In order to create a tree based topology, nodes exchange two kinds of messages. The DIO message is broadcasted by each node. It contains the following information:

* An identifier of the message, the letter "O"
* The rank of the node, which is the number of hops to reach the root node in our case
* The current configuration set by the gatweway, "P" for periodic transmission of sensor data and "O" for transmission of sensor data on change

Nodes that receive the DIO message can use it to choose the best node as their parent node (according to the number of hops). Furthermore, if a DIO message is not received by the parent node for some time, the parent node is considered disconnected and the child nodes will use another node as their parent node.


The second type of message that is exchanged is the DAO message. This message only contains the letter "A" and is send via unicast from each child node to its parent node periodically. The reason of this message is to inform the parent node that is has a child node. There is no need to broadcast this message since each node will only have one single parent. The parent node keeps track of a timer for each child node and if no DAO message is received for some time, the child node is considered disconnected.


Note that both of these messages are send in an unreliable way. The reason to prefer non-reliable transmission over reliable transmission (runicast) is that we do not block the channel by waiting for an acknowledgment. The timer is fixed to a duration that allows up to 4 failed transmissions before the node is considered disconnected.

#### Transmission of sensor data

The transmission of sensor data relies on reliable unicast, since the delivery of data is important. The data is forwarded from each node to its parent node until finally reaching the root node. The structure of the message is as follow:

<(id of sensor node)/(H for Humidity | B for battery)/(value)
