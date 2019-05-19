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


PROCESS(sensor_node_process, "Sensor node");
AUTOSTART_PROCESSES(&sensor_node_process);

/* definition of macros */
// maximum number of allowed children nodes
#define MAX_CHILDREN 10
// number of retransmissions in reliable unicast
#define RETRANSMISSION 5
// duration after which a node is considered as disconnected
#define TIME_OUT 45
// time to wait before sending new sensor data
#define DATA_TIME 30
/*---------------------------------------------------------------------------*/


/* predefined messages that will be exchanged between nodes */
// unicast message -> inform the parent node that we are still alive
static char DAO[] = "A";
/*---------------------------------------------------------------------------*/

/* Rime addresses */
// list of all children nodes
static linkaddr_t children_nodes[MAX_CHILDREN];
static linkaddr_t parent_node;
/*---------------------------------------------------------------------------*/

/* Timers */
// a timer associated to each child
static struct timer children_timer[MAX_CHILDREN];
// a timer associated to the parent node
static struct timer parent_timer;
// a timer associated to the transmission of data
static struct timer data_timer;
/*---------------------------------------------------------------------------*/

/* Other global variables */
// check if we have a parent node currently
static int has_parent = 0;
// the rank of this node
static int this_rank = INT_MAX;
// by default we send data periodically
static char config = 'C';

static int prev_temp[2] = {1 , 1};


/* Structures for broadcast / unicast */
static struct broadcast_conn broadcast;
static struct unicast_conn unicast;
static struct runicast_conn runicast;

static void send_temperature(char config) {

  // get temperature value
  unsigned int temp = temperature_sensor.value(0);
  int first_digit = temp/10;
  int second_digit = temp - (temp/10)*10;

  // configuration -> send periodically
  if(config == 'P') {
    if(timer_expired(&data_timer)) {
      // create message
      char msg[9];
      sprintf(msg, "%d.%d/T/%d.%d", linkaddr_node_addr.u8[0], linkaddr_node_addr.u8[1], first_digit, second_digit);
      // send message via runicast
      packetbuf_clear();
      packetbuf_copyfrom(msg, strlen(msg));
      runicast_send(&runicast, &parent_node, RETRANSMISSION);
    }
  }
  // configuration -> send on change
  else {
    // send only if temperature changed
    if(first_digit != prev_temp[0] || second_digit != prev_temp[1]) {
      prev_temp[0] = first_digit;
      prev_temp[1] = second_digit;
      // create message
      char msg[9];
      sprintf(msg, "%d.%d/T/%d.%d", linkaddr_node_addr.u8[0], linkaddr_node_addr.u8[1], first_digit, second_digit);
      // send message via runicast
      packetbuf_clear();
      packetbuf_copyfrom(msg, strlen(msg));
      runicast_send(&runicast, &parent_node, RETRANSMISSION);
    }

  }

}


static void broadcast_recv(struct broadcast_conn *c, const linkaddr_t *from) {
  //printf("broadcast message received from %d.%d -> %s\n", from->u8[0], from->u8[1], (char *)packetbuf_dataptr());
  // this node has no parent yet
  char *message = (char *)packetbuf_dataptr();
  // we received a DIO message
  if(message[0] == 'O') {

    int rank = atoi(&message[1]);
    config = message[2];
    // shortest hop rule
    if(rank+1 < this_rank) {
      has_parent = 1;
      linkaddr_t new_node;
      new_node.u8[0] = from -> u8[0];
      new_node.u8[1] = from -> u8[1];
      parent_node = new_node;
      this_rank = rank+1;
      timer_restart(&parent_timer);
    }

    else if (has_parent == 1 && linkaddr_cmp(&parent_node, from) != 0) {
      timer_restart(&parent_timer);
    }
  }

}

static void unicast_recv(struct unicast_conn *c, const linkaddr_t *from)
{
  //printf("unicast message received from %d.%d: '%s'\n", from->u8[0], from->u8[1], (char *)packetbuf_dataptr());
  // extract the message
  char *message = (char *)packetbuf_dataptr();
  int i;

  // we received a DAO message
  if(strcmp(message, DAO) == 0) {

    // check if this node is already a child of us
    int is_child = 0;
    for(i=0; i < MAX_CHILDREN; i++) {
      if(linkaddr_cmp(&(children_nodes[i]), from) != 0) {
        is_child = 1;
        timer_restart(&(children_timer[i]));
        break;
      }
    }

    // else this becomes a new child
    if(is_child == 0) {

      for(i=0; i < MAX_CHILDREN; i++) {
        if(linkaddr_cmp(&(children_nodes[i]), &linkaddr_null) != 0) {
          // create the new child
          linkaddr_t new_node;
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

static void runicast_recv(struct runicast_conn *c, const linkaddr_t *from, uint8_t seqno){
  //printf("runicast message received from %d.%d -> %s\n", from->u8[0], from->u8[1], (char *) packetbuf_dataptr());

  char *message = (char *)packetbuf_dataptr();

  packetbuf_clear();
  packetbuf_copyfrom(message, strlen(message));
  runicast_send(&runicast, &parent_node, RETRANSMISSION);
}

// constructs for broadcast and unicast
static const struct unicast_callbacks unicast_call = {unicast_recv};
static const struct broadcast_callbacks broadcast_call = {broadcast_recv};
static const struct runicast_callbacks runicast_call = {runicast_recv};
/*---------------------------------------------------------------------------*/

PROCESS_THREAD(sensor_node_process, ev, data)
{
  static struct etimer et;

  PROCESS_EXITHANDLER(broadcast_close(&broadcast));
  PROCESS_EXITHANDLER(unicast_close(&unicast));
  PROCESS_EXITHANDLER(runicast_close(&runicast));

  PROCESS_BEGIN();
  clock_init();

  // for loop counter
  int i;
  timer_set(&parent_timer, TIME_OUT*CLOCK_SECOND);
  timer_set(&data_timer, DATA_TIME*CLOCK_SECOND);
  for(i=0;i < MAX_CHILDREN ; i++) {
    timer_set(&(children_timer[i]), TIME_OUT*CLOCK_SECOND);
  }

  // Set up an identified best-effort broadcast connection
  broadcast_open(&broadcast, 129, &broadcast_call);
  // Set up an identified reliable unicast connection
  unicast_open(&unicast, 136, &unicast_call);
  runicast_open(&runicast, 144, &runicast_call);

  // Main loop
  while(1) {

    // Delay 2-4 seconds
    etimer_set(&et, CLOCK_SECOND * 6 + random_rand() % (CLOCK_SECOND * 6));
    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));

    if(has_parent == 0) {
      printf("This node has no parent\n");
    }
    else {
      // send a DIO broadcast message
      packetbuf_clear();
      char msg[3];
      sprintf(msg, "O%d%c", this_rank, config);
      packetbuf_copyfrom(msg, strlen(msg));
      broadcast_send(&broadcast);
      // send a DIA unicast message to parent node
      packetbuf_clear();
      packetbuf_copyfrom(DAO, strlen(DAO));
      unicast_send(&unicast, &parent_node);

      // check if there is some data to transmit

      send_temperature(config);
    }


    // no parent anymore
    if(timer_expired(&parent_timer)) {
      has_parent = 0;
      parent_node = linkaddr_null;
      this_rank = INT_MAX;
    }
    // check if a child disconnected
    for(i=0;i<MAX_CHILDREN;i++){
      if(timer_expired(&(children_timer[i]))){
        children_nodes[i] = linkaddr_null;
      }
    }

  }

  PROCESS_END();
}
/*---------------------------------------------------------------------------*/
