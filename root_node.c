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
PROCESS(root_node_process, "Root node");
AUTOSTART_PROCESSES(&root_node_process);
/*---------------------------------------------------------------------------*/

#define MAX_CHILDREN 10
#define RETRANSMISSION_TIME 5
#define ALIVE_TIME 15
#define ALIVE_TIME_OUT 30


// predefined messages that will be exchanged between nodes
static const char *IS_PARENT = "P";
static char *SEARCH_PARENT = "S";
static char *CHILD_ALIVE = "C";
static char *PARENT_ALIVE = "A";

// define the boolean type in C -> to make code more understandable
typedef enum { false, true } bool;

// a timer for each children of the parent node
struct timer children_alive[MAX_CHILDREN];

static linkaddr_t children_nodes[MAX_CHILDREN];

static linkaddr_t this_node;

// a timer to send alive messages to each children
struct timer alive;

// we need reliable unicast and broadcast
static struct runicast_conn runicast;
static struct broadcast_conn broadcast;

// what to do when a broadcast message is received
static void broadcast_recv(struct broadcast_conn *c, const linkaddr_t *from)
{
  // extract message
  char *message = strtok((char *)packetbuf_dataptr()," ");
  // a node searches for a parent
  if(strcmp(message, SEARCH_PARENT) == 0) {

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
        packetbuf_copyfrom(IS_PARENT, sizeof(&IS_PARENT));
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
  char *message = strtok((char *)packetbuf_dataptr()," ");

  // a keep alive message was received from a children node
  if(strcmp(message, CHILD_ALIVE) == 0) {
    // reset the keep alive timer
    int i;
    for(i=0; i < MAX_CHILDREN; i++) {
      if(linkaddr_cmp(&(children_nodes[i]), from)) {
        timer_restart(&(children_alive[i]));
      }
    }
  }
}

// constructs for broadcast and unicast
static const struct runicast_callbacks runicast_call = {runicast_recv};
static const struct broadcast_callbacks broadcast_call = {broadcast_recv};

/*---------------------------------------------------------------------------*/
PROCESS_THREAD(root_node_process, ev, data)
{
  static struct etimer et;

  PROCESS_EXITHANDLER(broadcast_close(&broadcast));
  PROCESS_EXITHANDLER(runicast_close(&runicast));

  PROCESS_BEGIN();
  clock_init();

  timer_set(&alive, ALIVE_TIME*CLOCK_SECOND);

  // set the nodes rime addr
  this_node.u8[0] = linkaddr_node_addr.u8[0];
  this_node.u8[1] = linkaddr_node_addr.u8[1];

  // Set up an identified best-effort broadcast connection
  broadcast_open(&broadcast, 129, &broadcast_call);
  // Set up an identified reliable unicast connection
  runicast_open(&runicast, 144, &runicast_call);

  while(1) {

    etimer_set(&et, CLOCK_SECOND * 4 + random_rand() % (CLOCK_SECOND*4));
    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));

    if(timer_expired(&alive)) {
      // check if children nodes still alive
      int i;
      for(i=0; i < MAX_CHILDREN; i++) {
        // send a parent alive message to each children
        if(linkaddr_cmp(&(children_nodes[i]), &linkaddr_null) != 0) {
          packetbuf_copyfrom(PARENT_ALIVE, sizeof(&PARENT_ALIVE));
          runicast_send(&runicast, &children_nodes[i], RETRANSMISSION_TIME);
        }
      }
    }

    // check if children nodes still alive
    int i;
    for(i=0; i < MAX_CHILDREN; i++) {
      if(timer_expired(&(children_alive[i]))) {
        children_nodes[i] = linkaddr_null;
      }
    }

  }
  PROCESS_END();
}
