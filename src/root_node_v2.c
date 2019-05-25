#include "contiki.h"
#include "dev/leds.h"
#include <stdio.h>
#include <stdlib.h>
#include "net/rime.h"
#include "random.h"
#include <string.h>
#include "sys/timer.h"
#include "uart0.h"
#include "dev/cc2420.h"


PROCESS(root_node_process, "Root node");
AUTOSTART_PROCESSES(&root_node_process);

/********************************************//**
*  MACRO DEFINITIONS
***********************************************/

// maximum number of supported children nodes
#define MAX_CHILDREN 10
// number of retransmissions in reliable unicast
#define RETRANSMISSION 5
// duration after which a node is considered as disconnected
#define TIME_OUT 45


/********************************************//**
*  CONSTANT DEFINITIONS
***********************************************/

// message used to tell the parent node that the node is still alive
static const char ALIVE[] = "A";
// the rank of the root node
static const int rank = 0;

/********************************************//**
*  RIME ADDRESSES
***********************************************/

// list of all children nodes
static rimeaddr_t children_nodes[MAX_CHILDREN];
// reference to this node
static rimeaddr_t this_node;

/********************************************//**
*  TIMERS
***********************************************/

// a timer associated to each child node
static struct timer children_timer[MAX_CHILDREN];

/********************************************//**
*  Other global variables
***********************************************/

// the current configuration
static char config = 'P';

/********************************************//**
*  Structures for broadcast / (r)unicast
***********************************************/

static struct broadcast_conn broadcast;
static struct unicast_conn unicast;
static struct runicast_conn runicast;

/********************************************//**
*  Function definitions
***********************************************/

/**
* This function is called upon a received runicast packet. Reliable unicast
* is only used to send sensor data. Upon reception of such a packet, is has
* to be forwarded to the gateway.
* @ param  c     : the unicast structure
* @ param  from  : the address of the unicasting node
* @ return /
*/
static void runicast_recv(struct runicast_conn *c, const rimeaddr_t *from, uint8_t seqno){
  printf("%s\n", (char *) packetbuf_dataptr());
  // TODO send it to the gateway
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
  // we got a new child node
  if(strcmp(message, ALIVE) == 0) {
    // check if this node is already a child of us
    int is_child = 0;
    int i;
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

static int uart_rx_callback(unsigned char c){
  if(c == 'P') {
    config = 'P';
  }
  else if (c == 'O') {
    config = 'O';
  }
  else {
    return -1;
  }
  return 0;
}



static void broadcast_recv(struct broadcast_conn *c, const rimeaddr_t *from) {
  //printf("broadcast message received from %d.%d -> %s\n", from->u8[0], from->u8[1], (char *)packetbuf_dataptr());
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

PROCESS_THREAD(root_node_process, ev, data)
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

  // create a connection with the gateway via usb
  uart0_init(BAUD2UBR(115200));
  uart0_set_input(uart_rx_callback);


  // Main loop
  while(1) {

    // Delay 2-4 seconds
    etimer_set(&et, CLOCK_SECOND * 6 + random_rand() % (CLOCK_SECOND * 6));
    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));

    // create the broadcast message
    char msg[3];
    sprintf(msg, "O%d%c", rank, config);
    // send the broadcast message
    packetbuf_clear();
    packetbuf_copyfrom(msg, strlen(msg));
    broadcast_send(&broadcast);

    // check if a child disconnected
    for(i=0;i<MAX_CHILDREN;i++){
      if(timer_expired(&(children_timer[i]))){
        children_nodes[i] = rimeaddr_null;
      }
    }
  }

  PROCESS_END();
}
/*---------------------------------------------------------------------------*/

