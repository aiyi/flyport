;------------------------------------------------------------------------------
;
; Title:			ds30 loader for PIC24FJ
;
; File description:	Device specific constants
;
; Copyright: 		Copyright © 09-10, Mikael Gustafsson
;
; Webpage: 			http://mrmackey.no-ip.org/elektronik/ds30loader/
;
; History:			1.1.1 New auto generated devices.inc
;						  Added support for 32GA102 and 32GA104
;					1.1.0 Added support for xxGB002, xxGB004
;					1.0.4 -
;					1.0.3 -
;					1.0.2 -
;					1.0.1 Removed some incorrect include for ga002 and ga004
;					1.0.0 Removed PIC24FxxKAyyy stuff, se separate fw
;					0.9.1 Added pps equ
;					0.9.0 First version of this file
;                                                                             
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
;    along with ds30 Loader. If not, see <http:;www.gnu.org/licenses/>.
;------------------------------------------------------------------------------ 


.ifdef	__24FJ256GA106
	.include	"p24FJ256GA106.inc"
	.equ		VALID_DEV,		1
	.equ		FLASHSIZE,		0x2AC00
	.equ		DEVICEID,		238
	.equ		HAS_UART2,		1
	.equ		HAS_UART3,		1
	.equ		HAS_UART4,		1
	.equ		HAS_PPS, 		1
.endif

;------------------------------------------------------------------------------
; 
;------------------------------------------------------------------------------ 
.ifndef VALID_DEV
	.error "Unknown device specified"
.endif
