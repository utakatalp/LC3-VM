#include <stdio.h>
#include <stdint.h>
#include <signal.h>
/* windows only */
#include <Windows.h>
#include <conio.h> // _kbhit

#define MEMORY_MAX (1 << 16)
uint16_t memory[MEMORY_MAX]; /* 65536 locations */

// Register definitions
enum
{
    R_R0 = 0,
    R_R1,
    R_R2,
    R_R3,
    R_R4,
    R_R5,
    R_R6,
    R_R7,
    R_PC, /* program counter */
    R_COND,
    R_COUNT
};

// Condition flags
enum
{
    FL_POS = 1 << 0, /* P */
    FL_ZRO = 1 << 1, /* Z */
    FL_NEG = 1 << 2, /* N */
};

uint16_t reg[R_COUNT];
// Instruction opcodes
enum
{
    OP_BR = 0, /* branch */
    OP_ADD,    /* add  */
    OP_LD,     /* load */
    OP_ST,     /* store */
    OP_JSR,    /* jump register */
    OP_AND,    /* bitwise and */
    OP_LDR,    /* load register */
    OP_STR,    /* store register */
    OP_RTI,    /* unused */
    OP_NOT,    /* bitwise not */
    OP_LDI,    /* load indirect */
    OP_STI,    /* store indirect */
    OP_JMP,    /* jump */
    OP_RES,    /* reserved (unused) */
    OP_LEA,    /* load effective address */
    OP_TRAP    /* execute trap */
};
enum
{
    TRAP_GETC = 0x20,  /* get character from keyboard, not echoed onto the terminal */
    TRAP_OUT = 0x21,   /* output a character */
    TRAP_PUTS = 0x22,  /* output a word string */
    TRAP_IN = 0x23,    /* get character from keyboard, echoed onto the terminal */
    TRAP_PUTSP = 0x24, /* output a byte string */
    TRAP_HALT = 0x25   /* halt the program */
};

enum {
    FLAG_POS = 1 << 0,
    FLAG_ZRO = 1 << 1,
    FLAG_NEG = 1 << 2, 
};
enum
{
    MR_KBSR = 0xFE00, /* keyboard status */
    MR_KBDR = 0xFE02  /* keyboard data */
};

HANDLE hStdin = INVALID_HANDLE_VALUE;
DWORD fdwMode, fdwOldMode;

void update_flags(uint16_t r)
{
    if (reg[r] == 0)
    {
        reg[R_COND] = FL_ZRO;
    }
    else if (reg[r] >> 15) /* a 1 in the left-most bit indicates negative */
    {
        reg[R_COND] = FL_NEG;
    }
    else
    {
        reg[R_COND] = FL_POS;
    }
}
uint16_t sign_extend(uint16_t x, int bit_count)
{
    if ((x >> (bit_count - 1)) & 1)
    {
        x |= (0xFFFF << bit_count);
    }
    return x;
}

uint16_t swap16(uint16_t x)
{
    return (x << 8) | (x >> 8);
}
uint16_t check_key()
{
    return WaitForSingleObject(hStdin, 1000) == WAIT_OBJECT_0 && _kbhit();
}
void read_image_file(FILE *file)
{
    /* the origin tells us where in memory to place the image */
    uint16_t origin;
    fread(&origin, sizeof(origin), 1, file);
    origin = swap16(origin);

    /* we know the maximum file size so we only need one fread */
    uint16_t max_read = MEMORY_MAX - origin;
    uint16_t *p = memory + origin;
    size_t read = fread(p, sizeof(uint16_t), max_read, file);

    /* swap to little endian */
    while (read-- > 0)
    {
        *p = swap16(*p);
        ++p;
    }
}

int read_image(const char *image_path)
{
    FILE *file = fopen(image_path, "rb");
    if (!file)
    {
        return 0;
    };
    read_image_file(file);
    fclose(file);
    return 1;
}

void mem_write(uint16_t address, uint16_t val)
{
    memory[address] = val;
}
uint16_t mem_read(uint16_t address)
{
    if (address == MR_KBSR)
    {
        if (check_key())
        {
            memory[MR_KBSR] = (1 << 15);
            memory[MR_KBDR] = getchar();
        }
        else
        {
            memory[MR_KBSR] = 0;
        }
    }
    return memory[address];
}

