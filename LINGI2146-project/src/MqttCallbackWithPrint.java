/*
We reimplemented MqttCallbackin order to print when a message has been received and to track the used topics
*/

import java.util.ArrayList;
import org.eclipse.paho.client.mqttv3.MqttCallback;
import org.eclipse.paho.client.mqttv3.MqttMessage;
import org.eclipse.paho.client.mqttv3.IMqttDeliveryToken;

public class MqttCallbackWithPrint implements MqttCallback{

    private ArrayList<String> topics;
    private String name;
    
    public MqttCallbackWithPrint(String name){
        this.name = name; // name = "Publisher" for the Gateway or another String for Subscribers
        this.topics = new ArrayList<>();
    }
    
    public void connectionLost(Throwable throwable) {
      System.out.println(getName() + " lost connection to broker");
    }
    
    // Modified in order to print when a message has arrived and to track the topics still used
    public void messageArrived(String topic, MqttMessage mqttMessage) throws Exception {
        
      System.out.println(getName()+ " got message: " + mqttMessage.toString() + " with topic: " + topic);
      if(getName().equals("Publisher") && !topics.contains(mqttMessage.toString())){
          topics.add(mqttMessage.toString());
      }
    }

    public void deliveryComplete(IMqttDeliveryToken iMqttDeliveryToken) {
    }
    
    public void resetTopicsCount(){
        topics = new ArrayList<>();
    }
    
    public ArrayList<String> getTopics(){
        return (ArrayList<String>) topics;
    }
    
    public String getName(){
        return name;
    }
}
