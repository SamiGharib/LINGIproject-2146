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
// duration after which a node is considered as disconnected
#define TIME_OUT 45
/*---------------------------------------------------------------------------*/


/* predefined messages that will be exchanged between nodes */
// unicast message -> inform the parent node that we are still alive
static char DAO[] = "A";
/*---------------------------------------------------------------------------*/

/* Rime addresses */
// list of all children nodes
static linkaddr_t children_nodes[MAX_CHILDREN];
/*---------------------------------------------------------------------------*/

/* Timers */
// a timer associated to each child
struct timer children_timer[MAX_CHILDREN];
/*---------------------------------------------------------------------------*/

static const int rank = 0;
static char config = 'P';

static struct broadcast_conn broadcast;
static struct unicast_conn unicast;
static struct runicast_conn runicast;


static void runicast_recv(struct runicast_conn *c, const linkaddr_t *from, uint8_t seqno){
  printf("runicast message received from %d.%d -> %s\n", from->u8[0], from->u8[1], (char *) packetbuf_dataptr());

  // TODO send it to the gateway
}

static void unicast_recv(struct unicast_conn *c, const linkaddr_t *from)
{
  printf("unicast message received from %d.%d: '%s'\n", from->u8[0], from->u8[1], (char *)packetbuf_dataptr());
  // extract the message
  char *message = (char *)packetbuf_dataptr();
  // we got a new child node

  if(strcmp(message, DAO) == 0) {

    // check if this node is already a child of us
    int is_child = 0;
    int i;

    for(i=0; i < MAX_CHILDREN; i++) {
      if(linkaddr_cmp(&(children_nodes[i]), from) != 0) {
        is_child = 1;
        //printf("restarted timer for existing child: %d\n", i);
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
          //printf("created new child node: %d\n", i);
          timer_restart(&(children_timer[i]));
          break;
        }
      }
    }
  }
}


static void broadcast_recv(struct broadcast_conn *c, const linkaddr_t *from) {
  //printf("broadcast message received from %d.%d -> %s\n", from->u8[0], from->u8[1], (char *)packetbuf_dataptr());
}


// constructs for broadcast and unicast
static const struct unicast_callbacks unicast_call = {unicast_recv};
static const struct broadcast_callbacks broadcast_call = {broadcast_recv};
static const struct runicast_callbacks runicast_call = {runicast_recv};

/*---------------------------------------------------------------------------*/
PROCESS_THREAD(root_node_process, ev, data)
{
  static struct etimer et;


  PROCESS_EXITHANDLER(broadcast_close(&broadcast));
  PROCESS_EXITHANDLER(unicast_close(&unicast));
  PROCESS_EXITHANDLER(runicast_close(&runicast));


  PROCESS_BEGIN();
  clock_init();

  // for loop counter
  int i;
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

    char msg[3];
    sprintf(msg, "O%d%c", rank, config);
    // send a DIO broadcast message
    packetbuf_clear();
    packetbuf_copyfrom(msg, strlen(msg));
    broadcast_send(&broadcast);

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
