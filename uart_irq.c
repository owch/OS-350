/**
 * @brief: uart_irq.c 
 * @author: NXP Semiconductors
 * @author: Y. Huang
 * @date: 2014/02/28
 */

#include <LPC17xx.h>
#include "uart.h"
#include "uart_polling.h"
#include "printf.h"
#include "k_message.h"
#include "k_memory.h"
#include "k_rtx.h"
#include "sys_proc.h"
#include "i_proc.h"
#include "k_process.h"


int g_buffer_size = MEM_BLK_SZ - 0x28;			//fix
uint8_t g_buffer[MEM_BLK_SZ - 0x28];
uint8_t *gp_buffer = g_buffer;
uint32_t g_buffer_index =0;
uint8_t g_send_char = 0;
uint8_t g_char_in;
uint8_t g_char_out;
msgbuf* message= NULL;

extern uint32_t g_switch_flag;

extern int k_release_processor(void);
/**
 * @brief: initialize the n_uart
 * NOTES: It only supports UART0. It can be easily extended to support UART1 IRQ.
 * The step number in the comments matches the item number in Section 14.1 on pg 298
 * of LPC17xx_UM
 */
int uart_irq_init(int n_uart) {

	LPC_UART_TypeDef *pUart;
	if ( n_uart ==0 ) {
		/*
		Steps 1 & 2: system control configuration.
		Under CMSIS, system_LPC17xx.c does these two steps
		 
		-----------------------------------------------------
		Step 1: Power control configuration. 
		        See table 46 pg63 in LPC17xx_UM
		-----------------------------------------------------
		Enable UART0 power, this is the default setting
		done in system_LPC17xx.c under CMSIS.
		Enclose the code for your refrence
		//LPC_SC->PCONP |= BIT(3);
	
		-----------------------------------------------------
		Step2: Select the clock source. 
		       Default PCLK=CCLK/4 , where CCLK = 100MHZ.
		       See tables 40 & 42 on pg56-57 in LPC17xx_UM.
		-----------------------------------------------------
		Check the PLL0 configuration to see how XTAL=12.0MHZ 
		gets to CCLK=100MHZin system_LPC17xx.c file.
		PCLK = CCLK/4, default setting after reset.
		Enclose the code for your reference
		//LPC_SC->PCLKSEL0 &= ~(BIT(7)|BIT(6));	
			
		-----------------------------------------------------
		Step 5: Pin Ctrl Block configuration for TXD and RXD
		        See Table 79 on pg108 in LPC17xx_UM.
		-----------------------------------------------------
		Note this is done before Steps3-4 for coding purpose.
		*/
		
		/* Pin P0.2 used as TXD0 (Com0) */
		LPC_PINCON->PINSEL0 |= (1 << 4);  
		
		/* Pin P0.3 used as RXD0 (Com0) */
		LPC_PINCON->PINSEL0 |= (1 << 6);  

		pUart = (LPC_UART_TypeDef *) LPC_UART0;	 
		
	} else if ( n_uart == 1) {
	    
		/* see Table 79 on pg108 in LPC17xx_UM */ 
		/* Pin P2.0 used as TXD1 (Com1) */
		LPC_PINCON->PINSEL4 |= (2 << 0);

		/* Pin P2.1 used as RXD1 (Com1) */
		LPC_PINCON->PINSEL4 |= (2 << 2);	      

		pUart = (LPC_UART_TypeDef *) LPC_UART1;
		
	} else {
		return 1; /* not supported yet */
	} 
	
	/*
	-----------------------------------------------------
	Step 3: Transmission Configuration.
	        See section 14.4.12.1 pg313-315 in LPC17xx_UM 
	        for baud rate calculation.
	-----------------------------------------------------
        */
	
	/* Step 3a: DLAB=1, 8N1 */
	pUart->LCR = UART_8N1; /* see uart.h file */ 

	/* Step 3b: 115200 baud rate @ 25.0 MHZ PCLK */
	pUart->DLM = 0; /* see table 274, pg302 in LPC17xx_UM */
	pUart->DLL = 9;	/* see table 273, pg302 in LPC17xx_UM */
	
	/* FR = 1.507 ~ 1/2, DivAddVal = 1, MulVal = 2
	   FR = 1.507 = 25MHZ/(16*9*115200)
	   see table 285 on pg312 in LPC_17xxUM
	*/
	pUart->FDR = 0x21;       
	
 

	/*
	----------------------------------------------------- 
	Step 4: FIFO setup.
	       see table 278 on pg305 in LPC17xx_UM
	-----------------------------------------------------
        enable Rx and Tx FIFOs, clear Rx and Tx FIFOs
	Trigger level 0 (1 char per interrupt)
	*/
	
	pUart->FCR = 0x07;

	/* Step 5 was done between step 2 and step 4 a few lines above */

	/*
	----------------------------------------------------- 
	Step 6 Interrupt setting and enabling
	-----------------------------------------------------
	*/
	/* Step 6a: 
	   Enable interrupt bit(s) wihtin the specific peripheral register.
           Interrupt Sources Setting: RBR, THRE or RX Line Stats
	   See Table 50 on pg73 in LPC17xx_UM for all possible UART0 interrupt sources
	   See Table 275 on pg 302 in LPC17xx_UM for IER setting 
	*/
	/* disable the Divisior Latch Access Bit DLAB=0 */
	pUart->LCR &= ~(BIT(7)); 
	
	//pUart->IER = IER_RBR | IER_THRE | IER_RLS; 
	pUart->IER = IER_RBR | IER_RLS;

	/* Step 6b: enable the UART interrupt from the system level */
	
	if ( n_uart == 0 ) {
		NVIC_EnableIRQ(UART0_IRQn); /* CMSIS function */
	} else if ( n_uart == 1 ) {
		NVIC_EnableIRQ(UART1_IRQn); /* CMSIS function */
	} else {
		return 1; /* not supported yet */
	}
	pUart->THR = '\0';
	return 0;
}


