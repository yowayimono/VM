#include "minvm.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/*
opcode: 表示指令的操作码，是一个 8 位的无符号整数。
size: 表示指令的长度，是一个 size_t 类型的整数，表示该指令占用的字节数。
execute: 是一个函数指针，指向实现该指令功能的函数。这些函数在之前的代码中都有实现。
*/
typedef struct instruction {
    uint8_t   opcode;
    size_t    size;
    bool    (*execute)(TOYVM*);
} instruction;

//print the register values
void Put(TOYVM vm, int idx) {
    printf("register value is the %d\n", vm.cpu.registers[idx]);
}

static bool StackIsEmpty(TOYVM* vm)
{
    return vm->cpu.stack_pointer >= vm->memory_size;
}


static bool StackIsFull(TOYVM* vm)
{
    return vm->cpu.stack_pointer <= vm->stack_limit;
}


static int32_t GetAvailableStackSize(TOYVM* vm)
{
    return vm->cpu.stack_pointer - vm->stack_limit;
}


static int32_t GetOccupiedStackSize(TOYVM* vm)
{
    return vm->memory_size - vm->cpu.stack_pointer;
}


static bool CanPerformMultipush(TOYVM* vm)
{
    return GetAvailableStackSize(vm) >= sizeof(int32_t) * N_REGISTERS;
}

/*******************************************************************************
* Returns 'true' if the stack can provide data for all registers.              *
*******************************************************************************/
static bool CanPerformMultipop(TOYVM* vm)
{
    return GetOccupiedStackSize(vm) >= sizeof(int32_t) * N_REGISTERS;
}

/*******************************************************************************
* Returns 'true' if the instructoin does not run over the memory.              *
*******************************************************************************/
static bool InstructionFitsInMemory(TOYVM* vm, uint8_t opcode);

/*******************************************************************************
* Returns the length of the instruction with opcode 'opcode'.                  *
*******************************************************************************/
static size_t GetInstructionLength(TOYVM* vm, uint8_t opcode);

void InitializeVM(TOYVM* vm, int32_t memory_size, int32_t stack_limit)
{
    /* Make sure both 'memory_size' and 'stack_limit' are divisible by 4. */
    memory_size
    += sizeof(int32_t) - (memory_size % sizeof(int32_t));
    
    stack_limit
    += sizeof(int32_t) - (stack_limit % sizeof(int32_t));
    
    //分配内存空间并赋值0，每个元素大小都是uint8_t
    uint8_t* memory = (uint8_t*)calloc(memory_size, sizeof(uint8_t));
    vm->memory = memory;
    vm->memory_size         = memory_size;
    vm->stack_limit         = stack_limit;
    vm->cpu.program_counter = 0;
    vm->cpu.stack_pointer   = (int32_t) memory_size;
    
    /***************************************************************************
    * Zero out all status flags.                                               *
    ***************************************************************************/
    vm->cpu.status.BAD_ACCESS       = 0;
    vm->cpu.status.COMPARISON_ABOVE = 0;
    vm->cpu.status.COMPARISON_EQUAL = 0;
    vm->cpu.status.COMPARISON_BELOW = 0;
    
    vm->cpu.status.BAD_INSTRUCTION        = 0;
    vm->cpu.status.INVALID_REGISTER_INDEX = 0;
    vm->cpu.status.STACK_OVERFLOW         = 0;
    vm->cpu.status.STACK_UNDERFLOW        = 0;
    
    //清空寄存器和操作码映射表
    memset(vm->cpu.registers, 0, sizeof(int32_t) * N_REGISTERS);
    memset(vm->opcode_map, 0, sizeof(vm->opcode_map));
    
    
    //初始化操作码映射表，把指令映射到操作码
    vm->opcode_map[ADD] = 1;
    vm->opcode_map[NEG] = 2;
    vm->opcode_map[MUL] = 3;
    vm->opcode_map[DIV] = 4;
    vm->opcode_map[MOD] = 5;
    
    vm->opcode_map[CMP] = 6;
    vm->opcode_map[JA]  = 7;
    vm->opcode_map[JE]  = 8;
    vm->opcode_map[JB]  = 9;
    vm->opcode_map[JMP] = 10;
    
    vm->opcode_map[CALL] = 11;
    vm->opcode_map[RET]  = 12;
    
    vm->opcode_map[LOAD]   = 13;
    vm->opcode_map[STORE]  = 14;
    vm->opcode_map[CONST]  = 15;
    vm->opcode_map[RLOAD]  = 16;
    vm->opcode_map[RSTORE] = 17;
    
    vm->opcode_map[HALT] = 18;
    vm->opcode_map[INT]  = 19;
    vm->opcode_map[NOP]  = 20;
    
    vm->opcode_map[PUSH]     = 21;
    vm->opcode_map[PUSH_ALL] = 22;
    vm->opcode_map[POP]      = 23;
    vm->opcode_map[POP_ALL]  = 24;
    vm->opcode_map[LSP]      = 25;
    
}


