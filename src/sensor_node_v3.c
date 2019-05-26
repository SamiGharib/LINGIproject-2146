#include "contiki.h"
#include "dev/leds.h"
#include "dev/button-sensor.h"
#include <stdio.h>
#include <stdlib.h>
#include "net/rime/rime.h"
#include "random.h"
#include <string.h>
#include "sys/timer.h"
#include <limits.h>
#include "dev/temperature-sensor.h"
#include "dev/battery-sensor.h"

PROCESS(sensor_node_process, "Sensor node");
AUTOSTART_PROCESSES(&sensor_node_process);

/********************************************//*
*  MACRO DEFINITIONS
***********************************************/

#define MIN_INDEX 0
#define MAX_INDEX 10
// number of retransmissions in reliable unicast
#define RETRANSMISSION 5
// duration after which a node is considered as disconnected
#define TIME_OUT 45
// duration after which the node sends data -> when config = periodically
#define DATA_TIME 30

#define NUM_HISTORY_ENTRIES 10

#define DEBUG DEBUG_FULL

/********************************************//**
*  RIME ADDRESSES
***********************************************/

// array of all the child nodes
static linkaddr_t children_nodes[MAX_INDEX][MAX_INDEX];
// reference to the parent node
static linkaddr_t parent_node;
// reference to this node
static linkaddr_t this_node;


/********************************************//**
*  TIMERS
***********************************************/

// a timer associated to each child node
static struct timer children_timer[MAX_INDEX][MAX_INDEX];
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
static int bat_subscriber = 0;

static char alive_msg[500];
static char battery_msg[10];
static char temp_msg[10];
static char broadcast_msg[4];
static char tmp[5];



