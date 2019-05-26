/*
 * This class is inspired from the serial connection at 
http://courses.cs.tau.ac.il/embedded/contiki-2.3/examples/sky-shell/src/se/sics/contiki/collect/SerialConnection.java
 It implements the Gateway and the MQTT broker
 */

/**
 *
 * @author sami
 */
import java.io.BufferedReader;
import java.io.BufferedWriter;
import java.io.InputStreamReader;
import java.io.OutputStreamWriter;
import java.util.ArrayList;
import java.util.Scanner;
import org.eclipse.paho.client.mqttv3.MqttClient;
import org.eclipse.paho.client.mqttv3.MqttMessage;

public class Gateway {
    public static final String SERIALDUMP_WINDOWS = "/home/user/contiki/tools/sky/serialdump-windows.exe";
    public static final String SERIALDUMP_LINUX = "/home/user/contiki/tools/sky/serialdump-linux"; //See ex session with hardware
    private Process serialDumpProcess;
    private ArrayList<String> topics;
    private ArrayList<String> previousTopics = new ArrayList<>();
    
    public Gateway(String port)
    {
        String fullCommand;
        String osName = System.getProperty("os.name").toLowerCase();
        if (osName.startsWith("win")) {
            fullCommand = SERIALDUMP_WINDOWS + " " + "-b115200" + " " + getMappedPortForWindows(port);
        } else {
            fullCommand = SERIALDUMP_LINUX + " " + "-b115200" + " " + port;
        }
        
        try{
            String[] command = fullCommand.split(" ");
            serialDumpProcess = Runtime.getRuntime().exec(command);
            final BufferedReader input = new BufferedReader(new InputStreamReader(serialDumpProcess.getInputStream()));
            final BufferedReader err = new BufferedReader(new InputStreamReader(serialDumpProcess.getErrorStream()));
            final BufferedWriter output = new BufferedWriter(new OutputStreamWriter(serialDumpProcess.getOutputStream()));
            
            final MqttClient gateway = new MqttClient("tcp://localhost:1883", MqttClient.generateClientId());
            gateway.connect();
            
            final MqttCallbackWithPrint callback = new MqttCallbackWithPrint("Publisher");
            gateway.setCallback(callback);
            gateway.subscribe("Topic");
            
            final Scanner scan = new Scanner(System.in);
            
            /* Start thread listening on stdout */
            Thread readInput = new Thread(new Runnable() {
                public void run() {
                String line;
                try {
                    while ((line = input.readLine()) != null) {
                        String[] data = line.split("/"); //Messages from nodes have the form "ID/Battery(Humidity)/value"
                        System.out.println("received from root: "+ line);
                        String sensed = "wrongdata";
                        if(data[1].equals("B")){
                            sensed = "Battery";
                        }
                        else if(data[1].equals("T")){
                            sensed = "Temperature";
                        }
                        String topic = data[0]+"/"+sensed; //nodeID/Battery
                        String value = data[2]; //value
                        MqttMessage msg = new MqttMessage();
                        if(sensed.equals("Battery") || sensed.equals("Temperature")){
                            msg.setPayload(value.getBytes());
                            gateway.publish(topic, msg);
                            System.out.println("Published to subcribers: "+line);
                        }
                        else{
                            System.out.println("Wrong message received: "+line);
                        }
                    }
                    input.close();
                    System.out.println("Serialdump process terminated.");
                    System.exit(1);
                    } catch (Exception e) {
                    System.out.println(e.getMessage());
                    System.exit(1);
                }
                }
      }, "read input data thread");
            
            /* Start thread listening on stdout and sending configuration to nodes */
            Thread writeOutput = new Thread(new Runnable() {
                public void run() {
                String config;
                try {
                    while(true) {
                        System.out.println("Select configuration [O/P]: Send data on change of value / Send data periodically");
                        while((config = scan.nextLine()) == null){
                            // Waits for an input from user
                        }
                        if(config.equals("P")){
                            output.write("P");
                            output.flush();
                            System.out.println("Data will be sent periodically");
                        }
                        else if(config.equals("O")){
                            output.write("O");
                            output.flush();
                            System.out.println("Data will be sent on change");
                        }
                        else{
                            System.out.println("Wrong configuration: "+config);
                        }
                    }
                } catch (Exception e) {
                    System.out.println(e.getMessage());
                    System.exit(1);
                }
                }
      }, "send configuration thread");
            
            Thread optimization = new Thread(new Runnable() {
                public void run() {
                try {
                    while(true) {
                        topics = callback.getTopics();
                        for(int i =0; i<previousTopics.size();i++){
                            String s = previousTopics.get(i);
                            if(!topics.contains(s)){
                                String stopSend = s+"/0";
                                output.write(stopSend);
                                output.flush();
                                System.out.println(stopSend + " has been sent to root node");
                            }
                        }
                        for(int i =0; i<topics.size();i++){
                            String s = topics.get(i);
                            if(!previousTopics.contains(s)){
                                String startSend = s+"/1";
                                output.write(startSend);
                                output.flush();
                                System.out.println(startSend + " has been sent to root node");
                            }
                        }
                        previousTopics = (ArrayList<String>) topics.clone();
                        callback.resetTopicsCount();
                        System.out.println("New comm");
                        Thread.sleep(30000); // wait 30 seconds to be sure that all subscribers are treated in the topics filling
                    }
                } catch (Exception e) {
                    System.out.println(e.getMessage());
                    System.exit(1);
                }
                }
      }, "optimization thread to send what data has no subscriber");
            
            optimization.start();
            readInput.start();
            writeOutput.start();
            
        } catch(Exception e){
            System.out.println(e.getMessage());
            System.exit(1);
        }
    }
 
    
    private String getMappedPortForWindows(String port) {
        if (port.startsWith("COM")) {
        port = "/dev/com" + port.substring(3);
        }
        return port;
    }
    
    
    public static void main(String[] args) throws WrongGatewayException{
        if(args.length != 1){
            throw new WrongGatewayException();
        }
        String port = args[0];
        Gateway gateway = new Gateway(port);
  }
    
}
