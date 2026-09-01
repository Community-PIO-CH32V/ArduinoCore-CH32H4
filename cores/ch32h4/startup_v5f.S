/* V5F startup -- the core the sketch runs on.
 *
 * Entered by NVIC_WakeUp_V5F() with no stack, no vector table and no CSRs
 * configured. The clock tree is already up: the V3F programmed every PLL
 * before waking this core, and reconfiguring PLLs underneath a running core is
 * exactly as bad as it sounds, so nothing here calls SystemInit().
 *
 * THIS CORE COPIES NOTHING. In one ELF the two cores share one .data and one
 * .bss, and the V3F -- which runs first -- has already initialised them. A
 * second .bss zero here would wipe the clock state the V3F just established.
 * See the header comment in startup_v3f.S.
 *
 * The vector table and the weak handler list below are WCH's, taken from
 * startup_ch32h417_v5f.S with only the table's own labels renamed. The handler
 * NAMES are deliberately left unprefixed -- USART1_IRQHandler and friends are
 * what a driver expects to define -- because only the V3F's table needs
 * prefixing to avoid the collision that forces two ELFs on other ports. The
 * V3F's table is small and entirely separate; see startup_v3f.S.
 */

    .section .init_v5f, "ax", @progbits
    .global _start_v5f
    .align  2
_start_v5f:
    j handle_reset_v5f

    .section .vector_v5f, "ax", @progbits
    .global _vector_base_v5f
    .align  1
