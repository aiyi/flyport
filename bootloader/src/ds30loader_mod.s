;------------------------------------------------------------------------------
; Title:			ds30 loader for PIC24FJ - Modified version for Picus Flyport
;
; File description:	Main firmwarefile
;
; Copyright: 		Copyright © 09-10, Mikael Gustafsson
;
; Version			1.2.0 september 2011
;
; Webpage: 			http://mrmackey.no-ip.org/elektronik/ds30loader/
;
; History:			(1.2.0 Upgrade from external flash memory)
;					1.1.1 New devices
;					1.1.0 New feature: bootloader protection
;						  New feature: auto baudrate detection
;						  New option: high baud rates selection
;					1.0.4 Bugfix: rs485 not working, nop inserted before check of trmt in Send()
;					      New feature: 0x00 goto protection
;					1.0.3 Added tx enable support, new Send function
;					1.0.2 Erase is now made just before write to increase reliability					
;					1.0.1 Fixed baudrate error check
;					1.0.0 Added flash verification
;						  Removed PIC24FxxKAyyy stuff, se separate fw
;						  Corrected buffer variable location to .bss
;						  Buffer is now properly sized
;					0.9.1 Removed initialization of stack limit register
;						  BRG is rounded instead of truncated
;						  Removed frc+pll option
;						  Added pps code
;						  Added baudrate error check
;					0.9.0 First version released, based on the dsPIC33F version
                                                              
;------------------------------------------------------------------------------

;-----------------------------------------------------------------------------
;    This file is part of ds30 Loader.
;
;    ds30 Loader is free software: you can redistribute it and/or modify
;    it under the terms of the GNU General Public License as published by
;    the Free Software Foundation.
;
;    ds30 Loader is distributed in the hope that it will be useful,
;    but WITHOUT ANY WARRANTY; without even the implied warranty of
;    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
;    GNU General Public License for more details.
;
;    You should have received a copy of the GNU General Public License
;    along with ds30 Loader. If not, see <http://www.gnu.org/licenses/>.
;------------------------------------------------------------------------------ 


;------------------------------------------------------------------------------
; Register usage
;------------------------------------------------------------------------------
		;.equ	MIXED,		W0		;immediate
		;equ	MIXED,		W1		;immediate
		.equ	WBUFPTR,	W2		;buffer pointer
		.equ	WCNT,		W3		;loop counter
		.equ	WADDR2,		W4		;memory pointer
		.equ	WADDR,		W5		;memory pointer		
		.equ	WPPSTEMP1,	W6		;used to restore pps register
		.equ	WPPSTEMP2,	W7		;used to restore pps register
		.equ	WTEMP1,		W8		;
		.equ	WUPGRADEOK,	W9		;Set to !=0 if serial or flash upgrade occurred	
		.equ	WDEL1,		W10		;delay outer
		.equ	WDEL2,		W11		;delay inner
		.equ	WDOERASE,	W12		;flag indicated erase should be done before next write
		.equ	WCMD,		W13		;command
		.equ 	WCRC, 		W14		;checksum
		.equ	WSTPTR,		W15		;stack pointer


;------------------------------------------------------------------------------
; Includes
;------------------------------------------------------------------------------
		.include "settings.inc"	


;-----------------------------------------------------------------------------
; UARTs
;------------------------------------------------------------------------------ 


		
		.equ    UMODE,	    U2MODE					;uart mode
		.equ    USTA,  		U2STA					;uart status
		.equ    UBRG,		U2BRG					;uart baudrate
		.equ    UTXREG,		U2TXREG					;uart transmit
		.equ	URXREG,		U2RXREG					;uart receive
		.equ	UIFS,		IFS1					;uart interupt flag sfr
		.equ	URXIF,		U2RXIF					;uart received interupt flag
		.equ	UTXIF,		U2TXIF					;uart transmit interupt flag		   	

				

