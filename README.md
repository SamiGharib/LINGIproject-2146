# LINGI2146: Group Project

### Description

This reposirtory contains the files necessary to create and run an IoT network with sensor nodes that produce data which will be send to MQTT subscribers. 

#### Sensor Network 

There are two kind of nodes in the sensor network:

* A unique root node which doesn't compute any sensor data but is responsible for the communication between the sensor nodes and the gateway (PC)
* An unlimited amount of sensor nodes that compute data and forward it to their parent node. They also forward data they receive from their parent to their children (if they have any children)

