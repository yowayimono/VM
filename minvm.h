#ifndef MINVM_H
#define MINVM_H

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

enum {
    /* Arithmetics */
    ADD = 0x01,
    NEG = 0x02,
    MUL = 0x03,
    DIV = 0x04,
    MOD = 0x05,
    
    /* Conditionals */
    CMP = 0x10,
    JA  = 0x11,
    JE  = 0x12,
    JB  = 0x13,
    JMP = 0x14,
    
    /* Subroutines */
    CALL = 0x20,
    RET  = 0x21,
    
    /* Moving data */
    LOAD   = 0x30,
    STORE  = 0x31,
    CONST  = 0x32,
    RLOAD  = 0x33,
    RSTORE = 0x34,
    
    /* Auxiliary */
    HALT = 0x40,
    INT  = 0x41,
    NOP  = 0x42,
    
    /* Stack */
    PUSH     = 0x50,
    PUSH_ALL = 0x51,
    POP      = 0x52,
    POP_ALL  = 0x53,
    LSP      = 0x54,
    
    /* Registers */
    REG1 = 0x00,
    REG2 = 0x01,
    REG3 = 0x02,
    REG4 = 0x03,
    
    /* Interupts */
    INTERRUPT_PRINT_INTEGER = 0x01,
    INTERRUPT_PRINT_STRING  = 0x02,
    
    /* Miscellaneous */
    N_REGISTERS = 4,
    
    OPCODE_MAP_SIZE = 256,
};

typedef struct VM_CPU {
    int32_t registers[N_REGISTERS];
    int32_t program_counter;
    int32_t stack_pointer;
    
    struct {
        uint8_t BAD_INSTRUCTION        : 1;
        uint8_t STACK_UNDERFLOW        : 1;
        uint8_t STACK_OVERFLOW         : 1;
        uint8_t INVALID_REGISTER_INDEX : 1;
        uint8_t BAD_ACCESS             : 1;
        uint8_t COMPARISON_BELOW       : 1;
        uint8_t COMPARISON_EQUAL       : 1;
        uint8_t COMPARISON_ABOVE       : 1;
    } status;
} VM_CPU;

typedef struct TOYVM {
    uint8_t* memory;
    int32_t  memory_size;
    int32_t  stack_limit;
    VM_CPU   cpu;
    size_t   opcode_map[OPCODE_MAP_SIZE];
} TOYVM;

/*******************************************************************************
* Initializes the virtual machine with RAM memory of length 'memory_size' and  *
* the stack fence at 'stack_limit'.
*******************************************************************************/
void InitializeVM(TOYVM* vm, int32_t memory_size, int32_t stack_limit);

/*******************************************************************************
* Writes 'size' bytes to the memory of the machine. The write begins from the  *
* beginning of the memory tape.                                                *
*******************************************************************************/
void WriteVMMemory(TOYVM* vm, uint8_t* mem, size_t size);

/*******************************************************************************
* Writes a single word 'value' (32-bit signed integer) at address 'address'.   *
*******************************************************************************/
void WriteWord(TOYVM* vm, int32_t address, int32_t value);

/*******************************************************************************
* Prints the status of the machine to stdout.                                  *
*******************************************************************************/
void PrintStatus(TOYVM* vm);

/*******************************************************************************
* Runs the virtual machine.                                                    *
*******************************************************************************/
void RunVM(TOYVM* vm);

void Put(TOYVM vm, int idx);

#endif /* MINVM_H */
