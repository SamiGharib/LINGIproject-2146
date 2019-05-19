#include "contiki.h"
#include "dev/leds.h"
#include "dev/button-sensor.h"
#include <stdio.h>
#include <stdlib.h>
#include "net/rime/rime.h"
#include "random.h"
#include <string.h>
#include "sys/timer.h"

PROCESS(root_node_process, "Root node");
AUTOSTART_PROCESSES(&root_node_process);

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
// if this timer expires the corresponding child node is not alive anymore
struct timer children_alive[MAX_CHILDREN];
// periodically send alives messages to children and parent node
struct timer this_alive;
/*---------------------------------------------------------------------------*/

/* Rime addresses */
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

// what to do when a broadcast message is received
static void broadcast_recv(struct broadcast_conn *c, const linkaddr_t *from)
{

  printf("broadcast message received from %d.%d: '%s'\n", from->u8[0], from->u8[1], (char *)packetbuf_dataptr());
  // extract message
  char *message = strtok((char *)packetbuf_dataptr()," ");
  printf("The message is: %s\n", message);

  // a node searches for a parent
  if(strcmp(message, SEARCH_PARENT) == 0) {
    // search for an empty space
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
        // need to break here
        break;
      }
    }
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

// what to do when a unicast message is received
static void runicast_recv(struct runicast_conn *c, const linkaddr_t *from, uint8_t seqno)
{

  printf("unicast message received from %d.%d: '%s'\n", from->u8[0], from->u8[1], (char *)packetbuf_dataptr());
  // extract message
  char *message = strtok((char *)packetbuf_dataptr()," ");

  // a keep alive message was received from a children node
  if(strcmp(message, CHILD_ALIVE) == 0) {
    // reset the keep alive timer
    int i;
    for(i=0; i < MAX_CHILDREN; i++) {
      if(linkaddr_cmp(&(children_nodes[i]), from) != 0) {
        timer_restart(&(children_alive[i]));
      }
    }
  }
}

// constructs for broadcast and unicast
static const struct runicast_callbacks runicast_call = {runicast_recv, sent_runicast, timedout_runicast};
static const struct broadcast_callbacks broadcast_call = {broadcast_recv};

/*---------------------------------------------------------------------------*/
PROCESS_THREAD(root_node_process, ev, data)
{
  static struct etimer et;

  PROCESS_EXITHANDLER(broadcast_close(&broadcast));
  PROCESS_EXITHANDLER(runicast_close(&runicast));

  PROCESS_BEGIN();
  clock_init();

  timer_set(&this_alive, KEEP_ALIVE_TIME*CLOCK_SECOND);

  // set the nodes rime addr
  this_node.u8[0] = linkaddr_node_addr.u8[0];
  this_node.u8[1] = linkaddr_node_addr.u8[1];

  // counter variable used in for loops
  int i;

  // Set up an identified best-effort broadcast connection
  broadcast_open(&broadcast, 129, &broadcast_call);
  // Set up an identified reliable unicast connection
  runicast_open(&runicast, 144, &runicast_call);

  while(1) {

    etimer_set(&et, CLOCK_SECOND * 4 + random_rand() % (CLOCK_SECOND*4));
    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));

    if(timer_expired(&this_alive)) {
      // check if children nodes still alive
      for(i=0; i < MAX_CHILDREN; i++) {
        // send a parent alive message to each children
        if(linkaddr_cmp(&(children_nodes[i]), &linkaddr_null) == 0) {
          packetbuf_copyfrom(PARENT_ALIVE, sizeof(&PARENT_ALIVE));
          runicast_send(&runicast, &(children_nodes[i]), RETRANSMISSION);
        }
      }
      timer_restart(&this_alive);
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
