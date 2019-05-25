/*
 * Exception used at the creation of a Subscriber
 */
class WrongSubscriberException extends Exception{ 
  public WrongSubscriberException(int flag){
      if(flag == 1){
          //No enough arguments as input
          System.out.println("Wrong subscriber initialization. A subscriber needs at least one topic at initialization.");
          System.out.println("Expected input form : \"SubscriberName\" \"nodeID/Topic1\" \"nodeID/Topic2\"");
      }
      else if(flag == 2){
          //Wrong topic field
          System.out.println("Available Topics : \"Battery\" and \"Humidity\"");
      }
      else if(flag == 3){
          System.out.println("You subscribed more than once to the same topic");
      }
    
    
    
  }  
}
