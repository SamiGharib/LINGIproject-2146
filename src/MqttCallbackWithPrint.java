/*
We reimplemented MqttCallbackin order to print when a message has been received and to track the used topics
*/

import java.util.ArrayList;
import java.util.Arrays;
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
        
      System.out.println(getName() + " lost connection to broker "+throwable.getCause().getMessage());
    }
    
    // Modified in order to print when a message has arrived and to track the topics still used
    public void messageArrived(String topic, MqttMessage mqttMessage) throws Exception {
        String []tab = mqttMessage.toString().split("/");
        String request;
        //The publisher fills in the topics to always have them
        if(getName().equals("Publisher")){
            if(tab[1].equals("Battery")){
            request = tab[0]+"/B";
            }
            else{
                request = tab[0]+"/T";
            }
            if(!topics.contains(request)){ 
                topics.add(request);
            }
        }
        
      else if(!getName().equals("Publisher")){
          System.out.println(getName()+ " got message: " + mqttMessage.toString() + " with topic: " + topic);
      }
    }

    public void deliveryComplete(IMqttDeliveryToken iMqttDeliveryToken) {
    }
    
    public void resetTopicsCount(){
        topics.clear();
    }
    
    public ArrayList<String> getTopics(){
        return (ArrayList<String>) topics;
    }
    
    public String getName(){
        return name;
    }
}
