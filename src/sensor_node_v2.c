#include "contiki.h"
#include "dev/leds.h"
#include "dev/button-sensor.h"
#include <stdio.h>
#include <stdlib.h>
#include "net/rime.h"
#include "random.h"
#include <string.h>
#include "sys/timer.h"
#include <limits.h>
#include "dev/temperature-sensor.h"
#include "dev/battery-sensor.h"

PROCESS(sensor_node_process, "Sensor node");
AUTOSTART_PROCESSES(&sensor_node_process);

/********************************************//**
*  MACRO DEFINITIONS
***********************************************/

// maximum number of supported children nodes
#define MAX_CHILDREN 10
// number of retransmissions in reliable unicast
#define RETRANSMISSION 5
// duration after which a node is considered as disconnected
#define TIME_OUT 45
// duration after which the node sends data -> when config = periodically
#define DATA_TIME 30


/********************************************//**
*  CONSTANT DEFINITIONS
***********************************************/

// message used to tell the parent node that the node is still alive
static const char ALIVE[] = "A";


/********************************************//**
*  RIME ADDRESSES
***********************************************/

// array of all the children nodes
static rimeaddr_t children_nodes[MAX_CHILDREN];
// reference to the parent node
static rimeaddr_t parent_node;
// reference to this node
static rimeaddr_t this_node;


/********************************************//**
*  TIMERS
***********************************************/

// a timer associated to each child node
static struct timer children_timer[MAX_CHILDREN];
// a timer associated to the parent node
static struct timer parent_timer;
// a timer associated to the transmission of data
static struct timer data_timer;


/********************************************//**
*  Structures for broadcast / (r)unicast
***********************************************/

static struct broadcast_conn broadcast;
static struct unicast_conn unicast;
static struct runicast_conn runicast;


/********************************************//**
*  Other global variables
***********************************************/

// check if we have a parent node currently
static int has_parent = 0;
// the rank of this node -> number of hops to root node
static int this_rank = INT_MAX;
// the current configuration: P -> periodically | C -> on Change
static char config = 'P';
// previous values of sensor data
static int prev_temp[2] = {1, 1};
static int prev_bat = 0;
// is there a subscriber for a given channel?: 0 -> no subscriber | 1 -> subscriber
static int temp_subscriber = 0;
static int bat_subscriber = 1;


/********************************************//**
*  Function definitions
***********************************************/

/**
* Sends battery data to the parent node if there is at least
* one subscriber for this channel and if the current configuration
* is respected
* @ param  config  : the current configuration
* @ return /
*/
static void send_battery(char config) {
  // There is at least one subscriber to the channel -> otherwise we don't send data
  if(bat_subscriber != 0 ) {

    // TODO get battery data
    int x = 1;

    // configuration is set to send periodically
    if(config == 'P') {
      // only send if the timer expired
      if(timer_expired(&data_timer)) {
        // create message with format <ID/channel/data>
        char msg[7];
        sprintf(msg, "%d.%d/B/%d", this_node.u8[0], this_node.u8[1], x);
        // send message via runicast to parent node
        packetbuf_clear();
        packetbuf_copyfrom(msg, strlen(msg));
        runicast_send(&runicast, &parent_node, RETRANSMISSION);
        timer_restart(&data_timer);
      }
    }
    // configuration -> send on change
    else {
      // send only if temperature changed
      if(x != prev_bat) {
        // update previous battery values
        prev_bat = x;
        // create message with format <ID/channel/data>
        char msg[7];
        sprintf(msg, "%d.%d/A/%d", this_node.u8[0], this_node.u8[1], x);
        // send message via runicast to parent node
        packetbuf_clear();
        packetbuf_copyfrom(msg, strlen(msg));
        runicast_send(&runicast, &parent_node, RETRANSMISSION);
      }
    }
  }
}