//把一段内存写（拷贝）到虚拟机中
void WriteVMMemory(TOYVM* vm, uint8_t* mem, size_t size)
{
    memcpy(mem, vm->memory, size);
}


/********************************
这个函数用于从虚拟机内存的指定地址读取一个32位的有符号整数。
函数首先从虚拟机内存中读取4个字节，然后按照小端序（Little 
Endian）的方式将这4个字节组合成一个32位整数。
*********************************/
static int32_t ReadWord(TOYVM* vm, int32_t address)
{
    uint8_t b1 = vm->memory[address];
    uint8_t b2 = vm->memory[address + 1];
    uint8_t b3 = vm->memory[address + 2];
    uint8_t b4 = vm->memory[address + 3];
    
   
    return (int32_t)((b4 << 24) | (b3 << 16) | (b2 << 8) | b1);
}


/*************************************************
 *这个函数用于将一个32位的有符号整数写入虚拟机内存的指定
 地址。函数首先将整数值按照小端序拆分为4个字节，然后分别
 写入虚拟机内存的指定地址和其后三个地址。
*************************************************/
void WriteWord(TOYVM* vm, int32_t address, int32_t value)
{
    uint8_t b1 =  value & 0xff;
    uint8_t b2 = (value & 0xff00) >> 8;
    uint8_t b3 = (value & 0xff0000) >> 16;
    uint8_t b4 = (value & 0xff000000) >> 24;
    
    vm->memory[address]     = b1;
    vm->memory[address + 1] = b2;
    vm->memory[address + 2] = b3;
    vm->memory[address + 3] = b4;
}


//这个函数用于从虚拟机内存的指定地址读取一个8位的无符号整数（字节）。
static uint8_t ReadByte(TOYVM* vm, size_t address)
{
    return vm->memory[address];
}

/*******************************************************************************
这个函数用于从虚拟机的栈中弹出一个32位的有符号整数（一个字）。首先，函数检查栈是否为空（
使用StackIsEmpty函数）。如果栈为空，则设置BAD_ACCESS标志位为1，表示发生了内存访问错误
并返回0。否则，从栈顶读取一个32位整数，并将栈指针增加4个字节，指向下一个位置，然后返回读
取的整数值。                                                         *
*******************************************************************************/
static int32_t PopVM(TOYVM* vm)
{
    if (StackIsEmpty(vm))
    {
        vm->cpu.status.BAD_ACCESS = 1;
        return 0;
    }
    
    int32_t word = ReadWord(vm, vm->cpu.stack_pointer);
    vm->cpu.stack_pointer += 4;
    return word;
}

