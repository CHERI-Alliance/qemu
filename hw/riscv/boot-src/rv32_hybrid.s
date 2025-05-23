.section .init
.globl _start
_start:
.option norvc
1:    
    auipc  t0, %pcrel_hi(fw_dyn) 
    addi   a2, t0, %pcrel_lo(1b) 
    csrr   a0, mhartid           
    lw     a1, 32(t0)      
    lw     t2, 24(t0)            
    jr     t0                    
start_addr:  
    .dword 0                     
fdt_laddr: 
    .dword 0                     
fw_dyn:   
    .dword 0                     