/**
 * @brief: use CMSIS ISR for UART0 IRQ Handler
 * NOTE: This example shows how to save/restore all registers rather than just
 *       those backed up by the exception stack frame. We add extra
 *       push and pop instructions in the assembly routine. 
 *       The actual c_UART0_IRQHandler does the rest of irq handling
 */
__asm void UART0_IRQHandler(void)
{
	CPSID I; //disable irq
	PRESERVE8
	IMPORT c_UART0_IRQHandler
	IMPORT k_release_processor
	PUSH{r4-r11, lr}
	BL c_UART0_IRQHandler
	LDR R4, =__cpp(&g_switch_flag)
	LDR R4, [R4]
	MOV R5, #0     
	CMP R4, R5
	CPSIE I; //enable irq
	BEQ  RESTORE    ; if g_switch_flag == 0, then restore the process that was interrupted
	BL k_release_processor  ; otherwise (i.e g_switch_flag == 1, then switch to the other process)
RESTORE
	POP{r4-r11, pc}
} 
/**
 * @brief: c UART0 IRQ Handler
 */
void c_UART0_IRQHandler(void){
	input_char();
}

input_char(){
	uint8_t IIR_IntId;	    // Interrupt ID from IIR 		 
	LPC_UART_TypeDef *pUart = (LPC_UART_TypeDef *)LPC_UART0;
	PCB* orig_proc;
	int sender_id;
	int i;
	
//	uart1_put_string("Entering c_UART0_IRQHandler\n\r");

	/* Reading IIR automatically acknowledges the interrupt */
	IIR_IntId = (pUart->IIR) >> 1 ; // skip pending bit in IIR 
	if (IIR_IntId & IIR_RDA) { // Receive Data Avaialbe
		/* read UART. Read RBR will clear the interrupt */
		g_char_in = pUart->RBR;
		
		//if(g_index)
		if(g_char_in =='\n'||g_char_in =='\r'){
				//g_buffer_index=0;
				send_KCD_message();
				
		}else{
			g_buffer[g_buffer_index] = g_char_in;
			g_buffer_index++;
		}
		
		//send message to CRT
		
		//uart1_put_string("Reading a char = ");
		
		//uart0_put_char(g_char_in);
		
		
		//g_buffer[12] = g_char_in; // nasty hack
		//g_send_char = 1;
		
		#ifdef _DEBUG_HOTKEYS
	
		//hot key interrupts
		if ( g_char_in == 'q' || g_char_in =='w' || g_char_in=='e' ) {
			printf("Current Process %d\r\n", gp_current_process->m_pid);
			switch(g_char_in){
				//ready
				case 'q' :
					print_RDY_PROC();
					break;
				
				//blocked on memory
				case 'w' :
					print_BLK_PROC();
					break;
				
				//blocked on message
				case 'e' :
					print_BLK_MSG_PROC();
					break;
			}
		}
		
		#endif
		
		
		
	} else if (IIR_IntId & IIR_THRE) {
	/* THRE Interrupt, transmit holding register becomes empty */

		if (*gp_buffer != '\0' ) {
		
			g_char_out = *gp_buffer;
			//uart1_put_string("Writing a char = ");
			//uart0_put_char(g_char_out);
			//uart1_put_string("\n\r");
			
			//g_char_out = *gp_buffer;
			pUart->THR = g_char_out;
			gp_buffer++;
			
			//printf("Writing a char = %c \n\r", g_char_out);		
			//pUart->THR = g_char_out;
			//gp_buffer++;
			
		} else {
			k_release_memory_block(message);
			gp_buffer=g_buffer;
			
			if(!is_message_empty(UART_PROC_ID)){
				orig_proc = gp_current_process;
				gp_current_process = gp_pcbs[UART_PROC_ID-1];
				message = k_receive_message(&sender_id);
				gp_current_process=orig_proc;
				
				gp_buffer=message->mtext;
				
				//for(i=0;i<strlen(gp_buffer);i++){
				//	pUart->THR = gp_buffer[i];
				//}
				
				
				g_char_out = *gp_buffer;
				pUart->THR = g_char_out;
				gp_buffer++;
				
			}else{
				//uart1_put_string("Finish writing. Turning off IER_THRE\n\r");
			//}		
				pUart->IER ^= IER_THRE; // toggle the IER_THRE bit 
				pUart->THR = '\0';
				g_send_char = 0;
				gp_buffer = g_buffer;			
		}
	}
	      
	} else {  /* not implemented yet */
			uart1_put_string("Should not get here!\n\r");
		return;
	}	
}