;------------------------------------------------------------------------------
; Constants, don't change
;------------------------------------------------------------------------------
		.equ	VERMAJ,		1							;firmware version major
		.equ	VERMIN,		3							;firmware version minor
		.equ	VERREV,		1							;firmware version revision

		.equ 	HELLO, 		0xC1		
		.equ 	OK, 		'K'							;erase/write ok
		.equ 	CHECKSUMERR,'N'							;checksum error
		.equ	VERFAIL,	'V'							;verification failed
		.equ   	BLPROT,     'P'                         ;bl protection tripped
		.equ   	UCMD,     	'U'                         ;unknown command
	

		.equ	BLDELAY,	( BLTIME * (FCY / 1000) / (65536 * 7) )				;delay berfore user application is loaded
		.ifdef USE_BRGH
			.equ	UARTBR,		( (((FCY / BAUDRATE) / 2) - 1) / 2 )			;brg calculation with rounding
		.else
			.equ	UARTBR,		( (((FCY / BAUDRATE) / 8) - 1) / 2 )			;brg calculation with rounding
		.endif

		.equ	PAGESIZER,	8													;pagesize [rows]
		.equ	ROWSIZEW,	64													;rowsize [words]
		.equ	STARTADDR,	( FLASHSIZE - BLPLP * PAGESIZER * ROWSIZEW * 2 )	;bootloader placement
		.equ	BLSTARTROW,	(STARTADDR / ROWSIZEW / 2)	
		.equ	BLENDROW,	(STARTADDR / ROWSIZEW / 2 + (BLSIZEP*PAGESIZER) - 1)	




		;Start address of external flash		
		.equ    EXT_FLASH_MSB,	0x1C	;External flash High Address Byte
		.equ    EXT_FLASH_ASB,	0x00	;External flash Middle Address Byte
		.equ    EXT_FLASH_LSB,	0x00	;External flash Low Address Byte
;------------------------------------------------------------------------------
; Validate user settings
;------------------------------------------------------------------------------
		; Internal cycle clock
		.if FCY > 16000000
			.error "Fcy specified is out of range"
		.endif

		; Baudrate error
		.ifdef USE_BRGH
			.equ REALBR,	( FCY / (4 * (UARTBR+1)) )
		.else
			.equ REALBR,	( FCY / (16 * (UARTBR+1)) )
		.endif
		.equ BAUDERR,	( (1000 * ( BAUDRATE - REALBR)) / BAUDRATE )
		.if ( BAUDERR > 25) || (BAUDERR < -25 )
			.error "Baudrate error is more than 2.5%. Remove this check or try another baudrate and/or clockspeed."
		.endif 
		

;------------------------------------------------------------------------------
; Global declarations
;------------------------------------------------------------------------------
        .global __reset          	;the label for the first line of code, needed by the linker script

;------------------------------------------------------------------------------
; Send macro
;------------------------------------------------------------------------------
		.macro SendL char
			mov #\char, W0
			rcall Send
		.endm
		
;------------------------------------------------------------------------------
; Start of code section in program memory
;------------------------------------------------------------------------------
		;! To enable breakpoints: use ".org" and comment next two ".section"
		;.org STARTADDR-520
		.section *, code, address(STARTADDR-4)	

usrapp:	
		nopr  						;these two instructions will be replaced
		nopr						;with a goto to the user app. by the pc program
		

;------------------------------------------------------------------------------
; Reset vector
;------------------------------------------------------------------------------
		.section *, code, address(STARTADDR)

__reset:mov 	#__SP_init, WSTPTR	;initalize the Stack Pointer