void disable_input_buffering()
{
    hStdin = GetStdHandle(STD_INPUT_HANDLE);
    GetConsoleMode(hStdin, &fdwOldMode);     /* save old mode */
    fdwMode = fdwOldMode ^ ENABLE_ECHO_INPUT /* no input echo */
              ^ ENABLE_LINE_INPUT;           /* return when one or
                                                more characters are available */
    SetConsoleMode(hStdin, fdwMode);         /* set new mode */
    FlushConsoleInputBuffer(hStdin);         /* clear buffer */
}

void restore_input_buffering()
{
    SetConsoleMode(hStdin, fdwOldMode);
}

void handle_interrupt(int signal)
{
    restore_input_buffering();
    printf("\n");
    exit(-2);
}

void op_add(uint16_t instr)
{
    uint16_t r0 = (instr >> 9) & 0x7; // Dest register
    uint16_t r1 = (instr >> 6) & 0x7; // source register

    uint16_t immflag = (instr >> 5) & 0x1;
    if (immflag)
    {
        uint16_t imm5 = instr & 0x1F;
        imm5 = sign_extend(imm5, 5);
        reg[r0] = reg[r1] + reg[imm5];
    }
    else
    {
        uint16_t r2 = instr & 0x7;
        reg[r0] = reg[r1] + reg[r2];
    }
}
void op_and(uint16_t instr)
{
    uint16_t r0 = (instr >> 9) & 0x7;
    uint16_t r1 = (instr >> 6) & 0x7;

    uint16_t immflag = (instr >> 5) & 0x1;

    if (immflag)
    {
        uint16_t imm5 = instr & 0x1F;
        imm5 = sign_extend(imm5, 5);
        reg[r0] = reg[r1] & imm5;
    }
    else
    {
        uint16_t r2 = instr & 0x7;
        reg[r0] = reg[r1] & reg[r2];
    }
    update_flags(r0);
    // @{AND}
}

void op_br(uint16_t instr)
{
    uint16_t pc_offset = sign_extend(instr & 0x1FF, 9);
    uint16_t cond_flag = (instr >> 9) & 0x7;
    if (cond_flag & reg[R_COND])
    {
        reg[R_PC] += pc_offset;
    }

    // @{BR}
}
void op_not(uint16_t instr)
{
    uint16_t r0 = (instr >> 9) & 0x7; // dest
    uint16_t r1 = (instr >> 6) & 0x7; // src
    reg[r0] = ~reg[r1];
    update_flags(r0);
    // @{NOT}
}
void op_jmp(uint16_t instr)
{
    uint16_t baseR = (instr >> 6) & 0x7;
    reg[R_PC] = reg[baseR];
    update_flags(baseR);
    // @{JMP}
}
void op_jsr(uint16_t instr)
{
    reg[R_R7] = reg[R_PC];
    uint16_t long_flag = (instr >> 11) & 0x1;
    if (long_flag)
    {
        uint16_t pc_offset = (instr & 0x7FF);
        pc_offset = sign_extend(pc_offset, 11);
        reg[R_PC] += pc_offset;
    }
    else
    {
        uint16_t baseR = (instr >> 6) & 0x7;
        reg[R_PC] = reg[baseR];
    }

    // @{JSR}
}
void op_ld(uint16_t instr)
{
    uint16_t r0 = (instr >> 9) & 0x7;
    uint16_t pc_offset = instr & 0x1FF;
    pc_offset = sign_extend(pc_offset, 9);
    reg[r0] = mem_read(reg[R_PC] + pc_offset);
    update_flags(r0);
    // @{LD}
}
void op_ldi(uint16_t instr)
{
    uint16_t r0 = (instr >> 9) & 0x7;
    uint16_t pc_offset = instr & 0x1FF;
    pc_offset = sign_extend(pc_offset, 9);

    reg[r0] = mem_read(mem_read(reg[R_PC] + pc_offset));
    update_flags(r0);
}
void op_ldr(uint16_t instr)
{
    uint16_t r0 = (instr >> 9) & 0x7;
    uint16_t baseR = (instr >> 6) & 0x7;
    uint16_t offset6 = instr & 0x3F;
    offset6 = sign_extend(offset6, 6);
    reg[r0] = mem_read(baseR + offset6);
    update_flags(r0);
    // @{LDR}
}
void op_lea(uint16_t instr)
{
    uint16_t r0 = (instr >> 9) & 0x7;
    uint16_t offset = (instr & 0x1FF);
    offset = sign_extend(offset, 9);
    reg[r0] = reg[R_PC] + offset;
    update_flags(r0);
    // @{LEA}
}
void op_st(uint16_t instr)
{
    // @{ST}
    uint16_t r0 = (instr >> 9) & 0x7;
    uint16_t offset = sign_extend(instr & 0x1FF, 9);
    mem_write(reg[R_PC] + offset, reg[r0]);
}
void op_sti(uint16_t instr)
{
    // @{STI}
    uint16_t r0 = (instr >> 9) & 0x7;
    uint16_t offset = sign_extend(instr & 0x1FF, 9);
    mem_write(mem_read(reg[R_PC] + offset), reg[r0]);
}
void op_str(uint16_t instr)
{
    // @{STR}
    uint16_t r0 = (instr >> 9) & 0x7;
    uint16_t baseR = (instr >> 6) & 0x7;
    uint16_t offset = sign_extend(instr & 0x3F, 6);
    mem_write((reg[baseR] + offset), reg[r0]);
}
void trap_getc()
{
    reg[R_R0] = (uint16_t)getchar();
    update_flags(R_R0);
}
void trap_out()
{
    putc((char)reg[R_R0], stdout);
    fflush(stdout);
}
void trap_puts()
{
    uint16_t *c = memory + reg[R_R0];
    while (*c)
    {
        putc((char)*c, stdout);
        ++c;
    }
    fflush(stdout);
}
void trap_in()
{
    printf("Enter a character: ");
    char cTrap = getchar();
    putc(cTrap, stdout);
    fflush(stdout);
    reg[R_R0] = (uint16_t)cTrap;
    update_flags(R_R0);
}
void trap_putsp()
{
    // one char per byte so 2 byte contains 2 char but in little endian format
    uint16_t *ch = memory + reg[R_R0];
    while (*ch)
    {
        char char1 = (*ch) & 0xFF;
        putc(char1, stdout);
        char char2 = (*ch) >> 8;
        if (char2)
        {
            putc(char2, stdout);
        }
        ++ch;
    }
    fflush(stdout);
}

