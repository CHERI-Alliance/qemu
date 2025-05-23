# hybrid code is just the same as non-cheri

.option norvc
.section .init
.globl _start
_start:
1:    
    auipc  t0, %pcrel_hi(fw_dyn) 
    addi   a2, t0, %pcrel_lo(1b) 
    csrr   a0, mhartid           
    ld     a1, 32(t0)      
    ld     t2, 24(t0)            
    jr     t0                    
start_addr:  
    .dword 0                     
fdt_laddr: 
    .dword 0                     
fw_dyn:   
    .dword 0                     