;------------------------------------------------------------------------------
; User specific entry code go here, see also user exit code section at end of file
;------------------------------------------------------------------------------

		mov		#0x0000, WUPGRADEOK	;Initialize flag

		;----------------------------------------------------------------------
		; UART, SPI pps config and chip select pin
		;----------------------------------------------------------------------	
		; Backup, these are restored in exit code at end of file
		mov		RPINR19, W0
		mov		W0, 0x1FF0
		mov		RPOR11,  W0
		mov		W0, 0x1FF2
		mov		RPINR20, W0
		mov		W0, 0x1FF4
		mov		RPOR6,  W0
		mov		W0, 0x1FF6
		mov		RPOR12, W0
		mov		W0, 0x1FF8
		mov		TRISD, W0
		mov		W0, 0x1FFA
		mov		LATD, W0
		mov		W0, 0x1FFC

		; Receive, map pin to uart: RPINR19 = 0b011000(=24d), i.e. function INR19(low)=U2RX is mapped in pin RP24(RD1)
		bclr	RPINR19, #U2RXR0		
		bclr	RPINR19, #U2RXR1		
		bclr	RPINR19, #U2RXR2		
		bset	RPINR19, #U2RXR3		
		bset	RPINR19, #U2RXR4		
		bclr	RPINR19, #U2RXR5		
		
		; Transmit, map uart to pin: RPOR11 = 0b000101(=5d), i.e. pin RP22(RD3) is function 5 (U2TX)
		bset	RPOR11, #RP22R0			
		bclr	RPOR11, #RP22R1			
		bset	RPOR11, #RP22R2			
		bclr	RPOR11, #RP22R3			
		bclr	RPOR11, #RP22R4			
		bclr	RPOR11, #RP22R5			

		; SPI1, Serial Data In (SDI1): RPINR20 = 0b011100(=28d) -> function INR20(low)=SDI1R is mapped in pin RP28(RB4)
		bclr	RPINR20, #SDI1R0
		bclr	RPINR20, #SDI1R1
		bset	RPINR20, #SDI1R2
		bset	RPINR20, #SDI1R3
		bset	RPINR20, #SDI1R4
		bclr	RPINR20, #SDI1R5

		; SPI1, Serial Data Out (SDO1): RPOR6 = 0b000111(=7d) -> pin RP13(RB2) is function 7 (SDO1)
		bset	RPOR6, #RP13R0
		bset	RPOR6, #RP13R1
		bset	RPOR6, #RP13R2
		bclr	RPOR6, #RP13R3
		bclr	RPOR6, #RP13R4
		bclr	RPOR6, #RP13R5

		; SPI1, Serial Clock Out (SCK): RPOR12 = 0b001000(=8d) -> pin RP25(RD4) is function 8 (SCK1)
		bclr	RPOR12, #RP25R0
		bclr	RPOR12, #RP25R1
		bclr	RPOR12, #RP25R2
		bset	RPOR12, #RP25R3
		bclr	RPOR12, #RP25R4
		bclr	RPOR12, #RP25R5

		;Masks to set/clear bit X of PORTD for Chip select
		.equ	CS_ORMASK,		0x0040	;bit6
		.equ	CS_ANDMASK,		0xFFBF	;inverse of the above

		;----------------------------------------------------------------------
		; I/O config : All pins are digital and PORTD.6 is Chip Select for ExtMem
		;----------------------------------------------------------------------
		MOV	#0xFFFF, W0
		MOV	W0, AD1PCFGL		;All pins are digital
		MOV	W0, AD1PCFGH
		MOV	#CS_ANDMASK, W1		;bit6 = 0
		MOV	#TRISD, W0			;W0 = address of TRISD
		AND	W1, [W0], [W0]		;TRISD &= 0xFFBF  <-> TRISD.6=0 (output)

		MOV	#CS_ORMASK, W1		;Chip select high
		MOV	#LATD, W0			
		IOR	W1, [W0], [W0]		;LATD.6=1
		
		repeat	#0x3FFF			;Wait for external memory to be fully powered
		nop

		;----------------------------------------------------------------------
		; SPI config
		;----------------------------------------------------------------------
		mov	#0x013C, W0			;0b0000.0001.0011.1100
		mov W0, SPI1CON1	
		mov #0x8000, W0			;Spi Enable
		mov W0, SPI1STAT

;------------------------------------------------------------------------------
; Init
;------------------------------------------------------------------------------
		clr		WDOERASE
        
		bset	UMODE, #BRGH
		mov		#UARTBR, W0 		;set	
		mov 	W0, UBRG			;baudrate
		bset 	UMODE, #UARTEN		;enable uart
		bset 	USTA, #UTXEN		;enable transmit			

;------------------------------------------------------------------------------
; Receive hello
;------------------------------------------------------------------------------
		rcall 	Receive
		sub 	#HELLO, W0			;check
		bra 	nz, exit			;prompt

;------------------------------------------------------------------------------
; Send device id and firmware version
;------------------------------------------------------------------------------
		SendL 	DEVICEID
		SendL	VERMAJ
		SendL	(VERMIN*16 + VERREV)
		
;------------------------------------------------------------------------------
; Main
;------------------------------------------------------------------------------
		; Send ok
Main:	SendL 	OK

		; Init checksum
main1:	clr 	WCRC
	
		;----------------------------------------------------------------------
		; Receive address
		;----------------------------------------------------------------------
		; Upper byte
		rcall 	Receive
		mov 	W0, TBLPAG
		; High byte, use PR1 as temporary sfr
		rcall 	Receive		
		mov.b	WREG, PR1+1
		; Low byte, use PR1 as temporary sfr
		rcall 	Receive
		mov.b	WREG, PR1
		;
		mov		PR1, WREG
		mov		W0,	WADDR
		mov		W0, WADDR2
		
		
		;----------------------------------------------------------------------
		; Receive command
		;----------------------------------------------------------------------
		rcall 	Receive
		mov		W0, WCMD
		

		;----------------------------------------------------------------------
		; Receive nr of data bytes that will follow
		;----------------------------------------------------------------------
		rcall 	Receive				
		mov 	W0, WCNT
	

		;----------------------------------------------------------------------
		; Receive data		
		;----------------------------------------------------------------------
		mov 	#buffer, WBUFPTR
