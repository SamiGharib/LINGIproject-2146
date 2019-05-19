#include "contiki.h"
#include "dev/leds.h"
#include "dev/button-sensor.h"
#include <stdio.h>
#include <stdlib.h>
#include "net/rime/rime.h"
#include "random.h"
#include <string.h>
#include "sys/timer.h"

PROCESS(sensor_node_process, "Sensor node");
AUTOSTART_PROCESSES(&sensor_node_process);

/* definition of macros */
// maximum number of allowed children nodes
#define MAX_CHILDREN 10
// number of retransmissions in reliable unicast
#define RETRANSMISSION 5
// periodically send keep alive messages to children and parent nodes
#define KEEP_ALIVE_TIME 15
// periodically check for time outs
#define KEEP_ALIVE_TIME_OUT 45
/*---------------------------------------------------------------------------*/

/* predefined messages that will be exchanged between nodes */
// we accept to be the parent of a node
static const char *IS_PARENT = "P";
// we search for a node to be our parent
static char *SEARCH_PARENT = "S";
// the child node is still alive
static char *CHILD_ALIVE = "C";
// the parent node is still alive
static char *PARENT_ALIVE = "A";
/*---------------------------------------------------------------------------*/

/* Timers */
// if this timer expires the parent node is not alive anymore
struct timer parent_alive;
// if this timer expires the corresponding child node is not alive anymore
struct timer children_alive[MAX_CHILDREN];
// periodically send alives messages to children and parent node
struct timer this_alive;
/*---------------------------------------------------------------------------*/

/* Rime addresses */
// used to store the address of the parent node
static linkaddr_t parent_node;
// list of all children nodes
static linkaddr_t children_nodes[MAX_CHILDREN];
// the address of this node
static linkaddr_t this_node;
/*---------------------------------------------------------------------------*/

/* Rime structures */
// structure for runicast
static struct runicast_conn runicast;
// structure for broadcast
static struct broadcast_conn broadcast;
/*---------------------------------------------------------------------------*/

/* Other global variables */
// keeps track if this node has currently a parent or not
static int has_parent = 0;
/*---------------------------------------------------------------------------*/



/*
 * Function that is called when a broadcast message is received.
 * If the send message was a SEARCH_PARENT message and we have a parent node
 * then we reply to the message with an IS_PARENT message via runicast.
 * We also add the node as a child
 */
static void broadcast_recv(struct broadcast_conn *c, const linkaddr_t *from)
{
  printf("broadcast message received from %d.%d: '%s'\n", from->u8[0], from->u8[1], (char *)packetbuf_dataptr());
  // extract message
  char *message = strtok((char *)packetbuf_dataptr()," ");
  // a node searches for a parent -> accept only if this node has already a parent
  if(strcmp(message, SEARCH_PARENT) == 0 && has_parent == 1) {
    // check if we have space for one more child node
    int i;
    for(i=0; i < MAX_CHILDREN; i++) {
      if(linkaddr_cmp(&(children_nodes[i]), &linkaddr_null) != 0) {
        // create the new child
        linkaddr_t new_child;
        new_child.u8[0] = from -> u8[0];
        new_child.u8[1] = from -> u8[1];
        children_nodes[i] = new_child;
        // Send a runicast packet to the broadcasting node
        packetbuf_copyfrom(IS_PARENT, sizeof(&IS_PARENT));
        runicast_send(&runicast, from, RETRANSMISSION);
        // create a timer for the new child node
        timer_set(&(children_alive[i]), KEEP_ALIVE_TIME_OUT*CLOCK_SECOND);
        break;
      }
    }
  }
}



/*
 * Function that is called when a runicast message is received.
 * If the send message was a IS_PARENT message we have a parent node and we can
 * start a timer for the parent node
 * If the send message was a CHILD_ALIVE message then the corresponding child
 * node is alive and we can restart the timer.
 * If the send message was a PARENT_ALIVE message then the parent node is still
 * alive and we can restert the timer.
 */
