#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include "emulator.h"
#include "gbn.h"

/* ******************************************************************
   Selective Repeat protocol.  Based on code given for Go Back N protocol, adapted from J.F.Kurose
   ALTERNATING BIT AND GO-BACK-N NETWORK EMULATOR: VERSION 1.2

   Network properties:
   - one way network delay averages five time units (longer if there
   are other messages in the channel for GBN), but can be larger
   - packets can be corrupted (either the header or the data portion)
   or lost, according to user-defined probabilities
   - packets will be delivered in the order in which they were sent
   (although some can be lost).

   Modifications:
   - removed bidirectional GBN code and other code not used by prac.
   - fixed C style to adhere to current programming style
   - added GBN implementation
**********************************************************************/

#define RTT  16.0       /* round trip time.  MUST BE SET TO 16.0 when submitting assignment */
#define WINDOWSIZE 6    /* the maximum number of buffered unacked packet
                          MUST BE SET TO 6 when submitting assignment */
#define SEQSPACE 13      /* the min sequence space for SR must be at least windowsize * 2 */
#define NOTINUSE (-1)   /* used to fill header fields that are not being used */

/* generic procedure to compute the checksum of a packet.  Used by both sender and receiver
   the simulator will overwrite part of your packet with 'z's.  It will not overwrite your
   original checksum.  This procedure must generate a different checksum to the original if
   the packet is corrupted.
*/
int ComputeChecksum(struct pkt packet)
{
  int checksum = 0;
  int i;

  checksum = packet.seqnum;
  checksum += packet.acknum;
  for ( i=0; i<20; i++ )
    checksum += (int)(packet.payload[i]);

  return checksum;
}

bool IsCorrupted(struct pkt packet)
{
  if (packet.checksum == ComputeChecksum(packet))
    return (false);
  else
    return (true);
}


/********* Sender (A) variables and functions ************/

static struct pkt buffer[SEQSPACE];  /* array for storing packets waiting for ACK. Since ACKs are non-cumulative
                                      in SR, it will make things much easier if we can index the buffer with 
                                      sequence numbers */
static int oldestUnacked;                /* seqnum of the oldest unACKED packet */
static int A_nextseqnum;               /* the next sequence number to be used by the sender */
static bool acked[SEQSPACE]; /* array to track which packets have been ACKed (1 means ACKed, 0 means not) */
static bool sent[SEQSPACE]; /* array to track which packets have been sent*/
static bool timer_running; /* records whether timer is currently running */ 

/* called from layer 5 (application layer), passed the message to be sent to other side */
void A_output(struct msg message)
{
  struct pkt sendpkt;
  int i;

  /* if not blocked waiting on ACK. 
  For GBN this was "windowcount < WINDOWSIZE". windowcount no longer exists - we need to express
  the number of packets in the window in terms of other variables instead*/
  if ( ((A_nextseqnum - oldestUnacked + SEQSPACE) % SEQSPACE) < WINDOWSIZE) { // modulo allows seqnums to wrap
    if (TRACE > 1)
      printf("----A: New message arrives, send window is not full, send new messge to layer3!\n");

    /* create packet */
    sendpkt.seqnum = A_nextseqnum;
    sendpkt.acknum = NOTINUSE;
    for ( i=0; i<20 ; i++ )
      sendpkt.payload[i] = message.data[i];
    sendpkt.checksum = ComputeChecksum(sendpkt);

    /* put packet in window buffer */
    buffer[sendpkt.seqnum] = sendpkt;

    /* mark  packet as sent */
    sent[A_nextseqnum] = true

    /* send out packet */
    if (TRACE > 0)
      printf("Sending packet %d to layer 3\n", sendpkt.seqnum);
    tolayer3 (A, sendpkt);

    /* start timer if first packet in window */
    if (!timer_running)
      starttimer(A, RTT);
      timer_running = true;

    /* get next sequence number, wrap back to 0 */
    A_nextseqnum = (A_nextseqnum + 1) % SEQSPACE;
  }
  /* if blocked,  window is full */
  else {
    if (TRACE > 0)
      printf("----A: New message arrives, send window is full\n");
    window_full++;
  }
}