rcvdata:
		rcall 	Receive				
		mov.b 	W0, [WBUFPTR++]
		dec		WCNT, WCNT
		bra 	nz, rcvdata			;last byte received is checksum	
		

		;----------------------------------------------------------------------
		; 0x00 goto protection
		;----------------------------------------------------------------------	
		.ifdef	PROT_GOTO
			cp0		TBLPAG
			bra		nz, chksum
			cp0		PR1
			bra		nz, chksum
			
			;
			mov 	#buffer, WBUFPTR
			; 1st word upper byte = goto instruction
			mov.b 	#0x04, W0
			mov.b	W0, [WBUFPTR++] 		
			; 1st word  low byte = low address byte
			mov.b 	#(0xff & STARTADDR), W0
			mov.b 	W0, [WBUFPTR++] 
			; 1st word high byte = high address byte
			mov.b 	#(0xff & (STARTADDR>>8)), W0
			mov.b 	W0, [WBUFPTR++]	
			;2nd word upper byte = unused
			clr.b	[WBUFPTR++]	
			; 2nd word  low byte = upper address byte
			mov.b 	#(0xff & (STARTADDR>>16)), W0
			mov.b 	W0, [WBUFPTR++]  
			; 2nd word high byte = unused
			clr.b 	[WBUFPTR++]  
		.endif
		
						
		;----------------------------------------------------------------------
		; Check checksum
		;----------------------------------------------------------------------
chksum:	cp0.b 	WCRC
		bra 	z, ptrinit
		SendL 	CHECKSUMERR
		bra 	main1			
		
	
		;----------------------------------------------------------------------
		; Init pointer
		;----------------------------------------------------------------------			
ptrinit:mov 	#buffer, WBUFPTR
		
		
		;----------------------------------------------------------------------
		; Check command
		;----------------------------------------------------------------------			
		; Erase page
		btsc	WCMD,	#0		
		bra		eraseact		
		; Write row
wrrow_:	btsc	WCMD,	#1		
		bra		blprot
		;Else, unknown command
		SendL   UCMD		
		bra     main1
		
					
		;----------------------------------------------------------------------
		; "Erase activation"
		;----------------------------------------------------------------------		
eraseact:
		mov		#0xffff, WDOERASE
		bra		Main
					
					
		;----------------------------------------------------------------------
		; Bootloader protection
		;----------------------------------------------------------------------
blprot:	nop
		.ifdef PROT_BL
			; Calculate row number of received address
			mov		TBLPAG, W1
			mov		WADDR, W0
			mov		#(ROWSIZEW*2), WTEMP1
			repeat	#17
			div.ud	W0, WTEMP1;		W = received address / (rowsizew*2)
			; Received row < bl start row = OK
			mov		#BLSTARTROW, WTEMP1
			cp		W0, WTEMP1
			bra		N, blprotok
			; Received row > bl end row = OK
			mov		#BLENDROW, WTEMP1
			cp		WTEMP1, W0
			bra		N, blprotok		
			; Protection tripped
			SendL   BLPROT		
		    bra     main1
			; Restore WADDR2
blprotok:	mov		WADDR, WADDR2
		.endif
		
			
		;----------------------------------------------------------------------		
		; Erase page
		;----------------------------------------------------------------------		
		btss	WDOERASE, #0		; if bit #0 of WDOERASE==0 jump to program, else erase page
		bra		program		
		tblwtl	WADDR, [WADDR]		;"Set base address of erase block", equivalent to setting nvmadr/u in dsPIC30F?
		; Erase
		mov 	#0x4042, W0
		rcall 	Write	
		; Erase finished
		clr		WDOERASE
		
		
		;----------------------------------------------------------------------		
		; Write row
		;----------------------------------------------------------------------		
program:mov 	#ROWSIZEW, WCNT
		; Load latches
latlo:	tblwth.b 	[WBUFPTR++], [WADDR] 	;upper byte
		tblwtl.b	[WBUFPTR++], [WADDR++] 	;low byte
		tblwtl.b	[WBUFPTR++], [WADDR++] 	;high byte	
		dec 	WCNT, WCNT
		bra 	nz, latlo
		; Write flash row
		mov 	#0x4001, W0		
		rcall 	Write

		
		;----------------------------------------------------------------------		
		; Verify row
		;----------------------------------------------------------------------
		mov 	#ROWSIZEW, WCNT
		mov 	#buffer, WBUFPTR	
		; Verify upper byte
verrow:	tblrdh.b [WADDR2], W0
		cp.b	W0, [WBUFPTR++]
		bra		NZ, vfail	
		; Verify low byte
		tblrdl.b [WADDR2++], W0
		cp.b	W0, [WBUFPTR++]
		bra		NZ, vfail
		; Verify high byte
		tblrdl.b [WADDR2++], W0
		cp.b	W0, [WBUFPTR++]
		bra		NZ, vfail
		; Loop
		dec		WCNT, WCNT
		bra 	nz, verrow
		; Verify completed without errors
		mov		#0x1234, WUPGRADEOK
		
		bra		Main	
		
			
		;----------------------------------------------------------------------
		; Verify fail
		;----------------------------------------------------------------------