_vector_base_v5f:
    .option norvc;
        .word   _start_v5f
    .word   0
    .word   NMI_Handler              
    .word   HardFault_Handler         
    .word   0
    .word   Ecall_M_Mode_Handler      
    .word   0
    .word   0
    .word   Ecall_U_Mode_Handler      
    .word   Break_Point_Handler      
    .word   0
    .word   0
    .word   SysTick0_Handler          
    .word   SysTick1_Handler           
    .word   SW_Handler
    .word   0
    .word   IPC_CH0_Handler          
    .word   IPC_CH1_Handler          
    .word   IPC_CH2_Handler          
    .word   IPC_CH3_Handler          
    .word   0
    .word   0
    .word   0
    .word   0
    .word   0
    .word   0
    .word   0
    .word   0
    .word   HSEM_Handler
    .word   0
    .word   0
    .word   0
    /* External Interrupts */
    .word   WWDG_IRQHandler            
    .word   EXTI15_8_IRQHandler         
    .word   FLASH_IRQHandler         
    .word   RCC_IRQHandler   
    .word   EXTI7_0_IRQHandler          
    .word   SPI1_IRQHandler           
    .word   DMA1_Channel2_IRQHandler   
    .word   DMA1_Channel3_IRQHandler   
    .word   DMA1_Channel4_IRQHandler   
    .word   DMA1_Channel5_IRQHandler  
    .word   DMA1_Channel6_IRQHandler   
    .word   DMA1_Channel7_IRQHandler   
    .word   DMA1_Channel8_IRQHandler  
    .word   USART2_IRQHandler          
    .word   I2C1_EV_IRQHandler        
    .word   I2C1_ER_IRQHandler         
    .word   USART1_IRQHandler          
    .word   SPI2_IRQHandler           
    .word   SPI3_IRQHandler            
    .word   SPI4_IRQHandler           
    .word   I2C2_EV_IRQHandler      
    .word   I2C2_ER_IRQHandler         
    .word   USBPD_IRQHandler        
    .word   USBPDWakeUp_IRQHandler         
    .word   USBHS_IRQHandler           
    .word   DMA1_Channel1_IRQHandler   
    .word   CAN1_SCE_IRQHandler       
    .word   CAN1_TX_IRQHandler      
    .word   CAN1_RX0_IRQHandler       
    .word   CAN1_RX1_IRQHandler        
    .word   USBSS_IRQHandler          
    .word   USBSS_LINK_IRQHandler           
    .word   USBHSWakeup_IRQHandler     
    .word   USBSSWakeup_IRQHandler    
    .word   RTCAlarm_IRQHandler        
    .word   USBFS_IRQHandler          
    .word   USBFSWakeup_IRQHandler     
    .word   ADC1_2_IRQHandler                                
    .word   TIM1_BRK_IRQHandler              
    .word   TIM1_UP_IRQHandler               
    .word   TIM1_TRG_COM_IRQHandler         
    .word   TIM1_CC_IRQHandler             
    .word   TIM2_IRQHandler                  
    .word   TIM3_IRQHandler                  
    .word   TIM4_IRQHandler                 
    .word   TIM5_IRQHandler                 
    .word   I2C3_EV_IRQHandler               
    .word   I2C3_ER_IRQHandler               
    .word   I2C4_EV_IRQHandler               
    .word   I2C4_ER_IRQHandler              
    .word   QSPI1_IRQHandler              
    .word   SERDES_IRQHandler               
    .word   USART3_IRQHandler              
    .word   USART4_IRQHandler             
    .word   TIM8_BRK_IRQHandler              
    .word   TIM8_UP_IRQHandler               
    .word   TIM8_TRG_COM_IRQHandler          
    .word   TIM8_CC_IRQHandler              
    .word   TIM9_IRQHandler                
    .word   TIM10_IRQHandler               
    .word   TIM11_IRQHandler                
    .word   TIM12_IRQHandler               
    .word   FMC_IRQHandler                  
    .word   SDMMC_IRQHandler                
    .word   LPTIM1_IRQHandler              
    .word   LPTIM2_IRQHandler               
    .word   USART5_IRQHandler               
    .word   USART6_IRQHandler              
    .word   TIM6_IRQHandler                 
    .word   TIM7_IRQHandler                 
    .word   DMA2_Channel1_IRQHandler         
    .word   DMA2_Channel2_IRQHandler         
    .word   DMA2_Channel3_IRQHandler         
    .word   DMA2_Channel4_IRQHandler         
    .word   DMA2_Channel5_IRQHandler        
    .word   DMA2_Channel6_IRQHandler       
    .word   DMA2_Channel7_IRQHandler        
    .word   DMA2_Channel8_IRQHandler         
    .word   ETH_IRQHandler                  
    .word   ETH_WKUP_IRQHandler 
    .word   CAN2_SCE_IRQHandler       
    .word   CAN2_TX_IRQHandler      
    .word   CAN2_RX0_IRQHandler       
    .word   CAN2_RX1_IRQHandler                
    .word   USART7_IRQHandler               
    .word   USART8_IRQHandler             
    .word   I3C_EV_IRQHandler                
    .word   I3C_ER_IRQHandler                
    .word   DVP_IRQHandler                   
    .word   ECDC_IRQHandler                  
    .word   PIOC_IRQHandler               
    .word   SAI_IRQHandler
    .word   LTDC_IRQHandler               
    .word   GPHA_IRQHandler 
    .word   0              
    .word   DFSDM0_IRQHandler              
    .word   DFSDM1_IRQHandler  
    .word   0  
    .word   0             
    .word   SWPMI_IRQHandler   
    .word   0  
    .word   0                   
    .word   QSPI2_IRQHandler              
    .word   SWPMI_WKUP_IRQHandler          
    .word   CAN3_SCE_IRQHandler       
    .word   CAN3_TX_IRQHandler      
    .word   CAN3_RX0_IRQHandler       
    .word   CAN3_RX1_IRQHandler            
    .word   LPTIM2_WKUP_IRQHandler         
    .word   LPTIM1_WKUP_IRQHandler          
    .word   I3C_WKUP_IRQHandler              
    .word   RTC_IRQHandler                    
    .word   HSADC_IRQHandler                  
    .word   UHSIF_IRQHandler                 
    .word   RNG_IRQHandler                    
    .word   SDIO_IRQHandler 
    .word   USART_WKUP_IRQHandler          

    .option rvc;

    .section .text, "ax", @progbits
    .global handle_reset_v5f
    .align  2
handle_reset_v5f:
.option push
.option norelax
    la gp, __global_pointer$
.option pop

    la sp, _estack_v5f

    /* CSR bit flags live in ch32h4_csr.h so both cores share one definition.
     * Only this core's composed register values are spelled out here. */
#include "ch32h4_csr.h"

#define CORECFGR_V5F (CORECFGR_FADD_DIV | CORECFGR_FMUL_DIV | CORECFGR_FMAC_DIV | \
                      CORECFGR_FDIV_DIV | CORECFGR_NLP_EN | CORECFGR_WCH_13_12 | \
                      CORECFGR_WCH_9_8 | CORECFGR_CSTA_FAULT_IE | CORECFGR_WCH_6 | \
                      CORECFGR_IE_REMAP_EN)
