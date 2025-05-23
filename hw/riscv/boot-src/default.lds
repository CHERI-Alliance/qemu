MEMORY
{
  ram (rwax) : ORIGIN = 0x80000000, LENGTH = 0x8000000
}

SECTIONS {

.init : {
    main.o(.init)
  } >ram
PROVIDE(_memory_start = ORIGIN(ram));
PROVIDE(_memory_end = ORIGIN(ram) + LENGTH(ram));
}