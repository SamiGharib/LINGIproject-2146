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
#define MIN_INDEX 0
#define MAX_INDEX 10
// number of retransmissions in reliable unicast
#define RETRANSMISSION 5
// duration after which a node is considered as disconnected
#define TIME_OUT 45

#define NUM_HISTORY_ENTRIES 10


static char gateway_msg[9];
static char broadcast_msg[4];

static int counter = 1;


struct history_entry {
  struct history_entry *next;
  rimeaddr_t addr;
  uint8_t seq;
};
LIST(history_table);
MEMB(history_mem, struct history_entry, NUM_HISTORY_ENTRIES);


/********************************************//**
*  CONSTANT DEFINITIONS
***********************************************/
// the rank of the root node
static const int rank = 0;

/********************************************//**
*  RIME ADDRESSES
***********************************************/

// list of all children nodes
static rimeaddr_t children_nodes[MAX_INDEX][MAX_INDEX];
// reference to this node
static rimeaddr_t this_node;

/********************************************//**
*  TIMERS
***********************************************/

// a timer associated to each child node
static struct timer children_timer[MAX_INDEX][MAX_INDEX];


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

static int index1 = 0;
static int index2 = 0;

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

  struct history_entry *e = NULL;
  for(e = list_head(history_table); e != NULL; e = e->next) {
    if(rimeaddr_cmp(&e->addr, from)) {
      break;
    }
  }
  if(e == NULL) {
    /* Create new history entry */
    e = memb_alloc(&history_mem);
    if(e == NULL) {
      e = list_chop(history_table); /* Remove oldest at full history */
    }
    rimeaddr_copy(&e->addr, from);
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

  printf("%s\n", (char *) packetbuf_dataptr());
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
  // get the indices of the sending node
  int index1 = from-> u8[0];
  int index2 = from-> u8[1];
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
    rimeaddr_t child_node;
    child_node.u8[0] = from -> u8[0];
    child_node.u8[1] = from -> u8[1];
    // check if this node is already a child of us
    if(rimeaddr_cmp(&(children_nodes[index1][index2]), &rimeaddr_null) != 0) {
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
      token = strtok(NULL, delim);
      timer_restart(&(children_timer[index1][index2]));
    }
  }
}

static int uart_rx_callback(unsigned char c){
  //printf("receivedfromgateway/=/-%c\n",c);
    // start of the message
    if(counter == 1) {
      if(c == 'P') {
        config = 'P';
      }
      else if (c == 'O') {
        config = 'O';
      }
      else{
	gateway_msg[counter] = c;
	index1 = c - '0';
	counter++;
      }
     }
    // character is a digit
     else{
        gateway_msg[counter] = c;
	if(counter == 3){
	 index2 = c - '0';
	}
	counter++;
	if(counter == 8){
	 // send the message to the node
       	 packetbuf_clear();
       	 packetbuf_copyfrom(gateway_msg, strlen(gateway_msg));
	 //printf("msg_gateway=%s\n",gateway_msg);
       	 //printf("[Remove if OK]/message/ send to %d.%d via interface: %d.%d\n", index1, index2, children_nodes[index1][index2].u8[0], children_nodes[index1][index2].u8[1]);
      	runicast_send(&runicast, &children_nodes[index1][index2], RETRANSMISSION);
	counter = 1;
	}
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
  int i, j;
  for(i=0;i < MAX_INDEX ; i++) {
    for(j=0; j < MAX_INDEX; j++)
    timer_set(&(children_timer[i][j]), TIME_OUT*CLOCK_SECOND);
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

  gateway_msg[0] = 'F';


  // create a connection with the gateway via usb
  uart0_init(BAUD2UBR(115200));
  uart0_set_input(uart_rx_callback);


  // Main loop
  while(1) {

    // Delay 2-4 seconds
    etimer_set(&et, CLOCK_SECOND * 6 + random_rand() % (CLOCK_SECOND * 6));
    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));

    // create the broadcast message
    sprintf(broadcast_msg, "O%d%c", rank, config);
    // send the broadcast message
    packetbuf_clear();
    packetbuf_copyfrom(broadcast_msg, strlen(broadcast_msg));
    broadcast_send(&broadcast);
   

    // check if a child disconnected
    for(i=0;i<MAX_INDEX;i++){
      for(j=0;j<MAX_INDEX;j++) {
        // remove the child node
        if(timer_expired(&(children_timer[i][j])) && rimeaddr_cmp(&(children_nodes[i][j]), &rimeaddr_null) == 0){
          children_nodes[i][j] = rimeaddr_null;
          }
        }
      }
  }

  PROCESS_END();
}
/*---------------------------------------------------------------------------*/