vfail:	SendL	VERFAIL
		bra		main1		
		
				
;------------------------------------------------------------------------------
; Write()
;------------------------------------------------------------------------------
Write:	mov 	W0, NVMCON
		mov 	#0x55, W0
		mov 	W0, NVMKEY
		mov 	#0xAA, W0
		mov 	W0, NVMKEY
		bset 	NVMCON, #WR
		nop
		nop	
		; Wait for erase/write to finish	
compl:	btsc	NVMCON, #WR		
		bra 	compl				
		return

		
;------------------------------------------------------------------------------
; Send()
;------------------------------------------------------------------------------
Send:	; Enable tx
		.ifdef USE_TXENABLE
			bset	LATR_TXE, #LATB_TXE
			nop
		.endif		
		;Send byte
		mov 	WREG, UTXREG
		nop
		nop
		; Wait until transmit shift register is empty
txwait:	btss	USTA, #TRMT
		bra		txwait
		; Disable tx 
		.ifdef USE_TXENABLE
			bclr	LATR_TXE, #LATB_TXE
		.endif
		; Send complete
		return	
		
		
;------------------------------------------------------------------------------
; Receive()
;------------------------------------------------------------------------------
		; Init delay
Receive:mov 	#BLDELAY, WDEL1
		; Check for received byte
rpt1:	clr		WDEL2
rptc:	clrwdt						;clear watchdog
		btss 	USTA, #URXDA		
		bra 	notrcv
		mov 	URXREG, W0			
		add 	WCRC, W0, WCRC		;add to checksum
		return
 		; Delay
notrcv:	dec 	WDEL2, WDEL2
		bra 	nz, rptc
		dec 	WDEL1, WDEL1
		bra 	nz, rpt1
		; If we get here, uart receive timed out
        mov 	#__SP_init, WSTPTR	;reinitialize the Stack Pointer
        
		
;------------------------------------------------------------------------------
; Exit point, check if upgrade from flash is flagged
;------------------------------------------------------------------------------		
exit:	
		;If at location STARTADDR-2 low part is FFFF then start external flash upgrade
		mov		#tblpage(STARTADDR-2), W0
		mov		W0, TBLPAG
		mov		#tbloffset(STARTADDR-2), W0
		tblrdl	 [W0], W1

		mov		#0xFFFF, W0

		cp		W0, W1
		bra		NZ, SkipUpgradeFromExtMem

		rcall   UpgradeFromExtMem

SkipUpgradeFromExtMem:
;------------------------------------------------------------------------------
; User specific exit code go here
;------------------------------------------------------------------------------
		;Restore PPS and CS Port
		mov		0x1FF0, W0
		mov		W0, RPINR19
		mov		0x1FF2, W0		
		mov		W0, RPOR11
		mov		0x1FF4, W0
		mov		W0, RPINR20
		mov		0x1FF6, W0
		mov		W0, RPOR6
		mov		0x1FF8, W0
		mov		W0, RPOR12
		mov		0x1FFA, W0
		mov		W0, TRISD
		mov		0x1FFC, W0		
		mov		W0, LATD

		mov 	#0x1234, W0					;If WUPGRADEOK is 0x1234 then UART upgrade occurred
		cp		W0, WUPGRADEOK
		bra		Z, EraseLastTwoPages
		; no UART upgrade, jump to application
		bra 	usrapp
		;call    EraseLastTwoPages			;Upgrade occurred: erase configuration flasg

;------------------------------------------------------------------------------
; Load user application
;------------------------------------------------------------------------------
LoadUserApplication:
        bra 	usrapp


;------------------------------------------------------------------------------
;Read from External Flash
;------------------------------------------------------------------------------
		.equ	WENDADDR,   W3		;End address (bootloader address) >> 4 (ex. 0x29C0)
		.equ	WROWCNT,	W4		;Row (64 program words) counter: 0 to 15
		.equ	W64CNT,		W5		;64 program words counter: 0 to 63
		.equ	WSPI_LO,	W6		;First byte read from spi
		.equ	WSPI_MID,	W7		;Second byte read from spi
		.equ	WSPI_HI,	W8		;Third byte read from spi
		.equ	WUPGRADEOK,	W9		;Set to 1 if serial or flash upgrade occurred	
		.equ	WINADDRL,	W10		;Internal flash address (low)
		.equ	WINADDRH,	W11		;Internal flash address (high)
		.equ    WGOTOL,	    W12		;Goto First Line of Application (first 3 bytes of Ext flash)
	    .equ    WGOTOH,     W13     ;Goto First Line of Application (following 3 bytes)

		.equ	SPI_READ_CMD,0x03	;Spi read command