#define INTSYSCR_V5F  (INTSYSCR_PMTCFG_3 | INTSYSCR_INESTEN | INTSYSCR_HWSTKEN)

    li t0, CORECFGR_V5F
    csrw 0xBC0, t0
    li t0, INESTCR_NEST_LVL_8
    csrw 0xBC1, t0
    li t0, INTSYSCR_V5F
    csrw 0x804, t0
    li t0, MSTATUS_RUN
    csrw mstatus, t0

    la t0, _vector_base_v5f
    ori t0, t0, MTVEC_MODE_VECT
    csrw mtvec, t0

    /* Turn the instruction cache on, scoped to the flash code and rodata by a
       PMP entry. It is off out of reset and it is worth a measured 145x for
       code executing from flash.

       The sequence is WCH's, from EVT/EXAM/CPU/ICache, and every value in it
       has a trap in it: the csrc clears two blanket region enables as well as
       ic_disable, and IC_Str is not the bit the manual says. ch32h4_csr.h
       carries the reasoning for each, with the measurements.

       PMP channel 1, TOR: pmpaddr0 is the base, pmpaddr1 the top, both
       address >> 2. */
    la t0, _cache_beg
    srli t0, t0, 2
    csrw pmpaddr0, t0
    la t0, _cache_end
    srli t0, t0, 2
    csrw pmpaddr1, t0

    li t0, CACHE_PMP1_STRTG
    csrw 0xbc3, t0
    li t0, PMPCFG0_V5F
    csrw 0x3a0, t0
    li t0, OPCACHE_INVAL
    csrw 0xbd0, t0
    li t0, CACHE_V5F_CSRC
    csrc 0xbc2, t0

    la t0, ch32h4_v5f_main
    csrw mepc, t0
    mret

