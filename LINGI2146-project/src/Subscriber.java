/*
 * This implements a Subcribers
 * Input : String subscriber name String Topic1 String Topic2 (if any)
 */
import org.eclipse.paho.client.mqttv3.MqttClient;
import org.eclipse.paho.client.mqttv3.MqttException;
import org.eclipse.paho.client.mqttv3.MqttMessage;

public class Subscriber {
    
    public static void main(String[] args) throws WrongSubscriberException, MqttException {
        if(args.length < 2){
            throw new WrongSubscriberException(1);
        }
        String[] test;
        for(int i = 1; i<args.length;i++){
            test = args[i].split("/");
            if(!test[1].equals("Temperature") && !test[1].equals("Humidity")){
                throw new WrongSubscriberException(2);
            }
        }
        
        String subscriberName = args[0];
        
        
        MqttCallbackWithPrint callback = new MqttCallbackWithPrint(subscriberName);
        MqttClient subscriber = new MqttClient("tcp://localhost:1883", subscriberName);
        subscriber.setCallback(callback);
        subscriber.connect();

        for(int i = 1; i<args.length-1;i++){
            subscriber.subscribe(args[i]);
        }
        try {
            while (true) {
                MqttMessage msg;
                for(int i =1; i<args.length;i++){
                    msg = new MqttMessage();
                    msg.setPayload(args[i].getBytes());
                    subscriber.publish("Topic", msg);
                    Thread.sleep(2500);
                }
            }
        } catch (Exception e) {
            e.printStackTrace();
            System.exit(1);
        }
    }

}