;CODE:
;   pageaddr = 0
;	while pageaddr < 0x29C00
;		Erase this page		
;		for row = 0 to 7 'For each page there are 1024/128=8 rows
;			For latch = 0 To 63 '64 latches
;				read lo from spi
;				read Mid from spi
;				read hi from spi
;      
;				write mid|lo in latch
;				write 0|hi in latch
;    		Next latch
;    		'Now 64 latches are prepared
;    		write latches in memory
;    	next row
;	wend

		;Mix content of WH and WL so that W2 = (WH|WL)>>4
		.macro	ShiftAddr WL, WH		;Example: WH|WL = 0xA2460
			lsr		\WL, #4, W0			;WL = 0x2460 -> W0 = 0x0246
			sl		\WH, #12, W1	    ;WH = 0x000A -> W1 = 0xA000
			ior		W1, W0, W2			;W2 = 0xA246 i.e. complete address shifted right by 4
		.endm


UpgradeFromExtMem:
		MOV		#CS_ANDMASK, W1			;Chip select low
		MOV		#LATD, W0
		AND		W1, [W0], [W0]			;LATD.6 = 0

		mov 	#SPI_READ_CMD, W0		;Read Command
		mov 	W0, SPI1BUF
		rcall	SpiOk
		;Send 3 bytes of external flash base-address
		mov 	#EXT_FLASH_MSB, W0		;Address MSB
		mov 	W0, SPI1BUF
		rcall	SpiOk

		mov 	#EXT_FLASH_ASB, W0		;Address Middle
		mov 	W0, SPI1BUF
		rcall	SpiOk

		mov 	#EXT_FLASH_LSB, W0		;Address LSB
		mov 	W0, SPI1BUF
		rcall	SpiOk

		mov		#0x0000, W14			;! For simulation only

		;Initialize Destination Address
		mov		#0x0000, WINADDRL
		mov		#0x0000, WINADDRH

		;Initialize End Address (shifted right by 4 bits)
		mov		#tblpage(STARTADDR),W1
		mov		#tbloffset(STARTADDR),W0
		ShiftAddr W0, W1
		mov		W2, WENDADDR			;WENDADDR = 0x29C00

		;Initialize "goto userapplication"
		mov		#0xFFFF, WGOTOL
		mov		#0xFFFF, WGOTOH

NextPage:
		;Erase this page
		mov		#0x4042, W0
		mov		W0, NVMCON 
		; Init pointer to row to be ERASED
		mov		WINADDRH, TBLPAG					;Hi address
		TBLWTL 	WINADDRL, [WINADDRL]     			;Set base address of erase page

		btsc	NVMCON, #WR							;wait for any previous write/erase to be complete 
		bra 	$-2

		DISI   	#5 									;Key sequence
		mov		#0x55, W0                  
		mov		W0, NVMKEY      
		mov		#0xAA, W1                     
		mov		W1, NVMKEY   
		bset	NVMCON, #WR 
		nop  
		nop 

		;Counter of 8 rows = 1024 addresses = one page
		mov		#PAGESIZER, WROWCNT

NextRow:
		;Read 64 triplets
		mov		#ROWSIZEW, W64CNT		

		;Setup for row programming
		mov		#0x4001, W0
		mov		W0, NVMCON
		;Set page
		mov		WINADDRH, TBLPAG

NextLatch:
		mov 	#0xAA, W0			;Read First Data (0xAA is dummy for debug purposes)
		mov 	W0, SPI1BUF
		rcall	SpiOk
		;Now W1 contains first data -> WSPI_LO
		mov	 	W1, WSPI_LO

		mov 	#0xF0, W0			;Read Second Data (0xF0 is dummy for debug purposes)
		mov 	W0, SPI1BUF
		rcall	SpiOk
		;Now W1 contains second data -> WSPI_MID
		mov	 	W1, WSPI_MID

		mov 	#0x55, W0			;Read Third Data (0x55 is dummy for debug purposes)
		mov 	W0, SPI1BUF
		rcall	SpiOk
		;Now W1 contains third data -> WSPI_HI
		mov	 	W1, WSPI_HI

		;build word containing low and middle byte
		sl		WSPI_MID, #8, WSPI_MID					;MID data is shifted left
		ior		WSPI_MID, WSPI_LO, WSPI_MID				;MID now contains MID|LO
		mov		#0x00FF,W1
		and		WSPI_HI, W1, WSPI_HI