int main(int argc, const char *argv[])
{
    signal(SIGINT, handle_interrupt);
    disable_input_buffering();
    if (argc < 2)
    {
        /* show usage string */
        printf("lc3 [image-file1] ...\n");
        exit(2);
    }

    for (int j = 1; j < argc; ++j)
    {
        if (!read_image(argv[j]))
        {
            printf("failed to load image: %s\n", argv[j]);
            exit(1);
        }
    }
    // @{Setup}


    int running = 1;
    while (running)
    {
        /* FETCH */
        uint16_t instr = mem_read(reg[R_PC]++);
        uint16_t op = instr >> 12;

        switch (op)
        {
        case OP_ADD:
            // @{ADD}
            op_add(instr);
            break;
        case OP_AND:
            op_and(instr);
            break;
        case OP_NOT:
            op_not(instr);
            break;
        case OP_BR:
            op_br(instr);
            break;
        case OP_JMP:
            op_jmp(instr);
            break;
        case OP_JSR:
            op_jsr(instr);
            break;
        case OP_LD:
            op_ld(instr);
            break;
        case OP_LDI:
            // @{LDI}
            op_ldi(instr);
            break;
        case OP_LDR:
            op_ldr(instr);
            break;
        case OP_LEA:
            op_lea(instr);
            break;
        case OP_ST:
            op_st(instr);
            break;
        case OP_STI:
            op_sti(instr);
            break;
        case OP_STR:
            op_str(instr);
            break;
        case OP_TRAP:
            // @{TRAP}
            reg[R_R7] = reg[R_PC];

            switch (instr & 0xFF)
            {
            case TRAP_GETC:
                // @{TRAP GETC}
                trap_getc();
                break;
            case TRAP_OUT:
                // @{TRAP OUT}
                trap_out();
                break;
            case TRAP_PUTS:
                // @{TRAP PUTS}
                trap_puts();
                break;
            case TRAP_IN:
                // @{TRAP IN}
                trap_in();
                break;
            case TRAP_PUTSP:
                // @{TRAP PUTSP}
                trap_putsp();
                break;
            case TRAP_HALT:
                // @{TRAP HALT}
                puts("HALT");
                fflush(stdout);
                running = 0;
                break;
            }
            break;
        case OP_RES:
        case OP_RTI:
        default:
            // @{BAD OPCODE}
            break;
        }
    }
    // @{Shutdown}
    restore_input_buffering();
}