/*******************************************************************************
这个函数用于将一个32位的无符号整数（一个字）压入虚拟机的栈中。函数首先使用WriteWord函数
将整数值写入栈顶位置，然后将栈指针减少4个字节，指向下一个空闲位置                                            *
*******************************************************************************/
static void PushVM(TOYVM* vm, uint32_t value)
{
    WriteWord(vm, vm->cpu.stack_pointer -= 4, value);
}
/******************************************************************************
 * 这个函数用于检查给定的字节值（操作码中的寄存器索引）是否有效。TOYVM虚拟机有4个寄存器
 * （REG1、REG2、REG3、REG4），这个函数检查给定的字节值是否与其中一个寄存器的索引相匹配。
 * 如果是有效的寄存器索引，返回true，否则返回false。
******************************************************************************/
static bool IsValidRegisterIndex(uint8_t byte)
{
    switch (byte)
    {
        case REG1:
        case REG2:
        case REG3:
        case REG4:
            return true;
    }
    
    return false;
}


//这个函数用于获取虚拟机当前的程序计数器值（program_counter），即下一条待执行指令的地址。
static int32_t GetProgramCounter(TOYVM* vm)
{
    return vm->cpu.program_counter;
}

//用于执行ADD指令
static bool ExecuteAdd(TOYVM* vm)
{
    uint8_t source_register_index;//源寄存器索引
    uint8_t target_register_index;//目标寄存器索引
    
    if (!InstructionFitsInMemory(vm, ADD))
    {
        vm->cpu.status.BAD_ACCESS = 1;
        return true;
    }
    
    source_register_index = ReadByte(vm, GetProgramCounter(vm) + 1);
    target_register_index = ReadByte(vm, GetProgramCounter(vm) + 2);
    
/*
 检查读取的源寄存器索引和目标寄存器索引是否有效。如果任何一个索引无效，即不是合法的寄存器索引，
 将INVALID_REGISTER_INDEX标志位设置为1，并返回true，表示执行失败。
*/

    if (!IsValidRegisterIndex(source_register_index) ||
        !IsValidRegisterIndex(target_register_index))
    {
        vm->cpu.status.INVALID_REGISTER_INDEX = 1;
        return true;
    }
    
    vm->cpu.registers[target_register_index]
    += vm->cpu.registers[source_register_index];
    
    //将程序计数器（program_counter）前进到下一条指令的地址，跳过当前ADD指令。
    vm->cpu.program_counter += GetInstructionLength(vm, ADD);
    return false;
}


//跟上一个指令大同小异，作用是取反
static bool ExecuteNeg(TOYVM* vm)
{
    if (!InstructionFitsInMemory(vm, NEG))//检查当前指令是否能正常执行
    {
        vm->cpu.status.BAD_ACCESS = 1;
        return true;
    }
    
    uint8_t register_index = ReadByte(vm, GetProgramCounter(vm) + 1);//读取寄存器索引
    
    if (!IsValidRegisterIndex(register_index))
    {
        vm->cpu.status.INVALID_REGISTER_INDEX = 1;
        return true;
    }
    
    vm->cpu.registers[register_index] = -vm->cpu.registers[register_index];
    vm->cpu.program_counter += GetInstructionLength(vm, NEG);//跳过当前执行完的指令
    return false;
}


//将两个寄存器的值相乘，结果存到目标寄存器步骤大同小异
static bool ExecuteMul(TOYVM* vm)
{
    uint8_t source_register_index;
    uint8_t target_register_index;
    
    if (!InstructionFitsInMemory(vm, MUL))
    {
        vm->cpu.status.BAD_ACCESS = 1;
        return true;
    }
    
    source_register_index = ReadByte(vm, GetProgramCounter(vm) + 1);
    target_register_index = ReadByte(vm, GetProgramCounter(vm) + 2);
    
    if (!IsValidRegisterIndex(source_register_index) ||
        !IsValidRegisterIndex(target_register_index))
    {
        vm->cpu.status.INVALID_REGISTER_INDEX = 1;
        return true;
    }
    
    vm->cpu.registers[target_register_index] *=
    vm->cpu.registers[source_register_index];
    /* Advance the program counter past this instruction. */
    vm->cpu.program_counter += GetInstructionLength(vm, MUL);
    return false;
}