;OVERRIDE PART
		;-------------
		;There may be 2 cases:
		;1) External Flash program is like the following:
		;   00000 goto 0x004B0 (for example) \                  /  00000 goto 0x29C00 (bootloader)
		;   00004 ...                        |                  |  00004 ...
		;     :                              |                  |   : 
		;   004B0 User Application           | this must become |  004B0 User Application
		;     :                              | >>>>>>>>>>>>>>>> |   :
		;   29BFC nopr                       |                  |  29BFC goto 0x004B0
		;   29BFE nopr                       /                  \    
		;   29C00 Bootloader                                       29C00 Bootloader (unmodified)
		;
		;2) External Flash program is already like it should be (as above, right side)
		;In any case first line is overridden with "goto bootloader" and WGOTOH|L will contain address of first line as received
		;(in the above example may be 004B0 or 29C00)
		;But, in the first case WGOTOH|L will be written at address 29BFC, in second case that address won't be modified.

		;On early reads, the first 6 bytes *might* contain "goto userapplication" and must be overridden with "goto bootloader"
		mov		#0xFFFF,W0
		cp		W0, WGOTOL
		bra		NZ, GotoLowLoaded		;Skip if WGOTOL already contains an address
		;Memorize userapp address (LO part)
		mov		WSPI_MID, WGOTOL
		;If user application address (MID|LO received from spi = userapp LO part) is still 0xFFFF it means that external flash is not programmed. Override with default value.
		mov		#0xFFFF,W0
		cp		W0, WGOTOL
		bra		NZ, GotoAddrLowOk		;Skip next action because received address is ok
		;Override userapp addr with bootloader addr
		mov		#tbloffset(STARTADDR), WGOTOL		;Low Part (0x9C00)
GotoAddrLowOk:
		;In any case: this code is executed only when bytes 0,1,2 are received, so override received data with default ones
		mov		#tbloffset(STARTADDR), WSPI_MID		;Low Part (0x9C00)
		mov     #0x0004, WSPI_HI					;Goto OPCODE
		bra		GotoOk								;Low part is loaded, next cycle it will load hi part

GotoLowLoaded:
		mov		#0xFFFF,W0
		cp		W0, WGOTOH
		bra		NZ, GotoOk			;Skip if WGOTOH already contains an address
		;Memorize userapp address (HI part)
		mov		WSPI_MID, WGOTOH
		;If user application address (MID|LO received from spi = userapp HI part) is still 0xFFFF it means that external flash is not programmed. Override with default value.
		mov		#0xFFFF,W0
		cp		W0, WGOTOH
		bra		NZ, GotoAddrHiOk		;Skip next action because received address is ok
		;Override userapp addr with bootloader addr
		mov		#tblpage(STARTADDR), WGOTOH 		;Hi Part (0x0002)
GotoAddrHiOk:
		;In any case: this code is executed only when bytes 3,4,5 are received, so override received data with default ones
		mov		#tblpage(STARTADDR), WSPI_MID 		;Hi Part (0x0002)
		mov     #0x0000, WSPI_HI
		;When it arrives here, WGOTOH and WGOTOL contain user application address. uInstructions in 0x29C00-2 will be "goto opcode | WGOTOL" and (next line)  "00 | WGOTOH"

GotoOk:

		ShiftAddr WGOTOL, WGOTOH			;W2 = 0x29C0 or 0x004B (as in example above)
		cp		W2, WENDADDR				;Is first goto-address W2 == limit address (0x29C0)?
		bra		Z, NotUsrappHi			    ;if yes, skip this part so override of address 29BFC doesn't occur

		;if current address is bootloader-4 then override these value (they go @ label usrapp)
		mov     #tblpage(STARTADDR-4),W0 
		cp		WINADDRH,W0
		bra		NZ, NotUsrappLow
		mov		#tbloffset(STARTADDR-4),W0
		cp		WINADDRL,W0
		bra		NZ, NotUsrappLow
		;If here: current address is second-last opcode of the page, data will be replaced with first goto-address
		mov		WGOTOL, WSPI_MID	;Override with first goto-address (low part)
		mov		#0x0004,WSPI_HI		;goto OPCODE
NotUsrappLow:
		;Hi part
		mov     #tblpage(STARTADDR-2),W0 
		cp		WINADDRH,W0
		bra		NZ, NotUsrappHi
		mov		#tbloffset(STARTADDR-2),W0
		cp		WINADDRL,W0
		bra		NZ, NotUsrappHi
		;If here: current address is last opcode of the page, data will be replaced with first goto-address
		mov		WGOTOH, WSPI_MID	;Override with first goto-address (hi part)
		mov		#0x0000,WSPI_HI
