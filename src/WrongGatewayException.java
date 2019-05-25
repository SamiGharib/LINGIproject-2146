/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

/**
 *
 * @author sami
 */
public class WrongGatewayException extends Exception{
    
    public WrongGatewayException(){
        System.out.println("Please enter one port as argument (example : /dev/ttyUSB0)");
    }
    
}