//两数相除
static bool ExecuteDiv(TOYVM* vm)
{
    uint8_t source_register_index;
    uint8_t target_register_index;
    
    if (!InstructionFitsInMemory(vm, DIV))
    {
        vm->cpu.status.BAD_ACCESS = 1;
        return true;
    }
    
    source_register_index = ReadByte(vm, GetProgramCounter(vm) + 1);
    target_register_index = ReadByte(vm, GetProgramCounter(vm) + 2);
    
    if (!IsValidRegisterIndex(source_register_index) ||
        !IsValidRegisterIndex(target_register_index))
    {
        vm->cpu.status.INVALID_REGISTER_INDEX = 1;
        return true;
    }
    
    vm->cpu.registers[target_register_index] /=
    vm->cpu.registers[source_register_index];
    /* Advance the program counter past this instruction. */
    vm->cpu.program_counter += GetInstructionLength(vm, DIV);
    return false;
}

//取模指令
static bool ExecuteMod(TOYVM* vm)
{
    uint8_t source_register_index;
    uint8_t target_register_index;
    
    if (!InstructionFitsInMemory(vm, MOD))
    {
        vm->cpu.status.BAD_ACCESS = 1;
        return true;
    }
    
    source_register_index = ReadByte(vm, GetProgramCounter(vm) + 1);
    target_register_index = ReadByte(vm, GetProgramCounter(vm) + 2);
    
    if (!IsValidRegisterIndex(source_register_index) ||
        !IsValidRegisterIndex(target_register_index))
    {
        vm->cpu.status.INVALID_REGISTER_INDEX = 1;
        return true;
    }
    
    vm->cpu.registers[target_register_index] =
    vm->cpu.registers[source_register_index] %
    vm->cpu.registers[target_register_index];
    
    /* Advance the program counter past this instruction. */
    vm->cpu.program_counter += GetInstructionLength(vm, MOD);
    return false;
}


//比较指令

static bool ExecuteCmp(TOYVM* vm)
{
    if (!InstructionFitsInMemory(vm, CMP))
    {
        vm->cpu.status.BAD_ACCESS = 1;
        return true;
    }
    
    uint8_t register_index_1 = ReadByte(vm, GetProgramCounter(vm) + 1);
    uint8_t register_index_2 = ReadByte(vm, GetProgramCounter(vm) + 2);
    
    if (!IsValidRegisterIndex(register_index_1) ||
        !IsValidRegisterIndex(register_index_2))
    {
        vm->cpu.status.INVALID_REGISTER_INDEX = 1;
        return true;
    }
    
    int32_t register_1 = vm->cpu.registers[register_index_1];
    int32_t register_2 = vm->cpu.registers[register_index_2];
    
    if (register_1 < register_2)
    {
        vm->cpu.status.COMPARISON_ABOVE = 0;
        vm->cpu.status.COMPARISON_EQUAL = 0;
        vm->cpu.status.COMPARISON_BELOW = 1;
    }
    else if (register_1 > register_2)
    {
        vm->cpu.status.COMPARISON_ABOVE = 1;
        vm->cpu.status.COMPARISON_EQUAL = 0;
        vm->cpu.status.COMPARISON_BELOW = 0;
    }
    else
    {
        vm->cpu.status.COMPARISON_ABOVE = 0;
        vm->cpu.status.COMPARISON_EQUAL = 1;
        vm->cpu.status.COMPARISON_BELOW = 0;
    }
    
    vm->cpu.program_counter += GetInstructionLength(vm, CMP);
    return false;
}


static bool ExecuteJumpIfAbove(TOYVM* vm)
{
    if (!InstructionFitsInMemory(vm, JA))
    {
        vm->cpu.status.BAD_ACCESS = 1;
        return true;
    }
    
    if (vm->cpu.status.COMPARISON_ABOVE)
    {
        //跳转到下个内存位置所存地址
        vm->cpu.program_counter = ReadWord(vm, GetProgramCounter(vm) + 1);
    }
    else
    {
        vm->cpu.program_counter += GetInstructionLength(vm, JA);
    }
    
    return false;
}