/**
* Sends temperature data to the parent node if there is at least
* one subscriber for this channel and if the current configuration
* is respected
* @ param  config  : the current configuration
* @ return /
*/
static void send_temperature(char config) {

  // There is at least one subscriber to the channel -> otherwise we don't send data
  if(temp_subscriber != 0 ) {
    // get temperature value
    unsigned int temp = temperature_sensor.value(0);
    int first_digit = temp/10;
    int second_digit = temp - (temp/10)*10;
    // configuration is set to send periodically
    if(config == 'P') {
      // only send if the timer expired
      if(timer_expired(&data_timer)) {
        // create message with format <ID/channel/data>
        char msg[9];
        sprintf(msg, "%d.%d/T/%d.%d", this_node.u8[0], this_node.u8[1], first_digit, second_digit);
        // send message via runicast to parent node
        packetbuf_clear();
        packetbuf_copyfrom(msg, strlen(msg));
        runicast_send(&runicast, &parent_node, RETRANSMISSION);
        timer_restart(&data_timer);
      }
    }
    // configuration -> send on change
    else {
      // send only if temperature changed
      if(first_digit != prev_temp[0] || second_digit != prev_temp[1]) {
        // set previous temperature to current temperature
        prev_temp[0] = first_digit;
        prev_temp[1] = second_digit;
        // create message with format <ID/channel/data>
        char msg[9];
        sprintf(msg, "%d.%d/T/%d.%d", rimeaddr_node_addr.u8[0], rimeaddr_node_addr.u8[1], first_digit, second_digit);
        // send message via runicast to parent node
        packetbuf_clear();
        packetbuf_copyfrom(msg, strlen(msg));
        runicast_send(&runicast, &parent_node, RETRANSMISSION);
      }
    }
  }
}

/**
* This function is called upon a received broadcast packet. It uses the
* information in this packet to find the best possible parent node
* according to the number of hops criterion. If the node already has
* the best parent node, then the timer of the parent node is restarted upon
* arrival of a packet of the parent node.
* @ param  c     : the broadcast structure
* @ param  from  : the address of the broadcasting node
* @ return /
*/
static void broadcast_recv(struct broadcast_conn *c, const rimeaddr_t *from) {
  //printf("broadcast message received from %d.%d -> %s\n", from->u8[0], from->u8[1], (char *)packetbuf_dataptr());
  // extract the message
  char *message = (char *)packetbuf_dataptr();
  // we received a valid broadcast message
  if(message[0] == 'O') {
    // extract the rank out of the message
    int rank = atoi(&message[1]);
    // extract the current configuration of the message
    char con = message[2];
    // only accept the configuration if it was send by a node with a lower rank
    if(rank < this_rank) {
      config = con;
    }
    // shortest hop rule
    if(rank+1 < this_rank) {
      // the broadcasting node becomes our new parent node
      has_parent = 1;
      rimeaddr_t new_node;
      new_node.u8[0] = from -> u8[0];
      new_node.u8[1] = from -> u8[1];
      parent_node = new_node;
      this_rank = rank+1;
      timer_restart(&parent_timer);
    }
    // the message was sent from our parent node -> restart timer
    else if (has_parent == 1 && rimeaddr_cmp(&parent_node, from) != 0) {
      timer_restart(&parent_timer);
    }
  }
}

/**
* This function is called upon a received unicast packet. It uses the
* information in this packet to determin if the sending node is or wants
* to be our child
* @ param  c     : the unicast structure
* @ param  from  : the address of the unicasting node
* @ return /
*/
static void unicast_recv(struct unicast_conn *c, const rimeaddr_t *from)
{
  //printf("unicast message received from %d.%d: '%s'\n", from->u8[0], from->u8[1], (char *)packetbuf_dataptr());
  // extract the message
  char *message = (char *)packetbuf_dataptr();
  // used as counter variable in for loops
  int i;
  // we received aN ALIVE message
  if(strcmp(message, ALIVE) == 0) {
    // check if this node is already a child of us
    int is_child = 0;
    for(i=0; i < MAX_CHILDREN; i++) {
      // the node is already a child node -> restart timer
      if(rimeaddr_cmp(&(children_nodes[i]), from) != 0) {
        is_child = 1;
        timer_restart(&(children_timer[i]));
        break;
      }
    }
    // else this becomes a new child node
    if(is_child == 0) {
      // check if we have space left
      for(i=0; i < MAX_CHILDREN; i++) {
        if(rimeaddr_cmp(&(children_nodes[i]), &rimeaddr_null) != 0) {
          // create the new child
          rimeaddr_t new_node;
          new_node.u8[0] = from -> u8[0];
          new_node.u8[1] = from -> u8[1];
          children_nodes[i] = new_node;
          timer_restart(&(children_timer[i]));
          break;
        }
      }
    }
  }
}