static void runicast_recv(struct runicast_conn *c, const linkaddr_t *from, uint8_t seqno)
{

  printf("unicast message received from %d.%d: '%s'\n", from->u8[0], from->u8[1], (char *)packetbuf_dataptr());
  // extract the message
  char *message = strtok((char *)packetbuf_dataptr()," ");

  // Another node wants to be our parent
  if(strcmp(message, IS_PARENT) == 0) {
    // if we still have no parent, accept to be this node's child
    if(has_parent == 0) {
      // keep track of the parent node
      has_parent = 1;
      parent_node = *from;
      // alert the parent node that we are his child now
      packetbuf_copyfrom(CHILD_ALIVE, sizeof(&CHILD_ALIVE));
      runicast_send(&runicast, from, RETRANSMISSION);
      // set a keep alive timer for the parent node
      timer_set(&parent_alive, KEEP_ALIVE_TIME_OUT*CLOCK_SECOND);
    }
  }

  // a keep alive message was received from children node
  if(strcmp(message, CHILD_ALIVE) == 0) {
    // reset the keep alive timer
    int i;
    for(i=0; i < MAX_CHILDREN; i++) {
      if(linkaddr_cmp(&(children_nodes[i]), from) != 0) {
        timer_restart(&(children_alive[i]));
      }
    }
  }

  // a keep alive message was received from parent node
  if(strcmp(message, PARENT_ALIVE) == 0 && linkaddr_cmp(&parent_node, from) != 0) {
    timer_restart(&parent_alive);
  }
}

static void
sent_runicast(struct runicast_conn *c, const linkaddr_t *to, uint8_t retransmissions)
{
  printf("runicast message sent to %d.%d, retransmissions %d\n",
	 to->u8[0], to->u8[1], retransmissions);
}
static void
timedout_runicast(struct runicast_conn *c, const linkaddr_t *to, uint8_t retransmissions)
{
  printf("runicast message timed out when sending to %d.%d, retransmissions %d\n",
	 to->u8[0], to->u8[1], retransmissions);
}

// constructs for broadcast and unicast
static const struct runicast_callbacks runicast_call = {runicast_recv, sent_runicast, timedout_runicast};
static const struct broadcast_callbacks broadcast_call = {broadcast_recv};



/*---------------------------------------------------------------------------*/
PROCESS_THREAD(sensor_node_process, ev, data)
{
  static struct etimer et;

  PROCESS_EXITHANDLER(broadcast_close(&broadcast));
  PROCESS_EXITHANDLER(runicast_close(&runicast));

  PROCESS_BEGIN();
  clock_init();

  // set the nodes rime addr
  this_node.u8[0] = linkaddr_node_addr.u8[0];
  this_node.u8[1] = linkaddr_node_addr.u8[1];

  // Set up an identified best-effort broadcast connection
  // 129 -> channel
  // &boradcast_call -> a structure with function pointers to functions that will be called when a packet has been received
  broadcast_open(&broadcast, 129, &broadcast_call);
  runicast_open(&runicast, 144, &runicast_call);

  // counter variable used in for loops
  int i;

  // set alive counter
  timer_set(&this_alive, KEEP_ALIVE_TIME*CLOCK_SECOND);

  while(1) {

    // Delay 2-4 seconds
    etimer_set(&et, CLOCK_SECOND * 4 + random_rand() % (CLOCK_SECOND * 4));
    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));

    // check if parent still alive
    if(timer_expired(&parent_alive)) {
      printf("No connection to parent node\n");
      has_parent = 0;
      parent_node = linkaddr_null;
    }

    // check if children nodes still alive
   for(i=0; i < MAX_CHILDREN; i++) {
     if(timer_expired(&(children_alive[i]))) {
       children_nodes[i] = linkaddr_null;
     }
   }

   if(timer_expired(&this_alive)) {

     // send keep alive message to parent node
     if(has_parent == 1) {
       // send keep alive message to parent node
       packetbuf_copyfrom(CHILD_ALIVE, sizeof(&CHILD_ALIVE));
       runicast_send(&runicast, &parent_node, RETRANSMISSION);
     }
      for(i=0; i < MAX_CHILDREN; i++) {
        if(linkaddr_cmp(&(children_nodes[i]), &linkaddr_null) == 0) {
          packetbuf_copyfrom(PARENT_ALIVE, sizeof(&PARENT_ALIVE));
          runicast_send(&runicast, &(children_nodes[i]), RETRANSMISSION);
        }
      }
      timer_restart(&this_alive);
    }

    // the node has no parent node
    if(has_parent == 0) {
      // Copy from external data into the packetbuffer
      packetbuf_copyfrom(SEARCH_PARENT, sizeof(&SEARCH_PARENT));
      // Send an identified best-effort broadcast packet
      broadcast_send(&broadcast);
      printf("Broadcasted No Parent packet\n");

    }
    // the node has a parent node
    else {
      // send data
      // alert the parent node that we are his child now
      //packetbuf_copyfrom("AA", 2);
      //runicast_send(&runicast, &parent_node, RETRANSMISSION);
      printf("Could send data right now\n");
    }


  }

  PROCESS_END();
}
/*---------------------------------------------------------------------------*/