static bool ExecuteJumpIfEqual(TOYVM* vm)
{
    if (!InstructionFitsInMemory(vm, JE))
    {
        vm->cpu.status.BAD_ACCESS = 1;
        return true;
    }
    
    if (vm->cpu.status.COMPARISON_EQUAL)
    {
        //跳转到下个内存位置所存地址
        vm->cpu.program_counter = ReadWord(vm, GetProgramCounter(vm) + 1);
    }
    else
    {
        vm->cpu.program_counter += GetInstructionLength(vm, JE);
    }
    
    return false;
}

static bool ExecuteJumpIfBelow(TOYVM* vm)
{
    if (!InstructionFitsInMemory(vm, JB))
    {
        vm->cpu.status.BAD_ACCESS = 1;
        return true;
    }
    
    if (vm->cpu.status.COMPARISON_BELOW)
    {
        //跳转到下个内存位置所存地址
        vm->cpu.program_counter = ReadWord(vm, GetProgramCounter(vm) + 1);
    }
    else
    {
        vm->cpu.program_counter += GetInstructionLength(vm, JB);
    }
    
    return false;
}

static bool ExecuteJump(TOYVM* vm)
{
    if (!InstructionFitsInMemory(vm, JMP))
    {
        vm->cpu.status.BAD_ACCESS = 1;
        return true;
    }
    
    //跳转指令
    vm->cpu.program_counter = ReadWord(vm, GetProgramCounter(vm) + 1);
    return false;
}

static bool ExecuteCall(TOYVM* vm)
{
    if (!InstructionFitsInMemory(vm, CALL))
    {
        vm->cpu.status.BAD_ACCESS = 1;
        return true;
    }
    
    if (GetAvailableStackSize(vm) < 4)
    {
        vm->cpu.status.STACK_OVERFLOW = 1;
        return true;
    }
    
    //读取需要调用的函数起始地址,保存当前执行位置到堆栈
    uint32_t address = ReadWord(vm, GetProgramCounter(vm) + 1);
    PushVM(vm, (uint32_t)(GetProgramCounter(vm) +
                          GetInstructionLength(vm, CALL)));
    //跳转
    vm->cpu.program_counter = address;
    return false;
}


//函数执行结束，返回调用函数的地方
static bool ExecuteRet(TOYVM* vm)
{
    if (!InstructionFitsInMemory(vm, RET))
    {
        vm->cpu.status.BAD_ACCESS = 1;
        return true;
    }
    
    if (StackIsEmpty(vm))
    {
        vm->cpu.status.STACK_UNDERFLOW = 1;
        return true;
    }
    
    vm->cpu.program_counter = PopVM(vm);
    return false;
}


//把内存中的数据读到指定寄存器
static bool ExecuteLoad(TOYVM* vm)
{
    if (!InstructionFitsInMemory(vm, LOAD))
    {
        vm->cpu.status.BAD_ACCESS = 1;
        return true;
    }
    
    //读取寄存器索引
    uint8_t register_index = ReadByte(vm, GetProgramCounter(vm) + 1);
    
    if (!IsValidRegisterIndex(register_index))
    {
        vm->cpu.status.INVALID_REGISTER_INDEX = 1;
        return true;
    }
    


    uint32_t address = ReadWord(vm, GetProgramCounter(vm) + 2);
    vm->cpu.registers[register_index] = ReadWord(vm, address);
    vm->cpu.program_counter += GetInstructionLength(vm, LOAD);
    return false;
}


//把指定寄存器的值写入指定的内存
static bool ExecuteStore(TOYVM* vm)
{
    if (!InstructionFitsInMemory(vm, STORE))
    {
        vm->cpu.status.BAD_ACCESS = 1;
        return true;
    }
    
    uint8_t register_index = ReadByte(vm, GetProgramCounter(vm) + 1);
    
    if (!IsValidRegisterIndex(register_index))
    {
        vm->cpu.status.INVALID_REGISTER_INDEX = 1;
        return true;
    }
    
    uint32_t address = ReadWord(vm, GetProgramCounter(vm) + 2);
    WriteWord(vm, address, vm->cpu.registers[register_index]);
    vm->cpu.program_counter += GetInstructionLength(vm, STORE);
    return false;
}


