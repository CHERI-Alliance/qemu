.option norvc
.section .init
.globl _start
_start:
1:    
    auipc  ct0, %pcrel_hi(fw_dyn) 
    caddi   ca2, ct0, %pcrel_lo(1b) 
    csrr   a0, mhartid           
    lw     a1, 40(ct0)      
    scaddr ca1, ct0,a1      # create a capability version of fdt_laddr
    lw     t2, 32(ct0)            
    scaddr ct0, ct0, t2
    jr     ct0                    
start_addr:  
    .dword 0                     
fdt_laddr: 
    .dword 0                     
fw_dyn:   
    .dword 0                     

