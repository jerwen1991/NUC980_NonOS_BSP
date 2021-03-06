;/*---------------------------------------------------------------------------------------------------------*/
;/*                                                                                                         */
;/* Copyright(c) 2019 Nuvoton Technology Corp. All rights reserved.                                         */
;/*                                                                                                         */
;/*---------------------------------------------------------------------------------------------------------*/


	.syntax	unified

	.globl	VectorBase_CBC
	.globl	VectorLimit_CBC

	.globl	VectorBase_CFB
	.globl	VectorLimit_CFB

	.globl	VectorBase_ECB
	.globl	VectorLimit_ECB


	.align	4
 
	.text
        
VectorBase_CBC:
    .incbin  "../../test_suite_aes.cbc.data"
VectorLimit_CBC:
    .space   4

VectorBase_CFB:
    .incbin  "../../test_suite_aes.cfb.data"
VectorLimit_CFB:
    .space   4

VectorBase_ECB:
    .incbin  "../../test_suite_aes.ecb.data"
VectorLimit_ECB:
    .space   4
    
    .end
    
    