//把一个常量值写入指定寄存器
static bool ExecuteConst(TOYVM* vm)
{
    if (!InstructionFitsInMemory(vm, CONST))
    {
        vm->cpu.status.BAD_ACCESS = 1;
        return true;
    }
    
    uint8_t register_index = ReadByte(vm, GetProgramCounter(vm) + 1);
    int32_t datum = ReadWord(vm, GetProgramCounter(vm) + 2);
    
    if (!IsValidRegisterIndex(register_index))
    {
        vm->cpu.status.INVALID_REGISTER_INDEX = 1;
        return true;
    }
    
    vm->cpu.registers[register_index] = datum;
    vm->cpu.program_counter += GetInstructionLength(vm, CONST);
    return false;
}

//从内存指定地址读取数据到寄存器
static bool ExecuteRload(TOYVM* vm)
{
    if (!InstructionFitsInMemory(vm, RLOAD))
    {
        vm->cpu.status.BAD_ACCESS = 1;
        return true;
    }
    
    uint8_t address_register_index = ReadByte(vm, GetProgramCounter(vm) + 1);
    uint8_t data_register_index    = ReadByte(vm, GetProgramCounter(vm) + 2);
    
    if (!IsValidRegisterIndex(address_register_index)
     || !IsValidRegisterIndex(data_register_index))
    {
        vm->cpu.status.INVALID_REGISTER_INDEX = 1;
    }
    
    vm->cpu.registers[data_register_index] =
        ReadWord(vm, vm->cpu.registers[address_register_index]);
    vm->cpu.program_counter += GetInstructionLength(vm, RLOAD);
    return false;
}

//将寄存器中的数据存储到内存地址寄存器所指定的内存位置
static bool ExecuteRstore(TOYVM* vm)
{
    if (!InstructionFitsInMemory(vm, RSTORE))
    {
        vm->cpu.status.BAD_ACCESS = 1;
        return true;
    }
    
    uint8_t source_register_index  = ReadByte(vm, GetProgramCounter(vm) + 1);
    uint8_t address_register_index = ReadByte(vm, GetProgramCounter(vm) + 2);
    
    if (!IsValidRegisterIndex(source_register_index)
     || !IsValidRegisterIndex(address_register_index))
    {
        vm->cpu.status.BAD_ACCESS = 1;
        return true;
    }
    
    WriteWord(vm,
              vm->cpu.registers[address_register_index],
              vm->cpu.registers[source_register_index]);
    
    return false;
}

static void PrintString(TOYVM* vm, uint32_t address)
{
    printf("%s", (const char*)(&vm->memory[address]));
}

static bool ExecuteInterrupt(TOYVM* vm)
{
    if (!InstructionFitsInMemory(vm, INT))
    {
        vm->cpu.status.BAD_ACCESS = 1;
        return true;
    }
    
    uint8_t interrupt_number = ReadByte(vm, GetProgramCounter(vm) + 1);
    
    if (StackIsEmpty(vm))
    {
        vm->cpu.status.STACK_UNDERFLOW = 1;
        return true;
    }
    
    switch (interrupt_number)
    {
        case INTERRUPT_PRINT_INTEGER:
            printf("%d", PopVM(vm));
            break;
            
        case INTERRUPT_PRINT_STRING:
            PrintString(vm, PopVM(vm));
            break;
            
        default:
            return true;
    }
    
    vm->cpu.program_counter += GetInstructionLength(vm, INT);
    return false;
}