struct history_entry {
  struct history_entry *next;
  linkaddr_t addr;
  uint8_t seq;
};
LIST(history_table);
MEMB(history_mem, struct history_entry, NUM_HISTORY_ENTRIES);

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

    int x = battery_sensor.value(0);

    // configuration is set to send periodically
    if(config == 'P') {
      // only send if the timer expired
      if(timer_expired(&data_timer)) {
        // create message with format <ID/channel/data>
        sprintf(battery_msg, "%d.%d/B/%d", this_node.u8[0], this_node.u8[1], x);
        // send message via runicast to parent node
        packetbuf_clear();
        packetbuf_copyfrom(battery_msg, strlen(battery_msg));
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
        sprintf(battery_msg, "%d.%d/B/%d", this_node.u8[0], this_node.u8[1], x);
        // send message via runicast to parent node
        packetbuf_clear();
        packetbuf_copyfrom(battery_msg, strlen(battery_msg));
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
        if(!runicast_is_transmitting(&runicast)) {

          sprintf(temp_msg, "%d.%d/T/%d.%d", this_node.u8[0], this_node.u8[1], first_digit, second_digit);
          // send message via runicast to parent node
          packetbuf_clear();
          packetbuf_copyfrom(temp_msg, strlen(temp_msg));
          runicast_send(&runicast, &parent_node, RETRANSMISSION);
          timer_restart(&data_timer);
        }
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
        sprintf(temp_msg, "%d.%d/T/%d.%d", linkaddr_node_addr.u8[0], linkaddr_node_addr.u8[1], first_digit, second_digit);
        // send message via runicast to parent node
        packetbuf_clear();
        packetbuf_copyfrom(temp_msg, strlen(temp_msg));
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
static void broadcast_recv(struct broadcast_conn *c, const linkaddr_t *from) {
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
      linkaddr_t new_node;
      new_node.u8[0] = from -> u8[0];
      new_node.u8[1] = from -> u8[1];
      parent_node = new_node;
      this_rank = rank+1;
      timer_restart(&parent_timer);
    }
    // the message was sent from our parent node -> restart timer
    else if (has_parent == 1 && linkaddr_cmp(&parent_node, from) != 0) {
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
static void unicast_recv(struct unicast_conn *c, const linkaddr_t *from)
{
  //printf("unicast message received from %d.%d: '%s'\n", from->u8[0], from->u8[1], (char *)packetbuf_dataptr());

  // get the indices of the sending node
  int index1 = from->u8[0];
  int index2 = from->u8[1];

  // no valid address
  if(index1 < MIN_INDEX || index1 > MAX_INDEX || index2 < MIN_INDEX || index2 > MAX_INDEX) {
    return;
  }
  // extract the message
  char *message = (char *)packetbuf_dataptr();
  // used as counter variable in for loops
  const char delim[2] = "/";
  char *token = strtok(message, delim);

  // we received an ALIVE message
  if(strcmp(token, "A") == 0) {

    // get the address of the sending node
    linkaddr_t child_node;
    child_node.u8[0] = from -> u8[0];
    child_node.u8[1] = from -> u8[1];
    // check if this node is already a child of us
    if(linkaddr_cmp(&(children_nodes[index1][index2]), &linkaddr_null) != 0) {
      // the node is not a child node yet
      // create the new child
      children_nodes[index1][index2] = child_node;
    }
    // reset the timer for the new child node
    timer_restart(&(children_timer[index1][index2]));
    // set up the outgoing links for the children nodes of the new child

    token = strtok(NULL, delim);
    while(token != NULL) {
      // we can reach the child nodes of the new node
      index1 = token[0] - '0';
      index2 = token[2] - '0';
      children_nodes[index1][index2] = child_node;
      timer_restart(&(children_timer[index1][index2]));
      token = strtok(NULL, delim);
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
static void runicast_recv(struct runicast_conn *c, const linkaddr_t *from, uint8_t seqno){

  struct history_entry *e = NULL;
  for(e = list_head(history_table); e != NULL; e = e->next) {
    if(linkaddr_cmp(&e->addr, from)) {
      break;
    }
  }
  if(e == NULL) {
    /* Create new history entry */
    e = memb_alloc(&history_mem);
    if(e == NULL) {
      e = list_chop(history_table); /* Remove oldest at full history */
    }
    linkaddr_copy(&e->addr, from);
    e->seq = seqno;
    list_push(history_table, e);
  } else {
    /* Detect duplicate callback */
    if(e->seq == seqno) {
      return;
    }
    /* Update existing history entry */
    e->seq = seqno;
  }

  // extract the message
  char *message = (char *)packetbuf_dataptr();

  if(message[0] == 'F') {

    int index1 = message[1] - '0'; // msg[1] - '0'?
    int index2 = message[3] - '0';

    if(this_node.u8[0] == index1 && this_node.u8[1] == index2) {
      if(message[5] == 'B') {
        if(message[7] == '0') {
          bat_subscriber = 0;
        }
        else if(message[7] == '1') {
          bat_subscriber = 1;
        }
      }
      else if(message[5] == 'T') {
        if(message[7] == '0') {
          temp_subscriber = 0;
        }
        else if(message[7] == '1') {
          temp_subscriber = 1;
        }
      }
    }
    else {
      packetbuf_clear();
      packetbuf_copyfrom(message, strlen(message));
      runicast_send(&runicast, &children_nodes[index1][index2], RETRANSMISSION);
    }
  }
  else {
    // TODO: aggregate the packets and send them when we send our sensor data
    packetbuf_clear();
    packetbuf_copyfrom(message, strlen(message));
    runicast_send(&runicast, &parent_node, RETRANSMISSION);
  }
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
    SENSORS_ACTIVATE(battery_sensor);

    // initialize all the timers
    int i, j;
    timer_set(&parent_timer, TIME_OUT*CLOCK_SECOND);
    timer_set(&data_timer, DATA_TIME*CLOCK_SECOND);
    for(i=0;i < MAX_INDEX ; i++) {
      for(j=0; j < MAX_INDEX; j++)
      timer_set(&(children_timer[i][j]), TIME_OUT*CLOCK_SECOND);
    }

    // set our id
    this_node.u8[0] = linkaddr_node_addr.u8[0];
    this_node.u8[1] = linkaddr_node_addr.u8[1];

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
        // message contains the message identififer, the current rank and the current configuration
        sprintf(broadcast_msg, "O%d%c", this_rank, config);
        packetbuf_copyfrom(broadcast_msg, strlen(broadcast_msg));
        broadcast_send(&broadcast);

        memset(alive_msg, 0, 500);
        strcat(alive_msg, "A/");
        for(i=0;i<MAX_INDEX;i++){
          for(j=0;j<MAX_INDEX;j++) {
            // append all the nodes accessible via this node
            if(linkaddr_cmp(&(children_nodes[i][j]), &linkaddr_null) == 0) {
              tmp[0] = i + '0';
              tmp[1] = '.';
              tmp[2] = j + '0';
              tmp[3] = '/';
              strcat(alive_msg, tmp);
            }
          }
        }

        //printf("send alive message: %s\n", alive_msg);
        packetbuf_clear();
        packetbuf_copyfrom(alive_msg, strlen(alive_msg));
        unicast_send(&unicast, &parent_node);
        // check if there is some sensor data to transmit
        send_temperature(config);
        send_battery(config);
      }

      // no parent anymore
      if(timer_expired(&parent_timer)) {
        printf("LOST CONNECTION TO PARENT");
        has_parent = 0;
        parent_node = linkaddr_null;
        this_rank = INT_MAX;
      }
      // check if a child disconnected
      for(i=0;i<MAX_INDEX;i++){
        for(j=0;j<MAX_INDEX;j++) {
          // remove the child node
          if(timer_expired(&(children_timer[i][j])) && linkaddr_cmp(&(children_nodes[i][j]), &linkaddr_null) == 0){
            children_nodes[i][j] = linkaddr_null;
          }
        }
      }
    }
    PROCESS_END();
  }