/* WCH's weak default handlers: every one of these is a label on a spin loop,
   so an unhandled interrupt stops visibly instead of running into whatever
   follows it in memory. A driver overrides the one it wants by defining the
   symbol strongly. */
    .section .text.vector_handler, "ax", @progbits
    .weak   NMI_Handler              
    .weak   HardFault_Handler         
    .weak   Ecall_M_Mode_Handler      
    .weak   Ecall_U_Mode_Handler      
    .weak   Break_Point_Handler      
    .weak   SysTick0_Handler          
    .weak   SysTick1_Handler  
    .weak   SW_Handler         
    .weak   IPC_CH0_Handler          
    .weak   IPC_CH1_Handler          
    .weak   IPC_CH2_Handler          
    .weak   IPC_CH3_Handler  
    .weak   HSEM_Handler        
    .weak   WWDG_IRQHandler            
    .weak   EXTI15_8_IRQHandler         
    .weak   FLASH_IRQHandler         
    .weak   RCC_IRQHandler 
    .weak   EXTI7_0_IRQHandler          
    .weak   SPI1_IRQHandler           
    .weak   DMA1_Channel2_IRQHandler   
    .weak   DMA1_Channel3_IRQHandler   
    .weak   DMA1_Channel4_IRQHandler   
    .weak   DMA1_Channel5_IRQHandler  
    .weak   DMA1_Channel6_IRQHandler   
    .weak   DMA1_Channel7_IRQHandler   
    .weak   DMA1_Channel8_IRQHandler  
    .weak   USART2_IRQHandler          
    .weak   I2C1_EV_IRQHandler        
    .weak   I2C1_ER_IRQHandler         
    .weak   USART1_IRQHandler          
    .weak   SPI2_IRQHandler           
    .weak   SPI3_IRQHandler            
    .weak   SPI4_IRQHandler           
    .weak   I2C2_EV_IRQHandler      
    .weak   I2C2_ER_IRQHandler         
    .weak   USBPD_IRQHandler        
    .weak   USBPDWakeUp_IRQHandler         
    .weak   USBHS_IRQHandler           
    .weak   DMA1_Channel1_IRQHandler   
    .weak   CAN1_SCE_IRQHandler       
    .weak   CAN1_TX_IRQHandler      
    .weak   CAN1_RX0_IRQHandler 
    .weak   CAN1_RX1_IRQHandler           
    .weak   USBSS_IRQHandler          
    .weak   USBSS_LINK_IRQHandler           
    .weak   USBHSWakeup_IRQHandler     
    .weak   USBSSWakeup_IRQHandler    
    .weak   RTCAlarm_IRQHandler        
    .weak   USBFS_IRQHandler          
    .weak   USBFSWakeup_IRQHandler     
    .weak   ADC1_2_IRQHandler                                
    .weak   TIM1_BRK_IRQHandler              
    .weak   TIM1_UP_IRQHandler               
    .weak   TIM1_TRG_COM_IRQHandler         
    .weak   TIM1_CC_IRQHandler             
    .weak   TIM2_IRQHandler                  
    .weak   TIM3_IRQHandler                  
    .weak   TIM4_IRQHandler                 
    .weak   TIM5_IRQHandler                 
    .weak   I2C3_EV_IRQHandler               
    .weak   I2C3_ER_IRQHandler               
    .weak   I2C4_EV_IRQHandler               
    .weak   I2C4_ER_IRQHandler              
    .weak   QSPI1_IRQHandler              
    .weak   SERDES_IRQHandler               
    .weak   USART3_IRQHandler              
    .weak   USART4_IRQHandler             
    .weak   TIM8_BRK_IRQHandler              
    .weak   TIM8_UP_IRQHandler               
    .weak   TIM8_TRG_COM_IRQHandler          
    .weak   TIM8_CC_IRQHandler              
    .weak   TIM9_IRQHandler                
    .weak   TIM10_IRQHandler               
    .weak   TIM11_IRQHandler                
    .weak   TIM12_IRQHandler               
    .weak   FMC_IRQHandler                  
    .weak   SDMMC_IRQHandler                
    .weak   LPTIM1_IRQHandler              
    .weak   LPTIM2_IRQHandler               
    .weak   USART5_IRQHandler               
    .weak   USART6_IRQHandler              
    .weak   TIM6_IRQHandler                 
    .weak   TIM7_IRQHandler                 
    .weak   DMA2_Channel1_IRQHandler         
    .weak   DMA2_Channel2_IRQHandler         
    .weak   DMA2_Channel3_IRQHandler         
    .weak   DMA2_Channel4_IRQHandler         
    .weak   DMA2_Channel5_IRQHandler        
    .weak   DMA2_Channel6_IRQHandler       
    .weak   DMA2_Channel7_IRQHandler        
    .weak   DMA2_Channel8_IRQHandler         
    .weak   ETH_IRQHandler                  
    .weak   ETH_WKUP_IRQHandler  
    .weak   CAN2_SCE_IRQHandler       
    .weak   CAN2_TX_IRQHandler      
    .weak   CAN2_RX0_IRQHandler 
    .weak   CAN2_RX1_IRQHandler                
    .weak   USART7_IRQHandler               
    .weak   USART8_IRQHandler             
    .weak   I3C_EV_IRQHandler                
    .weak   I3C_ER_IRQHandler                
    .weak   DVP_IRQHandler                   
    .weak   ECDC_IRQHandler                                   
    .weak   PIOC_IRQHandler               
    .weak   SAI_IRQHandler
    .weak   LTDC_IRQHandler               
    .weak   GPHA_IRQHandler               
    .weak   DFSDM0_IRQHandler              
    .weak   DFSDM1_IRQHandler              
    .weak   SWPMI_IRQHandler                 
    .weak   QSPI2_IRQHandler              
    .weak   SWPMI_WKUP_IRQHandler          
    .weak   CAN3_SCE_IRQHandler           
    .weak   CAN3_TX_IRQHandler             
    .weak   CAN3_RX0_IRQHandler 
    .weak   CAN3_RX1_IRQHandler                    
    .weak   LPTIM2_WKUP_IRQHandler         
    .weak   LPTIM1_WKUP_IRQHandler          
    .weak   I3C_WKUP_IRQHandler              
    .weak   RTC_IRQHandler                    
    .weak   HSADC_IRQHandler                  
    .weak   UHSIF_IRQHandler                 
    .weak   RNG_IRQHandler                    
    .weak   SDIO_IRQHandler 
    .weak   USART_WKUP_IRQHandler   