/**
* This function is called upon a received runicast packet. Reliable unicast
* is only used to send sensor data. Upon reception of such a packet, is has
* to be forwarded to the parent node.
* @ param  c     : the unicast structure
* @ param  from  : the address of the unicasting node
* @ return /
*/
static void runicast_recv(struct runicast_conn *c, const rimeaddr_t *from, uint8_t seqno){
  //printf("runicast message received from %d.%d -> %s\n", from->u8[0], from->u8[1], (char *) packetbuf_dataptr());
  // extract the message
  char *message = (char *)packetbuf_dataptr();
  // TODO: aggregate the packets and send them when we send our sensor data
  packetbuf_clear();
  packetbuf_copyfrom(message, strlen(message));
  runicast_send(&runicast, &parent_node, RETRANSMISSION);
}


/********************************************//**
*  Broadcast / (R)unicast constructs
***********************************************/

static const struct unicast_callbacks unicast_call = {unicast_recv};
static const struct broadcast_callbacks broadcast_call = {broadcast_recv};
static const struct runicast_callbacks runicast_call = {runicast_recv};


/********************************************//**
*  MAIN THREAD
***********************************************/

PROCESS_THREAD(sensor_node_process, ev, data)
{
  // timer used to wake up the node periodically
  static struct etimer et;

  PROCESS_EXITHANDLER(broadcast_close(&broadcast));
  PROCESS_EXITHANDLER(unicast_close(&unicast));
  PROCESS_EXITHANDLER(runicast_close(&runicast));

  PROCESS_BEGIN();
  clock_init();

  // initialize all the timers
  int i;
  timer_set(&parent_timer, TIME_OUT*CLOCK_SECOND);
  timer_set(&data_timer, DATA_TIME*CLOCK_SECOND);
  for(i=0;i < MAX_CHILDREN ; i++) {
    timer_set(&(children_timer[i]), TIME_OUT*CLOCK_SECOND);
  }

  // set our id
  this_node.u8[0] = rimeaddr_node_addr.u8[0];
  this_node.u8[1] = rimeaddr_node_addr.u8[1];

  // Set up an identified best-effort broadcast connection
  broadcast_open(&broadcast, 129, &broadcast_call);
  // Set up an identified unicast connection
  unicast_open(&unicast, 136, &unicast_call);
  // Set up an identified reliable unicast connection
  runicast_open(&runicast, 144, &runicast_call);

  // Main loop
  while(1) {

    // Delay 2-4 seconds
    etimer_set(&et, CLOCK_SECOND * 6 + random_rand() % (CLOCK_SECOND * 6));
    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));

    // to be executed if the node has a parent -> otherwise we wait for a braodcast message
    if(has_parent != 0) {

      // send a broadcast message
      packetbuf_clear();
      char msg[3];
      // message contains the message identififer, the current rank and the current configuration
      sprintf(msg, "O%d%c", this_rank, config);
      packetbuf_copyfrom(msg, strlen(msg));
      broadcast_send(&broadcast);

      // send an ALIVE message via unicast to the parent node
      packetbuf_clear();
      packetbuf_copyfrom(ALIVE, strlen(ALIVE));
      unicast_send(&unicast, &parent_node);

      // check if there is some sensor data to transmit
      send_temperature(config);
      send_battery(config);
    }

    // no parent anymore
    if(timer_expired(&parent_timer)) {
      has_parent = 0;
      parent_node = rimeaddr_null;
      this_rank = INT_MAX;
    }
    // check if a child disconnected
    for(i=0;i<MAX_CHILDREN;i++){
      if(timer_expired(&(children_timer[i]))){
        children_nodes[i] = rimeaddr_null;
      }
    }
  }
  PROCESS_END();
}