/* called from layer 3, when a packet arrives for layer 4
   In this practical this will always be an ACK as B never sends data.
*/
void A_input(struct pkt packet)
{
  int ackcount = 0;
  int i;

  /* if received ACK is not corrupted */
  if (!IsCorrupted(packet)) {
    if (TRACE > 0)
      printf("----A: uncorrupted ACK %d is received\n",packet.acknum);
    total_ACKs_received++;

    /* check if new ACK or duplicate */
    if (!acked[packet.acknum]) {
      /* packet is a new ACK */
      acked[packet.acknum] = true;

      if (TRACE > 0)
        printf("----A: ACK %d is not a duplicate\n",packet.acknum);
      new_ACKs++;

      /* update the oldest unACKed packet if necessary*/
      while (acked[oldestUnacked]){
        oldestUnacked = (oldestUnacked + 1) % SEQSPACE;

        /*stop timer*/
        stoptimer(A);
        timer_running = false;

        /*Unless we have reached the end of the received packets, restart timer for next oldest unACKed packet*/
        if (oldestUnacked != A_nextseqnum){
          starttimer(A, RTT);
          timer_running = true;
        }
        
      }
    }
    else
      if (TRACE > 0)
          printf ("----A: duplicate ACK received, do nothing!\n");
  }
  else
    if (TRACE > 0)
      printf ("----A: corrupted ACK is received, do nothing!\n");
}

/* called when A's timer goes off */
void A_timerinterrupt(void)
{
  int i;

  if (TRACE > 0)
    printf("----A: time out,resend packets!\n");

  for(i=0; i<WINDOWSIZE; i++) {
    int seq = (oldestUnacked + i) % SEQSPACE; /* seqnums to loop over*/

    if (TRACE > 0)
      printf ("---A: resending packet %d\n", seq;

    /* Resend packets that have already been sent but have not been ACKed*/
    if (sent[seq] && !acked[seq]){
      tolayer3(A, buffer[seq]);
      packets_resent++;
    }
    
    if (i==0){
      starttimer(A,RTT);
      timer_running = true;
    }
    
  }
}



/* the following routine will be called once (only) before any other */
/* entity A routines are called. You can use it to do any initialization */
void A_init(void)
{
  /* initialise A's window, buffer and sequence number */
  A_nextseqnum = 0;  /* A starts with seq num 0, do not change this */
  oldestUnacked = 0; /* Oldest unACKed packet*/
  timer_running = false; /* timer is initally off*/

  for (int i = 0; i < SEQSPACE; i++)
    acked[i] = false;
    sent[i] = false;
}



/********* Receiver (B)  variables and procedures ************/

static int expectedseqnum; /* the sequence number expected next by the receiver */
static int B_nextseqnum;   /* the sequence number for the next packets sent by B */
static struct pkt recv_buffer[SEQSPACE]; /* buffer for out of order packets */
static bool received[SEQSPACE]; /* array to indicate which packets have been received. Used for dealing with out of order packets*/

/* called from layer 3, when a packet arrives for layer 4 at B*/
void B_input(struct pkt packet)
{
  struct pkt sendpkt;
  int i;

  /* if not corrupted - whether it is in order does not matter at this stage for SR */
  if  (!IsCorrupted(packet)){
    if (TRACE > 0)
      printf("----B: packet %d is correctly received, send ACK!\n",packet.seqnum);
    packets_received++;

    /* record that the packet has been received*/
    received[packet.seqnum] = true;
    recv_buffer[packet.seqnum] = packet;

    /* update state variables */
    expectedseqnum = (expectedseqnum + 1) % SEQSPACE;

    /* always send an ACK for the received packet, even if duplicate or out of order*/
    /* Must use sequence number of the received packet for ACK */
    sendpkt.acknum = packet.seqnum;

    /* we don't have any data to send.  fill payload with 0's */
    for ( i=0; i<20 ; i++ )
      sendpkt.payload[i] = '0';

    /* computer checksum */
    sendpkt.checksum = ComputeChecksum(sendpkt);

    /* send out packet */
    tolayer3 (B, sendpkt);

    /* deliver all in-order packets to receiving application, starting from expectedseqnum */
    while (received[expectedseqnum]) {
        tolayer5(B, recv_buffer[expectedseqnum].payload);
        received[expectedseqnum] = false;
        expectedseqnum = (expectedseqnum + 1) % SEQSPACE;
    }

  }
  else {
    /* packet is corrupted - ignore it */
    if (TRACE > 0)
      printf("----B: packet corrupted or not expected sequence number, resend ACK!\n");
  }
}

/* the following routine will be called once (only) before any other */
/* entity B routines are called. You can use it to do any initialization */
void B_init(void)
{
  expectedseqnum = 0;
  B_nextseqnum = 1;

  for (int i = 0; i < SEQSPACE; i++)
    received[i] = false;
}

/******************************************************************************
 * The following functions need be completed only for bi-directional messages *
 *****************************************************************************/

/* Note that with simplex transfer from a-to-B, there is no B_output() */
void B_output(struct msg message)
{
}

/* called when B's timer goes off */
void B_timerinterrupt(void)
{
}