static bool ExecutePush(TOYVM* vm)
{
    if (!InstructionFitsInMemory(vm, PUSH))
    {
        vm->cpu.status.BAD_ACCESS = 1;
        return true;
    }
    
    if (StackIsFull(vm))
    {
        return true;
    }
    
    uint8_t register_index = ReadByte(vm, GetProgramCounter(vm) + 1);
    
    if (!IsValidRegisterIndex(register_index))
    {
        vm->cpu.status.INVALID_REGISTER_INDEX = 1;
        return true;
    }
    
    WriteWord(vm,
              vm->cpu.stack_pointer - 4,
              vm->cpu.registers[register_index]);
    
    vm->cpu.stack_pointer -= 4;
    vm->cpu.program_counter += GetInstructionLength(vm, PUSH);
    return false;
}

static bool ExecutePushAll(TOYVM* vm)
{
    if (!InstructionFitsInMemory(vm, PUSH_ALL))
    {
        vm->cpu.status.BAD_ACCESS = 1;
        return true;
    }
    
    if (!CanPerformMultipush(vm))
    {
        vm->cpu.status.STACK_OVERFLOW = 1;
        return true;
    }
    
    WriteWord(vm, vm->cpu.stack_pointer -= 4, vm->cpu.registers[REG1]);
    WriteWord(vm, vm->cpu.stack_pointer -= 4, vm->cpu.registers[REG2]);
    WriteWord(vm, vm->cpu.stack_pointer -= 4, vm->cpu.registers[REG3]);
    WriteWord(vm, vm->cpu.stack_pointer -= 4, vm->cpu.registers[REG4]);
    vm->cpu.program_counter += GetInstructionLength(vm, PUSH_ALL);
    return false;
}

static bool ExecutePop(TOYVM* vm)
{
    if (!InstructionFitsInMemory(vm, POP))
    {
        vm->cpu.status.BAD_ACCESS = 1;
        return true;
    }
    
    if (StackIsEmpty(vm))
    {
        return true;
    }
    
    uint8_t register_index = ReadByte(vm, GetProgramCounter(vm) + 1);
    
    if (!IsValidRegisterIndex(register_index))
    {
        vm->cpu.status.INVALID_REGISTER_INDEX = 1;
        return true;
    }
    
    int32_t datum = ReadWord(vm, vm->cpu.stack_pointer+4);
    vm->cpu.registers[register_index] = datum;
    vm->cpu.stack_pointer += 4;
    vm->cpu.program_counter += GetInstructionLength(vm, POP);
    return false;
}

static bool ExecutePopAll(TOYVM* vm)
{
    if (!InstructionFitsInMemory(vm, POP_ALL))
    {
        vm->cpu.status.BAD_ACCESS = 1;
        return true;
    }
    
    if (!CanPerformMultipop(vm))
    {
        vm->cpu.status.STACK_UNDERFLOW = 1;
        return true;
    }
    
    vm->cpu.registers[REG4] = ReadWord(vm, vm->cpu.stack_pointer);
    vm->cpu.registers[REG3] = ReadWord(vm, vm->cpu.stack_pointer + 4);
    vm->cpu.registers[REG2] = ReadWord(vm, vm->cpu.stack_pointer + 8);
    vm->cpu.registers[REG1] = ReadWord(vm, vm->cpu.stack_pointer + 12);
    vm->cpu.stack_pointer += 16;
    vm->cpu.program_counter += GetInstructionLength(vm, POP_ALL);
    return false;
}

static bool ExecuteLSP(TOYVM* vm)
{
    if (!InstructionFitsInMemory(vm, LSP))
    {
        vm->cpu.status.BAD_ACCESS = 1;
        return true;
    }
    
    uint8_t register_index = ReadByte(vm, GetProgramCounter(vm) + 1);
    
    if (!IsValidRegisterIndex(register_index))
    {
        vm->cpu.status.INVALID_REGISTER_INDEX = 1;
        return true;
    }
    
    vm->cpu.registers[register_index] = vm->cpu.stack_pointer;
    vm->cpu.program_counter += GetInstructionLength(vm, LSP);
    return false;
}

static bool ExecuteNop(TOYVM* vm) {
    if (!InstructionFitsInMemory(vm, NOP))
    {
        vm->cpu.status.BAD_ACCESS = 1;
        return true;
    }
    
    vm->cpu.program_counter += GetInstructionLength(vm, NOP);
    return false;
}

