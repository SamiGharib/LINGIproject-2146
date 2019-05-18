#include "contiki.h"
#include "dev/leds.h"
#include "dev/button-sensor.h"
#include <stdio.h>
#include <stdlib.h>
#include "net/rime/rime.h"
#include "random.h"
#include <string.h>
#include "sys/timer.h"

/*---------------------------------------------------------------------------*/
PROCESS(sensor_node_process, "Sensor node");
AUTOSTART_PROCESSES(&sensor_node_process);
/*---------------------------------------------------------------------------*/


// define the boolean type in C -> to make code more understandable
typedef enum { false, true } bool;
// at the beginning the sensor node has no parent
static bool has_parent = false;
// used to store the address of the parent node
static linkaddr_t parent_node;

#define MAX_CHILDREN 10
#define RETRANSMISSION_TIME 5
#define ALIVE_TIME 15
#define ALIVE_TIME_OUT 30

// list of all children nodes
static linkaddr_t children_nodes[MAX_CHILDREN];
// the address of this node
static linkaddr_t this_node;
// timer used to check if parent still alive


struct timer parent_alive;
struct timer children_alive[MAX_CHILDREN];

// predefined messages that will be exchanged between nodes
static char IS_PARENT [] = "P";
static char SEARCH_PARENT [] = "S";
static char CHILD_ALIVE [] = "C";
static char PARENT_ALIVE [] = "A";


static struct runicast_conn runicast;
static struct broadcast_conn broadcast;


// what to do when a broadcast message is received
static void broadcast_recv(struct broadcast_conn *c, const linkaddr_t *from)
{

  printf("broadcast message received from %d.%d: '%s'\n", from->u8[0], from->u8[1], (char *)packetbuf_dataptr());
  // extract message
  char *message = strtok((char *)packetbuf_dataptr()," ");
  // a node searches for a parent -> accept only if this node has already a parent
  if(strcmp(message, SEARCH_PARENT) == 0 && has_parent == true) {
    // search for an empty space
    int i;
    for(i=0; i < MAX_CHILDREN; i++) {
      if(linkaddr_cmp(&(children_nodes[i]), &linkaddr_null)) {
        // create the new child
        linkaddr_t new_child;
        new_child.u8[0] = from -> u8[0];
        new_child.u8[1] = from -> u8[1];
        children_nodes[i] = new_child;
        // Send a packet to be the parent of the broadcasting node
        packetbuf_copyfrom(IS_PARENT, sizeof(IS_PARENT));
        runicast_send(&runicast, from, RETRANSMISSION_TIME);
        // create a timer for the new child node
        timer_set(&(children_alive[i]), ALIVE_TIME_OUT*CLOCK_SECOND);
      }
    }
  }
}

// what to do when a unicast message is received
static void runicast_recv(struct runicast_conn *c, const linkaddr_t *from, uint8_t seqno)
{

  printf("unicast message received from %d.%d\n", from->u8[0], from->u8[1]);
  char *message = strtok((char *)packetbuf_dataptr()," ");

  // Another node wants to be our parent
  if(strcmp(message, IS_PARENT) == 0) {
    // if we still have no parent, accept to be this node's child
    if(has_parent == false) {
      // keep track of the parent node
      has_parent = true;
      parent_node = *from;
      // alert the parent node that we are his child now
      packetbuf_copyfrom(CHILD_ALIVE, sizeof(CHILD_ALIVE));
      runicast_send(&runicast, from, RETRANSMISSION_TIME);
      // set a keep alive timer for the parent node
      timer_set(&parent_alive, ALIVE_TIME*CLOCK_SECOND);
    }
  }

  // a keep alive message was received from children node
  if(strcmp(message, CHILD_ALIVE) == 0) {
    // reset the keep alive timer
    int i;
    for(i=0; i < MAX_CHILDREN; i++) {
      if(linkaddr_cmp(&(children_nodes[i]), from)) {
        timer_restart(&(children_alive[i]));
      }
    }
  }

  // a keep alive message was received from parent node
  if(strcmp(message, PARENT_ALIVE) == 0 && linkaddr_cmp(&parent_node, from)) {
    timer_restart(&parent_alive);
  }
}


// constructs for broadcast and unicast
static const struct runicast_callbacks runicast_call = {runicast_recv};
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


  while(1) {

    // Delay 2-4 seconds
    etimer_set(&et, CLOCK_SECOND * 4 + random_rand() % (CLOCK_SECOND * 4));
    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));

    // check if parent still alive
    if(timer_expired(&parent_alive)) {
      printf("Lost connection to parent node");
      has_parent = false;
      parent_node = linkaddr_null;
    }

    // check if children nodes still alive
   int i;
   for(i=0; i < MAX_CHILDREN; i++) {
     if(timer_expired(&(children_alive[i]))) {
       children_nodes[i] = linkaddr_null;
     }
   }

    // the node has no parent node
    if(has_parent == false) {
      // Copy from external data into the packetbuffer
      packetbuf_copyfrom(SEARCH_PARENT, sizeof(SEARCH_PARENT));
      // Send an identified best-effort broadcast packet
      broadcast_send(&broadcast);
      printf("Broadcasted No Parent packet");

    }

    // the node has a parent node
    else {
      // send data
      printf("Could send data right now");
    }


  }

  PROCESS_END();
}
/*---------------------------------------------------------------------------*/