NotUsrappHi:
		;End of Override -------------------

		;Write latches of internal flash memory
		TBLWTL 	WSPI_MID, [WINADDRL]					;Write PM mid|low bytes into program latch
		TBLWTH 	WSPI_HI, [WINADDRL++]					;Write PM high byte into program latch, increment address

		;If Low address has overflown, increment hi-address and setup table with new address
		cp0		WINADDRL
		bra		NZ, Continue							;non zero: not overflown
		inc		WINADDRH, WINADDRH
		mov		WINADDRH, TBLPAG

Continue:
		;Next Latch
		dec		W64CNT, W64CNT
		bra		NZ, NextLatch							;Keep on until 64 triplets and 64 latches are prepared
		;----------------------------

		;Finally write the 64 latches prepared so far into memory
		btsc	NVMCON, #WR					;wait for any previous write/erase to be complete 
		bra 	$-2

		DISI	#5							;Key sequence
		MOV		#0x55, W0
		MOV		W0, NVMKEY
		MOV		#0xAA, W0
		MOV		W0, NVMKEY
		BSET	NVMCON, #WR
		NOP
		NOP

		;Next row
		dec		WROWCNT, WROWCNT
		bra		NZ, NextRow
		;----------------------------
		
		;If reached limit, end program
		ShiftAddr WINADDRL, WINADDRH
		cp		W2, WENDADDR				;Is current address W2 < limit address(0x29C0)?
		bra		LT, NextPage				;if yes, limit is not reached and process next page
		;----------------------------
		
		;All memory written. End of procedure
		MOV		#CS_ORMASK, W1			;Chip select high
		MOV		#LATD, W0			
		IOR		W1, [W0], [W0]			;LATD.6=1

		;Upgrade has occurred
		;mov		#0x1234, WUPGRADEOK

		return


SpiOk:	;Wait for spi procedure to be complete
		btsc 	SPI1STAT, #SPITBF		;Continue if tx has started (bit clear), loop if not started
		bra		$-2
		btss 	SPI1STAT, #SPIRBF		;Continue if rx is complete (bit set), loop if not complete
		bra		$-2
		mov 	SPI1BUF, W1				;Read rx buffer
		mov 	#0xFF, W0
		and 	W1, W0, W1				;Low 8 bits are valid. W1 contains received byte.
		return

/*
SpiOk:	;!! For MPLAB Sim only, returns increasing values
		mov 	#0xFF, W0
		and 	W14, W0, W1				;Low 8 bits are valid. W1 contains received byte.
		inc		W14, W14
		return
*/

		;Mix content of WH and WL in W1 so that W1 = (WH|WL)>>4
		.macro	ShortAddr, WH, WL		;Example: WH|WL = 0xA2460
			lsr		WL, #4, W0			;WL = 0x2460 -> W0 = 0x0246
			sl		WH, #12, W1			;WH = 0x000A -> W1 = 0xA000
			ior		W1, W0, W2			;W1 = 0xA246 i.e. complete address shifted right by 4
		.endm

EraseLastTwoPages:
		;	Erasing page at 0x2A000
		mov    #0x4042, W0
		mov    W0, NVMCON 
		; Init pointer to row to be ERASED
		mov    #tblpage(0x2A000), W0
		mov    W0, TBLPAG ; Initialize PM Page Boundary SFR
		mov    #tbloffset(0x2A000), W0     ; Initialize in-page EA[15:0] pointer
		TBLWTL W0, [W0]     ; Set base address of erase block
		DISI   #5 
		
		mov    #0x55, W0                  
		mov    W0, NVMKEY      
		mov    #0xAA, W1                     
		mov    W1, NVMKEY   
		bset   NVMCON, #WR 
		nop  
		nop 

		;	Waits for erase to be completed
		btsc	NVMCON, #WR		
		bra 	$-2

		;	Erasing page at 0x2A400
		mov    #0x4042, W0
		mov    W0, NVMCON 
		; Init pointer to row to be ERASED
		mov    #tblpage(0x2A400), W0
		mov    W0, TBLPAG ; Initialize PM Page Boundary SFR
		mov    #tbloffset(0x2A400), W0     ; Initialize in-page EA[15:0] pointer
		TBLWTL W0, [W0]     ; Set base address of erase block
		DISI   #5 
		
		mov    #0x55, W0                  
		mov    W0, NVMKEY      
		mov    #0xAA, W1                     
		mov    W1, NVMKEY   
		bset   NVMCON, #WR 
		nop  
		nop 

		;	Clear SPI1STAT register 
		mov		0x0000, W0
		mov		W0, SPI1STAT

		bra 	usrapp
;------------------------------------------------------------------------------
; Uninitialized variables in data memory
;------------------------------------------------------------------------------
		.bss
buffer:	.space ( ROWSIZEW * 3 + 1 ) 	;+1 is for checksum

;------------------------------------------------------------------------------
; End of code
;------------------------------------------------------------------------------
		.end

 
