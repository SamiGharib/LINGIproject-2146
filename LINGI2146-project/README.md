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

Nodes that receive the DIO message can use it to choose the best node as their parent node (according to the number of hops). Furthermore, if a DIO message is not received by the parent node for some time, the parent node is considered disconnected and the child nodes will use another node as their parent node.
The second type of message that is exchanged is the DAO message. This message only contains the letter "A" and is send via unicast from each child node to its parent node periodically. The reason of this message is to inform the parent node that we selected it as a parent so that we receive configuration messages from our parent. There is no need to broadcast this message since each node will only have one single parent. The parent node keeps track of a timer for each child node and if no DAO message is received for some time, the child node is considered disconnected.

Note that both of these messages are send in an unreliable way. The reason to prefer non-reliable transmission over reliable transmission (runicast) is that the timer is fixed to a duration that allows up to 4 failed transmissions before the node is considered disconnected.

#### Transmission of sensor data

The transmission of sensor data relies on reliable unicast, since the delivery of data is important. The data is forwarded from each node to its parent node until finally reaching the root node. 

#### Transmission of configuration data

TODO


### Gateway (Not tested yet) called by : java Gateway port (port = /dev/ttyUSB0)

- Subscribers: Reads their requests (topics) via the broker which is already implemented in java (mqtt).(done via the MqttCallbackWithPrint)
			   Forwards the datas (number value) from the root node to the Subscribers also via mqtt.(Thread readInput)
				
- Root node: Reads the datas received from root node via serial communication (serialdump-linux or windows) (Thread readInput)
			 Forwards them requests from subscribers (nodeN/topic) and an option to indicate if a datas should be sent on change or periodically (O/P)(Thread writeOutput)
				Example of message it receives : node1/Temperature/24



### Subscribers called by : java Subscriber subscriberName nodeN/topic nodeM/topic ... (node/topic = topics to which it subscribes)