static bool ExecuteHalt(TOYVM* vm) {
    return true;
}

void PrintStatus(TOYVM* vm)
{
    printf("BAD_INSTRUCTION       : %d\n", vm->cpu.status.BAD_INSTRUCTION);
    printf("STACK_UNDERFLOW       : %d\n", vm->cpu.status.STACK_UNDERFLOW);
    printf("STACK_OVERFLOW        : %d\n", vm->cpu.status.STACK_OVERFLOW);
    printf("INVALID_REGISTER_INDEX: %d\n",
           vm->cpu.status.INVALID_REGISTER_INDEX);
    
    printf("BAD_ACCESS            : %d\n", vm->cpu.status.BAD_ACCESS);
    printf("COMPARISON_ABOVE      : %d\n", vm->cpu.status.COMPARISON_ABOVE);
    printf("COMPARISON_EQUAL      : %d\n", vm->cpu.status.COMPARISON_EQUAL);
    printf("COMPARISON_BELOW      : %d\n", vm->cpu.status.COMPARISON_BELOW);
}

/*
opcode: 表示指令的操作码，是一个 8 位的无符号整数。
size: 表示指令的长度，是一个 size_t 类型的整数，表示该指令占用的字节数。
execute: 是一个函数指针，指向实现该指令功能的函数。这些函数在之前的代码中都有实现。
*/
const instruction instructions[] = {
    { 0,        0, NULL       },
    { ADD,      3, ExecuteAdd },
    { NEG,      2, ExecuteNeg },
    { MUL,      3, ExecuteMul },
    { DIV,      3, ExecuteDiv },
    { MOD,      3, ExecuteMod },
    
    { CMP,      3, ExecuteCmp },
    { JA,       5, ExecuteJumpIfAbove },
    { JE,       5, ExecuteJumpIfEqual },
    { JB,       5, ExecuteJumpIfBelow },
    { JMP,      5, ExecuteJump },
    
    { CALL,     5, ExecuteCall },
    { RET,      1, ExecuteRet },
    
    { LOAD,     6, ExecuteLoad },
    { STORE,    6, ExecuteStore },
    { CONST,    6, ExecuteConst },
    { RLOAD,    3, ExecuteRload },
    { RSTORE,   3, ExecuteRstore },
    
    { HALT,     1, ExecuteHalt },
    { INT,      2, ExecuteInterrupt },
    { NOP,      1, ExecuteNop },
    
    { PUSH,     2, ExecutePush },
    { PUSH_ALL, 1, ExecutePushAll },
    { POP,      2, ExecutePop },
    { POP_ALL,  1, ExecutePopAll },
    { LSP,      2, ExecuteLSP }
};

static size_t GetInstructionLength(TOYVM* vm, uint8_t opcode)
{
    size_t index = vm->opcode_map[opcode];
    return instructions[index].size;
}

//用于检查指令是否在虚拟机的内存空间中。它接受虚拟机实例 vm 和指令的操作码 opcode 作为参数。
static bool InstructionFitsInMemory(TOYVM* vm, uint8_t opcode)
{
    size_t instruction_length = GetInstructionLength(vm, opcode);
    return vm->cpu.program_counter + instruction_length <= vm->memory_size;
}

void RunVM(TOYVM* vm)
{
    while (true)
    {
        int32_t program_counter = GetProgramCounter(vm);
        
        if (program_counter < 0 || program_counter >= vm->memory_size)
        {
            vm->cpu.status.BAD_ACCESS = 1;
            return;
        }
        
        uint8_t opcode = vm->memory[program_counter];
        size_t index = vm->opcode_map[opcode];
    
        if (index == 0)
        {
            vm->cpu.status.BAD_INSTRUCTION = 1;
            return;
        }
    
        bool (*opcode_exec)(TOYVM*) =
        instructions[index].execute;
    
        if (opcode_exec(vm))
        {
            return;
        }
    }
}