void send_KCD_message(){
	msgbuf* kcd_message;
	kcd_message= (msgbuf*)k_request_memory_block_i();
	if(kcd_message==NULL){
		//scrap message
		return;
	}
	kcd_message->mtype=DEFAULT;
	strncpy(kcd_message->mtext, (char*)g_buffer, g_buffer_index);
	
	// set current process to this process
	k_send_message_i(KCD_PROC_ID, kcd_message);
	
	clear_g_buffer();
}

void clear_g_buffer(){
	int i;
	for(i=0;i<g_buffer_size;i++){
		g_buffer[i]='\0';
	}
	g_buffer_index =0;
}

void print_RDY_PROC(){
		int j;
		printf("Ready Process:");
		for(j=0;j<(NUM_I_PROCS+NUM_SYS_PROCS+NUM_TEST_PROCS);j++){
			if(gp_pcbs[j]->m_state==RDY||gp_pcbs[j]->m_state==NEW){
				printf(" %d",gp_pcbs[j]->m_pid);
			}
		}
		printf("\r\n");
}
void print_BLK_PROC(){
	int j;
		printf("Memory Blocked Process:");
		for(j=0;j<(NUM_I_PROCS+NUM_SYS_PROCS+NUM_TEST_PROCS);j++){
			if(gp_pcbs[j]->m_state==BLK){
				printf(" %d",gp_pcbs[j]->m_pid);
			}
		}
		printf("\r\n");
}
void print_BLK_MSG_PROC(){
int j;
		printf("Message Blocked Process:");
		for(j=0;j<(NUM_I_PROCS+NUM_SYS_PROCS+NUM_TEST_PROCS);j++){
			if(gp_pcbs[j]->m_state==MSG_BLK){
				printf(" %d",gp_pcbs[j]->m_pid);
			}
		}
		printf("\r\n");
}