NMI_Handler:              
HardFault_Handler:         
Ecall_M_Mode_Handler:      
Ecall_U_Mode_Handler:      
Break_Point_Handler:      
SysTick0_Handler:          
SysTick1_Handler: 
SW_Handler:          
IPC_CH0_Handler:          
IPC_CH1_Handler:          
IPC_CH2_Handler:          
IPC_CH3_Handler: 
HSEM_Handler:         
WWDG_IRQHandler:            
EXTI15_8_IRQHandler:         
FLASH_IRQHandler:         
RCC_IRQHandler:   
EXTI7_0_IRQHandler:          
SPI1_IRQHandler:           
DMA1_Channel2_IRQHandler:   
DMA1_Channel3_IRQHandler:   
DMA1_Channel4_IRQHandler:   
DMA1_Channel5_IRQHandler:  
DMA1_Channel6_IRQHandler:   
DMA1_Channel7_IRQHandler:   
DMA1_Channel8_IRQHandler:  
USART2_IRQHandler:          
I2C1_EV_IRQHandler:        
I2C1_ER_IRQHandler:         
USART1_IRQHandler:          
SPI2_IRQHandler:           
SPI3_IRQHandler:            
SPI4_IRQHandler:           
I2C2_EV_IRQHandler:      
I2C2_ER_IRQHandler:         
USBPD_IRQHandler:        
USBPDWakeUp_IRQHandler:         
USBHS_IRQHandler:           
DMA1_Channel1_IRQHandler:   
CAN1_SCE_IRQHandler:       
CAN1_TX_IRQHandler:      
CAN1_RX0_IRQHandler: 
CAN1_RX1_IRQHandler:             
USBSS_IRQHandler:          
USBSS_LINK_IRQHandler:           
USBHSWakeup_IRQHandler:     
USBSSWakeup_IRQHandler:    
RTCAlarm_IRQHandler:        
USBFS_IRQHandler:          
USBFSWakeup_IRQHandler:     
ADC1_2_IRQHandler:                                
TIM1_BRK_IRQHandler:              
TIM1_UP_IRQHandler:               
TIM1_TRG_COM_IRQHandler:         
TIM1_CC_IRQHandler:             
TIM2_IRQHandler:                  
TIM3_IRQHandler:                  
TIM4_IRQHandler:                 
TIM5_IRQHandler:                 
I2C3_EV_IRQHandler:               
I2C3_ER_IRQHandler:               
I2C4_EV_IRQHandler:               
I2C4_ER_IRQHandler:              
QSPI1_IRQHandler:              
SERDES_IRQHandler:               
USART3_IRQHandler:              
USART4_IRQHandler:             
TIM8_BRK_IRQHandler:              
TIM8_UP_IRQHandler:               
TIM8_TRG_COM_IRQHandler:          
TIM8_CC_IRQHandler:              
TIM9_IRQHandler:                
TIM10_IRQHandler:               
TIM11_IRQHandler:                
TIM12_IRQHandler:               
FMC_IRQHandler:                  
SDMMC_IRQHandler:                
LPTIM1_IRQHandler:              
LPTIM2_IRQHandler:               
USART5_IRQHandler:               
USART6_IRQHandler:              
TIM6_IRQHandler:                 
TIM7_IRQHandler:                 
DMA2_Channel1_IRQHandler:         
DMA2_Channel2_IRQHandler:         
DMA2_Channel3_IRQHandler:         
DMA2_Channel4_IRQHandler:         
DMA2_Channel5_IRQHandler:        
DMA2_Channel6_IRQHandler:       
DMA2_Channel7_IRQHandler:        
DMA2_Channel8_IRQHandler:         
ETH_IRQHandler:                  
ETH_WKUP_IRQHandler:   
CAN2_SCE_IRQHandler:       
CAN2_TX_IRQHandler:      
CAN2_RX0_IRQHandler: 
CAN2_RX1_IRQHandler:           
USART7_IRQHandler:               
USART8_IRQHandler:             
I3C_EV_IRQHandler:                
I3C_ER_IRQHandler:                
DVP_IRQHandler:                   
ECDC_IRQHandler:                  
PIOC_IRQHandler:               
SAI_IRQHandler:
LTDC_IRQHandler:               
GPHA_IRQHandler:               
DFSDM0_IRQHandler:              
DFSDM1_IRQHandler:              
SWPMI_IRQHandler:                 
QSPI2_IRQHandler:              
SWPMI_WKUP_IRQHandler:          
CAN3_SCE_IRQHandler:           
CAN3_TX_IRQHandler:             
CAN3_RX0_IRQHandler: 
CAN3_RX1_IRQHandler:                       
LPTIM2_WKUP_IRQHandler:         
LPTIM1_WKUP_IRQHandler:          
I3C_WKUP_IRQHandler:              
RTC_IRQHandler:                    
HSADC_IRQHandler:                  
UHSIF_IRQHandler:                 
RNG_IRQHandler:                    
SDIO_IRQHandler: 
USART_WKUP_IRQHandler:
1:
	j 1b

